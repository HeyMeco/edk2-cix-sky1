#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Name the functions of a stripped AArch64 image from a reference build.

Matches each function of a reference ELF (built from source) against a raw
image by its instruction sequence with immediates and branch/literal
targets masked, so code that differs only in addresses matches.  Prints
(and with --json writes) "address name size" for every unique match; what
stays unmatched is the code the reference tree does not contain.

  funcmatch.py REF.elf REF.bin REF_BASE IMAGE.bin IMAGE_BASE [--json OUT] [--thumb]

REF.bin is REF.elf flattened with objcopy -O binary (it starts at REF_BASE).
--thumb matches Thumb-2 code (Cortex-M firmware): the image is decoded by
a linear sweep in 2- and 4-byte steps.  Needs capstone and llvm/GNU nm.
"""

import json
import re
import subprocess
import sys
from pathlib import Path

import capstone

MIN_INSNS = 6


def norm(md, data, base):
    toks = []
    for off in range(0, len(data) - 3, 4):
        insn = next(md.disasm(data[off:off + 4], base + off), None)
        if insn is None:
            toks.append("?")
            continue
        ops = re.sub(r"#-?0x[0-9a-f]+|#-?\d+", "#", insn.op_str)
        toks.append(insn.mnemonic + " " + ops)
    return toks


def norm_thumb(md, data, base):
    """Linear sweep: (offset, token) for each decoded Thumb-2 instruction."""
    toks = []
    off = 0
    while off < len(data) - 1:
        insn = next(md.disasm(data[off:off + 4], base + off), None)
        if insn is None:
            toks.append((off, "?"))
            off += 2
            continue
        ops = re.sub(r"#-?0x[0-9a-f]+|#-?\d+|\b0x[0-9a-f]+\b", "#", insn.op_str)
        toks.append((off, insn.mnemonic.split(".")[0] + " " + ops))
        off += insn.size
    return toks


def match_thumb(syms, rdata, ref_base, idata, img_base):
    md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB | capstone.CS_MODE_MCLASS | capstone.CS_MODE_V8)
    it = norm_thumb(md, idata, img_base)
    toks = [t for _, t in it]
    index = {}
    for i in range(len(toks) - MIN_INSNS):
        index.setdefault(tuple(toks[i:i + MIN_INSNS]), []).append(i)
    found = []
    for addr, size, name in sorted(syms):
        start = (addr & ~1) - ref_base
        seq = [t for _, t in norm_thumb(md, rdata[start:start + size], (addr & ~1))]
        if len(seq) < MIN_INSNS:
            continue
        hits = [i for i in index.get(tuple(seq[:MIN_INSNS]), []) if toks[i:i + len(seq)] == seq]
        if len(hits) == 1:
            found.append((img_base + it[hits[0]][0], name, size))
    return found


def main():
    a = sys.argv[1:]
    ref_elf, ref_bin, ref_base, img, img_base = a[0], a[1], int(a[2], 0), a[3], int(a[4], 0)
    out = a[a.index("--json") + 1] if "--json" in a else None
    thumb = "--thumb" in a
    md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
    syms = []
    nm = "/opt/homebrew/opt/llvm/bin/llvm-nm" if Path("/opt/homebrew/opt/llvm/bin/llvm-nm").exists() else "nm"
    for line in subprocess.run([nm, "-S", "--defined-only", ref_elf], capture_output=True, text=True).stdout.splitlines():
        p = line.split()
        if len(p) == 4 and p[2] in "tT" and int(p[1], 16) >= (2 if thumb else 4) * MIN_INSNS:
            syms.append((int(p[0], 16), int(p[1], 16), p[3]))
    rdata, idata = open(ref_bin, "rb").read(), open(img, "rb").read()
    if thumb:
        found = sorted(match_thumb(syms, rdata, ref_base, idata, img_base))
        report(found, syms, out)
        return
    rt, it = norm(md, rdata, ref_base), norm(md, idata, img_base)
    index = {}
    for i in range(len(it) - MIN_INSNS):
        index.setdefault(tuple(it[i:i + MIN_INSNS]), []).append(i)
    found = []
    for addr, size, name in sorted(syms):
        s = (addr - ref_base) // 4
        n = size // 4
        seq = rt[s:s + n]
        if len(seq) < n:
            continue
        hits = [i for i in index.get(tuple(seq[:MIN_INSNS]), []) if it[i:i + n] == seq]
        if len(hits) == 1:
            found.append((img_base + 4 * hits[0], name, size))
    report(sorted(found), syms, out)


def report(found, syms, out):
    for addr, name, size in found:
        print(f"{addr:#x} {name} {size:#x}")
    print(f"# {len(found)} of {len(syms)} reference functions matched", file=sys.stderr)
    if out:
        json.dump([{"address": a, "name": n, "size": s} for a, n, s in found], open(out, "w"), indent=1)


if __name__ == "__main__":
    main()
