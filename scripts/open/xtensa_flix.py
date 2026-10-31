#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Decoder for the 64-bit FLIX bundles of CSU_PM's Xtensa core (pm_core_v1).

  xtensa_flix.py ELF          decode every bundle of ELF and print a summary

decode_bundle(data) takes the 8 bytes of a bundle (op0 14 or 15 in the low
nibble of the first byte) and returns its operations as the 24-bit words the
core ISA would use for them, NOPs left out.  An emulator can then run them with
its ordinary decoder, provided it treats the bundle as one instruction:

  * every operation reads its operands before any operation writes (the
    compiler relies on it: `{l32i a7,a3,12 ; addi a3,a3,16}`,
    `{retw ; movi a2,0}`), then all register writes happen, then the one
    control transfer (at most one per bundle in CIX's code);
  * PC-relative operands use the bundle's own address with the core formulas:
    branch/J target = pc + 4 + offset, L32R = ((pc + 3) & ~3) + 4*offset,
    LEND of a LOOP = pc + 4 + imm8; the next instruction (fall-through, and
    LBEG of a LOOP) is pc + 8, not pc + 3;
  * a conditional branch in an op0=15 bundle carries an 18-bit offset; when it
    does not fit the core encoding (8-bit for B/BRI8/BBI, 12-bit for BZ) it is
    returned as a tuple ("branch", mnemonic, s, operand, offset): s is the
    tested address register, operand is the second register (ball ... bnone),
    the bit number (bbci/bbsi), the constant (beqi/bnei/blti/bgei from
    b4const, bltui/bgeui from b4constu) or None (beqz/bnez/bltz/bgez), and the
    target is pc + 4 + offset.

Encodings this module could not determine raise ValueError.  By default only
encodings seen in CIX's firmware are accepted; decode_bundle(data,
inferred=True) also accepts the few that follow from the observed tables but
never occur (listed as "inferred" below).

Formats
-------
op0 = bits 0..3.  In both formats bits 26..27 are 0 (enforced).

op0 = 14, three slots:
  slot 0  bits 4..27   24-bit core encoding: bits 4..23 of the bundle are bits
                       4..23 of the core word and bits 24..27 its op0 (only
                       0 QRST, 1 L32R, 2 LSAI occur).  Code points this core
                       has no use for are reused:
                         0x503000 (TLB group)          RETW (as RETW.N)
                         QRST op1=6 op2=0 r=0          MOV.N  at=t, as=s
                         LSAI r=14 (S32C1I)            LOOP (t=0), LOOPNEZ (t=2),
                                                       LOOPGTZ (t=1, inferred);
                                                       as=s, imm8 as in LOOP
                       0x0020f0 is NOP.
  slot 1  bits 28..47  20 bits, nibbles n0..n4 = bits 28-31 .. 44-47.  The core
                       RRR fields keep their roles: t = n0, r = n1, s = n2;
                       n4 (and for 9/a/b also n3) is the opcode:
                         4,5  EXTUI r=n1, t=n0, maskimm = n2+1, shift = n3 | (n4&1)<<4
                         6    ADDI  t=n0, s=n2, imm8 = n3:n1
                         7    ADDMI t=n0, s=n2, imm8 = n3:n1
                         8    MOVI  t=n0, imm12 = n3:n2:n1
                         9    n3: 0,1 SLLI r,s,sa  (32-sa = n3:t, as in the core)
                                  2 ADD  3 ADDX8  4 ADDX2  5 AND  6 MOVEQZ  7 MOVGEZ
                                  8 ADDX4  9 MOVLTZ  a MOVNEZ  c MUL16S  d MULL  e OR
                                  f SEXT r,s,t+7          (all r,s,t)
                         a    n3: 1 SRAI r,t,16+s   2 SRC r,s,t   3 r=0: MOV.N t,s;
                                  r=1: JX s;  r=2: SSL s   4 SRLI r,t,s
                                  5 s=0: NEG r,t; s=2: SRL r,t; s=1: SRA r,t (inferred)
                                  6 SLL r,s (t=0)   8 SUB r,s,t
                         b    n3=0: XOR r,s,t
                         c..f J, 18-bit offset in bits 28..45
  slot 2  bits 48..63  16 bits, nibbles t = 48-51, r = 52-55, s = 56-59,
                       g = 60-63 (the density layout with the op0 nibble
                       moved to the top and r/t exchanged):
                         2 ADD.N r,s,t   3 ADDI.N r,s,imm (t, 0 means -1)
                         4 ADDX2 r,s,t   5 ADDX4 r,s,t   6 AND   7 OR
                         8 SEXT r,s,t+7 (t 0 or 8)   9 SRLI r,t,s   a SUB   b XOR
                         c r<8: MOVI.N s,imm7 (imm7 = (r&7):t, -32..95);
                           r=8: MOV.N t,s;  0xc090: NOP
                         d s=0: ABS r,t;  s=1: NEG r,t;  s=4: SRL r,t;
                           s=2: SLL r,t and s=3: SRA r,t (inferred)
                         e SRAI r,t,s    f SRAI r,t,16+s

op0 = 15, two slots:
  slot 0  bits 4..27   as in op0 = 14
  slot 1  bits 28..59  conditional branch; bits 60..63 are 0:
                         A = bits 28..31, B = bits 32..35 (the tested register,
                         field s), offset = bits 36..53 (signed), op = bits 54..59
                         (always even).  op/2:
                          1 BBCI B,A   2 BBSI B,A   3 BALL (inferred)  4 BANY
                          5 BBC, 6 BBS (inferred)   7 BEQ   8 BEQI B,b4const[A]
                          9 BGE  10 BGEI  11 BGEU  12 BGEUI B,b4constu[A]  13 BLT
                         14 BLTI  15 BLTU  16 BLTUI  17 BNALL  18 BNE  19 BNEI
                         20 BNONE  21 BEQZ B  22 BGEZ  23 BLTZ  24 BNEZ
                         Two-register branches compare aB with aA (B is the core's
                         s, A its t).  Ops 3..20 are the branch mnemonics in
                         alphabetical order after BBCI/BBSI, 21..24 likewise.

How this was established
------------------------
From CIX's csupm_sky1 (CSU_PM 00.12.001, symbols, DWARF, .xt.prop), walking
the instruction ranges of .xt.prop: 2231 op0=14 and 464 op0=15 bundles, plus
321 op0=14 bundles in the 20 overlays (.xt.prop.overlayN).

  * Slot 0: the core decoding matches the known code: memcpy/memset/strlen
    (newlib-style hand-written loops), the literal-pool loads of strlen and
    strcmp, ssa8l/srli in memcpy's unaligned path.  RETW: function exits
    ({retw ; movi a2,0} for "return 0", {retw ; sub a2,a3,a2} in strlen).
    MOV.N: return-value copies after calls.  LOOP: the bundles before zero-
    overhead loop bodies (after alignment NOPs); the LEND offsets land on
    instruction starts.
  * Slot 1 and slot 2: each opcode from code with known meaning, e.g. memcpy's
    "addi a3,a3,16" and "src", FreeRTOS (vTaskPlaceOnEventListRestricted's
    MOVNEZ, uxTaskGetSystemState's ADDX8 for 40-byte TaskStatus_t, heap_4's
    OR of the allocated bit, prvDeleteTCB's AND), libgcc (64-bit NEG/SRL/SRC
    in __fixunssfdi, __lshrdi3; OR of the two compares and the MULL products
    in __udivdi3, whose other half uses core MULL for the same products;
    MOVGEZ in signed /2), Dinkumware printf (MOVEQZ in snprintf's "n == 0"
    handling, MOVLTZ/ABS in _Genld's exponent sign, MOVGEZ in _Litob's 64-bit
    abs), CoreSight ETR setup (AND with 0xc0 and the DBALO/DBAHI arguments),
    PL011 baud divisor (EXTUI ...,0,6 and SRLI ...,6), byte splitting with
    SRAI 24/16 and SRLI 8.
  * Format 15: the 18-bit offsets put every target on an instruction start;
    each op from its use: bnone/bany on strcmp's byte masks, bbci on
    Dinkumware's ctype digit bit (_DI = bit 5), beqi -1 after sbrk() and
    signal(), bnei 2 after _LDunscale (NaN code), bgeu/bltu loop exits, bltz
    on _Printf's result, bgez skipping the negation of a '*' width, bnall on
    the 2047 exponent mask.
  * Checks over the 2695 bundles of .xt.prop: every one decodes; all 601
    J/branch/LOOP targets are instruction starts with base pc+4, and every
    other base from pc+0 to pc+9 misplaces most of them (at least 69 of the
    125 J targets, 8 of the 12 LOOP ends and 321 of the 464 op0=15 branch
    targets); no bundle writes a register twice or has two control
    transfers; writes overwritten before any read in straight-line code are
    as rare as for ordinary instructions (0.14% vs 0.08%), and the hits are
    analysis artefacts (loop back-edges, FP register moves).
  * Held out: the 321 op0=14 bundles of the overlays (.xt.prop.overlayN) were
    only decoded after the tables were fixed; all decode, and their 16
    targets are instruction starts.
  * The returned words of all 3016 bundles, disassembled by GNU objdump with
    the pm_core_v1 configuration, give the intended mnemonics and operands.

Uncertain: LOOPNEZ vs LOOPGTZ for slot 0 t=2 (both uses have non-negative
counts, so the code behaves the same either way; LOOPNEZ follows the
alphabetical ordering seen elsewhere); MUL16S vs MUL16U for slot 1 9.c (CIX's
compiler uses MUL16S 100 times and MUL16U 6 times in 24-bit code, and these
are index-by-struct-size products); the inferred encodings above.  Nothing
here was checked by running code.
"""

import struct
import sys
from collections import Counter

B4CONST = (-1, 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 16, 32, 64, 128, 256)
B4CONSTU = (32768, 65536, 2, 3, 4, 5, 6, 7, 8, 10, 12, 16, 32, 64, 128, 256)


def _sx(v, bits):
    return v - (1 << bits) if v >> (bits - 1) & 1 else v


def _rrr(op2, op1, r, s, t):
    return op2 << 20 | op1 << 16 | r << 12 | s << 8 | t << 4


def _lsai(r, s, t, imm8):
    return (imm8 & 0xff) << 16 | r << 12 | s << 8 | t << 4 | 2


def _mov(t, s):
    return _rrr(2, 0, t, s, s)                    # or at,as,as


def _movi(t, v):
    v &= 0xfff
    return _lsai(10, v >> 8, t, v & 0xff)


def _bad(what, v):
    raise ValueError(f"xtensa_flix: undetermined {what} encoding {v:#x}")


# --- slot 0 ----------------------------------------------------------------

# core opcodes seen in slot 0, by (op0, op1, op2) or (op0, r) for LSAI
_S0_QRST = {  # (op1, op2): name
    (0, 1): "and", (0, 2): "or", (0, 3): "xor", (0, 8): "add", (0, 9): "addx2", (0, 10): "addx4",
    (0, 11): "addx8", (0, 12): "sub", (1, 0): "slli", (1, 1): "slli", (1, 2): "srai", (1, 3): "srai",
    (1, 4): "srli", (1, 8): "src", (1, 9): "srl", (1, 10): "sll", (1, 13): "mul16s", (2, 8): "mull",
    (3, 2): "sext", (3, 4): "min", (3, 5): "max", (3, 6): "minu", (3, 7): "maxu", (3, 8): "moveqz",
    (3, 9): "movnez", (4, 0): "extui", (5, 0): "extui",
}
# same encoding, not seen in a bundle
_S0_QRST_INFERRED = {
    (0, 13): "subx2", (0, 14): "subx4", (0, 15): "subx8", (1, 11): "sra", (1, 12): "mul16u",
    (2, 6): "saltu", (2, 7): "salt", (2, 10): "muluh", (2, 11): "mulsh", (2, 12): "quou", (2, 13): "quos",
    (2, 14): "remu", (2, 15): "rems", (3, 10): "movltz", (3, 11): "movgez",
}
_S0_LSAI = {0, 1, 2, 4, 5, 6, 9, 10, 12, 13}    # l8ui l16ui l32i s8i s16i s32i l16si movi addi addmi


def _slot0(v, inferred):
    w = (v & 0xfffff) << 4 | (v >> 20) & 15
    op0, t, s, r = w & 15, (w >> 4) & 15, (w >> 8) & 15, (w >> 12) & 15
    op1, op2 = (w >> 16) & 15, (w >> 20) & 15
    if w == 0x0020f0:
        return None                               # nop
    if w == 0x503000:
        return 0x000090                           # retw
    if op0 == 0 and op1 == 6 and op2 == 0 and r == 0:
        return _mov(t, s)                         # mov.n
    if op0 == 2 and r == 14:                      # loop / loopgtz / loopnez
        kind = {0: 8, 2: 9}.get(t, 10 if inferred and t == 1 else None)
        if kind is None:
            _bad("slot-0 loop", w)
        return (w >> 16) << 16 | kind << 12 | s << 8 | 0x76
    if op0 == 1:
        return w                                  # l32r
    if op0 == 2 and r in _S0_LSAI:
        return w
    if op0 == 0:
        if op1 == 0 and op2 == 4 and r in (0, 1, 2) and t == 0:
            return w                              # ssr ssl ssa8l
        if op1 == 0 and op2 == 4 and r == 15:
            return w                              # nsau
        if op1 == 0 and op2 == 6 and s in (0, 1):
            return w                              # neg abs
        if (op1, op2 if op1 < 4 else 0) in _S0_QRST:
            return w
        if inferred and ((op1, op2) in _S0_QRST_INFERRED or
                         (op1 == 0 and op2 == 4 and (r in (3, 4, 14)))):
            return w
    _bad("slot-0", w)


# --- op0 = 14, slot 1 ------------------------------------------------------

_S1_9 = {2: 8, 3: 11, 4: 9, 5: 1, 8: 10}          # n3 -> core op2 (op1 = 0)
_S1_9_RST = {6: (3, 8), 7: (3, 11), 9: (3, 10), 10: (3, 9), 12: (1, 13), 13: (2, 8), 15: (3, 2)}


def _slot1_14(v, inferred):
    g, n3, s, r, t = v >> 16, (v >> 12) & 15, (v >> 8) & 15, (v >> 4) & 15, v & 15
    if g >> 2 == 3:
        return (v & 0x3ffff) << 6 | 6                                 # j
    if g in (4, 5):
        return _rrr(s, 4 | (g & 1), r, n3, t)                         # extui r,t,sh,s+1
    if g == 6:
        return _lsai(12, s, t, n3 << 4 | r)                           # addi
    if g == 7:
        return _lsai(13, s, t, n3 << 4 | r)                           # addmi
    if g == 8:
        return _movi(t, _sx((v >> 4) & 0xfff, 12))
    if g == 9:
        if n3 in (0, 1):
            return _rrr(n3, 1, r, s, t)                               # slli
        if n3 == 14:
            return _rrr(2, 0, r, s, t)                                # or
        if n3 in _S1_9:
            return _rrr(_S1_9[n3], 0, r, s, t)
        if n3 in _S1_9_RST:
            op1, op2 = _S1_9_RST[n3]
            return _rrr(op2, op1, r, s, t)
    if g == 10:
        if n3 == 1:
            return _rrr(3, 1, r, s, t)                                # srai r,t,16+s
        if n3 == 2:
            return _rrr(8, 1, r, s, t)                                # src
        if n3 == 3 and r == 0:
            return _mov(t, s)                                         # mov.n
        if n3 == 3 and r == 1 and t == 0:
            return s << 8 | 0xa0                                      # jx
        if n3 == 3 and r == 2 and t == 0:
            return _rrr(4, 0, 1, s, 0)                                # ssl
        if n3 == 4:
            return _rrr(4, 1, r, s, t)                                # srli r,t,s
        if n3 == 5 and s == 0:
            return _rrr(6, 0, r, 0, t)                                # neg
        if n3 == 5 and s == 2:
            return _rrr(9, 1, r, 0, t)                                # srl
        if n3 == 5 and s == 1 and inferred:
            return _rrr(11, 1, r, 0, t)                               # sra
        if n3 == 6 and t == 0:
            return _rrr(10, 1, r, s, 0)                               # sll
        if n3 == 8:
            return _rrr(12, 0, r, s, t)                               # sub
    if g == 11 and n3 == 0:
        return _rrr(3, 0, r, s, t)                                    # xor
    _bad("op0=14 slot-1", v)


# --- op0 = 14, slot 2 ------------------------------------------------------

_S2_RRR = {2: 8, 4: 9, 5: 10, 6: 1, 7: 2, 10: 12, 11: 3}          # g -> core op2 (op1 = 0)


def _slot2_14(v, inferred):
    if v == 0xc090:
        return None                                                   # nop
    g, s, r, t = v >> 12, (v >> 8) & 15, (v >> 4) & 15, v & 15
    if g in _S2_RRR:
        return _rrr(_S2_RRR[g], 0, r, s, t)
    if g == 3:
        return _lsai(12, s, r, t if t else -1)                        # addi.n
    if g == 8 and t in (0, 8):
        return _rrr(2, 3, r, s, t)                                    # sext
    if g == 9:
        return _rrr(4, 1, r, s, t)                                    # srli r,t,s
    if g == 12:
        if not r & 8:
            i = (r & 7) << 4 | t
            return _movi(s, i - 128 if i >= 96 else i)                # movi.n
        if r == 8:
            return _mov(t, s)                                         # mov.n
    if g == 13:
        if s == 0:
            return _rrr(6, 0, r, 1, t)                                # abs
        if s == 1:
            return _rrr(6, 0, r, 0, t)                                # neg
        if s == 4:
            return _rrr(9, 1, r, 0, t)                                # srl
        if inferred and s == 2:
            return _rrr(10, 1, r, t, 0)                               # sll r,t
        if inferred and s == 3:
            return _rrr(11, 1, r, 0, t)                               # sra
    if g == 14:
        return _rrr(2, 1, r, s, t)                                    # srai r,t,s
    if g == 15:
        return _rrr(3, 1, r, s, t)                                    # srai r,t,16+s
    _bad("op0=14 slot-2", v)


# --- op0 = 15, slot 1 ------------------------------------------------------

_BR15 = {1: "bbci", 2: "bbsi", 3: "ball", 4: "bany", 5: "bbc", 6: "bbs", 7: "beq", 8: "beqi", 9: "bge",
         10: "bgei", 11: "bgeu", 12: "bgeui", 13: "blt", 14: "blti", 15: "bltu", 16: "bltui", 17: "bnall",
         18: "bne", 19: "bnei", 20: "bnone", 21: "beqz", 22: "bgez", 23: "bltz", 24: "bnez"}
_BR15_INFERRED = {"ball", "bbc", "bbs"}
_B_R = {"bnone": 0, "beq": 1, "blt": 2, "bltu": 3, "ball": 4, "bbc": 5, "bany": 8, "bne": 9, "bge": 10,
        "bgeu": 11, "bnall": 12, "bbs": 13}
_BRI8 = {"beqi": (2, 0), "bnei": (2, 1), "blti": (2, 2), "bgei": (2, 3), "bltui": (3, 2), "bgeui": (3, 3)}
_BZ = {"beqz": 0, "bnez": 1, "bltz": 2, "bgez": 3}


def _slot1_15(v, inferred):
    op, a, b, off = v >> 26, v & 15, (v >> 4) & 15, _sx((v >> 8) & 0x3ffff, 18)
    m = _BR15.get(op >> 1) if not op & 1 else None
    if m is None or (m in _BR15_INFERRED and not inferred):
        _bad("op0=15 branch", v)
    if m in _BZ:
        if -2048 <= off < 2048:
            return (off & 0xfff) << 12 | b << 8 | _BZ[m] << 6 | 0x16
        return ("branch", m, b, None, off)
    fits = -128 <= off < 128
    if m in _B_R:
        return (off & 0xff) << 16 | _B_R[m] << 12 | b << 8 | a << 4 | 7 if fits else ("branch", m, b, a, off)
    if m in ("bbci", "bbsi"):
        return ((off & 0xff) << 16 | (6 if m == "bbci" else 14) << 12 | b << 8 | a << 4 | 7 if fits
                else ("branch", m, b, a, off))
    n, mm = _BRI8[m]
    if fits:
        return (off & 0xff) << 16 | a << 12 | b << 8 | mm << 6 | n << 4 | 6
    return ("branch", m, b, (B4CONSTU if n == 3 else B4CONST)[a], off)


def decode_bundle(data: bytes, inferred: bool = False) -> list:
    """Operations of an 8-byte bundle as core 24-bit words (tuples for long branches), NOPs omitted."""
    if len(data) != 8:
        raise ValueError("xtensa_flix: a bundle is 8 bytes")
    w = int.from_bytes(data, "little")
    fmt = w & 15
    if fmt not in (14, 15) or w >> 26 & 3:
        _bad("bundle format", w)
    ops = [_slot0((w >> 4) & 0xffffff, inferred)]
    if fmt == 14:
        ops += [_slot1_14((w >> 28) & 0xfffff, inferred), _slot2_14(w >> 48, inferred)]
    else:
        if w >> 60:
            _bad("op0=15 bundle", w)
        ops.append(_slot1_15((w >> 28) & 0xffffffff, inferred))
    return [x for x in ops if x is not None]


# --- summary over an ELF ---------------------------------------------------

def _mnemonic(x):
    """Rough name of a decoded operation, for the summary."""
    if isinstance(x, tuple):
        return x[1]
    op0, t, s, r, op1, op2 = x & 15, (x >> 4) & 15, (x >> 8) & 15, (x >> 12) & 15, (x >> 16) & 15, x >> 20
    if op0 == 1:
        return "l32r"
    if op0 == 2:
        return {0: "l8ui", 1: "l16ui", 2: "l32i", 4: "s8i", 5: "s16i", 6: "s32i", 9: "l16si", 10: "movi",
                12: "addi", 13: "addmi"}[r]
    if op0 == 6:
        n, m = t & 3, t >> 2
        if n == 0:
            return "j"
        if n == 1:
            return ("beqz", "bnez", "bltz", "bgez")[m]
        if n == 2:
            return ("beqi", "bnei", "blti", "bgei")[m]
        return {1: {8: "loop", 9: "loopnez", 10: "loopgtz"}.get(r, "?"), 2: "bltui", 3: "bgeui"}[m]
    if op0 == 7:
        return {0: "bnone", 1: "beq", 2: "blt", 3: "bltu", 4: "ball", 5: "bbc", 6: "bbci", 8: "bany",
                9: "bne", 10: "bge", 11: "bgeu", 12: "bnall", 13: "bbs", 14: "bbsi"}[r & 14 if r in (7, 15) else r]
    if x == 0x90:
        return "retw"
    if x & 0xfff0ff == 0xa0:
        return "jx"
    if op1 == 0 and op2 == 2 and s == t:
        return "mov"
    if op1 == 0 and op2 == 4:
        return {0: "ssr", 1: "ssl", 2: "ssa8l", 3: "ssa8b", 4: "ssai", 14: "nsa", 15: "nsau"}.get(r, "?")
    if op1 == 0 and op2 == 6:
        return ("neg", "abs")[s]
    if op1 in (4, 5):
        return "extui"
    names = {0: {1: "and", 2: "or", 3: "xor", 8: "add", 9: "addx2", 10: "addx4", 11: "addx8", 12: "sub",
                 13: "subx2", 14: "subx4", 15: "subx8"},
             1: {0: "slli", 1: "slli", 2: "srai", 3: "srai", 4: "srli", 8: "src", 9: "srl", 10: "sll",
                 11: "sra", 12: "mul16u", 13: "mul16s"},
             2: {6: "saltu", 7: "salt", 8: "mull", 10: "muluh", 11: "mulsh", 12: "quou", 13: "quos",
                 14: "remu", 15: "rems"},
             3: {2: "sext", 4: "min", 5: "max", 6: "minu", 7: "maxu", 8: "moveqz", 9: "movnez",
                 10: "movltz", 11: "movgez"}}
    return names.get(op1, {}).get(op2, f"?{x:06x}")


def _bundles(path):
    """(section, address, 8 bytes) of every bundle in the instruction ranges of the ELF's .xt.prop tables."""
    from elftools.elf.elffile import ELFFile
    lengths = [3] * 8 + [2] * 6 + [8, 8]
    out = []
    with open(path, "rb") as fh:
        elf = ELFFile(fh)
        secs = {s.name: s for s in elf.iter_sections()}
        main = [s for s in secs.values() if s["sh_flags"] & 4 and s["sh_type"] == "SHT_PROGBITS" and s["sh_size"]
                and not s.name.startswith(".overlay")]
        for name, sec in secs.items():
            if name != ".xt.prop" and not name.startswith(".xt.prop."):
                continue
            prop = sec.data()
            codes = main if name == ".xt.prop" else [secs[name[len(".xt.prop"):]]]
            codes = [(c.name, c["sh_addr"], c.data()) for c in codes]
            for i in range(0, len(prop) - 11, 12):
                addr, size, flags = struct.unpack_from("<III", prop, i)
                if not flags & 2:                                     # XTENSA_PROP_INSN
                    continue
                for cname, base, data in codes:
                    if base <= addr < base + len(data):
                        break
                else:
                    continue
                p = addr - base
                while p < addr - base + size:
                    n = lengths[data[p] & 15]
                    if n == 8:
                        out.append((cname, base + p, bytes(data[p:p + 8])))
                    p += n
    return out


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__.split("\n\n")[1])
    bundles = _bundles(sys.argv[1])
    per_fmt, ok, mix, bad, longbr = Counter(), Counter(), Counter(), [], 0
    for sec, addr, data in bundles:
        fmt = data[0] & 15
        per_fmt[fmt] += 1
        try:
            ops = decode_bundle(data)
        except ValueError as e:
            bad.append((sec, addr, data.hex(), str(e)))
            continue
        ok[fmt] += 1
        for x in ops:
            mix[_mnemonic(x)] += 1
            longbr += isinstance(x, tuple)
    total = sum(per_fmt.values())
    print(f"{total} bundles ({per_fmt[14]} op0=14, {per_fmt[15]} op0=15), "
          f"{sum(ok.values())} decode fully ({ok[14]} op0=14, {ok[15]} op0=15)")
    print(f"{sum(mix.values())} operations, {longbr} of them branches beyond the core's range (tuples)")
    for name, n in sorted(mix.items(), key=lambda x: (-x[1], x[0])):
        print(f"{n:7}  {name}")
    for sec, addr, hx, err in bad:
        print(f"undecoded: {sec} {addr:#010x} {hx}  {err}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
