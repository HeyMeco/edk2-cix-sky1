#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""C declarations from an ELF's DWARF: the types and prototypes of the
compile units whose file name contains one of the patterns.

  dwarf_header.py ELF PATTERN... [--only-types] [--skip NAME,...]

Prints the named enums (with values), structs and unions (members in
order, bit-fields with their widths, anonymous members inline), typedefs
and function prototypes (static ones marked), each type once, dependencies
first.  For porting code from a binary with debug information: the
layouts come out as the compiler laid them out.
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from dwarf_dump import die_name  # noqa: E402
from elftools.elf.elffile import ELFFile  # noqa: E402

BASE = {"_Bool": "bool"}


class Emitter:
    def __init__(self, skip):
        self.done, self.out, self.skip = set(), [], set(skip)

    def ref(self, t):
        """Type name for use in a declaration; emits the type first."""
        if t is None:
            return "void"
        tag = t.tag
        if tag == "DW_TAG_base_type":
            n = die_name(t)
            return BASE.get(n, n)
        if tag == "DW_TAG_typedef":
            self.typedef(t)
            return die_name(t)
        if tag in ("DW_TAG_structure_type", "DW_TAG_union_type", "DW_TAG_enumeration_type"):
            kw = {"DW_TAG_structure_type": "struct", "DW_TAG_union_type": "union",
                  "DW_TAG_enumeration_type": "enum"}[tag]
            if die_name(t) is None:
                return self.body(t)
            if "DW_AT_declaration" not in t.attributes:
                self.compound(t)
            return f"{kw} {die_name(t)}"
        if tag in ("DW_TAG_const_type", "DW_TAG_volatile_type"):
            sub = t.get_DIE_from_attribute("DW_AT_type") if "DW_AT_type" in t.attributes else None
            q = "const" if tag == "DW_TAG_const_type" else "volatile"
            return f"{q} {self.ref(sub)}"
        if tag == "DW_TAG_pointer_type":
            sub = t.get_DIE_from_attribute("DW_AT_type") if "DW_AT_type" in t.attributes else None
            if sub is not None and sub.tag in ("DW_TAG_structure_type", "DW_TAG_union_type") \
                    and die_name(sub):
                kw = "struct" if sub.tag == "DW_TAG_structure_type" else "union"
                if die_name(sub) not in self.done:
                    self.out.append(f"{kw} {die_name(sub)};")
                return f"{kw} {die_name(sub)} *"
            if sub is not None and sub.tag == "DW_TAG_subroutine_type":
                return self.fnptr(sub, "")
            return self.ref(sub) + " *"
        if tag == "DW_TAG_array_type":
            return self.ref(t.get_DIE_from_attribute("DW_AT_type"))
        if tag == "DW_TAG_subroutine_type":
            return self.fnptr(t, "")
        return f"/* {tag} */ void"

    def fnptr(self, t, name):
        rt = self.ref(t.get_DIE_from_attribute("DW_AT_type")) if "DW_AT_type" in t.attributes else "void"
        ps = [self.ref(c.get_DIE_from_attribute("DW_AT_type")) for c in t.iter_children()
              if c.tag == "DW_TAG_formal_parameter"]
        return f"{rt} (*{name})({', '.join(ps) or 'void'})"

    def decl(self, t, name):
        """'type name[dims]' for a member or variable."""
        dims = ""
        while t is not None and t.tag == "DW_TAG_array_type":
            for c in t.iter_children():
                if c.tag == "DW_TAG_subrange_type":
                    if "DW_AT_count" in c.attributes:
                        dims += f"[{c.attributes['DW_AT_count'].value}]"
                    elif "DW_AT_upper_bound" in c.attributes:
                        dims += f"[{c.attributes['DW_AT_upper_bound'].value + 1}]"
                    else:
                        dims += "[]"
            t = t.get_DIE_from_attribute("DW_AT_type")
        if t is not None and t.tag == "DW_TAG_pointer_type" and "DW_AT_type" in t.attributes and \
                t.get_DIE_from_attribute("DW_AT_type").tag == "DW_TAG_subroutine_type":
            return self.fnptr(t.get_DIE_from_attribute("DW_AT_type"), name) + dims
        r = self.ref(t)
        sep = "" if r.endswith("*") else " "
        return f"{r}{sep}{name}{dims}"

    def body(self, t):
        tag = t.tag
        if tag == "DW_TAG_enumeration_type":
            vals = [f"\t{die_name(c)} = {c.attributes['DW_AT_const_value'].value}," for c in t.iter_children()
                    if c.tag == "DW_TAG_enumerator"]
            return "enum {\n" + "\n".join(vals) + "\n}"
        kw = "struct" if tag == "DW_TAG_structure_type" else "union"
        lines = []
        for m in t.iter_children():
            if m.tag != "DW_TAG_member":
                continue
            mt = m.get_DIE_from_attribute("DW_AT_type")
            d = self.decl(mt, die_name(m) or "")
            if "DW_AT_bit_size" in m.attributes:
                d += f" : {m.attributes['DW_AT_bit_size'].value}"
            off = m.attributes.get("DW_AT_data_member_location")
            d = d.replace("\n", "\n\t")
            lines.append(f"\t{d};" + (f"\t/* +{off.value:#x} */" if off is not None and tag ==
                                      "DW_TAG_structure_type" else ""))
        return f"{kw} {{\n" + "\n".join(lines) + "\n}"

    def compound(self, t):
        key = (t.tag, die_name(t))
        if key in self.done or die_name(t) in self.skip:
            return
        self.done.add(key)
        b = self.body(t)
        kw = {"DW_TAG_structure_type": "struct", "DW_TAG_union_type": "union",
              "DW_TAG_enumeration_type": "enum"}[t.tag]
        self.out.append(b.replace(kw + " {", f"{kw} {die_name(t)} {{", 1) + ";\n")

    def typedef(self, t):
        key = ("typedef", die_name(t))
        if key in self.done or die_name(t) in self.skip:
            return
        self.done.add(key)
        sub = t.get_DIE_from_attribute("DW_AT_type") if "DW_AT_type" in t.attributes else None
        self.out.append(f"typedef {self.decl(sub, die_name(t))};\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("elf")
    ap.add_argument("patterns", nargs="+")
    ap.add_argument("--only-types", action="store_true")
    ap.add_argument("--skip", default="")
    a = ap.parse_args()
    elf = ELFFile(open(a.elf, "rb"))
    em = Emitter([s for s in a.skip.split(",") if s])
    protos = []
    for cu in elf.get_dwarf_info().iter_CUs():
        top = cu.get_top_DIE()
        fn = die_name(top) or ""
        if not any(p in fn for p in a.patterns):
            continue
        for d in top.iter_children():
            if d.tag in ("DW_TAG_typedef",) and die_name(d):
                em.typedef(d)
            elif d.tag in ("DW_TAG_structure_type", "DW_TAG_union_type", "DW_TAG_enumeration_type") \
                    and die_name(d) and "DW_AT_declaration" not in d.attributes:
                em.compound(d)
            elif d.tag == "DW_TAG_subprogram" and die_name(d) and not a.only_types \
                    and "DW_AT_low_pc" in d.attributes:
                rt = em.ref(d.get_DIE_from_attribute("DW_AT_type")) if "DW_AT_type" in d.attributes else "void"
                ps = [em.decl(c.get_DIE_from_attribute("DW_AT_type"), die_name(c) or "")
                      for c in d.iter_children() if c.tag == "DW_TAG_formal_parameter"]
                st = "" if "DW_AT_external" in d.attributes else "static "
                protos.append(f"{st}{rt} {die_name(d)}({', '.join(ps) or 'void'});"
                              f"\t/* {Path(fn).name} */")
    print("\n".join(em.out))
    print("\n".join(protos))


if __name__ == "__main__":
    main()
