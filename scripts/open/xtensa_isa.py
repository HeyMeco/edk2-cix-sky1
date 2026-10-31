#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Which Xtensa instructions CSU_PM's core runs, and whether a build sticks to them.

  xtensa_isa.py census CIX.elf          instruction classes in a Cadence-built ELF
  xtensa_isa.py check  OURS.elf...       fail on any instruction outside ALLOWED

census walks the instruction ranges of the ELF's Xtensa property table
(.xt.prop), so literal pools and data are not taken for code, and decodes
the 24-bit formats by their opcode fields (Xtensa ISA reference).  It
counts 8-byte FLIX bundles (op0 14 and 15 on this core) without decoding
them: their formats belong to the core configuration, which is not public.

check disassembles with the toolchain's objdump (XTENSA_GNU_CONFIG set to
the pm_core_v1 plugin, see xtensa_core.py) and compares mnemonics with
ALLOWED: the base ISA plus the options CIX's own compiler uses on this core.
"""

import os
import re
import struct
import subprocess
import sys
from collections import Counter

BASE = """
abs add add.n addi addi.n addmi addx2 addx4 addx8 and ball bany bbc bbci bbs bbsi beq beq.n beqi beqz
beqz.n bge bgei bgeu bgeui bgez blt blti bltu bltui bltz bnall bne bnei bnez bnez.n bnone break break.n
call0 call4 call8 call12 callx0 callx4 callx8 callx12 dsync entry esync excw extui extw ill ill.n isync
j jx l16si l16ui l32e l32i l32i.n l32r l8ui memw mov mov.n movi movi.n moveqz movgez movltz movnez movsp
neg nop nop.n or ret ret.n retw retw.n rfde rfe rfi rfwo rfwu rotw rsil rsync s16i s32e s32i s32i.n s8i
sll slli sra srai src srl srli ssa8b ssa8l ssai ssl ssr sub subx2 subx4 subx8 syscall waiti xor
"""
OPTIONS = {
    "loops": "loop loopgtz loopnez",
    "mul16": "mul16s mul16u",
    "mul32": "mull muluh mulsh",
    "div32": "quos quou rems remu",
    "nsa": "nsa nsau",
    "minmax": "min max minu maxu",
    "sext": "sext",
    "salt": "salt saltu",
    "booleans": "all4 all8 andb andbc any4 any8 bf bt movf movt orb orbc xorb",
    "fp": "abs.s add.s addexp.s addexpm.s ceil.s const.s div0.s divn.s float.s floor.s lsi lsip lsx lsxp "
          "maddn.s mkdadj.s mksadj.s mov.s moveqz.s movf.s movgez.s movltz.s movnez.s movt.s mul.s neg.s "
          "nexp01.s oeq.s ole.s olt.s rfr round.s ssi ssip ssx ssxp sub.s trunc.s ueq.s ufloat.s ule.s ult.s "
          "un.s utrunc.s wfr",
}
# special and user registers seen in CIX's code on this core
SRS = """lbeg lend lcount sar br memctl windowbase windowstart ibreakenable ibreaka0 ibreaka1 dbreaka0 dbreaka1
dbreakc0 dbreakc1 configid0 configid1 epc1 epc2 epc3 epc4 epc5 epc6 epc7 depc eps2 eps3 eps4 eps5 eps6 eps7
excsave1 excsave2 excsave3 excsave4 excsave5 excsave6 excsave7 cpenable interrupt intset intclear intenable
ps vecbase exccause debugcause ccount prid icount icountlevel excvaddr ccompare0 ccompare1 ccompare2
misc0 misc1 misc2 misc3 fcr fsr"""
ALLOWED = set(BASE.split()) | {m for v in OPTIONS.values() for m in v.split()}
ALLOWED_SRS = set(SRS.split())

# --- census ---------------------------------------------------------------

RST0 = {1: "and", 2: "or", 3: "xor", 6: "neg/abs", 8: "add", 9: "addx2", 10: "addx4", 11: "addx8",
        12: "sub", 13: "subx2", 14: "subx4", 15: "subx8"}
RST1 = {0: "slli", 1: "slli", 2: "srai", 3: "srai", 4: "srli", 6: "xsr", 7: "accer", 8: "src", 9: "srl",
        10: "sll", 11: "sra", 12: "mul16u", 13: "mul16s", 15: "imp"}
RST2 = {0: "andb", 1: "andbc", 2: "orb", 3: "orbc", 4: "xorb", 6: "saltu", 7: "salt", 8: "mull",
        10: "muluh", 11: "mulsh", 12: "quou", 13: "quos", 14: "remu", 15: "rems"}
RST3 = {0: "rsr", 1: "wsr", 2: "sext", 3: "clamps", 4: "min", 5: "max", 6: "minu", 7: "maxu", 8: "moveqz",
        9: "movnez", 10: "movltz", 11: "movgez", 12: "movf", 13: "movt", 14: "rur", 15: "wur"}
LSAI = {0: "l8ui", 1: "l16ui", 2: "l32i", 4: "s8i", 5: "s16i", 6: "s32i", 7: "cache", 9: "l16si",
        10: "movi", 11: "l32ai", 12: "addi", 13: "addmi", 14: "s32c1i", 15: "s32ri"}
QRST = {8: "fp: indexed load/store", 9: "l32e/s32e", 10: "fp: arithmetic", 11: "fp: compare",
        14: "fp/other op1=14", 15: "fp/other op1=15"}


def classify(w):
    op0 = w & 15
    t, r, op1, op2 = (w >> 4) & 15, (w >> 12) & 15, (w >> 16) & 15, (w >> 20) & 15
    if op0 == 0:
        if op1 == 0:
            if op2 == 0:
                return f"st0 r={r}"
            if op2 == 4:
                return {14: "nsa", 15: "nsau"}.get(r, f"st1 r={r}")
            return RST0.get(op2, f"rst0 op2={op2}")
        if op1 in (1, 2, 3):
            return (RST1, RST2, RST3)[op1 - 1].get(op2, f"rst{op1} op2={op2}")
        if op1 in (4, 5):
            return "extui"
        return QRST.get(op1, f"qrst op1={op1}")
    if op0 == 1:
        return "l32r"
    if op0 == 2:
        return LSAI.get(r, f"lsai r={r}")
    if op0 == 3:
        return "fp: load/store immediate"
    if op0 == 4:
        return "mac16"
    if op0 == 5:
        return "call"
    if op0 == 6:
        n, m = (w >> 4) & 3, (w >> 6) & 3
        if n == 3 and m == 0:
            return "entry"
        if n == 3 and m == 1:
            return {0: "bf", 1: "bt", 8: "loop", 9: "loopnez", 10: "loopgtz"}.get(r, f"bi1 r={r}")
        return ("j", "bz", "bi0", "bi1")[n]
    if op0 == 7:
        return "b"
    return "density" if op0 < 14 else f"FLIX bundle (op0 {op0})"


def census(path):
    from elftools.elf.elffile import ELFFile
    with open(path, "rb") as fh:
        elf = ELFFile(fh)
        prop = elf.get_section_by_name(".xt.prop").data()
        code = [(s["sh_addr"], s.data()) for s in elf.iter_sections()
                if s["sh_flags"] & 4 and s["sh_type"] == "SHT_PROGBITS" and s["sh_size"]]
    lengths = [3] * 8 + [2] * 6 + [8, 8]
    count = Counter()
    for i in range(0, len(prop), 12):
        addr, size, flags = struct.unpack_from("<III", prop, i)
        if not flags & 2:                                   # XTENSA_PROP_INSN
            continue
        for base, data in code:
            if base <= addr < base + len(data):
                break
        else:
            continue
        p = addr - base
        while p < addr - base + size:
            w = int.from_bytes(data[p:p + 3], "little")
            count[classify(w)] += 1
            p += lengths[w & 15]
    for name, n in sorted(count.items(), key=lambda x: -x[1]):
        print(f"{n:7}  {name}")


# --- check ----------------------------------------------------------------

def check(paths, objdump):
    bad = 0
    for path in paths:
        out = subprocess.run([objdump, "-d", path], check=True, capture_output=True, text=True).stdout
        found = Counter()
        where = {}
        func, literal, prev = "?", False, ""
        for line in out.splitlines():
            m = re.match(r"^Disassembly of section (\S+):$", line)
            if m:
                literal = ".literal" in m.group(1) or ".lit4" in m.group(1)
                continue
            m = re.match(r"^[0-9a-f]+ <(.+)>:$", line)
            if m:
                func = m.group(1)
                continue
            parts = line.split("\t")
            if literal or len(parts) < 3 or not re.match(r"^\s*[0-9a-f]+:$", parts[0]):
                continue
            mn = parts[2].split()[0] if parts[2].split() else ""
            reg = re.match(r"^(rsr|wsr|xsr|rur|wur)\.(\w+)$", mn)
            ok = (reg.group(2) in ALLOWED_SRS) if reg else mn in ALLOWED
            if mn == ".byte" and prev in ("j", "jx", "ret", "ret.n", "retw", "retw.n", ".byte"):
                ok = True                                   # alignment padding, never executed
            prev = mn
            if not ok:
                found[mn] += 1
                where.setdefault(mn, f"{func} {parts[0].strip()}")
        for mn, n in found.items():
            print(f"{path}: {n} x {mn!r} (first in {where[mn]}) is not an instruction of pm_core_v1")
        bad += len(found)
    if not bad:
        print(f"{len(paths)} file(s): every instruction is one pm_core_v1 runs")
    return 1 if bad else 0


def main():
    if len(sys.argv) < 3 or sys.argv[1] not in ("census", "check"):
        sys.exit(__doc__)
    if sys.argv[1] == "census":
        census(sys.argv[2])
        return 0
    if not os.environ.get("XTENSA_GNU_CONFIG"):
        sys.exit("xtensa_isa: set XTENSA_GNU_CONFIG to the pm_core_v1 plugin (xtensa_core.py)")
    return check(sys.argv[2:], os.environ.get("OBJDUMP", "xtensa-esp-elf-objdump"))


if __name__ == "__main__":
    sys.exit(main())
