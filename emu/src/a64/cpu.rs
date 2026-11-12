// SPDX-License-Identifier: BSD-2-Clause-Patent
//! An AArch64 interpreter for the boot stages that run at EL3 (TF-A BL2 and
//! BL31): the integer instruction set, the system registers they touch,
//! stage-1 translation of the EL3 regime.
//!
//! What it does not have is deliberate, and a hard stop when met: FP and
//! SIMD (the images are built without), any encoding or system register
//! not listed here, exceptions taken to EL3 from EL3 (a fault stops the
//! run and says where: the firmware's own handler would only print a
//! crash dump).  Lower exception levels do not run here: an ERET below EL3
//! stops the run and the harness plays the lower level (`enter` takes its
//! next SMC or interrupt to the vector).
//!
//! An instruction is atomic: every address it touches is translated and
//! every value it reads is read before any register or memory changes, so
//! a stop leaves the state as it was before the instruction.

use std::collections::BTreeMap;

/// What the interpreter runs on: physical memory and devices, the system
/// registers that belong to the machine rather than the core (timers, the
/// GIC CPU interface), and time.
pub trait Bus {
    /// Read `size` (1, 2, 4, 8) bytes at a physical address; None: nothing answers there.
    fn read(&mut self, core: usize, addr: u64, size: u32) -> Option<u64>;
    /// Write; false: nothing answers there.
    fn write(&mut self, core: usize, addr: u64, size: u32, val: u64) -> bool;
    /// A system register the machine owns: Some(value) (for a write: Some(anything)) when it is one.
    fn sysreg(&mut self, core: usize, key: u16, write: Option<u64>) -> Option<u64>;
    /// One instruction's worth of time has passed.
    fn tick(&mut self);
}

pub const fn key(op0: u32, op1: u32, crn: u32, crm: u32, op2: u32) -> u16 {
    (((op0 & 3) << 14) | (op1 << 11) | (crn << 7) | (crm << 3) | op2) as u16
}

pub const NZCV: u16 = key(3, 3, 4, 2, 0);
pub const DAIF: u16 = key(3, 3, 4, 2, 1);
pub const SSBS: u16 = key(3, 3, 4, 2, 6);
pub const CURRENT_EL: u16 = key(3, 0, 4, 2, 2);
pub const SPSEL: u16 = key(3, 0, 4, 0, 0);
pub const SP_EL0: u16 = key(3, 0, 4, 1, 0);
pub const SP_EL1: u16 = key(3, 4, 4, 1, 0);
pub const SP_EL2: u16 = key(3, 6, 4, 1, 0);
pub const ELR_EL3: u16 = key(3, 6, 4, 0, 1);
pub const SPSR_EL3: u16 = key(3, 6, 4, 0, 0);
pub const ESR_EL3: u16 = key(3, 6, 5, 2, 0);
pub const SCTLR_EL3: u16 = key(3, 6, 1, 0, 0);
pub const SCR_EL3: u16 = key(3, 6, 1, 1, 0);
pub const TTBR0_EL3: u16 = key(3, 6, 2, 0, 0);
pub const TCR_EL3: u16 = key(3, 6, 2, 0, 2);
pub const MAIR_EL3: u16 = key(3, 6, 10, 2, 0);
pub const VBAR_EL3: u16 = key(3, 6, 12, 0, 0);
pub const MIDR_EL1: u16 = key(3, 0, 0, 0, 0);
pub const MPIDR_EL1: u16 = key(3, 0, 0, 0, 5);
pub const DCZID_EL0: u16 = key(3, 3, 0, 0, 7);

/// The system registers the interpreter knows, with the reset value of a
/// Cortex-A720 where one matters to TF-A (the identification registers:
/// from the core's reference manual; the Sky1's cores also have SVE, SPE,
/// TRBE, AMU, MPAM and RAS, which is what makes BL31 take those paths).
/// Everything else is storage that reads back what was written.  A register
/// not in this table stops the run.
pub const SYSREGS: &[(&str, u16, u64)] = &[
    ("MIDR_EL1", MIDR_EL1, 0x410f_d811),
    ("MPIDR_EL1", MPIDR_EL1, 0x8100_0000),
    ("ID_AA64PFR0_EL1", key(3, 0, 0, 4, 0), 0x1201_2111_2311_1111),
    ("ID_AA64PFR1_EL1", key(3, 0, 0, 4, 1), 0x321),
    ("ID_AA64DFR0_EL1", key(3, 0, 0, 5, 0), 0x0000_1103_1030_571a),
    ("ID_AA64MMFR0_EL1", key(3, 0, 0, 7, 0), 0x2100_0222_0010_1122),
    ("ID_AA64MMFR1_EL1", key(3, 0, 0, 7, 1), 0x0111_1121_1031_2122),
    ("ID_AA64MMFR2_EL1", key(3, 0, 0, 7, 2), 0x1221_0111_1210_1011),
    ("SCTLR_EL1", key(3, 0, 1, 0, 0), 0x30d0_0800),
    ("ACTLR_EL1", key(3, 0, 1, 0, 1), 0),
    ("CPACR_EL1", key(3, 0, 1, 0, 2), 0),
    ("TTBR0_EL1", key(3, 0, 2, 0, 0), 0),
    ("TTBR1_EL1", key(3, 0, 2, 0, 1), 0),
    ("TCR_EL1", key(3, 0, 2, 0, 2), 0),
    ("SPSR_EL1", key(3, 0, 4, 0, 0), 0),
    ("ELR_EL1", key(3, 0, 4, 0, 1), 0),
    ("ICC_PMR_EL1", key(3, 0, 4, 6, 0), 0),
    ("AFSR0_EL1", key(3, 0, 5, 1, 0), 0),
    ("AFSR1_EL1", key(3, 0, 5, 1, 1), 0),
    ("ESR_EL1", key(3, 0, 5, 2, 0), 0),
    ("ERRIDR_EL1", key(3, 0, 5, 3, 0), 2),
    ("ERRSELR_EL1", key(3, 0, 5, 3, 1), 0),
    ("ERXFR_EL1", key(3, 0, 5, 4, 0), 0),
    ("ERXCTLR_EL1", key(3, 0, 5, 4, 1), 0),
    ("ERXSTATUS_EL1", key(3, 0, 5, 4, 2), 0),
    ("ERXADDR_EL1", key(3, 0, 5, 4, 3), 0),
    ("ERXMISC0_EL1", key(3, 0, 5, 5, 0), 0),
    ("FAR_EL1", key(3, 0, 6, 0, 0), 0),
    ("PAR_EL1", key(3, 0, 7, 4, 0), 0),
    ("PMBLIMITR_EL1", key(3, 0, 9, 10, 0), 0),
    ("MAIR_EL1", key(3, 0, 10, 2, 0), 0),
    ("AMAIR_EL1", key(3, 0, 10, 3, 0), 0),
    ("MPAMIDR_EL1", key(3, 0, 10, 4, 4), 0x1_001e_003f),
    ("VBAR_EL1", key(3, 0, 12, 0, 0), 0),
    ("ISR_EL1", key(3, 0, 12, 1, 0), 0),
    ("DISR_EL1", key(3, 0, 12, 1, 1), 0),
    ("ICC_IAR0_EL1", key(3, 0, 12, 8, 0), 1023),
    ("ICC_EOIR0_EL1", key(3, 0, 12, 8, 1), 0),
    ("ICC_HPPIR0_EL1", key(3, 0, 12, 8, 2), 1023),
    ("ICC_RPR_EL1", key(3, 0, 12, 11, 3), 0xff),
    ("ICC_SGI0R_EL1", key(3, 0, 12, 11, 7), 0),
    ("ICC_HPPIR1_EL1", key(3, 0, 12, 12, 2), 1023),
    ("ICC_SRE_EL1", key(3, 0, 12, 12, 5), 0),
    ("ICC_IGRPEN0_EL1", key(3, 0, 12, 12, 6), 0),
    ("CONTEXTIDR_EL1", key(3, 0, 13, 0, 1), 0),
    ("TPIDR_EL1", key(3, 0, 13, 0, 4), 0),
    ("CNTKCTL_EL1", key(3, 0, 14, 1, 0), 0),
    // the cores' own (IMPLEMENTATION DEFINED) registers, by their encoding
    ("S3_0_C15_C2_7", key(3, 0, 15, 2, 7), 0),
    ("S3_0_C15_C3_3", key(3, 0, 15, 3, 3), 0),
    ("S3_0_C15_C3_5", key(3, 0, 15, 3, 5), 0),
    ("S3_0_C15_C3_6", key(3, 0, 15, 3, 6), 0),
    ("S3_0_C15_C4_0", key(3, 0, 15, 4, 0), 0),
    ("S3_0_C15_C4_1", key(3, 0, 15, 4, 1), 0),
    ("S3_0_C15_C4_2", key(3, 0, 15, 4, 2), 0),
    ("S3_0_C15_C4_3", key(3, 0, 15, 4, 3), 0),
    ("S3_0_C15_C4_5", key(3, 0, 15, 4, 5), 0),
    ("S3_0_C15_C4_6", key(3, 0, 15, 4, 6), 0),
    ("S3_0_C15_C9_3", key(3, 0, 15, 9, 3), 0),
    ("CSSELR_EL1", key(3, 2, 0, 0, 0), 0),
    ("CTR_EL0", key(3, 3, 0, 0, 1), 0x9444_c004),
    ("DCZID_EL0", DCZID_EL0, 4),
    ("PMCR_EL0", key(3, 3, 9, 12, 0), 0x4100_3000),
    ("TPIDR_EL0", key(3, 3, 13, 0, 2), 0),
    ("TPIDRRO_EL0", key(3, 3, 13, 0, 3), 0),
    ("AMCR_EL0", key(3, 3, 13, 2, 0), 0),
    ("AMCFGR_EL0", key(3, 3, 13, 2, 1), 0x1000_3f06),
    ("AMCGCR_EL0", key(3, 3, 13, 2, 2), 0x0304),
    ("AMCNTENCLR0_EL0", key(3, 3, 13, 2, 4), 0),
    ("AMCNTENSET0_EL0", key(3, 3, 13, 2, 5), 0),
    ("AMCG1IDR_EL0", key(3, 3, 13, 2, 6), 0x0007),
    ("AMCNTENCLR1_EL0", key(3, 3, 13, 3, 0), 0),
    ("AMCNTENSET1_EL0", key(3, 3, 13, 3, 1), 0),
    ("CNTFRQ_EL0", key(3, 3, 14, 0, 0), 0),
    ("CNTPCT_EL0", key(3, 3, 14, 0, 1), 0),
    ("CNTP_CTL_EL0", key(3, 3, 14, 2, 1), 0),
    ("CNTP_CVAL_EL0", key(3, 3, 14, 2, 2), 0),
    ("CNTV_CTL_EL0", key(3, 3, 14, 3, 1), 0),
    ("CNTV_CVAL_EL0", key(3, 3, 14, 3, 2), 0),
    ("VPIDR_EL2", key(3, 4, 0, 0, 0), 0),
    ("VMPIDR_EL2", key(3, 4, 0, 0, 5), 0),
    ("SCTLR_EL2", key(3, 4, 1, 0, 0), 0x30c5_0830),
    ("ACTLR_EL2", key(3, 4, 1, 0, 1), 0),
    ("HCR_EL2", key(3, 4, 1, 1, 0), 0),
    ("MDCR_EL2", key(3, 4, 1, 1, 1), 0),
    ("CPTR_EL2", key(3, 4, 1, 1, 2), 0x33ff),
    ("HSTR_EL2", key(3, 4, 1, 1, 3), 0),
    ("VTTBR_EL2", key(3, 4, 2, 1, 0), 0),
    ("SPSR_EL2", key(3, 4, 4, 0, 0), 0),
    ("ELR_EL2", key(3, 4, 4, 0, 1), 0),
    ("ESR_EL2", key(3, 4, 5, 2, 0), 0),
    ("FAR_EL2", key(3, 4, 6, 0, 0), 0),
    ("MPAMHCR_EL2", key(3, 4, 10, 4, 0), 0),
    ("MPAM2_EL2", key(3, 4, 10, 5, 0), 0),
    ("ICC_SRE_EL2", key(3, 4, 12, 9, 5), 0),
    ("CNTVOFF_EL2", key(3, 4, 14, 0, 3), 0),
    ("CNTHCTL_EL2", key(3, 4, 14, 1, 0), 0),
    ("CNTHP_CTL_EL2", key(3, 4, 14, 2, 1), 0),
    ("SCTLR_EL3", SCTLR_EL3, 0x30c5_0830),
    ("ACTLR_EL3", key(3, 6, 1, 0, 1), 0),
    ("SCR_EL3", SCR_EL3, 0),
    ("CPTR_EL3", key(3, 6, 1, 1, 2), 0),
    ("ZCR_EL3", key(3, 6, 1, 2, 0), 0),
    ("MDCR_EL3", key(3, 6, 1, 3, 1), 0),
    ("TTBR0_EL3", TTBR0_EL3, 0),
    ("TCR_EL3", TCR_EL3, 0),
    ("SPSR_EL3", SPSR_EL3, 0),
    ("ELR_EL3", ELR_EL3, 0),
    ("ESR_EL3", ESR_EL3, 0),
    ("MAIR_EL3", MAIR_EL3, 0),
    ("MPAM3_EL3", key(3, 6, 10, 5, 0), 0),
    ("VBAR_EL3", VBAR_EL3, 0),
    ("ICC_SRE_EL3", key(3, 6, 12, 12, 5), 0),
    ("ICC_IGRPEN1_EL3", key(3, 6, 12, 12, 7), 0),
    ("TPIDR_EL3", key(3, 6, 13, 0, 2), 0),
    ("S3_6_C15_C2_0", key(3, 6, 15, 2, 0), 0),
    ("S3_6_C15_C2_1", key(3, 6, 15, 2, 1), 0),
    ("S3_6_C15_C2_6", key(3, 6, 15, 2, 6), 0),
];

/// A register's name: from the table, the activity monitors' counters by
/// their pattern, or the generic S<op0>_<op1>_C<n>_C<m>_<op2>.
pub fn sysreg_name(k: u16) -> String {
    if let Some(n) = [(NZCV, "NZCV"), (DAIF, "DAIF"), (SSBS, "SSBS"), (CURRENT_EL, "CurrentEL"), (SPSEL, "SPSel")]
        .iter()
        .find(|e| e.0 == k)
    {
        return n.1.to_string();
    }
    if let Some(n) = [(SP_EL0, "SP_EL0"), (SP_EL1, "SP_EL1"), (SP_EL2, "SP_EL2")].iter().find(|e| e.0 == k) {
        return n.1.to_string();
    }
    if let Some(e) = SYSREGS.iter().find(|e| e.1 == k) {
        return e.0.to_string();
    }
    let (op0, op1, crn, crm, op2) = (2 | (k >> 14) & 1, (k >> 11) & 7, (k >> 7) & 15, (k >> 3) & 15, k & 7);
    if is_amu_counter(k) {
        return format!("AMEV_S{op0}_{op1}_C{crn}_C{crm}_{op2}");
    }
    format!("S{op0}_{op1}_C{crn}_C{crm}_{op2}")
}

/// AMEVCNTR0<n>_EL0, AMEVCNTR1<n>_EL0 and their EL2 offsets: counters, storage here.
fn is_amu_counter(k: u16) -> bool {
    let (op0, op1, crn, crm) = ((k >> 14) & 3, (k >> 11) & 7, (k >> 7) & 15, (k >> 3) & 15);
    op0 == 3 && crn == 13 && ((op1 == 3 && matches!(crm, 4 | 5 | 12 | 13)) || (op1 == 4 && matches!(crm, 8..=11)))
}

#[derive(Clone, Debug, PartialEq)]
pub enum Stop {
    /// the instruction budget ran out
    Limit,
    /// the PC reached an address the caller asked to stop at (not executed)
    Break(u64),
    /// a branch to itself: the firmware's panic and "no return" loops
    Hang(u64),
    /// WFI (the PC is past it)
    Wfi(u64),
    /// an exception return below EL3: the PC and PSTATE are the target's
    Eret,
    /// a translation or permission fault, or nothing at the address
    Fault { pc: u64, addr: u64, what: String },
    /// an encoding the interpreter does not have
    Undefined { pc: u64, insn: u32, why: String },
}

impl std::fmt::Display for Stop {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        match self {
            Stop::Limit => write!(f, "instruction limit"),
            Stop::Break(pc) => write!(f, "stop at {pc:#x}"),
            Stop::Hang(pc) => write!(f, "hang (branch to self) at {pc:#x}"),
            Stop::Wfi(pc) => write!(f, "wfi at {:#x}", pc - 4),
            Stop::Eret => write!(f, "eret to a lower exception level"),
            Stop::Fault { pc, addr, what } => write!(f, "fault at pc {pc:#x}: {what} {addr:#x}"),
            Stop::Undefined { pc, insn, why } => write!(f, "undefined at pc {pc:#x}: {insn:08x} ({why})"),
        }
    }
}

#[derive(Clone, Copy, PartialEq)]
enum Acc {
    Read,
    Write,
    Exec,
}

#[derive(Clone, Copy)]
struct Tlb {
    vpage: u64,
    ppage: u64,
    write: bool,
    exec: bool,
}

const TLB_SIZE: usize = 256;
const NO_PAGE: u64 = u64::MAX;

/// The callee-saved registers at a call: x19..x29.
#[derive(Clone)]
struct Frame {
    ret: u64,
    sp: u64,
    regs: [u64; 11],
    callee: u64,
    from: u64,
}

/// The interpreter's self-check: at every return to the address a call
/// left, with the stack pointer the call had, the registers the procedure
/// call standard says the callee preserves (x19..x29) must be what they
/// were.  A difference is a fault of the interpreter (or of the code).
/// Calls made on SP_EL3 are not followed: TF-A's C code runs on SP_EL0, and
/// what runs on SP_EL3 is the vectors' assembly that saves and loads the
/// lower exception level's registers.
#[derive(Clone, Default)]
pub struct Audit {
    stack: Vec<Frame>,
    pub checked: u64,
    /// (the callee's address, what happened)
    pub faults: Vec<(u64, String)>,
}

#[derive(Clone)]
pub struct Cpu {
    pub core: usize,
    pub x: [u64; 31],
    /// SP_EL0..SP_EL3
    pub sp: [u64; 4],
    pub pc: u64,
    /// N, Z, C, V in bits 3..0
    pub nzcv: u32,
    /// D, A, I, F in bits 3..0
    pub daif: u32,
    pub el: u32,
    pub spsel: bool,
    pub ssbs: bool,
    pub sys: BTreeMap<u16, u64>,
    excl: Option<(u64, u32)>,
    pub insns: u64,
    tlb: Vec<Tlb>,
    /// executed instructions: a bit per word of [cov_base, cov_base + 32 * 64 * cov.len())
    pub cov_base: u64,
    pub cov: Vec<u64>,
    pub audit: Option<Audit>,
    /// every MSR to a system register, in order (when Some)
    pub sys_log: Option<Vec<(u16, u64)>>,
    /// an SError waiting while PSTATE.A masks it (its syndrome): the next ESB defers it into DISR_EL1
    pub serror: Option<u64>,
}

const FLAG_N: u32 = 8;
const FLAG_Z: u32 = 4;
const FLAG_C: u32 = 2;
const FLAG_V: u32 = 1;

fn cond_holds(cond: u32, f: u32) -> bool {
    let (n, z, c, v) = (f & FLAG_N != 0, f & FLAG_Z != 0, f & FLAG_C != 0, f & FLAG_V != 0);
    let r = match cond >> 1 {
        0 => z,
        1 => c,
        2 => n,
        3 => v,
        4 => c && !z,
        5 => n == v,
        6 => n == v && !z,
        _ => true,
    };
    if cond & 1 == 1 && cond != 15 {
        !r
    } else {
        r
    }
}

fn nz(v: u64, sf: bool) -> u32 {
    let (neg, zero) = if sf { (v >> 63 != 0, v == 0) } else { ((v >> 31) & 1 != 0, v as u32 == 0) };
    (if neg { FLAG_N } else { 0 }) | (if zero { FLAG_Z } else { 0 })
}

/// x + y + carry: the result and N, Z, C, V.
fn add_with_carry(x: u64, y: u64, cin: bool, sf: bool) -> (u64, u32) {
    if sf {
        let (s1, c1) = x.overflowing_add(y);
        let (s, c2) = s1.overflowing_add(cin as u64);
        let v = ((x ^ s) & (y ^ s)) >> 63 != 0;
        (s, nz(s, true) | (if c1 || c2 { FLAG_C } else { 0 }) | (if v { FLAG_V } else { 0 }))
    } else {
        let (x, y) = (x as u32, y as u32);
        let wide = x as u64 + y as u64 + cin as u64;
        let s = wide as u32;
        let v = ((x ^ s) & (y ^ s)) >> 31 != 0;
        (s as u64, nz(s as u64, false) | (if wide >> 32 != 0 { FLAG_C } else { 0 }) | (if v { FLAG_V } else { 0 }))
    }
}

fn ones(n: u32) -> u64 {
    if n >= 64 {
        u64::MAX
    } else {
        (1u64 << n) - 1
    }
}

fn ror(v: u64, by: u32, size: u32) -> u64 {
    let by = by % size;
    if by == 0 {
        return v & ones(size);
    }
    ((v >> by) | (v << (size - by))) & ones(size)
}

fn replicate(v: u64, esize: u32) -> u64 {
    let mut out = 0;
    let mut at = 0;
    while at < 64 {
        out |= v << at;
        at += esize;
    }
    out
}

/// DecodeBitMasks of the reference manual: (wmask, tmask), None when reserved.
pub fn decode_bit_masks(n: u32, imms: u32, immr: u32, immediate: bool) -> Option<(u64, u64)> {
    let combined = (n << 6) | (!imms & 0x3f);
    if combined == 0 {
        return None;
    }
    let len = 31 - combined.leading_zeros();
    if len < 1 {
        return None;
    }
    let levels = (1u32 << len) - 1;
    if immediate && (imms & levels) == levels {
        return None;
    }
    let (s, r) = (imms & levels, immr & levels);
    let diff = s.wrapping_sub(r) & levels;
    let esize = 1u32 << len;
    let welem = ones(s + 1);
    let telem = ones(diff + 1);
    Some((replicate(ror(welem, r, esize), esize), replicate(telem, esize)))
}

fn shift(v: u64, kind: u32, amount: u32, sf: bool) -> u64 {
    let size = if sf { 64 } else { 32 };
    let v = if sf { v } else { v & 0xffff_ffff };
    let amount = amount % size;
    let r = match kind {
        0 => v << amount,
        1 => v >> amount,
        2 => {
            if sf {
                ((v as i64) >> amount) as u64
            } else {
                ((v as u32 as i32) >> amount) as u32 as u64
            }
        }
        _ => ror(v, amount, size),
    };
    if sf {
        r
    } else {
        r & 0xffff_ffff
    }
}

fn extend(v: u64, option: u32, lsl: u32) -> u64 {
    let e = match option {
        0 => v & 0xff,
        1 => v & 0xffff,
        2 => v & 0xffff_ffff,
        3 => v,
        4 => v as u8 as i8 as i64 as u64,
        5 => v as u16 as i16 as i64 as u64,
        6 => v as u32 as i32 as i64 as u64,
        _ => v,
    };
    e << lsl
}

fn sext(v: u64, bits: u32) -> u64 {
    let s = 64 - bits;
    (((v << s) as i64) >> s) as u64
}

impl Cpu {
    /// A core out of reset: EL3h, everything masked, the system registers at
    /// their reset values.
    pub fn new(core: usize) -> Cpu {
        let mut sys = BTreeMap::new();
        for (_, k, v) in SYSREGS {
            sys.insert(*k, *v);
        }
        Cpu {
            core,
            x: [0; 31],
            sp: [0; 4],
            pc: 0,
            nzcv: 0,
            daif: 0xf,
            el: 3,
            spsel: true,
            ssbs: false,
            sys,
            excl: None,
            insns: 0,
            tlb: vec![Tlb { vpage: NO_PAGE, ppage: 0, write: false, exec: false }; TLB_SIZE],
            cov_base: 0,
            cov: Vec::new(),
            audit: None,
            sys_log: None,
            serror: None,
        }
    }

    pub fn cur_sp(&self) -> u64 {
        self.sp[if self.spsel { self.el as usize } else { 0 }]
    }

    fn set_cur_sp(&mut self, v: u64) {
        let i = if self.spsel { self.el as usize } else { 0 };
        self.sp[i] = v;
    }

    #[inline]
    fn r(&self, n: u32) -> u64 {
        if n == 31 {
            0
        } else {
            self.x[n as usize]
        }
    }

    #[inline]
    fn rsp(&self, n: u32) -> u64 {
        if n == 31 {
            self.cur_sp()
        } else {
            self.x[n as usize]
        }
    }

    #[inline]
    fn w(&mut self, n: u32, v: u64, sf: bool) {
        if n != 31 {
            self.x[n as usize] = if sf { v } else { v & 0xffff_ffff };
        }
    }

    #[inline]
    fn wsp(&mut self, n: u32, v: u64, sf: bool) {
        let v = if sf { v } else { v & 0xffff_ffff };
        if n == 31 {
            self.set_cur_sp(v);
        } else {
            self.x[n as usize] = v;
        }
    }

    pub fn pstate(&self) -> u64 {
        ((self.nzcv as u64) << 28)
            | ((self.ssbs as u64) << 12)
            | ((self.daif as u64) << 6)
            | ((self.el as u64) << 2)
            | self.spsel as u64
    }

    fn set_pstate(&mut self, v: u64) {
        self.nzcv = ((v >> 28) & 0xf) as u32;
        self.ssbs = (v >> 12) & 1 != 0;
        self.daif = ((v >> 6) & 0xf) as u32;
        self.el = ((v >> 2) & 3) as u32;
        self.spsel = v & 1 != 0;
    }

    pub fn sysreg(&self, k: u16) -> u64 {
        self.sys.get(&k).copied().unwrap_or(0)
    }

    pub fn flush_tlb(&mut self) {
        for t in self.tlb.iter_mut() {
            t.vpage = NO_PAGE;
        }
    }

    pub fn mmu_on(&self) -> bool {
        self.sysreg(SCTLR_EL3) & 1 != 0
    }

    fn fault(&self, addr: u64, what: impl Into<String>) -> Stop {
        Stop::Fault { pc: self.pc, addr, what: what.into() }
    }

    /// The EL3 stage-1 walk (4K granule) of a virtual page: the TLB entry for it.
    fn walk<B: Bus>(&mut self, bus: &mut B, va: u64) -> Result<Tlb, Stop> {
        let vpage = va >> 12;
        if !self.mmu_on() {
            if va >> 48 != 0 {
                return Err(self.fault(va, "address beyond 48 bits, MMU off:"));
            }
            return Ok(Tlb { vpage, ppage: vpage, write: true, exec: true });
        }
        let tcr = self.sysreg(TCR_EL3);
        if (tcr >> 14) & 3 != 0 {
            return Err(self.fault(va, "TCR_EL3.TG0 is not 4K, walking for"));
        }
        let t0sz = (tcr & 0x3f) as u32;
        let bits = 64 - t0sz;
        if !(25..=48).contains(&bits) || va >> bits != 0 {
            return Err(self.fault(va, format!("translation fault (beyond the {bits}-bit input range):")));
        }
        let mut level = if bits > 39 {
            0
        } else if bits > 30 {
            1
        } else {
            2
        };
        // (the first table of a small input range is smaller than a page and aligned to its size)
        let mut base = self.sysreg(TTBR0_EL3) & 0x0000_ffff_ffff_fffe;
        let (mut ro, mut xn) = (false, false);
        loop {
            let shift = 39 - 9 * level;
            let mut index = (va >> shift) & 0x1ff;
            if shift + 9 > bits {
                index &= ones(bits - shift);
            }
            let Some(desc) = bus.read(self.core, base + index * 8, 8) else {
                return Err(self.fault(va, format!("translation table at {:#x} is not memory, walking for", base + index * 8)));
            };
            if desc & 1 == 0 {
                return Err(self.fault(va, format!("translation fault (level {level}):")));
            }
            if level < 3 && desc & 2 != 0 {
                ro |= desc >> 62 & 1 != 0;
                xn |= desc >> 60 & 1 != 0;
                base = desc & 0x0000_ffff_ffff_f000;
                level += 1;
                continue;
            }
            if (level == 3 && desc & 2 == 0) || level == 0 {
                return Err(self.fault(va, format!("translation fault (reserved descriptor, level {level}):")));
            }
            if desc >> 10 & 1 == 0 {
                return Err(self.fault(va, "access flag fault:"));
            }
            let span = 1u64 << shift;
            let out = (desc & 0x0000_ffff_ffff_f000 & !(span - 1)) | (va & (span - 1));
            let write = !(ro || desc >> 7 & 1 != 0);
            let wxn = self.sysreg(SCTLR_EL3) >> 19 & 1 != 0;
            let exec = !(xn || desc >> 54 & 1 != 0 || (wxn && write));
            return Ok(Tlb { vpage, ppage: out >> 12, write, exec });
        }
    }

    #[inline]
    fn translate<B: Bus>(&mut self, bus: &mut B, va: u64, acc: Acc) -> Result<u64, Stop> {
        let vpage = va >> 12;
        let slot = (vpage as usize) & (TLB_SIZE - 1);
        let mut t = self.tlb[slot];
        if t.vpage != vpage {
            t = self.walk(bus, va)?;
            self.tlb[slot] = t;
        }
        match acc {
            Acc::Read => {}
            Acc::Write if !t.write => return Err(self.fault(va, "permission fault, write to")),
            Acc::Exec if !t.exec => return Err(self.fault(va, "permission fault, execute at")),
            _ => {}
        }
        Ok((t.ppage << 12) | (va & 0xfff))
    }

    /// Where an access of `size` bytes at `va` goes, checked: one physical
    /// address, or two when it crosses a page.
    fn locate<B: Bus>(&mut self, bus: &mut B, va: u64, size: u32, acc: Acc) -> Result<(u64, Option<u64>), Stop> {
        let pa = self.translate(bus, va, acc)?;
        if (va & 0xfff) + size as u64 > 0x1000 {
            let next = self.translate(bus, (va & !0xfff).wrapping_add(0x1000), acc)?;
            return Ok((pa, Some(next)));
        }
        Ok((pa, None))
    }

    fn read_at<B: Bus>(&mut self, bus: &mut B, va: u64, at: (u64, Option<u64>), size: u32) -> Result<u64, Stop> {
        match at.1 {
            None => bus.read(self.core, at.0, size).ok_or_else(|| self.fault(va, "nothing answers a read of")),
            Some(next) => {
                let first = 0x1000 - (at.0 & 0xfff) as u32;
                let mut v = 0u64;
                for i in 0..size {
                    let pa = if i < first { at.0 + i as u64 } else { next + (i - first) as u64 };
                    let b = bus.read(self.core, pa, 1).ok_or_else(|| self.fault(va, "nothing answers a read of"))?;
                    v |= b << (8 * i);
                }
                Ok(v)
            }
        }
    }

    fn write_at<B: Bus>(&mut self, bus: &mut B, va: u64, at: (u64, Option<u64>), size: u32, v: u64) -> Result<(), Stop> {
        match at.1 {
            None => {
                if !bus.write(self.core, at.0, size, v) {
                    return Err(self.fault(va, "nothing answers a write to"));
                }
            }
            Some(next) => {
                let first = 0x1000 - (at.0 & 0xfff) as u32;
                for i in 0..size {
                    let pa = if i < first { at.0 + i as u64 } else { next + (i - first) as u64 };
                    if !bus.write(self.core, pa, 1, (v >> (8 * i)) & 0xff) {
                        return Err(self.fault(va, "nothing answers a write to"));
                    }
                }
            }
        }
        Ok(())
    }

    fn load<B: Bus>(&mut self, bus: &mut B, va: u64, size: u32) -> Result<u64, Stop> {
        let at = self.locate(bus, va, size, Acc::Read)?;
        self.read_at(bus, va, at, size)
    }

    fn undefined(&self, insn: u32, why: impl Into<String>) -> Stop {
        Stop::Undefined { pc: self.pc, insn, why: why.into() }
    }

    fn mrs<B: Bus>(&mut self, bus: &mut B, insn: u32, k: u16) -> Result<u64, Stop> {
        Ok(match k {
            NZCV => (self.nzcv as u64) << 28,
            DAIF => (self.daif as u64) << 6,
            SSBS => (self.ssbs as u64) << 12,
            CURRENT_EL => (self.el as u64) << 2,
            SPSEL => self.spsel as u64,
            SP_EL0 => {
                if !self.spsel {
                    return Err(self.undefined(insn, "SP_EL0 read while it is the stack pointer"));
                }
                self.sp[0]
            }
            SP_EL1 => self.sp[1],
            SP_EL2 => self.sp[2],
            _ => {
                if let Some(v) = bus.sysreg(self.core, k, None) {
                    return Ok(v);
                }
                match self.sys.get(&k) {
                    Some(v) => *v,
                    None if is_amu_counter(k) => 0,
                    None => return Err(self.undefined(insn, format!("mrs {}: not a register of this model", sysreg_name(k)))),
                }
            }
        })
    }

    fn msr<B: Bus>(&mut self, bus: &mut B, insn: u32, k: u16, v: u64) -> Result<(), Stop> {
        if let Some(log) = self.sys_log.as_mut() {
            log.push((k, v));
        }
        match k {
            NZCV => self.nzcv = ((v >> 28) & 0xf) as u32,
            DAIF => self.daif = ((v >> 6) & 0xf) as u32,
            SSBS => self.ssbs = (v >> 12) & 1 != 0,
            SPSEL => self.spsel = v & 1 != 0,
            SP_EL0 => {
                if !self.spsel {
                    return Err(self.undefined(insn, "SP_EL0 written while it is the stack pointer"));
                }
                self.sp[0] = v;
            }
            SP_EL1 => self.sp[1] = v,
            SP_EL2 => self.sp[2] = v,
            CURRENT_EL | MIDR_EL1 | MPIDR_EL1 | DCZID_EL0 => {
                return Err(self.undefined(insn, format!("msr {}: read-only", sysreg_name(k))))
            }
            _ => {
                if bus.sysreg(self.core, k, Some(v)).is_some() {
                    return Ok(());
                }
                if !self.sys.contains_key(&k) && !is_amu_counter(k) {
                    return Err(self.undefined(insn, format!("msr {}: not a register of this model", sysreg_name(k))));
                }
                if matches!(k, SCTLR_EL3 | TTBR0_EL3 | TCR_EL3 | MAIR_EL3) {
                    self.flush_tlb();
                }
                self.sys.insert(k, v);
            }
        }
        Ok(())
    }

    /// Take an exception from a lower level to EL3: `offset` is the
    /// vector's (0x400 synchronous, 0x480 IRQ, 0x500 FIQ, 0x580 SError from
    /// AArch64), `esr` the syndrome.  The caller has left the lower level's
    /// state in place (PC, PSTATE, registers).
    pub fn enter(&mut self, offset: u64, esr: u64) {
        let spsr = self.pstate();
        self.sys.insert(SPSR_EL3, spsr);
        self.sys.insert(ELR_EL3, self.pc);
        if offset & 0x180 == 0 {
            self.sys.insert(ESR_EL3, esr);
        }
        self.el = 3;
        self.spsel = true;
        self.daif = 0xf;
        self.excl = None;
        self.pc = self.sysreg(VBAR_EL3) + offset;
    }

    fn call(&mut self, target: u64) {
        // (only code on the C runtime's stack, SP_EL0: the assembly under the vectors, which saves
        // and loads the lower level's registers on SP_EL3, keeps to no procedure call standard)
        if self.spsel {
            return;
        }
        let Some(a) = self.audit.as_mut() else { return };
        let sp = self.sp[if self.spsel { self.el as usize } else { 0 }];
        // frames deeper than this call on the same stack never returned
        while let Some(top) = a.stack.last() {
            if top.sp <= sp && sp - top.sp < 0x10000 {
                a.stack.pop();
            } else {
                break;
            }
        }
        let mut regs = [0u64; 11];
        regs.copy_from_slice(&self.x[19..30]);
        a.stack.push(Frame { ret: self.pc + 4, sp, regs, callee: target, from: self.pc });
    }

    fn ret(&mut self, target: u64) {
        let Some(a) = self.audit.as_mut() else { return };
        let sp = self.sp[if self.spsel { self.el as usize } else { 0 }];
        let Some(at) = a.stack.iter().rposition(|f| f.ret == target && f.sp == sp) else { return };
        let f = a.stack[at].clone();
        a.stack.truncate(at);
        a.checked += 1;
        for i in 0..11 {
            if self.x[19 + i] != f.regs[i] {
                a.faults.push((
                    f.callee,
                    format!("the call at {:#x} to {:#x} returned (from {:#x}) with x{} = {:#x}, was {:#x}", f.from, f.callee, self.pc, 19 + i, self.x[19 + i], f.regs[i]),
                ));
            }
        }
    }

    /// Run until something stops it: at most `limit` instructions, not
    /// executing the instruction at an address in `stops`.
    pub fn run<B: Bus>(&mut self, bus: &mut B, limit: u64, stops: &[u64]) -> Stop {
        let end = self.insns.saturating_add(limit);
        loop {
            if self.insns >= end {
                return Stop::Limit;
            }
            if !stops.is_empty() && stops.contains(&self.pc) {
                return Stop::Break(self.pc);
            }
            if let Err(stop) = self.step(bus) {
                return stop;
            }
        }
    }

    /// One instruction.
    pub fn step<B: Bus>(&mut self, bus: &mut B) -> Result<(), Stop> {
        let pc = self.pc;
        let pa = self.translate(bus, pc, Acc::Exec)?;
        if pc & 3 != 0 {
            return Err(self.fault(pc, "PC alignment fault:"));
        }
        let Some(insn) = bus.read(self.core, pa, 4) else {
            return Err(self.fault(pc, "nothing to execute at"));
        };
        let insn = insn as u32;
        self.exec(bus, insn)?;
        self.insns += 1;
        self.mark(pc);
        bus.tick();
        Ok(())
    }

    #[inline]
    fn mark(&mut self, pc: u64) {
        let i = pc.wrapping_sub(self.cov_base) >> 2;
        if let Some(w) = self.cov.get_mut((i >> 6) as usize) {
            *w |= 1 << (i & 63);
        }
    }

    fn exec<B: Bus>(&mut self, bus: &mut B, insn: u32) -> Result<(), Stop> {
        match (insn >> 25) & 0xf {
            0b1000 | 0b1001 => self.dp_imm(insn),
            0b1010 | 0b1011 => self.branch_sys(bus, insn),
            0b0100 | 0b0110 | 0b1100 | 0b1110 => self.load_store(bus, insn),
            0b0101 | 0b1101 => self.dp_reg(insn),
            0b0111 | 0b1111 => Err(self.undefined(insn, "FP/SIMD")),
            _ => Err(self.undefined(insn, "unallocated")),
        }
    }

    fn dp_imm(&mut self, insn: u32) -> Result<(), Stop> {
        let sf = insn >> 31 != 0;
        let (rd, rn) = (insn & 31, (insn >> 5) & 31);
        match (insn >> 23) & 7 {
            0 | 1 => {
                // ADR, ADRP
                let imm = sext((((insn >> 5) & 0x7ffff) << 2 | ((insn >> 29) & 3)) as u64, 21);
                let v = if insn >> 31 != 0 { (self.pc & !0xfff).wrapping_add(imm << 12) } else { self.pc.wrapping_add(imm) };
                self.w(rd, v, true);
            }
            2 => {
                // ADD, ADDS, SUB, SUBS (immediate)
                let imm = (((insn >> 10) & 0xfff) as u64) << (if (insn >> 22) & 1 != 0 { 12 } else { 0 });
                let (sub, set) = ((insn >> 30) & 1 != 0, (insn >> 29) & 1 != 0);
                let a = self.rsp(rn);
                let (v, f) = if sub { add_with_carry(a, !imm, true, sf) } else { add_with_carry(a, imm, false, sf) };
                if set {
                    self.nzcv = f;
                    self.w(rd, v, sf);
                } else {
                    self.wsp(rd, v, sf);
                }
            }
            4 => {
                // AND, ORR, EOR, ANDS (immediate)
                let n = (insn >> 22) & 1;
                if !sf && n != 0 {
                    return Err(self.undefined(insn, "logical immediate, N with 32 bits"));
                }
                let Some((imm, _)) = decode_bit_masks(n, (insn >> 10) & 0x3f, (insn >> 16) & 0x3f, true) else {
                    return Err(self.undefined(insn, "logical immediate, reserved mask"));
                };
                let a = self.r(rn);
                let opc = (insn >> 29) & 3;
                let v = match opc {
                    0 | 3 => a & imm,
                    1 => a | imm,
                    _ => a ^ imm,
                };
                let v = if sf { v } else { v & 0xffff_ffff };
                if opc == 3 {
                    self.nzcv = nz(v, sf);
                    self.w(rd, v, sf);
                } else {
                    self.wsp(rd, v, sf);
                }
            }
            5 => {
                // MOVN, MOVZ, MOVK
                let hw = (insn >> 21) & 3;
                if !sf && hw > 1 {
                    return Err(self.undefined(insn, "move wide, shift beyond 32 bits"));
                }
                let imm = ((insn >> 5) & 0xffff) as u64;
                let at = hw * 16;
                let v = match (insn >> 29) & 3 {
                    0 => !(imm << at),
                    2 => imm << at,
                    3 => (self.r(rd) & !(0xffff << at)) | (imm << at),
                    _ => return Err(self.undefined(insn, "move wide, opc 1")),
                };
                self.w(rd, v, sf);
            }
            6 => {
                // SBFM, BFM, UBFM
                let (n, immr, imms) = ((insn >> 22) & 1, (insn >> 16) & 0x3f, (insn >> 10) & 0x3f);
                let opc = (insn >> 29) & 3;
                if opc == 3 || (n != 0) != sf || (!sf && (immr > 31 || imms > 31)) {
                    return Err(self.undefined(insn, "bitfield, reserved"));
                }
                let Some((wmask, tmask)) = decode_bit_masks(n, imms, immr, false) else {
                    return Err(self.undefined(insn, "bitfield, reserved mask"));
                };
                let size = if sf { 64 } else { 32 };
                let src = if sf { self.r(rn) } else { self.r(rn) & 0xffff_ffff };
                let dst = if opc == 1 { self.r(rd) } else { 0 };
                let bot = (dst & !wmask) | (ror(src, immr, size) & wmask);
                let top = if opc == 0 {
                    if (src >> imms) & 1 != 0 {
                        u64::MAX
                    } else {
                        0
                    }
                } else {
                    dst
                };
                self.w(rd, (top & !tmask) | (bot & tmask), sf);
            }
            7 => {
                // EXTR
                let (n, imms, rm) = ((insn >> 22) & 1, (insn >> 10) & 0x3f, (insn >> 16) & 31);
                if (insn >> 29) & 3 != 0 || (insn >> 21) & 1 != 0 || (n != 0) != sf || (!sf && imms > 31) {
                    return Err(self.undefined(insn, "extract, reserved"));
                }
                let (hi, lo) = (self.r(rn), self.r(rm));
                let v = if sf {
                    if imms == 0 {
                        lo
                    } else {
                        (lo >> imms) | (hi << (64 - imms))
                    }
                } else {
                    let (hi, lo) = (hi as u32, lo as u32);
                    (if imms == 0 { lo } else { (lo >> imms) | (hi << (32 - imms)) }) as u64
                };
                self.w(rd, v, sf);
            }
            _ => return Err(self.undefined(insn, "data processing (immediate), unallocated")),
        }
        self.pc += 4;
        Ok(())
    }

    fn branch_sys<B: Bus>(&mut self, bus: &mut B, insn: u32) -> Result<(), Stop> {
        let pc = self.pc;
        if (insn >> 26) & 0x1f == 0b00101 {
            // B, BL
            let target = pc.wrapping_add(sext((insn & 0x03ff_ffff) as u64, 26) << 2);
            if insn >> 31 != 0 {
                self.call(target);
                self.x[30] = pc + 4;
            } else if target == pc {
                return Err(Stop::Hang(pc));
            }
            self.pc = target;
            return Ok(());
        }
        if (insn >> 25) & 0x3f == 0b011010 {
            // CBZ, CBNZ
            let sf = insn >> 31 != 0;
            let v = self.r(insn & 31);
            let zero = if sf { v == 0 } else { v as u32 == 0 };
            let taken = zero != ((insn >> 24) & 1 != 0);
            self.pc = if taken { pc.wrapping_add(sext(((insn >> 5) & 0x7ffff) as u64, 19) << 2) } else { pc + 4 };
            return Ok(());
        }
        if (insn >> 25) & 0x3f == 0b011011 {
            // TBZ, TBNZ
            let bit = ((insn >> 31) << 5) | ((insn >> 19) & 31);
            let set = (self.r(insn & 31) >> bit) & 1 != 0;
            let taken = set == ((insn >> 24) & 1 != 0);
            self.pc = if taken { pc.wrapping_add(sext(((insn >> 5) & 0x3fff) as u64, 14) << 2) } else { pc + 4 };
            return Ok(());
        }
        if insn >> 24 == 0b0101_0100 {
            // B.cond
            if insn & 0x10 != 0 {
                return Err(self.undefined(insn, "conditional branch, bit 4"));
            }
            self.pc = if cond_holds(insn & 15, self.nzcv) { pc.wrapping_add(sext(((insn >> 5) & 0x7ffff) as u64, 19) << 2) } else { pc + 4 };
            return Ok(());
        }
        if insn >> 24 == 0b1101_0100 {
            let what = match ((insn >> 21) & 7, insn & 0x1f) {
                (0, 1) => "svc",
                (0, 2) => "hvc",
                (0, 3) => "smc",
                (1, 0) => "brk",
                (2, 0) => "hlt",
                _ => "exception generation, unallocated",
            };
            return Err(self.undefined(insn, format!("{what} at EL3")));
        }
        if insn >> 25 == 0b110_1011 {
            // BR, BLR, RET, ERET
            let rn = (insn >> 5) & 31;
            if (insn >> 16) & 31 != 31 || (insn >> 10) & 0x3f != 0 || insn & 31 != 0 {
                return Err(self.undefined(insn, "branch to register, pointer authentication or reserved"));
            }
            match (insn >> 21) & 15 {
                0 => {
                    let target = self.r(rn);
                    if rn == 30 {
                        self.ret(target);
                    }
                    self.pc = target;
                }
                1 => {
                    let target = self.r(rn);
                    self.call(target);
                    self.x[30] = pc + 4;
                    self.pc = target;
                }
                2 => {
                    let target = self.r(rn);
                    self.ret(target);
                    self.pc = target;
                }
                4 => {
                    if rn != 31 {
                        return Err(self.undefined(insn, "eret, Rn"));
                    }
                    let spsr = self.sysreg(SPSR_EL3);
                    if spsr & 0x10 != 0 {
                        return Err(self.undefined(insn, format!("eret to AArch32 (SPSR_EL3 {spsr:#x})")));
                    }
                    if spsr & 2 != 0 || (spsr & 0xc == 0 && spsr & 1 != 0) {
                        return Err(self.undefined(insn, format!("eret with an illegal SPSR_EL3 {spsr:#x}")));
                    }
                    self.pc = self.sysreg(ELR_EL3);
                    self.set_pstate(spsr);
                    self.excl = None;
                    if self.el < 3 {
                        self.insns += 1;
                        self.mark(pc);
                        return Err(Stop::Eret);
                    }
                }
                _ => return Err(self.undefined(insn, "branch to register, unallocated")),
            }
            return Ok(());
        }
        if insn >> 22 == 0b11_0101_0100 {
            return self.system(bus, insn);
        }
        Err(self.undefined(insn, "branch, exception or system, unallocated"))
    }

    fn system<B: Bus>(&mut self, bus: &mut B, insn: u32) -> Result<(), Stop> {
        let l = (insn >> 21) & 1 != 0;
        let (op0, op1, crn, crm, op2, rt) =
            ((insn >> 19) & 3, (insn >> 16) & 7, (insn >> 12) & 15, (insn >> 8) & 15, (insn >> 5) & 7, insn & 31);
        match op0 {
            0 => {
                if l || rt != 31 {
                    return Err(self.undefined(insn, "system, unallocated"));
                }
                match crn {
                    4 => match (op1, op2) {
                        // MSR to PSTATE
                        (0, 5) => self.spsel = crm & 1 != 0,
                        (3, 6) => self.daif |= crm,
                        (3, 7) => self.daif &= !crm,
                        _ => return Err(self.undefined(insn, "msr to a PSTATE field this model does not have")),
                    },
                    2 if op1 == 3 => match (crm << 3) | op2 {
                        // hints: WFI stops; ESB defers a masked SError; the rest do nothing here
                        3 => {
                            self.mark(self.pc);
                            self.pc += 4;
                            self.insns += 1;
                            return Err(Stop::Wfi(self.pc));
                        }
                        16 => {
                            if self.daif & 4 != 0 {
                                if let Some(syndrome) = self.serror.take() {
                                    self.sys.insert(key(3, 0, 12, 1, 1), (1 << 31) | (syndrome & 0x1ff_ffff));
                                }
                            }
                        }
                        _ => {}
                    },
                    3 if op1 == 3 => match op2 {
                        // CLREX, DSB, DMB, ISB, SB
                        2 => self.excl = None,
                        4 | 5 | 6 | 7 => {}
                        _ => return Err(self.undefined(insn, "barrier, unallocated")),
                    },
                    _ => return Err(self.undefined(insn, "system, unallocated")),
                }
            }
            1 => {
                if l {
                    return Err(self.undefined(insn, "sysl"));
                }
                match (op1, crn, crm, op2) {
                    // DC ZVA: the block DCZID_EL0 gives (64 bytes)
                    (3, 7, 4, 1) => {
                        let va = self.r(rt) & !63;
                        let at = self.locate(bus, va, 64, Acc::Write)?;
                        for i in 0..8 {
                            self.write_at(bus, va + 8 * i, (at.0 + 8 * i, None), 8, 0)?;
                        }
                    }
                    // DC IVAC, CVAC, CVAU, CIVAC, ISW, CSW, CISW; IC IALLU, IVAU: no caches here
                    (0, 7, 6, 1) | (3, 7, 10, 1) | (3, 7, 11, 1) | (3, 7, 14, 1) => {}
                    (0, 7, 6, 2) | (0, 7, 10, 2) | (0, 7, 14, 2) | (0, 7, 5, 0) | (3, 7, 5, 1) => {}
                    // TLBI
                    (_, 8, _, _) => self.flush_tlb(),
                    _ => return Err(self.undefined(insn, "sys instruction this model does not have")),
                }
            }
            _ => {
                let k = key(op0, op1, crn, crm, op2);
                if l {
                    let v = self.mrs(bus, insn, k)?;
                    self.w(rt, v, true);
                } else {
                    let v = self.r(rt);
                    self.msr(bus, insn, k, v)?;
                }
            }
        }
        self.pc += 4;
        Ok(())
    }

    fn load_store<B: Bus>(&mut self, bus: &mut B, insn: u32) -> Result<(), Stop> {
        if (insn >> 26) & 1 != 0 {
            return Err(self.undefined(insn, "FP/SIMD load or store"));
        }
        let size = insn >> 30;
        let (rt, rn) = (insn & 31, (insn >> 5) & 31);
        match (insn >> 27) & 7 {
            0b001 if (insn >> 24) & 1 == 0 => {
                // exclusives and ordered
                let (o2, load, o1, o0) = ((insn >> 23) & 1 != 0, (insn >> 22) & 1 != 0, (insn >> 21) & 1 != 0, (insn >> 15) & 1 != 0);
                let (rs, rt2) = ((insn >> 16) & 31, (insn >> 10) & 31);
                let bytes = 1u32 << size;
                let va = self.rsp(rn);
                if o2 && o1 {
                    return Err(self.undefined(insn, "compare and swap"));
                }
                if o2 {
                    // LDAR, STLR (and LDLAR, STLLR)
                    if load {
                        let v = self.load(bus, va, bytes)?;
                        self.w(rt, v, true);
                    } else {
                        let at = self.locate(bus, va, bytes, Acc::Write)?;
                        self.write_at(bus, va, at, bytes, self.r(rt) & ones(8 * bytes))?;
                    }
                } else if o1 {
                    // LDXP, STXP
                    if size < 2 {
                        return Err(self.undefined(insn, "exclusive pair, size"));
                    }
                    if load {
                        let a = self.load(bus, va, bytes)?;
                        let b = self.load(bus, va + bytes as u64, bytes)?;
                        self.w(rt, a, true);
                        self.w(rt2, b, true);
                        self.excl = Some((va, 2 * bytes));
                    } else {
                        let at = self.locate(bus, va, bytes, Acc::Write)?;
                        let at2 = self.locate(bus, va + bytes as u64, bytes, Acc::Write)?;
                        let ok = self.excl == Some((va, 2 * bytes));
                        if ok {
                            self.write_at(bus, va, at, bytes, self.r(rt) & ones(8 * bytes))?;
                            self.write_at(bus, va + bytes as u64, at2, bytes, self.r(rt2) & ones(8 * bytes))?;
                        }
                        self.excl = None;
                        self.w(rs, !ok as u64, false);
                    }
                } else if load {
                    // LDXR, LDAXR
                    let v = self.load(bus, va, bytes)?;
                    self.w(rt, v, true);
                    self.excl = Some((va, bytes));
                } else {
                    // STXR, STLXR
                    let at = self.locate(bus, va, bytes, Acc::Write)?;
                    let ok = self.excl == Some((va, bytes));
                    if ok {
                        self.write_at(bus, va, at, bytes, self.r(rt) & ones(8 * bytes))?;
                    }
                    self.excl = None;
                    self.w(rs, !ok as u64, false);
                }
                let _ = o0;
            }
            0b011 if (insn >> 24) & 1 == 0 => {
                // LDR (literal), LDRSW (literal), PRFM (literal)
                let va = self.pc.wrapping_add(sext(((insn >> 5) & 0x7ffff) as u64, 19) << 2);
                match size {
                    0 => {
                        let v = self.load(bus, va, 4)?;
                        self.w(rt, v, true);
                    }
                    1 => {
                        let v = self.load(bus, va, 8)?;
                        self.w(rt, v, true);
                    }
                    2 => {
                        let v = self.load(bus, va, 4)?;
                        self.w(rt, sext(v, 32), true);
                    }
                    _ => {}
                }
            }
            0b101 => {
                // LDP, STP, LDPSW (and the non-temporal forms)
                let (mode, load) = ((insn >> 23) & 3, (insn >> 22) & 1 != 0);
                let rt2 = (insn >> 10) & 31;
                let (bytes, signed) = match size {
                    0 => (4u32, false),
                    1 if load && mode != 0 => (4, true),
                    2 => (8, false),
                    _ => return Err(self.undefined(insn, "load/store pair, opc")),
                };
                let offset = sext(((insn >> 15) & 0x7f) as u64, 7).wrapping_mul(bytes as u64);
                let base = self.rsp(rn);
                let va = if mode == 1 { base } else { base.wrapping_add(offset) };
                if load {
                    let a = self.load(bus, va, bytes)?;
                    let b = self.load(bus, va.wrapping_add(bytes as u64), bytes)?;
                    let (a, b) = if signed { (sext(a, 32), sext(b, 32)) } else { (a, b) };
                    self.w(rt, a, true);
                    self.w(rt2, b, true);
                } else {
                    let at = self.locate(bus, va, bytes, Acc::Write)?;
                    let at2 = self.locate(bus, va.wrapping_add(bytes as u64), bytes, Acc::Write)?;
                    let (a, b) = (self.r(rt) & ones(8 * bytes), self.r(rt2) & ones(8 * bytes));
                    self.write_at(bus, va, at, bytes, a)?;
                    self.write_at(bus, va.wrapping_add(bytes as u64), at2, bytes, b)?;
                }
                if mode == 1 || mode == 3 {
                    self.wsp(rn, base.wrapping_add(offset), true);
                }
            }
            0b111 => {
                let opc = (insn >> 22) & 3;
                let bytes = 1u32 << size;
                let base = self.rsp(rn);
                // the address, and the base register's new value if it is written back
                let (va, writeback) = if (insn >> 24) & 1 != 0 {
                    (base.wrapping_add((((insn >> 10) & 0xfff) as u64) << size), None)
                } else if (insn >> 21) & 1 == 0 {
                    let imm = sext(((insn >> 12) & 0x1ff) as u64, 9);
                    match (insn >> 10) & 3 {
                        0 => (base.wrapping_add(imm), None),
                        1 => (base, Some(base.wrapping_add(imm))),
                        3 => (base.wrapping_add(imm), Some(base.wrapping_add(imm))),
                        _ => return Err(self.undefined(insn, "unprivileged load or store")),
                    }
                } else if (insn >> 10) & 3 == 2 {
                    let option = (insn >> 13) & 7;
                    if option & 2 == 0 {
                        return Err(self.undefined(insn, "register offset, option"));
                    }
                    let lsl = if (insn >> 12) & 1 != 0 { size } else { 0 };
                    (base.wrapping_add(extend(self.r((insn >> 16) & 31), option, lsl)), None)
                } else {
                    return Err(self.undefined(insn, "atomic memory operation or pointer authentication"));
                };
                match opc {
                    0 => {
                        let at = self.locate(bus, va, bytes, Acc::Write)?;
                        self.write_at(bus, va, at, bytes, self.r(rt) & ones(8 * bytes))?;
                    }
                    1 => {
                        let v = self.load(bus, va, bytes)?;
                        self.w(rt, v, true);
                    }
                    2 if size == 3 => {} // PRFM
                    2 => {
                        let v = self.load(bus, va, bytes)?;
                        self.w(rt, sext(v, 8 * bytes), true);
                    }
                    _ => {
                        if size >= 2 {
                            return Err(self.undefined(insn, "load, sign-extending to 32 bits, size"));
                        }
                        let v = self.load(bus, va, bytes)?;
                        self.w(rt, sext(v, 8 * bytes), false);
                    }
                }
                if let Some(wb) = writeback {
                    self.wsp(rn, wb, true);
                }
            }
            _ => return Err(self.undefined(insn, "load or store, unallocated")),
        }
        self.pc += 4;
        Ok(())
    }

    fn dp_reg(&mut self, insn: u32) -> Result<(), Stop> {
        let sf = insn >> 31 != 0;
        let (rd, rn, rm) = (insn & 31, (insn >> 5) & 31, (insn >> 16) & 31);
        if (insn >> 28) & 1 == 0 {
            if (insn >> 24) & 1 == 0 {
                // AND, BIC, ORR, ORN, EOR, EON, ANDS, BICS (shifted register)
                let imm6 = (insn >> 10) & 0x3f;
                if !sf && imm6 > 31 {
                    return Err(self.undefined(insn, "logical, shift beyond 32 bits"));
                }
                let mut b = shift(self.r(rm), (insn >> 22) & 3, imm6, sf);
                if (insn >> 21) & 1 != 0 {
                    b = !b;
                }
                let a = self.r(rn);
                let opc = (insn >> 29) & 3;
                let v = match opc {
                    0 | 3 => a & b,
                    1 => a | b,
                    _ => a ^ b,
                };
                let v = if sf { v } else { v & 0xffff_ffff };
                if opc == 3 {
                    self.nzcv = nz(v, sf);
                }
                self.w(rd, v, sf);
            } else {
                let (sub, set) = ((insn >> 30) & 1 != 0, (insn >> 29) & 1 != 0);
                let (a, b) = if (insn >> 21) & 1 == 0 {
                    // ADD, SUB (shifted register)
                    let imm6 = (insn >> 10) & 0x3f;
                    if (insn >> 22) & 3 == 3 || (!sf && imm6 > 31) {
                        return Err(self.undefined(insn, "add/sub shifted register, reserved"));
                    }
                    (self.r(rn), shift(self.r(rm), (insn >> 22) & 3, imm6, sf))
                } else {
                    // ADD, SUB (extended register)
                    let imm3 = (insn >> 10) & 7;
                    if (insn >> 22) & 3 != 0 || imm3 > 4 {
                        return Err(self.undefined(insn, "add/sub extended register, reserved"));
                    }
                    (self.rsp(rn), extend(self.r(rm), (insn >> 13) & 7, imm3))
                };
                let (v, f) = if sub { add_with_carry(a, !b, true, sf) } else { add_with_carry(a, b, false, sf) };
                if set {
                    self.nzcv = f;
                    self.w(rd, v, sf);
                } else if (insn >> 21) & 1 != 0 {
                    self.wsp(rd, v, sf);
                } else {
                    self.w(rd, v, sf);
                }
            }
            self.pc += 4;
            return Ok(());
        }
        if (insn >> 24) & 1 != 0 {
            // MADD, MSUB, SMADDL, SMSUBL, SMULH, UMADDL, UMSUBL, UMULH
            let ra = (insn >> 10) & 31;
            let sub = (insn >> 15) & 1 != 0;
            if (insn >> 29) & 3 != 0 {
                return Err(self.undefined(insn, "multiply, reserved"));
            }
            let (n, m, a) = (self.r(rn), self.r(rm), self.r(ra));
            let v = match ((insn >> 21) & 7, sf) {
                (0, _) => {
                    let p = n.wrapping_mul(m);
                    if sub {
                        a.wrapping_sub(p)
                    } else {
                        a.wrapping_add(p)
                    }
                }
                (1, true) => {
                    let p = (n as u32 as i32 as i64).wrapping_mul(m as u32 as i32 as i64) as u64;
                    if sub {
                        a.wrapping_sub(p)
                    } else {
                        a.wrapping_add(p)
                    }
                }
                (5, true) => {
                    let p = (n & 0xffff_ffff).wrapping_mul(m & 0xffff_ffff);
                    if sub {
                        a.wrapping_sub(p)
                    } else {
                        a.wrapping_add(p)
                    }
                }
                (2, true) if !sub => (((n as i64 as i128) * (m as i64 as i128)) >> 64) as u64,
                (6, true) if !sub => (((n as u128) * (m as u128)) >> 64) as u64,
                _ => return Err(self.undefined(insn, "multiply, unallocated")),
            };
            self.w(rd, v, sf);
            self.pc += 4;
            return Ok(());
        }
        match (insn >> 21) & 15 {
            0 => {
                // ADC, ADCS, SBC, SBCS
                if (insn >> 10) & 0x3f != 0 {
                    return Err(self.undefined(insn, "flag manipulation"));
                }
                let (a, b) = (self.r(rn), self.r(rm));
                let c = self.nzcv & FLAG_C != 0;
                let (v, f) = if (insn >> 30) & 1 != 0 { add_with_carry(a, !b, c, sf) } else { add_with_carry(a, b, c, sf) };
                if (insn >> 29) & 1 != 0 {
                    self.nzcv = f;
                }
                self.w(rd, v, sf);
            }
            2 => {
                // CCMN, CCMP (register, immediate)
                if (insn >> 29) & 1 == 0 || (insn >> 10) & 1 != 0 || (insn >> 4) & 1 != 0 {
                    return Err(self.undefined(insn, "conditional compare, reserved"));
                }
                let a = self.r(rn);
                let b = if (insn >> 11) & 1 != 0 { rm as u64 } else { self.r(rm) };
                self.nzcv = if cond_holds((insn >> 12) & 15, self.nzcv) {
                    if (insn >> 30) & 1 != 0 {
                        add_with_carry(a, !b, true, sf).1
                    } else {
                        add_with_carry(a, b, false, sf).1
                    }
                } else {
                    insn & 15
                };
            }
            4 => {
                // CSEL, CSINC, CSINV, CSNEG
                if (insn >> 29) & 1 != 0 || (insn >> 11) & 1 != 0 {
                    return Err(self.undefined(insn, "conditional select, reserved"));
                }
                let v = if cond_holds((insn >> 12) & 15, self.nzcv) {
                    self.r(rn)
                } else {
                    let b = self.r(rm);
                    match ((insn >> 30) & 1, (insn >> 10) & 1) {
                        (0, 0) => b,
                        (0, _) => b.wrapping_add(1),
                        (_, 0) => !b,
                        _ => (!b).wrapping_add(1),
                    }
                };
                self.w(rd, v, sf);
            }
            6 => {
                if (insn >> 29) & 1 != 0 {
                    return Err(self.undefined(insn, "data processing (1 or 2 sources), S"));
                }
                let op = (insn >> 10) & 0x3f;
                let n = if sf { self.r(rn) } else { self.r(rn) & 0xffff_ffff };
                let v = if (insn >> 30) & 1 == 0 {
                    let m = if sf { self.r(rm) } else { self.r(rm) & 0xffff_ffff };
                    let size = if sf { 64 } else { 32 };
                    match op {
                        2 => {
                            if m == 0 {
                                0
                            } else {
                                n / m
                            }
                        }
                        3 => {
                            if sf {
                                if m == 0 {
                                    0
                                } else {
                                    (n as i64).wrapping_div(m as i64) as u64
                                }
                            } else if m == 0 {
                                0
                            } else {
                                (n as u32 as i32).wrapping_div(m as u32 as i32) as u32 as u64
                            }
                        }
                        8 => shift(n, 0, (m % size) as u32, sf),
                        9 => shift(n, 1, (m % size) as u32, sf),
                        10 => shift(n, 2, (m % size) as u32, sf),
                        11 => shift(n, 3, (m % size) as u32, sf),
                        _ => return Err(self.undefined(insn, "data processing (2 sources), unallocated")),
                    }
                } else {
                    if rm != 0 {
                        return Err(self.undefined(insn, "data processing (1 source), pointer authentication"));
                    }
                    match (op, sf) {
                        (0, true) => n.reverse_bits(),
                        (0, false) => (n as u32).reverse_bits() as u64,
                        (1, true) => ((n & 0xff00_ff00_ff00_ff00) >> 8) | ((n & 0x00ff_00ff_00ff_00ff) << 8),
                        (1, false) => (((n & 0xff00_ff00) >> 8) | ((n & 0x00ff_00ff) << 8)) & 0xffff_ffff,
                        (2, true) => ((n >> 32) as u32).swap_bytes() as u64 >> 0 << 32 | (n as u32).swap_bytes() as u64,
                        (2, false) => (n as u32).swap_bytes() as u64,
                        (3, true) => n.swap_bytes(),
                        (4, true) => n.leading_zeros() as u64,
                        (4, false) => (n as u32).leading_zeros() as u64,
                        (5, true) => ((n ^ ((n as i64) >> 1) as u64) << 1 | 1).leading_zeros() as u64,
                        (5, false) => {
                            let n = n as u32;
                            ((n ^ ((n as i32) >> 1) as u32) << 1 | 1).leading_zeros() as u64
                        }
                        _ => return Err(self.undefined(insn, "data processing (1 source), unallocated")),
                    }
                };
                self.w(rd, v, sf);
            }
            _ => return Err(self.undefined(insn, "data processing (register), unallocated")),
        }
        self.pc += 4;
        Ok(())
    }
}

/// The class of an instruction word, for the self-check's choice of what
/// it can run natively: 'd' data processing (registers only), 'm' load or
/// store, 'b' a branch that depends only on registers and flags, 's'
/// system or exception, 'x' not an instruction of this interpreter.
pub fn class(insn: u32) -> char {
    match (insn >> 25) & 0xf {
        0b1000 | 0b1001 | 0b0101 | 0b1101 => 'd',
        0b1010 | 0b1011 => {
            if insn >> 24 == 0b1101_0100 || insn >> 22 == 0b11_0101_0100 || insn >> 25 == 0b110_1011 {
                's'
            } else {
                'b'
            }
        }
        0b0100 | 0b0110 | 0b1100 | 0b1110 => {
            if (insn >> 26) & 1 != 0 {
                'x'
            } else {
                'm'
            }
        }
        _ => 'x',
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn masks() {
        // 0x00ff00ff00ff00ff: N=0 immr=0 imms=0b100111
        assert_eq!(decode_bit_masks(0, 0b100111, 0, true).unwrap().0, 0x00ff_00ff_00ff_00ff);
        // 0xffff000000000000 >> ... : N=1 immr=16 imms=15 -> ror(0xffff, 16) in 64 bits
        assert_eq!(decode_bit_masks(1, 15, 16, true).unwrap().0, 0xffff_0000_0000_0000);
        assert!(decode_bit_masks(0, 0x3f, 0, true).is_none());
    }

    #[test]
    fn flags() {
        assert_eq!(add_with_carry(1, !1, true, true), (0, FLAG_Z | FLAG_C));
        assert_eq!(add_with_carry(0, !1, true, true), (u64::MAX, FLAG_N));
        assert_eq!(add_with_carry(0x7fff_ffff, 1, false, false), (0x8000_0000, FLAG_N | FLAG_V));
        assert!(cond_holds(0xb, FLAG_N));
        assert!(!cond_holds(0xb, FLAG_N | FLAG_V));
    }
}
