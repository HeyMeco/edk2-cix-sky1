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
CONDITIONS = {"eq", "ne", "cs", "hs", "cc", "lo", "mi", "pl", "vs", "vc", "hi", "ls", "ge", "lt", "gt", "le"}


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
    """DDR function starts, variables -> offsets of their pointer fields,
    the compile unit of each (start address -> CU name), and the starts of
    the functions that are not static."""
    dw = img.elf.get_dwarf_info()
    funcs, variables, unit, external = set(), {}, {}, set()
    for cu in dw.iter_CUs():
        top = cu.get_top_DIE()
        comp = top.attributes.get("DW_AT_comp_dir")
        if not comp or DDR_DIR not in comp.value:
            continue
        name = top.attributes["DW_AT_name"].value.decode()
        for die in cu.iter_DIEs():
            if die.tag == "DW_TAG_subprogram" and "DW_AT_low_pc" in die.attributes:
                funcs.add(die.attributes["DW_AT_low_pc"].value & ~1)
                unit[die.attributes["DW_AT_low_pc"].value & ~1] = name
                if "DW_AT_external" in die.attributes:
                    external.add(die.attributes["DW_AT_low_pc"].value & ~1)
            elif die.tag == "DW_TAG_variable" and "DW_AT_location" in die.attributes:
                loc = die.attributes["DW_AT_location"].value
                if isinstance(loc, list) and len(loc) == 5 and loc[0] == 3:   # DW_OP_addr
                    addr = int.from_bytes(bytes(loc[1:5]), "little")
                    t = die.get_DIE_from_attribute("DW_AT_type") if "DW_AT_type" in die.attributes else None
                    variables[addr] = pointer_offsets(t) if t is not None else []
                    unit[addr] = name
    return funcs, variables, unit, external


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
    def __init__(self, img, original=False, skip=(), needed=()):
        self.img = img
        self.original = original
        self.skip = set() if original else set(skip)
        self.needed = set(needed)
        dfuncs, self.dvars, self.unit, self.external = ddr_units(img)
        self.starts = sorted(dfuncs)
        self.code_lo = self.starts[0]
        self.code_hi = max(s + img.funcs[s][1] for s in self.starts)
        for addr, (name, _) in img.funcs.items():
            if self.code_lo <= addr < self.code_hi and addr not in dfuncs:
                sys.exit(f"{name} at {addr:#x} sits inside the DDR code range")
        # a function's bytes run up to the next one (padding after its symbol size)
        self.ends = dict(zip(self.starts, self.starts[1:] + [self.code_hi]))
        self.externals = defaultdict(set)
        self.data_objects = {}       # start -> (name, size, section)
        self.strings = {}            # address -> bytes
        self.anon = {}               # start -> size

    def is_code(self, addr):
        return self.code_lo <= addr < self.code_hi

    def func_of(self, addr):
        return self.starts[bisect_right(self.starts, addr) - 1]

    def fname(self, start):
        return self.img.funcs[start][0]

    def emitted(self, start):
        return self.fname(start) not in self.skip

    @staticmethod
    def sym(name):
        return name.replace(".", "_")

    def vsym(self, name):
        """Label of a driver variable: its CIX name (global), or for a
        function's static local (CIX symbol name.N) a local ddr_ label."""
        return f"ddr_{self.sym(name)}" if "." in name else name

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
            f = self.func_of(target)
            if target == f and thumb:
                return self.fname(f)
            if not self.emitted(f):
                raise ValueError(f"{value:#x} (from {user:#x}) points into {self.fname(f)}, which is left out")
            return f"ddr_f_{self.fname(f)}+{target - f + thumb:#x}"
        s = img.symbol_at(target)
        if s is not None:
            name, start, size, kind = s
            if kind == "f" and target != start and not pointer:
                return None                  # a constant inside a function body
            if name == "__Vectors" and not pointer:
                return None
            if kind == "o" and start in self.dvars:
                if name in self.skip and "." not in name:
                    self.externals[name].add(user)       # defined by the C sources
                else:
                    self.data_objects[start] = (name, size, img.section_of(start)[0])
                off = target - start
                return f"{self.vsym(name)}+{off:#x}"
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

    def code_refs(self, f, insns, literals):
        """DDR functions that function f calls, branches to or takes the
        address of."""
        refs, end = set(), self.ends[f]
        for pc, i in insns.items():
            if not f <= pc < end:
                continue
            m = i.mnemonic.split(".")[0]
            imm = [op.imm for op in i.operands if op.type == carm.ARM_OP_IMM]
            if (m in ("b", "bl") or (m[:1] == "b" and m[1:] in CONDITIONS)) and imm \
                    and self.is_code(imm[-1]) and not f <= imm[-1] < end:
                refs.add(self.func_of(imm[-1]))
        for lit in literals:
            if f <= lit < end:
                v = struct.unpack("<I", self.img.read(lit, 4))[0]
                if self.is_code(v & ~1) and not f <= (v & ~1) < end:
                    refs.add(self.func_of(v & ~1))
        return refs

    def drop_unused_statics(self, insns, literals):
        """A function CIX made static that no function left here, no
        variable and no C object (self.needed: their undefined symbols)
        refers to is left out too: the C port of its file has it, perhaps
        inlined."""
        refs = {f: self.code_refs(f, insns, literals) for f in self.starts}
        data_refs = set()
        for v, ptrs in self.dvars.items():
            sy = self.img.symbol_at(v)
            if self.img.section_of(v)[1] is None:              # .bss
                continue
            for off in ptrs:
                if sy and off + 4 <= sy[2]:
                    w = struct.unpack("<I", self.img.read(v + off, 4))[0]
                    if self.is_code(w & ~1):
                        data_refs.add(self.func_of(w & ~1))
        kept = set(self.kept)
        while True:
            used = set().union(*(refs[f] for f in kept)) | data_refs
            drop = {f for f in kept if f not in self.external and f not in used
                    and self.fname(f) not in self.needed}
            if not drop:
                break
            kept -= drop
        self.kept = [f for f in self.starts if f in kept]

    def emit_function(self, w, f, lit_words, insns, code_pointers):
        img = self.img
        name, size = img.funcs[f]
        if not self.original:
            w(f"\t.section .text.ddr.{name}, \"ax\", %progbits\n\t.p2align 2")
            if f & 3:
                w("\t.byte 0x00, 0xbf")                  # nop: keep the literal pools' alignment
        w(f"\t.global {name}\n\t.type {name}, %function\n\t.size {name}, {size}")
        w(f"ddr_f_{name}:\n\t.thumb_func\n{name}:")
        pc, end = f, self.ends[f]
        while pc < end:
            if pc in lit_words:
                value = struct.unpack("<I", img.read(pc, 4))[0]
                expr = self.describe(value, pc, pointer=pc in code_pointers)
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
            branch = m in ("b", "bl", "cbz", "cbnz") or (m[:1] == "b" and m[1:] in CONDITIONS)
            if branch and imm and not (f <= imm[-1] < end):
                tgt = imm[-1]
                if m in ("cbz", "cbnz") or i.size != 4:
                    raise ValueError(f"short branch out of {name} at {pc:#x}")
                if self.is_code(tgt):
                    if self.func_of(tgt) != tgt:
                        raise ValueError(f"branch from {pc:#x} into the middle of {self.fname(self.func_of(tgt))}")
                    tname = self.fname(tgt)
                else:
                    t = img.funcs.get(tgt & ~1)
                    if t is None:
                        raise ValueError(f"call from {pc:#x} to {tgt:#x}, not a function start")
                    if m != "bl" and m != "b":
                        raise ValueError(f"conditional branch out of the driver at {pc:#x}")
                    tname = t[0]
                    self.externals[tname].add(pc)
                w(f"\tbl {tname}" if m == "bl" else f"\t{m}.w {tname}")
                pc += 4
                continue
            if m == "adr" and imm and not (f <= ((pc + 4) & ~3) + imm[-1] < end):
                raise ValueError(f"adr out of {name} at {pc:#x}")
            w("\t.byte " + ", ".join(f"{b:#04x}" for b in img.read(pc, i.size)))
            pc += i.size

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
        self.kept = [s for s in self.starts if self.original or self.emitted(s)]
        if not self.original:
            self.drop_unused_statics(insns, literals)

        out = []
        w = out.append
        w("/* Generated by scripts/open/se_ddr_island.py from CIX's se_fw.elf; do not edit. */")
        w("\t.syntax unified\n\t.thumb\n\t.cpu cortex-m33")
        if self.original:
            w("\t.section .text.ddr_island, \"ax\", %progbits\n\t.p2align 2")
        for s in self.kept:
            self.emit_function(w, s, lit_words, insns, dis.code_pointers)

        if self.original:
            for v in sorted(self.dvars):
                sy = img.symbol_at(v)
                if sy and sy[1] == v:
                    self.data_objects.setdefault(v, (sy[0], sy[2], img.section_of(v)[0]))
            for start, (name, _, _) in sorted(self.data_objects.items()):
                w(f"\t.set {self.vsym(name)}, {start:#x}")
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

        # The driver's variables, a block per compile unit and section with
        # CIX's layout: its code reaches a unit's variables as offsets from
        # one of them (section anchors).  So a variable can only move to the
        # C sources once no function of its unit is left here.
        left_units = {self.unit[f] for f in self.kept}
        for v in sorted(self.dvars):
            sy = img.symbol_at(v)
            if not (sy and sy[1] == v):
                continue
            if sy[0] in self.skip and "." not in sy[0]:
                if self.unit[v] in left_units:
                    raise SystemExit(f"{sy[0]} is defined by the C sources, but functions of "
                                     f"{self.unit[v]} that reach it through CIX's layout are not "
                                     f"(declare it extern until the whole file is ported)")
                continue
            if "." not in sy[0] or self.unit[v] in left_units:
                self.data_objects.setdefault(v, (sy[0], sy[2], img.section_of(v)[0]))
        done = set()
        while set(self.data_objects) - done:
            done = set(self.data_objects)
            for start in sorted(done):
                name, size, sec = self.data_objects[start]
                if sec in (".data", ".rodata") or sec.startswith(".rodata"):
                    self.emit_data(lambda _: None, start, img.read(start, size), set(self.dvars.get(start) or []))
        blocks = defaultdict(list)
        for start, (name, size, sec) in sorted(self.data_objects.items()):
            blocks[(self.unit.get(start, "?"), sec)].append((start, name, size))
        for (unit, sec), objs in sorted(blocks.items()):
            kind = "bss" if sec == ".bss" else ("data" if sec == ".data" else "rodata")
            flags = {"bss": '"aw", %nobits', "data": '"aw", %progbits', "rodata": '"a", %progbits'}[kind]
            base = objs[0][0]
            align = min(3, (base & -base).bit_length() - 1)
            w(f"\t.section .{kind}.ddr.{self.sym(unit)}, {flags}\n\t.p2align {align}")
            cursor = base
            for start, name, size in objs:
                if start < cursor:
                    raise ValueError(f"{name} overlaps the variable before it")
                if start > cursor:
                    w(f"\t.space {start - cursor}")
                label = self.vsym(name)
                if "." not in name:
                    w(f"\t.global {label}\n\t.type {label}, %object\n\t.size {label}, {size}")
                w(f"{label}:")
                if kind == "bss":
                    w(f"\t.space {size}")
                else:
                    self.emit_data(w, start, img.read(start, size), set(self.dvars.get(start) or []))
                cursor = start + size
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


def defined_symbols(paths, undefined=False):
    """Functions and variables the object files define (any binding), or
    with undefined, the symbols they use from elsewhere."""
    names = set()
    for p in paths:
        with open(p, "rb") as f:
            for s in ELFFile(f).iter_sections():
                if isinstance(s, SymbolTableSection):
                    for sym in s.iter_symbols():
                        if undefined:
                            if sym["st_shndx"] == "SHN_UNDEF" and sym.name:
                                names.add(sym.name)
                        elif sym["st_info"]["type"] in ("STT_FUNC", "STT_OBJECT") and \
                                sym["st_shndx"] != "SHN_UNDEF" and sym.name:
                            names.add(sym.name)
    return names


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
    ap.add_argument("--skip", action="append", default=[], metavar="NAME",
                    help="leave out the function or variable NAME (the open sources define it)")
    ap.add_argument("--skip-obj", nargs="*", default=[], metavar="OBJ",
                    help="leave out every function and variable these object files define")
    ap.add_argument("--list", metavar="FILE", help="write the functions left in the island, one per line")
    a = ap.parse_args()
    img = Image(a.elf)
    skip = set(a.skip) | defined_symbols(a.skip_obj)
    island = Island(img, a.original_layout, skip, defined_symbols(a.skip_obj, undefined=True))
    text = island.emit()
    for name in a.data:
        if name not in skip:
            text += emit_data_object(img, name)
    with open(a.out, "w") as f:
        f.write(text)
    kept_bytes = sum(island.ends[s] - s for s in island.kept)
    if a.list:
        with open(a.list, "w") as f:
            f.writelines(f"{img.funcs[s][0]} {island.ends[s] - s}\n" for s in island.kept)
    if a.report:
        n_insn, n_lit, n_tab = island.counts
        print(f"code {island.code_lo:#x}-{island.code_hi:#x} ({island.code_hi - island.code_lo} bytes), "
              f"{len(island.starts)} functions, {n_insn} instructions, {n_lit} literal words, {n_tab} branch tables")
        print(f"island: {len(island.kept)} of {len(island.starts)} functions, {kept_bytes} code bytes; "
              f"{len(island.data_objects)} variables, {len(island.anon)} anonymous constants, "
              f"{len(island.strings)} strings; data: {', '.join(n for n in a.data if n not in skip) or 'none'}")
        print("external symbols:")
        for k, v in sorted(island.externals.items()):
            print(f"  {k} ({len(v)})")


if __name__ == "__main__":
    main()
