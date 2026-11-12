// SPDX-License-Identifier: BSD-2-Clause-Patent
//! MCS-51 interpreter for the EC firmware (ITE IT5570).
//!
//! Code space: 0x0000-0x7FFF is the common area (flash 0x0000-0x7FFF);
//! 0x8000-0xFFFF is the bank selected by P1[2:0] (bank n maps flash
//! 0x8000*n), as Radxa's Keil build switches banks (?B_SWITCH, P1.0-P1.2).
//! Code bytes, external data (MOVX) and SFR writes go to the `Bus` (the
//! IT5570 model, chip.rs). Port of scripts/open/i8051_emu.py, instruction
//! for instruction (its quirks included: the timers' mode 0 counts 16
//! bits, PCON's power-down wakes at once, the parity bit is computed on
//! every read of PSW).
//!
//! An instruction is atomic towards the model only as far as the 8051 is:
//! the one fault that can stop it (a code fetch from the e-flash while it
//! is busy) is taken before anything of the instruction happened, except
//! for MOVC, whose fetch is its last action.

pub const ACC: u8 = 0xE0;
pub const B: u8 = 0xF0;
pub const PSW: u8 = 0xD0;
pub const SP: u8 = 0x81;
pub const DPL: u8 = 0x82;
pub const DPH: u8 = 0x83;
pub const IE: u8 = 0xA8;
pub const IP: u8 = 0xB8;
pub const P1: u8 = 0x90;
pub const P2: u8 = 0xA0;

/// Why the CPU stopped: the model refused a code fetch, or an undefined opcode.
#[derive(Debug, Clone)]
pub struct Halt(pub String);

pub trait Bus {
    /// The code byte at physical address `phys` (bank applied).
    fn code(&mut self, phys: u32) -> Result<u8, Halt>;
    fn xrd(&mut self, a: u16) -> u8;
    fn xwr(&mut self, a: u16, v: u8);
    /// An SFR other than ACC, B, SP, DPL, DPH, PSW was written.
    fn sfr_write(&mut self, a: u8, v: u8);
}

/// The interrupt vectors and their IE/IP bit.
pub const VECTORS: [(u16, u8); 6] = [(0x03, 0), (0x0B, 1), (0x13, 2), (0x1B, 3), (0x23, 4), (0x2B, 5)];

/// Machine cycles per opcode (ec_emu.py's _CYC).
pub static CYC: [u8; 256] = {
    let mut t = [1u8; 256];
    let two: [u8; 40] = [
        0x02, 0x12, 0x22, 0x32, 0x80, 0x73, 0x40, 0x50, 0x60, 0x70, 0x20, 0x30, 0x10, 0xD5, 0xE0, 0xE2, 0xE3, 0xF0,
        0xF2, 0xF3, 0x83, 0x93, 0x90, 0xA3, 0x85, 0x75, 0x86, 0x87, 0xA6, 0xA7, 0x43, 0x53, 0x63, 0x72, 0x82, 0xA0,
        0xB0, 0x92, 0xC0, 0xD0,
    ];
    let mut i = 0;
    while i < two.len() {
        t[two[i] as usize] = 2;
        i += 1;
    }
    let mut o = 0xB4;
    while o < 0xC0 {
        t[o] = 2;
        o += 1;
    }
    o = 0xD8;
    while o < 0xE0 {
        t[o] = 2;
        o += 1;
    }
    o = 0x88;
    while o < 0x90 {
        t[o] = 2;
        o += 1;
    }
    o = 0xA8;
    while o < 0xB0 {
        t[o] = 2;
        o += 1;
    }
    i = 0;
    while i < 8 {
        t[0x01 + 0x20 * i] = 2;
        t[0x11 + 0x20 * i] = 2;
        i += 1;
    }
    t[0xA4] = 4;
    t[0x84] = 4;
    t
};

/// Instruction lengths (for coverage: the bytes an executed instruction covers).
pub fn op_len(op: u8) -> u32 {
    let lo = op & 0x0F;
    let hi = op >> 4;
    if lo >= 8 {
        return match hi {
            0x7 | 0x8 | 0xA | 0xD => 2,
            0xB => 3,
            _ => 1,
        };
    }
    if lo == 6 || lo == 7 {
        return match hi {
            0x7 | 0x8 | 0xA => 2,
            0xB => 3,
            _ => 1,
        };
    }
    if lo == 1 {
        return 2;
    }
    match op {
        0x02 | 0x12 | 0x10 | 0x20 | 0x30 | 0x43 | 0x53 | 0x63 | 0x75 | 0x85 | 0x90 | 0xB4 | 0xB5 | 0xD5 => 3,
        0x05 | 0x15 | 0x24 | 0x25 | 0x34 | 0x35 | 0x40 | 0x42 | 0x44 | 0x45 | 0x50 | 0x52 | 0x54 | 0x55 | 0x60
        | 0x62 | 0x64 | 0x65 | 0x70 | 0x72 | 0x74 | 0x80 | 0x82 | 0x92 | 0x94 | 0x95 | 0xA0 | 0xA2 | 0xB0 | 0xB2
        | 0xC0 | 0xC2 | 0xC5 | 0xD0 | 0xD2 | 0xE5 | 0xF5 => 2,
        _ => 1,
    }
}

#[derive(Clone)]
pub struct Cpu {
    pub iram: [u8; 256],
    pub sfr: [u8; 256],
    pub pc: u16,
    /// priority levels being serviced
    pub in_isr: Vec<u8>,
    /// pending interrupt vectors: bit i = VECTORS[i]
    pub pending: u8,
}

fn rel(pc: u16, ln: u16, off: u8) -> u16 {
    pc.wrapping_add(ln).wrapping_add(off as i8 as i16 as u16)
}

impl Cpu {
    pub fn new() -> Cpu {
        let mut c = Cpu { iram: [0; 256], sfr: [0; 256], pc: 0, in_isr: Vec::new(), pending: 0 };
        c.sfr[SP as usize] = 7;
        c.sfr[P1 as usize] = 0xFF;
        c
    }

    pub fn bank(&self) -> u32 {
        (self.sfr[P1 as usize] & 7) as u32
    }

    /// The physical code address of `a` in the current bank.
    pub fn phys(&self, a: u16) -> u32 {
        if a < 0x8000 {
            a as u32
        } else {
            (self.bank() * 0x8000 + (a as u32 - 0x8000)) & 0x1FFFF
        }
    }

    pub fn code<Bu: Bus>(&self, bus: &mut Bu, a: u16) -> Result<u8, Halt> {
        bus.code(self.phys(a))
    }

    fn psw(&self) -> u8 {
        let p = self.sfr[PSW as usize] & 0xFE;
        p | (self.sfr[ACC as usize].count_ones() as u8 & 1)
    }

    pub fn rd(&self, a: u8) -> u8 {
        if a < 0x80 {
            return self.iram[a as usize];
        }
        if a == PSW {
            return self.psw();
        }
        self.sfr[a as usize]
    }

    pub fn wr<Bu: Bus>(&mut self, bus: &mut Bu, a: u8, v: u8) {
        if a < 0x80 {
            self.iram[a as usize] = v;
            return;
        }
        self.sfr[a as usize] = v;
        if !matches!(a, ACC | B | SP | DPL | DPH | PSW) {
            bus.sfr_write(a, v);
        }
    }

    fn bit_rd(&self, b: u8) -> u8 {
        if b < 0x80 {
            return (self.iram[0x20 + (b >> 3) as usize] >> (b & 7)) & 1;
        }
        (self.rd(b & 0xF8) >> (b & 7)) & 1
    }

    fn bit_wr<Bu: Bus>(&mut self, bus: &mut Bu, b: u8, v: u8) {
        let m = 1u8 << (b & 7);
        if b < 0x80 {
            let a = 0x20 + (b >> 3) as usize;
            self.iram[a] = if v != 0 { self.iram[a] | m } else { self.iram[a] & !m };
            return;
        }
        let a = b & 0xF8;
        // read-modify-write of ports uses the latch (sfr), not the pins
        let cur = if a != PSW { self.sfr[a as usize] } else { self.psw() };
        self.wr(bus, a, if v != 0 { cur | m } else { cur & !m });
    }

    /// read-modify-write instructions read the port latch, not the pins
    fn sfr_latch(&self, a: u8) -> u8 {
        if a < 0x80 {
            return self.iram[a as usize];
        }
        if a == PSW {
            return self.psw();
        }
        self.sfr[a as usize]
    }

    fn bit_latch(&self, b: u8) -> u8 {
        if b < 0x80 {
            return self.bit_rd(b);
        }
        (self.sfr_latch(b & 0xF8) >> (b & 7)) & 1
    }

    #[inline]
    pub fn a(&self) -> u8 {
        self.sfr[ACC as usize]
    }

    #[inline]
    fn set_a(&mut self, v: u8) {
        self.sfr[ACC as usize] = v;
    }

    fn regaddr(&self, n: u8) -> usize {
        ((self.sfr[PSW as usize] & 0x18) + n) as usize
    }

    pub fn r(&self, n: u8) -> u8 {
        self.iram[self.regaddr(n)]
    }

    fn cy(&self) -> u8 {
        self.sfr[PSW as usize] >> 7
    }

    fn set_cy(&mut self, v: bool) {
        let p = &mut self.sfr[PSW as usize];
        *p = (*p & 0x7F) | if v { 0x80 } else { 0 };
    }

    pub fn dptr(&self) -> u16 {
        ((self.sfr[DPH as usize] as u16) << 8) | self.sfr[DPL as usize] as u16
    }

    fn set_dptr(&mut self, v: u16) {
        self.sfr[DPH as usize] = (v >> 8) as u8;
        self.sfr[DPL as usize] = v as u8;
    }

    pub fn push(&mut self, v: u8) {
        let sp = self.sfr[SP as usize].wrapping_add(1);
        self.sfr[SP as usize] = sp;
        self.iram[sp as usize] = v;
    }

    pub fn pop(&mut self) -> u8 {
        let sp = self.sfr[SP as usize];
        let v = self.iram[sp as usize];
        self.sfr[SP as usize] = sp.wrapping_sub(1);
        v
    }

    fn add(&mut self, v: u8, c: u8) {
        let a = self.a();
        let r = a as u32 + v as u32 + c as u32;
        let mut psw = self.sfr[PSW as usize] & 0x3B;
        if r > 0xFF {
            psw |= 0x80;
        }
        if (a & 0xF) as u32 + (v & 0xF) as u32 + c as u32 > 0xF {
            psw |= 0x40;
        }
        let r8 = r as u8;
        if (a ^ r8) & (v ^ r8) & 0x80 != 0 {
            psw |= 0x04;
        }
        self.sfr[PSW as usize] = psw;
        self.set_a(r8);
    }

    fn subb(&mut self, v: u8) {
        let a = self.a();
        let c = self.cy();
        let r = a as i32 - v as i32 - c as i32;
        let mut psw = self.sfr[PSW as usize] & 0x3B;
        if r < 0 {
            psw |= 0x80;
        }
        if (a & 0xF) as i32 - (v & 0xF) as i32 - (c as i32) < 0 {
            psw |= 0x40;
        }
        // Python: (a ^ v) & (a ^ r) & 0x80 with r unmasked (negative): bit 7 of r's two's complement
        if (a as i32 ^ v as i32) & (a as i32 ^ r) & 0x80 != 0 {
            psw |= 0x04;
        }
        self.sfr[PSW as usize] = psw;
        self.set_a(r as u8);
    }

    // ---- interrupts ----
    /// Enter the highest-priority pending enabled interrupt, if any may
    /// interrupt what runs. Returns the vector entered.
    pub fn check_irq(&mut self) -> Option<u16> {
        if self.pending == 0 {
            return None;
        }
        let ie = self.sfr[IE as usize];
        if ie & 0x80 == 0 {
            return None;
        }
        let ip = self.sfr[IP as usize];
        let cur: i32 = self.in_isr.last().map(|&l| l as i32).unwrap_or(-1);
        let mut best: Option<(usize, u16, i32)> = None;
        for (i, &(vec, bit)) in VECTORS.iter().enumerate() {
            if self.pending >> i & 1 != 0 && ie & (1 << bit) != 0 {
                let lvl = ((ip >> bit) & 1) as i32;
                if lvl > cur && best.map_or(true, |b| lvl > b.2) {
                    best = Some((i, vec, lvl));
                }
            }
        }
        let (i, vec, lvl) = best?;
        self.pending &= !(1 << i);
        let pc = self.pc;
        self.push(pc as u8);
        self.push((pc >> 8) as u8);
        self.in_isr.push(lvl as u8);
        self.pc = vec;
        Some(vec)
    }

    /// Execute one instruction; the opcode executed.
    pub fn step<Bu: Bus>(&mut self, bus: &mut Bu) -> Result<u8, Halt> {
        let pc = self.pc;
        let op = self.code(bus, pc)?;
        let b1 = self.code(bus, pc.wrapping_add(1))?;
        let b2 = self.code(bus, pc.wrapping_add(2))?;
        let lo = op & 0x0F;
        let hi = op >> 4;
        let psw_bank = self.sfr[PSW as usize] & 0x18;
        // register-direct group (low nibble 8-F)
        if lo >= 8 {
            let ra = (psw_bank + (op & 7)) as usize;
            let r = self.iram[ra];
            match hi {
                0x0 => {
                    self.iram[ra] = r.wrapping_add(1);
                    self.pc = pc.wrapping_add(1)
                }
                0x1 => {
                    self.iram[ra] = r.wrapping_sub(1);
                    self.pc = pc.wrapping_add(1)
                }
                0x2 => {
                    self.add(r, 0);
                    self.pc = pc.wrapping_add(1)
                }
                0x3 => {
                    let c = self.cy();
                    self.add(r, c);
                    self.pc = pc.wrapping_add(1)
                }
                0x4 => {
                    self.set_a(self.a() | r);
                    self.pc = pc.wrapping_add(1)
                }
                0x5 => {
                    self.set_a(self.a() & r);
                    self.pc = pc.wrapping_add(1)
                }
                0x6 => {
                    self.set_a(self.a() ^ r);
                    self.pc = pc.wrapping_add(1)
                }
                0x7 => {
                    self.iram[ra] = b1;
                    self.pc = pc.wrapping_add(2)
                }
                0x8 => {
                    self.wr(bus, b1, r);
                    self.pc = pc.wrapping_add(2)
                }
                0x9 => {
                    self.subb(r);
                    self.pc = pc.wrapping_add(1)
                }
                0xA => {
                    self.iram[ra] = self.rd(b1);
                    self.pc = pc.wrapping_add(2)
                }
                0xB => {
                    self.set_cy(r < b1);
                    self.pc = if r != b1 { rel(pc, 3, b2) } else { pc.wrapping_add(3) }
                }
                0xC => {
                    self.iram[ra] = self.a();
                    self.set_a(r);
                    self.pc = pc.wrapping_add(1)
                }
                0xD => {
                    let r = r.wrapping_sub(1);
                    self.iram[ra] = r;
                    self.pc = if r != 0 { rel(pc, 2, b1) } else { pc.wrapping_add(2) }
                }
                0xE => {
                    self.set_a(r);
                    self.pc = pc.wrapping_add(1)
                }
                _ => {
                    self.iram[ra] = self.a();
                    self.pc = pc.wrapping_add(1)
                }
            }
            return Ok(op);
        }
        // @Ri group (low nibble 6, 7)
        if (lo == 6 || lo == 7) && hi != 0x8 && hi != 0xA {
            let ia = self.iram[(psw_bank + (op & 1)) as usize] as usize;
            let v = self.iram[ia];
            match hi {
                0x0 => self.iram[ia] = v.wrapping_add(1),
                0x1 => self.iram[ia] = v.wrapping_sub(1),
                0x2 => self.add(v, 0),
                0x3 => {
                    let c = self.cy();
                    self.add(v, c)
                }
                0x4 => self.set_a(self.a() | v),
                0x5 => self.set_a(self.a() & v),
                0x6 => self.set_a(self.a() ^ v),
                0x7 => {
                    self.iram[ia] = b1;
                    self.pc = pc.wrapping_add(2);
                    return Ok(op);
                }
                0x9 => self.subb(v),
                0xB => {
                    self.set_cy(v < b1);
                    self.pc = if v != b1 { rel(pc, 3, b2) } else { pc.wrapping_add(3) };
                    return Ok(op);
                }
                0xC => {
                    self.iram[ia] = self.a();
                    self.set_a(v);
                }
                0xD => {
                    let a = self.a();
                    self.set_a((a & 0xF0) | (v & 0x0F));
                    self.iram[ia] = (v & 0xF0) | (a & 0x0F);
                }
                0xE => self.set_a(v),
                _ => self.iram[ia] = self.a(),
            }
            self.pc = pc.wrapping_add(1);
            return Ok(op);
        }
        if lo == 1 {
            // AJMP / ACALL
            let t = (pc.wrapping_add(2) & 0xF800) | (((op & 0xE0) as u16) << 3) | b1 as u16;
            if op & 0x10 != 0 {
                let ret = pc.wrapping_add(2);
                self.push(ret as u8);
                self.push((ret >> 8) as u8);
            }
            self.pc = t;
            return Ok(op);
        }
        let n1 = pc.wrapping_add(1);
        let n2 = pc.wrapping_add(2);
        let n3 = pc.wrapping_add(3);
        match op {
            0x00 => self.pc = n1,
            0x02 => self.pc = ((b1 as u16) << 8) | b2 as u16,
            0x03 => {
                self.set_a(self.a().rotate_right(1));
                self.pc = n1
            }
            0x04 => {
                self.set_a(self.a().wrapping_add(1));
                self.pc = n1
            }
            0x05 => {
                let v = self.rd(b1).wrapping_add(1);
                self.wr(bus, b1, v);
                self.pc = n2
            }
            0x10 => {
                if self.bit_rd(b1) != 0 {
                    self.bit_wr(bus, b1, 0);
                    self.pc = rel(pc, 3, b2)
                } else {
                    self.pc = n3
                }
            }
            0x12 => {
                self.push(n3 as u8);
                self.push((n3 >> 8) as u8);
                self.pc = ((b1 as u16) << 8) | b2 as u16
            }
            0x13 => {
                let a = self.a();
                let c = self.cy();
                self.set_cy(a & 1 != 0);
                self.set_a((a >> 1) | (c << 7));
                self.pc = n1
            }
            0x14 => {
                self.set_a(self.a().wrapping_sub(1));
                self.pc = n1
            }
            0x15 => {
                let v = self.rd(b1).wrapping_sub(1);
                self.wr(bus, b1, v);
                self.pc = n2
            }
            0x20 => self.pc = if self.bit_rd(b1) != 0 { rel(pc, 3, b2) } else { n3 },
            0x22 => {
                let h = self.pop();
                let l = self.pop();
                self.pc = ((h as u16) << 8) | l as u16
            }
            0x23 => {
                self.set_a(self.a().rotate_left(1));
                self.pc = n1
            }
            0x24 => {
                self.add(b1, 0);
                self.pc = n2
            }
            0x25 => {
                let v = self.rd(b1);
                self.add(v, 0);
                self.pc = n2
            }
            0x30 => self.pc = if self.bit_rd(b1) == 0 { rel(pc, 3, b2) } else { n3 },
            0x32 => {
                let h = self.pop();
                let l = self.pop();
                self.pc = ((h as u16) << 8) | l as u16;
                self.in_isr.pop();
            }
            0x33 => {
                let a = self.a();
                let c = self.cy();
                self.set_cy(a >> 7 != 0);
                self.set_a((a << 1) | c);
                self.pc = n1
            }
            0x34 => {
                let c = self.cy();
                self.add(b1, c);
                self.pc = n2
            }
            0x35 => {
                let v = self.rd(b1);
                let c = self.cy();
                self.add(v, c);
                self.pc = n2
            }
            0x40 => self.pc = if self.cy() != 0 { rel(pc, 2, b1) } else { n2 },
            0x42 => {
                let v = self.sfr_latch(b1) | self.a();
                self.wr(bus, b1, v);
                self.pc = n2
            }
            0x43 => {
                let v = self.sfr_latch(b1) | b2;
                self.wr(bus, b1, v);
                self.pc = n3
            }
            0x44 => {
                self.set_a(self.a() | b1);
                self.pc = n2
            }
            0x45 => {
                self.set_a(self.a() | self.rd(b1));
                self.pc = n2
            }
            0x50 => self.pc = if self.cy() == 0 { rel(pc, 2, b1) } else { n2 },
            0x52 => {
                let v = self.sfr_latch(b1) & self.a();
                self.wr(bus, b1, v);
                self.pc = n2
            }
            0x53 => {
                let v = self.sfr_latch(b1) & b2;
                self.wr(bus, b1, v);
                self.pc = n3
            }
            0x54 => {
                self.set_a(self.a() & b1);
                self.pc = n2
            }
            0x55 => {
                self.set_a(self.a() & self.rd(b1));
                self.pc = n2
            }
            0x60 => self.pc = if self.a() == 0 { rel(pc, 2, b1) } else { n2 },
            0x62 => {
                let v = self.sfr_latch(b1) ^ self.a();
                self.wr(bus, b1, v);
                self.pc = n2
            }
            0x63 => {
                let v = self.sfr_latch(b1) ^ b2;
                self.wr(bus, b1, v);
                self.pc = n3
            }
            0x64 => {
                self.set_a(self.a() ^ b1);
                self.pc = n2
            }
            0x65 => {
                self.set_a(self.a() ^ self.rd(b1));
                self.pc = n2
            }
            0x70 => self.pc = if self.a() != 0 { rel(pc, 2, b1) } else { n2 },
            0x72 => {
                let c = self.cy() | self.bit_rd(b1);
                self.set_cy(c != 0);
                self.pc = n2
            }
            0x73 => self.pc = (self.a() as u16).wrapping_add(self.dptr()),
            0x74 => {
                self.set_a(b1);
                self.pc = n2
            }
            0x75 => {
                self.wr(bus, b1, b2);
                self.pc = n3
            }
            0x80 => self.pc = rel(pc, 2, b1),
            0x82 => {
                let c = self.cy() & self.bit_rd(b1);
                self.set_cy(c != 0);
                self.pc = n2
            }
            0x83 => {
                let v = self.code(bus, n1.wrapping_add(self.a() as u16))?;
                self.set_a(v);
                self.pc = n1
            }
            0x84 => {
                let (a, b) = (self.a(), self.sfr[B as usize]);
                let mut psw = self.sfr[PSW as usize] & 0x7B;
                if b == 0 {
                    psw |= 0x04;
                } else {
                    self.set_a(a / b);
                    self.sfr[B as usize] = a % b;
                }
                self.sfr[PSW as usize] = psw;
                self.pc = n1
            }
            0x85 => {
                let v = self.rd(b1);
                self.wr(bus, b2, v);
                self.pc = n3
            }
            0x86 | 0x87 => {
                let v = self.iram[self.r(op & 1) as usize];
                self.wr(bus, b1, v);
                self.pc = n2
            }
            0x90 => {
                self.set_dptr(((b1 as u16) << 8) | b2 as u16);
                self.pc = n3
            }
            0x92 => {
                let c = self.cy();
                self.bit_wr(bus, b1, c);
                self.pc = n2
            }
            0x93 => {
                let v = self.code(bus, (self.a() as u16).wrapping_add(self.dptr()))?;
                self.set_a(v);
                self.pc = n1
            }
            0x94 => {
                self.subb(b1);
                self.pc = n2
            }
            0x95 => {
                let v = self.rd(b1);
                self.subb(v);
                self.pc = n2
            }
            0xA0 => {
                let c = self.cy() | (1 - self.bit_rd(b1));
                self.set_cy(c != 0);
                self.pc = n2
            }
            0xA2 => {
                let c = self.bit_rd(b1);
                self.set_cy(c != 0);
                self.pc = n2
            }
            0xA3 => {
                self.set_dptr(self.dptr().wrapping_add(1));
                self.pc = n1
            }
            0xA4 => {
                let r = self.a() as u16 * self.sfr[B as usize] as u16;
                self.set_a(r as u8);
                self.sfr[B as usize] = (r >> 8) as u8;
                let mut psw = self.sfr[PSW as usize] & 0x7B;
                if r > 0xFF {
                    psw |= 0x04;
                }
                self.sfr[PSW as usize] = psw;
                self.pc = n1
            }
            0xA5 => return Err(Halt(format!("undefined opcode A5 at {pc:04X}"))),
            0xA6 | 0xA7 => {
                let ia = self.r(op & 1) as usize;
                self.iram[ia] = self.rd(b1);
                self.pc = n2
            }
            0xB0 => {
                let c = self.cy() & (1 - self.bit_rd(b1));
                self.set_cy(c != 0);
                self.pc = n2
            }
            0xB2 => {
                let v = 1 - self.bit_latch(b1);
                self.bit_wr(bus, b1, v);
                self.pc = n2
            }
            0xB3 => {
                let c = self.cy();
                self.set_cy(c == 0);
                self.pc = n1
            }
            0xB4 => {
                let a = self.a();
                self.set_cy(a < b1);
                self.pc = if a != b1 { rel(pc, 3, b2) } else { n3 }
            }
            0xB5 => {
                let a = self.a();
                let v = self.rd(b1);
                self.set_cy(a < v);
                self.pc = if a != v { rel(pc, 3, b2) } else { n3 }
            }
            0xC0 => {
                let v = self.rd(b1);
                self.push(v);
                self.pc = n2
            }
            0xC2 => {
                self.bit_wr(bus, b1, 0);
                self.pc = n2
            }
            0xC3 => {
                self.set_cy(false);
                self.pc = n1
            }
            0xC4 => {
                self.set_a(self.a().rotate_left(4));
                self.pc = n1
            }
            0xC5 => {
                let v = self.rd(b1);
                let a = self.a();
                self.wr(bus, b1, a);
                self.set_a(v);
                self.pc = n2
            }
            0xD0 => {
                let v = self.pop();
                self.wr(bus, b1, v);
                self.pc = n2
            }
            0xD2 => {
                self.bit_wr(bus, b1, 1);
                self.pc = n2
            }
            0xD3 => {
                self.set_cy(true);
                self.pc = n1
            }
            0xD4 => {
                let mut a = self.a() as u32;
                let mut c = self.cy();
                if (a & 0xF) > 9 || self.sfr[PSW as usize] & 0x40 != 0 {
                    a += 6;
                }
                if (a >> 4) > 9 || c != 0 || a > 0xFF {
                    a += 0x60;
                }
                if a > 0xFF {
                    c = 1;
                }
                self.set_a(a as u8);
                self.set_cy(c != 0);
                self.pc = n1
            }
            0xD5 => {
                let v = self.rd(b1).wrapping_sub(1);
                self.wr(bus, b1, v);
                self.pc = if v != 0 { rel(pc, 3, b2) } else { n3 }
            }
            0xE0 => {
                let v = bus.xrd(self.dptr());
                self.set_a(v);
                self.pc = n1
            }
            0xE2 | 0xE3 => {
                let a = self.r(op & 1) as u16 | ((self.sfr[P2 as usize] as u16) << 8);
                let v = bus.xrd(a);
                self.set_a(v);
                self.pc = n1
            }
            0xE4 => {
                self.set_a(0);
                self.pc = n1
            }
            0xE5 => {
                self.set_a(self.rd(b1));
                self.pc = n2
            }
            0xF0 => {
                bus.xwr(self.dptr(), self.a());
                self.pc = n1
            }
            0xF2 | 0xF3 => {
                let a = self.r(op & 1) as u16 | ((self.sfr[P2 as usize] as u16) << 8);
                bus.xwr(a, self.a());
                self.pc = n1
            }
            0xF4 => {
                self.set_a(!self.a());
                self.pc = n1
            }
            0xF5 => {
                let a = self.a();
                self.wr(bus, b1, a);
                self.pc = n2
            }
            _ => return Err(Halt(format!("opcode {op:02X} at {pc:04X} not decoded"))),
        }
        Ok(op)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    struct Mem {
        code: Vec<u8>,
        x: Vec<u8>,
    }

    impl Bus for Mem {
        fn code(&mut self, phys: u32) -> Result<u8, Halt> {
            Ok(*self.code.get(phys as usize).unwrap_or(&0))
        }
        fn xrd(&mut self, a: u16) -> u8 {
            self.x[a as usize]
        }
        fn xwr(&mut self, a: u16, v: u8) {
            self.x[a as usize] = v
        }
        fn sfr_write(&mut self, _a: u8, _v: u8) {}
    }

    fn run(code: &[u8], steps: usize) -> Cpu {
        let mut m = Mem { code: code.to_vec(), x: vec![0; 0x10000] };
        let mut c = Cpu::new();
        for _ in 0..steps {
            c.step(&mut m).unwrap();
        }
        c
    }

    #[test]
    fn arith() {
        // MOV A,#0x99; ADD A,#0x01; DA A -> 0x00, CY
        let c = run(&[0x74, 0x99, 0x24, 0x01, 0xD4], 3);
        assert_eq!(c.a(), 0x00);
        assert_eq!(c.cy(), 1);
        // MOV A,#5; CLR C; SUBB A,#7 -> 0xFE, CY, AC
        let c = run(&[0x74, 0x05, 0xC3, 0x94, 0x07], 3);
        assert_eq!(c.a(), 0xFE);
        assert_eq!(c.sfr[PSW as usize] & 0xC4, 0xC0);
        // MOV A,#0x80; CLR C; SUBB A,#1 -> 0x7F, OV
        let c = run(&[0x74, 0x80, 0xC3, 0x94, 0x01], 3);
        assert_eq!(c.a(), 0x7F);
        assert_eq!(c.sfr[PSW as usize] & 0x84, 0x04);
        // MOV A,#200; MOV B,#7; DIV AB
        let c = run(&[0x74, 200, 0x75, 0xF0, 7, 0x84], 3);
        assert_eq!((c.a(), c.sfr[B as usize]), (28, 4));
    }

    #[test]
    fn lengths() {
        for op in 0..=255u8 {
            if op == 0xA5 {
                continue;
            }
            // every non-jump instruction moves the pc by its length
            let mut code = vec![op, 0x01, 0x00, 0, 0, 0];
            if matches!(op, 0x22 | 0x32 | 0x73) || op & 0x0F == 1 || matches!(op, 0x02 | 0x12 | 0x80) {
                continue;
            }
            code.resize(16, 0);
            let mut m = Mem { code, x: vec![0; 0x10000] };
            let mut c = Cpu::new();
            // conditions so that no conditional branch is taken
            c.sfr[ACC as usize] = 0x01;
            c.iram[0] = 0x00;
            c.step(&mut m).unwrap();
            let ln = op_len(op) as u16;
            let ok = c.pc == ln || {
                // a taken branch: the offset 0 lands at pc + len as well, except DJNZ/CJNE
                let r = c.pc.wrapping_sub(ln);
                r == 0
            };
            assert!(ok || matches!(op, 0x10 | 0x20 | 0x30 | 0x40 | 0x50 | 0x60 | 0x70 | 0xB4 | 0xB5 | 0xD5) || op & 0xF8 == 0xD8 || op & 0xF8 == 0xB8 || op & 0xFE == 0xB6, "op {op:02X} pc {:X} len {ln}", c.pc);
        }
    }
}
