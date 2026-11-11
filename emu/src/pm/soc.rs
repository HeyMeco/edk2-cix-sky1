// SPDX-License-Identifier: BSD-2-Clause-Patent
//! Sky1 as CSU_PM sees it, around the Xtensa interpreter.
//!
//! As the SE's model does for the SE, this is only as much of the SoC as
//! the firmware needs to make progress: registers are plain memory unless a
//! device below gives them behaviour.
//!
//! Devices:
//!   * Cadence I2C controllers (cdns,i2c-r1p14; 16-bit registers) at
//!     0xa0010000, 0xa0011000, 0xa0012000 (masters) and 0xa0013000 (the
//!     slave the EC reads, a master on the MS-R1), with slaves attached by
//!     7-bit address, and a host that can address the slave side;
//!   * the PL011 at 0xa0030000 (UART4 on the board), whose output is kept;
//!   * the clock generator's PLLs (they lock at once), the mailboxes and
//!     the interrupt aggregator, the SE's side of the boot handshake and of
//!     suspend/resume (SePeer, after firmware/se/service/str.c);
//!   * for the power sequence (modelled, not known): the S5 power
//!     controller (sleep type, the PM's wake interrupt on aggregator line
//!     18, warm reset), the DSU's and cores' power policy units (status
//!     follows the policy), the DDR controllers' low-power handshake;
//!   * the SP805 watchdog, the counters (fans' PWM and tachometers) and the
//!     PVT sensors.
//!
//! Every register read is recorded by address (`reads`), every doorbell the
//! PM rings towards the AP in order (`doorbells`), so the tests can compare
//! more than the writes.

use crate::xtensa::{Bus, Io, Xtensa};
use std::collections::{BTreeMap, HashMap};
use std::rc::Rc;

pub type Cpu = Xtensa<PmSoc>;

/// a call to any of these is a failure (CIX: argument 0 is a trace code;
/// the open firmware's assert() ends in __assert_func)
pub const ASSERTS: [&str; 5] = ["vAssertCalledWithTRC", "vAssertCalled", "_xt_panic", "arch_panic", "__assert_func"];
/// busy-waits on CCOUNT, done at once (argument: cycles): CIX's, ours
pub const DELAYS: [&str; 2] = ["cc_delay", "udelay_cycles"];
/// IRAM/DRAM, overlay storage at 0x20100000
pub const RAM: (u32, u32) = (0x20000000, 0x110000);
pub const RESET: u32 = 0x20000000;
/// the core clock both images set (LCRU divider 1)
pub const CORE_HZ: u64 = 400_000_000;

/// Sparse little-endian memory: registers nothing models.
#[derive(Clone, Default)]
pub struct Mem {
    pub pages: HashMap<u32, Vec<u8>>,
}

impl Mem {
    pub fn read(&self, addr: u32, size: u32) -> u64 {
        let Some(page) = self.pages.get(&(addr >> 12)) else { return 0 };
        let o = (addr & 0xfff) as usize;
        let mut v = 0u64;
        for k in 0..size as usize {
            v |= (page[o + k] as u64) << (8 * k);
        }
        v
    }

    pub fn read32(&self, addr: u32) -> u32 {
        self.read(addr, 4) as u32
    }

    pub fn write(&mut self, addr: u32, size: u32, value: u64) {
        let page = self.pages.entry(addr >> 12).or_insert_with(|| vec![0; 0x1000 + 8]);
        let o = (addr & 0xfff) as usize;
        for k in 0..size as usize {
            page[o + k] = (value >> (8 * k)) as u8;
        }
    }

    pub fn write32(&mut self, addr: u32, value: u32) {
        self.write(addr, 4, value as u64)
    }
}

// --- I2C ---------------------------------------------------------------------

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum DevKind {
    /// a register file with an 8-bit pointer
    Plain,
    /// MP5475: fault register 09 clears the bits written to it
    Mp5475,
    /// MP2845 as CIX's dev_paged_pmic.c uses it (PMBus-like, paged)
    Mp2845,
    /// AU4683 as CIX's dev_au4683.c uses it: paged, its own formats
    Au4683,
    /// TI TMP75: register 0 the temperature
    Tmp75,
}

/// A slave with an 8-bit register pointer: a write's first byte selects
/// the register, the rest are written from there; reads continue from the
/// pointer.  Most PMICs and sensors on Sky1 boards behave this way.  The
/// kinds are modelled on what the firmware does with them, not on
/// datasheets:
///
/// * MP5475 (four bucks, VOUT at 0x13 + 8 * buck): CIX's firmware writes
///   0xff to 09 after reading 07-09; a test raises a fault by setting them.
/// * MP2845: register 0 selects the page (a buck), VOUT_COMMAND (0x62, two
///   bytes, 5 mV steps in bits 9:0) and READ_VOUT (0xa7, the same) are per
///   page, 0x83 the revision; the vendor registers the PSI setting writes
///   (0x5f, 0x64, 0x73) are kept per page too.
/// * AU4683: VOUT_COMMAND 0x21 ((mV - 245) / 5, 12 bits), READ_VOUT 0x8b in
///   PMBus LINEAR11 (exponent -10 here), revision 0x9b.
/// * TMP75: whole degrees, then sixteenths in the high nibble, as the
///   MS-R1's board sensors at 0x4d on I2C 3.
#[derive(Clone)]
pub struct I2cDevice {
    pub name: String,
    pub kind: DevKind,
    pub regs: Vec<u8>,
    ptr: u8,
    first: bool,
    page: u8,
    paged: HashMap<(u8, u8), [u8; 3]>,
    n: u32,
}

impl I2cDevice {
    pub fn new(name: &str, kind: DevKind) -> Self {
        let mut d = I2cDevice { name: name.to_string(), kind, regs: vec![0; 256], ptr: 0, first: true, page: 0, paged: HashMap::new(), n: 0 };
        match kind {
            DevKind::Au4683 => d.regs[0x9b] = 1,
            DevKind::Tmp75 => d.set_temp(25.0),
            _ => {}
        }
        d
    }

    pub fn set_temp(&mut self, temp: f64) {
        let whole = temp as i64;
        self.regs[0] = whole as u8;
        self.regs[1] = ((((temp - whole as f64) * 16.0) as i64) << 4 & 0xf0) as u8;
        self.regs[0x23] = self.regs[0];
    }

    fn is_paged(&self, reg: u8) -> bool {
        match self.kind {
            DevKind::Mp2845 => matches!(reg, 0x5f | 0x62 | 0x64 | 0x73 | 0xa7),
            DevKind::Au4683 => matches!(reg, 0x21 | 0x8b),
            _ => false,
        }
    }

    fn start(&mut self, read: bool) {
        self.first = !read;
        self.n = 0;
    }

    fn plain_write(&mut self, byte: u8) {
        if self.first {
            self.ptr = byte;
            self.first = false;
        } else {
            self.regs[self.ptr as usize] = byte;
            self.ptr = self.ptr.wrapping_add(1);
        }
    }

    fn paged_write(&mut self, byte: u8) {
        if self.first {
            self.ptr = byte;
            self.first = false;
            self.n = 0;
            return;
        }
        if self.ptr == 0 {
            self.page = byte & 3;
            return;
        }
        if self.is_paged(self.ptr) {
            let r = self.paged.entry((self.page, self.ptr)).or_insert([0; 3]);
            if self.n < 3 {
                r[self.n as usize] = byte;
            }
            let copy = *r;
            if self.ptr == 0x62 && self.n == 1 {
                self.paged.insert((self.page, 0xa7), copy);
            }
            self.n += 1;
            return;
        }
        self.plain_write(byte);
    }

    fn write(&mut self, byte: u8) {
        match self.kind {
            DevKind::Mp5475 if !self.first && self.ptr == 0x09 => {
                self.regs[0x09] &= !byte;
                self.ptr = self.ptr.wrapping_add(1);
            }
            DevKind::Mp2845 => self.paged_write(byte),
            DevKind::Au4683 => {
                self.paged_write(byte);
                if self.ptr == 0x21 && self.n == 2 {
                    let r = self.paged[&(self.page, 0x21)];
                    let mv = (r[0] as u32 | (r[1] as u32 & 0xf) << 8) * 5 + 245;
                    let mant = ((mv as f64 * 1000.0 / 977.0).round_ties_even() as u32).min(0x7ff);
                    self.paged.insert((self.page, 0x8b), [mant as u8, ((0x16 << 3) | mant >> 8) as u8, 0]);
                }
            }
            _ => self.plain_write(byte),
        }
    }

    fn read(&mut self) -> u8 {
        if self.is_paged(self.ptr) {
            let r = self.paged.get(&(self.page, self.ptr)).copied().unwrap_or([0; 3]);
            let v = if self.n < 3 { r[self.n as usize] } else { 0 };
            self.n += 1;
            return v;
        }
        let v = self.regs[self.ptr as usize];
        self.ptr = self.ptr.wrapping_add(1);
        v
    }
}

/// The model for an address nobody attached: Sky1 boards' PMICs by their
/// address, a register file otherwise.
fn auto_device(name: &str, addr: u8, au4683: bool) -> I2cDevice {
    let kind = match addr {
        0x60 => DevKind::Mp5475,
        0x45 => DevKind::Mp2845,
        0x21 if au4683 => DevKind::Au4683,
        _ => DevKind::Plain,
    };
    I2cDevice::new(name, kind)
}

// Cadence I2C register offsets and bits (Linux i2c-cadence.c)
pub const CR: u32 = 0;
const SR: u32 = 4;
const ADDR: u32 = 8;
const DATA: u32 = 12;
const ISR: u32 = 16;
const XFER: u32 = 20;
const TOUT: u32 = 28;
const IMR: u32 = 32;
const IER: u32 = 36;
const IDR: u32 = 40;
pub const CR_RW: u32 = 0x01;
const CR_MS: u32 = 0x02;
const CR_HOLD: u32 = 0x10;
const CR_CLRFIFO: u32 = 0x40;
const SR_RXRW: u32 = 0x08;
const SR_RXDV: u32 = 0x20;
const SR_BA: u32 = 0x100;
const ISR_COMP: u32 = 0x01;
const ISR_DATA: u32 = 0x02;
pub const ISR_NACK: u32 = 0x04;
const ISR_SLV_RDY: u32 = 0x10;

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Fault {
    /// a bus held low: no transfer ever starts or ends, the bus stays busy
    Stuck,
    /// every address NACKed
    Nack,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum FaultMode {
    /// no device answers
    Nack,
    /// a voltage write reaches the regulator, then is NACKed: set, but the
    /// write failed; reads answer
    Lost,
}

/// A voltage write: register and two bytes (MP5475, VOUT_COMMAND)
pub const VOUT_LEN: usize = 3;

#[derive(Clone, Debug)]
pub struct Transfer {
    /// bus writes the firmware had made before it
    pub writes: usize,
    pub addr: u8,
    pub write: bool,
    pub tx: Vec<u8>,
}

/// Transfers on a bus from the after-th on fail (a test arms it).  Each
/// transfer is logged with the number of register writes before it, to
/// order the I2C against the clock generator.
#[derive(Clone, Default)]
pub struct BusFault {
    pub mode: Option<FaultMode>,
    pub after: usize,
    pub n: usize,
    pub log: Vec<Transfer>,
}

impl BusFault {
    pub fn arm(&mut self, mode: Option<FaultMode>, after: usize) {
        self.mode = mode;
        self.after = after;
        self.n = 0;
    }
}

#[derive(Clone, Debug)]
pub struct I2cLog {
    pub bus: String,
    pub what: String,
    /// where the firmware was
    pub pc: u32,
}

/// A Cadence I2C controller.
///
/// Master side: a write of ADDR starts a transfer: for a write, the TX
/// FIFO goes to the slave (and bytes written to DATA while the transfer is
/// open follow it); for a read, XFER bytes come back through DATA.  Every
/// transfer completes at once.  The interrupt status bits not masked
/// (IER/IDR) drive the controller's source on aggregator line 24.
///
/// Slave side (CR without MS; modelled on what CIX's dev_i2c_isr does with
/// it, not known): a host transfer to the controller's own address (ADDR)
/// puts the bytes written in the receive FIFO and raises DATA and COMP; a
/// host read raises SLV_RDY with SR's RXRW set, takes what the firmware
/// then writes to DATA and ends with COMP (PmSoc::slave_write, slave_read).
#[derive(Clone)]
pub struct CadenceI2c {
    pub base: u32,
    pub name: String,
    pub devices: BTreeMap<u8, I2cDevice>,
    pub fault: Option<Fault>,
    /// a test's failing-bus plan and transfer log
    pub plan: Option<BusFault>,
    /// voltage writes are lost (set, then NACKed) whatever the plan
    pub lose_vout: bool,
    r: HashMap<u32, u32>,
    pub isr: u32,
    tx: Vec<u8>,
    rx: Vec<u8>,
    dev: Option<u8>,
    open: bool,
    reading: bool,
    sent: Vec<u8>,
    got: Vec<u8>,
    /// slave side: a host read in progress, the bytes the firmware gave
    slave_reading: bool,
    pub slave_tx: Vec<u8>,
}

fn hex(b: &[u8]) -> String {
    b.iter().map(|x| format!("{x:02x}")).collect::<Vec<_>>().join(" ")
}

impl CadenceI2c {
    fn new(base: u32, name: &str) -> Self {
        let mut c = CadenceI2c {
            base,
            name: name.to_string(),
            devices: BTreeMap::new(),
            fault: None,
            plan: None,
            lose_vout: false,
            r: HashMap::new(),
            isr: 0,
            tx: Vec::new(),
            rx: Vec::new(),
            dev: None,
            open: false,
            reading: false,
            sent: Vec::new(),
            got: Vec::new(),
            slave_reading: false,
            slave_tx: Vec::new(),
        };
        c.reset();
        c
    }

    fn reg(&self, off: u32) -> u32 {
        self.r.get(&off).copied().unwrap_or(0)
    }

    pub fn cr(&self) -> u32 {
        self.reg(CR)
    }

    pub fn irq(&self) -> bool {
        self.isr & !self.reg(IMR) & 0x2ff != 0
    }

    pub fn reset(&mut self) {
        self.r = HashMap::from([(CR, 0), (SR, 0), (ADDR, 0), (XFER, 0), (IMR, 0x2ff), (TOUT, 0x1f)]);
        self.isr = 0;
        self.tx.clear();
        self.rx.clear();
        self.dev = None;
        self.open = false;
        self.slave_reading = false;
    }

    fn read(&mut self, off: u32) -> u64 {
        match off {
            DATA => {
                if self.rx.is_empty() {
                    0
                } else {
                    self.rx.remove(0) as u64
                }
            }
            ISR => self.isr as u64,
            SR => {
                let mut sr = 0;
                if !self.rx.is_empty() {
                    sr |= SR_RXDV;
                }
                if self.open || self.fault == Some(Fault::Stuck) {
                    sr |= SR_BA;
                }
                if self.slave_reading {
                    sr |= SR_RXRW | SR_BA;
                }
                sr as u64
            }
            _ => self.reg(off) as u64,
        }
    }

    fn finish(&mut self, log: &mut Vec<I2cLog>, pc: u32) {
        if self.open {
            if let Some(a) = self.dev {
                let (kind, data) = if self.reading { ("R", &self.got) } else { ("W", &self.sent) };
                let what = format!("{} {} {}", self.devices[&a].name, kind, hex(data));
                log.push(I2cLog { bus: self.name.clone(), what, pc });
            }
        }
        self.open = false;
    }

    fn write(&mut self, off: u32, value: u32, log: &mut Vec<I2cLog>, io: &Io, nwrites: usize, auto: Option<bool>) {
        match off {
            ISR => self.isr &= !value,
            IER => {
                let v = self.reg(IMR) & !value;
                self.r.insert(IMR, v);
            }
            IDR => {
                let v = self.reg(IMR) | value;
                self.r.insert(IMR, v);
            }
            DATA => {
                if self.slave_reading {
                    self.slave_tx.push(value as u8);
                } else if self.open && self.dev.is_some() && !self.reading {
                    let a = self.dev.unwrap();
                    self.devices.get_mut(&a).unwrap().write(value as u8);
                    self.sent.push(value as u8);
                    self.isr |= ISR_COMP;
                } else {
                    self.tx.push(value as u8);
                }
            }
            CR => {
                let mut value = value;
                if value & CR_CLRFIFO != 0 {
                    self.tx.clear();
                    self.rx.clear();
                    value &= !CR_CLRFIFO;
                }
                self.r.insert(CR, value);
                if value & CR_HOLD == 0 {
                    self.finish(log, io.pc);
                }
            }
            _ => {
                self.r.insert(off, value);
                if off == ADDR && self.reg(CR) & CR_MS != 0 {
                    // (a slave's own address starts nothing)
                    self.planned_transfer((value & 0x7f) as u8, log, io, nwrites, auto);
                }
            }
        }
    }

    /// A transfer through the test's failing-bus plan, if one is armed.
    fn planned_transfer(&mut self, addr: u8, log: &mut Vec<I2cLog>, io: &Io, nwrites: usize, auto: Option<bool>) {
        let write = self.reg(CR) & CR_RW == 0;
        let vout = write && self.tx.len() >= VOUT_LEN;
        let mut mode = None;
        if let Some(plan) = &mut self.plan {
            plan.log.push(Transfer { writes: nwrites, addr, write, tx: self.tx.clone() });
            let n = plan.n;
            plan.n += 1;
            if n >= plan.after {
                mode = plan.mode;
            }
        }
        match mode {
            Some(FaultMode::Nack) => {
                self.fault = Some(Fault::Nack);
                self.transfer(addr, log, io, auto);
                self.fault = None;
            }
            Some(FaultMode::Lost) => {
                self.transfer(addr, log, io, auto);
                if vout {
                    self.isr |= ISR_NACK;
                }
            }
            None => {
                self.transfer(addr, log, io, auto);
                if vout && self.lose_vout {
                    self.isr |= ISR_NACK;
                }
            }
        }
    }

    fn transfer(&mut self, addr: u8, log: &mut Vec<I2cLog>, io: &Io, auto: Option<bool>) {
        self.finish(log, io.pc);
        let mut note = |what: String| log.push(I2cLog { bus: self.name.clone(), what, pc: io.pc });
        match self.fault {
            Some(Fault::Stuck) => {
                note(format!("@{addr:#04x} STUCK"));
                self.dev = None;
                self.tx.clear();
                return;
            }
            Some(Fault::Nack) => {
                note(format!("@{addr:#04x} NACK (fault)"));
                self.dev = None;
                self.isr |= ISR_NACK;
                self.tx.clear();
                return;
            }
            None => {}
        }
        if !self.devices.contains_key(&addr) {
            if let Some(au) = auto {
                self.devices.insert(addr, auto_device(&format!("{}:{addr:#04x}", self.name), addr, au));
            }
        }
        if !self.devices.contains_key(&addr) {
            note(format!("@{addr:#04x} NACK"));
            self.dev = None;
            self.isr |= ISR_NACK;
            self.tx.clear();
            return;
        }
        self.dev = Some(addr);
        let read = self.reg(CR) & CR_RW != 0;
        self.reading = read;
        let dev = self.devices.get_mut(&addr).unwrap();
        dev.start(read);
        self.open = true;
        if read {
            let n = self.r.get(&XFER).copied().unwrap_or(0).max(1);
            self.got = (0..n).map(|_| dev.read()).collect();
            self.rx = self.got.clone();
            self.r.insert(XFER, 0); // everything received
            self.isr |= ISR_COMP | ISR_DATA;
        } else {
            self.sent.clear();
            for b in std::mem::take(&mut self.tx) {
                dev.write(b);
                self.sent.push(b);
            }
            self.isr |= ISR_COMP;
        }
        if self.reg(CR) & CR_HOLD == 0 {
            self.finish(log, io.pc);
        }
    }
}

// --- clock generator -----------------------------------------------------------

/// the SE's 0x46570000, SoC 0x06570000
pub const CGC: u32 = 0xa0060000;

/// PLL registers as CSU_PM reads them (pll_ctrl_*): +0x00 [11:0] feedback
/// divider, +0x04 [23:0] fraction and bit 24 output enable, +0x08 [2:0] and
/// [6:4] post dividers, [13:8] reference divider, +0x0c bit 4 locked.
/// FoutVCO = 24 MHz * (fbdiv + frac / 2^24) / refdiv, which CIX's firmware
/// expects between 1.6 and 2.1 GHz.  The reset values are not known (a dump
/// of SoC 0x06570000 from a running board would tell); these give 2 GHz.
const PLL_RESET: [(u32, u32); 4] = [(0x00, 83), (0x04, 0x1555555), (0x08, 0x111), (0x0c, 0x10)];

pub fn plls() -> Vec<u32> {
    let mut v: Vec<u32> = (0..7).map(|i| CGC + 0x100 * i).collect();
    v.extend((0..5).map(|i| CGC + 0x1000 + 0x100 * i));
    v.extend([CGC + 0x2000, CGC + 0x2100, CGC + 0x3000, CGC + 0x3100, CGC + 0x4000]);
    v.extend((0..4).map(|i| CGC + 0x5000 + 0x100 * i));
    v.extend([CGC + 0x6000, CGC + 0x7000]);
    v
}

fn is_pll_lock(addr: u32) -> bool {
    if addr & 0xff != 0x0c || !(CGC..CGC + 0x8000).contains(&addr) {
        return false;
    }
    let (blk, n) = ((addr - CGC) >> 12, ((addr - CGC) >> 8) & 15);
    match blk {
        0 => n < 7,
        1 => n < 5,
        2 | 3 => n < 2,
        4 | 6 | 7 => n == 0,
        5 => n < 4,
        _ => false,
    }
}

// --- the SE, as far as CSU_PM sees it ----------------------------------------------

/// the SE's 0x46610090 (firmware/se/service/main.c)
pub const PM_MAGIC: u32 = 0xa0100090;
/// mailbox 8 fast channel 0, the SE's 0x4661008c
pub const PM_FAST_CHANNEL: u32 = 0xa010008c;
// S5 registers the SE writes around suspend (firmware/se/service/str.c)
const S5_SW_SUSPEND: u32 = 0x5600020c;
const S5_SW_STR: u32 = 0x56000228;
const S5_SW_REG6: u32 = 0x56000518;
const S5_PM_RUN: u32 = 0x56000704;

// --- S5 power controller, power policy units, DDR controllers --------------------------

/// CIX's S5 register block (mclp_retention.c's layout)
pub const S5_PMCTRL: u32 = 0x56000000;
const S5_SLP_TYP: u32 = 0x100;
const S5_CSU_PM_INTR: u32 = 0x220;
const S5_CSU_PM_INTR_EN: u32 = 0x22c;
pub const S5_WARM_RST: u32 = 0x300;
/// aggregator line 18, source 0: the S5 controller's PM interrupt
const S5_LINE: u32 = 18;

/// Arm PPUs of the DSU (0x4f030000) and the cores (0x4f080000 + core <<
/// 20): the power status (+8) follows the policy (+0) at once (modelled;
/// CIX's power_up_dsu_and_bsp and wait_for_bsp_and_dsu_idle poll it).
fn ppu_status(addr: u32) -> bool {
    if addr & 0xffff != 8 || addr >> 24 != 0x4f {
        return false;
    }
    if addr == 0x4f030008 {
        return true;
    }
    (addr >> 16) & 0xf == 8 && (addr >> 20) & 0xf < 12
}

// Cadence (Denali) DDR controllers at 0x4c010000 + 0x20000 * channel: CIX's
// mclp_swif sends a low-power command in LP_CMD (+0x2bc [22:16]) and polls
// LP_STATE (+0x2f8 [14:8], bit 14: the last command completed) for the
// state _s_lp_SwCmdStMap gives the command.  Modelled: the state changes
// at once.
const DDRC_LP_CMD: u32 = 0x2bc;
const DDRC_LP_STATE: u32 = 0x2f8;

fn lp_cmd_state(cmd: u32) -> u32 {
    match cmd {
        2 => 0,
        3 => 1,
        0x23 => 2,
        5 => 3,
        0x25 => 4,
        9 => 11,
        0x29 => 12,
        0x11 => 13,
        0x31 => 14,
        0x51 => 15,
        _ => 0,
    }
}

// --- the PM's local watchdog -----------------------------------------------------------

/// an Arm SP805 (CIX's dev_wdt.c): the SE's 0x46560000
pub const WDT: u32 = 0xa0050000;
const WDT_LOAD: u32 = 0x000;
const WDT_CONTROL: u32 = 0x008;
const WDT_INTCLR: u32 = 0x00c;
const WDT_LOCK: u32 = 0xc00;
const WDT_UNLOCK: u32 = 0x1acce551;

// --- counters (fans' PWM and tachometers) and PVT sensors --------------------------------

/// CIX's dev_counters.c: three blocks of eight counters (0x1000 apart): 0
/// and 1 the tachometers (aggregator lines 22 and 23), 2 the PWM outputs.
pub const CNTS: [u32; 3] = [0xa0040000, 0xa0048000, 0x44110000];
const CNTS_LINES: [u32; 2] = [22, 23];
/// CCOUNT cycles per 25 MHz counter tick
const CNT_TICK: u64 = 16;

/// PVT sensors (src/pvt.c's layout): sensor i at PVT_BASE + 0x1000 (i + 1)
pub const PVT_BASE: u32 = 0xa0070000;
pub const PVT_N: usize = 13;
const PVT_SAFE_LINE: usize = 28;
const PVT_WARN_LINE: usize = 29;

/// A temperature as the sensor's 10-bit code (CIX's
/// convert_temperature_to_data).
pub fn pvt_code(celsius: f64) -> u32 {
    ((celsius * celsius * 6.8504e-4 + celsius * 3.584 + 223.3) as i64 & 0x3ff) as u32
}

// --- mailboxes and the interrupt aggregator ----------------------------------------------

/// mailbox i at MBOX + i * 0x10000 (SoC 0x06590000 + ...)
pub const MBOX: u32 = 0xa0080000;
pub const MBOX_N: usize = 10;
// Mailbox registers as CSU_PM uses them (dev_mbox_*): 32 data words (the
// SCMI shared memory, in TF-A's layout), +0x80 doorbell (the sender writes
// it), +0x84..+0xa8 identification and fast channels, +0xbc/+0xc0 which
// status bits interrupt the AP / the PM, +0xc4 clear, +0xc8 status.
// Even mailboxes carry AP -> PM requests, odd ones PM -> AP; TF-A's SCMI
// channel is SoC 0x065b0000, mailbox 2.
const MBOX_DOORBELL: u32 = 0x80;
const MBOX_TO_AP_EN: u32 = 0xbc;
const MBOX_TO_PM_EN: u32 = 0xc0;
const MBOX_CLEAR: u32 = 0xc4;
const MBOX_STATUS: u32 = 0xc8;

/// line L (12..30): +4*(L-12) raw, +0x80 enable
pub const AGG: u32 = 0xa0000500;
/// bit i: mailbox i
const MBOX_LINE: u32 = 25;
/// bit i: I2C controller i
const I2C_LINE: u32 = 24;

/// Written by CIX's SE firmware before it starts the PM, as the SE's model
/// runs it on the Orion O6 image with blank eFuses: the PM strap
/// (drv_memcfg_set_pm_strap_info: checksum, board, V/F version, DVFS mode,
/// PVT average), the harvest word (cores and blocks off, boot core) and the
/// CPU frequency mode.
pub const SE_HANDOFF: [(u32, u32); 4] = [
    (0xa0000800, 0x020fffef), // PM strap word 0 (the SE's 0x46510800)
    (0xa0000804, 0x00000000), // PM strap word 1
    (0x56000508, 0x0000a000), // S5_SW_HARVEST
    (0x5600050c, 0x00000000), // S5_SW_CPU_FREQ
];

#[derive(Clone, Debug, PartialEq)]
pub enum Event {
    Sleep(u32),
    Wake,
    WarmReset(u32),
    /// the watchdog ran out twice: the SoC's reset
    WatchdogReset,
}

impl std::fmt::Display for Event {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        match self {
            Event::Sleep(v) => write!(f, "('sleep', {v})"),
            Event::Wake => write!(f, "('wake', 1)"),
            Event::WarmReset(v) => write!(f, "('warm reset', {v})"),
            Event::WatchdogReset => write!(f, "('watchdog reset', 1)"),
        }
    }
}

/// The SP805: its lock (0xc00: writing 0x1acce551 unlocks, anything else
/// locks; reads 1 while locked), which CIX's dev_wdt_set_timeout checks,
/// and its count: loaded from WdogLoad when the control register enables
/// it or the interrupt is cleared, it runs at `hz`; the first time it runs
/// out the interrupt is up (recorded: the lines' use is not known), the
/// second resets the SoC (Event::WatchdogReset, once).
#[derive(Clone)]
pub struct Sp805 {
    locked: u32,
    /// CCOUNT at which the count runs out, when it runs
    pub due: Option<u64>,
    pub expired: u32,
    /// counts per second (the boards' 5 s timeout: the load value at 25 MHz)
    pub hz: u64,
    /// the count runs (a test turns it on: an expiry wakes a sleeping PM)
    pub count: bool,
}

/// Bus for the interpreter: devices by 4 KiB block, plain memory elsewhere.
#[derive(Clone)]
pub struct PmSoc {
    /// unknown I2C addresses answer as register files
    pub auto_i2c: bool,
    /// address 0x21 answers as an AU4683
    pub au4683: bool,
    pub mem: Mem,
    pub console: Vec<u8>,
    pub i2c_log: Vec<I2cLog>,
    /// 0-2 masters (PMICs); 3 the slave the EC reads, a master for the
    /// MS-R1's board sensors
    pub i2c: Vec<CadenceI2c>,
    uart: HashMap<u32, u32>,
    /// the SE's side: the handshake words the PM wrote
    pub se_log: Vec<u32>,
    pub mbox_status: [u32; MBOX_N],
    /// doorbells the PM rang: (mailbox, value), in order
    pub doorbells: Vec<(usize, u32)>,
    /// sleep, wake, warm reset
    pub events: Vec<Event>,
    ddrc_lp: [u32; 4],
    pub wdt: Sp805,
    /// tachometers running: (block, odd node) -> next interrupt, period
    tachs: Vec<((usize, usize), u64, u64)>,
    pub fan_max_rpm: u64,
    pub fan_pulses: u64,
    pub fan_rpm: HashMap<usize, u64>,
    pub pvt_temp: [Option<f64>; PVT_N],
    /// aggregator lines' sources raised by models and tests
    pub irq_raw: [u32; 32],
    /// first access of each 4 KiB block: the instruction
    pub touched: Vec<(u32, u32)>,
    touched_set: HashMap<u32, ()>,
    /// registers read, with the number of reads
    pub reads: HashMap<u32, u32>,
    pub count_reads: bool,
}

#[derive(Clone, Copy)]
enum Dev {
    I2c(usize),
    Uart,
    Mbox(usize),
    S5,
    Ddrc(usize),
    Wdt,
    Counter(usize, usize),
    Pvt(usize),
    None,
}

fn device(addr: u32) -> Dev {
    let b = addr & !0xfff;
    match b {
        0xa0010000..=0xa0013000 => Dev::I2c(((b - 0xa0010000) >> 12) as usize),
        0xa0030000 => Dev::Uart,
        0xa0080000..=0xa0110000 if b & 0xffff == 0 => Dev::Mbox(((b - MBOX) >> 16) as usize),
        S5_PMCTRL => Dev::S5,
        0x4c010000 | 0x4c030000 | 0x4c050000 | 0x4c070000 => Dev::Ddrc(((b - 0x4c010000) >> 17) as usize),
        WDT => Dev::Wdt,
        0xa0040000..=0xa0047000 => Dev::Counter(0, ((b - CNTS[0]) >> 12) as usize),
        0xa0048000..=0xa004f000 => Dev::Counter(1, ((b - CNTS[1]) >> 12) as usize),
        0x44110000..=0x44117000 => Dev::Counter(2, ((b - CNTS[2]) >> 12) as usize),
        0xa0071000..=0xa007d000 => Dev::Pvt(((b - PVT_BASE) >> 12) as usize - 1),
        _ => Dev::None,
    }
}

impl PmSoc {
    pub fn new(auto_i2c: bool) -> Self {
        let mut soc = PmSoc {
            auto_i2c,
            au4683: false,
            mem: Mem::default(),
            console: Vec::new(),
            i2c_log: Vec::new(),
            i2c: (0..4).map(|i| CadenceI2c::new(0xa0010000 + 0x1000 * i, &format!("i2c{i}"))).collect(),
            uart: HashMap::new(),
            se_log: Vec::new(),
            mbox_status: [0; MBOX_N],
            doorbells: Vec::new(),
            events: Vec::new(),
            ddrc_lp: [0x40; 4], // idle, the last command completed
            wdt: Sp805 { locked: 1, due: None, expired: 0, hz: 25_000_000, count: false },
            tachs: Vec::new(),
            fan_max_rpm: 6000,
            fan_pulses: 2,
            fan_rpm: HashMap::new(),
            pvt_temp: [None; PVT_N],
            irq_raw: [0; 32],
            touched: Vec::new(),
            touched_set: HashMap::new(),
            reads: HashMap::new(),
            count_reads: false,
        };
        for base in plls() {
            for (off, v) in PLL_RESET {
                soc.mem.write32(base + off, v);
            }
        }
        for (addr, v) in SE_HANDOFF {
            soc.mem.write32(addr, v);
        }
        soc
    }

    fn mbox_pending(&self, i: usize) -> u32 {
        self.mbox_status[i] & self.mem.read32(MBOX + i as u32 * 0x10000 + MBOX_TO_PM_EN)
    }

    fn s5_pending(&self) -> u32 {
        self.mem.read32(S5_PMCTRL + S5_CSU_PM_INTR) & 1
    }

    fn agg_raw(&self, line: u32) -> u32 {
        match line {
            MBOX_LINE => (0..MBOX_N).filter(|i| self.mbox_pending(*i) != 0).map(|i| 1 << i).sum(),
            I2C_LINE => {
                self.i2c.iter().enumerate().filter(|(_, c)| c.irq()).map(|(i, _)| 1u32 << i).sum::<u32>()
                    | self.irq_raw[line as usize]
            }
            S5_LINE => self.s5_pending(),
            _ => self.irq_raw[(line & 31) as usize],
        }
    }

    pub fn update_irqs(&mut self, io: &mut Io) {
        let mut lines = 0;
        for line in 12..=30 {
            if self.agg_raw(line) & self.mem.read32(AGG + 0x80 + 4 * (line - 12)) != 0 {
                lines |= 1 << line;
            }
        }
        io.int_lines = lines;
    }

    // --- the AP's and the SE's side ------------------------------------------------

    /// The AP writes a mailbox's doorbell.
    pub fn ring(&mut self, io: &mut Io, mbox: usize, bits: u32) {
        self.mbox_status[mbox] |= bits;
        self.update_irqs(io);
    }

    /// The SE's side of the boot handshake: the PM announces itself with
    /// 0x5c5a, the SE clears it, finishes its SoC init and writes 0x55ca;
    /// the PM answers 0x5a5c, which the SE clears before it loads BL2.  On
    /// wake-up the SE waits for the PM's 0xaacc (start the sensor fusion
    /// hub: the SE answers 0xccaa) or 0xaabb (stop it: the SE writes 0).
    fn pm_wrote(&mut self, value: u32) -> u32 {
        self.se_log.push(value);
        match value {
            0x5c5a => 0x55ca,
            0x5a5c | 0xaabb => 0,
            0xaacc => 0xccaa,
            v => v,
        }
    }

    /// Suspend to RAM (firmware/se/service/str.c): the AP's FFA_SUSPEND
    /// makes the SE's idle loop flush the system cache, mark the S5
    /// registers and write 2 into the PM's fast channel; here at once.
    pub fn se_suspend(&mut self) {
        let m = &mut self.mem;
        m.write32(S5_SW_SUSPEND, m.read32(S5_SW_SUSPEND) | 1);
        m.write32(S5_SW_STR, 1);
        m.write32(S5_SW_REG6, m.read32(S5_SW_REG6) | 1);
        m.write32(PM_FAST_CHANNEL, 2);
    }

    /// The SE resumes and writes 1 into the fast channel.
    pub fn se_wake(&mut self) {
        self.mem.write32(S5_PM_RUN, 0);
        self.mem.write32(PM_FAST_CHANNEL, 1);
    }

    /// A wake-up source raises the S5 controller's PM interrupt.
    pub fn s5_wake(&mut self, io: &mut Io) {
        self.mem.write32(S5_PMCTRL + S5_CSU_PM_INTR, 1);
        self.events.push(Event::Wake);
        self.update_irqs(io);
    }

    // --- the I2C slave's host (the EC on the Orion O6) --------------------------------

    /// The controller's own address when it is a slave.
    pub fn slave_addr(&self, bus: usize) -> Option<u8> {
        let c = &self.i2c[bus];
        (c.cr() & CR_MS == 0).then(|| (c.reg(ADDR) & 0x7f) as u8)
    }

    /// The host writes bytes to the slave controller.
    pub fn slave_write(&mut self, io: &mut Io, bus: usize, data: &[u8]) {
        let c = &mut self.i2c[bus];
        c.rx.extend_from_slice(data);
        c.isr |= ISR_DATA | ISR_COMP;
        self.update_irqs(io);
    }

    /// The host starts a read from the slave controller: the firmware is
    /// asked for data (slave_tx collects what it writes).
    pub fn slave_read_start(&mut self, io: &mut Io, bus: usize) {
        let c = &mut self.i2c[bus];
        c.slave_reading = true;
        c.slave_tx.clear();
        c.isr |= ISR_SLV_RDY;
        self.update_irqs(io);
    }

    /// The host ends its read (NACK, stop): what the firmware gave.
    pub fn slave_read_end(&mut self, io: &mut Io, bus: usize) -> Vec<u8> {
        let c = &mut self.i2c[bus];
        c.slave_reading = false;
        c.isr |= ISR_COMP | ISR_NACK;
        let out = std::mem::take(&mut c.slave_tx);
        self.update_irqs(io);
        out
    }

    // --- timed models: tachometers, the watchdog -----------------------------------------

    fn tach_control(&mut self, io: &Io, inst: usize, node: usize, value: u32) {
        let key = (inst, node);
        let pos = self.tachs.iter().position(|t| t.0 == key);
        if value & 0x20000 != 0 {
            let period = (self.mem.read32(CNTS[inst] + 0x1000 * node as u32) as u64).max(1) * CNT_TICK;
            let entry = (key, io.ccount + period, period);
            match pos {
                Some(p) => self.tachs[p] = entry,
                None => self.tachs.push(entry),
            }
        } else if let Some(p) = pos {
            self.tachs.remove(p);
        }
    }

    /// The fan on PWM 2:3+k and tachometer k >> 1:4+2(k & 1) turns at
    /// fan_max_rpm times the duty (or what a test sets in fan_rpm[k]), with
    /// fan_pulses pulses a turn.
    pub fn fan_speed(&self, k: usize) -> u64 {
        if let Some(v) = self.fan_rpm.get(&k) {
            return *v;
        }
        let pwm = CNTS[2] + 0x1000 * (3 + k as u32);
        let (period, high) = (self.mem.read32(pwm + 0x2c) as u64, self.mem.read32(pwm + 0x28) as u64);
        if period != 0 {
            self.fan_max_rpm * high.min(period) / period
        } else {
            0
        }
    }

    fn tach_fire(&mut self, io: &mut Io, idx: usize) {
        let ((inst, node), due, period) = self.tachs[idx];
        let k = 2 * inst + (node - 5) / 2;
        let even = CNTS[inst] + 0x1000 * (node as u32 - 1);
        let pulses = self.fan_speed(k) * self.fan_pulses * period / (CORE_HZ * 60);
        let v = self.mem.read32(even + 0x18).wrapping_add(pulses as u32);
        self.mem.write32(even + 0x18, v);
        let line = CNTS_LINES[inst] as usize;
        self.irq_raw[line] |= 1 << (13 + (node - 5) / 2);
        self.tachs[idx].1 = due + period;
        self.update_irqs(io);
    }

    fn wdt_reload(&mut self, io: &Io) {
        let ctl = self.mem.read32(WDT + WDT_CONTROL);
        if ctl & 1 == 0 || !self.wdt.count {
            self.wdt.due = None;
            return;
        }
        let load = self.mem.read32(WDT + WDT_LOAD) as u64;
        self.wdt.due = Some(io.ccount + load.max(1) * CORE_HZ / self.wdt.hz.max(1));
    }

    // --- PVT ---------------------------------------------------------------------------

    /// A PVT sensor's reading (+0x04: three 10-bit codes, max, min and
    /// latest) follows pvt_temp[i] (degrees C) once a test sets it; it
    /// raises line 29 (its bit) while the code is at or above its warning
    /// threshold (+0x0c [9:0], when set) and line 28 below its safe one
    /// (+0x0c [19:10]).  Without a temperature it is plain memory.
    pub fn pvt_update(&mut self, io: &mut Io) {
        let (mut warn, mut safe) = (0, 0);
        for i in 0..PVT_N {
            let base = PVT_BASE + 0x1000 * (i as u32 + 1);
            let Some(t) = self.pvt_temp[i] else { continue };
            if self.mem.read32(base) & 1 == 0 {
                continue;
            }
            let (thr, c) = (self.mem.read32(base + 0x0c), pvt_code(t));
            if thr & 0x3ff != 0 && c >= thr & 0x3ff {
                warn |= 1 << i;
            }
            if c < (thr >> 10) & 0x3ff {
                safe |= 1 << i;
            }
        }
        self.irq_raw[PVT_WARN_LINE] = warn;
        self.irq_raw[PVT_SAFE_LINE] = safe;
        self.update_irqs(io);
    }

    pub fn set_temps(&mut self, io: &mut Io, t: f64) {
        self.pvt_temp = [Some(t); PVT_N];
        self.pvt_update(io);
    }

    fn note(&mut self, io: &Io, addr: u32) {
        let b = addr & !0xfff;
        if self.touched_set.insert(b, ()).is_none() {
            self.touched.push((b, io.pc));
        }
    }
}

impl Bus for PmSoc {
    fn read(&mut self, io: &mut Io, addr: u32, size: u32) -> u64 {
        self.note(io, addr);
        if self.count_reads {
            *self.reads.entry(addr).or_insert(0) += 1;
        }
        let off = addr & 0xfff;
        match device(addr) {
            Dev::I2c(i) => {
                let v = self.i2c[i].read(off);
                return v;
            }
            Dev::Uart => {
                return if off == 0x18 { 0x90 } else { self.uart.get(&off).copied().unwrap_or(0) as u64 };
            }
            Dev::Mbox(i) => {
                return if off == MBOX_STATUS { self.mbox_status[i] as u64 } else { self.mem.read(addr, size) };
            }
            Dev::Ddrc(i) => {
                let mut v = self.mem.read(addr, size);
                if off == DDRC_LP_STATE && size == 4 {
                    v = (v & !0x7f00) | (self.ddrc_lp[i] as u64) << 8;
                }
                return v;
            }
            Dev::Wdt => {
                return if off == WDT_LOCK { self.wdt.locked as u64 } else { self.mem.read(addr, size) };
            }
            Dev::Pvt(i) => {
                if let (0x04, Some(t)) = (off, self.pvt_temp[i]) {
                    let c = pvt_code(t) as u64;
                    return c | c << 10 | c << 20;
                }
                return self.mem.read(addr, size);
            }
            Dev::S5 | Dev::Counter(..) => return self.mem.read(addr, size),
            Dev::None => {}
        }
        if is_pll_lock(addr) {
            return self.mem.read(addr, size) | 0x10; // PLLs lock at once
        }
        if ppu_status(addr) {
            return (self.mem.read(addr, size) & !0xf) | (self.mem.read(addr - 8, 4) & 0xf);
        }
        if (AGG..AGG + 0x4c).contains(&addr) {
            return self.agg_raw(12 + (addr - AGG) / 4) as u64;
        }
        self.mem.read(addr, size)
    }

    fn write(&mut self, io: &mut Io, addr: u32, size: u32, value: u64) {
        self.note(io, addr);
        let off = addr & 0xfff;
        match device(addr) {
            Dev::I2c(i) => {
                let auto = self.auto_i2c.then_some(self.au4683);
                let nwrites = io.nwrites;
                let (c, log) = (&mut self.i2c[i], &mut self.i2c_log);
                c.write(off, value as u32, log, io, nwrites, auto);
                self.update_irqs(io);
            }
            Dev::Uart => {
                if off == 0 {
                    self.console.push(value as u8);
                }
                self.uart.insert(off, value as u32);
            }
            Dev::Mbox(i) => {
                if off == MBOX_CLEAR {
                    self.mbox_status[i] &= !(value as u32);
                    self.update_irqs(io);
                    return;
                }
                let mut value = value;
                if off == MBOX_DOORBELL {
                    self.doorbells.push((i, value as u32));
                }
                if addr == PM_MAGIC {
                    value = self.pm_wrote(value as u32) as u64;
                }
                self.mem.write(addr, size, value);
                if off == MBOX_TO_PM_EN || off == MBOX_TO_AP_EN {
                    self.update_irqs(io);
                }
            }
            Dev::S5 => {
                self.mem.write(addr, size, value);
                if off == S5_SLP_TYP && value != 0 {
                    self.events.push(Event::Sleep(value as u32));
                } else if off == S5_WARM_RST && value & 1 != 0 {
                    self.events.push(Event::WarmReset(value as u32));
                }
                if off == S5_CSU_PM_INTR || off == S5_CSU_PM_INTR_EN {
                    self.update_irqs(io);
                }
            }
            Dev::Ddrc(i) => {
                self.mem.write(addr, size, value);
                if off == DDRC_LP_CMD && size == 4 {
                    self.ddrc_lp[i] = 0x40 | lp_cmd_state((value as u32 >> 16) & 0x7f);
                }
            }
            Dev::Wdt => {
                if off == WDT_LOCK {
                    self.wdt.locked = (value as u32 != WDT_UNLOCK) as u32;
                    return;
                }
                self.mem.write(addr, size, value);
                if matches!(off, WDT_LOAD | WDT_CONTROL | WDT_INTCLR) {
                    if off == WDT_INTCLR {
                        self.wdt.expired = 0;
                    }
                    self.wdt_reload(io);
                }
            }
            Dev::Counter(inst, node) => {
                self.mem.write(addr, size, value);
                if inst < 2 && (node == 5 || node == 7) {
                    if off == 0x24 {
                        self.tach_control(io, inst, node, value as u32);
                    } else if off == 0x34 && value & 64 != 0 {
                        self.irq_raw[CNTS_LINES[inst] as usize] &= !(1 << (13 + (node - 5) / 2));
                        self.update_irqs(io);
                    }
                }
            }
            Dev::Pvt(_) => {
                self.mem.write(addr, size, value);
                if off == 0x00 || off == 0x0c {
                    self.pvt_update(io);
                }
            }
            Dev::None => {
                self.mem.write(addr, size, value);
                if (AGG + 0x80..AGG + 0xcc).contains(&addr) {
                    self.update_irqs(io);
                }
            }
        }
    }

    fn next_event(&mut self, io: &Io) -> Option<u64> {
        let mut due = self.tachs.iter().map(|t| t.1).min();
        if let Some(w) = self.wdt.due {
            due = Some(due.map_or(w, |d| d.min(w)));
        }
        due.map(|d| d.saturating_sub(io.ccount).max(1))
    }

    fn advance(&mut self, io: &mut Io, _cycles: u64) {
        let mut order: Vec<usize> = (0..self.tachs.len()).collect();
        order.sort_by_key(|i| self.tachs[*i].1);
        let keys: Vec<_> = order.iter().map(|i| self.tachs[*i].0).collect();
        for key in keys {
            while let Some(idx) = self.tachs.iter().position(|t| t.0 == key) {
                if self.tachs[idx].1 > io.ccount {
                    break;
                }
                self.tach_fire(io, idx);
            }
        }
        while let Some(due) = self.wdt.due {
            if due > io.ccount {
                break;
            }
            self.wdt.expired += 1;
            if self.wdt.expired >= 2 {
                self.events.push(Event::WatchdogReset);
                self.wdt.due = None;
            } else {
                let load = self.mem.read32(WDT + WDT_LOAD) as u64;
                self.wdt.due = Some(due + load.max(1) * CORE_HZ / self.wdt.hz.max(1));
            }
        }
    }
}

// --- the AP's side of SCMI -----------------------------------------------------------

pub const SMT_STATUS: u32 = 0x04;
pub const SMT_FLAGS: u32 = 0x10;
pub const SMT_LEN: u32 = 0x14;
pub const SMT_HEADER: u32 = 0x18;
pub const SMT_PAYLOAD: u32 = 0x1c;
const SMT_FREE: u32 = 1;
const SMT_ERROR: u32 = 2;

/// An SCMI answer: the status (None without a payload) and the words after it.
pub type Answer = Result<(Option<i32>, Vec<u32>), String>;

fn asleep(why: &Option<String>) -> bool {
    why.as_deref().map_or(false, |w| w.starts_with("waiti with nothing"))
}

/// Send one SCMI command the way TF-A does (drivers/arm/css/scmi with
/// sky1_bl31_setup.c's channel): wait for the channel to be free, write
/// header and payload into the shared memory, ring the doorbell, run the
/// PM until it frees the channel.
pub fn scmi(cpu: &mut Cpu, protocol: u32, msg: u32, args: &[u32], mbox: usize, budget: u64) -> Answer {
    scmi_raw(cpu, protocol, msg, args, mbox, budget, None, 0)
}

thread_local! {
    /// Instructions run between two looks at a channel (scmi, pm_msg): how
    /// soon after the PM's answer the AP goes on.
    pub static POLL: std::cell::Cell<u64> = const { std::cell::Cell::new(20_000) };
}

pub fn poll() -> u64 {
    POLL.with(|p| p.get())
}

/// scmi() with the message length as the caller gives it (a malformed
/// request when it does not match the arguments) and the channel flags
/// (bit 0: the agent wants an interrupt when the answer is there, as
/// Linux does; TF-A polls).
#[allow(clippy::too_many_arguments)]
pub fn scmi_raw(cpu: &mut Cpu, protocol: u32, msg: u32, args: &[u32], mbox: usize, budget: u64, len: Option<u32>, flags: u32) -> Answer {
    let base = MBOX + mbox as u32 * 0x10000;
    let chunk = poll();
    let end = cpu.insns + budget;
    while cpu.bus.mem.read32(base + SMT_STATUS) & SMT_FREE == 0 {
        if cpu.insns >= end {
            return Err(format!("channel {mbox} never free"));
        }
        let why = cpu.run(chunk);
        if asleep(&why) {
            return Err(format!("channel {mbox} never free"));
        }
        if let Some(w) = why {
            return Err(format!("stopped: {w}"));
        }
    }
    let header = (msg & 0xff) | (protocol & 0xff) << 10;
    let m = &mut cpu.bus.mem;
    // (the error bit of an earlier refusal goes too, as Linux clears it)
    m.write32(base + SMT_STATUS, m.read32(base + SMT_STATUS) & !(SMT_FREE | SMT_ERROR));
    m.write32(base + SMT_HEADER, header);
    for (i, a) in args.iter().enumerate() {
        m.write32(base + SMT_PAYLOAD + 4 * i as u32, *a);
    }
    m.write32(base + SMT_LEN, len.unwrap_or(4 + 4 * args.len() as u32));
    m.write32(base + SMT_FLAGS, flags);
    cpu.bus.ring(&mut cpu.io, mbox, 1);
    while cpu.bus.mem.read32(base + SMT_STATUS) & SMT_FREE == 0 {
        if cpu.insns >= end {
            return Err(format!("no answer to protocol {protocol:#x} message {msg}"));
        }
        let why = cpu.run(chunk);
        let free = cpu.bus.mem.read32(base + SMT_STATUS) & SMT_FREE != 0;
        if asleep(&why) && !free {
            return Err(format!("no answer to protocol {protocol:#x} message {msg} (the PM sleeps)"));
        }
        if let Some(w) = &why {
            if !asleep(&why) {
                return Err(format!("stopped: {w} at {}", cpu.func(cpu.pc)));
            }
        }
    }
    let m = &cpu.bus.mem;
    if m.read32(base + SMT_STATUS) & SMT_ERROR != 0 {
        return Err("channel error".to_string());
    }
    let n = (m.read32(base + SMT_LEN) as i64 - 4).div_euclid(4).clamp(0, 25);
    let words: Vec<u32> = (0..n as u32).map(|i| m.read32(base + SMT_PAYLOAD + 4 * i)).collect();
    Ok((words.first().map(|w| *w as i32), words.iter().skip(1).copied().collect()))
}

/// Run the PM for ms milliseconds of simulated time (CCOUNT at CORE_HZ).
/// Time asleep in WAITI goes by at once: CCOUNT jumps to the next enabled
/// CCOMPARE, never past the end; a PM with nothing to wake it sleeps out
/// the rest.  Deterministic: the same image and inputs give the same run.
/// Returns None, or why the PM stopped (an assertion...).
pub fn run_for(cpu: &mut Cpu, ms: f64) -> Option<String> {
    let chunk = 50_000;
    let end = cpu.io.ccount + (ms * CORE_HZ as f64 / 1000.0).floor() as u64;
    cpu.until = Some(end);
    let mut out = None;
    while cpu.io.ccount < end {
        // a cycle or more per instruction
        let why = cpu.run(chunk.min(end - cpu.io.ccount).max(1));
        match why.as_deref() {
            None => continue,
            Some("until") => break,
            Some(w) if w.starts_with("waiti with nothing") => {
                cpu.skip(end - cpu.io.ccount);
                break;
            }
            _ => {
                out = why;
                break;
            }
        }
    }
    cpu.until = None;
    out
}

pub fn boot(elf: &str, soc: Option<PmSoc>) -> Result<Cpu, String> {
    let mut cpu = Xtensa::new(soc.unwrap_or_else(|| PmSoc::new(true)));
    cpu.add_ram(RAM.0, RAM.1);
    cpu.load_elf(elf)?;
    for name in DELAYS {
        if let Some(a) = cpu.byname.get(name).copied() {
            cpu.set_stub(
                a,
                Rc::new(|c: &mut Cpu| {
                    let n = c.arg(0) as u64;
                    c.skip(n);
                    None
                }),
            );
        }
    }
    cpu.pc = RESET;
    Ok(cpu)
}

/// pm_emu: run an image from reset and print its console, the I2C traffic
/// and where it stopped.
pub fn main(args: &[String]) -> i32 {
    let mut elf = None;
    let mut count = 5_000_000u64;
    let mut until = None;
    let mut it = args.iter();
    while let Some(a) = it.next() {
        match a.as_str() {
            "--count" => count = it.next().and_then(|v| v.parse().ok()).unwrap_or(count),
            "--until" => until = it.next().cloned(),
            _ => elf = Some(a.clone()),
        }
    }
    let Some(elf) = elf else {
        eprintln!("usage: sky1emu pm ELF [--count N] [--until SYMBOL]");
        return 2;
    };
    let mut cpu = match boot(&elf, None) {
        Ok(c) => c,
        Err(e) => {
            eprintln!("{e}");
            return 2;
        }
    };
    for name in ASSERTS {
        if let Some(a) = cpu.byname.get(name).copied() {
            cpu.set_stop(a, name);
        }
    }
    if let Some(u) = until {
        let a = cpu.addr(&u);
        cpu.set_stop(a, &u);
    }
    let why = cpu.run(count);
    println!(
        "stopped: {} after {} instructions ({:.1} M cycles), in",
        why.as_deref().unwrap_or("None"),
        cpu.insns,
        cpu.io.ccount as f64 / 1e6
    );
    for pc in cpu.backtrace(16) {
        println!("  {}", cpu.func(pc));
    }
    println!("console:");
    println!("{}", cpu.bus.console.iter().map(|b| *b as char).collect::<String>());
    println!("i2c:");
    let n = cpu.bus.i2c_log.len();
    for l in &cpu.bus.i2c_log[n.saturating_sub(40)..] {
        println!("  {} {}", l.bus, l.what);
    }
    println!("handshake: {}", cpu.bus.se_log.iter().map(|v| format!("{v:#x}")).collect::<Vec<_>>().join(" "));
    println!("blocks:");
    for (b, pc) in &cpu.bus.touched {
        println!("  {b:#010x} first in {}", cpu.func(*pc));
    }
    0
}
