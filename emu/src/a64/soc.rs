// SPDX-License-Identifier: BSD-2-Clause-Patent
//! The Sky1 as TF-A sees it: DRAM, and the devices BL2 and BL31 touch.
//!
//! What is modelled is inferred from the code that drives it (the public
//! TF-A tree's drivers, and CIX's images where they do more).  A register
//! with no behaviour reads back what was written, 0 before; every device
//! access is logged in order, which is what the tests compare.  Devices
//! with behaviour:
//!
//! * the Cadence xSPI controller (0x04180000, its slave DMA port at
//!   0x10000) with a SPI NOR behind it: a whole flash image;
//! * the console UART (0x040d0000, PL011-like);
//! * the system counter (0x16002000) and the generic timer registers;
//! * four TZC-400 (0x0c0b0000 + n * 0x20000);
//! * the GICv3 distributor and redistributors (0x0e010000, 0x0e090000)
//!   and the CPU interface's system registers;
//! * the AP's mailboxes: to the PM (SCMI, with a responder) and to the SE;
//! * what the SE leaves in DRAM for BL2: the key-management block at
//!   0x801ff000 and the DRAM description at 0x83c00000.
//!
//! Time is simulated time in nanoseconds, and not any CPU's instruction
//! count: it passes when the firmware looks at it (a microsecond for every
//! read of the counter, so a delay loop ends and a time-out can expire)
//! and when the harness lets it pass (a core asleep).  Two images that
//! read the clock at the same points of their work therefore see the same
//! time, however many instructions each took to get there, and what they
//! derive from it (the boot record, the counter rewritten after a suspend)
//! is compared exactly.  The system counter runs at 1 GHz (the SE programs
//! CNTFID0 so).

use super::cpu::{key, Bus};
use std::collections::{BTreeMap, HashMap};
use std::rc::Rc;

pub type Page = [u8; 4096];
static ZERO: Page = [0; 4096];

pub const DRAM_BASE: u64 = 0x8000_0000;
pub const UART: u64 = 0x040d_0000;
pub const XSPI: u64 = 0x0418_0000;
pub const XSPI_SDMA: u64 = 0x1_0000;
pub const SYSCNT: u64 = 0x1600_2000;
pub const TZC: u64 = 0x0c0b_0000;
pub const GICD: u64 = 0x0e01_0000;
pub const GICR: u64 = 0x0e09_0000;
/// The AP's channel to the PM: 32 message words (an SCMI shared-memory area), then the doorbell.
pub const PM_MBOX: u64 = 0x065b_0000;
/// The AP's channel to the SE (TEE side): 32 message words, the doorbell, the status.
pub const SE_MBOX: u64 = 0x0508_0000;
/// The cluster's and the cores' power policy units, and the cores' reset vector registers.
pub const PPU_CLUSTER: u64 = 0x0f03_0000;
pub const PPU_CORE: u64 = 0x0f08_0000;
pub const CPU_RVB: u64 = 0x0fc5_0308;
pub const KM_BLOCK: u64 = 0x801f_f000;
pub const MEM_OUTPUT: u64 = 0x83c0_0000;
pub const CORES: usize = 12;
/// What reading the counter costs in simulated time, ns.
pub const TICK: u64 = 1000;

const CNTPCT_EL0: u16 = key(3, 3, 14, 0, 1);
const CNTVCT_EL0: u16 = key(3, 3, 14, 0, 2);
const ICC_IAR0_EL1: u16 = key(3, 0, 12, 8, 0);
const ICC_EOIR0_EL1: u16 = key(3, 0, 12, 8, 1);
const ICC_HPPIR0_EL1: u16 = key(3, 0, 12, 8, 2);
const ICC_IAR1_EL1: u16 = key(3, 0, 12, 12, 0);
const ICC_EOIR1_EL1: u16 = key(3, 0, 12, 12, 1);
const ICC_HPPIR1_EL1: u16 = key(3, 0, 12, 12, 2);
const ICC_RPR_EL1: u16 = key(3, 0, 12, 11, 3);
const ICC_SGI0R_EL1: u16 = key(3, 0, 12, 11, 7);
const ICC_SGI1R_EL1: u16 = key(3, 0, 12, 11, 5);
const ICC_ASGI1R_EL1: u16 = key(3, 0, 12, 11, 6);

/// One device access.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Io {
    pub write: bool,
    pub addr: u64,
    pub size: u8,
    pub val: u64,
    pub core: u8,
}

/// DRAM: pages allocated when first written, shared between copies of a
/// machine until written again (a copy costs a thousand reference counts:
/// the tests keep one before every question).
#[derive(Clone)]
pub struct Ram {
    /// 0x8000_0000..0x1_0000_0000 in 2 MiB groups of pages
    lo: Vec<Option<Rc<Group>>>,
    hi: HashMap<u64, Rc<Page>>,
    /// the end of DRAM (exclusive)
    pub end: u64,
}

type Group = [Option<Rc<Page>>; 512];

impl Ram {
    pub fn new(end: u64) -> Ram {
        Ram { lo: vec![None; 0x400], hi: HashMap::new(), end }
    }

    #[inline]
    fn page(&self, addr: u64) -> &Page {
        if addr < 0x1_0000_0000 {
            let i = ((addr - DRAM_BASE) >> 12) as usize;
            match &self.lo[i >> 9] {
                Some(g) => match &g[i & 511] {
                    Some(p) => p,
                    None => &ZERO,
                },
                None => &ZERO,
            }
        } else {
            self.hi.get(&(addr >> 12)).map(|p| &**p).unwrap_or(&ZERO)
        }
    }

    #[inline]
    fn page_mut(&mut self, addr: u64) -> &mut Page {
        let slot = if addr < 0x1_0000_0000 {
            let i = ((addr - DRAM_BASE) >> 12) as usize;
            let g = self.lo[i >> 9].get_or_insert_with(|| Rc::new(std::array::from_fn(|_| None)));
            Rc::make_mut(g)[i & 511].get_or_insert_with(|| Rc::new(ZERO))
        } else {
            self.hi.entry(addr >> 12).or_insert_with(|| Rc::new(ZERO))
        };
        Rc::make_mut(slot)
    }

    #[inline]
    pub fn read(&self, addr: u64, size: u32) -> u64 {
        let o = (addr & 0xfff) as usize;
        let p = self.page(addr);
        if o + size as usize <= 4096 {
            let mut b = [0u8; 8];
            b[..size as usize].copy_from_slice(&p[o..o + size as usize]);
            u64::from_le_bytes(b)
        } else {
            let mut v = 0;
            for i in 0..size as u64 {
                v |= (self.page(addr + i)[((addr + i) & 0xfff) as usize] as u64) << (8 * i);
            }
            v
        }
    }

    #[inline]
    pub fn write(&mut self, addr: u64, size: u32, val: u64) {
        let o = (addr & 0xfff) as usize;
        if o + size as usize <= 4096 {
            let p = self.page_mut(addr);
            p[o..o + size as usize].copy_from_slice(&val.to_le_bytes()[..size as usize]);
        } else {
            for i in 0..size as u64 {
                self.page_mut(addr + i)[((addr + i) & 0xfff) as usize] = (val >> (8 * i)) as u8;
            }
        }
    }

    pub fn bytes(&self, addr: u64, len: usize) -> Vec<u8> {
        let mut out = Vec::with_capacity(len);
        let mut a = addr;
        while out.len() < len {
            let o = (a & 0xfff) as usize;
            let n = (4096 - o).min(len - out.len());
            out.extend_from_slice(&self.page(a)[o..o + n]);
            a += n as u64;
        }
        out
    }

    pub fn load(&mut self, addr: u64, data: &[u8]) {
        let mut a = addr;
        let mut at = 0;
        while at < data.len() {
            let o = (a & 0xfff) as usize;
            let n = (4096 - o).min(data.len() - at);
            self.page_mut(a)[o..o + n].copy_from_slice(&data[at..at + n]);
            a += n as u64;
            at += n;
        }
    }

    /// The pages ever written in [from, to): their addresses.
    pub fn touched(&self, from: u64, to: u64) -> Vec<u64> {
        let mut out = Vec::new();
        let mut a = from & !0xfff;
        while a < to.min(0x1_0000_0000) {
            let i = ((a - DRAM_BASE) >> 12) as usize;
            match &self.lo[i >> 9] {
                None => {
                    a = (a | 0x1f_ffff) + 1;
                    continue;
                }
                Some(g) => {
                    if g[i & 511].is_some() {
                        out.push(a);
                    }
                }
            }
            a += 0x1000;
        }
        out
    }
}

/// The xSPI controller's STIG state and the SPI NOR behind it.
#[derive(Clone, Default)]
pub struct Xspi {
    cmd: [u32; 6],
    /// the instruction waiting for its data phase: (opcode, address, address bytes)
    pending: Option<(u8, u64, u32)>,
    /// bytes the slave DMA port returns next
    out: std::collections::VecDeque<u8>,
    /// a program in progress: (address, bytes still to come)
    program: Option<(u64, u32)>,
    sdma_size: u32,
    sdma_write: bool,
    wel: bool,
    done: bool,
    /// every SPI transaction: (opcode, address, address bytes, data bytes, write)
    pub ops: Vec<(u8, u64, u32, u32, bool)>,
}

#[derive(Clone)]
pub struct Soc {
    pub ram: Ram,
    /// device registers with no behaviour, by word address
    pub regs: HashMap<u64, u32>,
    /// simulated time, ns
    pub time: u64,
    pub io: Vec<Io>,
    pub log_io: bool,
    pub uart: Vec<u8>,
    pub flash: Rc<Vec<u8>>,
    pub jedec: [u8; 3],
    pub xspi: Xspi,
    /// device registers read before anything wrote them, that no model answers: address -> reads
    pub unmodelled: BTreeMap<u64, u32>,
    /// print every device access as it happens
    pub trace: bool,
    /// a device register to report accesses of ($SKY1EMU_WATCH), and whether the last instruction made one
    pub watch: u64,
    pub watch_hit: Option<(bool, u64)>,
    /// messages the AP sent to the PM: [route, header, payload...] (route 0: SCMI, 0x0c: PM message)
    pub pm_msgs: Vec<Vec<u32>>,
    /// messages the AP sent to the SE: [command, parameters...]
    pub se_msgs: Vec<Vec<u32>>,
    /// the PM and the SE answer (false: a scenario without them)
    pub pm_alive: bool,
    pub se_alive: bool,
    /// SCMI power domain states the AP set, and the system power state
    pub pd_state: BTreeMap<u32, u32>,
    pub sys_state: Option<u32>,
    /// the cores' reset address the AP gave the PM (SCMI AP core protocol): (address, attributes)
    pub reset_addr: (u64, u32),
    /// the interrupt each core's CPU interface has pending for EL3 (group 0)
    pub pending: [Option<u32>; CORES],
    /// power policies written to the cores' PPUs: (core, policy)
    pub power: Vec<(u8, u32)>,
    /// a secure group 1 interrupt (OP-TEE's) is pending for a core
    pub pending_s: [bool; CORES],
    /// the xSPI controller never gets idle (a scenario)
    pub xspi_stuck: bool,
    /// the priorities of the interrupts each core has taken and not ended (the last runs)
    pub running: Vec<Vec<u8>>,
    /// SGIs sent: (core, register, value)
    pub sgis: Vec<(u8, u16, u64)>,
    /// interrupts acknowledged and ended: (core, INTID, end)
    pub acks: Vec<(u8, u32, bool)>,
}

impl Soc {
    /// `dram_mb`: installed DRAM in MiB.
    pub fn new(flash: Rc<Vec<u8>>, dram_mb: u32) -> Soc {
        let mut s = Soc {
            ram: Ram::new(DRAM_BASE + dram_mb as u64 * 0x10_0000),
            regs: HashMap::new(),
            time: 0,
            io: Vec::new(),
            log_io: true,
            uart: Vec::new(),
            flash,
            jedec: [0xef, 0x40, 0x17],
            xspi: Xspi::default(),
            unmodelled: BTreeMap::new(),
            trace: false,
            watch: std::env::var("SKY1EMU_WATCH").ok().and_then(|v| crate::util::parse_int(&v)).unwrap_or(0),
            watch_hit: None,
            pm_msgs: Vec::new(),
            se_msgs: Vec::new(),
            pm_alive: true,
            se_alive: true,
            pd_state: BTreeMap::new(),
            sys_state: None,
            reset_addr: (0, 0),
            pending: [None; CORES],
            power: Vec::new(),
            pending_s: [false; CORES],
            xspi_stuck: false,
            running: vec![Vec::new(); CORES],
            sgis: Vec::new(),
            acks: Vec::new(),
        };
        // the PM has left its channel free; the SE's is idle
        s.regs.insert(PM_MBOX + 4, 1);
        // the SE has set the system counter going at 1 GHz
        s.regs.insert(SYSCNT, 3);
        s.regs.insert(SYSCNT + 0x20, 1_000_000_000);
        s
    }

    pub fn reg(&self, addr: u64) -> u32 {
        self.regs.get(&(addr & !3)).copied().unwrap_or(0)
    }

    pub fn set_reg(&mut self, addr: u64, v: u32) {
        self.regs.insert(addr & !3, v);
    }

    /// The console's text since `from`.
    pub fn console(&self, from: usize) -> String {
        String::from_utf8_lossy(&self.uart[from.min(self.uart.len())..]).into_owned()
    }

    fn flash_read(&self, addr: u64, n: usize) -> Vec<u8> {
        (0..n).map(|i| self.flash.get(addr as usize + i).copied().unwrap_or(0xff)).collect()
    }

    /// A STIG command was triggered (CMD_REG_0 written).
    fn xspi_trigger(&mut self) {
        let c = self.xspi.cmd;
        let kind = c[1] & 0x7f;
        self.xspi.done = true;
        if kind == 127 {
            // the data sequence of the pending instruction
            let count = (c[2] >> 16) | ((c[3] & 0xffff) << 16);
            let write = (c[4] >> 4) & 1 != 0;
            let Some((op, addr, nbytes)) = self.xspi.pending.take() else { return };
            self.xspi.ops.push((op, addr, nbytes, count, write));
            self.xspi.sdma_size = count;
            self.xspi.sdma_write = write;
            self.xspi.out.clear();
            if write {
                if op == 0x02 || op == 0x32 || op == 0x12 {
                    self.xspi.program = Some((addr, count));
                }
                return;
            }
            let data: Vec<u8> = match op {
                0x9f => (0..count as usize).map(|i| self.jedec[i % 3]).collect(),
                0x03 | 0x0b | 0x3b | 0x6b | 0xbb | 0xeb | 0x13 | 0x0c | 0x6c | 0xec => self.flash_read(addr, count as usize),
                0x05 => vec![if self.xspi.wel { 2 } else { 0 }; count as usize],
                0x35 => vec![2; count as usize], // status register 2: quad enable set
                0x15 => vec![0; count as usize],
                0x5a => vec![0xff; count as usize], // no SFDP
                _ => vec![0; count as usize],
            };
            self.xspi.out.extend(data);
            return;
        }
        let op = ((c[3] >> 16) & 0xff) as u8;
        let nbytes = (c[3] >> 28) & 7;
        let addr = ((c[1] >> 24) as u64) | ((c[2] as u64) << 8) | (((c[3] & 0xff) as u64) << 40);
        let addr = if nbytes >= 8 { addr } else { addr & ((1u64 << (8 * nbytes)) - 1) };
        if kind == 1 {
            self.xspi.pending = Some((op, addr, nbytes));
            return;
        }
        // no data phase
        self.xspi.ops.push((op, addr, nbytes, 0, false));
        match op {
            0x06 => self.xspi.wel = true,
            0x04 => self.xspi.wel = false,
            0x20 | 0x21 | 0x52 | 0xd8 | 0xdc if self.xspi.wel => {
                let span: u64 = match op {
                    0x20 | 0x21 => 0x1000,
                    0x52 => 0x8000,
                    _ => 0x10000,
                };
                let a = (addr & !(span - 1)) as usize;
                let f = Rc::make_mut(&mut self.flash);
                for b in f.iter_mut().skip(a).take(span as usize) {
                    *b = 0xff;
                }
                self.xspi.wel = false;
            }
            _ => {}
        }
    }

    /// The redistributor frame that is a core's (see the GICR_TYPER model below).
    fn gicr_of(&self, core: usize) -> u64 {
        for n in 0..CORES as u64 {
            let aff = self.regs.get(&(GICR + n * 0x40000 + 0x100)).map(|v| *v as u64).unwrap_or(n << 8);
            if aff == (core as u64) << 8 {
                return GICR + n * 0x40000;
            }
        }
        GICR + core as u64 * 0x40000
    }

    /// The AP rang the PM's doorbell: the PM takes the message in the shared area and answers in it.
    fn pm_doorbell(&mut self) {
        let w = |s: &Soc, i: u64| s.reg(PM_MBOX + 4 * i);
        let route = (w(self, 0) >> 8) & 0xff;
        let header = w(self, 6);
        let len = w(self, 5);
        let n = (len.saturating_sub(4) / 4).min(25) as u64;
        let payload: Vec<u32> = (0..n).map(|i| w(self, 7 + i)).collect();
        let mut msg = vec![route, header];
        msg.extend(&payload);
        self.pm_msgs.push(msg);
        if !self.pm_alive {
            return;
        }
        let arg = |i: usize| payload.get(i).copied().unwrap_or(0);
        if route == 0x0c {
            // a PM message: status code 0, no data back
            self.set_reg(PM_MBOX + 8, 0);
            for i in 0..13 {
                self.set_reg(PM_MBOX + 4 * (7 + 12 + i), 0);
            }
        } else {
            let (proto, id) = ((header >> 10) & 0xff, header & 0xff);
            let mut out: Vec<u32> = vec![0];
            match (proto, id) {
                (_, 0) => out.push(match proto {
                    0x10 | 0x11 => 0x2_0000,
                    _ => 0x1_0000,
                }),
                (_, 1) | (_, 2) => out.push(0),
                (0x11, 4) => {
                    self.pd_state.insert(arg(1), arg(2));
                }
                (0x11, 5) => out.push(self.pd_state.get(&arg(0)).copied().unwrap_or(0)),
                (0x12, 3) => self.sys_state = Some(arg(1)),
                (0x12, 4) => out.push(self.sys_state.unwrap_or(0)),
                (0x90, 3) => self.reset_addr = (arg(0) as u64 | ((arg(1) as u64) << 32), arg(2)),
                (0x90, 4) => {
                    out.extend([self.reset_addr.0 as u32, (self.reset_addr.0 >> 32) as u32, self.reset_addr.1]);
                }
                _ => {}
            }
            self.set_reg(PM_MBOX + 0x14, 4 + 4 * out.len() as u32);
            for (i, v) in out.iter().enumerate() {
                self.set_reg(PM_MBOX + 0x1c + 4 * i as u64, *v);
            }
        }
        let st = self.reg(PM_MBOX + 4);
        self.set_reg(PM_MBOX + 4, st | 1);
    }

    /// The AP rang the SE's doorbell: the SE answers the command with error code 0 and no data.
    fn se_doorbell(&mut self) {
        let head = self.reg(SE_MBOX);
        let size = (head & 0x7f) as u64;
        let words = ((size + 3) / 4).clamp(2, 32);
        let msg: Vec<u32> = (1..words).map(|i| self.reg(SE_MBOX + 4 * i)).collect();
        self.se_msgs.push(msg);
        if !self.se_alive {
            return;
        }
        // header: 8 bytes, then the error code and what the command returns (nothing here)
        self.set_reg(SE_MBOX, (head & !0x7f) | 16);
        for i in 2..32 {
            self.set_reg(SE_MBOX + 4 * i, 0);
        }
        let st = self.reg(SE_MBOX + 0x84);
        self.set_reg(SE_MBOX + 0x84, st | 1);
    }

    fn dev_read(&mut self, core: usize, addr: u64, size: u32) -> u64 {
        let word = addr & !3;
        let v: Option<u64> = if (XSPI_SDMA..XSPI_SDMA + 0x1000).contains(&addr) {
            let mut v = 0u64;
            for i in 0..size {
                v |= (self.xspi.out.pop_front().unwrap_or(0) as u64) << (8 * i);
            }
            Some(v)
        } else if (XSPI..XSPI + 0x2000).contains(&addr) {
            match addr - XSPI {
                0x44 => Some(if self.xspi.done { 1 << 15 } else { 0 }),
                0x100 => Some((1 << 16) | if self.xspi_stuck { 1 << 7 } else { 0 }),
                0x110 => Some(if self.xspi.done { (1 << 23) | (1 << 21) | (1 << 16) } else { 1 << 16 }),
                0x240 => Some(self.xspi.sdma_size as u64),
                0x244 => Some((self.xspi.sdma_write as u64) << 8),
                0xf00 => Some(0x6523_0000),
                0xf04 => Some(0x0120_0001),
                _ => None,
            }
        } else if (UART..UART + 0x1000).contains(&addr) {
            match addr - UART {
                0x18 => Some(0x90),
                _ => None,
            }
        } else if (SYSCNT..SYSCNT + 0x1000).contains(&addr) {
            match addr - SYSCNT {
                0x08 => {
                    self.time += TICK;
                    Some(self.time & 0xffff_ffff)
                }
                0x0c => Some(self.time >> 32),
                _ => None,
            }
        } else if (TZC..TZC + 4 * 0x20000).contains(&addr) {
            match (addr - TZC) & 0x1ffff {
                // BUILD_CONFIG: 4 filters, 40-bit addresses, 9 regions
                0x000 => Some((3 << 24) | (39 << 8) | 8),
                // GATE_KEEPER: the filters follow the request at once
                0x008 => {
                    let r = self.reg(word) & 0xf;
                    Some(((r << 16) | r) as u64)
                }
                0xfe0 => Some(0x60),
                0xfe4 => Some(0xb4),
                0xfe8 => Some(0x2b),
                0xfec => Some(0x00),
                0xfd0 => Some(0x04),
                0xff0 => Some(0x0d),
                0xff4 => Some(0xf0),
                0xff8 => Some(0x05),
                0xffc => Some(0xb1),
                _ => None,
            }
        } else if (GICD..GICD + 0x10000).contains(&addr) {
            match addr - GICD {
                // GICD_TYPER: 480 SPIs (ITLinesNumber 15), security extensions, 10 ID bits
                0x004 => Some((9 << 19) | (1 << 10) | 15),
                0xffe8 => Some(0x3b),
                _ => None,
            }
        } else if (GICR..GICR + CORES as u64 * 0x40000).contains(&addr) {
            // The redistributor frames are the cores GICD_RDOFFR (0xc800) leaves on, in order; a
            // frame is the core whose affinity software wrote to its GICR_MPIDR (0x100).
            let n = (addr - GICR) / 0x40000;
            let frames = CORES as u64 - (self.reg(GICD + 0xc800) & 0xfff).count_ones() as u64;
            let aff = self.regs.get(&(GICR + n * 0x40000 + 0x100)).map(|v| *v as u64).unwrap_or(n << 8);
            match (addr - GICR) % 0x40000 {
                // GICR_TYPER: affinity, processor number, VLPIS (GICv4: four pages a frame), Last
                0x008 => Some((if size == 8 { aff << 32 } else { 0 }) | (n << 8) | 2 | if n + 1 >= frames { 0x10 } else { 0 }),
                0x00c => Some(aff),
                // GICR_WAKER: ChildrenAsleep follows ProcessorSleep; the GIC-600/700's Quiescent (31)
                // follows its Sleep (0)
                0x014 => {
                    let w = self.regs.get(&word).copied().unwrap_or(6) & 0x7fff_fffb;
                    Some((w | if w & 2 != 0 { 4 } else { 0 } | if w & 1 != 0 { 0x8000_0000 } else { 0 }) as u64)
                }
                0xffe8 => Some(0x3b),
                _ => None,
            }
        } else if addr == PPU_CLUSTER + 8 {
            // PPU_PWSR: the cluster is in the state its policy asks for (on until told otherwise)
            Some((self.regs.get(&PPU_CLUSTER).copied().unwrap_or(8) & 0xf) as u64)
        } else if addr >= PPU_CORE && addr < PPU_CORE + CORES as u64 * 0x10_0000 && (addr - PPU_CORE) % 0x10_0000 == 8 {
            Some((self.regs.get(&(addr - 8)).copied().unwrap_or(0) & 0xf) as u64)
        } else {
            None
        };
        let v = match v {
            Some(v) => v,
            None => {
                if !self.regs.contains_key(&word) {
                    *self.unmodelled.entry(word).or_insert(0) += 1;
                }
                let w = self.reg(word) as u64;
                let shift = 8 * (addr & 3);
                if size == 8 {
                    w | ((self.reg(word + 4) as u64) << 32)
                } else {
                    (w >> shift) & if size == 4 { 0xffff_ffff } else { (1 << (8 * size)) - 1 }
                }
            }
        };
        if self.log_io {
            self.io.push(Io { write: false, addr, size: size as u8, val: v, core: core as u8 });
        }
        if self.trace {
            eprintln!("  [{:>12}] c{core} R{size} {addr:#010x} -> {v:#x}", self.time);
        }
        if self.watch != 0 && addr & !3 == self.watch {
            self.watch_hit = Some((false, v));
        }
        v
    }

    fn dev_write(&mut self, core: usize, addr: u64, size: u32, val: u64) {
        if self.log_io {
            self.io.push(Io { write: true, addr, size: size as u8, val, core: core as u8 });
        }
        if self.trace {
            eprintln!("  [{:>12}] c{core} W{size} {addr:#010x} <- {val:#x}", self.time);
        }
        if self.watch != 0 && addr & !3 == self.watch {
            self.watch_hit = Some((true, val));
        }
        let word = addr & !3;
        // the register's new value, for accesses narrower than a word
        let merged = |old: u32| -> u32 {
            if size >= 4 {
                val as u32
            } else {
                let shift = 8 * (addr & 3) as u32;
                let mask = ((1u32 << (8 * size)) - 1) << shift;
                (old & !mask) | (((val as u32) << shift) & mask)
            }
        };
        if (XSPI_SDMA..XSPI_SDMA + 0x1000).contains(&addr) {
            if let Some((a, left)) = self.xspi.program {
                let n = size.min(left);
                let f = Rc::make_mut(&mut self.flash);
                for i in 0..n {
                    if let Some(b) = f.get_mut(a as usize + i as usize) {
                        *b &= (val >> (8 * i)) as u8;
                    }
                }
                self.xspi.program = if left > n { Some((a + n as u64, left - n)) } else { None };
                if left <= n {
                    self.xspi.wel = false;
                }
            }
            return;
        }
        if (XSPI..XSPI + 0x18).contains(&addr) {
            let i = ((addr - XSPI) / 4) as usize;
            self.xspi.cmd[i] = val as u32;
            if i == 0 {
                self.xspi_trigger();
            }
            return;
        }
        if (UART..UART + 0x1000).contains(&addr) && addr - UART == 0 {
            self.uart.push(val as u8);
            return;
        }
        let old = self.reg(word);
        self.regs.insert(word, merged(old));
        if size == 8 {
            self.regs.insert(word + 4, (val >> 32) as u32);
        }
        if word >= PPU_CORE && word < PPU_CORE + CORES as u64 * 0x10_0000 && (word - PPU_CORE) % 0x10_0000 == 0 {
            self.power.push((((word - PPU_CORE) / 0x10_0000) as u8, val as u32));
        }
        if word == PM_MBOX + 0x80 && val & 1 != 0 {
            self.regs.insert(word, merged(old) & !1);
            self.pm_doorbell();
        } else if word == SE_MBOX + 0x80 && val & 1 != 0 {
            self.regs.insert(word, merged(old) & !1);
            self.se_doorbell();
        }
    }
}

impl Bus for Soc {
    #[inline]
    fn read(&mut self, core: usize, addr: u64, size: u32) -> Option<u64> {
        if addr >= DRAM_BASE {
            if addr + size as u64 > self.ram.end {
                return None;
            }
            return Some(self.ram.read(addr, size));
        }
        Some(self.dev_read(core, addr, size))
    }

    #[inline]
    fn write(&mut self, core: usize, addr: u64, size: u32, val: u64) -> bool {
        if addr >= DRAM_BASE {
            if addr + size as u64 > self.ram.end {
                return false;
            }
            self.ram.write(addr, size, val);
            return true;
        }
        self.dev_write(core, addr, size, val);
        true
    }

    fn sysreg(&mut self, core: usize, k: u16, write: Option<u64>) -> Option<u64> {
        match (k, write) {
            (CNTPCT_EL0, None) | (CNTVCT_EL0, None) => {
                self.time += TICK;
                Some(self.time)
            }
            // (1020: the highest pending interrupt is a secure group 1 one, which EL3 does not take)
            (ICC_HPPIR0_EL1, None) => Some(self.pending[core].unwrap_or(if self.pending_s[core] { 1020 } else { 1023 }) as u64),
            (ICC_IAR0_EL1, None) => {
                let id = self.pending[core].take().unwrap_or(1023);
                if id < 1020 {
                    self.acks.push((core as u8, id, false));
                    // its priority as the firmware configured it: GICD_IPRIORITYR, or the core's
                    // redistributor's for an SGI or PPI
                    let at = if id < 32 { self.gicr_of(core) + 0x1_0400 + id as u64 } else { GICD + 0x400 + id as u64 };
                    let p = (self.reg(at) >> (8 * (at & 3))) as u8;
                    self.running[core].push(p);
                }
                Some(id as u64)
            }
            (ICC_RPR_EL1, None) => Some(self.running[core].last().copied().unwrap_or(0xff) as u64),
            (ICC_EOIR0_EL1, Some(v)) | (ICC_EOIR1_EL1, Some(v)) => {
                self.acks.push((core as u8, v as u32, true));
                self.running[core].pop();
                Some(0)
            }
            (ICC_HPPIR1_EL1, None) | (ICC_IAR1_EL1, None) => Some(1023),
            (ICC_SGI0R_EL1, Some(v)) | (ICC_SGI1R_EL1, Some(v)) | (ICC_ASGI1R_EL1, Some(v)) => {
                self.sgis.push((core as u8, k, v));
                Some(0)
            }
            _ => None,
        }
    }

    #[inline]
    fn tick(&mut self) {}
}

/// What the SE firmware leaves in DRAM before it starts BL2: the DRAM
/// description ("CDOB") and the key-management block (firmware/se's
/// MemOutputLib.c and km.c).  `secure`: the secure boot enable the eFuses
/// give; `rotpk`: the root key hash the SE verified bootloader1 with.
pub fn se_handover(soc: &mut Soc, dram_mb: u32, secure: bool, rotpk: &[u8; 32], channel_mask: u8) {
    let mut out = [0u8; 20];
    out[0..4].copy_from_slice(&0x424f_4443u32.to_le_bytes());
    out[4..6].copy_from_slice(&1u16.to_le_bytes());
    out[8] = 8; // LPDDR5
    out[9] = channel_mask;
    out[10] = 1;
    out[12..16].copy_from_slice(&dram_mb.to_le_bytes());
    out[16..20].copy_from_slice(&dram_mb.to_le_bytes());
    soc.ram.load(MEM_OUTPUT, &out);
    // csec_km_meta_t: hpubk[32], rng[64], config[16], boot_cfg[256], key_cfg[2048]
    let mut km = vec![0u8; 32 + 64 + 64 + 256 + 2048];
    km[0..32].copy_from_slice(rotpk);
    for (i, b) in km[32..96].iter_mut().enumerate() {
        *b = (i as u8).wrapping_mul(0x9d).wrapping_add(0x3b);
    }
    let cfg = |km: &mut Vec<u8>, i: usize, v: u32| km[96 + 4 * i..100 + 4 * i].copy_from_slice(&v.to_le_bytes());
    cfg(&mut km, 1, 4); // life cycle
    cfg(&mut km, 3, secure as u32);
    cfg(&mut km, 4, 1);
    km[96 + 20..96 + 28].copy_from_slice(&[0xa5, 0xb1, 0xee, 0xce, 0x00, 0x20, 0x0c, 0x11]);
    for (i, name) in ["ap.coreid", "se.pblbde", "pb.tfabde", "se.memrbp"].iter().enumerate() {
        km[160 + 16 * i..160 + 16 * i + name.len()].copy_from_slice(name.as_bytes());
    }
    soc.ram.load(KM_BLOCK, &km);
}
