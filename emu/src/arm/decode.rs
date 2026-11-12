// SPDX-License-Identifier: BSD-2-Clause-Patent
//! Thumb-2 decoder: the encodings the SE's and the SFH's images use
//! (ARMv8-M mainline without floating point, DSP or exclusive access).
//! Anything else is an error, never a guess.

/// Data-processing operations (`Insn::ra` of `DpI` and `DpR`).
pub mod dp {
    pub const AND: u8 = 0;
    pub const EOR: u8 = 1;
    pub const SUB: u8 = 2;
    pub const RSB: u8 = 3;
    pub const ADD: u8 = 4;
    pub const ADC: u8 = 5;
    pub const SBC: u8 = 6;
    pub const ORR: u8 = 7;
    pub const BIC: u8 = 8;
    pub const ORN: u8 = 9;
    pub const MOV: u8 = 10;
    pub const MVN: u8 = 11;
    pub const TST: u8 = 12;
    pub const TEQ: u8 = 13;
    pub const CMP: u8 = 14;
    pub const CMN: u8 = 15;
}

// shifts (`Insn::sh`)
pub const LSL: u8 = 0;
pub const LSR: u8 = 1;
pub const ASR: u8 = 2;
pub const ROR: u8 = 3;
pub const RRX: u8 = 4;

// `Insn::s`: whether the flags are set
pub const S_NO: u8 = 0;
pub const S_YES: u8 = 1;
/// 16-bit encodings: outside an IT block only
pub const S_NOT_IT: u8 = 2;

// load/store addressing (`Insn::f`)
pub const F_INDEX: u8 = 1;
pub const F_ADD: u8 = 2;
pub const F_WBACK: u8 = 4;

// load/store size (`Insn::ra`): log2 of the bytes, | SIGNED
pub const SIGNED: u8 = 4;

// extensions (`Insn::ra` of `Ext`)
pub const SXTH: u8 = 0;
pub const UXTH: u8 = 1;
pub const SXTB: u8 = 4;
pub const UXTB: u8 = 5;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Op {
    /// rd = rn OP imm (ra: dp::*; f: carry out of the immediate, 0 keep, 1 clear, 2 set)
    DpI,
    /// rd = rn OP shift(rm, sh, imm)
    DpR,
    /// rd = shift(rn, ra, rm & 0xff)
    ShiftR,
    /// rd = aligned PC + imm (imm: the address)
    Adr,
    Mul,
    Mla,
    Mls,
    /// ra:rd (low:high as in the encoding: rd = RdHi, ra = RdLo)
    Umull,
    Smull,
    Umlal,
    Sdiv,
    Udiv,
    /// rd = half(rn) * half(rm) [+ ra]; f bit 1: high half of rn, bit 0: of rm
    Smulxy,
    Clz,
    Rev,
    Rev16,
    /// rd = [rn +] extend(ror(rm, imm)); ra: SXTH.., rn 15: nothing added
    Ext,
    /// imm: lsb, ra: width; rn 15: clear
    Bfi,
    Ubfx,
    Sbfx,
    /// rd: Rt; ra: size; f: F_*; imm: offset
    LdrI,
    StrI,
    /// offset rm << imm
    LdrR,
    StrR,
    /// imm: the address
    LdrLit,
    /// rd: Rt, ra: Rt2
    LdrdI,
    StrdI,
    /// imm: register mask; f: F_WBACK; rn
    Ldmia,
    Ldmdb,
    Stmia,
    Stmdb,
    /// imm: the target
    B,
    /// ra: the condition
    Bcond,
    Bl,
    BlxR,
    Bx,
    /// f: 1 for CBNZ
    Cbz,
    /// f: 1 for TBH
    Tb,
    /// imm: firstcond:mask
    It,
    Nop,
    Wfi,
    Wfe,
    /// DSB, DMB (f 0), ISB (f 1)
    Barrier,
    /// f: 1 disable; imm: bit 1 I, bit 0 F
    Cps,
    /// imm: SYSm
    Mrs,
    /// imm: SYSm; f: mask
    Msr,
    Svc,
    Udf,
    Bkpt,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Insn {
    pub op: Op,
    /// bytes: 2 or 4
    pub len: u8,
    pub rd: u8,
    pub rn: u8,
    pub rm: u8,
    pub ra: u8,
    pub sh: u8,
    pub s: u8,
    pub f: u8,
    pub imm: u32,
}

impl Insn {
    fn new(op: Op, len: u8) -> Insn {
        Insn { op, len, rd: 0, rn: 0, rm: 0, ra: 0, sh: 0, s: S_NO, f: 0, imm: 0 }
    }

    /// Whether QEMU ends a translation block after this instruction (a
    /// branch, taken or not, or one that changes what the next may do).
    pub fn ends_block(&self) -> bool {
        match self.op {
            Op::B | Op::Bcond | Op::Bl | Op::BlxR | Op::Bx | Op::Cbz | Op::Tb | Op::Wfi | Op::Cps | Op::Msr | Op::Svc
            | Op::Udf | Op::Bkpt => true,
            Op::Barrier => self.f == 1,
            Op::Ldmia | Op::Ldmdb => self.imm & 0x8000 != 0,
            Op::LdrI | Op::LdrR | Op::LdrLit | Op::DpI | Op::DpR | Op::Adr => {
                self.rd == 15 && !(matches!(self.op, Op::DpI | Op::DpR) && self.ra >= dp::TST)
            }
            _ => false,
        }
    }
}

fn sx(v: u32, bits: u32) -> u32 {
    let s = 32 - bits;
    (((v << s) as i32) >> s) as u32
}

/// ThumbExpandImm_C: the value, and what it does to the carry (0 keep, 1 clear, 2 set).
fn expand_imm(imm12: u32) -> (u32, u8) {
    if imm12 >> 10 == 0 {
        let b = imm12 & 0xff;
        let v = match (imm12 >> 8) & 3 {
            0 => b,
            1 => b << 16 | b,
            2 => b << 24 | b << 8,
            _ => b << 24 | b << 16 | b << 8 | b,
        };
        (v, 0)
    } else {
        let v = (0x80 | (imm12 & 0x7f)).rotate_right(imm12 >> 7);
        (v, if v >> 31 != 0 { 2 } else { 1 })
    }
}

/// DecodeImmShift.
fn imm_shift(ty: u32, imm5: u32) -> (u8, u32) {
    match ty {
        0 => (LSL, imm5),
        1 => (LSR, if imm5 == 0 { 32 } else { imm5 }),
        2 => (ASR, if imm5 == 0 { 32 } else { imm5 }),
        _ => {
            if imm5 == 0 {
                (RRX, 1)
            } else {
                (ROR, imm5)
            }
        }
    }
}

/// True if the halfword starts a 32-bit instruction.
pub fn is32(hw: u16) -> bool {
    hw >> 11 >= 0x1d
}

/// Decode the instruction at `pc`: its first halfword, and the second if `is32(hw1)`.
pub fn decode(pc: u32, hw1: u16, hw2: u16) -> Result<Insn, String> {
    let r = if is32(hw1) { decode32(pc, hw1 as u32, hw2 as u32) } else { decode16(pc, hw1 as u32) };
    r.ok_or_else(|| {
        if is32(hw1) {
            format!("unknown instruction {hw1:04x} {hw2:04x} at {pc:#x}")
        } else {
            format!("unknown instruction {hw1:04x} at {pc:#x}")
        }
    })
}

fn decode16(pc: u32, hw: u32) -> Option<Insn> {
    let mut i = Insn::new(Op::Nop, 2);
    let (r0, r3, r6, r8) = ((hw & 7) as u8, (hw >> 3 & 7) as u8, (hw >> 6 & 7) as u8, (hw >> 8 & 7) as u8);
    match hw >> 11 {
        0..=2 => {
            // LSL, LSR, ASR by an immediate; LSL #0 is MOVS
            i.op = Op::DpR;
            i.ra = dp::MOV;
            i.rd = r0;
            i.rm = r3;
            (i.sh, i.imm) = imm_shift(hw >> 11, hw >> 6 & 31);
            i.s = S_NOT_IT;
            if hw >> 6 == 0 {
                i.s = S_YES; // MOVS Rd, Rm: flags in or out of an IT block (not permitted in one)
            }
        }
        3 => {
            i.rd = r0;
            i.rn = r3;
            i.s = S_NOT_IT;
            i.ra = if hw & 0x200 != 0 { dp::SUB } else { dp::ADD };
            if hw & 0x400 != 0 {
                i.op = Op::DpI;
                i.imm = hw >> 6 & 7;
            } else {
                i.op = Op::DpR;
                i.rm = r6;
            }
        }
        4..=7 => {
            i.op = Op::DpI;
            i.rd = r8;
            i.rn = r8;
            i.imm = hw & 0xff;
            i.s = S_NOT_IT;
            i.ra = match hw >> 11 {
                4 => dp::MOV,
                5 => {
                    i.s = S_YES;
                    dp::CMP
                }
                6 => dp::ADD,
                _ => dp::SUB,
            };
        }
        8 => {
            if hw & 0x400 == 0 {
                // data processing
                i.rd = r0;
                i.rn = r0;
                i.rm = r3;
                i.s = S_NOT_IT;
                i.op = Op::DpR;
                match hw >> 6 & 15 {
                    0 => i.ra = dp::AND,
                    1 => i.ra = dp::EOR,
                    n @ (2 | 3 | 4 | 7) => {
                        i.op = Op::ShiftR;
                        i.ra = [0, 0, LSL, LSR, ASR, 0, 0, ROR][n as usize];
                    }
                    5 => i.ra = dp::ADC,
                    6 => i.ra = dp::SBC,
                    8 => {
                        i.ra = dp::TST;
                        i.s = S_YES;
                    }
                    9 => {
                        // RSBS Rd, Rm, #0
                        i.op = Op::DpI;
                        i.ra = dp::RSB;
                        i.rn = r3;
                        i.imm = 0;
                    }
                    10 => {
                        i.ra = dp::CMP;
                        i.s = S_YES;
                    }
                    11 => {
                        i.ra = dp::CMN;
                        i.s = S_YES;
                    }
                    12 => i.ra = dp::ORR,
                    13 => {
                        i.op = Op::Mul;
                        i.rn = r3;
                        i.rm = r0;
                    }
                    14 => i.ra = dp::BIC,
                    _ => i.ra = dp::MVN,
                }
            } else {
                // special data and branch exchange
                let rdn = ((hw >> 4 & 8) | (hw & 7)) as u8;
                let rm = (hw >> 3 & 15) as u8;
                match hw >> 8 & 3 {
                    0 => {
                        i.op = Op::DpR;
                        i.ra = dp::ADD;
                        i.rd = rdn;
                        i.rn = rdn;
                        i.rm = rm;
                    }
                    1 => {
                        i.op = Op::DpR;
                        i.ra = dp::CMP;
                        i.rn = rdn;
                        i.rm = rm;
                        i.s = S_YES;
                    }
                    2 => {
                        i.op = Op::DpR;
                        i.ra = dp::MOV;
                        i.rd = rdn;
                        i.rm = rm;
                    }
                    _ => {
                        if hw & 7 != 0 {
                            return None;
                        }
                        i.op = if hw & 0x80 != 0 { Op::BlxR } else { Op::Bx };
                        i.rm = rm;
                    }
                }
            }
        }
        9 => {
            i.op = Op::LdrLit;
            i.rd = r8;
            i.ra = 2;
            i.imm = ((pc + 4) & !3) + ((hw & 0xff) << 2);
        }
        10 | 11 => {
            // register offset
            let opb = hw >> 9 & 7;
            i.op = if opb < 3 { Op::StrR } else { Op::LdrR };
            i.ra = [2, 1, 0, SIGNED, 2, 1, 0, 1 | SIGNED][opb as usize];
            i.rd = r0;
            i.rn = r3;
            i.rm = r6;
            i.f = F_INDEX | F_ADD;
        }
        12..=17 => {
            let imm5 = hw >> 6 & 31;
            i.op = if hw & 0x800 != 0 { Op::LdrI } else { Op::StrI };
            i.rd = r0;
            i.rn = r3;
            i.f = F_INDEX | F_ADD;
            (i.ra, i.imm) = match hw >> 12 {
                6 => (2, imm5 << 2),
                7 => (0, imm5),
                _ => (1, imm5 << 1),
            };
        }
        18 | 19 => {
            i.op = if hw & 0x800 != 0 { Op::LdrI } else { Op::StrI };
            i.rd = r8;
            i.rn = 13;
            i.ra = 2;
            i.imm = (hw & 0xff) << 2;
            i.f = F_INDEX | F_ADD;
        }
        20 => {
            i.op = Op::Adr;
            i.rd = r8;
            i.imm = ((pc + 4) & !3) + ((hw & 0xff) << 2);
        }
        21 => {
            i.op = Op::DpI;
            i.ra = dp::ADD;
            i.rd = r8;
            i.rn = 13;
            i.imm = (hw & 0xff) << 2;
        }
        22 | 23 => match hw >> 8 & 15 {
            0 => {
                i.op = Op::DpI;
                i.ra = if hw & 0x80 != 0 { dp::SUB } else { dp::ADD };
                i.rd = 13;
                i.rn = 13;
                i.imm = (hw & 0x7f) << 2;
            }
            1 | 3 | 9 | 11 => {
                i.op = Op::Cbz;
                i.f = (hw >> 11 & 1) as u8;
                i.rn = r0;
                i.imm = pc + 4 + ((hw >> 9 & 1) << 6 | (hw >> 3 & 31) << 1);
            }
            2 => {
                i.op = Op::Ext;
                i.ra = [SXTH, SXTB, UXTH, UXTB][(hw >> 6 & 3) as usize];
                i.rd = r0;
                i.rm = r3;
                i.rn = 15;
            }
            4 | 5 => {
                i.op = Op::Stmdb;
                i.rn = 13;
                i.f = F_WBACK;
                i.imm = (hw & 0xff) | (hw & 0x100) << 6;
            }
            6 => {
                if hw & 0xffec != 0xb660 {
                    return None;
                }
                i.op = Op::Cps;
                i.f = (hw >> 4 & 1) as u8;
                i.imm = hw & 3;
            }
            10 => {
                i.op = match hw >> 6 & 3 {
                    0 => Op::Rev,
                    1 => Op::Rev16,
                    _ => return None,
                };
                i.rd = r0;
                i.rm = r3;
            }
            12 | 13 => {
                i.op = Op::Ldmia;
                i.rn = 13;
                i.f = F_WBACK;
                i.imm = (hw & 0xff) | (hw & 0x100) << 7;
            }
            14 => {
                i.op = Op::Bkpt;
                i.imm = hw & 0xff;
            }
            15 => {
                if hw & 15 != 0 {
                    i.op = Op::It;
                    i.imm = hw & 0xff;
                } else {
                    i.op = match hw >> 4 & 15 {
                        0 | 1 => Op::Nop,
                        2 => Op::Wfe,
                        3 => Op::Wfi,
                        _ => return None,
                    };
                }
            }
            _ => return None,
        },
        24 => {
            i.op = Op::Stmia;
            i.rn = r8;
            i.f = F_WBACK;
            i.imm = hw & 0xff;
        }
        25 => {
            i.op = Op::Ldmia;
            i.rn = r8;
            i.imm = hw & 0xff;
            i.f = if i.imm >> r8 & 1 != 0 { 0 } else { F_WBACK };
        }
        26 | 27 => {
            let cond = (hw >> 8 & 15) as u8;
            match cond {
                14 => {
                    i.op = Op::Udf;
                    i.imm = hw & 0xff;
                }
                15 => {
                    i.op = Op::Svc;
                    i.imm = hw & 0xff;
                }
                _ => {
                    i.op = Op::Bcond;
                    i.ra = cond;
                    i.imm = (pc + 4).wrapping_add(sx((hw & 0xff) << 1, 9));
                }
            }
        }
        28 => {
            i.op = Op::B;
            i.imm = (pc + 4).wrapping_add(sx((hw & 0x7ff) << 1, 12));
        }
        _ => return None,
    }
    Some(i)
}

fn decode32(pc: u32, hw1: u32, hw2: u32) -> Option<Insn> {
    let mut i = Insn::new(Op::Nop, 4);
    let rn = (hw1 & 15) as u8;
    let rd = (hw2 >> 8 & 15) as u8;
    let rt = (hw2 >> 12 & 15) as u8;
    let rm = (hw2 & 15) as u8;
    i.rn = rn;
    if hw1 & 0xfe40 == 0xe800 {
        // load and store multiple
        let (w, l) = (hw1 & 0x20 != 0, hw1 & 0x10 != 0);
        i.op = match (hw1 >> 7 & 3, l) {
            (1, false) => Op::Stmia,
            (1, true) => Op::Ldmia,
            (2, false) => Op::Stmdb,
            (2, true) => Op::Ldmdb,
            _ => return None,
        };
        i.imm = hw2;
        i.f = if w { F_WBACK } else { 0 };
        let bad = if l { 0x2000 } else { 0xa000 };
        if hw2 & bad != 0 || rn == 15 || hw2.count_ones() < 2 || (w && hw2 >> rn & 1 != 0) {
            return None;
        }
    } else if hw1 & 0xfe40 == 0xe840 {
        // load and store double, table branch
        if hw1 & 0x0120 == 0 {
            if hw1 & 0xfff0 != 0xe8d0 || hw2 & 0xffe0 != 0xf000 {
                return None;
            }
            i.op = Op::Tb;
            i.f = (hw2 >> 4 & 1) as u8;
            i.rm = rm;
        } else {
            i.op = if hw1 & 0x10 != 0 { Op::LdrdI } else { Op::StrdI };
            i.rd = rt;
            i.ra = rd;
            i.imm = (hw2 & 0xff) << 2;
            i.f = puw(hw1 >> 8 & 1, hw1 >> 7 & 1, hw1 >> 5 & 1);
            if rn == 15 || rt == 15 || rd == 15 || rt == 13 || rd == 13 {
                return None;
            }
            if i.f & F_WBACK != 0 && (rn == rt || rn == rd) {
                return None;
            }
        }
    } else if hw1 & 0xfe00 == 0xea00 {
        // data processing, shifted register
        let s = hw1 & 0x10 != 0;
        i.op = Op::DpR;
        i.rd = rd;
        i.rm = rm;
        i.s = s as u8;
        (i.sh, i.imm) = imm_shift(hw2 >> 4 & 3, (hw2 >> 12 & 7) << 2 | (hw2 >> 6 & 3));
        if hw2 & 0x8000 != 0 {
            return None;
        }
        i.ra = dp_op(hw1 >> 5 & 15, rd, rn, s)?;
    } else if hw1 & 0xf800 == 0xf000 && hw2 & 0x8000 == 0 {
        let imm12 = (hw1 >> 10 & 1) << 11 | (hw2 >> 12 & 7) << 8 | (hw2 & 0xff);
        i.rd = rd;
        if hw1 & 0x200 == 0 {
            // data processing, modified immediate
            let s = hw1 & 0x10 != 0;
            i.op = Op::DpI;
            i.s = s as u8;
            (i.imm, i.f) = expand_imm(imm12);
            i.ra = dp_op(hw1 >> 5 & 15, rd, rn, s)?;
        } else {
            // plain binary immediate
            let lsb = (hw2 >> 12 & 7) << 2 | (hw2 >> 6 & 3);
            let msb = hw2 & 31;
            match hw1 >> 4 & 31 {
                0 | 10 => {
                    let sub = hw1 & 0x80 != 0;
                    if rn == 15 {
                        i.op = Op::Adr;
                        let base = (pc + 4) & !3;
                        i.imm = if sub { base.wrapping_sub(imm12) } else { base + imm12 };
                    } else {
                        i.op = Op::DpI;
                        i.ra = if sub { dp::SUB } else { dp::ADD };
                        i.imm = imm12;
                    }
                }
                4 => {
                    i.op = Op::DpI;
                    i.ra = dp::MOV;
                    i.imm = (hw1 & 15) << 12 | imm12;
                }
                20 | 28 => {
                    i.op = if hw1 & 0x80 != 0 { Op::Ubfx } else { Op::Sbfx };
                    i.imm = lsb;
                    i.ra = (msb + 1) as u8;
                    if lsb + msb + 1 > 32 || hw2 & 0x20 != 0 {
                        return None;
                    }
                }
                22 => {
                    i.op = Op::Bfi;
                    i.imm = lsb;
                    if msb < lsb || hw2 & 0x20 != 0 {
                        return None;
                    }
                    i.ra = (msb - lsb + 1) as u8;
                }
                _ => return None,
            }
            if hw1 & 0x400 != 0 && !matches!(hw1 >> 4 & 31, 0 | 4 | 10) {
                return None;
            }
        }
        if i.rd == 15 && !(i.op == Op::DpI && i.ra >= dp::TST) {
            return None;
        }
    } else if hw1 & 0xf800 == 0xf000 {
        // branches and miscellaneous control
        let s = hw1 >> 10 & 1;
        let (j1, j2) = (hw2 >> 13 & 1, hw2 >> 11 & 1);
        match (hw2 >> 14 & 1, hw2 >> 12 & 1) {
            (0, 0) => {
                let cond = hw1 >> 6 & 15;
                if cond < 14 {
                    i.op = Op::Bcond;
                    i.ra = cond as u8;
                    let imm = s << 20 | j2 << 19 | j1 << 18 | (hw1 & 0x3f) << 12 | (hw2 & 0x7ff) << 1;
                    i.imm = (pc + 4).wrapping_add(sx(imm, 21));
                } else if hw1 & 0xffe0 == 0xf380 && hw2 & 0xf300 == 0x8000 {
                    i.op = Op::Msr;
                    i.imm = hw2 & 0xff;
                    i.f = (hw2 >> 10 & 3) as u8;
                } else if hw1 == 0xf3af && hw2 == 0x8000 {
                    i.op = Op::Nop;
                } else if hw1 == 0xf3bf && hw2 & 0xff0f == 0x8f0f {
                    i.op = Op::Barrier;
                    i.f = match hw2 >> 4 & 15 {
                        4 | 5 => 0,
                        6 => 1,
                        _ => return None,
                    };
                } else if hw1 == 0xf3ef && hw2 & 0xf000 == 0x8000 {
                    i.op = Op::Mrs;
                    i.rd = rd;
                    i.imm = hw2 & 0xff;
                } else if hw1 & 0xfff0 == 0xf7f0 && hw2 & 0xf000 == 0xa000 {
                    i.op = Op::Udf;
                    i.imm = (hw1 & 15) << 12 | (hw2 & 0xfff);
                } else {
                    return None;
                }
            }
            (b14, 1) => {
                let (i1, i2) = (!(j1 ^ s) & 1, !(j2 ^ s) & 1);
                let imm = s << 24 | i1 << 23 | i2 << 22 | (hw1 & 0x3ff) << 12 | (hw2 & 0x7ff) << 1;
                i.op = if b14 != 0 { Op::Bl } else { Op::B };
                i.imm = (pc + 4).wrapping_add(sx(imm, 25));
            }
            _ => return None,
        }
    } else if hw1 & 0xfe00 == 0xf800 {
        // load and store single
        let load = hw1 & 0x10 != 0;
        let size = (hw1 >> 5 & 3) as u8;
        let signed = hw1 & 0x100 != 0;
        if size == 3 || (signed && (!load || size == 2)) {
            return None;
        }
        i.ra = size | if signed { SIGNED } else { 0 };
        i.rd = rt;
        if rt == 15 && !(load && size == 2) {
            return None; // PLD, PLI
        }
        if rn == 15 {
            if !load {
                return None;
            }
            i.op = Op::LdrLit;
            let base = (pc + 4) & !3;
            i.imm = if hw1 & 0x80 != 0 { base + (hw2 & 0xfff) } else { base.wrapping_sub(hw2 & 0xfff) };
        } else if hw1 & 0x80 != 0 {
            i.op = if load { Op::LdrI } else { Op::StrI };
            i.imm = hw2 & 0xfff;
            i.f = F_INDEX | F_ADD;
        } else if hw2 & 0x800 != 0 {
            i.op = if load { Op::LdrI } else { Op::StrI };
            i.imm = hw2 & 0xff;
            i.f = puw(hw2 >> 10 & 1, hw2 >> 9 & 1, hw2 >> 8 & 1);
            // (index and add without write-back is the unprivileged form: not used)
            if i.f & (F_INDEX | F_WBACK) == 0 || (i.f & F_WBACK != 0 && rn == rt) || hw2 >> 8 & 7 == 6 {
                return None;
            }
        } else if hw2 & 0xfc0 == 0 {
            i.op = if load { Op::LdrR } else { Op::StrR };
            i.rm = rm;
            i.imm = hw2 >> 4 & 3;
            i.f = F_INDEX | F_ADD;
            if rm == 13 || rm == 15 {
                return None;
            }
        } else {
            return None;
        }
    } else if hw1 & 0xff00 == 0xfa00 {
        // data processing, register
        i.rd = rd;
        i.rm = rm;
        if hw2 & 0xf000 != 0xf000 || rd == 13 || rd == 15 || rm == 13 || rm == 15 {
            return None;
        }
        if hw1 & 0x80 == 0 && hw2 & 0xf0 == 0 {
            i.op = Op::ShiftR;
            i.ra = (hw1 >> 5 & 3) as u8;
            i.s = (hw1 >> 4 & 1) as u8;
            if rn == 13 || rn == 15 {
                return None;
            }
        } else if hw1 & 0x80 == 0 && hw2 & 0xc0 == 0x80 {
            i.op = Op::Ext;
            i.ra = match hw1 >> 4 & 7 {
                0 => SXTH,
                1 => UXTH,
                4 => SXTB,
                5 => UXTB,
                _ => return None,
            };
            i.imm = (hw2 >> 4 & 3) * 8;
            if rn == 13 {
                return None;
            }
        } else if hw1 & 0xfff0 == 0xfa90 && hw2 & 0xc0 == 0x80 && rn == rm {
            i.op = match hw2 >> 4 & 3 {
                0 => Op::Rev,
                1 => Op::Rev16,
                _ => return None,
            };
        } else if hw1 & 0xfff0 == 0xfab0 && hw2 & 0xf0 == 0x80 && rn == rm {
            i.op = Op::Clz;
        } else {
            return None;
        }
    } else if hw1 & 0xff80 == 0xfb00 {
        // multiply
        i.rd = rd;
        i.rm = rm;
        i.ra = rt;
        if rd == 13 || rd == 15 || rn == 13 || rn == 15 || rm == 13 || rm == 15 || rt == 13 {
            return None;
        }
        match (hw1 >> 4 & 7, hw2 >> 4 & 15) {
            (0, 0) => i.op = if rt == 15 { Op::Mul } else { Op::Mla },
            (0, 1) => {
                i.op = Op::Mls;
                if rt == 15 {
                    return None;
                }
            }
            (1, x) if x < 4 => {
                i.op = Op::Smulxy;
                i.f = x as u8;
            }
            _ => return None,
        }
    } else if hw1 & 0xff80 == 0xfb80 {
        // long multiply, divide
        i.rd = rd;
        i.rm = rm;
        i.ra = rt;
        if rd == 13 || rd == 15 || rn == 13 || rn == 15 || rm == 13 || rm == 15 || rt == 13 {
            return None;
        }
        i.op = match (hw1 >> 4 & 7, hw2 >> 4 & 15) {
            (0, 0) => Op::Smull,
            (1, 15) => Op::Sdiv,
            (2, 0) => Op::Umull,
            (3, 15) => Op::Udiv,
            (6, 0) => Op::Umlal,
            _ => return None,
        };
        let long = matches!(i.op, Op::Smull | Op::Umull | Op::Umlal);
        if long && (rt == 15 || rt == rd) || !long && rt != 15 {
            return None;
        }
    } else {
        return None;
    }
    Some(i)
}

fn puw(p: u32, u: u32, w: u32) -> u8 {
    (if p != 0 { F_INDEX } else { 0 }) | (if u != 0 { F_ADD } else { 0 }) | (if w != 0 { F_WBACK } else { 0 })
}

/// The operation of a 32-bit data-processing encoding (None: not one this decoder has).
fn dp_op(op: u32, rd: u8, rn: u8, s: bool) -> Option<u8> {
    let test = rd == 15;
    if test && !s {
        return None;
    }
    Some(match op {
        0 if test => dp::TST,
        0 => dp::AND,
        1 if !test => dp::BIC,
        2 if !test && rn == 15 => dp::MOV,
        2 if !test => dp::ORR,
        3 if !test && rn == 15 => dp::MVN,
        3 if !test => dp::ORN,
        4 if test => dp::TEQ,
        4 => dp::EOR,
        8 if test => dp::CMN,
        8 => dp::ADD,
        10 if !test => dp::ADC,
        11 if !test => dp::SBC,
        13 if test => dp::CMP,
        13 => dp::SUB,
        14 if !test => dp::RSB,
        _ => return None,
    })
}
