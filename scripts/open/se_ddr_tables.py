#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Register tables of CIX's DDR driver as C, from CIX's SE firmware debug ELF.

  se_ddr_tables.py se_fw.elf firmware/se/ddr [--check]

CIX's LPDDR5 register initialisation (driver/ddr/src/Tech/lp5/
MemLpddr5RegInit.c) copies large local arrays from anonymous read-only
data: the controller's register/value list and the values of the PI's and
PHY's consecutive registers.  This writes them as const arrays
(MemLpddr5RegData.c), with the register offset each row starts at.  The
addresses and sizes are those of CIX's memcpy calls (2002233e, 20022388,
20022392, 2002241a, 20022428, 20022436 in the debug build).  --check
compares an existing file instead of writing it.
"""

import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from se_ddr_island import Image  # noqa: E402

LP5_BASE = 0x20046c4c
# (name, address, words or pairs, kind, first register offset, what)
TABLES = [
    ("DdrcInitList", 0x20045b54, 79, "pairs", None,
     "controller registers: offset, value (MemLpddr5DdrcRegInit)"),
    ("DdrpiInitList", 0x20045ebc, 1227, "words", 0x2000,
     "PI registers 0 to 1226 (MemLpddr5DdrpiRegInit)"),
    ("DdrpiDarrayMapList", 0x20045dcc, 60, "words", 0x391c,
     "PI data array map, from PI register 1607 (MemLpddr5DdrpiRegInit)"),
    ("DdrpInitValList", LP5_BASE + 0x59c, 105, "words", 0x5400,
     "PHY registers from 1280 (MemLpddr5DdrpRegInit)"),
    ("DdrpAcValList", LP5_BASE + 0x740, 69, "words", 0x5000,
     "PHY address/command slice, from register 1024 (MemLpddr5DdrpRegInit)"),
    ("DdrpDsliceValList", LP5_BASE + 0x854, 139, "words", 0x4000,
     "PHY data slice 0, written to all slices at once (MemLpddr5DdrpRegInit)"),
]


def generate(elf):
    img = Image(elf)
    out = ["/* SPDX-License-Identifier: BSD-2-Clause-Patent */",
           "/*",
           " * LPDDR5 register tables of CIX's DDR driver (the local arrays of",
           " * driver/ddr/src/Tech/lp5/MemLpddr5RegInit.c), generated from CIX's SE",
           " * firmware debug build by scripts/open/se_ddr_tables.py; do not edit.",
           " */",
           '#include "mem_types.h"',
           ""]
    for name, addr, n, kind, first, what in TABLES:
        out.append(f"/* {what} */")
        if kind == "pairs":
            data = struct.unpack(f"<{2 * n}I", img.read(addr, 8 * n))
            out.append(f"const REG_VAL_PAIR {name}[{n}] = {{")
            for i in range(n):
                out.append(f"\t{{ {data[2 * i]:#06x}, {data[2 * i + 1]:#010x} }},")
        else:
            data = struct.unpack(f"<{n}I", img.read(addr, 4 * n))
            out.append(f"const uint32_t {name}[{n}] = {{")
            for i in range(0, n, 4):
                row = ", ".join(f"{v:#010x}" for v in data[i:i + 4])
                out.append(f"\t/* {first + 4 * i:#06x} */ {row},")
        out.append("};")
        out.append("")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("elf")
    ap.add_argument("outdir")
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    path = Path(a.outdir) / "MemLpddr5RegData.c"
    text = generate(a.elf)
    if a.check:
        same = path.read_text() == text
        print(f"{path}: {'matches' if same else 'DIFFERS from'} CIX's tables")
        sys.exit(0 if same else 1)
    path.write_text(text)
    print(f"{path}: {sum(t[2] for t in TABLES)} entries")


if __name__ == "__main__":
    main()
