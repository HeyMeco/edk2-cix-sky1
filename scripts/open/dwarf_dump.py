#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Print a global variable of an ELF as a C initializer, using its DWARF.

  dwarf_dump.py ELF NAME|0xADDR [--type T] [--count N] [--follow N]
                [--ram BASE:FILE] [--types]

The value comes from the ELF's loaded sections (initialised data; .bss
reads as zeros), or from a memory image (--ram, e.g. the PM's RAM saved
by pm_emu after boot, for tables built at run time).  An address needs
--type (a struct, union, enum or typedef name, "struct x" or "x"); --type
also overrides a variable's type (for void pointers' targets).  Pointers print as the symbol they point to (plus an
offset); with --follow N, pointers to data objects are followed N levels
deep and the object they point to is printed after the variable (an array
when the symbol is larger than one element).  --types prints the type
declaration instead.  Written for CIX's CSU_PM image (configuration
tables of its SCP-firmware modules) but nothing in it is specific to that.
"""

import argparse
import struct
import sys

from elftools.elf.elffile import ELFFile


def die_name(d):
    a = d.attributes.get("DW_AT_name")
    return a.value.decode() if a else None


class Dwarf:
    def __init__(self, path):
        self.f = open(path, "rb")
        self.elf = ELFFile(self.f)
        self.dw = self.elf.get_dwarf_info()
        self.vars = {}
        for cu in self.dw.iter_CUs():
            for d in cu.iter_DIEs():
                if d.tag == "DW_TAG_variable" and "DW_AT_location" in d.attributes:
                    n = die_name(d)
                    loc = d.attributes["DW_AT_location"].value
                    if n and isinstance(loc, list) and loc and loc[0] == 3 and len(loc) == 5:
                        self.vars.setdefault(n, (int.from_bytes(bytes(loc[1:5]), "little"), d))
        self.syms = {}
        self.sym_at = {}
        st = self.elf.get_section_by_name(".symtab")
        for s in st.iter_symbols():
            if s["st_value"] and s["st_info"]["type"] in ("STT_OBJECT", "STT_FUNC") and s.name:
                self.syms.setdefault(s.name, (s["st_value"], s["st_size"]))
                self.sym_at.setdefault(s["st_value"], (s.name, s["st_size"], s["st_info"]["type"]))
        self.secs = [s for s in self.elf.iter_sections()
                     if s["sh_flags"] & 2 and s["sh_addr"] and not s.name.startswith(".overlay")]

    def read(self, addr, n):
        for base, img in getattr(self, "ram", []):
            if base <= addr and addr + n <= base + len(img):
                return img[addr - base:addr - base + n]
        for s in self.secs:
            if s["sh_addr"] <= addr < s["sh_addr"] + s["sh_size"]:
                if s["sh_type"] == "SHT_NOBITS":
                    return bytes(n)
                o = addr - s["sh_addr"]
                return s.data()[o:o + n]
        return None

    def symbolize(self, v):
        if v == 0:
            return "NULL"
        if v in self.sym_at:
            n, _, t = self.sym_at[v]
            return n if t == "STT_FUNC" else "&" + n
        best = None
        for a, (n, sz, t) in self.sym_at.items():
            if a < v < a + max(sz, 1) and (best is None or a > best[0]):
                best = (a, n)
        if best:
            return f"(void *)&{best[1]} + {v - best[0]:#x}"
        return f"(void *){v:#x}"


def fwk_id(v):
    """SCP-firmware's fwk_id_t as the macro that makes it."""
    t, m = v & 15, (v >> 4) & 0xff
    if t == 0:
        return "FWK_ID_INVALID" if v == 0 else f"{v:#x}"
    if t == 1:
        return "FWK_ID_NONE"
    if t == 2:
        return f"FWK_ID_MODULE({m})"
    if t == 3:
        return f"FWK_ID_ELEMENT({m}, {(v >> 12) & 0x3ff})"
    if t == 4:
        return f"FWK_ID_SUB_ELEMENT({m}, {(v >> 12) & 0x3ff}, {(v >> 22) & 0x3ff})"
    if t == 5:
        return f"FWK_ID_API({m}, {(v >> 12) & 15})"
    if t == 6:
        return f"FWK_ID_EVENT({m}, {(v >> 12) & 0x3f})"
    if t == 7:
        return f"FWK_ID_NOTIFICATION({m}, {(v >> 12) & 0x3f})"
    return f"{v:#x}"


def find_type(dw, name):
    tag = None
    if name.startswith(("struct ", "union ", "enum ")):
        kw, name = name.split(" ", 1)
        tag = {"struct": "DW_TAG_structure_type", "union": "DW_TAG_union_type",
               "enum": "DW_TAG_enumeration_type"}[kw]
    for cu in dw.dw.iter_CUs():
        for d in cu.iter_DIEs():
            if die_name(d) == name and (d.tag == tag if tag else
                                        d.tag in ("DW_TAG_typedef", "DW_TAG_structure_type",
                                                  "DW_TAG_union_type", "DW_TAG_base_type")):
                if "DW_AT_byte_size" in d.attributes or d.tag == "DW_TAG_typedef":
                    return d
    sys.exit(f"{name}: no such type")


def strip(t):
    while t is not None and t.tag in ("DW_TAG_typedef", "DW_TAG_const_type", "DW_TAG_volatile_type"):
        t = t.get_DIE_from_attribute("DW_AT_type") if "DW_AT_type" in t.attributes else None
    return t


def size_of(t):
    t = strip(t)
    if t is None:
        return 0
    if "DW_AT_byte_size" in t.attributes:
        return t.attributes["DW_AT_byte_size"].value
    if t.tag == "DW_TAG_pointer_type":
        return 4
    if t.tag == "DW_TAG_array_type":
        return size_of(t.get_DIE_from_attribute("DW_AT_type")) * array_count(t)
    return 0


def array_count(t):
    n = 1
    for c in t.iter_children():
        if c.tag == "DW_TAG_subrange_type":
            if "DW_AT_count" in c.attributes:
                n *= c.attributes["DW_AT_count"].value
            elif "DW_AT_upper_bound" in c.attributes:
                n *= c.attributes["DW_AT_upper_bound"].value + 1
            else:
                n = 0
    return n


def type_name(t):
    if t is None:
        return "void"
    if t.tag in ("DW_TAG_typedef", "DW_TAG_base_type"):
        return die_name(t)
    if t.tag == "DW_TAG_const_type":
        return "const " + type_name(t.get_DIE_from_attribute("DW_AT_type") if "DW_AT_type" in t.attributes else None)
    if t.tag == "DW_TAG_volatile_type":
        return "volatile " + type_name(t.get_DIE_from_attribute("DW_AT_type"))
    if t.tag == "DW_TAG_pointer_type":
        return type_name(t.get_DIE_from_attribute("DW_AT_type") if "DW_AT_type" in t.attributes else None) + " *"
    if t.tag == "DW_TAG_structure_type":
        return "struct " + (die_name(t) or "<anon>")
    if t.tag == "DW_TAG_union_type":
        return "union " + (die_name(t) or "<anon>")
    if t.tag == "DW_TAG_enumeration_type":
        return "enum " + (die_name(t) or "<anon>")
    if t.tag == "DW_TAG_array_type":
        return type_name(t.get_DIE_from_attribute("DW_AT_type")) + f"[{array_count(t)}]"
    if t.tag == "DW_TAG_subroutine_type":
        return "fn"
    return t.tag


class Printer:
    def __init__(self, dw, follow):
        self.dw, self.follow = dw, follow
        self.todo = []
        self.done = set()

    def value(self, t, data, off, indent):
        t0 = t
        t = strip(t)
        pad = "\t" * indent
        if t is None:
            return "?"
        if t.tag == "DW_TAG_base_type":
            sz = t.attributes["DW_AT_byte_size"].value
            enc = t.attributes["DW_AT_encoding"].value
            raw = data[off:off + sz]
            if enc == 4:                                 # float
                return repr(struct.unpack("<f" if sz == 4 else "<d", raw)[0])
            v = int.from_bytes(raw, "little", signed=enc in (5, 6) and sz > 1 and False)
            if enc == 2:                                 # bool
                return "true" if v else "false"
            if enc in (5,):                              # signed
                v = int.from_bytes(raw, "little", signed=True)
                return str(v)
            return hex(v) if v > 9 else str(v)
        if t.tag == "DW_TAG_enumeration_type":
            sz = t.attributes["DW_AT_byte_size"].value
            v = int.from_bytes(data[off:off + sz], "little")
            for c in t.iter_children():
                if c.tag == "DW_TAG_enumerator" and c.attributes["DW_AT_const_value"].value == v:
                    return die_name(c)
            return str(v)
        if t.tag == "DW_TAG_pointer_type":
            v = int.from_bytes(data[off:off + 4], "little")
            s = self.dw.symbolize(v)
            pt = t.get_DIE_from_attribute("DW_AT_type") if "DW_AT_type" in t.attributes else None
            if pt is not None and strip(pt) is not None and strip(pt).tag == "DW_TAG_base_type" \
                    and size_of(pt) == 1 and v:
                b = self.dw.read(v, 128)
                if b:
                    return s + f" /* \"{b.split(bytes(1))[0].decode('latin1')}\" */"
            if self.follow > 0 and v and v in self.dw.sym_at and self.dw.sym_at[v][2] == "STT_OBJECT" \
                    and pt is not None and v not in self.done:
                self.todo.append((v, pt, self.follow - 1))
                self.done.add(v)
            return s
        if t.tag == "DW_TAG_array_type":
            et = t.get_DIE_from_attribute("DW_AT_type")
            n, es = array_count(t), size_of(et)
            if es == 1 and strip(et).tag == "DW_TAG_base_type":
                b = data[off:off + n]
                return "\"" + b.split(bytes(1))[0].decode("latin1") + "\""
            items = [self.value(et, data, off + i * es, indent + 1) for i in range(n)]
            if all(len(x) < 12 for x in items):
                return "{ " + ", ".join(items) + " }"
            return "{\n" + "".join(f"{pad}\t[{i}] = {x},\n" for i, x in enumerate(items)) + pad + "}"
        if t.tag == "DW_TAG_union_type" and die_name(t) == "__fwk_id":
            return fwk_id(int.from_bytes(data[off:off + 4], "little"))
        if t.tag == "DW_TAG_union_type":               # the first member only
            for m in t.iter_children():
                if m.tag == "DW_TAG_member":
                    return "{ " + f".{die_name(m) or '<anon>'} = " + \
                        self.value(m.get_DIE_from_attribute("DW_AT_type"), data, off, indent + 1) + " }"
        if t.tag in ("DW_TAG_structure_type", "DW_TAG_union_type"):
            out = []
            for m in t.iter_children():
                if m.tag != "DW_TAG_member":
                    continue
                mo = off + (m.attributes["DW_AT_data_member_location"].value
                            if "DW_AT_data_member_location" in m.attributes else 0)
                mt = m.get_DIE_from_attribute("DW_AT_type")
                if "DW_AT_bit_size" in m.attributes:
                    bs = m.attributes["DW_AT_bit_size"].value
                    if "DW_AT_data_bit_offset" in m.attributes:
                        bo = m.attributes["DW_AT_data_bit_offset"].value
                        word = int.from_bytes(data[off:off + size_of(t)], "little")
                    else:
                        sz = size_of(mt)
                        bo = sz * 8 - m.attributes["DW_AT_bit_offset"].value - bs
                        word = int.from_bytes(data[mo:mo + sz], "little")
                    v = (word >> bo) & ((1 << bs) - 1)
                    out.append(f"{pad}\t.{die_name(m)} = {v:#x},\n")
                    continue
                out.append(f"{pad}\t.{die_name(m) or '<anon>'} = {self.value(mt, data, mo, indent + 1)},\n")
            return "{\n" + "".join(out) + pad + "}"
        return f"/* {type_name(t0)} */"

    def object(self, name, addr, t, size):
        es = size_of(t)
        n = size // es if es and size > es and strip(t).tag != "DW_TAG_array_type" else 1
        data = self.dw.read(addr, max(size, es * n))
        if data is None:
            return f"/* {name}: not in a loaded section */\n"
        if n > 1:
            body = "{\n" + "".join(f"\t[{i}] = {self.value(t, data, i * es, 1)},\n" for i in range(n)) + "}"
            return f"{type_name(t)} {name}[{n}] = {body};\n"
        return f"{type_name(t)} {name} = {self.value(t, data, 0, 0)};\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("elf")
    ap.add_argument("name")
    ap.add_argument("--follow", type=int, default=0)
    ap.add_argument("--type")
    ap.add_argument("--count", type=int)
    ap.add_argument("--ram", action="append", default=[])
    ap.add_argument("--types", action="store_true")
    a = ap.parse_args()
    dw = Dwarf(a.elf)
    dw.ram = []
    for r in a.ram:
        base, path = r.split(":", 1)
        dw.ram.append((int(base, 0), open(path, "rb").read()))
    if a.name.startswith("0x"):
        if not a.type:
            sys.exit("an address needs --type")
        addr, t = int(a.name, 16), find_type(dw, a.type)
        name = dw.sym_at.get(addr, (f"obj_{addr:08x}",))[0]
    else:
        if a.name not in dw.vars:
            sys.exit(f"{a.name}: no such variable in the DWARF")
        addr, die = dw.vars[a.name]
        name = a.name
        t = find_type(dw, a.type) if a.type else die.get_DIE_from_attribute("DW_AT_type")
    if a.types:
        print_type(t)
        return
    size = size_of(t) * a.count if a.count else dw.syms.get(name, (addr, size_of(t)))[1] or size_of(t)
    p = Printer(dw, a.follow)
    print(f"/* {addr:#010x} */")
    print(p.object(name, addr, t, size_of(t) if strip(t).tag == "DW_TAG_array_type" else max(size, size_of(t))))
    while p.todo:
        v, pt, f = p.todo.pop(0)
        name, sz, _ = dw.sym_at[v]
        p.follow = f
        print(f"/* {v:#010x} */")
        print(p.object(name, v, pt, sz))


def print_type(t, seen=None, indent=0):
    seen = seen or set()
    s = strip(t)
    print(type_name(t))
    if s is not None and s.tag in ("DW_TAG_structure_type", "DW_TAG_union_type"):
        for m in s.iter_children():
            if m.tag == "DW_TAG_member":
                mo = m.attributes.get("DW_AT_data_member_location")
                print(f"  +{mo.value if mo else 0:#05x} {type_name(m.get_DIE_from_attribute('DW_AT_type'))} {die_name(m)}")


if __name__ == "__main__":
    main()
