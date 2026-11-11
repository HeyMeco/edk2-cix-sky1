// SPDX-License-Identifier: BSD-2-Clause-Patent
//! Instruction decoding for pm_core_v1: the 24-bit core encodings, the
//! 16-bit density ones and the operations of a FLIX bundle.

use super::flix::{Slot, B4CONST, B4CONSTU};

pub const LBEG: u32 = 0;
pub const LEND: u32 = 1;
pub const LCOUNT: u32 = 2;
pub const WINDOWBASE: u32 = 72;
pub const WINDOWSTART: u32 = 73;
pub const PS: u32 = 230;
pub const FCR: u32 = 232; // user registers
pub const FSR: u32 = 233;

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Cond {
    Eqz,
    Nez,
    Ltz,
    Gez,
    Eqi,
    Nei,
    Lti,
    Gei,
    Ltui,
    Geui,
    Bbci,
    Bbsi,
    Bnone,
    Beq,
    Blt,
    Bltu,
    Ball,
    Bbc,
    Bany,
    Bne,
    Bge,
    Bgeu,
    Bnall,
    Bbs,
    Bf,
    Bt,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Bin {
    And,
    Or,
    Xor,
    Addx(u8),
    Subx(u8),
    Mul16u,
    Mul16s,
    Saltu,
    Salt,
    Mull,
    Muluh,
    Mulsh,
    Quou,
    Quos,
    Remu,
    Rems,
    Min,
    Max,
    Minu,
    Maxu,
}

/// Groups of floating-point encodings, executed by their fields
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum FpGroup {
    Lsci,
    Lsx,
    Fp0,
    Fp1op,
    Fp1,
    Dfp0,
    Dfp0op,
    Dfp1,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum K {
    Unknown,
    Nop,
    L32r { t: u8, off: u32 },
    Call { n: u8, off: i32 },
    Callx { k: u8, s: u8 },
    J { off: i32 },
    Jx { s: u8 },
    Ret,
    Retw,
    Br { cond: Cond, s: u8, x: i32, off: i32 },
    Entry { s: u8, frame: u32 },
    Loop { kind: u8, s: u8, imm8: u32 },
    Load { t: u8, s: u8, off: u32, size: u8, signed: bool },
    Store { t: u8, s: u8, off: u32, size: u8 },
    Movi { t: u8, v: u32 },
    Addi { t: u8, s: u8, v: u32 },
    Bin { op: Bin, r: u8, s: u8, t: u8 },
    Neg { r: u8, t: u8 },
    Abs { r: u8, t: u8 },
    Extui { r: u8, t: u8, sh: u8, mask: u32 },
    Slli { r: u8, s: u8, sh: u8 },
    Srai { r: u8, t: u8, sh: u8 },
    Srli { r: u8, t: u8, sh: u8 },
    Src { r: u8, s: u8, t: u8 },
    Srl { r: u8, t: u8 },
    Sll { r: u8, s: u8 },
    Sra { r: u8, t: u8 },
    Xsr { sr: u32, t: u8 },
    Rsr { sr: u32, t: u8 },
    Wsr { sr: u32, t: u8 },
    Rur { ur: u32, r: u8 },
    Wur { ur: u32, t: u8 },
    Sext { r: u8, s: u8, bits: u8 },
    MovCond { cond: u8, r: u8, s: u8, t: u8 },
    MovB { want: u8, r: u8, s: u8, t: u8 },
    Movsp { s: u8, t: u8 },
    Rfe,
    Rfde,
    Rfw { under: bool },
    Rfi { level: u8 },
    Clrex,
    Getex { t: u8 },
    Break { s: u8, t: u8 },
    BreakN { s: u8 },
    Syscall,
    Simcall,
    Rsil { t: u8, s: u8 },
    Waiti { s: u8 },
    AnyAll { width: u8, all: bool, s: u8, t: u8 },
    BoolOp { op: u8, r: u8, s: u8, t: u8 },
    Ssr { s: u8 },
    Ssl { s: u8 },
    Ssa8l { s: u8 },
    Ssa8b { s: u8 },
    Ssai { v: u8 },
    Rotw { k: i8 },
    Nsa { s: u8, t: u8 },
    Nsau { s: u8, t: u8 },
    TlbRead { t: u8 },
    Fp { g: FpGroup, op: u8, r: u8, s: u8, t: u8, imm: u32 },
    /// operations bundle_ops[start .. start + n]
    Bundle { start: u32, n: u8 },
}

#[derive(Clone, Copy, Debug)]
pub struct Insn {
    pub k: K,
    /// highest address register used: the window check's operand
    pub maxr: u8,
    /// transfers control or changes the window: last in a bundle
    pub ctl: bool,
    /// needs coprocessor 0
    pub fp: bool,
    /// writes SAR: after the bundle's operations that read it
    pub late: bool,
    pub len: u8,
}

fn mk(k: K, maxr: u8) -> Insn {
    Insn { k, maxr, ctl: false, fp: false, late: false, len: 0 }
}

fn ctl(k: K, maxr: u8) -> Insn {
    Insn { k, maxr, ctl: true, fp: false, late: false, len: 0 }
}

fn fp(k: K, maxr: u8) -> Insn {
    Insn { k, maxr, ctl: false, fp: true, late: false, len: 0 }
}

fn late(k: K, maxr: u8) -> Insn {
    Insn { k, maxr, ctl: false, fp: false, late: true, len: 0 }
}

fn unknown() -> Insn {
    mk(K::Unknown, 0)
}

fn sx(v: u32, bits: u32) -> i32 {
    let v = v & ((1u64 << bits) - 1) as u32;
    if v >> (bits - 1) != 0 {
        (v as i64 - (1i64 << bits)) as i32
    } else {
        v as i32
    }
}

fn m3(a: u32, b: u32, c: u32) -> u8 {
    a.max(b).max(c) as u8
}

fn m2(a: u32, b: u32) -> u8 {
    a.max(b) as u8
}

fn bin(op: Bin, r: u32, s: u32, t: u32) -> Insn {
    mk(K::Bin { op, r: r as u8, s: s as u8, t: t as u8 }, m3(r, s, t))
}

fn br(cond: Cond, s: u32, x: i32, off: i32, maxr: u8) -> Insn {
    ctl(K::Br { cond, s: s as u8, x, off }, maxr)
}

pub fn decode24(w: u32) -> Insn {
    let (op0, t, s, r) = (w & 15, (w >> 4) & 15, (w >> 8) & 15, (w >> 12) & 15);
    let (op1, op2) = ((w >> 16) & 15, (w >> 20) & 15);
    let imm8 = (w >> 16) & 0xff;
    match op0 {
        0 => qrst(w, op1, op2, r, s, t),
        1 => mk(K::L32r { t: t as u8, off: (0xffff0000 | (w >> 8)) << 2 }, t as u8),
        2 => lsai(r, s, t, imm8),
        3 => lsci(r, s, t, imm8),
        5 => {
            let n = (w >> 4) & 3;
            ctl(K::Call { n: n as u8, off: sx(w >> 6, 18) << 2 }, 4 * n as u8)
        }
        6 => si(w, r, s, t, imm8),
        7 => branch_b(r, s, t, sx(imm8, 8)),
        _ => unknown(),
    }
}

fn branch_b(r: u32, s: u32, t: u32, off: i32) -> Insn {
    if matches!(r, 6 | 7 | 14 | 15) {
        let bit = (((r & 1) << 4) | t) as i32;
        return br(if r < 8 { Cond::Bbci } else { Cond::Bbsi }, s, bit, off, s as u8);
    }
    let cond = match r {
        0 => Cond::Bnone,
        1 => Cond::Beq,
        2 => Cond::Blt,
        3 => Cond::Bltu,
        4 => Cond::Ball,
        5 => Cond::Bbc,
        8 => Cond::Bany,
        9 => Cond::Bne,
        10 => Cond::Bge,
        11 => Cond::Bgeu,
        12 => Cond::Bnall,
        _ => Cond::Bbs,
    };
    br(cond, s, t as i32, off, m2(s, t))
}

/// A FLIX branch whose offset does not fit the core encoding.
pub fn long_branch(m: &str, s: u8, operand: Option<i32>, off: i32) -> Insn {
    let o = operand.unwrap_or(0);
    let (cond, reg) = match m {
        "beqz" => (Cond::Eqz, false),
        "bnez" => (Cond::Nez, false),
        "bltz" => (Cond::Ltz, false),
        "bgez" => (Cond::Gez, false),
        "beqi" => (Cond::Eqi, false),
        "bnei" => (Cond::Nei, false),
        "blti" => (Cond::Lti, false),
        "bgei" => (Cond::Gei, false),
        "bltui" => (Cond::Ltui, false),
        "bgeui" => (Cond::Geui, false),
        "bbci" => (Cond::Bbci, false),
        "bbsi" => (Cond::Bbsi, false),
        "bnone" => (Cond::Bnone, true),
        "beq" => (Cond::Beq, true),
        "blt" => (Cond::Blt, true),
        "bltu" => (Cond::Bltu, true),
        "ball" => (Cond::Ball, true),
        "bany" => (Cond::Bany, true),
        "bne" => (Cond::Bne, true),
        "bge" => (Cond::Bge, true),
        "bgeu" => (Cond::Bgeu, true),
        "bnall" => (Cond::Bnall, true),
        "bbc" => (Cond::Bbc, true),
        _ => (Cond::Bbs, true),
    };
    let maxr = if reg { (s as i32).max(o) as u8 } else { s };
    br(cond, s as u32, o, off, maxr)
}

fn si(w: u32, r: u32, s: u32, _t: u32, imm8: u32) -> Insn {
    let (n, m) = ((w >> 4) & 3, (w >> 6) & 3);
    if n == 0 {
        return ctl(K::J { off: sx(w >> 6, 18) }, 0);
    }
    if n == 1 {
        let cond = [Cond::Eqz, Cond::Nez, Cond::Ltz, Cond::Gez][m as usize];
        return br(cond, s, 0, sx(w >> 12, 12), s as u8);
    }
    let off = sx(imm8, 8);
    if n == 2 {
        let cond = [Cond::Eqi, Cond::Nei, Cond::Lti, Cond::Gei][m as usize];
        return br(cond, s, B4CONST[r as usize], off, s as u8);
    }
    match m {
        0 => ctl(K::Entry { s: s as u8, frame: (w >> 12) << 3 }, 0),
        1 => match r {
            0 | 1 => br(if r == 0 { Cond::Bf } else { Cond::Bt }, s, 0, off, 0),
            8..=10 => ctl(K::Loop { kind: r as u8, s: s as u8, imm8 }, s as u8),
            _ => unknown(),
        },
        2 => br(Cond::Ltui, s, B4CONSTU[r as usize], off, s as u8),
        _ => br(Cond::Geui, s, B4CONSTU[r as usize], off, s as u8),
    }
}

fn load(t: u32, s: u32, off: u32, size: u8, signed: bool) -> Insn {
    mk(K::Load { t: t as u8, s: s as u8, off, size, signed }, m2(s, t))
}

fn store(t: u32, s: u32, off: u32, size: u8) -> Insn {
    mk(K::Store { t: t as u8, s: s as u8, off, size }, m2(s, t))
}

fn lsai(r: u32, s: u32, t: u32, imm8: u32) -> Insn {
    match r {
        0 => load(t, s, imm8, 1, false),
        1 => load(t, s, 2 * imm8, 2, false),
        2 | 11 => load(t, s, 4 * imm8, 4, false), // l32i, l32ai
        9 => load(t, s, 2 * imm8, 2, true),
        4 => store(t, s, imm8, 1),
        5 => store(t, s, 2 * imm8, 2),
        6 | 15 => store(t, s, 4 * imm8, 4), // s32i, s32ri
        7 => mk(K::Nop, s as u8),           // cache operations
        10 => mk(K::Movi { t: t as u8, v: sx((s << 8) | imm8, 12) as u32 }, t as u8),
        12 => mk(K::Addi { t: t as u8, s: s as u8, v: sx(imm8, 8) as u32 }, m2(s, t)),
        13 => mk(K::Addi { t: t as u8, s: s as u8, v: (sx(imm8, 8) << 8) as u32 }, m2(s, t)),
        _ => unknown(),
    }
}

fn lsci(r: u32, s: u32, t: u32, imm8: u32) -> Insn {
    if !matches!(r, 0 | 4 | 8 | 12 | 1 | 5 | 9 | 13) {
        return unknown();
    }
    fp(K::Fp { g: FpGroup::Lsci, op: r as u8, r: r as u8, s: s as u8, t: t as u8, imm: imm8 }, s as u8)
}

fn qrst(w: u32, op1: u32, op2: u32, r: u32, s: u32, t: u32) -> Insn {
    match op1 {
        0 => rst0(w, op2, r, s, t),
        1 => rst1(op2, r, s, t),
        2 => rst2(op2, r, s, t),
        3 => rst3(op2, r, s, t),
        4 | 5 => {
            let sh = ((op1 & 1) << 4) | s;
            mk(K::Extui { r: r as u8, t: t as u8, sh: sh as u8, mask: ((1u64 << (op2 + 1)) - 1) as u32 }, m2(r, t))
        }
        8 => {
            if op2 > 7 {
                return unknown();
            }
            fp(K::Fp { g: FpGroup::Lsx, op: op2 as u8, r: r as u8, s: s as u8, t: t as u8, imm: 0 }, m2(s, t))
        }
        9 => {
            let off = ((r << 2) as i32 - 64) as u32;
            match op2 {
                0 => load(t, s, off, 4, false),  // l32e
                4 => store(t, s, off, 4),        // s32e
                _ => unknown(),
            }
        }
        10 => fp0(op2, r, s, t),
        11 => fp1(op2, r, s, t),
        14 => dfp1(op2, r, s, t),
        15 => dfp0(op2, r, s, t),
        _ => unknown(),
    }
}

fn rst0(w: u32, op2: u32, r: u32, s: u32, t: u32) -> Insn {
    match op2 {
        0 => st0(w, r, s, t),
        1 => bin(Bin::And, r, s, t),
        2 => bin(Bin::Or, r, s, t),
        3 => bin(Bin::Xor, r, s, t),
        4 => st1(r, s, t),
        5 => {
            // TLB / MPU: no effect here; reads return 0
            if matches!(r, 3 | 7 | 11 | 15) {
                mk(K::TlbRead { t: t as u8 }, t as u8)
            } else {
                mk(K::Nop, m2(s, t))
            }
        }
        6 if s == 0 => mk(K::Neg { r: r as u8, t: t as u8 }, m2(r, t)),
        6 if s == 1 => mk(K::Abs { r: r as u8, t: t as u8 }, m2(r, t)),
        8..=11 => bin(Bin::Addx((op2 & 3) as u8), r, s, t),
        12..=15 => bin(Bin::Subx((op2 & 3) as u8), r, s, t),
        _ => unknown(),
    }
}

fn st0(_w: u32, r: u32, s: u32, t: u32) -> Insn {
    match r {
        0 => match t {
            8 => ctl(K::Ret, 0),
            9 => ctl(K::Retw, 0),
            10 => ctl(K::Jx { s: s as u8 }, s as u8),
            12..=15 => {
                let k = t - 12;
                ctl(K::Callx { k: k as u8, s: s as u8 }, m2(s, 4 * k))
            }
            _ => unknown(),
        },
        1 => ctl(K::Movsp { s: s as u8, t: t as u8 }, m2(s, t)),
        2 => mk(K::Nop, 0), // sync group, NOP
        3 => match (t, s) {
            (0, 0) => ctl(K::Rfe, 0),
            (0, 2) => ctl(K::Rfde, 0),
            (0, 4) => ctl(K::Rfw { under: false }, 0),
            (0, 5) => ctl(K::Rfw { under: true }, 0),
            // CLREX: CIX's image has it only where each interrupt
            // dispatcher hands over to the RTOS
            (2, 1) => mk(K::Clrex, 0),
            (1, _) => ctl(K::Rfi { level: s as u8 }, 0),
            _ => unknown(),
        },
        4 => mk(K::Break { s: s as u8, t: t as u8 }, 0),
        5 => mk(if s == 0 { K::Syscall } else { K::Simcall }, 0),
        6 => mk(K::Rsil { t: t as u8, s: s as u8 }, t as u8),
        7 => mk(K::Waiti { s: s as u8 }, 0),
        8..=11 => mk(K::AnyAll { width: if r < 10 { 4 } else { 8 }, all: r == 9 || r == 11, s: s as u8, t: t as u8 }, 0),
        _ => unknown(),
    }
}

fn st1(r: u32, s: u32, t: u32) -> Insn {
    match r {
        0 => late(K::Ssr { s: s as u8 }, s as u8),
        1 => late(K::Ssl { s: s as u8 }, s as u8),
        2 => late(K::Ssa8l { s: s as u8 }, s as u8),
        3 => late(K::Ssa8b { s: s as u8 }, s as u8),
        4 => late(K::Ssai { v: (s | ((t & 1) << 4)) as u8 }, 0),
        10 => mk(K::Getex { t: t as u8 }, t as u8),
        8 => ctl(K::Rotw { k: sx(t, 4) as i8 }, 0),
        14 => mk(K::Nsa { s: s as u8, t: t as u8 }, m2(s, t)),
        15 => mk(K::Nsau { s: s as u8, t: t as u8 }, m2(s, t)),
        _ => unknown(),
    }
}

fn rst1(op2: u32, r: u32, s: u32, t: u32) -> Insn {
    match op2 {
        0 | 1 => mk(K::Slli { r: r as u8, s: s as u8, sh: (32 - (((op2 & 1) << 4) | t)) as u8 }, m2(r, s)),
        2 | 3 => mk(K::Srai { r: r as u8, t: t as u8, sh: (((op2 & 1) << 4) | s) as u8 }, m2(r, t)),
        4 => mk(K::Srli { r: r as u8, t: t as u8, sh: s as u8 }, m2(r, t)),
        6 => ctl(K::Xsr { sr: (r << 4) | s, t: t as u8 }, t as u8),
        8 => mk(K::Src { r: r as u8, s: s as u8, t: t as u8 }, m3(r, s, t)),
        9 => mk(K::Srl { r: r as u8, t: t as u8 }, m2(r, t)),
        10 => mk(K::Sll { r: r as u8, s: s as u8 }, m2(r, s)),
        11 => mk(K::Sra { r: r as u8, t: t as u8 }, m2(r, t)),
        12 => bin(Bin::Mul16u, r, s, t),
        13 => bin(Bin::Mul16s, r, s, t),
        15 => mk(K::Nop, 0), // IMP (cache tag access): nothing here
        _ => unknown(),
    }
}

fn rst2(op2: u32, r: u32, s: u32, t: u32) -> Insn {
    match op2 {
        0..=4 => mk(K::BoolOp { op: op2 as u8, r: r as u8, s: s as u8, t: t as u8 }, 0),
        6 => bin(Bin::Saltu, r, s, t),
        7 => bin(Bin::Salt, r, s, t),
        8 => bin(Bin::Mull, r, s, t),
        10 => bin(Bin::Muluh, r, s, t),
        11 => bin(Bin::Mulsh, r, s, t),
        12 => bin(Bin::Quou, r, s, t),
        13 => bin(Bin::Quos, r, s, t),
        14 => bin(Bin::Remu, r, s, t),
        15 => bin(Bin::Rems, r, s, t),
        _ => unknown(),
    }
}

fn rst3(op2: u32, r: u32, s: u32, t: u32) -> Insn {
    match op2 {
        0 => mk(K::Rsr { sr: (r << 4) | s, t: t as u8 }, t as u8),
        1 => {
            let sr = (r << 4) | s;
            let mut i = mk(K::Wsr { sr, t: t as u8 }, t as u8);
            i.ctl = matches!(sr, WINDOWBASE | WINDOWSTART | PS | LEND | LBEG | LCOUNT);
            i
        }
        2 => mk(K::Sext { r: r as u8, s: s as u8, bits: (t + 8) as u8 }, m2(r, s)),
        4 => bin(Bin::Min, r, s, t),
        5 => bin(Bin::Max, r, s, t),
        6 => bin(Bin::Minu, r, s, t),
        7 => bin(Bin::Maxu, r, s, t),
        8..=11 => mk(K::MovCond { cond: (op2 - 8) as u8, r: r as u8, s: s as u8, t: t as u8 }, m3(r, s, t)),
        12 | 13 => mk(K::MovB { want: (op2 - 12) as u8, r: r as u8, s: s as u8, t: t as u8 }, m2(r, s)),
        14 => {
            let ur = (s << 4) | t;
            let mut i = mk(K::Rur { ur, r: r as u8 }, r as u8);
            i.fp = ur == FCR || ur == FSR;
            i
        }
        15 => {
            let ur = (r << 4) | s;
            let mut i = mk(K::Wur { ur, t: t as u8 }, t as u8);
            i.fp = ur == FCR || ur == FSR;
            i
        }
        _ => unknown(),
    }
}

fn fpk(g: FpGroup, op: u32, r: u32, s: u32, t: u32) -> K {
    K::Fp { g, op: op as u8, r: r as u8, s: s as u8, t: t as u8, imm: 0 }
}

fn fp0(op2: u32, r: u32, s: u32, t: u32) -> Insn {
    let k = fpk(FpGroup::Fp0, op2, r, s, t);
    match op2 {
        0 | 1 | 2 | 4 | 5 | 6 | 7 => fp(k, 0),
        8 | 9 | 10 | 11 | 14 => fp(k, r as u8),
        12 | 13 => fp(k, s as u8),
        15 => {
            let k = fpk(FpGroup::Fp1op, t, r, s, t);
            match t {
                0 | 1 | 2 | 3 | 6 | 7 | 11 | 12 | 13 | 14 | 15 => fp(k, 0),
                4 => fp(k, r as u8),
                5 => fp(k, s as u8),
                _ => unknown(),
            }
        }
        _ => unknown(),
    }
}

fn fp1(op2: u32, r: u32, s: u32, t: u32) -> Insn {
    let k = fpk(FpGroup::Fp1, op2, r, s, t);
    match op2 {
        1..=7 => fp(k, 0),
        8..=11 => fp(k, t as u8),
        12 | 13 => fp(k, 0),
        _ => unknown(),
    }
}

fn dfp0(op2: u32, r: u32, s: u32, t: u32) -> Insn {
    let k = fpk(FpGroup::Dfp0, op2, r, s, t);
    match op2 {
        0 | 1 | 2 | 4 | 5 | 6 | 7 => fp(k, 0),
        8 | 9 | 10 | 11 | 14 => fp(k, r as u8),
        12 | 13 => fp(k, s as u8),
        15 => {
            let k = fpk(FpGroup::Dfp0op, t, r, s, t);
            match t {
                0 | 1 | 2 | 3 | 6 | 7 | 11 | 12 | 13 | 14 | 15 => fp(k, 0),
                4 => fp(k, r as u8),
                _ => unknown(),
            }
        }
        _ => unknown(),
    }
}

fn dfp1(op2: u32, r: u32, s: u32, t: u32) -> Insn {
    let k = fpk(FpGroup::Dfp1, op2, r, s, t);
    match op2 {
        1..=7 => fp(k, 0),
        8 => fp(k, m2(s, t)),
        _ => unknown(),
    }
}

pub fn decode16(w: u32) -> Insn {
    let (op0, t, s, r) = (w & 15, (w >> 4) & 15, (w >> 8) & 15, (w >> 12) & 15);
    match op0 {
        8 => load(t, s, 4 * r, 4, false),
        9 => store(t, s, 4 * r, 4),
        10 => bin(Bin::Addx(0), r, s, t),
        11 => mk(K::Addi { t: r as u8, s: s as u8, v: if t == 0 { u32::MAX } else { t } }, m2(r, s)),
        12 => {
            if t & 8 == 0 {
                let mut v = (((t & 7) << 4) | r) as i32;
                if v >= 96 {
                    v -= 128;
                }
                return mk(K::Movi { t: s as u8, v: v as u32 }, s as u8);
            }
            let off = (((t & 3) << 4) | r) as i32;
            br(if t & 4 != 0 { Cond::Nez } else { Cond::Eqz }, s, 0, off, s as u8)
        }
        13 => match (r, t) {
            (0, _) => mk(K::Bin { op: Bin::Or, r: t as u8, s: s as u8, t: s as u8 }, m2(s, t)),
            (15, 0) => ctl(K::Ret, 0),
            (15, 1) => ctl(K::Retw, 0),
            (15, 2) => mk(K::BreakN { s: s as u8 }, 0),
            (15, 3) => mk(K::Nop, 0),
            _ => unknown(),
        },
        _ => unknown(),
    }
}

/// The instruction a FLIX slot stands for.
pub fn decode_slot(slot: &Slot) -> Insn {
    match *slot {
        Slot::Branch(m, s, operand, off) => long_branch(m, s, operand, off),
        Slot::Word(w) => {
            if w & 15 >= 8 {
                decode16(w)
            } else {
                decode24(w)
            }
        }
    }
}
