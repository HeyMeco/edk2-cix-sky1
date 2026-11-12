// SPDX-License-Identifier: BSD-2-Clause-Patent
//! The ITE IT5570 around the 8051, as the O6 EC firmware uses it.
//!
//! Port of scripts/open/ec_emu.py's `EC`, `SmbusMaster`, `SmbusSlave`
//! and `SpiNor`, behaviour for behaviour, so that both produce the same
//! events from the same image:
//!   * 8051 timers 0/1/2, idle (PCON.0) skipping to the next event
//!   * the interrupt controller (ISR/IER/IVECT -> 8051 INT1), the wake-up
//!     controller (WUEMR/WUESR, edges from the board's pins)
//!   * GPIO data/control/mirror registers, with board-level pin events
//!   * UART1 (16550 at 0x2700) captured as text
//!   * SMBus host channels A-F (devices answer whole transfers), the SMBus
//!     slave on A and B driven by a bus master (the SoC's host commands)
//!   * SMFI follow mode to the shared SPI NOR and to the internal e-flash
//!     (erase/program with busy times, scratch ROM, the watchdog's mirror)
//!   * an event queue for board stimuli (power button, power goods, ...)
//! What the firmware does is recorded as `events`: (ms, kind, detail).
//! Registers not modelled read back what was written.
//!
//! Time is the 8051's machine cycles (768 per ms: 9.2 MHz / 12); the
//! firmware never changes its clock here, so simulated time and the cycle
//! count are the same thing.

use super::board::Board;
use super::cpu::{Bus, Cpu, Halt, CYC};
use std::cmp::Ordering;
use std::collections::BinaryHeap;

pub const MC_PER_MS: u64 = 768;

/// A scheduled action: a board stimulus, a bus byte, a reset.
pub type Act = Box<dyn FnOnce(&mut Ec)>;

struct Item {
    t: u64,
    seq: u64,
    act: Act,
}

impl PartialEq for Item {
    fn eq(&self, o: &Self) -> bool {
        (self.t, self.seq) == (o.t, o.seq)
    }
}
impl Eq for Item {}
impl PartialOrd for Item {
    fn partial_cmp(&self, o: &Self) -> Option<Ordering> {
        Some(self.cmp(o))
    }
}
impl Ord for Item {
    fn cmp(&self, o: &Self) -> Ordering {
        // a min-heap on (time, order of scheduling)
        (o.t, o.seq).cmp(&(self.t, self.seq))
    }
}

/// Python's round(x, 3) (correctly rounded; t/768 is never a tie).
pub fn round3(x: f64) -> f64 {
    format!("{x:.3}").parse().unwrap()
}

/// One thing the firmware did: (ms rounded to µs, kind, detail).
#[derive(Clone, Debug, PartialEq)]
pub struct Event {
    pub ms: f64,
    pub kind: &'static str,
    pub detail: String,
}

/// Pins: port index (A..J = 0..9, M = 10) * 8 + bit.
pub type Pin = u8;
pub const PORTS: &[u8] = b"ABCDEFGHIJM";
pub const NPINS: usize = 88;

pub fn pin_id(name: &str) -> Pin {
    let b = name.as_bytes();
    let p = PORTS.iter().position(|&c| c == b[0]).expect("port") as u8;
    p * 8 + (b[1] - b'0')
}

pub fn pin_name(p: Pin) -> String {
    format!("{}{}", PORTS[(p / 8) as usize] as char, p % 8)
}

pub fn gpdr(port: usize) -> u16 {
    if port == 10 {
        0x160D
    } else {
        0x1601 + port as u16
    }
}

pub fn gpcr(port: usize) -> u16 {
    if port == 10 {
        0x16A0
    } else {
        0x1610 + 8 * port as u16
    }
}

fn port_of_gpdr(a: u16) -> Option<usize> {
    match a {
        0x1601..=0x160A => Some((a - 0x1601) as usize),
        0x160D => Some(10),
        _ => None,
    }
}

fn port_of_gpdmr(a: u16) -> Option<usize> {
    match a {
        0x1661..=0x166A => Some((a - 0x1661) as usize),
        0x166D => Some(10),
        _ => None,
    }
}

/// pin -> (WU number, INTC number): ITE's numbering as in Zephyr's
/// it8xxx2.dtsi / ite-intc.h (same family as the IT5570); the pins the O6
/// EC enables (D1, H0, H2, F5, H4, H6, A6, A7, B3, B5, J3, ...) match.
pub const PIN_WU: &[(&str, u8, u8)] = &[
    ("A0", 91, 96), ("A1", 92, 97), ("A2", 93, 98), ("A3", 80, 88), ("A4", 81, 89), ("A5", 82, 90),
    ("A6", 83, 91), ("A7", 100, 105), ("B0", 101, 106), ("B1", 102, 107), ("B2", 84, 92), ("B3", 103, 108),
    ("B4", 94, 99), ("B5", 104, 109), ("B6", 105, 110), ("B7", 106, 111), ("C0", 85, 93), ("C1", 107, 112),
    ("C2", 95, 100), ("C3", 108, 113), ("C4", 22, 21), ("C5", 109, 114), ("C6", 23, 6), ("C7", 86, 94),
    ("D0", 20, 1), ("D1", 21, 31), ("D2", 24, 17), ("D3", 110, 115), ("D4", 111, 116), ("D5", 112, 117),
    ("D6", 113, 118), ("D7", 87, 95), ("E0", 70, 72), ("E1", 71, 73), ("E2", 72, 74), ("E3", 73, 75),
    ("E4", 114, 119), ("E5", 40, 176), ("E6", 45, 177), ("E7", 46, 178), ("F0", 96, 101), ("F1", 97, 102),
    ("F2", 98, 103), ("F3", 99, 104), ("F4", 64, 52), ("F5", 65, 53), ("F6", 66, 54), ("F7", 67, 55),
    ("G0", 115, 120), ("G1", 116, 121), ("G2", 117, 122), ("G3", 123, 144), ("G4", 124, 145), ("G5", 125, 146),
    ("G6", 118, 123), ("G7", 126, 147), ("H0", 60, 48), ("H1", 61, 49), ("H2", 62, 50), ("H3", 63, 51),
    ("H4", 88, 85), ("H5", 89, 86), ("H6", 90, 87), ("I0", 119, 124), ("I1", 120, 125), ("I2", 121, 126),
    ("I3", 122, 127), ("I4", 74, 76), ("I5", 75, 77), ("I6", 76, 78), ("I7", 77, 79), ("J0", 128, 128),
    ("J1", 129, 129), ("J2", 130, 130), ("J3", 131, 131), ("J4", 132, 132), ("J5", 133, 133), ("J6", 134, 134),
    ("J7", 135, 135),
];

/// (WUEMR, WUESR) addresses and bit of wake-up input n
pub fn wu_reg(n: u8) -> (u16, u16, u8) {
    let (g, b) = if n < 80 { (n / 10, n % 10) } else { (8 + (n - 80) / 8, (n - 80) % 8) };
    let g = g as u16;
    if g <= 4 {
        return (0x1B00 + g - 1, 0x1B04 + g - 1, b);
    }
    if g == 5 {
        return (0x1B0C, 0x1B0D, b);
    }
    (0x1B10 + 4 * (g - 6), 0x1B11 + 4 * (g - 6), b)
}

pub fn isr_addr(g: u16) -> u16 {
    if g < 4 {
        0x1100 + g
    } else {
        0x1114 + 4 * (g - 4)
    }
}

pub fn ier_addr(g: u16) -> u16 {
    if g < 4 {
        0x1104 + g
    } else {
        0x1115 + 4 * (g - 4)
    }
}

// ---------------------------------------------------------------- SPI NOR
/// The shared SPI NOR, as the EC sees it in SMFI follow mode (ECINDAR
/// 0xFFFFFExx: 0xFE00 ends a transfer, 0xFD00 shifts a byte); also the
/// IT5570E's internal e-flash (the running image).
pub struct SpiNor {
    pub name: &'static str,
    pub data: Vec<u8>,
    cmd: Vec<u8>,
    /// None: RDSR, read live
    out: Option<std::collections::VecDeque<u8>>,
    pub status: u8,
    pub wel: bool,
    /// write in progress until (cycles)
    pub busy_until: u64,
    /// a fault to inject: erase/program commands are taken (busy) but
    /// change nothing (the gap scenarios)
    pub fail_writes: bool,
}

pub const JEDEC: [u8; 3] = [0xC8, 0x40, 0x17];
const PROG_MS: f64 = 0.03;

fn erase_ms(op: u8) -> u64 {
    match op {
        0x20 => 45,
        0x52 => 120,
        0xD8 => 150,
        _ => 5,
    }
}

impl SpiNor {
    pub fn new(name: &'static str, data: Vec<u8>) -> SpiNor {
        SpiNor {
            name,
            data,
            cmd: Vec::new(),
            out: Some(Default::default()),
            status: 0,
            wel: false,
            busy_until: 0,
            fail_writes: false,
        }
    }

    pub fn busy(&self, t: u64) -> bool {
        t < self.busy_until
    }

    fn addr(&self) -> usize {
        ((self.cmd[1] as usize) << 16) | ((self.cmd[2] as usize) << 8) | self.cmd[3] as usize
    }

    /// A byte written (sel 0xFD) or the chip select released (0xFE).
    /// True if the part became busy.
    fn xfer_write(&mut self, t: u64, sel: u8, v: u8, log: &mut Log) -> bool {
        if sel == 0xFE {
            let mut b = false;
            if !self.cmd.is_empty() {
                b = self.done(t, log);
            }
            self.cmd.clear();
            self.out = Some(Default::default());
            return b;
        }
        self.cmd.push(v);
        self.start();
        false
    }

    fn start(&mut self) {
        let op = self.cmd[0];
        if self.cmd.len() == 1 {
            match op {
                0x9F => self.out = Some(JEDEC.iter().copied().collect()),
                0x05 => self.out = None,
                0x06 => self.wel = true,
                0x04 => self.wel = false,
                _ => {}
            }
        }
        if (op == 0x03 || op == 0x0B) && self.cmd.len() == 4 {
            let a = self.addr();
            let lo = a.min(self.data.len());
            let hi = (a + 0x1000).min(self.data.len());
            let mut o: std::collections::VecDeque<u8> = self.data[lo..hi].iter().copied().collect();
            if op == 0x0B {
                o.push_front(0);
            }
            self.out = Some(o);
        }
    }

    fn xfer_read(&mut self, t: u64) -> u8 {
        match &mut self.out {
            None => self.status | if self.busy(t) { 1 } else { 0 } | if self.wel { 2 } else { 0 },
            Some(o) => o.pop_front().unwrap_or(0xFF),
        }
    }

    fn busy_for(&mut self, t: u64, ms: f64) {
        self.busy_until = t + (ms * MC_PER_MS as f64) as u64;
    }

    fn done(&mut self, t: u64, log: &mut Log) -> bool {
        let c = self.cmd.clone();
        let op = c[0];
        if self.busy(t) && op != 0x05 {
            log.ev(t, self.name, format!("BUSY cmd {}", crate::util::hex(&c[..c.len().min(4)])));
            return false;
        }
        if matches!(op, 0x20 | 0xD8 | 0x52 | 0xD7) && c.len() >= 4 && self.wel {
            let n: usize = match op {
                0x20 => 0x1000,
                0x52 => 0x8000,
                0xD8 => 0x10000,
                _ => 0x400,
            };
            let a = self.addr() & !(n - 1);
            if !self.fail_writes {
                let hi = (a + n).min(self.data.len());
                if a < hi {
                    self.data[a..hi].fill(0xFF);
                }
            }
            log.ev(t, self.name, format!("erase {a:06X}+{n:X}"));
            self.wel = false;
            self.busy_for(t, erase_ms(op) as f64);
            return true;
        } else if op == 0x02 && c.len() >= 4 && self.wel {
            let a = self.addr();
            if !self.fail_writes {
                for (i, &b) in c[4..].iter().enumerate() {
                    if let Some(d) = self.data.get_mut(a + i) {
                        *d &= b;
                    }
                }
            }
            log.ev(t, self.name, format!("program {a:06X} {}", crate::util::hex(&c[4..])));
            self.wel = false;
            self.busy_for(t, PROG_MS * (c.len() as f64 - 4.0).max(1.0));
            return true;
        } else if op == 0x01 && c.len() >= 2 {
            log.ev(t, self.name, format!("cmd {}", crate::util::hex(&c)));
            self.wel = false;
        } else if !matches!(op, 0x05 | 0x06 | 0x04 | 0x9F | 0x03 | 0x0B) {
            log.ev(t, self.name, format!("cmd {}", crate::util::hex(&c)));
        }
        false
    }
}

// ---------------------------------------------------------------- events
#[derive(Default)]
pub struct Log {
    pub events: Vec<Event>,
}

impl Log {
    pub fn ev(&mut self, t: u64, kind: &'static str, detail: String) {
        self.events.push(Event { ms: round3(t as f64 / MC_PER_MS as f64), kind, detail });
    }
}

// ---------------------------------------------------------------- SMBus
/// (HOSTA, HOCTL, HOCMD, TRASLA, D0, D1, HOBDB, SMBPCTL, HOCTL2)
pub const SMB_CH: [(char, [u16; 9]); 6] = [
    ('A', [0x1C00, 0x1C01, 0x1C02, 0x1C03, 0x1C04, 0x1C05, 0x1C06, 0x1C0A, 0x1C10]),
    ('B', [0x1C11, 0x1C12, 0x1C13, 0x1C14, 0x1C15, 0x1C16, 0x1C17, 0x1C1B, 0x1C21]),
    ('C', [0x1C29, 0x1C2A, 0x1C2B, 0x1C2C, 0x1C2D, 0x1C2E, 0x1C2F, 0x1C31, 0x1C32]),
    ('D', [0x1C35, 0x1C36, 0x1C37, 0x1C38, 0x1C39, 0x1C3A, 0x1C3B, 0x1C3D, 0x1C3E]),
    ('E', [0x1CA0, 0x1CA1, 0x1CA2, 0x1CA3, 0x1CA4, 0x1CA6, 0x1CA7, 0x1CA9, 0x1CAA]),
    ('F', [0x1CB0, 0x1CB1, 0x1CB2, 0x1CB3, 0x1CB4, 0x1CB6, 0x1CB7, 0x1CB9, 0x1CBA]),
];
const HOSTA: usize = 0;
const HOCTL: usize = 1;
const HOCMD: usize = 2;
const TRASLA: usize = 3;
const D0: usize = 4;
const D1: usize = 5;
const HOBDB: usize = 6;
const PCTL: usize = 7;
const HOCTL2: usize = 8;

/// A device on one of the EC's SMBus host channels.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Dev {
    Csu,
    Pd(usize),
}

struct Cur {
    prot: u8,
    addr: u8,
    rd: bool,
    wr: Vec<u8>,
    rdbuf: std::collections::VecDeque<u8>,
    n: u8,
    i: u32,
    dev: Option<Dev>,
    got: Vec<u8>,
    lastfed: bool,
}

/// An IT5570 SMBus host channel. SMBus protocols (HOCTL[4:2]) run to
/// completion; block transfers and the I2C-compatible mode (protocol 7)
/// move a byte per BYTE_DONE.
pub struct SmbusMaster {
    pub name: char,
    r: [u16; 9],
    pub devices: Vec<(u8, Dev)>,
    cur: Option<Cur>,
    /// (gap scenarios) the bus hangs: a transfer started stays busy
    /// (HOSTA bit 0) until the firmware kills it (HOCTL bit 1)
    pub hang: bool,
}

/// (SLDA, SLSTA, INTC number)
pub const SMB_SLAVE: [(char, u16, u16, u8); 2] = [('A', 0x1C09, 0x1C0B, 9), ('B', 0x1C1A, 0x1C1C, 10)];

#[derive(Clone, Copy, PartialEq, Debug)]
enum Step {
    W(u8),
    A,
    R,
    P,
}

/// The IT5570's SMBus slave on channel A or B, driven by a bus master
/// (the SoC). A transfer is a list of steps; each data byte raises the
/// channel's interrupt with SLSTA bit 1 (data) and bit 3 (master reads)
/// set, and the next byte waits until the firmware clears bit 1; a STOP
/// sets bit 5. As the vendor firmware's slave handler expects.
pub struct SmbusSlave {
    pub name: char,
    slda: u16,
    slsta: u16,
    intn: u8,
    steps: std::collections::VecDeque<Step>,
    cur: Option<Step>,
    pub rx: Vec<u8>,
    done: Option<Box<dyn FnOnce(&mut Ec, Vec<u8>)>>,
    wr: Vec<u8>,
    /// (gap scenarios) the master gives up after this many steps: no
    /// STOP, the transfer is cut short
    pub cut_after: Option<usize>,
    /// time per byte, ms
    pub byte_ms: f64,
}

pub const BYTE_MS: f64 = 0.1; // a byte at 100 kHz

// ---------------------------------------------------------------- the chip
pub struct Io {
    pub t: u64,
    q: BinaryHeap<Item>,
    seq: u64,
    pub log: Log,
    uart: Vec<u8>,
    pub uart_lines: Vec<(f64, String)>,
    pub idle: bool,
    pub log_regs: bool,
    /// register writes (cycles, address, value) when log_regs
    pub reglog: Vec<(u64, u16, u8)>,
    /// levels the board drives on EC inputs
    pub pins: [Option<u8>; NPINS],
    pub board: Option<Board>,
    scar: Option<(u32, Vec<u8>)>,
    pub spi: SpiNor,
    pub eflash: SpiNor,
    pub smb: Vec<SmbusMaster>,
    pub slave: Vec<SmbusSlave>,
    /// 0x1C00-0x1CFF: 1-6 a master channel's register, 7-8 a slave's
    smb_addr: Vec<u8>,
    /// the external data space (registers at 0x1000-0x2FFF)
    pub x: Vec<u8>,
    /// the code the CPU runs (the image at the last reset)
    pub code: Vec<u8>,
    /// code fetches go through the scratch ROM / busy check
    watch: bool,
    /// executed instruction starts (physical), when coverage is on
    pub cov: Option<Vec<u8>>,
    /// wake-up inputs (n) a pin raises on both edges (PWRSW)
    pub both_edges: Vec<Pin>,
    /// external RAM addresses whose writes are logged (`watch` events; ec-probe)
    pub watch_x: Vec<u16>,
    /// the extended model (gaps.rs; off: exactly ec_emu.py): the external
    /// watchdog's count, the CSU_PM's real register file
    pub ext: bool,
    /// the external watchdog: running, and the start it belongs to
    wdt_running: bool,
    wdt_gen: u64,
    /// the EC has no power (AC lost): it runs nothing, its pins float
    pub off: bool,
    /// (no board) what ADC channel 0 reads
    pub adc0: Option<u16>,
}

pub struct Ec {
    pub cpu: Cpu,
    pub io: Io,
    pub halted: Option<String>,
    /// the host (SoC) side of the host interface, test.rs
    pub host: super::test::Host,
    /// instructions executed since the last reset
    pub insns: u64,
    /// (ec-probe) when an event whose text contains this is logged, a
    /// `stack` event with the pc, the bank and the stack follows it
    pub stack_on: Option<String>,
    /// (ec-probe) logical pcs at which a `pc` event shows the registers
    pub break_pc: Vec<u16>,
}

pub const EC_SPI_BASE: usize = 0x10000; // the O6 flash layout's ec_addr
pub const SCRATCH: usize = 0x400;

impl Ec {
    pub fn new(image: &[u8], board: bool, log_regs: bool, flash: Option<&[u8]>) -> Ec {
        let mut ef = image.to_vec();
        if ef.len() < 0x20000 {
            ef.resize(0x20000, 0xFF);
        }
        let spi = match flash {
            Some(f) => f.to_vec(),
            None => vec![0; 0x800000],
        };
        let mut smb_addr = vec![0u8; 0x100];
        for (i, (_, r)) in SMB_CH.iter().enumerate() {
            for k in [HOSTA, HOCTL, HOBDB, PCTL] {
                smb_addr[(r[k] - 0x1C00) as usize] = 1 + i as u8;
            }
        }
        for (i, s) in SMB_SLAVE.iter().enumerate() {
            smb_addr[(s.1 - 0x1C00) as usize] = 7 + i as u8;
            smb_addr[(s.2 - 0x1C00) as usize] = 7 + i as u8;
        }
        let io = Io {
            t: 0,
            q: BinaryHeap::new(),
            seq: 0,
            log: Log::default(),
            uart: Vec::new(),
            uart_lines: Vec::new(),
            idle: false,
            log_regs,
            reglog: Vec::new(),
            pins: [None; NPINS],
            board: None,
            scar: None,
            spi: SpiNor::new("spi", spi),
            eflash: SpiNor::new("eflash", ef),
            smb: SMB_CH.iter().map(|&(n, r)| SmbusMaster { name: n, r, devices: Vec::new(), cur: None, hang: false }).collect(),
            slave: SMB_SLAVE
                .iter()
                .map(|&(n, slda, slsta, intn)| SmbusSlave {
                    name: n,
                    slda,
                    slsta,
                    intn,
                    steps: Default::default(),
                    cur: None,
                    rx: Vec::new(),
                    done: None,
                    wr: Vec::new(),
                    cut_after: None,
                    byte_ms: BYTE_MS,
                })
                .collect(),
            smb_addr,
            x: vec![0; 0x10000],
            code: Vec::new(),
            watch: false,
            cov: None,
            both_edges: vec![pin_id("B3")],
            watch_x: Vec::new(),
            ext: false,
            wdt_running: false,
            wdt_gen: 0,
            off: false,
            adc0: None,
        };
        let mut ec = Ec { cpu: Cpu::new(), io, halted: None, host: Default::default(), insns: 0, stack_on: None, break_pc: Vec::new() };
        ec.boot(image, 0x01);
        if board {
            Board::attach(&mut ec.io);
        }
        ec
    }

    /// the chip out of reset, running image (RAM and registers cleared)
    fn boot(&mut self, image: &[u8], rsts: u8) {
        self.cpu = Cpu::new();
        self.insns = 0;
        let mut code = image.to_vec();
        if code.len() < 0x20000 {
            code.resize(0x20000, 0);
        }
        self.io.code = code;
        self.io.x = vec![0; 0x10000];
        self.io.idle = false;
        self.io.scar = None;
        self.io.watch = false;
        self.io.wdt_running = false;
        self.io.wdt_gen += 1;
        let x = &mut self.io.x;
        x[0x2000] = 0x55; // ECHIPID1
        x[0x2001] = 0x70; // ECHIPID2
        x[0x2002] = 0x03; // ECHIPVER
        x[0x2006] = rsts; // RSTS: power-on reset / watchdog
        for p in 0..PORTS.len() {
            for b in 0..8 {
                x[(gpcr(p) + b) as usize] = 0x84;
            }
        }
    }

    /// a wrong key in EWDKEYR resets the chip; with the mirror bits in
    /// SMFI 0x1063 the e-flash is first loaded from the SPI flash's EC
    /// image (how UEFI's 3E19 installs a new EC firmware)
    pub fn watchdog_reset(&mut self) {
        let mirror = self.io.x[0x1063] & 0x18;
        let t = self.io.t;
        if mirror != 0 {
            let img = self.io.spi.data[EC_SPI_BASE..EC_SPI_BASE + 0x20000].to_vec();
            self.io.eflash.data[..0x20000].copy_from_slice(&img);
            let h = crate::util::hex(&crate::util::sha256(&img));
            self.io.log.ev(t, "reset", format!("watchdog, mirrored {}", &h[..12]));
        } else {
            self.io.log.ev(t, "reset", "watchdog".into());
        }
        let before: Vec<[Option<u8>; 8]> = (0..PORTS.len()).map(|p| self.io.snapshot(p)).collect();
        let img = self.io.eflash.data[..0x20000].to_vec();
        self.boot(&img, 0x02);
        for (p, snap) in before.iter().enumerate() {
            for (b, lvl) in snap.iter().enumerate() {
                if lvl.is_some() {
                    let pin = (p * 8 + b) as Pin;
                    self.io.log.ev(t, "out", format!("{} {}=Z", pin_name(pin), super::board::net(pin)));
                }
            }
        }
    }

    pub fn ms(&self) -> f64 {
        self.io.ms()
    }

    pub fn at(&mut self, ms: f64, act: Act) {
        self.io.at(ms, act)
    }

    pub fn after(&mut self, ms: f64, act: Act) {
        self.io.after(ms, act)
    }

    /// Run until `ms` of simulated time (or a halt).
    pub fn run(&mut self, ms: f64) {
        let end = (ms * MC_PER_MS as f64) as u64;
        self.run_until(end);
    }

    pub fn run_until(&mut self, end: u64) {
        if self.halted.is_some() {
            return;
        }
        while self.io.t < end {
            while self.io.q.peek().is_some_and(|i| i.t <= self.io.t) {
                let it = self.io.q.pop().unwrap();
                (it.act)(self);
            }
            if self.io.off {
                // no power: nothing runs until the next board event
                let next = self.io.q.peek().map_or(end, |i| i.t).min(end);
                if next > self.io.t {
                    self.io.t = next;
                }
                continue;
            }
            if self.io.idle {
                self.irqs();
                if self.cpu.pending != 0 && self.cpu.sfr[0xA8] & 0x80 != 0 && self.enabled_pending() {
                    self.io.idle = false;
                } else {
                    let n = self.next_timer();
                    let tq = self.io.q.peek().map(|i| i.t as i64 - self.io.t as i64);
                    let cand = [n.map(|v| v as i64), tq, Some(end as i64 - self.io.t as i64)];
                    let step = cand.iter().flatten().filter(|&&v| v > 0).min().copied().unwrap_or(1) as u64;
                    self.timers(step);
                    self.io.t += step;
                    continue;
                }
            }
            let pc = self.cpu.pc;
            let phys = self.cpu.phys(pc);
            if !self.break_pc.is_empty() && self.break_pc.contains(&pc) {
                let c = &self.cpu;
                let r: Vec<String> = (0..8).map(|i| format!("{:02X}", c.r(i))).collect();
                let s = format!("{pc:04X} bank {} A={:02X} DPTR={:04X} R0-7 {}", c.bank(), c.a(), c.dptr(), r.join(" "));
                self.io.ev("pc", s);
            }
            if let Err(h) = self.io.code(phys) {
                return self.halt(h);
            }
            let op = match self.cpu.step(&mut self.io) {
                Ok(op) => op,
                Err(h) => {
                    self.cpu.pc = pc;
                    return self.halt(h);
                }
            };
            if let Some(c) = &mut self.io.cov {
                c[phys as usize] = 1;
            }
            if self.stack_on.is_some() {
                self.stack_probe(pc);
            }
            self.insns += 1;
            let n = CYC[op as usize] as u64;
            self.timers(n);
            self.io.t += n;
            self.irqs();
            if self.cpu.pending != 0 {
                if let Some(v) = self.cpu.check_irq() {
                    if v == 0x0B {
                        self.cpu.sfr[0x88] &= !0x20;
                    } else if v == 0x1B {
                        self.cpu.sfr[0x88] &= !0x80;
                    }
                }
            }
        }
    }

    /// AC lost: the EC (and the board's rails, the SoC) lose power; the
    /// EC's outputs float.
    pub fn ac_lost(&mut self) {
        let t = self.io.t;
        self.io.log.ev(t, "power", "AC lost".into());
        for p in 0..PORTS.len() {
            for (b, lvl) in self.io.snapshot(p).iter().enumerate() {
                if lvl.is_some() {
                    let pin = (p * 8 + b) as Pin;
                    self.io.log.ev(t, "out", format!("{} {}=Z", pin_name(pin), super::board::net(pin)));
                }
            }
        }
        self.io.off = true;
        self.io.idle = false;
        // nothing is driven: every GPIO an input without pull-up
        for p in 0..PORTS.len() {
            for b in 0..8 {
                self.io.x[(gpcr(p) + b) as usize] = 0x80;
            }
        }
        // the board: rails, power goods and the SoC off
        for (n, l) in [("J3", 0), ("H4", 0), ("H6", 0), ("D1", 0), ("H0", 1), ("B5", 1)] {
            let p = pin_id(n);
            self.io.pins[p as usize] = Some(l);
        }
    }

    /// AC back: the EC starts from its power-on reset with the e-flash's image.
    pub fn ac_restored(&mut self) {
        let t = self.io.t;
        self.io.log.ev(t, "power", "AC restored".into());
        self.io.off = false;
        let img = self.io.eflash.data[..0x20000].to_vec();
        self.boot(&img, 0x01);
    }

    fn stack_probe(&mut self, pc: u16) {
        let pat = self.stack_on.clone().unwrap();
        let n = self.io.log.events.len();
        let hit = self.io.log.events.iter().rev().take(3).any(|e| e.kind != "stack" && (e.kind.to_string() + " " + &e.detail).contains(&pat))
            && self.io.log.events.last().is_some_and(|e| e.kind != "stack");
        if hit && self.io.log.events[n - 1].ms == round3(self.ms()) {
            let sp = self.cpu.sfr[0x81] as usize;
            let st: Vec<String> = (0x08..=sp).map(|i| format!("{:02X}", self.cpu.iram[i])).collect();
            let s = format!("pc {:04X} bank {} sp {:02X}: {}", pc, self.cpu.bank(), sp, st.join(" "));
            self.io.ev("stack", s);
        }
    }

    fn halt(&mut self, h: Halt) {
        let t = self.io.t;
        self.io.log.ev(t, "halt", h.0.clone());
        self.halted = Some(h.0);
    }

    fn enabled_pending(&self) -> bool {
        let ie = self.cpu.sfr[0xA8];
        (0..6).any(|i| self.cpu.pending >> i & 1 != 0 && ie >> i & 1 != 0)
    }

    // ---- timers ----
    fn timers(&mut self, n: u64) {
        let s = &mut self.cpu.sfr;
        let (tcon, tmod) = (s[0x88], s[0x89]);
        for (tr, tf, tl, th, sh) in [(0x10u8, 0x20u8, 0x8Ausize, 0x8Cusize, 0), (0x40, 0x80, 0x8B, 0x8D, 4)] {
            if tcon & tr == 0 {
                continue;
            }
            let mode = (tmod >> sh) & 3;
            if mode == 2 {
                let mut v = s[tl] as u64 + n;
                if v > 0xFF {
                    let p = 0x100 - s[th] as u64;
                    let k = (v - 0xFF + p - 1) / p;
                    v -= k * p;
                    s[0x88] |= tf;
                }
                s[tl] = v as u8;
            } else {
                let mut v = (((s[th] as u64) << 8) | s[tl] as u64) + n;
                if v > 0xFFFF {
                    s[0x88] |= tf;
                    v &= 0xFFFF;
                }
                s[th] = (v >> 8) as u8;
                s[tl] = v as u8;
            }
        }
        if s[0xC8] & 0x04 != 0 {
            // T2CON.TR2, auto-reload
            let mut v = (((s[0xCD] as u64) << 8) | s[0xCC] as u64) + n;
            if v > 0xFFFF {
                let p = 0x10000 - (((s[0xCB] as u64) << 8) | s[0xCA] as u64);
                let k = (v - 0xFFFF + p - 1) / p;
                v -= k * p;
                s[0xC8] |= 0x80;
            }
            s[0xCD] = (v >> 8) as u8;
            s[0xCC] = v as u8;
        }
    }

    /// machine cycles until the next timer overflow (or None)
    fn next_timer(&self) -> Option<u64> {
        let s = &self.cpu.sfr;
        let mut best: Option<u64> = None;
        let (tcon, tmod) = (s[0x88], s[0x89]);
        for (tr, tl, th, sh) in [(0x10u8, 0x8Ausize, 0x8Cusize, 0), (0x40, 0x8B, 0x8D, 4)] {
            if tcon & tr != 0 {
                let mode = (tmod >> sh) & 3;
                let d = if mode == 2 { 0x100 - s[tl] as u64 } else { 0x10000 - (((s[th] as u64) << 8) | s[tl] as u64) };
                best = Some(best.map_or(d, |b| b.min(d)));
            }
        }
        if s[0xC8] & 0x04 != 0 {
            let d = 0x10000 - (((s[0xCD] as u64) << 8) | s[0xCC] as u64);
            best = Some(best.map_or(d, |b| b.min(d)));
        }
        best
    }

    fn irqs(&mut self) {
        let s = &self.cpu.sfr;
        let (tf0, tf1, tf2) = (s[0x88] & 0x20 != 0, s[0x88] & 0x80 != 0, s[0xC8] & 0x80 != 0);
        let c = &mut self.cpu;
        if tf0 {
            c.pending |= 1 << 1;
        }
        if tf1 {
            c.pending |= 1 << 3;
        }
        if tf2 {
            c.pending |= 1 << 5;
        } else {
            c.pending &= !(1 << 5);
        }
        if self.io.int_pending().is_some() {
            self.cpu.pending |= 1 << 2;
        } else {
            self.cpu.pending &= !(1 << 2);
        }
    }

    /// The executed code as ranges of physical addresses (instruction starts and their lengths).
    pub fn cov_ranges(&self) -> Vec<(u32, u32)> {
        let mut r = Vec::new();
        if let Some(c) = &self.io.cov {
            for (a, &v) in c.iter().enumerate() {
                if v != 0 {
                    let ln = super::cpu::op_len(self.io.code[a]);
                    r.push((a as u32, a as u32 + ln));
                }
            }
        }
        crate::cov::normalize(r)
    }
}

impl Io {
    pub fn ms(&self) -> f64 {
        self.t as f64 / MC_PER_MS as f64
    }

    pub fn at(&mut self, ms: f64, act: Act) {
        let t = (ms * MC_PER_MS as f64) as u64;
        self.q.push(Item { t, seq: self.seq, act });
        self.seq += 1;
    }

    pub fn after(&mut self, ms: f64, act: Act) {
        self.at(self.ms() + ms, act)
    }

    pub fn ev(&mut self, kind: &'static str, detail: String) {
        let t = self.t;
        self.log.ev(t, kind, detail);
    }

    // ---- pins ----
    pub fn is_output(&self, p: Pin) -> bool {
        (self.x[(gpcr((p / 8) as usize) + (p % 8) as u16) as usize] & 0xC0) == 0x40
    }

    pub fn out_level(&self, p: Pin) -> u8 {
        (self.x[gpdr((p / 8) as usize) as usize] >> (p % 8)) & 1
    }

    /// level on a pin: the EC's output, or what the board drives
    pub fn pin(&self, p: Pin) -> u8 {
        if self.is_output(p) {
            return self.out_level(p);
        }
        self.pins[p as usize]
            .unwrap_or(if self.x[(gpcr((p / 8) as usize) + (p % 8) as u16) as usize] & 0x04 != 0 { 1 } else { 0 })
    }

    /// board drives an EC input
    pub fn drive(&mut self, p: Pin, level: u8) {
        let old = self.pin(p);
        self.pins[p as usize] = Some(level);
        let new = self.pin(p);
        if old != new {
            self.ev("in", format!("{} {}={}", pin_name(p), super::board::net(p), new));
            self.wuc_edge(p, old, new);
        }
    }

    fn wuc_edge(&mut self, p: Pin, old: u8, new: u8) {
        let name = pin_name(p);
        if let Some(&(_, n, intn)) = PIN_WU.iter().find(|e| e.0 == name) {
            let (emr, esr, b) = wu_reg(n);
            let falling = (self.x[emr as usize] >> b) & 1;
            // PWRSW (GPB3, WU103) interrupts on both edges: the vendor's
            // handler reads the pin for press/release and never re-arms
            // the edge (it does for every other pin), and a short press in
            // S0 does not turn an O6 off, so the release must be seen
            let both = self.both_edges.contains(&p);
            if both || (falling == 1 && old == 1 && new == 0) || (falling == 0 && old == 0 && new == 1) {
                self.x[esr as usize] |= 1 << b;
                self.int_raise(intn);
            }
        }
    }

    fn gpio_read(&self, port: usize) -> u8 {
        let mut v = 0;
        for b in 0..8 {
            v |= self.pin((port * 8 + b) as Pin) << b;
        }
        v
    }

    fn snapshot(&self, port: usize) -> [Option<u8>; 8] {
        let mut s = [None; 8];
        for (b, e) in s.iter_mut().enumerate() {
            let p = (port * 8 + b) as Pin;
            if self.is_output(p) {
                *e = Some(self.out_level(p));
            }
        }
        s
    }

    fn gpio_changed(&mut self, port: usize, before: [Option<u8>; 8]) {
        for (b, old) in before.iter().enumerate() {
            let p = (port * 8 + b) as Pin;
            let new = if self.is_output(p) { Some(self.pin(p)) } else { None };
            if let Some(n) = new {
                if Some(n) != *old {
                    self.ev("out", format!("{} {}={}", pin_name(p), super::board::net(p), n));
                    if self.board.is_some() {
                        self.board_output(p, n);
                    }
                }
            }
        }
    }

    // ---- scratch ROM and the e-flash's busy time ----
    /// SCAR0H written: bit 7 off maps SCRATCH bytes of the e-flash at
    /// SCAR0 into scratch SRAM (copied now)
    fn set_scar(&mut self, v: u8) {
        if v & 0x80 != 0 {
            self.scar = None;
        } else {
            let a = (((v & 3) as u32) << 16) | ((self.x[0x1041] as u32) << 8) | self.x[0x1040] as u32;
            let lo = (a as usize).min(self.code.len());
            let hi = (a as usize + SCRATCH).min(self.code.len());
            self.scar = Some((a, self.code[lo..hi].to_vec()));
        }
        self.fetch_watch();
    }

    fn fetch_watch(&mut self) {
        self.watch = self.scar.is_some() || self.eflash.busy(self.t);
    }

    /// follow mode target: ECINDAR3 bit 6 set = internal e-flash
    fn fm(&mut self) -> &mut SpiNor {
        if self.x[0x103E] & 0x40 != 0 {
            &mut self.eflash
        } else {
            &mut self.spi
        }
    }

    // ---- interrupt controller ----
    pub fn int_raise(&mut self, n: u8) {
        let a = isr_addr((n >> 3) as u16);
        self.x[a as usize] |= 1 << (n & 7);
    }

    pub fn int_pending(&self) -> Option<u16> {
        for g in 0..24u16 {
            let p = self.x[isr_addr(g) as usize] & self.x[ier_addr(g) as usize];
            if p != 0 {
                return Some(g * 8 + p.trailing_zeros() as u16);
            }
        }
        None
    }

    // ---- register access ----
    fn xread(&mut self, a: u16) -> Option<u8> {
        if !(0x1000..0x3000).contains(&a) {
            return None;
        }
        let v = if let Some(p) = port_of_gpdr(a) {
            self.gpio_read(p)
        } else if let Some(p) = port_of_gpdmr(a) {
            self.gpio_read(p)
        } else if a == 0x1110 {
            // IVECT
            match self.int_pending() {
                Some(n) => (0x10 + n) as u8,
                None => 0x10,
            }
        } else if a == 0x103F && self.x[0x103C] == 0xFD {
            // follow mode read
            let t = self.t;
            self.fm().xfer_read(t)
        } else if a == 0x103F && self.x[0x103C] != 0xFE {
            // indirect memory read
            let fa = ((self.x[0x103D] as usize) << 16) | ((self.x[0x103C] as usize) << 8) | self.x[0x103B] as usize;
            let d = &self.fm().data;
            d[fa % d.len()]
        } else if let Some(v) = self.smb_read(a) {
            v
        } else if a == 0x2705 {
            // UART1 LSR
            0x60
        } else if let Some(v) = self.board_read(a) {
            v
        } else {
            self.x[a as usize]
        };
        Some(v)
    }

    fn xwrite(&mut self, a: u16, v: u8) -> bool {
        if !(0x1000..0x3000).contains(&a) {
            return false;
        }
        if self.log_regs {
            self.reglog.push((self.t, a, v));
        }
        if let Some(p) = port_of_gpdr(a) {
            let snap = self.snapshot(p);
            self.x[a as usize] = v;
            self.gpio_changed(p, snap);
            return true;
        }
        if (0x1610..0x1660).contains(&a) || (0x16A0..0x16A8).contains(&a) {
            let p = if a >= 0x16A0 { 10 } else { ((a - 0x1610) >> 3) as usize };
            let snap = self.snapshot(p);
            self.x[a as usize] = v;
            self.gpio_changed(p, snap);
            return true;
        }
        if (0x1B04..=0x1B07).contains(&a) || ((0x1B0D..=0x1B35).contains(&a) && (a - 0x1B0D) % 4 == 0) {
            self.x[a as usize] &= !v; // WUESR: write 1 to clear
            for &(_, n, intn) in PIN_WU {
                // the INTC follows WUESR
                let (_, esr, b) = wu_reg(n);
                if esr == a && v >> b & 1 != 0 {
                    self.x[isr_addr((intn >> 3) as u16) as usize] &= !(1 << (intn & 7));
                }
            }
            return true;
        }
        if (0x1100..0x1180).contains(&a) && (0..24).any(|g| isr_addr(g) == a) {
            self.x[a as usize] &= !v; // ISR: write 1 to clear
            return true;
        }
        if a == 0x2700 && self.x[0x2703] & 0x80 == 0 {
            // UART1 THR
            if v == 0x0A {
                let s: String = self.uart.iter().map(|&c| c as char).collect();
                let line = s.trim_end_matches('\r').to_string();
                let ms = round3(self.ms());
                self.uart_lines.push((ms, line.clone()));
                self.ev("uart", line);
                self.uart.clear();
            } else if v != 0x0D {
                self.uart.push(v);
            }
            return true;
        }
        if self.smb_write(a, v) {
            return true;
        }
        if a == 0x1042 {
            self.x[a as usize] = v;
            self.set_scar(v);
            return true;
        }
        if self.ext && (a == 0x1F06 || (a == 0x1F07 && v == 0x5C && self.wdt_running)) {
            // EWDCNTLLR written: the external watchdog (re)starts; EWDKEYR
            // 0x5C restarts a running one. ETWCFG bit 4 (EWDSRC): 1.024 kHz,
            // else 32.768 kHz; the count is EWDCNTLHR:EWDCNTLLR.
            self.x[a as usize] = v;
            let count = ((self.x[0x1F09] as u64) << 8) | self.x[0x1F06] as u64;
            let hz = if self.x[0x1F01] & 0x10 != 0 { 1024 } else { 32768 };
            self.wdt_running = true;
            self.wdt_gen += 1;
            let gen = self.wdt_gen;
            let ms = count as f64 * 1000.0 / hz as f64;
            self.after(
                ms,
                Box::new(move |ec: &mut Ec| {
                    if ec.io.wdt_running && ec.io.wdt_gen == gen && !ec.io.off {
                        ec.io.ev("note", "external watchdog expired".into());
                        ec.watchdog_reset();
                    }
                }),
            );
            return true;
        }
        if a == 0x1F07 && v != 0x5C {
            // EWDKEYR: a wrong key
            self.x[a as usize] = v;
            self.after(0.1, Box::new(|ec: &mut Ec| ec.watchdog_reset()));
            return true;
        }
        if a == 0x103F && (self.x[0x103C] == 0xFD || self.x[0x103C] == 0xFE) {
            self.x[a as usize] = v;
            let (t, sel) = (self.t, self.x[0x103C]);
            let ef = self.x[0x103E] & 0x40 != 0;
            let dev = if ef { &mut self.eflash } else { &mut self.spi };
            let busy = dev.xfer_write(t, sel, v, &mut self.log);
            if busy && ef {
                self.fetch_watch();
            }
            return true;
        }
        self.x[a as usize] = v;
        true
    }

    // ---- SMBus ----
    fn smb_owner(&self, a: u16) -> u8 {
        if (0x1C00..0x1D00).contains(&a) {
            self.smb_addr[(a - 0x1C00) as usize]
        } else {
            0
        }
    }

    fn smb_read(&mut self, a: u16) -> Option<u8> {
        let o = self.smb_owner(a);
        if !(1..=6).contains(&o) {
            return None; // the slaves read like memory
        }
        let ch = (o - 1) as usize;
        let r = self.smb[ch].r;
        if a == r[PCTL] {
            return Some(self.x[a as usize] | 3); // SMCLK, SMDAT high: bus idle
        }
        if a == r[HOBDB] && self.smb[ch].cur.as_ref().is_some_and(|c| c.rd) {
            return Some(self.x[a as usize]);
        }
        None
    }

    fn smb_write(&mut self, a: u16, v: u8) -> bool {
        let o = self.smb_owner(a);
        if o == 0 {
            return false;
        }
        if o >= 7 {
            return self.slave_write((o - 7) as usize, a, v);
        }
        let ch = (o - 1) as usize;
        let r = self.smb[ch].r;
        if a == r[HOSTA] {
            // write 1 to clear
            self.x[a as usize] &= !v;
            if self.smb[ch].cur.is_some() && v & 0x80 != 0 {
                self.smb_next_byte(ch);
            }
            return true;
        }
        if a == r[HOCTL] {
            self.x[a as usize] = v & !0x40;
            if v & 0x02 != 0 && self.smb[ch].cur.is_some() {
                // KILL
                self.smb_finish(ch, 0x10);
            }
            if v & 0x40 != 0 {
                self.smb_start(ch, v);
            }
            return true;
        }
        if a == r[HOBDB] {
            self.x[a as usize] = v;
            return true;
        }
        false
    }

    fn smb_dev(&self, ch: usize, addr: u8) -> Option<Dev> {
        self.smb[ch].devices.iter().find(|d| d.0 == addr).map(|d| d.1)
    }

    fn smb_log(&mut self, ch: usize, addr: u8, wr: &[u8], rd: &[u8], ok: bool) {
        let s = format!(
            "{} {:02X} w[{}] r[{}]{}",
            self.smb[ch].name,
            addr,
            crate::util::hex_sp(wr),
            crate::util::hex_sp(rd),
            if ok { "" } else { " NACK" }
        );
        self.ev("i2c", s);
    }

    fn smb_finish(&mut self, ch: usize, status: u8) {
        let r = self.smb[ch].r;
        self.x[r[HOSTA] as usize] = status;
        self.smb[ch].cur = None;
    }

    fn smb_start(&mut self, ch: usize, ctl: u8) {
        let r = self.smb[ch].r;
        let prot = (ctl >> 2) & 7;
        let sa = self.x[r[TRASLA] as usize];
        let (addr, rd) = (sa >> 1, sa & 1 != 0);
        let cmd = self.x[r[HOCMD] as usize];
        let dev = self.smb_dev(ch, addr);
        if self.smb[ch].hang {
            // busy for ever (a device holding the clock low)
            let s = format!("{} {:02X} HANG", self.smb[ch].name, addr);
            self.ev("i2c", s);
            self.smb[ch].cur = Some(Cur { prot, addr, rd, wr: vec![], rdbuf: Default::default(), n: 0, i: 0, dev, got: vec![], lastfed: false });
            self.x[r[HOSTA] as usize] = 0x01;
            return;
        }
        if prot == 5 || prot == 7 {
            let mut cur = Cur {
                prot,
                addr,
                rd,
                wr: if prot == 5 { vec![cmd] } else { vec![] },
                rdbuf: Default::default(),
                n: self.x[r[D0] as usize],
                i: 0,
                dev,
                got: Vec::new(),
                lastfed: false,
            };
            let Some(dev) = dev else {
                self.smb[ch].cur = Some(cur);
                self.smb_log(ch, addr, &[], &[], false);
                self.smb_finish(ch, 0x04);
                return;
            };
            if rd {
                let wr = cur.wr.clone();
                let Some(mut data) = self.dev_xfer(dev, &wr, if prot == 7 { 255 } else { 33 }) else {
                    self.smb[ch].cur = Some(cur);
                    self.smb_log(ch, addr, &wr, &[], false);
                    self.smb_finish(ch, 0x04);
                    return;
                };
                let mut got0 = Vec::new();
                if prot == 5 {
                    let n = data.first().copied().unwrap_or(0);
                    self.x[r[D0] as usize] = n;
                    got0 = data[..data.len().min(1)].to_vec(); // the count is on the wire too
                    let hi = (1 + n as usize).min(data.len());
                    data = data[1.min(data.len())..hi].to_vec();
                    cur.n = n;
                }
                cur.rdbuf = data.into_iter().collect();
                cur.got = got0;
                self.smb[ch].cur = Some(cur);
                self.smb_feed(ch);
            } else {
                if prot == 7 {
                    cur.wr.push(self.x[r[HOBDB] as usize]);
                }
                if prot == 5 {
                    cur.wr.push(self.x[r[D0] as usize]);
                    cur.wr.push(self.x[r[HOBDB] as usize]);
                    cur.i = 1;
                }
                let end = prot == 5 && cur.i >= cur.n as u32;
                self.smb[ch].cur = Some(cur);
                self.x[r[HOSTA] as usize] = 0x81;
                if end {
                    self.smb_end_write(ch);
                }
            }
            return;
        }
        let x = &self.x;
        let (wr, rdn): (Vec<u8>, usize) = match prot {
            0 => (vec![], 0),
            1 => {
                if rd {
                    (vec![], 1)
                } else {
                    (vec![cmd], 0)
                }
            }
            2 => {
                if rd {
                    (vec![cmd], 1)
                } else {
                    (vec![cmd, x[r[D0] as usize]], 0)
                }
            }
            3 => {
                if rd {
                    (vec![cmd], 2)
                } else {
                    (vec![cmd, x[r[D0] as usize], x[r[D1] as usize]], 0)
                }
            }
            4 => (vec![cmd, x[r[D0] as usize], x[r[D1] as usize]], 2),
            _ => (vec![cmd], 0),
        };
        let Some(dev) = dev else {
            self.smb_log(ch, addr, &wr, &[], false);
            self.smb_finish(ch, 0x04);
            return;
        };
        let Some(mut data) = self.dev_xfer(dev, &wr, rdn) else {
            self.smb_log(ch, addr, &wr, &[], false);
            self.smb_finish(ch, 0x04);
            return;
        };
        if data.len() < rdn {
            let s = format!("{} {:02X} short read {}<{}", self.smb[ch].name, addr, data.len(), rdn);
            self.ev("note", s);
            data.resize(rdn, 0);
        }
        self.smb_log(ch, addr, &wr, &data, true);
        if rdn >= 1 {
            self.x[r[D0] as usize] = data[0];
        }
        if rdn >= 2 {
            self.x[r[D1] as usize] = data[1];
        }
        self.smb_finish(ch, 0x02);
    }

    fn smb_feed(&mut self, ch: usize) {
        let r = self.smb[ch].r;
        let c = self.smb[ch].cur.as_mut().unwrap();
        let b = c.rdbuf.pop_front().unwrap_or(0xFF);
        c.got.push(b);
        c.i += 1;
        self.x[r[HOBDB] as usize] = b;
        self.x[r[HOSTA] as usize] = 0x81;
    }

    fn smb_next_byte(&mut self, ch: usize) {
        let r = self.smb[ch].r;
        let (prot, rd, i, n, lastfed) = {
            let c = self.smb[ch].cur.as_ref().unwrap();
            (c.prot, c.rd, c.i, c.n, c.lastfed)
        };
        let stop = prot == 7 && self.x[r[HOCTL2] as usize] & 0x02 == 0;
        if rd {
            let last = (prot == 5 && i >= n as u32) || stop || lastfed;
            if last {
                let c = self.smb[ch].cur.as_ref().unwrap();
                let (addr, wr, got) = (c.addr, c.wr.clone(), c.got.clone());
                self.smb_log(ch, addr, &wr, &got, true);
                self.smb_finish(ch, 0x02);
                return;
            }
            self.smb_feed(ch);
            if prot == 7 && self.x[r[HOCTL] as usize] & 0x20 != 0 {
                // last byte: NACK, STOP
                self.smb[ch].cur.as_mut().unwrap().lastfed = true;
                self.x[r[HOSTA] as usize] |= 0x02;
            }
            return;
        }
        if stop {
            // I2C mode off: STOP after the bytes sent
            self.smb_end_write(ch);
            return;
        }
        if prot == 7 && self.x[r[HOCTL2] as usize] & 0x04 != 0 {
            // direction switch: repeated start, then read
            let (dev, wr) = {
                let c = self.smb[ch].cur.as_ref().unwrap();
                (c.dev.unwrap(), c.wr.clone())
            };
            let data = match self.dev_xfer(dev, &wr, 255) {
                Some(d) => d,
                // (extended model) the device NACKs: the transfer fails
                None if self.ext => {
                    let addr = self.smb[ch].cur.as_ref().unwrap().addr;
                    self.smb_log(ch, addr, &wr, &[], false);
                    self.smb_finish(ch, 0x04);
                    return;
                }
                None => Vec::new(),
            };
            let c = self.smb[ch].cur.as_mut().unwrap();
            c.rd = true;
            c.rdbuf = data.into_iter().collect();
            c.got = Vec::new();
            self.smb_feed(ch);
            return;
        }
        let b = self.x[r[HOBDB] as usize];
        let c = self.smb[ch].cur.as_mut().unwrap();
        c.wr.push(b);
        c.i += 1;
        if prot == 5 && c.i >= c.n as u32 {
            self.smb_end_write(ch);
            return;
        }
        self.x[r[HOSTA] as usize] = 0x81;
    }

    fn smb_end_write(&mut self, ch: usize) {
        let (addr, dev, wr) = {
            let c = self.smb[ch].cur.as_ref().unwrap();
            (c.addr, c.dev.unwrap(), c.wr.clone())
        };
        let ok = self.dev_xfer(dev, &wr, 0).is_some();
        self.smb_log(ch, addr, &wr, &[], ok);
        self.smb_finish(ch, if ok { 0x02 } else { 0x04 });
    }

    // ---- SMBus slave ----
    /// master writes wr, then (repeated start) reads rdn bytes; `done`
    /// gets the bytes read after the STOP
    pub fn slave_transfer(&mut self, i: usize, wr: &[u8], rdn: usize, done: Option<Box<dyn FnOnce(&mut Ec, Vec<u8>)>>) {
        // a read starts with the address-match event, whose byte is not
        // sent (UEFI's GetEcInfo layout only fits that way)
        let s = &mut self.slave[i];
        s.steps = wr.iter().map(|&b| Step::W(b)).collect();
        s.steps.push_back(Step::A);
        for _ in 0..rdn {
            s.steps.push_back(Step::R);
        }
        s.steps.push_back(Step::P);
        s.rx.clear();
        s.done = done;
        s.wr = wr.to_vec();
        let ms = s.byte_ms;
        self.after(ms, Box::new(move |ec: &mut Ec| ec.io.slave_next(i)));
    }

    fn slave_next(&mut self, i: usize) {
        let (slda, slsta, intn) = (self.slave[i].slda as usize, self.slave[i].slsta as usize, self.slave[i].intn);
        if let Some(n) = self.slave[i].cut_after {
            if n == 0 {
                // the master gives up: no more bytes, a STOP 10 ms later
                self.slave[i].steps.clear();
                self.slave[i].steps.push_back(Step::P);
                self.slave[i].cut_after = None;
                self.after(10.0, Box::new(move |ec: &mut Ec| ec.io.slave_next(i)));
                return;
            }
            self.slave[i].cut_after = Some(n - 1);
        }
        let Some(kind) = self.slave[i].steps.pop_front() else { return };
        self.slave[i].cur = Some(kind);
        match kind {
            Step::W(b) => {
                self.x[slda] = b;
                self.x[slsta] |= 0x12;
                self.x[slsta] &= !0x08;
            }
            Step::R | Step::A => self.x[slsta] |= 0x1A,
            Step::P => {
                self.x[slsta] |= 0x20;
                let s = format!(
                    "{} w[{}] r[{}]",
                    self.slave[i].name,
                    crate::util::hex_sp(&self.slave[i].wr),
                    crate::util::hex_sp(&self.slave[i].rx)
                );
                self.ev("host", s);
                if let Some(done) = self.slave[i].done.take() {
                    let rx = self.slave[i].rx.clone();
                    self.after(0.0, Box::new(move |ec: &mut Ec| done(ec, rx)));
                }
            }
        }
        self.int_raise(intn);
    }

    fn slave_write(&mut self, i: usize, a: u16, v: u8) -> bool {
        let s = &self.slave[i];
        if a == s.slsta {
            self.x[a as usize] &= !v;
            let cur = self.slave[i].cur;
            if v & 0x02 != 0 && matches!(cur, Some(Step::W(_)) | Some(Step::R) | Some(Step::A)) {
                if cur == Some(Step::R) {
                    let b = self.x[self.slave[i].slda as usize];
                    self.slave[i].rx.push(b);
                }
                self.slave[i].cur = None;
                let ms = self.slave[i].byte_ms;
                self.after(ms, Box::new(move |ec: &mut Ec| ec.io.slave_next(i)));
            }
            return true;
        }
        if a == s.slda {
            self.x[a as usize] = v;
            return true;
        }
        false
    }
}

impl Bus for Io {
    #[inline]
    fn code(&mut self, phys: u32) -> Result<u8, Halt> {
        if !self.watch {
            return Ok(self.code[phys as usize]);
        }
        if let Some((a, d)) = &self.scar {
            if *a <= phys && phys < *a + SCRATCH as u32 {
                return Ok(d.get((phys - a) as usize).copied().unwrap_or(0xFF));
            }
        }
        if self.eflash.busy(self.t) {
            return Err(Halt(format!("code fetch from the e-flash at {phys:05X} while it is busy")));
        }
        if self.scar.is_none() {
            self.watch = false;
        }
        Ok(self.code[phys as usize])
    }

    #[inline]
    fn xrd(&mut self, a: u16) -> u8 {
        match self.xread(a) {
            Some(v) => v,
            None => self.x[a as usize],
        }
    }

    #[inline]
    fn xwr(&mut self, a: u16, v: u8) {
        if !self.watch_x.is_empty() && self.watch_x.contains(&a) {
            let old = self.x[a as usize];
            self.ev("watch", format!("{a:04X} {old:02X}->{v:02X}"));
        }
        if !self.xwrite(a, v) {
            self.x[a as usize] = v;
        }
    }

    fn sfr_write(&mut self, a: u8, v: u8) {
        if a == 0x87 && v & 1 != 0 {
            // PCON.IDL
            self.idle = true;
        }
        // PCON.PD: wake is immediate here
    }
}
