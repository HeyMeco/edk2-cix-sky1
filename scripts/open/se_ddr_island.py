#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Relinkable DDR driver from CIX's SE firmware debug ELF.

  se_ddr_island.py se_fw.elf OUT.S [--report] [--original-layout] [--data NAME]...

The DDR driver (CIX security/firmware/driver/ddr, identified through the
DWARF compile units) is emitted as GNU assembly:

* its code bytes unchanged, found by recursive descent from each function
  entry (Thumb-2, Armv8-M Mainline), except
* calls and tail calls to functions outside the driver, re-emitted as
  `bl`/`b.w <name>`,
* literal-pool words that hold an address, re-emitted as
  `.word <symbol>+<offset>`: the driver's functions, variables, strings and
  anonymous constants, or a symbol outside it.  A literal that points into
  the body of a function outside the driver is a constant that happens to
  look like an SRAM address and stays as it is;
* the driver's variables (static locals included), with the words their
  DWARF types declare as pointers relocated the same way, and the strings
  and anonymous constants its code loads.

The result links into the open SE firmware like any object; the symbols it
needs from outside are listed by --report and must be provided there.

--original-layout emits no data: every variable, string, constant and
outside symbol is set to its address in the ELF instead, so that linking
the code at its original address must give back CIX's bytes exactly (the
check that the transcription is right).
"""

import argparse
import struct
import sys
from bisect import bisect_right
from collections import defaultdict

import capstone
from capstone import arm as carm
from elftools.elf.elffile import ELFFile
from elftools.elf.sections import SymbolTableSection

DDR_DIR = b"/driver/ddr"


class Image:
    def __init__(self, path):
        self.f = open(path, "rb")
        self.elf = ELFFile(self.f)
        self.sections = []
        for s in self.elf.iter_sections():
            if s["sh_flags"] & 2 and s["sh_size"]:   # SHF_ALLOC
                data = s.data() if s["sh_type"] != "SHT_NOBITS" else None
                self.sections.append((s["sh_addr"], s["sh_size"], s.name, data))
        self.funcs, self.objects = {}, {}
        for s in self.elf.iter_sections():
            if isinstance(s, SymbolTableSection):
                for sym in s.iter_symbols():
                    t, v, n = sym["st_info"]["type"], sym["st_value"], sym["st_size"]
                    if t == "STT_FUNC" and n:
                        self.funcs[v & ~1] = (sym.name, n)
                    elif t == "STT_OBJECT" and n:
                        self.objects[v] = (sym.name, n)
        self.lo = min(a for a, _, _, _ in self.sections)
        self.hi = max(a + n for a, n, _, _ in self.sections)
        syms = [(a, n, name, "f") for a, (name, n) in self.funcs.items()]
        syms += [(a, n, name, "o") for a, (name, n) in self.objects.items()]
        syms.sort()
        self.sym_addrs = [a for a, _, _, _ in syms]
        self.syms = syms

    def section_of(self, addr):
        for a, n, name, data in self.sections:
            if a <= addr < a + n:
                return name, data, a, n
        return None, None, None, None

    def read(self, addr, size):
        _, data, base, _ = self.section_of(addr)
        if data is None:
            raise ValueError(f"{addr:#x} not in a loaded section")
        return data[addr - base:addr - base + size]

    def symbol_at(self, addr):
        """(name, start, size, kind) of the symbol containing addr."""
        i = bisect_right(self.sym_addrs, addr) - 1
        while i >= 0:
            a, n, name, kind = self.syms[i]
            if a <= addr < a + n:
                return name, a, n, kind
            if a + 0x10000 < addr:
                break
            i -= 1
        return None


def strip(t):
    while t is not None and t.tag in ("DW_TAG_typedef", "DW_TAG_const_type", "DW_TAG_volatile_type"):
        t = t.get_DIE_from_attribute("DW_AT_type") if "DW_AT_type" in t.attributes else None
    return t


def type_size(t):
    t = strip(t)
    if t is None:
        return 0
    if "DW_AT_byte_size" in t.attributes:
        return t.attributes["DW_AT_byte_size"].value
    if t.tag == "DW_TAG_array_type":
        n = 1
        for sub in t.iter_children():
            if sub.tag == "DW_TAG_subrange_type":
                if "DW_AT_count" in sub.attributes:
                    n *= sub.attributes["DW_AT_count"].value
                elif "DW_AT_upper_bound" in sub.attributes:
                    n *= sub.attributes["DW_AT_upper_bound"].value + 1
        return n * type_size(t.get_DIE_from_attribute("DW_AT_type"))
    return 0


def pointer_offsets(t, base=0, depth=0):
    """Byte offsets of the pointers inside an object of DWARF type t."""
    t = strip(t)
    if t is None or depth > 16:
        return []
    if t.tag in ("DW_TAG_pointer_type", "DW_TAG_subroutine_type"):
        return [base]
    if t.tag == "DW_TAG_structure_type":
        out = []
        for m in t.iter_children():
            if m.tag == "DW_TAG_member" and "DW_AT_data_member_location" in m.attributes:
                off = m.attributes["DW_AT_data_member_location"].value
                if isinstance(off, int):
                    out += pointer_offsets(m.get_DIE_from_attribute("DW_AT_type"), base + off, depth + 1)
        return out
    if t.tag == "DW_TAG_array_type":
        elem = t.get_DIE_from_attribute("DW_AT_type")
        size = type_size(elem)
        inner = pointer_offsets(elem, 0, depth + 1)
        if not inner or not size:
            return []
        n = type_size(t) // size
        return [base + k * size + o for k in range(n) for o in inner]
    return []


def ddr_units(img):
    """DDR function starts and variables -> offsets of their pointer fields."""
    dw = img.elf.get_dwarf_info()
    funcs, variables = set(), {}
    for cu in dw.iter_CUs():
        comp = cu.get_top_DIE().attributes.get("DW_AT_comp_dir")
        if not comp or DDR_DIR not in comp.value:
            continue
        for die in cu.iter_DIEs():
            if die.tag == "DW_TAG_subprogram" and "DW_AT_low_pc" in die.attributes:
                funcs.add(die.attributes["DW_AT_low_pc"].value & ~1)
            elif die.tag == "DW_TAG_variable" and "DW_AT_location" in die.attributes:
                loc = die.attributes["DW_AT_location"].value
                if isinstance(loc, list) and len(loc) == 5 and loc[0] == 3:   # DW_OP_addr
                    addr = int.from_bytes(bytes(loc[1:5]), "little")
                    t = die.get_DIE_from_attribute("DW_AT_type") if "DW_AT_type" in die.attributes else None
                    variables[addr] = pointer_offsets(t) if t is not None else []
    return funcs, variables


class Disasm:
    """Recursive descent over one function: decoded instructions and the
    literal-pool words its PC-relative loads use, following branches inside
    the function and TBB/TBH tables."""

    def __init__(self, img):
        self.img = img
        self.md = capstone.Cs(capstone.CS_ARCH_ARM,
                              capstone.CS_MODE_THUMB | capstone.CS_MODE_MCLASS | capstone.CS_MODE_V8)
        self.md.detail = True
        self.code_pointers = set()

    def insn(self, addr):
        return next(self.md.disasm(self.img.read(addr, 4), addr), None)

    def function(self, start, size):
        end = start + size
        todo, seen, literals, tables = [start], {}, {}, []
        while todo:
            pc = todo.pop()
            while start <= pc < end and pc not in seen:
                i = self.insn(pc)
                if i is None:
                    raise ValueError(f"cannot decode at {pc:#x}")
                seen[pc] = i
                m, ops = i.mnemonic.split(".")[0], i.operands
                for op in ops:
                    if op.type == carm.ARM_OP_MEM and op.mem.base == carm.ARM_REG_PC:
                        lit = ((pc + 4) & ~3) + op.mem.disp
                        literals[lit] = max(literals.get(lit, 0), 8 if m in ("ldrd", "vldr") else 4)
                if m == "ldr" and i.op_str.startswith("pc, [r") and "lsl #2" in i.op_str:
                    # adr rT, table ; ldr.w pc, [rT, rI, lsl #2]: table of code addresses
                    prev = seen.get(pc - 2) or seen.get(pc - 4)
                    imm = [op.imm for op in prev.operands if op.type == carm.ARM_OP_IMM] if prev else []
                    if prev is None or prev.mnemonic.split(".")[0] != "adr" or not imm:
                        raise ValueError(f"unknown indirect jump at {pc:#x}")
                    table = ((prev.address + 4) & ~3) + imm[-1]
                    n = self.table_entries(seen, prev.address)
                    for k in range(n):
                        literals[table + 4 * k] = 4
                        self.code_pointers.add(table + 4 * k)
                        todo.append(struct.unpack("<I", self.img.read(table + 4 * k, 4))[0] & ~1)
                    break
                if m in ("tbb", "tbh"):
                    n, width, base = self.table_entries(seen, pc), (1 if m == "tbb" else 2), pc + 4
                    tables.append((base, n * width))
                    for k in range(n):
                        todo.append(base + 2 * int.from_bytes(self.img.read(base + k * width, width), "little"))
                    break
                if m in ("b", "bl", "cbz", "cbnz") or (m.startswith("b") and len(m) == 3 and m != "bic"):
                    imm = [op.imm for op in ops if op.type == carm.ARM_OP_IMM]
                    if imm and start <= imm[-1] < end:
                        todo.append(imm[-1])
                if (m == "b" and i.cc in (carm.ARM_CC_AL, carm.ARM_CC_INVALID) and not self.in_it(seen, pc)) \
                        or (m == "bx" and "lr" in i.op_str) \
                        or (m in ("pop", "ldm") and "pc" in i.op_str) \
                        or (m == "ldr" and i.op_str.startswith("pc")) or m == "udf":
                    break
                pc += i.size
        return seen, literals, tables

    @staticmethod
    def in_it(seen, pc):
        for back in (2, 4, 6, 8):
            i = seen.get(pc - back)
            if i is not None and i.mnemonic.startswith("it"):
                return len(i.mnemonic) - 1 >= back // 2
        return False

    @staticmethod
    def table_entries(seen, pc):
        # compiler pattern: cmp rN, #K ; bhi default ; (adr ;) table jump
        for back in range(2, 20, 2):
            i = seen.get(pc - back)
            if i is not None and i.mnemonic.split(".")[0] == "cmp":
                imm = [op.imm for op in i.operands if op.type == carm.ARM_OP_IMM]
                if imm:
                    return imm[0] + 1
        raise ValueError(f"cannot size the branch table at {pc:#x}")


class Island:
    def __init__(self, img, original=False):
        self.img = img
        self.original = original
        dfuncs, self.dvars = ddr_units(img)
        self.starts = sorted(dfuncs)
        self.code_lo = self.starts[0]
        self.code_hi = max(s + img.funcs[s][1] for s in self.starts)
        for addr, (name, _) in img.funcs.items():
            if self.code_lo <= addr < self.code_hi and addr not in dfuncs:
                sys.exit(f"{name} at {addr:#x} sits inside the DDR code range")
        self.externals = defaultdict(set)
        self.data_objects = {}       # start -> (name, size, section)
        self.strings = {}            # address -> bytes
        self.anon = {}               # start -> size

    def is_code(self, addr):
        return self.code_lo <= addr < self.code_hi

    @staticmethod
    def sym(name):
        return name.replace(".", "_")

    def describe(self, value, user, pointer=True):
        """Assembly expression for an address value, or None for a constant.
        pointer: the word is declared a pointer (DWARF)."""
        img = self.img
        if not (img.lo <= value < img.hi):
            return None
        fn = img.symbol_at(value & ~1)
        thumb = 1 if (value & 1 and fn is not None and fn[3] == "f") else 0
        target = value - thumb
        if self.is_code(target):
            return f"ddr_text+{target - self.code_lo + thumb:#x}"
        s = img.symbol_at(target)
        if s is not None:
            name, start, size, kind = s
            if kind == "f" and target != start and not pointer:
                return None                  # a constant inside a function body
            if name == "__Vectors" and not pointer:
                return None
            if kind == "o" and start in self.dvars:
                self.data_objects[start] = (name, size, img.section_of(start)[0])
                return f"ddr_{self.sym(name)}+{target - start:#x}"
            self.externals[name].add(user)
            off = target - start + thumb
            return f"{self.sym(name)}+{off:#x}" if off else self.sym(name)
        sec, _, base, length = img.section_of(target)
        if sec and sec.startswith(".rodata.str"):
            data = img.read(target, min(512, base + length - target))
            self.strings[target] = data[:data.index(b"\0") + 1]
            return f"ddr_str_{target:x}"
        if sec and sec.startswith(".rodata"):
            for start, size in self.anon.items():
                if start <= target < start + size:
                    return f"ddr_anon_{start:x}+{target - start:#x}"
            i = bisect_right(img.sym_addrs, target)
            nxt = min(img.sym_addrs[i] if i < len(img.sym_addrs) else base + length, base + length)
            self.anon[target] = nxt - target
            return f"ddr_anon_{target:x}"
        if not pointer:
            return None
        raise ValueError(f"address {value:#x} ({sec}) referenced from {user:#x} has no symbol")

    def emit(self):
        img = self.img
        dis = Disasm(img)
        insns, literals, tables = {}, {}, []
        for s in self.starts:
            seen, lits, tabs = dis.function(s, img.funcs[s][1])
            insns.update(seen)
            literals.update(lits)
            tables.extend(tabs)
        lit_words = {lit + k for lit, width in literals.items() for k in range(0, width, 4)}
        self.counts = (len(insns), len(lit_words), len(tables))

        out = []
        w = out.append
        w("/* Generated by scripts/open/se_ddr_island.py from CIX's se_fw.elf; do not edit. */")
        w("\t.syntax unified\n\t.thumb\n\t.cpu cortex-m33")
        w("\t.section .text.ddr_island, \"ax\", %progbits\n\t.p2align 2")
        w("\t.global ddr_text\nddr_text:")
        for s in self.starts:
            name = img.funcs[s][0]
            w(f"\t.global {name}\n\t.type {name}, %function\n\t.set {name}, ddr_text+{s - self.code_lo + 1:#x}")
        pc = self.code_lo
        while pc < self.code_hi:
            if pc in lit_words:
                value = struct.unpack("<I", img.read(pc, 4))[0]
                expr = self.describe(value, pc, pointer=pc in dis.code_pointers)
                w(f"\t.word {expr}" if expr else f"\t.word {value:#010x}")
                pc += 4
                continue
            i = insns.get(pc)
            if i is None:
                w(f"\t.byte {img.read(pc, 1)[0]:#04x}")
                pc += 1
                continue
            m = i.mnemonic.split(".")[0]
            imm = [op.imm for op in i.operands if op.type == carm.ARM_OP_IMM]
            if m in ("bl", "b") and imm and not self.is_code(imm[-1]):
                if i.size != 4:
                    raise ValueError(f"short branch out of the driver at {pc:#x}")
                tgt = img.funcs.get(imm[-1] & ~1)
                if tgt is None:
                    raise ValueError(f"call from {pc:#x} to {imm[-1]:#x}, not a function start")
                if m == "b" and i.cc not in (carm.ARM_CC_AL, carm.ARM_CC_INVALID):
                    raise ValueError(f"conditional branch out of the driver at {pc:#x}")
                self.externals[tgt[0]].add(pc)
                w(f"\t{'bl' if m == 'bl' else 'b.w'} {tgt[0]}")
                pc += 4
                continue
            if m == "adr" and imm and not self.is_code(((pc + 4) & ~3) + imm[-1]):
                raise ValueError(f"adr out of the driver at {pc:#x}")
            w("\t.byte " + ", ".join(f"{b:#04x}" for b in img.read(pc, i.size)))
            pc += i.size

        for v in sorted(self.dvars):
            s = img.symbol_at(v)
            if s and s[1] == v:
                self.data_objects.setdefault(v, (s[0], s[2], img.section_of(v)[0]))
        by_section = defaultdict(list)
        for start, (name, size, sec) in sorted(self.data_objects.items()):
            by_section[sec].append((start, name, size))
        if self.original:
            for start, (name, _, _) in sorted(self.data_objects.items()):
                w(f"\t.set ddr_{self.sym(name)}, {start:#x}")
            for start in sorted(self.anon):
                w(f"\t.set ddr_anon_{start:x}, {start:#x}")
            for addr in sorted(self.strings):
                w(f"\t.set ddr_str_{addr:x}, {addr:#x}")
            for name in sorted(self.externals):
                a = next((a for a, (n, _) in img.funcs.items() if n == name), None)
                if a is not None:
                    w(f"\t.set {name}, {a | 1:#x}")
                else:
                    a = next(a for a, (n, _) in img.objects.items() if n == name)
                    w(f"\t.set {self.sym(name)}, {a:#x}")
            return "\n".join(out) + "\n"
        for sec, objs in sorted(by_section.items()):
            kind = "bss" if sec == ".bss" else ("data" if sec == ".data" else "rodata")
            flags = {"bss": '"aw", %nobits', "data": '"aw", %progbits', "rodata": '"a", %progbits'}[kind]
            w(f"\t.section .{kind}.ddr_island, {flags}")
            for start, name, size in objs:
                w(f"\t.p2align 2\nddr_{self.sym(name)}:")
                if kind == "bss":
                    w(f"\t.space {size}")
                    continue
                self.emit_data(w, start, img.read(start, size), set(self.dvars.get(start) or []))
        w("\t.section .rodata.ddr_island_anon, \"a\", %progbits")
        done = set()
        while set(self.anon) - done:
            for start in sorted(set(self.anon) - done):
                done.add(start)
                w(f"\t.p2align 2\nddr_anon_{start:x}:")
                self.emit_data(w, start, img.read(start, self.anon[start]), set())
        w("\t.section .rodata.ddr_island_str, \"a\", %progbits")
        for addr, data in sorted(self.strings.items()):
            w(f"\t.p2align 2\nddr_str_{addr:x}:\n\t.byte " + ", ".join(f"{b:#04x}" for b in data))
        return "\n".join(out) + "\n"

    def emit_data(self, w, start, data, ptrs):
        k = 0
        while k < len(data):
            if k in ptrs and k + 4 <= len(data):
                value = struct.unpack_from("<I", data, k)[0]
                expr = self.describe(value, start + k) if value else None
                if expr:
                    w(f"\t.word {expr}")
                    k += 4
                    continue
            w(f"\t.byte {data[k]:#04x}")
            k += 1


def emit_data_object(img, name):
    found = [(addr, size) for addr, (n, size) in img.objects.items() if n == name]
    if not found:
        raise SystemExit(f"no data object {name}")
    addr, size = found[0]
    data = img.read(addr, size)
    out = [f"\t.section .rodata.cix.{name}, \"a\", %progbits", "\t.p2align 2",
           f"\t.global {name}", f"\t.type {name}, %object", f"\t.size {name}, {size}", f"{name}:"]
    for i in range(0, size, 16):
        out.append("\t.byte " + ", ".join(f"{b:#04x}" for b in data[i:i + 16]))
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("elf")
    ap.add_argument("out")
    ap.add_argument("--report", action="store_true")
    ap.add_argument("--original-layout", action="store_true")
    ap.add_argument("--data", action="append", default=[], metavar="NAME",
                    help="also emit CIX's data object NAME as it is (no pointers in it)")
    a = ap.parse_args()
    img = Image(a.elf)
    island = Island(img, a.original_layout)
    text = island.emit()
    for name in a.data:
        text += emit_data_object(img, name)
    with open(a.out, "w") as f:
        f.write(text)
    if a.report:
        n_insn, n_lit, n_tab = island.counts
        print(f"code {island.code_lo:#x}-{island.code_hi:#x} ({island.code_hi - island.code_lo} bytes), "
              f"{len(island.starts)} functions, {n_insn} instructions, {n_lit} literal words, {n_tab} branch tables")
        print(f"{len(island.data_objects)} variables, {len(island.anon)} anonymous constants, "
              f"{len(island.strings)} strings")
        print("external symbols:")
        for k, v in sorted(island.externals.items()):
            print(f"  {k} ({len(v)})")


if __name__ == "__main__":
    main()
