#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""How much of CIX's CSU_PM code the open firmware has a counterpart for.

  csupm_coverage.py OPEN.elf [--cix CIX.elf] [--units] [--tracker]

CIX's image (release/pm_fw/Release/csupm_sky1, full DWARF) lists every
function with its size and compile unit.  A function counts as covered
when the open image has a function of the same name (the port keeps CIX's
names), or when the open sources name it as what they translate (`CIX's
foo`, or the name itself in code or a comment).  It is a measure of the
port, not of equivalence: sky1emu pm-test (emu/) checks behaviour.  Code CIX's image
has that the open firmware replaces rather than translates (FreeRTOS's
port, by our port of upstream FreeRTOS; the Cadence runtime) and the debug CLI, which is not ported, are reported
apart.

--units lists, per CIX compile unit, the code not covered (largest first);
--tracker prints the lines for docs/open-firmware.md's tracker.
"""

import argparse
import re
import sys
from collections import defaultdict
from pathlib import Path

from elftools.elf.elffile import ELFFile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE / "re"))
from dwarfmap import cu_map  # noqa: E402

CIX_PM = "release/pm_fw/Release/csupm_sky1"
SOURCES = ("src/**/*.[ch]", "scp/**/*.[ch]", "include/*.h", "arch/*", "lib/*.c")

# group, label in the report, how the open firmware treats it
GROUPS = (
    ("scp", "SCP-firmware framework and modules", "port"),
    ("sky1", "CIX Source/projects/sky1", "port"),
    ("common", "CIX Source/common", "port"),
    ("runtime", "FreeRTOS, Cadence runtime", "upstream FreeRTOS, own port and libc"),
    ("cli", "debug CLI", "not ported"),
)


def group(cu):
    if "/super/" not in cu:
        return "runtime"			# Cadence's xclib, libgcc, HAL, XTOS
    p = cu.split("/super/", 1)[1]
    if "/cli/" in p or "FreeRTOS-Plus-CLI" in p:
        return "cli"
    if p.startswith("Thirdparty/rtos"):
        return "runtime"
    if p.startswith("Thirdparty/scpfw"):
        return "scp"
    if p.startswith("Source/common"):
        return "common"
    return "sky1"


def open_names(elf, root):
    names = set()
    with open(elf, "rb") as f:
        for s in ELFFile(f).get_section_by_name(".symtab").iter_symbols():
            if s["st_info"]["type"] == "STT_FUNC":
                names.add(s.name)
    text = "".join(p.read_text(errors="replace") for g in SOURCES for p in root.glob(g))
    cited = set(re.findall(r"CIX(?:'s|:)\s+`?([A-Za-z_]\w+)", text))
    return names, cited | set(re.findall(r"[A-Za-z_]\w{3,}", text))


def bar(pct, width=20):
    n = round(pct * width / 100)
    return "█" * n + "░" * (width - n)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("open_elf")
    ap.add_argument("--cix")
    ap.add_argument("--units", action="store_true")
    ap.add_argument("--tracker", action="store_true")
    a = ap.parse_args()
    if a.cix is None:
        from cix_debug import fetch
        a.cix = fetch(CIX_PM)
    root = HERE.parent.parent / "firmware" / "csupm"
    names, cited = open_names(a.open_elf, root)

    total, done = defaultdict(int), defaultdict(int)
    units = []
    for cu in cu_map(a.cix):
        g = group(cu["cu"])
        t = c = 0
        for f in cu["functions"]:
            n = f["name"].removeprefix("__overlay__")
            t += f["size"]
            if n in names or n in cited:
                c += f["size"]
        total[g] += t
        done[g] += c
        if g in ("scp", "sky1", "common") and t:
            units.append((t - c, t, cu["cu"].split("/super/", 1)[-1]))

    port_t = sum(total[g] for g, _, k in GROUPS if k == "port")
    port_c = sum(done[g] for g, _, k in GROUPS if k == "port")
    if a.tracker:
        print(f"port, in the emulator, by CIX's code ({sum(total.values()) // 1000} KB)"
              f"   {port_c * 100 // port_t}% of what is ported")
        for g, label, kind in GROUPS:
            kb = f"{total[g] // 1000:3d} KB"
            if kind == "port":
                p = done[g] * 100 // total[g]
                print(f"  {label:38s} {kb}   {bar(p)} {p:3d}%")
            else:
                print(f"  {label:38s} {kb}   {kind}")
    else:
        for g, label, kind in GROUPS:
            p = done[g] * 100 // max(total[g], 1)
            print(f"{label:38s} {total[g]:7d} bytes  {done[g]:7d} covered {p:3d}%  ({kind})")
        print(f"{'ported parts together':38s} {port_t:7d} bytes  {port_c:7d} covered "
              f"{port_c * 100 // port_t:3d}%")
    if a.units:
        for left, t, n in sorted(units, reverse=True):
            if left:
                print(f"  {left:6d} of {t:6d} bytes left  {n}")


if __name__ == "__main__":
    main()
