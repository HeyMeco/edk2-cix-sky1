// SPDX-License-Identifier: BSD-2-Clause-Patent
//! An interpreter for ARMv8-M (Thumb-2, mainline) as the SE's and the SFH's images use it.
//!
//! The core: the integer instructions of decode.rs, IT blocks, the two
//! stacks and their limits, exception entry and return on the stack as the
//! architecture does them, priorities (PRIMASK, FAULTMASK, BASEPRI, the
//! handlers' priorities), faults and their escalation to HardFault and
//! lockup, the NVIC with level-sensitive lines, SysTick, WFI.  One security
//! state (EXC_RETURN reads as the secure state's: 0xfffffff9, ...fd, ...f1);
//! no FPU, no MPU (its registers are memory).
//!
//! Memory: RAM regions, and everything else in 64 KiB pages made when
//! first touched, read and written through the bus (`Bus`): a read gives
//! what the device model says or, when it says nothing, what was last
//! written; every write outside RAM is recorded in `writes` with the
//! instruction that made it.  The system control space (0xe000e000) is the
//! core's: its writes are recorded and passed to the bus like any other,
//! and take effect here.
//!
//! An instruction is atomic: when it faults, no register has changed (a
//! store of several words may have written some of them, as on the core).
//! Time (`Io::time`, in units the bus defines) is apart from the count of
//! instructions: each instruction takes `Io::tick` units, a core asleep in
//! WFI jumps to the next event, and so does one polling SysTick's
//! COUNTFLAG with the tick interrupt off.
//!
//! `stops` end a run at an address, `stubs` replace a function at its
//! entry, `hooks` are called at an address.  Every instruction executed is
//! marked for the coverage reports.  `audit` checks the interpreter on the
//! code it runs: at every return of a call the registers a callee must
//! preserve are compared with what they were at the call, and at every
//! exception return the registers of the interrupted code.

// The interpreter serves two harnesses (se, sfh), each using part of it: only a build with both says
// what is unused.
#![cfg_attr(not(all(feature = "se", feature = "sfh")), allow(dead_code))]

pub mod decode;

use crate::elf::{Elf, STT_FUNC, STT_OBJECT};
use decode::*;
use std::collections::{BTreeMap, HashMap};
use std::rc::Rc;

pub const PAGE: u32 = 0x10000;

// exception numbers
pub const NMI: u32 = 2;
pub const HARDFAULT: u32 = 3;
pub const MEMMANAGE: u32 = 4;
pub const BUSFAULT: u32 = 5;
pub const USAGEFAULT: u32 = 6;
pub const SVCALL: u32 = 11;
pub const PENDSV: u32 = 14;
pub const SYSTICK: u32 = 15;

// CFSR bits
const CFSR_PRECISERR: u32 = 1 << 9;
const CFSR_BFARVALID: u32 = 1 << 15;
const CFSR_UNDEFINSTR: u32 = 1 << 16;
const CFSR_INVSTATE: u32 = 1 << 17;
const CFSR_INVPC: u32 = 1 << 18;
const CFSR_STKOF: u32 = 1 << 20;
const CFSR_UNALIGNED: u32 = 1 << 24;
const CFSR_DIVBYZERO: u32 = 1 << 25;

const SCS: u32 = 0xe000e000;

/// What the bus sees of the core.
#[derive(Clone, Debug, Default)]
pub struct Io {
    /// simulated time, in units the bus defines
    pub time: u64,
    /// time units an instruction takes (0 counts as 1)
    pub tick: u32,
    /// time units a count of SysTick takes when it runs from the core's clock (CLKSOURCE 1) and from the
    /// reference (CLKSOURCE 0); 0 counts as `tick`
    pub systick_unit: [u32; 2],
    /// level-sensitive external interrupt lines driven by the SoC model
    pub lines: [u64; 8],
    /// something changed that may let an exception be taken
    pub irq_check: bool,
    /// the instruction being run
    pub pc: u32,
    /// bus writes made so far, the one being made included
    pub nwrites: usize,
    /// the bus asks the run to end (with this reason) after the instruction
    pub stop: Option<String>,
}

impl Io {
    pub fn set_line(&mut self, n: u32, level: bool) {
        let (w, b) = ((n / 64) as usize, 1u64 << (n % 64));
        if level {
            self.lines[w] |= b;
        } else {
            self.lines[w] &= !b;
        }
        self.irq_check = true;
    }
}

pub trait Bus {
    /// A read outside RAM: Some(value) is what the device gives (it also becomes the memory's contents);
    /// None: what was last written.
    fn read(&mut self, io: &mut Io, mem: &mut Mem, addr: u32, size: u32) -> Option<u32>;
    /// A write outside RAM, before the memory takes the value (`mem` still has the old one).
    fn write(&mut self, io: &mut Io, mem: &mut Mem, addr: u32, size: u32, value: u32);
    /// The time of the model's next event, if it has one.
    fn next_event(&mut self, _io: &Io) -> Option<u64> {
        None
    }
    /// Time has reached the next event.
    fn event(&mut self, _io: &mut Io, _mem: &mut Mem) {}
}

/// A bus with nothing on it: all memory.
#[derive(Clone, Default)]
pub struct NoBus;

impl Bus for NoBus {
    fn read(&mut self, _io: &mut Io, _mem: &mut Mem, _addr: u32, _size: u32) -> Option<u32> {
        None
    }
    fn write(&mut self, _io: &mut Io, _mem: &mut Mem, _addr: u32, _size: u32, _value: u32) {}
}

#[derive(Clone)]
struct Ram {
    lo: u32,
    buf: Vec<u8>,
}

/// The memory: RAM regions (the first is the image's), and pages made on demand for everything else.
/// A window of the address space can be put on other memory (`remap`): the pages behind it are kept by
/// where the window points, so what is written through one mapping is not seen through another.
#[derive(Clone, Default)]
pub struct Mem {
    ram: Vec<Ram>,
    /// by page key: the address, or for a remapped window where it points
    pages: HashMap<u64, Rc<Box<[u8]>>>,
    /// address ranges that answer with a bus fault
    pub faults: Vec<(u32, u32)>,
    /// (first address, last address + 1, key of the first address)
    remap: Vec<(u32, u32, u64)>,
}

impl Mem {
    pub fn add_ram(&mut self, lo: u32, size: u32) {
        self.ram.push(Ram { lo, buf: vec![0; size as usize] });
    }

    #[inline]
    fn ram_at(&self, addr: u32, size: u32) -> Option<(usize, usize)> {
        for (i, r) in self.ram.iter().enumerate() {
            let off = addr.wrapping_sub(r.lo) as usize;
            if off < r.buf.len() && off + size as usize <= r.buf.len() {
                return Some((i, off));
            }
        }
        None
    }

    pub fn is_ram(&self, addr: u32) -> bool {
        self.ram_at(addr, 1).is_some()
    }

    /// Put the addresses lo..hi on the memory whose key starts at `key` (lo itself: as they are).
    pub fn set_remap(&mut self, lo: u32, hi: u32, key: u64) {
        self.remap.retain(|r| r.0 != lo);
        if key != lo as u64 {
            self.remap.push((lo, hi, key));
        }
    }

    /// The key of an address's memory.
    #[inline]
    pub fn key(&self, addr: u32) -> u64 {
        for (lo, hi, key) in &self.remap {
            if *lo <= addr && addr < *hi {
                return key + (addr - lo) as u64;
            }
        }
        addr as u64
    }

    fn page(&mut self, key: u64) -> &mut [u8] {
        let p = self.pages.entry(key & !(PAGE as u64 - 1)).or_insert_with(|| Rc::new(vec![0u8; PAGE as usize].into_boxed_slice()));
        &mut Rc::make_mut(p)[..]
    }

    pub fn read8(&self, addr: u32) -> u8 {
        if let Some((i, off)) = self.ram_at(addr, 1) {
            return self.ram[i].buf[off];
        }
        let key = self.key(addr);
        self.pages.get(&(key & !(PAGE as u64 - 1))).map_or(0, |p| p[(key & (PAGE as u64 - 1)) as usize])
    }

    pub fn write8(&mut self, addr: u32, v: u8) {
        if let Some((i, off)) = self.ram_at(addr, 1) {
            self.ram[i].buf[off] = v;
        } else {
            let key = self.key(addr);
            self.page(key)[(key & (PAGE as u64 - 1)) as usize] = v;
        }
    }

    /// Little-endian read of 1, 2 or 4 bytes; a page never touched reads as zero (and is made).
    pub fn read(&mut self, addr: u32, size: u32) -> u32 {
        if let Some((i, off)) = self.ram_at(addr, size) {
            let b = &self.ram[i].buf[off..];
            return match size {
                1 => b[0] as u32,
                2 => u16::from_le_bytes([b[0], b[1]]) as u32,
                _ => u32::from_le_bytes([b[0], b[1], b[2], b[3]]),
            };
        }
        let off = (addr & (PAGE - 1)) as usize;
        if off + size as usize <= PAGE as usize && !self.is_ram(addr) {
            let key = self.key(addr);
            if !self.pages.contains_key(&(key & !(PAGE as u64 - 1))) {
                self.page(key);
            }
            let p = &self.pages[&(key & !(PAGE as u64 - 1))];
            let mut v = 0u32;
            for k in (0..size as usize).rev() {
                v = v << 8 | p[off + k] as u32;
            }
            return v;
        }
        self.peek(addr, size)
    }

    /// The same without making the page (for a look from outside).
    pub fn peek(&self, addr: u32, size: u32) -> u32 {
        let mut v = 0u32;
        for k in (0..size).rev() {
            v = v << 8 | self.read8(addr.wrapping_add(k)) as u32;
        }
        v
    }

    pub fn write(&mut self, addr: u32, size: u32, value: u32) {
        if let Some((i, off)) = self.ram_at(addr, size) {
            self.ram[i].buf[off..off + size as usize].copy_from_slice(&value.to_le_bytes()[..size as usize]);
            return;
        }
        let off = (addr & (PAGE - 1)) as usize;
        if off + size as usize <= PAGE as usize && !self.is_ram(addr) {
            let key = self.key(addr);
            self.page(key)[off..off + size as usize].copy_from_slice(&value.to_le_bytes()[..size as usize]);
            return;
        }
        for k in 0..size {
            self.write8(addr.wrapping_add(k), (value >> (8 * k)) as u8);
        }
    }

    pub fn read32(&self, addr: u32) -> u32 {
        self.peek(addr, 4)
    }

    pub fn bytes(&self, addr: u32, n: u32) -> Vec<u8> {
        if let Some((i, off)) = self.ram_at(addr, n) {
            return self.ram[i].buf[off..off + n as usize].to_vec();
        }
        (0..n).map(|k| self.read8(addr.wrapping_add(k))).collect()
    }

    pub fn write_bytes(&mut self, addr: u32, data: &[u8]) {
        if let Some((i, off)) = self.ram_at(addr, data.len() as u32) {
            self.ram[i].buf[off..off + data.len()].copy_from_slice(data);
            return;
        }
        for (k, b) in data.iter().enumerate() {
            self.write8(addr.wrapping_add(k as u32), *b);
        }
    }

    /// The pages outside RAM touched so far (those with an address of their own), sorted.
    pub fn mapped(&self) -> Vec<u32> {
        let mut v: Vec<u32> = self.pages.keys().filter(|k| **k <= u32::MAX as u64).map(|k| *k as u32).collect();
        v.sort();
        v
    }

    /// All pages by key, sorted.
    pub fn keys(&self) -> Vec<u64> {
        let mut v: Vec<u64> = self.pages.keys().copied().collect();
        v.sort();
        v
    }

    pub fn page_data(&self, page: u32) -> Option<&[u8]> {
        self.key_data(page as u64)
    }

    pub fn key_data(&self, key: u64) -> Option<&[u8]> {
        self.pages.get(&key).map(|p| &p[..])
    }

    /// Bytes by key (what is behind a window, wherever it points now).
    pub fn key_bytes(&self, key: u64, n: u32) -> Vec<u8> {
        (0..n as u64)
            .map(|k| {
                let a = key + k;
                self.pages.get(&(a & !(PAGE as u64 - 1))).map_or(0, |p| p[(a & (PAGE as u64 - 1)) as usize])
            })
            .collect()
    }

    /// Make the page (as a first access would).
    pub fn map(&mut self, addr: u32) {
        let key = self.key(addr);
        self.page(key);
    }

    pub fn zero_page(&mut self, page: u32) {
        if self.pages.contains_key(&(page as u64)) {
            self.page(page as u64).fill(0);
        }
    }

    fn faulting(&self, addr: u32) -> bool {
        self.faults.iter().any(|(lo, hi)| *lo <= addr && addr < *hi)
    }
}

#[derive(Clone, Copy, Debug)]
pub struct Write {
    pub pc: u32,
    pub addr: u32,
    pub size: u8,
    pub value: u32,
}

/// The reads of one address outside RAM.
#[derive(Clone, Debug, Default)]
pub struct Reads {
    pub count: u32,
    /// (instruction, how many)
    pub by: Vec<(u32, u32)>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Stop {
    /// a stop address (its name)
    At(String),
    /// WFI (`wfi_stops`), or asleep with nothing to wake the core
    Wfi,
    /// in a branch to itself (`spin_sleeps`) with nothing to interrupt it
    Spin,
    /// the run's limit of time or instructions
    Limit,
    /// the bus asked (`Io::stop`)
    Bus(String),
    /// a fault where none can be taken: the core locks up
    Lockup(String),
    /// SYSRESETREQ
    Reset,
    /// the interpreter's self-check failed
    Audit(String),
    /// the emulator cannot go on (an instruction it does not have)
    Error(String),
}

impl std::fmt::Display for Stop {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        match self {
            Stop::At(s) => write!(f, "{s}"),
            Stop::Wfi => write!(f, "asleep (WFI)"),
            Stop::Spin => write!(f, "in a branch to itself"),
            Stop::Limit => write!(f, "limit"),
            Stop::Bus(s) => write!(f, "{s}"),
            Stop::Lockup(s) => write!(f, "lockup: {s}"),
            Stop::Reset => write!(f, "system reset request"),
            Stop::Audit(s) => write!(f, "audit: {s}"),
            Stop::Error(s) => write!(f, "error: {s}"),
        }
    }
}

enum Trap {
    /// a fault: CFSR bits, the exception it belongs to, the address for BFAR
    Fault(u32, u32, Option<u32>),
    Svc,
    Stop(Stop),
}

/// The core's registers (not the system control space): what a call from outside saves and restores.
#[derive(Clone, Debug, PartialEq)]
pub struct Context {
    pub r: [u32; 16],
    pub flags: [bool; 4],
    pub ipsr: u32,
    pub it: u8,
    pub primask: bool,
    pub faultmask: bool,
    pub basepri: u8,
    pub control: u32,
    pub sp_other: u32,
}

#[derive(Clone)]
struct CallFrame {
    ret: u32,
    sp: u32,
    regs: [u32; 8],
    /// the call's own address, for the report
    from: u32,
}

#[derive(Clone)]
struct ExcFrame {
    n: u32,
    r: [u32; 16],
    flags: [bool; 4],
    it: u8,
    depth: usize,
}

pub type Stub<B> = Rc<dyn Fn(&mut Arm<B>) -> Option<u32>>;
pub type Hook<B> = Rc<dyn Fn(&mut Arm<B>)>;

const MARK_STOP: u8 = 1;
const MARK_STUB: u8 = 2;
const MARK_HOOK: u8 = 4;
const NO_INSN: Insn = Insn { op: Op::Nop, len: 0, rd: 0, rn: 0, rm: 0, ra: 0, sh: 0, s: 0, f: 0, imm: 0 };

#[derive(Clone)]
pub struct Arm<B: Bus> {
    /// r[15] is the address of the instruction to run next
    pub r: [u32; 16],
    pub n: bool,
    pub z: bool,
    pub c: bool,
    pub v: bool,
    pub ipsr: u32,
    it: u8,
    thumb: bool,
    pub primask: bool,
    pub faultmask: bool,
    pub basepri: u8,
    /// bit 1: SPSEL (thread mode on the process stack)
    pub control: u32,
    /// the stack pointer not in r[13]
    sp_other: u32,
    pub msplim: u32,
    pub psplim: u32,

    pub mem: Mem,
    pub bus: B,
    pub io: Io,

    // system control space
    pub vtor: u32,
    enabled: [u64; 8],
    latched: [u64; 8],
    active: [u64; 8],
    /// by exception number
    prio: Vec<u8>,
    sys_pending: u32,
    sys_active: u32,
    shcsr: u32,
    pub cfsr: u32,
    pub hfsr: u32,
    pub bfar: u32,
    ccr: u32,
    /// CCR.UNALIGN_TRP: an unaligned word or halfword access is a UsageFault
    unalign_trap: bool,
    aircr: u32,
    st_ctrl: u32,
    st_reload: u32,
    /// the count while SysTick is off
    st_cur: u32,
    st_flag: bool,
    /// the time COUNTFLAG is next set
    st_next: u64,
    /// the time the count was last cleared by a write
    st_cleared: u64,
    /// (instruction, stack pointer, bus access number) of the last read of SysTick's control register
    st_poll: (u32, u32, u64),
    /// reads and writes outside RAM so far
    accesses: u64,
    next_ev: u64,
    bus_next: Option<u64>,

    /// the core answers reads of its system control space (off: they read as memory, or as the bus says)
    pub scs_reads: bool,
    /// a poll of COUNTFLAG with the tick interrupt off jumps to the time it is set
    pub systick_jump: bool,
    /// every WFI ends the run (as Stop::Wfi), pending interrupt or not
    pub wfi_stops: bool,
    /// pending exceptions are taken (off: whoever drives the core delivers interrupts)
    pub deliver: bool,
    /// a branch to itself passes the time to the next event, as WFI does (off: it is run, again and again)
    pub spin_sleeps: bool,
    asleep: Option<Stop>,

    // decoded instructions, marks and coverage of the first RAM region, per halfword
    code: Vec<Insn>,
    mark: Vec<u8>,
    cov: Vec<u8>,
    ram_base: u32,
    /// the span of the instructions decoded so far (a store into it forgets them)
    code_lo: u32,
    code_hi: u32,
    pub stops: HashMap<u32, String>,
    pub stubs: HashMap<u32, Stub<B>>,
    pub hooks: HashMap<u32, Hook<B>>,
    /// the next instruction runs even with a stop on it
    pub pass_stop: bool,

    pub writes: Vec<Write>,
    /// reads outside RAM, when asked for: by address, how many, and by which instructions (the first of
    /// them first; at most eight are kept)
    pub reads: Option<HashMap<u32, Reads>>,
    /// the addresses in the order they were first read
    pub read_order: Vec<u32>,
    /// reads are put down to the return address in LR, not to the instruction (to see past a function
    /// every register access goes through)
    pub reads_by_caller: bool,
    pub last_read: Option<u32>,
    /// instructions run
    pub count: u64,
    /// exceptions taken, by number
    pub taken: BTreeMap<u32, u64>,
    /// the instruction to run next starts a translation block as QEMU would cut them
    pub block_start: bool,
    tb_start: u32,

    pub audit: bool,
    /// calls counted by (target, return address), when asked for
    pub call_counts: Option<HashMap<(u32, u32), u64>>,
    calls: Vec<CallFrame>,
    excs: Vec<ExcFrame>,

    // symbols
    funcs: Rc<Vec<(u32, u32, String)>>,
    syms: Rc<HashMap<String, (u32, u32, u8)>>,
    pub path: String,
}

fn bit(w: &[u64; 8], n: u32) -> bool {
    w[(n / 64) as usize] >> (n % 64) & 1 != 0
}

fn set_bit(w: &mut [u64; 8], n: u32, v: bool) {
    if v {
        w[(n / 64) as usize] |= 1 << (n % 64);
    } else {
        w[(n / 64) as usize] &= !(1 << (n % 64));
    }
}

impl<B: Bus> Arm<B> {
    pub fn new(bus: B) -> Self {
        Arm {
            r: [0; 16],
            n: false,
            z: false,
            c: false,
            v: false,
            ipsr: 0,
            it: 0,
            thumb: true,
            primask: false,
            faultmask: false,
            basepri: 0,
            control: 0,
            sp_other: 0,
            msplim: 0,
            psplim: 0,
            mem: Mem::default(),
            bus,
            io: Io::default(),
            vtor: 0,
            enabled: [0; 8],
            latched: [0; 8],
            active: [0; 8],
            prio: vec![0; 16 + 496],
            sys_pending: 0,
            sys_active: 0,
            shcsr: 0,
            cfsr: 0,
            hfsr: 0,
            bfar: 0,
            ccr: 0x200,
            unalign_trap: false,
            aircr: 0,
            st_ctrl: 0,
            st_reload: 0,
            st_cur: 0,
            st_flag: false,
            st_next: u64::MAX,
            st_cleared: 0,
            st_poll: (0, 0, 0),
            accesses: 0,
            next_ev: u64::MAX,
            bus_next: None,
            scs_reads: true,
            systick_jump: true,
            wfi_stops: false,
            deliver: true,
            spin_sleeps: false,
            asleep: None,
            code: Vec::new(),
            mark: Vec::new(),
            cov: Vec::new(),
            ram_base: 0,
            code_lo: 0,
            code_hi: 0,
            stops: HashMap::new(),
            stubs: HashMap::new(),
            hooks: HashMap::new(),
            pass_stop: false,
            writes: Vec::new(),
            reads: None,
            read_order: Vec::new(),
            reads_by_caller: false,
            last_read: None,
            count: 0,
            taken: BTreeMap::new(),
            block_start: true,
            tb_start: 0,
            audit: false,
            call_counts: None,
            calls: Vec::new(),
            excs: Vec::new(),
            funcs: Rc::new(Vec::new()),
            syms: Rc::new(HashMap::new()),
            path: String::new(),
        }
    }

    // --- memory ------------------------------------------------------------------------

    /// Add a RAM region; the first is where code runs from (decoded instructions, stops, coverage).
    pub fn add_ram(&mut self, lo: u32, size: u32) {
        if self.mem.ram.is_empty() {
            let n = (size / 2) as usize;
            self.code = vec![NO_INSN; n];
            self.mark = vec![0; n];
            self.cov = vec![0; n];
            self.ram_base = lo;
        }
        self.mem.add_ram(lo, size);
    }

    fn idx(&self, pc: u32) -> Option<usize> {
        let i = (pc.wrapping_sub(self.ram_base) >> 1) as usize;
        (i < self.code.len()).then_some(i)
    }

    /// Load an ELF's segments (at their physical addresses) and its symbols.
    pub fn load_elf(&mut self, path: &str) -> Result<(), String> {
        let elf = Elf::load(path)?;
        for s in &elf.segments {
            if !s.data.is_empty() {
                self.write_bytes(s.paddr, &s.data);
            }
        }
        // names as the Python reference resolved them: one entry per name (the last in the table),
        // sorted by address, size and name
        let mut by_name: HashMap<String, (u32, u32, u8)> = HashMap::new();
        for s in &elf.symbols {
            if (s.kind == STT_FUNC || s.kind == STT_OBJECT) && !s.name.is_empty() {
                by_name.insert(s.name.clone(), (s.value, s.size, s.kind));
            }
        }
        let mut funcs: Vec<(u32, u32, String)> =
            by_name.iter().filter(|(_, v)| v.2 == STT_FUNC).map(|(n, v)| (v.0 & !1, v.1, n.clone())).collect();
        funcs.sort();
        self.funcs = Rc::new(funcs);
        self.syms = Rc::new(by_name);
        self.path = path.to_string();
        Ok(())
    }

    pub fn sym(&self, name: &str) -> Option<u32> {
        self.syms.get(name).map(|s| s.0 & !1)
    }

    pub fn addr(&self, name: &str) -> u32 {
        self.sym(name).unwrap_or_else(|| panic!("{}: no symbol {name}", self.path))
    }

    /// The function `pc` is in, or the address in hexadecimal.
    pub fn func(&self, pc: u32) -> String {
        let i = self.funcs.partition_point(|f| f.0 <= pc);
        if i > 0 {
            let (a, n, name) = &self.funcs[i - 1];
            if *a <= pc && pc < *a + (*n).max(2) {
                return name.clone();
            }
        }
        format!("{pc:#x}")
    }

    /// The image's file name.
    pub fn name(&self) -> String {
        std::path::Path::new(&self.path).file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default()
    }

    pub fn read32(&self, addr: u32) -> u32 {
        self.mem.read32(addr)
    }

    pub fn read_bytes(&self, addr: u32, n: u32) -> Vec<u8> {
        self.mem.bytes(addr, n)
    }

    /// Write memory from outside (no bus, no record); decoded instructions there are forgotten.
    pub fn write_bytes(&mut self, addr: u32, data: &[u8]) {
        self.mem.write_bytes(addr, data);
        self.forget(addr, data.len() as u32);
    }

    pub fn write32(&mut self, addr: u32, v: u32) {
        self.write_bytes(addr, &v.to_le_bytes());
    }

    fn forget(&mut self, addr: u32, n: u32) {
        if addr < self.code_hi && addr.wrapping_add(n) > self.code_lo {
            let lo = addr.max(self.code_lo + 2) - 2;
            let hi = addr.saturating_add(n).min(self.code_hi);
            let mut a = lo & !1;
            while a < hi {
                if let Some(i) = self.idx(a) {
                    self.code[i] = NO_INSN;
                }
                a += 2;
            }
        }
    }

    #[inline]
    fn load(&mut self, addr: u32, size: u32) -> Result<u32, Trap> {
        if self.unalign_trap && addr & (size - 1) != 0 {
            return Err(Trap::Fault(CFSR_UNALIGNED, USAGEFAULT, None));
        }
        if let Some(r) = self.mem.ram.first() {
            let off = addr.wrapping_sub(r.lo) as usize;
            if off < r.buf.len() && off + size as usize <= r.buf.len() {
                let b = &r.buf[off..];
                return Ok(match size {
                    1 => b[0] as u32,
                    2 => u16::from_le_bytes([b[0], b[1]]) as u32,
                    _ => u32::from_le_bytes([b[0], b[1], b[2], b[3]]),
                });
            }
        }
        self.load_slow(addr, size)
    }

    fn load_slow(&mut self, addr: u32, size: u32) -> Result<u32, Trap> {
        if self.mem.is_ram(addr) {
            return Ok(self.mem.read(addr, size));
        }
        if self.mem.faulting(addr) {
            return Err(Trap::Fault(CFSR_PRECISERR | CFSR_BFARVALID, BUSFAULT, Some(addr)));
        }
        self.io.pc = self.r[15];
        self.accesses += 1;
        let mut dev = self.bus.read(&mut self.io, &mut self.mem, addr, size);
        if dev.is_none() && self.scs_reads && addr & 0xfffff000 == SCS {
            dev = self.scs_read(addr, size);
        }
        if let Some(v) = dev {
            self.mem.write(addr, size, v);
        }
        self.last_read = Some(addr);
        if let Some(r) = &mut self.reads {
            let pc = if self.reads_by_caller { self.r[14] & !1 } else { self.r[15] };
            let e = r.entry(addr).or_insert_with(|| {
                self.read_order.push(addr);
                Reads::default()
            });
            e.count += 1;
            if let Some(b) = e.by.iter_mut().find(|b| b.0 == pc) {
                b.1 += 1;
            } else if e.by.len() < 8 {
                e.by.push((pc, 1));
            }
        }
        self.update_next();
        Ok(self.mem.read(addr, size))
    }

    #[inline]
    fn store(&mut self, addr: u32, size: u32, value: u32) -> Result<(), Trap> {
        if self.unalign_trap && addr & (size - 1) != 0 {
            return Err(Trap::Fault(CFSR_UNALIGNED, USAGEFAULT, None));
        }
        if let Some(r) = self.mem.ram.first_mut() {
            let off = addr.wrapping_sub(r.lo) as usize;
            if off < r.buf.len() && off + size as usize <= r.buf.len() {
                r.buf[off..off + size as usize].copy_from_slice(&value.to_le_bytes()[..size as usize]);
                if addr < self.code_hi {
                    self.forget(addr, size);
                }
                return Ok(());
            }
        }
        self.store_slow(addr, size, value)
    }

    fn store_slow(&mut self, addr: u32, size: u32, value: u32) -> Result<(), Trap> {
        if self.mem.is_ram(addr) {
            self.mem.write(addr, size, value);
            return Ok(());
        }
        if self.mem.faulting(addr) {
            return Err(Trap::Fault(CFSR_PRECISERR | CFSR_BFARVALID, BUSFAULT, Some(addr)));
        }
        let value = if size == 4 { value } else { value & ((1 << (8 * size)) - 1) };
        self.io.pc = self.r[15];
        self.accesses += 1;
        self.writes.push(Write { pc: self.r[15], addr, size: size as u8, value });
        self.io.nwrites = self.writes.len();
        if addr & 0xfffff000 == SCS {
            self.scs_write(addr, size, value)?;
        }
        self.bus.write(&mut self.io, &mut self.mem, addr, size, value);
        self.mem.write(addr, size, value);
        self.update_next();
        Ok(())
    }

    // --- stops, stubs, hooks -----------------------------------------------------------

    fn set_mark(&mut self, pc: u32, m: u8, on: bool) {
        if let Some(i) = self.idx(pc) {
            if on {
                self.mark[i] |= m;
            } else {
                self.mark[i] &= !m;
            }
        }
    }

    pub fn set_stop(&mut self, pc: u32, name: &str) {
        self.stops.insert(pc, name.to_string());
        self.set_mark(pc, MARK_STOP, true);
    }

    pub fn del_stop(&mut self, pc: u32) {
        self.stops.remove(&pc);
        self.set_mark(pc, MARK_STOP, false);
    }

    pub fn set_stub(&mut self, pc: u32, f: Stub<B>) {
        self.stubs.insert(pc, f);
        self.set_mark(pc, MARK_STUB, true);
    }

    pub fn set_hook(&mut self, pc: u32, f: Hook<B>) {
        self.hooks.insert(pc, f);
        self.set_mark(pc, MARK_HOOK, true);
    }

    /// The marks at `pc`: (stop, stub, hook).
    pub fn marks(&self, pc: u32) -> (bool, bool, bool) {
        let m = self.idx(pc).map_or(0, |i| self.mark[i]);
        (m & MARK_STOP != 0, m & MARK_STUB != 0, m & MARK_HOOK != 0)
    }

    /// Return from the function just entered with `r0` (a stub's doing).
    pub fn ret(&mut self, r0: Option<u32>) -> Result<(), Stop> {
        if let Some(v) = r0 {
            self.r[0] = v;
        }
        let lr = self.r[14];
        match self.bx(lr) {
            Ok(pc) => {
                self.r[15] = pc;
                self.block_start = true;
                Ok(())
            }
            Err(Trap::Stop(s)) => Err(s),
            Err(_) => Err(Stop::Error("a stub returned into a fault".into())),
        }
    }

    // --- registers -----------------------------------------------------------------------

    pub fn context(&self) -> Context {
        Context {
            r: self.r,
            flags: [self.n, self.z, self.c, self.v],
            ipsr: self.ipsr,
            it: self.it,
            primask: self.primask,
            faultmask: self.faultmask,
            basepri: self.basepri,
            control: self.control,
            sp_other: self.sp_other,
        }
    }

    pub fn set_context(&mut self, c: &Context) {
        self.r = c.r;
        [self.n, self.z, self.c, self.v] = c.flags;
        self.ipsr = c.ipsr;
        self.it = c.it;
        self.primask = c.primask;
        self.faultmask = c.faultmask;
        self.basepri = c.basepri;
        self.control = c.control;
        self.sp_other = c.sp_other;
        self.io.irq_check = true;
    }

    /// Start at the reset vector of the table at `vtor`.
    pub fn reset(&mut self, vtor: u32) {
        self.vtor = vtor;
        self.r[13] = self.mem.read32(vtor) & !3;
        let pc = self.mem.read32(vtor + 4);
        self.r[14] = 0xffffffff;
        self.r[15] = pc & !1;
        self.thumb = pc & 1 != 0;
        self.ipsr = 0;
        self.it = 0;
        self.control = 0;
        self.block_start = true;
    }

    fn handler_mode(&self) -> bool {
        self.ipsr != 0
    }

    /// True if r[13] is the process stack.
    fn on_psp(&self) -> bool {
        !self.handler_mode() && self.control & 2 != 0
    }

    pub fn msp(&self) -> u32 {
        if self.on_psp() {
            self.sp_other
        } else {
            self.r[13]
        }
    }

    pub fn psp(&self) -> u32 {
        if self.on_psp() {
            self.r[13]
        } else {
            self.sp_other
        }
    }

    fn set_msp(&mut self, v: u32) {
        if self.on_psp() {
            self.sp_other = v & !3;
        } else {
            self.r[13] = v & !3;
        }
    }

    fn set_psp(&mut self, v: u32) {
        if self.on_psp() {
            self.r[13] = v & !3;
        } else {
            self.sp_other = v & !3;
        }
    }

    /// Change mode or SPSEL, keeping the two stack pointers in their places.
    fn switch(&mut self, ipsr: u32, control: u32) {
        let (msp, psp) = (self.msp(), self.psp());
        self.ipsr = ipsr;
        self.control = control;
        if self.on_psp() {
            self.r[13] = psp;
            self.sp_other = msp;
        } else {
            self.r[13] = msp;
            self.sp_other = psp;
        }
    }

    fn sp_limit(&self) -> u32 {
        if self.on_psp() {
            self.psplim
        } else {
            self.msplim
        }
    }

    pub fn xpsr(&self) -> u32 {
        (self.n as u32) << 31
            | (self.z as u32) << 30
            | (self.c as u32) << 29
            | (self.v as u32) << 28
            | ((self.it & 3) as u32) << 25
            | (self.thumb as u32) << 24
            | ((self.it >> 2) as u32) << 10
            | self.ipsr
    }

    #[inline]
    fn reg(&self, i: u8) -> u32 {
        if i == 15 {
            self.r[15].wrapping_add(4)
        } else {
            self.r[i as usize]
        }
    }

    #[inline]
    fn set_reg(&mut self, i: u8, v: u32) {
        // (the program counter is written by the branches themselves)
        if i == 13 {
            self.r[13] = v & !3;
        } else {
            self.r[i as usize] = v;
        }
    }

    #[inline]
    fn cond(&self, cond: u8) -> bool {
        let r = match cond >> 1 {
            0 => self.z,
            1 => self.c,
            2 => self.n,
            3 => self.v,
            4 => self.c && !self.z,
            5 => self.n == self.v,
            6 => self.n == self.v && !self.z,
            _ => true,
        };
        if cond & 1 != 0 && cond != 15 {
            !r
        } else {
            r
        }
    }

    // --- the system control space --------------------------------------------------------

    fn systick_unit(&self) -> u64 {
        let u = self.io.systick_unit[(self.st_ctrl >> 2 & 1 == 0) as usize];
        (if u != 0 { u } else { self.io.tick }).max(1) as u64
    }

    fn systick_count(&self) -> u32 {
        if self.st_ctrl & 1 != 0 && self.st_next != u64::MAX {
            let left = (self.st_next.saturating_sub(self.io.time)).div_ceil(self.systick_unit());
            (left as u32).min(self.st_reload)
        } else {
            self.st_cur
        }
    }

    fn scs_read(&mut self, addr: u32, size: u32) -> Option<u32> {
        let a = addr & !3;
        let off = a - SCS;
        let word = |w: &[u64; 8], k: u32| (w[(k / 2) as usize] >> (32 * (k % 2))) as u32;
        let v = match off {
            0x010 => {
                // a poll: the instruction that read the register last reads it again in the same frame, with
                // no bus access in between.  Nothing changes for it until the count runs out, or the
                // model's next event.
                let poll = self.st_poll == (self.r[15], self.r[13], self.accesses);
                self.st_poll = (self.r[15], self.r[13], self.accesses + 1);
                if poll && self.systick_jump && self.st_ctrl & 3 == 1 && !self.st_flag {
                    let to = self.st_next.min(self.bus_next.unwrap_or(u64::MAX));
                    if to != u64::MAX && to > self.io.time {
                        self.io.time = to;
                    }
                    self.events();
                }
                let v = self.st_ctrl | (self.st_flag as u32) << 16;
                self.st_flag = false;
                v
            }
            0x014 => self.st_reload,
            0x018 => self.systick_count(),
            0x100..=0x13c => word(&self.enabled, (off - 0x100) / 4),
            0x180..=0x1bc => word(&self.enabled, (off - 0x180) / 4),
            0x200..=0x23c | 0x280..=0x2bc => {
                let k = (off & 0x7f) / 4;
                word(&self.latched, k) | (word(&self.io.lines, k) & !word(&self.active, k))
            }
            0x300..=0x33c => word(&self.active, (off - 0x300) / 4),
            0x400..=0x5ef => {
                let n = 16 + (off - 0x400) as usize;
                u32::from_le_bytes([self.prio[n], self.prio[n + 1], self.prio[n + 2], self.prio[n + 3]])
            }
            0xd04 => {
                let pend = self.highest_pending().map_or(0, |(n, _)| n);
                self.ipsr
                    | pend << 12
                    | ((self.sys_pending >> SYSTICK & 1) << 26)
                    | ((self.sys_pending >> PENDSV & 1) << 28)
                    | ((self.sys_pending >> NMI & 1) << 31)
                    | ((self.latched.iter().any(|w| *w != 0) as u32) << 22)
            }
            0xd08 => self.vtor,
            0xd0c => 0xfa050000 | (self.aircr & 0x0700),
            0xd14 => self.ccr,
            0xd18 | 0xd1c | 0xd20 => {
                let n = 4 + (off - 0xd18) as usize;
                u32::from_le_bytes([self.prio[n], self.prio[n + 1], self.prio[n + 2], self.prio[n + 3]])
            }
            0xd24 => {
                self.shcsr & 0x000f0000
                    | (self.sys_active >> MEMMANAGE & 1)
                    | (self.sys_active >> BUSFAULT & 1) << 1
                    | (self.sys_active >> HARDFAULT & 1) << 2
                    | (self.sys_active >> USAGEFAULT & 1) << 3
                    | (self.sys_active >> NMI & 1) << 5
                    | (self.sys_active >> SVCALL & 1) << 7
                    | (self.sys_active >> PENDSV & 1) << 10
                    | (self.sys_active >> SYSTICK & 1) << 11
            }
            0xd28 => self.cfsr,
            0xd2c => self.hfsr,
            0xd38 => self.bfar,
            _ => return None,
        };
        let sh = 8 * (addr & 3);
        Some(if size == 4 { v } else { (v >> sh) & ((1 << (8 * size)) - 1) })
    }

    fn scs_write(&mut self, addr: u32, size: u32, value: u32) -> Result<(), Trap> {
        let off = addr - SCS;
        // priorities are bytes
        if (0x400..0x5f0).contains(&off) || (0xd18..0xd24).contains(&off) {
            for k in 0..size {
                let o = off + k;
                let n = if o >= 0xd18 { 4 + (o - 0xd18) } else { 16 + (o - 0x400) } as usize;
                self.prio[n] = (value >> (8 * k)) as u8 & 0xe0;
            }
            self.io.irq_check = true;
            return Ok(());
        }
        // the others are words: a narrower write changes its bytes of a write-one register, or of the
        // word as it reads
        let a = addr & !3;
        let off = a - SCS;
        let sh = 8 * (addr & 3);
        let w1 = matches!(off, 0x100..=0x2bc | 0xd04 | 0xd28 | 0xd2c);
        let v = if size == 4 {
            value
        } else if w1 {
            value << sh
        } else {
            let old = self.scs_read(a, 4).unwrap_or_else(|| self.mem.read32(a));
            let mask = ((1u32 << (8 * size)) - 1) << sh;
            (old & !mask) | (value << sh)
        };
        let set = |w: &mut [u64; 8], k: u32, v: u32, on: bool| {
            let (i, s) = ((k / 2) as usize, 32 * (k % 2));
            if on {
                w[i] |= (v as u64) << s;
            } else {
                w[i] &= !((v as u64) << s);
            }
        };
        match off {
            0x010 => {
                let was = self.st_ctrl & 1 != 0;
                if was {
                    self.st_cur = self.systick_count();
                }
                self.st_ctrl = v & 7;
                if v & 1 != 0 {
                    let n = if self.st_cur == 0 { self.st_reload as u64 + 1 } else { self.st_cur as u64 };
                    self.st_next = self.io.time + n * self.systick_unit();
                } else {
                    self.st_next = u64::MAX;
                }
            }
            0x014 => {
                self.st_reload = v & 0xffffff;
                // a count just cleared takes the reload value at its next clock: this one, if it comes first
                if self.st_ctrl & 1 != 0 && self.io.time.saturating_sub(self.st_cleared) < self.systick_unit() {
                    self.st_next = self.st_cleared + (self.st_reload as u64 + 1) * self.systick_unit();
                }
            }
            0x018 => {
                self.st_cur = 0;
                self.st_flag = false;
                self.st_cleared = self.io.time;
                if self.st_ctrl & 1 != 0 {
                    self.st_next = self.io.time + (self.st_reload as u64 + 1) * self.systick_unit();
                }
            }
            0x100..=0x13c => set(&mut self.enabled, (off - 0x100) / 4, v, true),
            0x180..=0x1bc => set(&mut self.enabled, (off - 0x180) / 4, v, false),
            0x200..=0x23c => set(&mut self.latched, (off - 0x200) / 4, v, true),
            0x280..=0x2bc => set(&mut self.latched, (off - 0x280) / 4, v, false),
            0xd04 => {
                if v & 1 << 31 != 0 {
                    self.sys_pending |= 1 << NMI;
                }
                if v & 1 << 28 != 0 {
                    self.sys_pending |= 1 << PENDSV;
                }
                if v & 1 << 27 != 0 {
                    self.sys_pending &= !(1 << PENDSV);
                }
                if v & 1 << 26 != 0 {
                    self.sys_pending |= 1 << SYSTICK;
                }
                if v & 1 << 25 != 0 {
                    self.sys_pending &= !(1 << SYSTICK);
                }
            }
            0xd08 => self.vtor = v & !0x7f,
            0xd0c => {
                if v >> 16 == 0x05fa {
                    self.aircr = v & 0x0700;
                    if v & 4 != 0 {
                        return Err(Trap::Stop(Stop::Reset));
                    }
                }
            }
            0xd14 => {
                self.ccr = v;
                self.unalign_trap = v & 8 != 0;
            }
            0xd24 => self.shcsr = v,
            0xd28 => self.cfsr &= !v,
            0xd2c => self.hfsr &= !v,
            0xd38 => self.bfar = v,
            _ => {}
        }
        self.io.irq_check = true;
        Ok(())
    }

    fn update_next(&mut self) {
        self.bus_next = self.bus.next_event(&self.io);
        self.next_ev = self.st_next.min(self.bus_next.unwrap_or(u64::MAX));
    }

    /// What is due at the time it is now.
    fn events(&mut self) {
        if self.io.time >= self.st_next {
            self.st_flag = true;
            if self.st_ctrl & 2 != 0 {
                self.sys_pending |= 1 << SYSTICK;
                self.io.irq_check = true;
            }
            let period = (self.st_reload as u64 + 1) * self.systick_unit();
            let behind = (self.io.time - self.st_next) / period + 1;
            self.st_next += behind * period;
        }
        // (a bound: an event that leaves itself due would spin here)
        for _ in 0..64 {
            match self.bus.next_event(&self.io) {
                Some(t) if t <= self.io.time => self.bus.event(&mut self.io, &mut self.mem),
                _ => break,
            }
        }
        self.update_next();
    }

    // --- exceptions ------------------------------------------------------------------------

    fn priority(&self, n: u32) -> i32 {
        match n {
            1 => -3,
            NMI => -2,
            HARDFAULT => -1,
            _ => self.prio[n as usize] as i32,
        }
    }

    /// The current execution priority (256: thread mode, nothing masked).
    pub fn exec_priority(&self) -> i32 {
        let mut p = 256;
        let mut s = self.sys_active;
        while s != 0 {
            let n = s.trailing_zeros();
            p = p.min(self.priority(n));
            s &= s - 1;
        }
        for (i, w) in self.active.iter().enumerate() {
            let mut w = *w;
            while w != 0 {
                let n = 16 + 64 * i as u32 + w.trailing_zeros();
                p = p.min(self.priority(n));
                w &= w - 1;
            }
        }
        if self.basepri != 0 {
            p = p.min(self.basepri as i32);
        }
        if self.primask {
            p = p.min(0);
        }
        if self.faultmask {
            p = p.min(-1);
        }
        p
    }

    /// The pending exception that would be taken first, were nothing masked: (number, priority).
    fn highest_pending(&self) -> Option<(u32, i32)> {
        let mut best: Option<(u32, i32)> = None;
        let mut s = self.sys_pending;
        while s != 0 {
            let n = s.trailing_zeros();
            let p = self.priority(n);
            if best.map_or(true, |b| p < b.1) {
                best = Some((n, p));
            }
            s &= s - 1;
        }
        for i in 0..8 {
            let mut w = (self.latched[i] | (self.io.lines[i] & !self.active[i])) & self.enabled[i];
            while w != 0 {
                let n = 16 + 64 * i as u32 + w.trailing_zeros();
                let p = self.priority(n);
                if best.map_or(true, |b| p < b.1) {
                    best = Some((n, p));
                }
                w &= w - 1;
            }
        }
        best
    }

    /// True if an interrupt is pending that could wake the core from WFI.
    pub fn wake_pending(&self) -> bool {
        self.highest_pending().is_some()
    }

    fn take_pending(&mut self) -> Result<(), Stop> {
        self.io.irq_check = false;
        if !self.deliver {
            return Ok(());
        }
        if let Some((n, p)) = self.highest_pending() {
            if p < self.exec_priority() {
                let ret = self.r[15];
                return self.enter(n, ret);
            }
        }
        Ok(())
    }

    /// Exception entry: stack the frame, switch to handler mode, go to the vector.
    pub fn enter(&mut self, n: u32, ret: u32) -> Result<(), Stop> {
        if self.audit {
            let mut r = self.r;
            r[15] = ret;
            self.excs.push(ExcFrame { n, r, flags: [self.n, self.z, self.c, self.v], it: self.it, depth: self.calls.len() });
        }
        let sp = self.r[13];
        let align = sp & 4 != 0;
        let fp = (sp.wrapping_sub(0x20)) & !7;
        let limit = self.sp_limit();
        if fp < limit {
            // the frame does not fit: a stack overflow fault, nothing stacked (the stack pointer goes to the limit)
            self.r[13] = limit;
            self.cfsr |= CFSR_STKOF;
            if n == HARDFAULT || self.exec_priority() <= -1 {
                return Err(Stop::Lockup(format!("stack overflow at exception entry ({n}), pc {ret:#x}")));
            }
            let to = if self.shcsr & 1 << 18 != 0 && self.priority(USAGEFAULT) < self.exec_priority() {
                USAGEFAULT
            } else {
                self.hfsr |= 1 << 30;
                HARDFAULT
            };
            self.msplim = 0;
            self.psplim = 0;
            return self.enter(to, ret);
        }
        let xpsr = self.xpsr() | (align as u32) << 9;
        let frame = [self.r[0], self.r[1], self.r[2], self.r[3], self.r[12], self.r[14], ret, xpsr];
        for (k, v) in frame.iter().enumerate() {
            self.mem.write(fp + 4 * k as u32, 4, *v);
        }
        self.forget(fp, 0x20);
        self.r[13] = fp;
        let exc_return = if self.handler_mode() { 0xfffffff1 } else { 0xfffffff9 | (self.control & 2) << 1 };
        self.switch(n, self.control & !2);
        self.r[14] = exc_return;
        self.it = 0;
        let vec = self.mem.read32(self.vtor.wrapping_add(4 * n));
        self.r[15] = vec & !1;
        self.thumb = vec & 1 != 0;
        if n < 16 {
            self.sys_pending &= !(1 << n);
            self.sys_active |= 1 << n;
        } else {
            set_bit(&mut self.latched, n - 16, false);
            set_bit(&mut self.active, n - 16, true);
        }
        *self.taken.entry(n).or_default() += 1;
        self.block_start = true;
        self.io.irq_check = true;
        Ok(())
    }

    /// Exception return: unstack the frame the EXC_RETURN value names.
    fn exc_return(&mut self, exc: u32) -> Result<u32, Trap> {
        let n = self.ipsr;
        let to_thread = exc & 8 != 0;
        if n < 16 {
            self.sys_active &= !(1 << n);
        } else {
            set_bit(&mut self.active, n - 16, false);
        }
        if n != NMI {
            self.faultmask = false;
        }
        let nested = self.sys_active != 0 || self.active.iter().any(|w| *w != 0);
        if to_thread == nested {
            return Err(Trap::Fault(CFSR_INVPC, USAGEFAULT, None));
        }
        let control = if to_thread { (self.control & !2) | (exc >> 1 & 2) } else { self.control };
        // the frame is on the stack the return goes to
        let (msp, psp) = (self.msp(), self.psp());
        let fp = if to_thread && control & 2 != 0 { psp } else { msp };
        let mut f = [0u32; 8];
        for (k, v) in f.iter_mut().enumerate() {
            *v = self.mem.read32(fp + 4 * k as u32);
        }
        let xpsr = f[7];
        if (xpsr & 0x1ff != 0) == to_thread {
            return Err(Trap::Fault(CFSR_INVPC, USAGEFAULT, None));
        }
        self.switch(xpsr & 0x1ff, control);
        self.r[13] = (fp + 0x20) | (xpsr >> 9 & 1) << 2;
        self.r[0] = f[0];
        self.r[1] = f[1];
        self.r[2] = f[2];
        self.r[3] = f[3];
        self.r[12] = f[4];
        self.r[14] = f[5];
        self.n = xpsr >> 31 != 0;
        self.z = xpsr >> 30 & 1 != 0;
        self.c = xpsr >> 29 & 1 != 0;
        self.v = xpsr >> 28 & 1 != 0;
        self.it = ((xpsr >> 25 & 3) | (xpsr >> 10 & 0x3f) << 2) as u8;
        self.thumb = xpsr >> 24 & 1 != 0;
        self.io.irq_check = true;
        self.block_start = true;
        if self.audit {
            if let Some(e) = self.excs.pop() {
                let mut now = self.r;
                now[15] = f[6] & !1;
                let same = now == e.r && [self.n, self.z, self.c, self.v] == e.flags && self.it == e.it;
                if e.n != n || !same {
                    let diff: Vec<String> =
                        (0..16).filter(|i| now[*i] != e.r[*i]).map(|i| format!("r{i} {:#x} was {:#x}", now[i], e.r[i])).collect();
                    return Err(Trap::Stop(Stop::Audit(format!(
                        "return from exception {n} (entered as {}) to {:#x}: {}{}",
                        e.n,
                        f[6],
                        diff.join(", "),
                        if [self.n, self.z, self.c, self.v] != e.flags || self.it != e.it { " flags" } else { "" }
                    ))));
                }
                self.calls.truncate(e.depth);
            }
        }
        Ok(f[6] & !1)
    }

    /// Take a fault raised by the instruction at `pc` (nothing of it has happened).
    fn fault(&mut self, bits: u32, which: u32, addr: Option<u32>, pc: u32) -> Result<(), Stop> {
        self.cfsr |= bits;
        if let Some(a) = addr {
            self.bfar = a;
        }
        let pri = self.exec_priority();
        let enabled = match which {
            USAGEFAULT => self.shcsr & 1 << 18 != 0,
            BUSFAULT => self.shcsr & 1 << 17 != 0,
            MEMMANAGE => self.shcsr & 1 << 16 != 0,
            _ => true,
        };
        let n = if enabled && self.priority(which) < pri {
            which
        } else if pri > -1 {
            self.hfsr |= 1 << 30;
            HARDFAULT
        } else {
            return Err(Stop::Lockup(format!(
                "fault (CFSR {:#x}) at {pc:#x} ({}) at priority {pri}",
                self.cfsr,
                self.func(pc)
            )));
        };
        self.enter(n, pc)
    }

    // --- execution ---------------------------------------------------------------------------

    fn fetch(&mut self, pc: u32) -> Result<Insn, Stop> {
        let (hw1, hw2) = (self.mem.peek(pc, 2) as u16, self.mem.peek(pc + 2, 2) as u16);
        let insn = decode(pc, hw1, hw2).map_err(|e| Stop::Error(format!("{e} ({})", self.func(pc))))?;
        if let Some(i) = self.idx(pc) {
            self.code[i] = insn;
            if self.code_hi == 0 {
                (self.code_lo, self.code_hi) = (pc, pc + 4);
            } else {
                self.code_lo = self.code_lo.min(pc);
                self.code_hi = self.code_hi.max(pc + 4);
            }
        }
        Ok(insn)
    }

    /// Run one instruction (or take a pending exception and run its handler's first).
    #[inline]
    pub fn step(&mut self) -> Result<(), Stop> {
        if self.io.time >= self.next_ev {
            self.events();
        }
        if self.io.irq_check {
            self.take_pending()?;
        }
        let pc = self.r[15];
        let i = (pc.wrapping_sub(self.ram_base) >> 1) as usize;
        let insn = if i < self.code.len() {
            let m = self.mark[i];
            if m != 0 && !self.pass_stop {
                if m & MARK_STOP != 0 {
                    return Err(Stop::At(self.stops[&pc].clone()));
                }
                if m & MARK_HOOK != 0 {
                    let h = self.hooks[&pc].clone();
                    h(self);
                }
                if m & MARK_STUB != 0 {
                    let f = self.stubs[&pc].clone();
                    let r0 = f(self);
                    return self.ret(r0);
                }
            }
            self.cov[i] = 1;
            let c = self.code[i];
            if c.len != 0 {
                c
            } else {
                self.fetch(pc)?
            }
        } else {
            self.fetch(pc)?
        };
        self.pass_stop = false;
        if !self.thumb {
            return self.fault(CFSR_INVSTATE, USAGEFAULT, None, pc);
        }
        self.io.time += self.io.tick.max(1) as u64;
        self.count += 1;
        let it0 = self.it;
        let mut in_it = false;
        if it0 != 0 {
            in_it = true;
            self.it = if it0 & 7 == 0 { 0 } else { (it0 & 0xe0) | ((it0 << 1) & 0x1f) };
            if !self.cond(it0 >> 4) {
                self.r[15] = pc + insn.len as u32;
                self.block(&insn, pc);
                return Ok(());
            }
        }
        match self.exec(&insn, pc, in_it) {
            Ok(next) => {
                self.r[15] = next;
                self.block(&insn, pc);
                if let Some(why) = self.io.stop.take() {
                    return Err(Stop::Bus(why));
                }
                Ok(())
            }
            Err(Trap::Fault(bits, which, addr)) => {
                self.it = it0;
                self.fault(bits, which, addr, pc)
            }
            Err(Trap::Svc) => {
                // taken after the instruction; with nothing to take it, a HardFault
                let next = pc + insn.len as u32;
                let pri = self.exec_priority();
                if self.priority(SVCALL) < pri {
                    self.enter(SVCALL, next)
                } else if pri > -1 {
                    self.hfsr |= 1 << 30;
                    self.enter(HARDFAULT, next)
                } else {
                    Err(Stop::Lockup(format!("SVC at {pc:#x} at priority {pri}")))
                }
            }
            Err(Trap::Stop(s)) => {
                if matches!(s, Stop::Wfi | Stop::Reset) {
                    self.r[15] = pc + insn.len as u32;
                    self.block_start = true;
                } else {
                    self.it = it0;
                }
                if s == Stop::Spin {
                    // (the time it took is given back: the loop is run again when something happens)
                    self.io.time -= self.io.tick.max(1) as u64;
                    self.count -= 1;
                }
                Err(s)
            }
        }
    }

    /// Where QEMU would start a new translation block.
    #[inline]
    fn block(&mut self, insn: &Insn, pc: u32) {
        if self.block_start {
            self.tb_start = pc;
        }
        let next = pc + insn.len as u32;
        self.block_start = if insn.ends_block() || self.r[15] != next {
            true
        } else {
            let off = next - (self.tb_start & !0x3ff);
            off >= 0x400 || (off == 0x3fe && is32(self.mem.peek(next, 2) as u16))
        };
    }

    /// Run until a stop, or until `until` (time) or `count` instructions have passed.  A core asleep
    /// in WFI (or in a branch to itself, with `spin_sleeps`) jumps to the next event; with none to come
    /// it stays asleep: Stop::Wfi or Stop::Spin, and the next run finds it so.
    pub fn run(&mut self, until: u64, count: u64) -> Stop {
        let end = self.count.saturating_add(count);
        loop {
            if let Some(s) = self.asleep.clone() {
                // asleep: to the next event, as often as it takes for something to be pending (a WFI ends
                // when something is pending; a branch to itself only when it is taken)
                loop {
                    if self.wake_pending() {
                        self.io.irq_check = true;
                        if s == Stop::Wfi || self.would_take() {
                            self.asleep = None;
                            break;
                        }
                    }
                    let next = self.next_ev;
                    if next == u64::MAX {
                        return s;
                    }
                    if next >= until {
                        self.io.time = self.io.time.max(until);
                        return Stop::Limit;
                    }
                    self.io.time = self.io.time.max(next);
                    self.events();
                }
            }
            if self.io.time >= until || self.count >= end {
                return Stop::Limit;
            }
            match self.step() {
                Ok(()) => {}
                Err(s @ (Stop::Wfi | Stop::Spin)) if !(self.wfi_stops && s == Stop::Wfi) => self.asleep = Some(s),
                Err(s) => return s,
            }
        }
    }

    /// Asleep in WFI (Stop::Wfi) or in a branch to itself (Stop::Spin), as `run` left the core.
    pub fn sleeping(&self) -> Option<&Stop> {
        self.asleep.as_ref()
    }

    /// True if the highest pending exception would be taken now.
    fn would_take(&self) -> bool {
        self.deliver && self.highest_pending().map_or(false, |(_, p)| p < self.exec_priority())
    }

    /// Call a function with up to four arguments on the stack at `sp`, until it returns to `ret` (an
    /// address nothing runs at); the registers are put back afterwards.
    pub fn call(&mut self, func: u32, args: &[u32], sp: u32, ret: u32, count: u64) -> Result<u32, Stop> {
        let saved = self.context();
        // (a core asleep is woken for the call and put back to sleep after it)
        let asleep = self.asleep.take();
        let (calls, excs) = (self.calls.len(), self.excs.len());
        for (i, a) in args.iter().enumerate() {
            self.r[i] = *a;
        }
        self.r[13] = sp;
        self.r[14] = ret | 1;
        self.r[15] = func & !1;
        self.it = 0;
        let had = self.stops.get(&ret).cloned();
        self.set_stop(ret, "return");
        self.block_start = true;
        let stop = self.run(u64::MAX, count);
        match had {
            Some(name) => self.set_stop(ret, &name),
            None => self.del_stop(ret),
        }
        let r0 = self.r[0];
        self.set_context(&saved);
        self.asleep = asleep;
        self.calls.truncate(calls);
        self.excs.truncate(excs);
        self.block_start = true;
        match stop {
            Stop::At(s) if s == "return" => Ok(r0),
            s => Err(s),
        }
    }

    // --- coverage ------------------------------------------------------------------------------

    /// The address ranges of the instructions run so far.
    pub fn covered(&self) -> Vec<(u32, u32)> {
        let mut out: Vec<(u32, u32)> = Vec::new();
        for (i, c) in self.cov.iter().enumerate() {
            if *c != 0 {
                let lo = self.ram_base + 2 * i as u32;
                let hi = lo + (self.code[i].len as u32).max(2);
                match out.last_mut() {
                    Some(last) if last.1 >= lo => last.1 = last.1.max(hi),
                    _ => out.push((lo, hi)),
                }
            }
        }
        out
    }

    // --- instructions ----------------------------------------------------------------------------

    /// BXWritePC and LoadWritePC: an exception return in handler mode, else a branch that sets T.
    fn bx(&mut self, v: u32) -> Result<u32, Trap> {
        if self.handler_mode() && v >> 24 == 0xff {
            return self.exc_return(v);
        }
        self.thumb = v & 1 != 0;
        if self.audit {
            self.audit_return(v & !1)?;
        }
        Ok(v & !1)
    }

    fn audit_call(&mut self, ret: u32, from: u32) {
        if self.calls.len() > 4096 {
            self.calls.drain(..2048);
        }
        let mut regs = [0u32; 8];
        regs.copy_from_slice(&self.r[4..12]);
        self.calls.push(CallFrame { ret, sp: self.r[13], regs, from });
    }

    fn audit_return(&mut self, to: u32) -> Result<(), Trap> {
        let floor = self.excs.last().map_or(0, |e| e.depth);
        let Some(k) = (floor..self.calls.len()).rev().find(|k| self.calls[*k].ret == to && self.calls[*k].sp == self.r[13]) else {
            return Ok(());
        };
        let f = self.calls[k].clone();
        self.calls.truncate(k);
        if f.regs[..] != self.r[4..12] {
            let diff: Vec<String> = (0..8)
                .filter(|i| f.regs[*i] != self.r[4 + i])
                .map(|i| format!("r{} {:#x} was {:#x}", 4 + i, self.r[4 + i], f.regs[i]))
                .collect();
            return Err(Trap::Stop(Stop::Audit(format!(
                "return to {to:#x} ({}) from the call at {:#x}: {}",
                self.func(to),
                f.from,
                diff.join(", ")
            ))));
        }
        Ok(())
    }

    #[inline]
    fn nz(&mut self, v: u32) {
        self.n = v >> 31 != 0;
        self.z = v == 0;
    }

    #[inline]
    fn add_c(&mut self, x: u32, y: u32, c: bool, s: bool) -> u32 {
        let (r1, c1) = x.overflowing_add(y);
        let (r, c2) = r1.overflowing_add(c as u32);
        if s {
            self.nz(r);
            self.c = c1 || c2;
            self.v = (!(x ^ y) & (x ^ r)) >> 31 != 0;
        }
        r
    }

    /// Shift_C with the carry as it is now for "unchanged".
    #[inline]
    fn shift(&self, x: u32, ty: u8, n: u32) -> (u32, bool) {
        if n == 0 {
            return (x, self.c);
        }
        match ty {
            LSL => match n {
                1..=31 => (x << n, x >> (32 - n) & 1 != 0),
                32 => (0, x & 1 != 0),
                _ => (0, false),
            },
            LSR => match n {
                1..=31 => (x >> n, x >> (n - 1) & 1 != 0),
                32 => (0, x >> 31 != 0),
                _ => (0, false),
            },
            ASR => match n {
                1..=31 => (((x as i32) >> n) as u32, x >> (n - 1) & 1 != 0),
                _ => (((x as i32) >> 31) as u32, x >> 31 != 0),
            },
            ROR => {
                let r = x.rotate_right(n & 31);
                (r, r >> 31 != 0)
            }
            _ => ((self.c as u32) << 31 | x >> 1, x & 1 != 0),
        }
    }

    /// The result of a data-processing operation; the flags are set here.
    fn dp(&mut self, kind: u8, a: u32, b: u32, s: bool, carry: bool) -> u32 {
        match kind {
            dp::AND | dp::TST => {
                let r = a & b;
                if s {
                    self.nz(r);
                    self.c = carry;
                }
                r
            }
            dp::EOR | dp::TEQ => {
                let r = a ^ b;
                if s {
                    self.nz(r);
                    self.c = carry;
                }
                r
            }
            dp::ORR => {
                let r = a | b;
                if s {
                    self.nz(r);
                    self.c = carry;
                }
                r
            }
            dp::BIC => {
                let r = a & !b;
                if s {
                    self.nz(r);
                    self.c = carry;
                }
                r
            }
            dp::ORN => {
                let r = a | !b;
                if s {
                    self.nz(r);
                    self.c = carry;
                }
                r
            }
            dp::MOV => {
                if s {
                    self.nz(b);
                    self.c = carry;
                }
                b
            }
            dp::MVN => {
                let r = !b;
                if s {
                    self.nz(r);
                    self.c = carry;
                }
                r
            }
            dp::ADD | dp::CMN => self.add_c(a, b, false, s),
            dp::SUB | dp::CMP => self.add_c(a, !b, true, s),
            dp::RSB => self.add_c(!a, b, true, s),
            dp::ADC => self.add_c(a, b, self.c, s),
            _ => self.add_c(a, !b, self.c, s),
        }
    }

    /// Write a data-processing result: a register, or the program counter (a branch).
    #[inline]
    fn dp_write(&mut self, i: &Insn, r: u32, next: u32) -> Result<u32, Trap> {
        if i.ra >= dp::TST {
            return Ok(next);
        }
        if i.rd == 15 {
            if self.audit {
                self.audit_return(r & !1)?;
            }
            return Ok(r & !1);
        }
        if i.rd == 13 && r & !3 < self.sp_limit() && r < self.r[13] {
            return Err(Trap::Fault(CFSR_STKOF, USAGEFAULT, None));
        }
        self.set_reg(i.rd, r);
        Ok(next)
    }

    fn exec(&mut self, i: &Insn, pc: u32, in_it: bool) -> Result<u32, Trap> {
        let next = pc + i.len as u32;
        let s = i.s == S_YES || (i.s == S_NOT_IT && !in_it);
        match i.op {
            Op::DpI => {
                let a = self.reg(i.rn);
                let carry = match i.f {
                    0 => self.c,
                    1 => false,
                    _ => true,
                };
                let r = self.dp(i.ra, a, i.imm, s, carry);
                self.dp_write(i, r, next)
            }
            Op::DpR => {
                let a = self.reg(i.rn);
                let (b, carry) = self.shift(self.reg(i.rm), i.sh, i.imm);
                let r = self.dp(i.ra, a, b, s, carry);
                self.dp_write(i, r, next)
            }
            Op::ShiftR => {
                let n = self.reg(i.rm) & 0xff;
                let x = self.reg(i.rn);
                let (r, carry) = if i.ra == ROR && n != 0 && n & 31 == 0 { (x, x >> 31 != 0) } else { self.shift(x, i.ra, n) };
                if s {
                    self.nz(r);
                    self.c = carry;
                }
                self.set_reg(i.rd, r);
                Ok(next)
            }
            Op::Adr => {
                self.set_reg(i.rd, i.imm);
                Ok(next)
            }
            Op::Mul => {
                let r = self.reg(i.rn).wrapping_mul(self.reg(i.rm));
                if s {
                    self.nz(r);
                }
                self.set_reg(i.rd, r);
                Ok(next)
            }
            Op::Mla => {
                let r = self.reg(i.rn).wrapping_mul(self.reg(i.rm)).wrapping_add(self.reg(i.ra));
                self.set_reg(i.rd, r);
                Ok(next)
            }
            Op::Mls => {
                let r = self.reg(i.ra).wrapping_sub(self.reg(i.rn).wrapping_mul(self.reg(i.rm)));
                self.set_reg(i.rd, r);
                Ok(next)
            }
            Op::Umull | Op::Smull | Op::Umlal => {
                let (x, y) = (self.reg(i.rn), self.reg(i.rm));
                let p = match i.op {
                    Op::Smull => (x as i32 as i64).wrapping_mul(y as i32 as i64) as u64,
                    Op::Umull => x as u64 * y as u64,
                    _ => (x as u64 * y as u64).wrapping_add((self.reg(i.rd) as u64) << 32 | self.reg(i.ra) as u64),
                };
                self.set_reg(i.ra, p as u32);
                self.set_reg(i.rd, (p >> 32) as u32);
                Ok(next)
            }
            Op::Sdiv | Op::Udiv => {
                let (x, y) = (self.reg(i.rn), self.reg(i.rm));
                let r = if y == 0 {
                    if self.ccr & 0x10 != 0 {
                        return Err(Trap::Fault(CFSR_DIVBYZERO, USAGEFAULT, None));
                    }
                    0
                } else if i.op == Op::Udiv {
                    x / y
                } else {
                    (x as i32).wrapping_div(y as i32) as u32
                };
                self.set_reg(i.rd, r);
                Ok(next)
            }
            Op::Smulxy => {
                let half = |v: u32, high: bool| (if high { v >> 16 } else { v }) as i16 as i32;
                let p = half(self.reg(i.rn), i.f & 2 != 0).wrapping_mul(half(self.reg(i.rm), i.f & 1 != 0));
                // (the accumulate sets Q on overflow; nothing here reads Q)
                let r = if i.ra == 15 { p as u32 } else { (p as u32).wrapping_add(self.reg(i.ra)) };
                self.set_reg(i.rd, r);
                Ok(next)
            }
            Op::Clz => {
                let r = self.reg(i.rm).leading_zeros();
                self.set_reg(i.rd, r);
                Ok(next)
            }
            Op::Rev => {
                let r = self.reg(i.rm).swap_bytes();
                self.set_reg(i.rd, r);
                Ok(next)
            }
            Op::Rev16 => {
                let x = self.reg(i.rm);
                self.set_reg(i.rd, (x >> 8 & 0x00ff00ff) | (x << 8 & 0xff00ff00));
                Ok(next)
            }
            Op::Ext => {
                let x = self.reg(i.rm).rotate_right(i.imm);
                let e = match i.ra {
                    SXTH => x as i16 as i32 as u32,
                    UXTH => x & 0xffff,
                    SXTB => x as i8 as i32 as u32,
                    _ => x & 0xff,
                };
                let r = if i.rn == 15 { e } else { self.reg(i.rn).wrapping_add(e) };
                self.set_reg(i.rd, r);
                Ok(next)
            }
            Op::Bfi => {
                let mask = (if i.ra == 32 { u32::MAX } else { (1u32 << i.ra) - 1 }) << i.imm;
                let src = if i.rn == 15 { 0 } else { self.reg(i.rn) << i.imm };
                let r = (self.reg(i.rd) & !mask) | (src & mask);
                self.set_reg(i.rd, r);
                Ok(next)
            }
            Op::Ubfx => {
                let mask = if i.ra == 32 { u32::MAX } else { (1u32 << i.ra) - 1 };
                let r = (self.reg(i.rn) >> i.imm) & mask;
                self.set_reg(i.rd, r);
                Ok(next)
            }
            Op::Sbfx => {
                let sh = 32 - i.ra as u32;
                let r = (((self.reg(i.rn) >> i.imm) << sh) as i32 >> sh) as u32;
                self.set_reg(i.rd, r);
                Ok(next)
            }
            Op::LdrI | Op::StrI => {
                let base = self.reg(i.rn);
                let offs = if i.f & F_ADD != 0 { base.wrapping_add(i.imm) } else { base.wrapping_sub(i.imm) };
                let addr = if i.f & F_INDEX != 0 { offs } else { base };
                let wback = i.f & F_WBACK != 0;
                if wback && i.rn == 13 && offs < self.r[13] && offs < self.sp_limit() {
                    return Err(Trap::Fault(CFSR_STKOF, USAGEFAULT, None));
                }
                if i.op == Op::StrI {
                    let v = self.reg(i.rd);
                    self.store(addr, 1 << (i.ra & 3), v)?;
                    if wback {
                        self.set_reg(i.rn, offs);
                    }
                    Ok(next)
                } else {
                    let v = self.load_x(addr, i.ra)?;
                    if i.rd == 15 {
                        // the write-back first: an exception return unstacks from the stack pointer
                        let old = self.r[i.rn as usize];
                        if wback {
                            self.set_reg(i.rn, offs);
                        }
                        return self.bx(v).inspect_err(|_| self.r[i.rn as usize] = old);
                    }
                    if wback {
                        self.set_reg(i.rn, offs);
                    }
                    self.set_reg(i.rd, v);
                    Ok(next)
                }
            }
            Op::LdrR | Op::StrR => {
                let addr = self.reg(i.rn).wrapping_add(self.reg(i.rm) << i.imm);
                if i.op == Op::StrR {
                    let v = self.reg(i.rd);
                    self.store(addr, 1 << (i.ra & 3), v)?;
                    Ok(next)
                } else {
                    let v = self.load_x(addr, i.ra)?;
                    if i.rd == 15 {
                        return self.bx(v);
                    }
                    self.set_reg(i.rd, v);
                    Ok(next)
                }
            }
            Op::LdrLit => {
                let v = self.load_x(i.imm, i.ra)?;
                if i.rd == 15 {
                    return self.bx(v);
                }
                self.set_reg(i.rd, v);
                Ok(next)
            }
            Op::LdrdI | Op::StrdI => {
                let base = self.reg(i.rn);
                let offs = if i.f & F_ADD != 0 { base.wrapping_add(i.imm) } else { base.wrapping_sub(i.imm) };
                let addr = if i.f & F_INDEX != 0 { offs } else { base };
                let wback = i.f & F_WBACK != 0;
                if wback && i.rn == 13 && offs < self.r[13] && offs < self.sp_limit() {
                    return Err(Trap::Fault(CFSR_STKOF, USAGEFAULT, None));
                }
                if i.op == Op::StrdI {
                    let (a, b) = (self.reg(i.rd), self.reg(i.ra));
                    self.store(addr, 4, a)?;
                    self.store(addr.wrapping_add(4), 4, b)?;
                } else {
                    let a = self.load(addr, 4)?;
                    let b = self.load(addr.wrapping_add(4), 4)?;
                    self.set_reg(i.rd, a);
                    self.set_reg(i.ra, b);
                }
                if wback {
                    self.set_reg(i.rn, offs);
                }
                Ok(next)
            }
            Op::Ldmia | Op::Ldmdb => {
                let n = i.imm.count_ones();
                let base = self.reg(i.rn);
                let start = if i.op == Op::Ldmia { base } else { base.wrapping_sub(4 * n) };
                let mut vals = [0u32; 16];
                let mut a = start;
                for k in 0..16 {
                    if i.imm >> k & 1 != 0 {
                        vals[k] = self.load(a, 4)?;
                        a = a.wrapping_add(4);
                    }
                }
                // the registers and the base first: an exception return unstacks from the new stack pointer
                // and its frame's registers win; when the return faults, nothing has happened
                let saved = self.r;
                for k in 0..15 {
                    if i.imm >> k & 1 != 0 {
                        self.set_reg(k as u8, vals[k]);
                    }
                }
                if i.f & F_WBACK != 0 {
                    self.set_reg(i.rn, if i.op == Op::Ldmia { a } else { start });
                }
                if i.imm >> 15 != 0 {
                    return self.bx(vals[15]).inspect_err(|_| self.r = saved);
                }
                Ok(next)
            }
            Op::Stmia | Op::Stmdb => {
                let n = i.imm.count_ones();
                let base = self.reg(i.rn);
                let start = if i.op == Op::Stmia { base } else { base.wrapping_sub(4 * n) };
                if i.rn == 13 && i.op == Op::Stmdb && start < self.sp_limit() {
                    return Err(Trap::Fault(CFSR_STKOF, USAGEFAULT, None));
                }
                let mut a = start;
                for k in 0..16u8 {
                    if i.imm >> k & 1 != 0 {
                        let v = self.reg(k);
                        self.store(a, 4, v)?;
                        a = a.wrapping_add(4);
                    }
                }
                if i.f & F_WBACK != 0 {
                    self.set_reg(i.rn, if i.op == Op::Stmia { a } else { start });
                }
                Ok(next)
            }
            Op::B => {
                if i.imm == pc && self.spin_sleeps && !in_it {
                    return Err(Trap::Stop(Stop::Spin));
                }
                Ok(i.imm)
            }
            Op::Bcond => Ok(if self.cond(i.ra) { i.imm } else { next }),
            Op::Bl => {
                self.r[14] = next | 1;
                if self.audit {
                    self.audit_call(next, pc);
                }
                if let Some(c) = &mut self.call_counts {
                    *c.entry((i.imm, pc)).or_default() += 1;
                }
                Ok(i.imm)
            }
            Op::BlxR => {
                let t = self.reg(i.rm);
                self.thumb = t & 1 != 0;
                self.r[14] = next | 1;
                if self.audit {
                    self.audit_call(next, pc);
                }
                if let Some(c) = &mut self.call_counts {
                    *c.entry((t & !1, pc)).or_default() += 1;
                }
                Ok(t & !1)
            }
            Op::Bx => {
                let t = self.reg(i.rm);
                self.bx(t)
            }
            Op::Cbz => Ok(if (self.reg(i.rn) == 0) != (i.f != 0) { i.imm } else { next }),
            Op::Tb => {
                let base = self.reg(i.rn);
                let idx = self.reg(i.rm);
                let off = if i.f != 0 { self.load(base.wrapping_add(idx << 1), 2)? } else { self.load(base.wrapping_add(idx), 1)? };
                Ok(pc.wrapping_add(4).wrapping_add(off << 1))
            }
            Op::It => {
                self.it = i.imm as u8;
                Ok(next)
            }
            Op::Nop | Op::Barrier | Op::Wfe => Ok(next),
            Op::Wfi => {
                if self.wfi_stops || !self.wake_pending() {
                    return Err(Trap::Stop(Stop::Wfi));
                }
                Ok(next)
            }
            Op::Cps => {
                let on = i.f != 0;
                if i.imm & 2 != 0 {
                    self.primask = on;
                }
                if i.imm & 1 != 0 && (!on || self.exec_priority() > -1) {
                    self.faultmask = on;
                }
                self.io.irq_check = true;
                Ok(next)
            }
            Op::Mrs => {
                let v = match i.imm {
                    0..=7 => {
                        let mut v = 0;
                        if i.imm & 1 != 0 {
                            v |= self.ipsr;
                        }
                        if i.imm & 4 == 0 {
                            v |= self.xpsr() & 0xf8000000;
                        }
                        v
                    }
                    8 => self.msp(),
                    9 => self.psp(),
                    10 => self.msplim,
                    11 => self.psplim,
                    16 => self.primask as u32,
                    17 | 18 => self.basepri as u32,
                    19 => self.faultmask as u32,
                    20 => self.control,
                    _ => return Err(Trap::Stop(Stop::Error(format!("MRS of system register {} at {pc:#x}", i.imm)))),
                };
                self.set_reg(i.rd, v);
                Ok(next)
            }
            Op::Msr => {
                let v = self.reg(i.rn);
                match i.imm {
                    0..=3 => {
                        if i.f & 2 != 0 {
                            self.n = v >> 31 != 0;
                            self.z = v >> 30 & 1 != 0;
                            self.c = v >> 29 & 1 != 0;
                            self.v = v >> 28 & 1 != 0;
                        }
                    }
                    8 => self.set_msp(v),
                    9 => self.set_psp(v),
                    10 => self.msplim = v & !7,
                    11 => self.psplim = v & !7,
                    16 => self.primask = v & 1 != 0,
                    17 => self.basepri = v as u8 & 0xe0,
                    18 => {
                        let b = v as u8 & 0xe0;
                        if b != 0 && (b < self.basepri || self.basepri == 0) {
                            self.basepri = b;
                        }
                    }
                    19 => {
                        if v & 1 == 0 || self.exec_priority() > -1 {
                            self.faultmask = v & 1 != 0;
                        }
                    }
                    20 => {
                        let c = if self.handler_mode() { (self.control & 2) | (v & 1) } else { v & 3 };
                        self.switch(self.ipsr, c);
                    }
                    _ => return Err(Trap::Stop(Stop::Error(format!("MSR of system register {} at {pc:#x}", i.imm)))),
                }
                self.io.irq_check = true;
                Ok(next)
            }
            Op::Svc => Err(Trap::Svc),
            Op::Udf => Err(Trap::Fault(CFSR_UNDEFINSTR, USAGEFAULT, None)),
            Op::Bkpt => Err(Trap::Stop(Stop::At(format!("bkpt {:#x}", i.imm)))),
        }
    }

    #[inline]
    fn load_x(&mut self, addr: u32, size: u8) -> Result<u32, Trap> {
        let v = self.load(addr, 1 << (size & 3))?;
        Ok(match size {
            x if x == SIGNED => v as i8 as i32 as u32,
            x if x == SIGNED | 1 => v as i16 as i32 as u32,
            _ => v,
        })
    }

    /// Decode the instruction at `pc` without running it (for the checks of the decoder).
    pub fn insn_at(&self, pc: u32) -> Result<Insn, String> {
        decode(pc, self.mem.peek(pc, 2) as u16, self.mem.peek(pc + 2, 2) as u16)
    }

    /// Make an exception pending (the model's doing: NMI, PendSV, SysTick; or an external interrupt's latch).
    pub fn pend(&mut self, n: u32) {
        if n < 16 {
            self.sys_pending |= 1 << n;
        } else {
            set_bit(&mut self.latched, n - 16, true);
        }
        self.io.irq_check = true;
    }

    /// Enable an external interrupt as a write to the NVIC would.
    pub fn irq_enable(&mut self, irq: u32) {
        set_bit(&mut self.enabled, irq, true);
        self.io.irq_check = true;
    }

    pub fn irq_enabled(&self, irq: u32) -> bool {
        bit(&self.enabled, irq)
    }

    pub fn in_handler(&self) -> bool {
        self.handler_mode()
    }
}
