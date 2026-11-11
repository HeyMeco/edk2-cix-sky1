// SPDX-License-Identifier: BSD-2-Clause-Patent
//! Decoder for the 64-bit FLIX bundles of CSU_PM's Xtensa core
//! (pm_core_v1).  The formats, and how they were established from CIX's
//! image, are documented in scripts/open/xtensa_flix.py, which the build's
//! instruction check uses; this is the same decoder for the interpreter.
//!
//! A bundle's operations come back as the 24-bit words the core ISA would
//! use for them (NOPs left out), or as a long branch when a conditional
//! branch's 18-bit offset does not fit the core encoding.

pub const B4CONST: [i32; 16] = [-1, 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 16, 32, 64, 128, 256];
pub const B4CONSTU: [i32; 16] = [32768, 65536, 2, 3, 4, 5, 6, 7, 8, 10, 12, 16, 32, 64, 128, 256];

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Slot {
    /// a core 24-bit instruction word
    Word(u32),
    /// mnemonic index into BR15, tested register, operand (register, bit
    /// or constant; None for the compare-with-zero branches), offset
    Branch(&'static str, u8, Option<i32>, i32),
}

fn sx(v: u32, bits: u32) -> i32 {
    if v >> (bits - 1) & 1 != 0 {
        (v as i64 - (1i64 << bits)) as i32
    } else {
        v as i32
    }
}

fn rrr(op2: u32, op1: u32, r: u32, s: u32, t: u32) -> u32 {
    op2 << 20 | op1 << 16 | r << 12 | s << 8 | t << 4
}

fn lsai(r: u32, s: u32, t: u32, imm8: u32) -> u32 {
    (imm8 & 0xff) << 16 | r << 12 | s << 8 | t << 4 | 2
}

fn mov(t: u32, s: u32) -> u32 {
    rrr(2, 0, t, s, s) // or at,as,as
}

fn movi(t: u32, v: i32) -> u32 {
    let v = v as u32 & 0xfff;
    lsai(10, v >> 8, t, v & 0xff)
}

fn bad<T>(what: &str, v: u64) -> Result<T, String> {
    Err(format!("xtensa_flix: undetermined {what} encoding {v:#x}"))
}

fn s0_qrst(op1: u32, op2: u32) -> bool {
    matches!(
        (op1, op2),
        (0, 1) | (0, 2) | (0, 3) | (0, 8) | (0, 9) | (0, 10) | (0, 11) | (0, 12) | (1, 0) | (1, 1) | (1, 2) | (1, 3)
            | (1, 4) | (1, 8) | (1, 9) | (1, 10) | (1, 13) | (2, 8) | (3, 2) | (3, 4) | (3, 5) | (3, 6) | (3, 7)
            | (3, 8) | (3, 9) | (4, 0) | (5, 0)
    )
}

fn s0_qrst_inferred(op1: u32, op2: u32) -> bool {
    matches!(
        (op1, op2),
        (0, 13) | (0, 14) | (0, 15) | (1, 11) | (1, 12) | (2, 6) | (2, 7) | (2, 10) | (2, 11) | (2, 12) | (2, 13)
            | (2, 14) | (2, 15) | (3, 10) | (3, 11)
    )
}

fn slot0(v: u32, inferred: bool) -> Result<Option<u32>, String> {
    let w = (v & 0xfffff) << 4 | (v >> 20) & 15;
    let (op0, t, s, r) = (w & 15, (w >> 4) & 15, (w >> 8) & 15, (w >> 12) & 15);
    let (op1, op2) = ((w >> 16) & 15, (w >> 20) & 15);
    if w == 0x0020f0 {
        return Ok(None); // nop
    }
    if w == 0x503000 {
        return Ok(Some(0x000090)); // retw
    }
    if op0 == 0 && op1 == 6 && op2 == 0 && r == 0 {
        return Ok(Some(mov(t, s))); // mov.n
    }
    if op0 == 2 && r == 14 {
        // loop / loopgtz / loopnez
        let kind = match t {
            0 => 8,
            2 => 9,
            1 if inferred => 10,
            _ => return bad("slot-0 loop", w as u64),
        };
        return Ok(Some((w >> 16) << 16 | kind << 12 | s << 8 | 0x76));
    }
    if op0 == 1 {
        return Ok(Some(w)); // l32r
    }
    if op0 == 2 && matches!(r, 0 | 1 | 2 | 4 | 5 | 6 | 9 | 10 | 12 | 13) {
        return Ok(Some(w));
    }
    if op0 == 0 {
        if op1 == 0 && op2 == 4 && matches!(r, 0 | 1 | 2) && t == 0 {
            return Ok(Some(w)); // ssr ssl ssa8l
        }
        if op1 == 0 && op2 == 4 && r == 15 {
            return Ok(Some(w)); // nsau
        }
        if op1 == 0 && op2 == 6 && matches!(s, 0 | 1) {
            return Ok(Some(w)); // neg abs
        }
        if s0_qrst(op1, if op1 < 4 { op2 } else { 0 }) {
            return Ok(Some(w));
        }
        if inferred && (s0_qrst_inferred(op1, op2) || (op1 == 0 && op2 == 4 && matches!(r, 3 | 4 | 14))) {
            return Ok(Some(w));
        }
    }
    bad("slot-0", w as u64)
}

fn slot1_14(v: u32, inferred: bool) -> Result<Option<u32>, String> {
    let (g, n3, s, r, t) = (v >> 16, (v >> 12) & 15, (v >> 8) & 15, (v >> 4) & 15, v & 15);
    let w = match (g, n3) {
        _ if g >> 2 == 3 => (v & 0x3ffff) << 6 | 6,                // j
        (4, _) | (5, _) => rrr(s, 4 | (g & 1), r, n3, t),          // extui r,t,sh,s+1
        (6, _) => lsai(12, s, t, n3 << 4 | r),                     // addi
        (7, _) => lsai(13, s, t, n3 << 4 | r),                     // addmi
        (8, _) => movi(t, sx((v >> 4) & 0xfff, 12)),
        (9, 0) | (9, 1) => rrr(n3, 1, r, s, t),                    // slli
        (9, 14) => rrr(2, 0, r, s, t),                             // or
        (9, 2) => rrr(8, 0, r, s, t),
        (9, 3) => rrr(11, 0, r, s, t),
        (9, 4) => rrr(9, 0, r, s, t),
        (9, 5) => rrr(1, 0, r, s, t),
        (9, 8) => rrr(10, 0, r, s, t),
        (9, 6) => rrr(8, 3, r, s, t),
        (9, 7) => rrr(11, 3, r, s, t),
        (9, 9) => rrr(10, 3, r, s, t),
        (9, 10) => rrr(9, 3, r, s, t),
        (9, 12) => rrr(13, 1, r, s, t),
        (9, 13) => rrr(8, 2, r, s, t),
        (9, 15) => rrr(2, 3, r, s, t),
        (10, 1) => rrr(3, 1, r, s, t),                             // srai r,t,16+s
        (10, 2) => rrr(8, 1, r, s, t),                             // src
        (10, 3) if r == 0 => mov(t, s),                            // mov.n
        (10, 3) if r == 1 && t == 0 => s << 8 | 0xa0,              // jx
        (10, 3) if r == 2 && t == 0 => rrr(4, 0, 1, s, 0),         // ssl
        (10, 4) => rrr(4, 1, r, s, t),                             // srli r,t,s
        (10, 5) if s == 0 => rrr(6, 0, r, 0, t),                   // neg
        (10, 5) if s == 2 => rrr(9, 1, r, 0, t),                   // srl
        (10, 5) if s == 1 && inferred => rrr(11, 1, r, 0, t),      // sra
        (10, 6) if t == 0 => rrr(10, 1, r, s, 0),                  // sll
        (10, 8) => rrr(12, 0, r, s, t),                            // sub
        (11, 0) => rrr(3, 0, r, s, t),                             // xor
        _ => return bad("op0=14 slot-1", v as u64),
    };
    Ok(Some(w))
}

fn slot2_14(v: u32, inferred: bool) -> Result<Option<u32>, String> {
    if v == 0xc090 {
        return Ok(None); // nop
    }
    let (g, s, r, t) = (v >> 12, (v >> 8) & 15, (v >> 4) & 15, v & 15);
    let w = match g {
        2 => rrr(8, 0, r, s, t),
        4 => rrr(9, 0, r, s, t),
        5 => rrr(10, 0, r, s, t),
        6 => rrr(1, 0, r, s, t),
        7 => rrr(2, 0, r, s, t),
        10 => rrr(12, 0, r, s, t),
        11 => rrr(3, 0, r, s, t),
        3 => lsai(12, s, r, if t != 0 { t } else { 0xff }),       // addi.n (0 means -1)
        8 if t == 0 || t == 8 => rrr(2, 3, r, s, t),               // sext
        9 => rrr(4, 1, r, s, t),                                   // srli r,t,s
        12 if r & 8 == 0 => {
            let i = ((r & 7) << 4 | t) as i32;
            movi(s, if i >= 96 { i - 128 } else { i })            // movi.n
        }
        12 if r == 8 => mov(t, s),                                 // mov.n
        13 if s == 0 => rrr(6, 0, r, 1, t),                        // abs
        13 if s == 1 => rrr(6, 0, r, 0, t),                        // neg
        13 if s == 4 => rrr(9, 1, r, 0, t),                        // srl
        13 if inferred && s == 2 => rrr(10, 1, r, t, 0),           // sll r,t
        13 if inferred && s == 3 => rrr(11, 1, r, 0, t),           // sra
        14 => rrr(2, 1, r, s, t),                                  // srai r,t,s
        15 => rrr(3, 1, r, s, t),                                  // srai r,t,16+s
        _ => return bad("op0=14 slot-2", v as u64),
    };
    Ok(Some(w))
}

const BR15: [&str; 25] = [
    "", "bbci", "bbsi", "ball", "bany", "bbc", "bbs", "beq", "beqi", "bge", "bgei", "bgeu", "bgeui", "blt", "blti",
    "bltu", "bltui", "bnall", "bne", "bnei", "bnone", "beqz", "bgez", "bltz", "bnez",
];

fn b_r(m: &str) -> Option<u32> {
    Some(match m {
        "bnone" => 0,
        "beq" => 1,
        "blt" => 2,
        "bltu" => 3,
        "ball" => 4,
        "bbc" => 5,
        "bany" => 8,
        "bne" => 9,
        "bge" => 10,
        "bgeu" => 11,
        "bnall" => 12,
        "bbs" => 13,
        _ => return None,
    })
}

fn slot1_15(v: u32, inferred: bool) -> Result<Slot, String> {
    let (op, a, b, off) = (v >> 26, v & 15, (v >> 4) & 15, sx((v >> 8) & 0x3ffff, 18));
    let m = if op & 1 == 0 && (1..25).contains(&(op >> 1)) { BR15[(op >> 1) as usize] } else { "" };
    if m.is_empty() || (matches!(m, "ball" | "bbc" | "bbs") && !inferred) {
        return bad("op0=15 branch", v as u64);
    }
    let bz = match m {
        "beqz" => Some(0),
        "bnez" => Some(1),
        "bltz" => Some(2),
        "bgez" => Some(3),
        _ => None,
    };
    if let Some(k) = bz {
        if (-2048..2048).contains(&off) {
            return Ok(Slot::Word((off as u32 & 0xfff) << 12 | b << 8 | k << 6 | 0x16));
        }
        return Ok(Slot::Branch(m, b as u8, None, off));
    }
    let fits = (-128..128).contains(&off);
    let o8 = (off as u32 & 0xff) << 16;
    if let Some(r) = b_r(m) {
        return Ok(if fits { Slot::Word(o8 | r << 12 | b << 8 | a << 4 | 7) } else { Slot::Branch(m, b as u8, Some(a as i32), off) });
    }
    if m == "bbci" || m == "bbsi" {
        let r = if m == "bbci" { 6 } else { 14 };
        return Ok(if fits { Slot::Word(o8 | r << 12 | b << 8 | a << 4 | 7) } else { Slot::Branch(m, b as u8, Some(a as i32), off) });
    }
    let (n, mm) = match m {
        "beqi" => (2, 0),
        "bnei" => (2, 1),
        "blti" => (2, 2),
        "bgei" => (2, 3),
        "bltui" => (3, 2),
        _ => (3, 3), // bgeui
    };
    if fits {
        return Ok(Slot::Word(o8 | a << 12 | b << 8 | mm << 6 | n << 4 | 6));
    }
    let k = if n == 3 { B4CONSTU[a as usize] } else { B4CONST[a as usize] };
    Ok(Slot::Branch(m, b as u8, Some(k), off))
}

/// Operations of an 8-byte bundle, NOPs omitted.
pub fn decode_bundle(data: &[u8; 8], inferred: bool) -> Result<Vec<Slot>, String> {
    let w = u64::from_le_bytes(*data);
    let fmt = w & 15;
    if !(fmt == 14 || fmt == 15) || (w >> 26) & 3 != 0 {
        return bad("bundle format", w);
    }
    let mut ops = Vec::with_capacity(3);
    if let Some(x) = slot0(((w >> 4) & 0xffffff) as u32, inferred)? {
        ops.push(Slot::Word(x));
    }
    if fmt == 14 {
        if let Some(x) = slot1_14(((w >> 28) & 0xfffff) as u32, inferred)? {
            ops.push(Slot::Word(x));
        }
        if let Some(x) = slot2_14((w >> 48) as u32, inferred)? {
            ops.push(Slot::Word(x));
        }
    } else {
        if w >> 60 != 0 {
            return bad("op0=15 bundle", w);
        }
        ops.push(slot1_15(((w >> 28) & 0xffffffff) as u32, inferred)?);
    }
    Ok(ops)
}
