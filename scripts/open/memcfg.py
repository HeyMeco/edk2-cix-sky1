#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Decode memory_config.bin (CDCB) and compare two of them by meaning.

Board DRAM settings are compiled into memory_config.bin by the CIX
mem_config generators.  Formats v1.6 and v2.0 differ in the per-entry
layout of the LPDDR5 bus configuration (BCL5) and PHY pad configuration
(PADC) blocks and in a few reserved fields; every other block keeps its
layout.  This decodes both and compares field by field, so a board can be
moved from one format to the other without changing a single setting.

  memcfg.py dump FILE
  memcfg.py compare OLD NEW     exit 1 if any setting differs
"""

import struct
import sys

# Entry layouts per format: (field, struct code)
BCL5 = {
    (1, 6): [("MaxMemFreq", "H"), ("RankPerCh", "B"), ("CA_ODT", "B"), ("CK_ODT", "B"), ("CS_ODT", "B"),
             ("DQ_ODT", "B"), ("WCK_ODT", "B"), ("SOC_ODT", "B"), ("NTDQ_ODT", "B"), ("ODT_PDDS", "B"),
             ("rsvd", "B"), ("DqVref", "H"), ("CaVref", "H")],
    (2, 0): [("MaxMemFreq", "H"), ("RankPerCh", "B"), ("DevWidth", "B"), ("CA_ODT", "B"), ("CK_ODT", "B"),
             ("CS_ODT", "B"), ("DQ_ODT", "B"), ("WCK_ODT", "B"), ("SOC_ODT", "B"), ("NTDQ_ODT", "B"),
             ("ODT_PDDS", "B"), ("DqVref", "H"), ("CaVref", "H")],
}
_PAD = [("DqDrv", "B"), ("DqOdt", "B"), ("DqsDrv", "B"), ("DqsOdt", "B"), ("CkDrv", "B"), ("CaDrv", "B"),
        ("CsDrv", "B"), ("CkeDrv", "B"), ("FdbkDrv", "B"), ("FdbkOdt", "B"), ("RstDrv", "B"), ("FFE", "B"),
        ("DFE", "B"), ("CTLE", "B")]
PADC = {
    (1, 6): [("MaxMemFreq", "H"), ("RankPerCh", "B")] + _PAD + [("Rsvd", "3s")],
    (2, 0): [("MaxMemFreq", "H"), ("RankPerCh", "B"), ("DevWidth", "B")] + _PAD + [("Rsvd", "2s")],
}
ENTRY_BLOCKS = {b"BCL5": BCL5, b"PADC": PADC}
NEW_IN_V2 = {"DevWidth"}       # fields v2.0 added; checked separately
QUICK_SIZE = 0x20


def _entries(body, layout):
    fmt = "<" + "".join(c for _, c in layout)
    size = struct.calcsize(fmt)
    out = []
    for o in range(0, len(body) - size + 1, size):
        out.append(dict(zip((n for n, _ in layout), struct.unpack_from(fmt, body, o))))
    return out


def parse(data):
    """{"version", "quick", "blocks": [{sig, mask, raw, entries?}]}"""
    hdr = struct.unpack_from("<IIHHHBBBBH", data, QUICK_SIZE)
    sig, _, major, minor, tb_off, count = hdr[:6]
    if sig != 0x42434443:
        raise ValueError("no CDCB header")
    ver = (major, minor)
    blocks = []
    for i in range(count):
        guid, off = struct.unpack_from("<HH", data, QUICK_SIZE + tb_off + 4 * i)
        o = QUICK_SIZE + off
        bsig = data[o:o + 4]
        bsize, _cks, comp, mask = struct.unpack_from("<HBBH", data, o + 8)
        b = {"sig": bsig.decode(), "guid": guid, "mask": mask, "comp": comp,
             "raw": data[o + 16:o + bsize]}
        if bsig in ENTRY_BLOCKS:
            layouts = ENTRY_BLOCKS[bsig]
            if ver not in layouts:
                raise ValueError(f"no {bsig.decode()} layout for v{ver[0]}.{ver[1]}")
            b["entries"] = _entries(b["raw"], layouts[ver])
        blocks.append(b)
    return {"version": ver, "quick": data[:QUICK_SIZE], "blocks": blocks}


def compare(old, new, device_width=None):
    """Differences between two configs, ignoring format-only changes.

    device_width(block) -> the width the v2.0 DevWidth must hold for the
    entries of that v1.6 block (None: do not check)."""
    a, b = parse(old), parse(new)
    diffs = []
    if a["quick"][:16] != b["quick"][:16]:
        diffs.append("quick configuration differs")
    ka = [(x["sig"], x["mask"]) for x in a["blocks"]]
    kb = [(x["sig"], x["mask"]) for x in b["blocks"]]
    if ka != kb:
        diffs.append(f"block lists differ: {ka} vs {kb}")
        return diffs
    for x, y in zip(a["blocks"], b["blocks"]):
        name = f"{x['sig']} mask {x['mask']:#06x}"
        if "entries" in x:
            if len(x["entries"]) != len(y["entries"]):
                diffs.append(f"{name}: {len(x['entries'])} vs {len(y['entries'])} entries")
                continue
            for i, (ea, eb) in enumerate(zip(x["entries"], y["entries"])):
                for f, v in ea.items():
                    if f in ("rsvd", "Rsvd"):
                        continue
                    if eb.get(f) != v:
                        diffs.append(f"{name} entry {i}: {f} {v} -> {eb.get(f)}")
                if device_width and "DevWidth" in eb:
                    want = device_width(x)
                    if want is not None and eb["DevWidth"] != want:
                        diffs.append(f"{name} entry {i}: DevWidth {eb['DevWidth']}, expected {want}")
        elif x["sig"] == "FEAT":
            # v2.0 turned two of the three trailing reserved bytes into SSC
            # and DfsEn; 0 keeps them off, as the reserved bytes were.
            if x["raw"][:-3] != y["raw"][:-3] or any(y["raw"][-3:]):
                diffs.append(f"{name}: settings differ")
        elif x["raw"] != y["raw"]:
            diffs.append(f"{name}: contents differ")
    return diffs


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "dump":
        c = parse(open(sys.argv[2], "rb").read())
        print(f"v{c['version'][0]}.{c['version'][1]}, {len(c['blocks'])} blocks")
        for b in c["blocks"]:
            print(f"  {b['sig']} guid {b['guid']:#06x} mask {b['mask']:#06x} {len(b['raw'])} bytes")
            for e in b.get("entries", []):
                print("     ", {k: v for k, v in e.items() if k not in ("rsvd", "Rsvd")})
    elif len(sys.argv) == 4 and sys.argv[1] == "compare":
        d = compare(open(sys.argv[2], "rb").read(), open(sys.argv[3], "rb").read())
        for x in d:
            print(x)
        print("same settings" if not d else f"{len(d)} difference(s)")
        sys.exit(1 if d else 0)
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
