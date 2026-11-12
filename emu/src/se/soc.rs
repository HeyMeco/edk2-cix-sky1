// SPDX-License-Identifier: BSD-2-Clause-Patent
//! Sky1 as the SE sees it: the boot ROM's hand-over in SRAM, the QSPI
//! flash, PLLs that lock, power controllers that follow, the crypto
//! engine's status, the PM's handshakes, the mailboxes, and (DdrModel) the
//! four LPDDR5 channels as far as CIX's DDR driver waits for them.
//! Registers without a model behave like memory.

use crate::arm::{Bus, Io, Mem};
use std::collections::{BTreeMap, VecDeque};

pub const SRAM: u32 = 0x20000000;
pub const SRAM_SIZE: u32 = 0x80000;
pub const QSPI: u32 = 0xe0044000;
pub const BL1_HEADER: usize = 0x188000;
/// where the boot ROM leaves its copy of the bootloader1 header
pub const BL1_HEADER_SRAM: u32 = 0x2007e800;
pub const EFUSE: u32 = 0xb0040000;
pub const LIFE_CYCLE: u32 = 0xb00300e0;

/// SRAM repair controllers (their status at +0x40000010)
/// The FCH's xSPI controller: command registers 0..5, and the slave DMA port
const XSPI: u32 = 0x44180000;
const XSPI_CMD_STATUS: u32 = XSPI + 0x44;
const XSPI_INTR: u32 = XSPI + 0x110;
const XSPI_SDMA_SIZE: u32 = XSPI + 0x240;
const XSPI_SDMA_INFO: u32 = XSPI + 0x244;
const XSPI_SDMA_PORT: u32 = 0x40010000;

const MEMR_BASES: [u32; 20] = [
    0x0fc50000, 0x10000000, 0x14000000, 0x15000000, 0x14250000, 0x07000000, 0x14330000, 0x14230000, 0x06000000,
    0x0d030000, 0x0a000000, 0x08000000, 0x14050000, 0x09310000, 0x14270000, 0x0b1a0000, 0x0b000000, 0x0b0d0000,
    0x09250000, 0x00000000,
];

#[derive(Clone)]
pub struct SkySoc {
    pub flash: std::rc::Rc<Vec<u8>>,
    /// byte address in the fuse mirror -> bytes
    pub efuse: BTreeMap<u32, Vec<u8>>,
    pub life_cycle: u32,
    pub boot_reason: u32,
    pub jedec: [u8; 3],
    qspi_fifo: VecDeque<u8>,
    qspi_started: bool,
    qspi_wel: bool,
    qspi_wbuf: Vec<u8>,
    /// instruction, address, data bytes
    qspi_cmd: Option<(u8, u32, u32)>,
    pm_magic: VecDeque<u32>,
    rng: u32,
    /// the mailbox (its base) with a request waiting
    pub mbox_pending: Option<u32>,
    window: u32,
    /// None when the DDR driver's entry points are stubbed
    pub ddr: Option<DdrModel>,
    /// the run ends at this write (address, value)
    pub stop_write: Option<(u32, u32)>,
    /// the model with time (off: as the Python reference had it)
    pub t: Timed,
}

/// Time units: 1/2400 MHz, so that a cycle of the 800 MHz core clock and one of the 24 MHz reference are whole.
pub const US: u64 = 2400;
pub const MS: u64 = 1000 * US;
pub const SEC: u64 = 1000 * MS;
/// units a cycle of the 24 MHz reference takes
pub const REF_TICK: u64 = 100;

pub const MBOX_BASE: u32 = 0xa0050000;
pub const MBOX_CHANNELS: usize = 9;
/// the interrupt of mailbox channel k
pub fn mbox_irq(k: usize) -> u32 {
    43 - k as u32
}
pub const IRQ_WDT: u32 = 32;
pub const IRQ_SE_INTR: u32 = 231;
pub const IRQ_PM_WDT: u32 = 230;
pub const IRQ_AP_WDT: u32 = 232;
pub const WDT: u32 = 0xb0020000;
pub const SE_TIMER: u32 = 0xb0010000;
pub const PM_MAGIC: u32 = 0x46610090;
pub const SLOT_REG: u32 = 0xb0030100;
pub const WINDOW: u32 = 0x60000000;
pub const SLOT_SIZE: u32 = 0x08000000;
/// page keys of memory behind the window that the SE has no other address for: this | SoC address
pub const SOC_KEY: u64 = 1 << 48;
pub const SFH_SOC: u64 = 0x1c_a000_0000;

/// Something the model does at a time.
#[derive(Clone, Debug)]
pub enum Action {
    Line(u32, bool),
    /// the PM puts a word in its handshake register
    PmMagic(u32),
    /// status bits of a mailbox channel come up
    Mbox(usize, u32),
    /// the SFH's firmware has started: its handshake word
    SfhUp,
}

/// The model with time: what the Python reference's had not got, and the switches that make things fail.
/// What is "modelled, not known" is said at each.
#[derive(Clone)]
pub struct Timed {
    pub on: bool,
    sched: std::collections::BTreeMap<u64, Vec<Action>>,
    /// events for the report, with their time
    pub log: Vec<(u64, String)>,
    /// PLL blocks (offset in the clock generator, e.g. 0x7000) that never lock; 0xffff: the PCIe PHY's
    pub dead_plls: Vec<u32>,
    /// the PM answers the handshakes, this long after it is asked
    pub pm_alive: bool,
    pub pm_delay: u64,
    /// what the PM says when it comes up (0x5c5a; 0xaacc: it wants the sensor hub, the system is suspended)
    pub pm_boot_magic: u32,
    /// what the PM says when the SE tells it it has resumed (0xaacc: start the sensor hub, 0xaabb: stop it)
    pub pm_resume_magic: Option<u32>,
    /// the audio subsystem powers up when asked (its power self-test passes)
    pub audio_power: bool,
    /// the FCH's xSPI controller answers (the flash is reached through it when fuse 0x155 is set)
    pub xspi: bool,
    xspi_intr: u32,
    xspi_status: u32,
    xspi_op: (u32, u32),
    xspi_size: u32,
    xspi_info: u32,
    xspi_data: VecDeque<u8>,
    xspi_more: bool,
    /// the SFH's firmware answers, this long after the core is released
    pub sfh_alive: bool,
    pub sfh_delay: u64,
    sfh_word: u32,
    /// the AP takes what the SE puts in a FIFO channel, this long after
    pub ap_reads: bool,
    pub ap_delay: u64,
    /// mailbox interrupt status, per channel
    pub mbox_status: [u32; MBOX_CHANNELS],
    /// channels the SE acknowledged a request on (cleared by whoever asks)
    pub mbox_acked: [bool; MBOX_CHANNELS],
    mbox_fifo: [Vec<u32>; MBOX_CHANNELS],
    /// messages the SE sent through FIFO channels: (channel, words)
    pub sent: Vec<(usize, Vec<u32>)>,
    /// the watchdog cannot be fed any more (writes of its reload register are lost)
    pub wdt_stuck: bool,
    /// time units a count of the watchdog takes (the 24 MHz reference's 100; less: a watchdog that runs
    /// faster, for a scenario that would else take its 10 s of the SE's idle loop to simulate)
    pub wdt_tick: u64,
    /// watchdog: the time it runs out, and its interrupt
    wdt_at: u64,
    wdt_irq: bool,
    /// SE timers 0 and 1: the time they were started
    timer_on: [Option<u64>; 2],
    /// the UART's text, and the time each line started
    pub uart: Vec<u8>,
    pub uart_lines: Vec<(u64, usize)>,
    /// postcodes
    pub postcodes: Vec<u32>,
    /// the window's eight slots
    slots: [u32; 8],
    /// a system reset was asked for: (reason register, by)
    pub reset: Option<String>,
    /// characters waiting in the UART's receiver
    pub uart_rx: VecDeque<u8>,
    /// S5 registers the model answers: wake-up source and type, GPIO bank mask and state
    pub s5: BTreeMap<u32, u32>,
}

impl Default for Timed {
    fn default() -> Self {
        Timed {
            on: false,
            sched: Default::default(),
            log: Vec::new(),
            dead_plls: Vec::new(),
            pm_alive: true,
            pm_delay: 2 * MS,
            pm_boot_magic: 0x5c5a,
            pm_resume_magic: None,
            audio_power: true,
            xspi: false,
            xspi_intr: 0,
            xspi_status: 0,
            xspi_op: (0, 0),
            xspi_size: 0,
            xspi_info: 0,
            xspi_data: VecDeque::new(),
            xspi_more: false,
            sfh_alive: true,
            sfh_delay: 3 * MS,
            sfh_word: 0,
            ap_reads: true,
            ap_delay: 20 * US,
            mbox_status: [0; MBOX_CHANNELS],
            mbox_acked: [false; MBOX_CHANNELS],
            mbox_fifo: Default::default(),
            sent: Vec::new(),
            wdt_stuck: false,
            wdt_tick: REF_TICK,
            wdt_at: u64::MAX,
            wdt_irq: false,
            timer_on: [None; 2],
            uart: Vec::new(),
            uart_lines: Vec::new(),
            postcodes: Vec::new(),
            slots: [0; 8],
            reset: None,
            uart_rx: VecDeque::new(),
            s5: BTreeMap::new(),
        }
    }
}

impl SkySoc {
    pub fn new(flash: &[u8], life_cycle: u32, efuse: BTreeMap<u32, Vec<u8>>, ddr: bool) -> SkySoc {
        SkySoc {
            flash: std::rc::Rc::new(flash.to_vec()),
            efuse,
            life_cycle,
            boot_reason: 2,
            jedec: [0xef, 0x60, 0x18],
            qspi_fifo: VecDeque::new(),
            qspi_started: false,
            qspi_wel: false,
            qspi_wbuf: Vec::new(),
            qspi_cmd: None,
            pm_magic: VecDeque::new(),
            rng: 0x12345678,
            mbox_pending: None,
            window: 0,
            ddr: ddr.then(DdrModel::new),
            stop_write: None,
            t: Timed::default(),
        }
    }

    /// What the boot ROM leaves: the bootloader1 header in SRAM, the fuses in the fuse mirror.
    pub fn reset_state(&self, mem: &mut Mem) {
        let end = (BL1_HEADER + 0x1000).min(self.flash.len());
        if BL1_HEADER < end {
            mem.write_bytes(BL1_HEADER_SRAM, &self.flash[BL1_HEADER..end]);
        }
        for (addr, data) in &self.efuse {
            mem.map(EFUSE + addr);
            mem.write_bytes(EFUSE + addr, data);
        }
    }

    // QSPI: indirect mode on the flash image
    /// The xSPI controller's command register 0 written (the last of the six): a phase starts.  Modelled
    /// after the public Linux driver of the controller (spi-cadence-xspi.c: STIG mode) and what the two
    /// images do: a command phase (opcode and address), then a data phase whose bytes go through the
    /// slave DMA port; interrupt status bit 21 asks for the data, bit 23 says the phase is done, the command
    /// status has bit 15 when it completed.
    fn xspi_start(&mut self, mem: &Mem, r0: u32) {
        let r = |n: u32| mem.read32(XSPI + 4 * n);
        if r0 & 0xffff == 0 {
            let addr = r(1) >> 24 | (r(2) & 0xff) << 8 | (r(2) >> 8 & 0xff) << 16 | (r(2) >> 16 & 0xff) << 24;
            self.t.xspi_op = (r(3) >> 16 & 0xff, addr);
            self.t.xspi_status = 0;
            if r(1) & 1 == 0 {
                self.t.xspi_intr |= 1 << 23;
                self.t.xspi_status = 1 << 15;
            }
        } else {
            let size = r(2) >> 16 | (r(3) & 0xffff) << 16;
            let out = r(4) >> 4 & 1;
            let (op, addr) = self.t.xspi_op;
            self.t.xspi_data.clear();
            if out == 0 {
                for i in 0..size.min(0x1000000) {
                    let b = match op {
                        0x9f => self.jedec.get(i as usize).copied().unwrap_or(0),
                        0x03 | 0x0b | 0x6b => self.flash.get((addr + i) as usize).copied().unwrap_or(0xff),
                        _ => 0xff,
                    };
                    self.t.xspi_data.push_back(b);
                }
            }
            self.t.xspi_size = size;
            self.t.xspi_info = out << 8;
            self.t.xspi_intr |= 1 << 21;
            self.t.xspi_more = true;
        }
    }

    fn qspi_start(&mut self, mem: &Mem) {
        let ccr = mem.read32(QSPI + 0x0c);
        let addr = mem.read32(QSPI + 0x18) as usize;
        let n = mem.read32(QSPI + 0x20).wrapping_add(1) as usize;
        let inst = (ccr & 0xff) as u8;
        let data_mode = ccr >> 18 & 3;
        self.qspi_started = true;
        self.qspi_fifo.clear();
        self.qspi_cmd = Some((inst, addr as u32, if data_mode != 0 { n as u32 } else { 0 }));
        match inst {
            0x03 | 0x13 | 0x0b | 0x3b | 0x3c | 0x6b | 0x6c => {
                for i in 0..n {
                    self.qspi_fifo.push_back(self.flash.get(addr.wrapping_add(i)).copied().unwrap_or(0xff));
                }
            }
            0x9f => self.qspi_fifo.extend(self.jedec.iter().take(n)),
            0x05 => self.qspi_fifo.push_back(if self.qspi_wel { 2 } else { 0 }),
            0x06 => self.qspi_wel = true,
            0x20 => {
                let a = addr & !0xfff;
                let flash = std::rc::Rc::make_mut(&mut self.flash);
                for i in a..(a + 0x1000).min(flash.len()) {
                    flash[i] = 0xff;
                }
                self.qspi_wel = false;
            }
            0x02 => self.qspi_wbuf.clear(),
            _ => {}
        }
    }

    /// With the SYSHUB window on the PM (SoC 0x1c90000000), its first MiB is the PM's SRAM, which the
    /// SE also sees at 0x46a10000.
    fn pm_alias(&self, addr: u32) -> Option<u32> {
        (self.window == 0x392 && (0x60000000..0x60108000).contains(&addr)).then(|| addr - 0x60000000 + 0x46a10000)
    }
}

impl SkySoc {
    /// Do `a` at time `t`.
    pub fn at(&mut self, t: u64, a: Action) {
        self.t.sched.entry(t).or_default().push(a);
    }

    fn note(&mut self, io: &Io, what: String) {
        self.t.log.push((io.time, what));
    }

    fn mbox_line(&self, io: &mut Io, mem: &Mem, k: usize, en: Option<(u32, u32)>) {
        let base = MBOX_BASE + 0x10000 * k as u32;
        // (a write being made to an enable register counts with its new value)
        let reg = |off: u32| match en {
            Some((o, v)) if o == off => v,
            _ => mem.read32(base + off),
        };
        io.set_line(mbox_irq(k), self.t.mbox_status[k] & (reg(0xbc) | reg(0xc0)) != 0);
    }

    /// Raise status bits of a mailbox channel (a request from the AP: bit 0 of a register channel).
    pub fn mbox_raise(&mut self, io: &mut Io, mem: &Mem, k: usize, bits: u32) {
        self.t.mbox_status[k] |= bits;
        self.mbox_line(io, mem, k, None);
    }

    fn apply(&mut self, io: &mut Io, mem: &mut Mem, a: Action) {
        match a {
            Action::Line(n, level) => io.set_line(n, level),
            Action::PmMagic(v) => {
                self.pm_magic.push_back(v);
            }
            Action::Mbox(k, bits) => self.mbox_raise(io, mem, k, bits),
            Action::SfhUp => self.t.sfh_word = 0xaabb,
        }
    }

    fn wdt_set(&mut self, io: &Io, mem: &Mem, load: Option<u32>, ctrl: Option<u32>) {
        let ctrl = ctrl.unwrap_or_else(|| mem.read32(WDT + 8));
        let load = load.unwrap_or_else(|| mem.read32(WDT));
        self.t.wdt_at = if ctrl & 1 != 0 { io.time + (load as u64).max(1) * self.t.wdt_tick } else { u64::MAX };
    }

    /// Start the watchdog's count again from `load`.
    pub fn wdt_restart(&mut self, io: &Io, load: u32) {
        self.t.wdt_at = io.time + (load as u64).max(1) * self.t.wdt_tick;
    }

    /// A slot register was written: put that part of the window on the memory it now shows.  DRAM's
    /// first GiB keeps the window's own addresses (as at boot), the PM's SRAM the address the SE has for
    /// it; anything else gets pages of its own.
    fn set_slot(&mut self, mem: &mut Mem, k: usize, v: u32) {
        self.t.slots[k] = v;
        let soc = (v as u64) << 27;
        let lo = WINDOW + k as u32 * SLOT_SIZE;
        let key = if (0x8000_0000..0xc000_0000).contains(&soc) {
            WINDOW as u64 + (soc - 0x8000_0000)
        } else if soc == 0x1c_9000_0000 {
            0x46a10000
        } else {
            SOC_KEY | soc
        };
        mem.set_remap(lo, lo + SLOT_SIZE, key);
    }

    fn timed_read(&mut self, io: &mut Io, mem: &mut Mem, addr: u32, size: u32) -> Option<Option<u32>> {
        let word = mem.read32(addr & !3);
        if self.t.xspi {
            match addr & !3 {
                XSPI_INTR => return Some(Some(self.t.xspi_intr)),
                XSPI_CMD_STATUS => return Some(Some(self.t.xspi_status)),
                XSPI_SDMA_SIZE => return Some(Some(self.t.xspi_size)),
                XSPI_SDMA_INFO => return Some(Some(self.t.xspi_info)),
                XSPI_SDMA_PORT => {
                    let mut v = 0;
                    for i in 0..size {
                        v |= (self.t.xspi_data.pop_front().unwrap_or(0xff) as u32) << (8 * i);
                    }
                    return Some(Some(v));
                }
                _ => {}
            }
        }
        // PLLs
        if (0x46570000..0x46578000).contains(&addr) && addr & 0xff == 0x0c {
            let dead = self.t.dead_plls.contains(&(addr & 0xff00 & 0x7fff));
            return Some(Some(if dead { word & !0x10 } else { word | 0x10 }));
        }
        if addr == 0x46576100 && self.t.dead_plls.contains(&0xffff) {
            return Some(Some(word & !0x40));
        }
        if (MBOX_BASE..MBOX_BASE + 0x10000 * MBOX_CHANNELS as u32).contains(&addr) {
            let k = ((addr - MBOX_BASE) >> 16) as usize;
            return Some(match addr & 0xffff {
                0xc8 => Some(self.t.mbox_status[k]),
                0xc4 => Some(0),
                0xb4 => Some(4),
                _ => None,
            });
        }
        if addr & 0xffffe000 == SE_TIMER && addr & 0xfff == 0x1c {
            // the 64-bit count of the two timers, at the reference clock, from when they were started
            // (modelled, not known: that it counts up from zero)
            let n = match (self.t.timer_on[0], self.t.timer_on[1]) {
                (Some(a), Some(_)) => (io.time - a) / REF_TICK,
                _ => 0,
            };
            return Some(Some(if addr & 0x1000 != 0 { (n >> 32) as u32 } else { n as u32 }));
        }
        match addr {
            0xe000e010 => return Some(None), // SysTick is the core's
            0xb0020004 => {
                let left = if self.t.wdt_at == u64::MAX { mem.read32(WDT) as u64 } else { self.t.wdt_at.saturating_sub(io.time) / self.t.wdt_tick };
                return Some(Some(left as u32));
            }
            0xa003002c => return Some(Some(word | 1)), // the SE's clock is back after a sleep request
            // the audio subsystem's power state [14:12] follows the request in bit 0 (modelled, not known:
            // CIX's self-test asks with 0xfff and waits for state 3)
            0x47000020 if self.t.audio_power => return Some(Some((word & !0x7000) | if word & 1 != 0 { 0x3000 } else { 0 })),
            0xa0010018 => return Some(Some(if self.t.uart_rx.is_empty() { 0x10 } else { 0 })),
            0xa0010000 => return Some(Some(self.t.uart_rx.pop_front().unwrap_or(0) as u32)),
            0x48080100 => return Some(Some(self.t.sfh_word)),
            _ => {}
        }
        if let Some(v) = self.t.s5.get(&addr) {
            return Some(Some(*v));
        }
        let _ = size;
        None
    }

    fn timed_write(&mut self, io: &mut Io, mem: &mut Mem, addr: u32, size: u32, value: u32) {
        let old = mem.read32(addr & !3);
        if self.t.xspi && addr == XSPI {
            self.xspi_start(mem, value);
        }
        if self.t.xspi && addr == XSPI_INTR {
            // written with what was read: the bits are taken back; a data phase is done once its request
            // for the data (SDMA trigger) has been taken
            self.t.xspi_intr &= !value;
            if self.t.xspi_more && value & 1 << 21 != 0 {
                self.t.xspi_more = false;
                self.t.xspi_intr |= 1 << 23;
                self.t.xspi_status = 1 << 15;
            }
        }
        match addr {
            0xa0010000 => {
                if self.t.uart.last().map_or(true, |c| *c == b'\n') {
                    self.t.uart_lines.push((io.time, self.t.uart.len()));
                }
                self.t.uart.push(value as u8);
            }
            0xa0020f00 => self.t.postcodes.push(value),
            0x56000300 if value & 1 != 0 => {
                let why = format!("system reset, reason {:#x}", mem.read32(0x56000500));
                self.note(io, why.clone());
                self.t.reset = Some(why.clone());
                io.stop = Some(why);
            }
            0x56000704 => {
                // the PM released (it was held): it comes up and says so
                if old & 1 != 0 && value & 1 == 0 && self.t.pm_alive {
                    self.at(io.time + self.t.pm_delay, Action::PmMagic(self.t.pm_boot_magic));
                }
            }
            PM_MAGIC => {
                if value == 0x55ca {
                    if self.t.pm_alive {
                        self.at(io.time + self.t.pm_delay, Action::PmMagic(0x5a5c));
                    }
                } else {
                    // the SE's answer takes the PM's word's place
                    self.note(io, format!("SE to PM, handshake: {value:#x}"));
                    self.pm_magic.pop_front();
                    if value != 0 {
                        mem.write(PM_MAGIC, 4, value);
                    }
                }
            }
            0x56000308 => {
                // the SFH's core out of reset: its firmware starts
                if old & 1 == 0 && value & 1 != 0 {
                    self.note(io, "SFH core released".into());
                    if self.t.sfh_alive {
                        self.at(io.time + self.t.sfh_delay, Action::SfhUp);
                    }
                }
            }
            0x48080100 => self.t.sfh_word = value,
            0x4661008c => {
                self.note(io, format!("SE to PM: {value:#x}"));
                if value == 1 {
                    if let Some(m) = self.t.pm_resume_magic {
                        self.at(io.time + self.t.pm_delay, Action::PmMagic(m));
                    }
                }
            }
            // the wake-up interrupt goes when the SE takes back its sleep request (modelled, not known: which
            // of the handler's two writes does it)
            0x5600021c if value == 0 => io.set_line(IRQ_SE_INTR, false),
            _ => {}
        }
        if (SLOT_REG..SLOT_REG + 32).contains(&addr) && size == 4 {
            self.set_slot(mem, ((addr - SLOT_REG) / 4) as usize, value);
        }
        if addr & 0xfffff000 == WDT {
            match addr & 0xfff {
                0 if self.t.wdt_stuck => {}
                0 => self.wdt_set(io, mem, Some(value), None),
                8 => {
                    if (old ^ value) & 1 != 0 {
                        self.wdt_set(io, mem, None, Some(value));
                    }
                }
                0xc => {
                    self.t.wdt_irq = false;
                    io.set_line(IRQ_WDT, false);
                }
                _ => {}
            }
        }
        if addr & 0xffffe000 == SE_TIMER && addr & 0xfff == 0x24 {
            let n = (addr >> 12 & 1) as usize;
            if value & 1 << 17 != 0 {
                self.t.timer_on[n].get_or_insert(io.time);
            } else {
                self.t.timer_on[n] = None;
            }
        }
        if (MBOX_BASE..MBOX_BASE + 0x10000 * MBOX_CHANNELS as u32).contains(&addr) {
            let k = ((addr - MBOX_BASE) >> 16) as usize;
            match addr & 0xffff {
                0xc4 => {
                    self.t.mbox_status[k] &= !value;
                    self.mbox_line(io, mem, k, None);
                }
                0xbc => {
                    // the FIFO's contents are handed to the peer: it takes them, the channel says done
                    if value & 8 != 0 && old & 8 == 0 {
                        let words = std::mem::take(&mut self.t.mbox_fifo[k]);
                        self.note(io, format!("mailbox {k}: sent {words:x?}"));
                        self.t.sent.push((k, words));
                        if self.t.ap_reads {
                            self.at(io.time + self.t.ap_delay, Action::Mbox(k, 8));
                        }
                    }
                    self.mbox_line(io, mem, k, Some((0xbc, value)));
                }
                0xc0 => self.mbox_line(io, mem, k, Some((0xc0, value))),
                0xac => self.t.mbox_fifo[k].push(value),
                0xcc => self.t.mbox_fifo[k].clear(),
                0x80 if value & 2 != 0 => self.t.mbox_acked[k] = true,
                _ => {}
            }
        }
    }

    fn timed_event(&mut self, io: &mut Io, mem: &mut Mem) {
        while let Some((&t, _)) = self.t.sched.iter().next() {
            if t > io.time {
                break;
            }
            for a in self.t.sched.remove(&t).unwrap() {
                self.apply(io, mem, a);
            }
        }
        if io.time >= self.t.wdt_at {
            let ctrl = mem.read32(WDT + 8);
            if !self.t.wdt_irq {
                // first time out: the interrupt, and the count starts again
                self.t.wdt_irq = true;
                io.set_line(IRQ_WDT, true);
                self.note(io, "SE watchdog: interrupt".into());
                self.wdt_set(io, mem, None, None);
            } else if ctrl & 2 != 0 {
                self.note(io, "SE watchdog: reset".into());
                self.t.reset = Some("SE watchdog reset".into());
                io.stop = Some("SE watchdog reset".into());
                self.t.wdt_at = u64::MAX;
            } else {
                self.wdt_set(io, mem, None, None);
            }
        }
    }
}

impl Bus for SkySoc {
    fn next_event(&mut self, _io: &Io) -> Option<u64> {
        if !self.t.on {
            return None;
        }
        let s = self.t.sched.keys().next().copied().unwrap_or(u64::MAX);
        let t = s.min(self.t.wdt_at);
        (t != u64::MAX).then_some(t)
    }

    fn event(&mut self, io: &mut Io, mem: &mut Mem) {
        self.timed_event(io, mem);
    }

    fn read(&mut self, io: &mut Io, mem: &mut Mem, addr: u32, size: u32) -> Option<u32> {
        if self.t.on {
            if let Some(v) = self.timed_read(io, mem, addr, size) {
                return v;
            }
        } else if let Some(alias) = self.pm_alias(addr) {
            return Some(mem.peek(alias, size));
        }
        if let Some(ddr) = &mut self.ddr {
            if let Some(v) = ddr.read(mem, addr, size) {
                return Some(v);
            }
        }
        let word = mem.read32(addr & !3);
        if (QSPI..QSPI + 0x100).contains(&addr) {
            return match addr - QSPI {
                0x04 => {
                    let mut sr = word & !0xf014;
                    if self.qspi_started {
                        sr |= 4;
                        sr |= if self.qspi_fifo.is_empty() { 0x10 } else { 1 << 12 };
                    }
                    Some(sr)
                }
                0x1c => {
                    let k = match mem.read32(QSPI + 0x0c) >> 20 & 3 {
                        3 => 4,
                        1 => 2,
                        _ => 1,
                    };
                    let mut v = 0u32;
                    for i in 0..k {
                        v |= (self.qspi_fifo.pop_front().unwrap_or(0) as u32) << (8 * i);
                    }
                    Some(v)
                }
                _ => None,
            };
        }
        if (0x46570000..0x46578000).contains(&addr) && addr & 0xff == 0x0c {
            return Some(word | 0x10); // PLL locked
        }
        match addr {
            0x46576100 => return Some(word | 0x40),     // PCIe PHY PLL locked
            0xe000e010 => return Some(word | 0x10000),  // SysTick COUNTFLAG
            0xe000ed90 => return Some(16 << 8),         // MPU regions
            0xa0010018 => return Some(0x10),            // UART: RX empty, TX free
            0xa0020018 => return Some(0),
            0x56000218 => return Some(self.boot_reason),
            LIFE_CYCLE => return Some(self.life_cycle),
            // crypto engine
            0xa0040008 => return Some(0xfffff),
            0xa0040014 => return Some(1 << 24),
            0xa0043398 => return Some(1),
            0xa0043304 => return Some(0),
            0xa0043308 => return Some(1),
            _ => {}
        }
        if (0xa0043310..0xa0043330).contains(&addr) {
            self.rng = self.rng.wrapping_mul(1103515245).wrapping_add(12345);
            return Some(self.rng);
        }
        // power controllers follow their requests
        if addr == 0x4f030008 || ((0x4f080008..0x50000000).contains(&addr) && (addr - 0x4f080008) % 0x100000 == 0) {
            return Some(mem.read32(addr - 8));
        }
        // CI-700: SLC flush and MTE tag flush done
        if (0x50000000..0x54000000).contains(&addr) && matches!(addr & 0xffff, 0x0f68 | 0x0a38) {
            return Some(word | 1);
        }
        // SRAM repair controllers: chunk taken, data received, repaired
        if MEMR_BASES.contains(&addr.wrapping_sub(0x40000010)) {
            return Some((word & !0xf0) | 0xe);
        }
        if addr == 0x46610090 {
            return Some(self.pm_magic.front().copied().unwrap_or(word));
        }
        if addr == 0xa00500c8 || addr == 0xa00700c8 {
            return Some((self.mbox_pending == Some(addr - 0xc8)) as u32);
        }
        None
    }

    fn write(&mut self, io: &mut Io, mem: &mut Mem, addr: u32, size: u32, value: u32) {
        if self.t.on {
            self.timed_write(io, mem, addr, size, value);
        } else if let Some(alias) = self.pm_alias(addr) {
            mem.write(alias, size, value);
        }
        if let Some(ddr) = &mut self.ddr {
            ddr.write(io, mem, addr, size, value);
        }
        if addr == 0xb0030100 {
            self.window = value;
        }
        if addr == QSPI && value & 2 != 0 {
            self.qspi_start(mem);
        } else if addr == QSPI + 0x1c && matches!(self.qspi_cmd, Some((0x02, _, _))) {
            self.qspi_wbuf.push(value as u8);
            let (_, a, n) = self.qspi_cmd.unwrap();
            if self.qspi_wbuf.len() == n as usize {
                let flash = std::rc::Rc::make_mut(&mut self.flash);
                for (i, b) in self.qspi_wbuf.iter().enumerate() {
                    if let Some(f) = flash.get_mut(a as usize + i) {
                        *f &= *b;
                    }
                }
                self.qspi_wel = false;
            }
        } else if self.t.on {
            // (the PM's handshakes are in timed_write)
        } else if addr == 0x56000704 && value & 1 == 0 {
            self.pm_magic.push_back(0x5c5a); // PM released: it comes up
        } else if addr == 0x46610090 {
            if value == 0x55ca {
                self.pm_magic.push_back(0x5a5c);
            } else if value == 0 && !self.pm_magic.is_empty() {
                self.pm_magic.pop_front();
            }
        }
        if self.stop_write == Some((addr, value)) {
            io.stop = Some(format!("write {value:#x} to {addr:#x}"));
        }
    }
}

pub const DDRC: u32 = 0x4c010000; // channel ch at DDRC + ch * DDRC_STRIDE (SMN 0x0c010000)
pub const DDRC_STRIDE: u32 = 0x20000;
const DDR_FANOUT: u32 = 0x46510600; // PM public DDR fan-out registers, one per channel
pub const DDR_IRQ: [u32; 4] = [219, 221, 223, 225]; // CIX's ddrc_irq_type
const DDR_RESET: u32 = 0x56000400; // SoC reset release; bit 0: the DDR subsystem (MemDdrcReset)

const STATUS: u32 = 0x57c;
const ACK: u32 = 0x5a0;
const MASK: u32 = 0x5c4;
const GROUPS: usize = 9;
/// bits of a fan-out register the SE drives
const FANOUT_SE: u32 = 0x1 | 0xc | 0x3f0 | 0x400 | 0x4000 | 0x80000000;

/// The four LPDDR5 channels (Cadence Denali controller at +0, PI at
/// +0x2000, PHY at +0x4000 of each channel) and the PM's DDR fan-out
/// registers, as far as CIX's DDR driver waits for them.  Registers are
/// memory apart from the status bits below; what they do is read from what
/// CIX's driver writes, polls and acknowledges (and the bit names in its
/// DWARF), not from Cadence's documentation.  Modelled, not known:
///
/// * reset (DDR_RESET bit 0 from 1 to 0, CIX's MemDdrcReset): every register of
///   the four channels reads 0 afterwards and the state below starts over.
/// * interrupt groups: status at 0x57c + 4g, acknowledge at 0x5a0 + 4g
///   (write 1 to clear), mask at 0x5c4 + 4g (CIX's DdrcIrqTlb gives status
///   and acknowledge; the mask offset follows from enable_ddr_init_monitor
///   unmasking INIT_COMP at 0x5e4).  The channel's interrupt (DDR_IRQ) is
///   up while a status bit is set and not masked; the master mask (0x578)
///   is not modelled.
/// * START of the controller (CTL_0 bit 0 going from 0 to 1): the PI trains at frequency 0,
///   then asks the PM (fan-out register: freq_change_req, type = frequency
///   index) for each further frequency in PI_FREQ_MAP (PI_13) in rising
///   order, and for PI_INIT_WORK_FREQ (PI_14) last if it is not the last
///   one trained; each request is dropped when the SE sets
///   freq_change_ack and the next comes when it clears it.  Then INIT_COMP
///   (0x59c bit 9) and PI_INT_STATUS bit 0 (PI init done) are set.  No
///   training step fails (0x598 DFI errors stay clear) and PI training
///   status bits other than bit 0 stay clear.  Without the PI started
///   (CIX's boot environment 0 does not write PI registers) INIT_COMP comes
///   at once.
/// * mode registers: a write (CTL_205 WRITE_MODEREG, bit 25) stores
///   CTL_217's MRSINGLE_DATA for the rank (bit 24: all ranks) and sets
///   MRW_DONE (0x59c bit 19); a read (CTL_206 READ_MODEREG, bit 24) puts
///   the value in PERIPHERAL_MRR_DATA (CTL_207) for every device (16 bits
///   each) and sets MRR_DONE (bit 18).  Registers never written read as
///   `mr_defaults` (manufacturer, revision, density: a 16 Gb x16 LPDDR5
///   device by default).
/// * low-power commands (CTL_175 LP_CMD): CTL_190 LP_STATE reads
///   0x40 | command (bit 6 = valid).  Only the status bits the driver
///   polls are ever set (else a status nobody acknowledges would keep the
///   interrupt up: the group masks' reset values are not known).
/// * BIST: the controller's (0x3cc bit 0) sets BIST_DONE (0x598 bit 0) and
///   reads back passed (0x3cc bit 8); data initialisation (0x40c bit 8)
///   reads back done (0x40c bit 0); the PI's (PI_324, 0x2510 bit 0) sets
///   PI_INT_STATUS bit 16 and passed (0x2510 bit 8).
/// * fan-out DFS handshake: lp_ext_ack follows lp_ext_req; the command
///   strobe makes the controller ask for a clock change
///   (cntrl_freq_change_req) until the SE acknowledges it
///   (cntrl_freq_change_ack), after which lp_ext_done reads set until
///   lp_ext_req is cleared.
#[derive(Clone)]
pub struct DdrModel {
    pub resets: u32,
    pub status: [[u32; GROUPS]; 4],
    pi_status: [u32; 4],
    started: [bool; 4],
    /// frequencies the PI still asks for
    freq_req: [VecDeque<u32>; 4],
    /// request shown in the fan-out register
    req_up: [bool; 4],
    req_acked: [bool; 4],
    lp_state: [u32; 4],
    bist_pass: [bool; 4],
    pi_bist_pass: [bool; 4],
    data_init: [bool; 4],
    /// (rank, mode register) -> value
    mr: [BTreeMap<(u32, u32), u32>; 4],
    dfs_creq: [bool; 4],
    dfs_done: [bool; 4],
    /// (channel, event), for reports
    pub log: Vec<(usize, String)>,
    pub mr_defaults: BTreeMap<u32, u32>,
    /// what goes wrong (kept over a reset of the subsystem)
    pub fault: DdrFault,
}

/// Failures of the DDR subsystem a test can ask for.
#[derive(Clone, Default)]
pub struct DdrFault {
    /// channels whose initialisation never completes (bit per channel)
    pub init_never: u32,
    /// status bits of group 7 (0x598: BIST and DFI errors) set when initialisation completes
    pub init_status: u32,
    /// PI interrupt status bits set with "PI init done"
    pub pi_status: u32,
    /// the controller's BIST does not read back as passed
    pub bist_fail: bool,
    /// the PI's BIST does not read back as passed
    pub pi_bist_fail: bool,
}

impl DdrModel {
    pub fn new() -> DdrModel {
        DdrModel {
            resets: 0,
            status: [[0; GROUPS]; 4],
            pi_status: [0; 4],
            started: [false; 4],
            freq_req: Default::default(),
            req_up: [false; 4],
            req_acked: [false; 4],
            lp_state: [0; 4],
            bist_pass: [false; 4],
            pi_bist_pass: [false; 4],
            data_init: [false; 4],
            mr: Default::default(),
            dfs_creq: [false; 4],
            dfs_done: [false; 4],
            log: Vec::new(),
            mr_defaults: BTreeMap::from([(5, 0x01), (6, 0x00), (7, 0x00), (8, 0x18)]),
            fault: DdrFault::default(),
        }
    }

    /// DDR subsystem reset: the channels' state, and their registers read as zero (their reset values
    /// are not known).
    fn reset(&mut self, io: &mut Io, mem: &mut Mem) {
        for ch in 0..4 {
            mem.zero_page(DDRC + ch as u32 * DDRC_STRIDE);
            io.set_line(DDR_IRQ[ch], false);
        }
        let (resets, defaults, fault) = (self.resets, std::mem::take(&mut self.mr_defaults), std::mem::take(&mut self.fault));
        *self = DdrModel::new();
        self.resets = resets;
        self.mr_defaults = defaults;
        self.fault = fault;
    }

    /// The channel and the offset in it, if `addr` is a controller, PI or PHY register.
    pub fn channel(addr: u32) -> Option<(usize, u32)> {
        if (DDRC..DDRC + 4 * DDRC_STRIDE).contains(&addr) && (addr - DDRC) % DDRC_STRIDE < 0x10000 {
            Some((((addr - DDRC) / DDRC_STRIDE) as usize, (addr - DDRC) % DDRC_STRIDE))
        } else {
            None
        }
    }

    fn update_line(&self, io: &mut Io, mem: &Mem, ch: usize) {
        let base = DDRC + ch as u32 * DDRC_STRIDE;
        let up = (0..GROUPS).any(|g| self.status[ch][g] & !mem.read32(base + MASK + 4 * g as u32) != 0);
        io.set_line(DDR_IRQ[ch], up);
    }

    /// Status bits without the interrupt line (for a test that raises the interrupt once itself).
    pub fn set_status_quiet(&mut self, ch: usize, off: u32, bits: u32) {
        self.status[ch][((off - STATUS) / 4) as usize] |= bits;
    }

    pub fn set_status(&mut self, io: &mut Io, mem: &Mem, ch: usize, off: u32, bits: u32) {
        self.status[ch][((off - STATUS) / 4) as usize] |= bits;
        self.update_line(io, mem, ch);
    }

    fn read(&mut self, mem: &Mem, addr: u32, size: u32) -> Option<u32> {
        if (DDR_FANOUT..DDR_FANOUT + 16).contains(&addr) && size == 4 {
            let ch = ((addr - DDR_FANOUT) / 4) as usize;
            let mut v = mem.read32(addr & !3) & FANOUT_SE;
            if v & 1 != 0 {
                v |= 2; // lp_ext_ack
            }
            if self.dfs_creq[ch] {
                v |= 1 << 30;
            }
            if self.dfs_done[ch] {
                v |= 1 << 11;
            }
            if self.req_up[ch] {
                if let Some(f) = self.freq_req[ch].front() {
                    v |= 1 << 20 | (f & 0x1f) << 15;
                }
            }
            return Some(v);
        }
        let (ch, off) = Self::channel(addr)?;
        if size != 4 {
            return None;
        }
        let word = mem.read32(addr & !3);
        if (STATUS..STATUS + 4 * GROUPS as u32).contains(&off) {
            return Some(self.status[ch][((off - STATUS) / 4) as usize]);
        }
        match off {
            0x2f8 => Some((word & !0x7f00) | self.lp_state[ch] << 8), // CTL_190 LP_STATE
            0x3cc => Some((word & !0x100) | if self.bist_pass[ch] && !self.fault.bist_fail { 0x100 } else { 0 }),
            0x40c => Some((word & !1) | self.data_init[ch] as u32),
            0x24a0 => Some(self.pi_status[ch]), // PI_INT_STATUS (PI_296)
            0x2510 => Some((word & !0x100) | if self.pi_bist_pass[ch] && !self.fault.pi_bist_fail { 0x100 } else { 0 }),
            _ => None,
        }
    }

    fn write(&mut self, io: &mut Io, mem: &mut Mem, addr: u32, size: u32, value: u32) {
        if (DDR_FANOUT..DDR_FANOUT + 16).contains(&addr) && size == 4 {
            self.fanout_write(io, mem, ((addr - DDR_FANOUT) / 4) as usize, value);
            return;
        }
        if addr == DDR_RESET && mem.read32(addr) & 1 != 0 && value & 1 == 0 {
            // DDRC put in reset
            self.resets += 1;
            self.reset(io, mem);
            return;
        }
        let Some((ch, off)) = Self::channel(addr) else { return };
        if size != 4 {
            return;
        }
        let base = DDRC + ch as u32 * DDRC_STRIDE;
        if (ACK..ACK + 4 * GROUPS as u32).contains(&off) {
            self.status[ch][((off - ACK) / 4) as usize] &= !value;
            self.update_line(io, mem, ch);
        } else if (MASK..MASK + 4 * GROUPS as u32).contains(&off) {
            self.update_line(io, mem, ch);
        } else if off == 0 && value & 1 != 0 && mem.read32(addr) & 1 == 0 {
            // START, rising
            self.start(io, mem, ch, base);
        } else if off == 0x334 && value & 1 << 25 != 0 {
            // CTL_205 WRITE_MODEREG
            let (mr, data) = (value & 0xff, mem.read32(base + 0x364) >> 16 & 0xff);
            if value & 1 << 24 != 0 {
                self.mr[ch].insert((0, mr), data);
                self.mr[ch].insert((1, mr), data);
            } else {
                self.mr[ch].insert((value >> 8 & 0xff, mr), data);
            }
            self.set_status(io, mem, ch, 0x59c, 1 << 19); // MRW_DONE
        } else if off == 0x338 && value & 1 << 24 != 0 {
            // CTL_206 READ_MODEREG
            let (mr, rank) = (value >> 8 & 0xff, value >> 16 & 0xff);
            let data =
                self.mr[ch].get(&(rank, mr)).copied().unwrap_or_else(|| self.mr_defaults.get(&mr).copied().unwrap_or(0));
            mem.write(base + 0x33c, 4, data | data << 16);
            self.set_status(io, mem, ch, 0x59c, 1 << 18); // MRR_DONE
        } else if off == 0x2bc {
            // CTL_175 LP_CMD
            self.lp_state[ch] = 0x40 | (value >> 16 & 0x3f);
        } else if off == 0x3cc {
            if value & 1 != 0 && !self.bist_pass[ch] {
                self.bist_pass[ch] = true;
                self.set_status(io, mem, ch, 0x598, 1); // BIST_DONE
            } else if value & 1 == 0 {
                self.bist_pass[ch] = false;
            }
        } else if off == 0x40c {
            self.data_init[ch] = value & 0x100 != 0;
        } else if off == 0x2510 {
            if value & 1 != 0 && !self.pi_bist_pass[ch] {
                self.pi_bist_pass[ch] = true;
                self.pi_status[ch] |= 1 << 16;
            } else if value & 1 == 0 {
                self.pi_bist_pass[ch] = false;
            }
        } else if off == 0x24a8 {
            // PI_INT_ACK
            self.pi_status[ch] &= !value;
        }
    }

    fn start(&mut self, io: &mut Io, mem: &Mem, ch: usize, base: u32) {
        self.started[ch] = true;
        if mem.read32(base + 0x2000) & 1 == 0 {
            // PI not started
            self.log.push((ch, "init".into()));
            self.init_done(io, mem, ch);
            return;
        }
        let (fmap, work) = (mem.read32(base + 0x2034), mem.read32(base + 0x2038) & 0x1f);
        let mut freqs: VecDeque<u32> = (1..32).filter(|f| fmap >> f & 1 != 0).collect();
        if work != freqs.back().copied().unwrap_or(0) {
            freqs.push_back(work);
        }
        self.log.push((ch, format!("train, frequency changes {freqs:?}")));
        let any = !freqs.is_empty();
        self.freq_req[ch] = freqs;
        if any {
            self.req_up[ch] = true;
        } else {
            self.init_done(io, mem, ch);
        }
    }

    fn init_done(&mut self, io: &mut Io, mem: &Mem, ch: usize) {
        if self.fault.init_never >> ch & 1 != 0 {
            return;
        }
        self.pi_status[ch] |= 1 | self.fault.pi_status;
        if self.fault.init_status != 0 {
            self.set_status(io, mem, ch, 0x598, self.fault.init_status);
        }
        self.set_status(io, mem, ch, 0x59c, 1 << 9); // INIT_COMP
    }

    fn fanout_write(&mut self, io: &mut Io, mem: &Mem, ch: usize, value: u32) {
        if value & 0x400 != 0 && value & 1 != 0 {
            self.dfs_creq[ch] = true; // command strobe: clock change wanted
        }
        if value & 1 << 31 != 0 && self.dfs_creq[ch] {
            self.dfs_creq[ch] = false;
            self.dfs_done[ch] = true;
        }
        if value & 1 == 0 {
            self.dfs_done[ch] = false;
        }
        if !self.freq_req[ch].is_empty() {
            if value & 1 << 14 != 0 && self.req_up[ch] {
                self.req_up[ch] = false;
                self.req_acked[ch] = true;
            } else if value & 1 << 14 == 0 && self.req_acked[ch] {
                self.req_acked[ch] = false;
                self.freq_req[ch].pop_front();
                if !self.freq_req[ch].is_empty() {
                    self.req_up[ch] = true;
                } else {
                    self.init_done(io, mem, ch);
                }
            }
        }
    }
}
