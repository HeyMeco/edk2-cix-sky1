#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""ACPI board-delta parity check.

The vendor Orange Pi firmware is a CIX Merak build plus board edits.  This
tool computes, device by device, the edits the vendor made on top of its
CIX base (vendor-base -> vendor-board) and the edits this tree makes on top
of its own CIX base (our-base -> our-board), and reports devices where the
two edit sets differ.  Version drift between the two CIX bases cancels out.

    compare.py VENDOR_BASE.dsl VENDOR_BOARD.dsl OUR_BASE.dsl OUR_BOARD.dsl
"""

import difflib
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from devices import devices  # noqa: E402


def edits(a, b):
    out = {}
    for p in set(a) | set(b):
        if p not in a:
            out[p] = ("ADDED",)
        elif p not in b:
            out[p] = ("REMOVED",)
        elif a[p] != b[p]:
            d = [l for l in difflib.unified_diff(a[p].split("\n"), b[p].split("\n"), lineterm="", n=0)
                 if not l.startswith(("---", "+++", "@@"))]
            out[p] = tuple(sorted(d))
    return out


def main():
    vb, vt, ob, ot = (devices(Path(p).read_text(errors="replace")) for p in sys.argv[1:5])
    allow = set(sys.argv[5].split(",")) if len(sys.argv) > 5 else set()
    ve, oe = edits(vb, vt), edits(ob, ot)
    bad = 0
    for p in sorted(set(ve) | set(oe)):
        if ve.get(p) == oe.get(p) or p in allow:
            continue
        bad += 1
        print(f"## {p}")
        print("   vendor edit:", "none" if p not in ve else "")
        for l in ve.get(p, ()):
            print("     ", l)
        print("   our edit:", "none" if p not in oe else "")
        for l in oe.get(p, ()):
            print("     ", l)
    print(f"{bad} device(s) differ" if bad else "board deltas match")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
