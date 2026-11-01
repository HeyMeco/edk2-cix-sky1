#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""firmware/se/ddr/mem_types.h from CIX's SE firmware debug ELF.

  se_ddr_header.py se_fw.elf OUT.h

The types of CIX's DDR driver (the compile units under driver/ddr, laid
out as CIX's compiler laid them out), the prototypes of its external
functions and the declarations of its external variables, for the files
in firmware/se/ddr that translate it.  The basic integer types come from
<stdint.h>; the four functions DWARF describes only through an abstract
origin, and log_print's variable arguments, are added by hand.  Run once;
the header is then edited like any source (agents append to it).
"""

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from dwarf_dump import die_name  # noqa: E402
from dwarf_header import Emitter  # noqa: E402
from elftools.elf.elffile import ELFFile  # noqa: E402

DDR_DIR = b"/driver/ddr"
BASIC = {"uint8_t", "uint16_t", "uint32_t", "uint64_t", "int8_t", "int16_t", "int32_t", "int64_t",
         "uintptr_t", "size_t"}
EXTRA = """\
/* Functions DWARF names only through their abstract origin */
MEM_INIT_OUTPUT_BUFFER *GetMemOutputBuffer(void);	/* MemOutputLib.c */
MEM_FEATURE_S *GetMemFeatPtr(void);	/* MemStructLib.c */
MEM_PHY_PAD_CFG_ENTRY_S *GetPhyPadCfgPtr(uint8_t fsp);	/* MemStructLib.c */

/* What the driver uses from the rest of the firmware */
void *memset(void *s, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
void *malloc(size_t n);
void free(void *p);
"""


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("elf")
    ap.add_argument("out")
    a = ap.parse_args()
    elf = ELFFile(open(a.elf, "rb"))
    em = Emitter(sorted(BASIC))
    protos, externs = [], []
    for cu in elf.get_dwarf_info().iter_CUs():
        top = cu.get_top_DIE()
        comp = top.attributes.get("DW_AT_comp_dir")
        if not comp or DDR_DIR not in comp.value:
            continue
        fn = Path(die_name(top)).name
        for d in top.iter_children():
            if d.tag == "DW_TAG_typedef" and die_name(d):
                em.typedef(d)
            elif d.tag in ("DW_TAG_structure_type", "DW_TAG_union_type", "DW_TAG_enumeration_type") \
                    and die_name(d) and "DW_AT_declaration" not in d.attributes:
                em.compound(d)
            elif d.tag == "DW_TAG_subprogram" and die_name(d) and "DW_AT_low_pc" in d.attributes \
                    and "DW_AT_external" in d.attributes:
                rt = em.ref(d.get_DIE_from_attribute("DW_AT_type")) if "DW_AT_type" in d.attributes else "void"
                ps = [em.decl(c.get_DIE_from_attribute("DW_AT_type"), die_name(c) or "")
                      for c in d.iter_children() if c.tag == "DW_TAG_formal_parameter"]
                if any(c.tag == "DW_TAG_unspecified_parameters" for c in d.iter_children()):
                    ps.append("...")
                protos.append(f"{rt} {die_name(d)}({', '.join(ps) or 'void'});\t/* {fn} */")
            elif d.tag == "DW_TAG_variable" and die_name(d) and "DW_AT_external" in d.attributes \
                    and "DW_AT_location" in d.attributes:
                t, const = d.get_DIE_from_attribute("DW_AT_type"), ""
                while t.tag == "DW_TAG_const_type":         # const array: const elements
                    t, const = t.get_DIE_from_attribute("DW_AT_type"), "const "
                decl = em.decl(t, die_name(d))
                if decl.startswith("const "):
                    const = ""
                externs.append(f"extern {const}{decl};\t/* {fn} */")
    # the SE's include/storage.h has firmware_entry; CIX builds with short
    # enums, so its IRQn_Type array is 16-bit
    text_out = []
    for l in em.out:
        if "struct firmware_entry_s {" in l:
            l = "/* firmware_entry: include/storage.h */\n"
        elif l.startswith("typedef struct firmware_entry_s firmware_entry;"):
            continue
        text_out.append(l)
    em.out = text_out
    externs = [e.replace("extern IRQn_Type ddrc_irq_type[4];\t/* syscall_se.c */",
                         "extern int16_t ddrc_irq_type[4];\t/* syscall_se.c: IRQn_Type, CIX builds with short enums */")
               for e in externs]
    basic = re.compile(r"typedef [\w ]+ (%s);$" % "|".join(BASIC))
    body = "\n".join(l for l in em.out if not basic.match(l.strip()))
    text = ("/* SPDX-License-Identifier: BSD-2-Clause-Patent */\n"
            "/*\n * Types, functions and variables of CIX's DDR driver (driver/ddr), from the\n"
            " * DWARF of CIX's SE firmware debug build (scripts/open/se_ddr_header.py),\n"
            " * for the files here that translate it.\n */\n"
            "#ifndef MEM_TYPES_H\n#define MEM_TYPES_H\n\n#include <stdbool.h>\n#include <stddef.h>\n"
            "#include <stdint.h>\n\ntypedef unsigned int uint;\n\n"
            + body + "\n/* External functions (CIX's static ones are declared in their files) */\n"
            + "\n".join(protos) + "\n\n" + EXTRA + "\n/* External variables */\n" + "\n".join(externs)
            + "\n\n#endif\n")
    Path(a.out).write_text(text)


if __name__ == "__main__":
    main()
