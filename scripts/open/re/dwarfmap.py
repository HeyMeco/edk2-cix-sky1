#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Map a firmware ELF with DWARF debug info: compile units (with their
directories) and the functions each defines, with address and size.

  dwarfmap.py FIRMWARE.elf [--json OUT]

Prints one line per compile unit: code bytes, function count, path.
"""

import json
import sys
from collections import defaultdict

from elftools.elf.elffile import ELFFile


def cu_map(path):
    with open(path, "rb") as f:
        elf = ELFFile(f)
        dw = elf.get_dwarf_info()
        out = []
        for cu in dw.iter_CUs():
            top = cu.get_top_DIE()
            name = top.attributes.get("DW_AT_name")
            comp = top.attributes.get("DW_AT_comp_dir")
            name = name.value.decode(errors="replace") if name else "?"
            comp = comp.value.decode(errors="replace") if comp else ""
            funcs = []
            for die in top.iter_children():
                if die.tag != "DW_TAG_subprogram" or "DW_AT_low_pc" not in die.attributes:
                    continue
                low = die.attributes["DW_AT_low_pc"].value
                high = die.attributes.get("DW_AT_high_pc")
                size = 0
                if high is not None:
                    size = high.value if high.form != "DW_FORM_addr" else high.value - low
                fname = die.attributes.get("DW_AT_name")
                funcs.append({"name": fname.value.decode() if fname else "?", "addr": low, "size": size})
            out.append({"cu": name, "dir": comp, "functions": funcs})
        return out


def main():
    cus = cu_map(sys.argv[1])
    for c in sorted(cus, key=lambda c: c["cu"]):
        size = sum(f["size"] for f in c["functions"])
        print(f"{size:8d} {len(c['functions']):4d}  {c['cu']}")
    if "--json" in sys.argv:
        json.dump(cus, open(sys.argv[sys.argv.index("--json") + 1], "w"), indent=1)


if __name__ == "__main__":
    main()
