// SPDX-License-Identifier: BSD-2-Clause-Patent
//! Interpreter for CSU_PM's Xtensa core (Cadence LX7 "pm_core_v1").
//!
//! The core as CIX's firmware uses it (firmware/csupm/README.md): windowed
//! ABI with 64 address registers, density, zero-overhead loops, MUL16/
//! MUL32/MUL32_HIGH, DIV32, NSA, MIN/MAX, SEXT, booleans, SALT/SALTU, a
//! single-precision FPU with the double-precision extension (coprocessor 0;
//! 64-bit FP registers, single-precision operations use the low half),
//! exclusive-access state (GETEX), XEA2 exceptions and interrupts (32
//! lines, EXCM level 5, NMI 7), three CCOUNT/CCOMPARE timers, relocatable
//! vectors, and 8-byte FLIX bundles (flix.rs), which run as one
//! instruction: all slots read, then write, then transfer control.  MPU,
//! cache and TLB instructions are accepted and do nothing.
//!
//! Memory outside RAM goes to the bus; every bus write is recorded in
//! `writes`.  `stops` end a run at an address; `stubs` replace a function
//! at its entry, before its ENTRY, with the windowed calling convention.
//! Every instruction executed is marked for the coverage reports.

pub mod decode;
pub mod flix;

use crate::elf::{Elf, STT_FUNC, STT_NOTYPE, STT_OBJECT};
use decode::{Bin, Cond, FpGroup, Insn, K};
use std::collections::{BTreeSet, HashMap};
use std::rc::Rc;

// special registers
pub const SAR: u32 = 3;
pub const BR: u32 = 4;
pub const EPC1: u32 = 177;
pub const DEPC: u32 = 192;
pub const EPS2: u32 = 194;
pub const CPENABLE: u32 = 224;
pub const INTSET: u32 = 226;
pub const INTCLEAR: u32 = 227;
pub const INTENABLE: u32 = 228;
pub const PS: u32 = 230;
pub const VECBASE: u32 = 231;
pub const EXCCAUSE: u32 = 232;
pub const CCOUNT: u32 = 234;
#[allow(dead_code)]
pub const PRID: u32 = 235;
pub const EXCVADDR: u32 = 238;
use decode::{LBEG, LCOUNT, LEND, WINDOWBASE, WINDOWSTART};

// PS fields
pub const PS_INTLEVEL: u32 = 0xf;
pub const PS_EXCM: u32 = 1 << 4;
pub const PS_UM: u32 = 1 << 5;
pub const PS_WOE: u32 = 1 << 18;
const PS_OWB_SHIFT: u32 = 8;
const PS_CALLINC_SHIFT: u32 = 16;

// exception causes
const ILLEGAL: u32 = 0;
const SYSCALL: u32 = 1;
const LEVEL1_INT: u32 = 4;
const ALLOCA: u32 = 5;
const DIVIDE_BY_ZERO: u32 = 6;
const CP0_DISABLED: u32 = 32;

// vector offsets from VECBASE (CIX's ELF: _WindowOverflow4 is VECBASE)
const VEC_OVERFLOW: [u32; 3] = [0x000, 0x080, 0x100];
const VEC_UNDERFLOW: [u32; 3] = [0x040, 0x0c0, 0x140];
const VEC_LEVEL: [u32; 8] = [0, 0, 0x17c, 0x19c, 0x1bc, 0x1dc, 0x1fc, 0x21c];
const VEC_KERNEL: u32 = 0x23c;
const VEC_USER: u32 = 0x25c;
const VEC_DOUBLE: u32 = 0x27c;
const INTLEVEL: [u8; 32] = [1, 1, 1, 1, 2, 3, 1, 4, 5, 4, 5, 5, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 7];
const EXCM_LEVEL: u32 = 5;
// CCOMPARE0..2 interrupts: CIX's image takes its tick from CCOMPARE1 on 7
// and programs CCOMPARE2 with _hw_timer_callback on 8; 6 for CCOMPARE0 is
// a guess
pub const TIMER_INT: [u32; 3] = [6, 7, 8];

/// What the bus sees of the core.
#[derive(Clone, Debug, Default)]
pub struct Io {
    pub ccount: u64,
    /// level-sensitive interrupt lines driven by the SoC model
    pub int_lines: u32,
    /// the instruction being run
    pub pc: u32,
    /// bus writes made so far, the one being made included
    pub nwrites: usize,
}

pub trait Bus {
    fn read(&mut self, io: &mut Io, addr: u32, size: u32) -> u64;
    fn write(&mut self, io: &mut Io, addr: u32, size: u32, value: u64);
    /// Cycles until the model's next timed event, if it has one.
    fn next_event(&mut self, _io: &Io) -> Option<u64> {
        None
    }
    /// Time went by (a busy-wait done at once, or WAITI).
    fn advance(&mut self, _io: &mut Io, _cycles: u64) {}
}

enum Trap {
    Exc(u32, Option<u32>),
    Vector(u32),
    Stop(String),
}

#[derive(Clone, Copy, Debug)]
pub struct Write {
    pub pc: u32,
    pub addr: u32,
    #[allow(dead_code)]
    pub size: u8,
    pub value: u64,
}

#[derive(Clone)]
struct Ram {
    lo: u32,
    hi: u32,
    buf: Vec<u8>,
    /// index + 1 of the decoded instruction at each address
    code: Vec<u32>,
    /// bit 0: a stop or stub at this address
    mark: Vec<u8>,
    /// executed
    cov: Vec<u8>,
}

#[derive(Clone, Copy)]
struct Div {
    b: f64,
    breg: u8,
    a: Option<f64>,
}

pub type Stub<B> = Rc<dyn Fn(&mut Xtensa<B>) -> Option<u32>>;

#[derive(Clone)]
pub struct Xtensa<B: Bus> {
    pub bus: B,
    pub io: Io,
    pub ar: [u32; 64],
    snap: [u32; 64],
    in_bundle: bool,
    pub wb4: u32,
    pub ws: u32,
    pub sr: [u32; 256],
    pub ur: [u32; 256],
    pub fr: [u64; 16],
    pub br: u32,
    pub sar: u32,
    pub lbeg: u32,
    pub lend: u32,
    pub lcount: u32,
    pub ccompare: [u32; 3],
    /// edge, software and timer interrupts
    pub int_latched: u32,
    pub intenable: u32,
    pub pc: u32,
    ram: Vec<Ram>,
    insns_cache: Vec<Insn>,
    bundle_ops: Vec<Insn>,
    cmin: u32,
    cmax: u32,
    pub writes: Vec<Write>,
    stops: HashMap<u32, String>,
    stubs: HashMap<u32, Stub<B>>,
    pub stop_write: Option<(u32, u64)>,
    /// The window audit (a debugging aid, off unless Some): at every
    /// windowed call the caller's registers that the call must preserve,
    /// by (return address, caller's stack pointer); checked when the call
    /// returns.  A mismatch stops the run: the firmware's window handlers,
    /// its interrupt entry and exit, or this interpreter lost a register.
    pub audit: Option<HashMap<(u32, u32), Vec<u32>>>,
    /// ... and at every interrupt the interrupted code's a0-a15, SAR and
    /// loop registers, by (its pc, its stack pointer); checked when an
    /// RFI or RFE goes back there.
    audit_irq: HashMap<(u32, u32), Vec<u32>>,
    /// stop on a write into this range of RAM (a debugging aid)
    pub watch: Option<(u32, u32)>,
    /// ... of a value with these bytes only, when given
    pub watch_byte: Option<u8>,
    /// CCOUNT a WAITI may not sleep past
    pub until: Option<u64>,
    pub waiting: bool,
    pub insns: u64,
    div: Option<Div>,
    exclusive: u32,
    monitor: bool,
    // symbols
    pub symbols: Vec<(u32, String, u32)>,
    sym_addrs: Vec<u32>,
    pub byname: HashMap<String, u32>,
    pub objects: HashMap<String, (u32, u32)>,
    /// the first object of each name in the symbol table
    pub objects_first: HashMap<String, u32>,
    /// the first symbol of each name, whatever its type: (value, size)
    pub all_syms: HashMap<String, (u32, u32)>,
    /// objects in symbol-table order: (name, value, size)
    pub object_list: Vec<(String, u32, u32)>,
    pub overlay_window: Option<(u32, u32)>,
    pub overlay_syms: HashMap<u32, Vec<(u32, String)>>,
    /// instructions run in the overlay window: (overlay, address)
    pub overlay_cov: BTreeSet<(u32, u32)>,
    pub entry: u32,
    pub elf_path: String,
    /// the last instructions run (a ring), for a crash's story
    trail: [u32; 64],
    trail_at: usize,
}

fn sx(v: u32, bits: u32) -> u32 {
    let sh = 32 - bits;
    (((v << sh) as i32) >> sh) as u32
}

fn f2u(x: f64) -> u64 {
    (x as f32).to_bits() as u64
}

fn to_int(f: f64, signed: bool) -> u32 {
    if f.is_nan() {
        return if signed { 0x7fffffff } else { 0xffffffff };
    }
    let (lo, hi) = if signed { (-2147483648.0, 2147483647.0) } else { (0.0, 4294967295.0) };
    if f > hi {
        return hi as i64 as u32;
    }
    if f < lo {
        return lo as i64 as u32;
    }
    f as i64 as u32
}

fn fcmp(op: u8, a: f64, b: f64) -> bool {
    let un = a.is_nan() || b.is_nan();
    match op {
        1 => un,
        2 => !un && a == b,
        3 => un || a == b,
        4 => !un && a < b,
        5 => un || a < b,
        6 => !un && a <= b,
        _ => un || a <= b,
    }
}

fn round(op: u8, x: f64) -> f64 {
    match op {
        8 => x.round_ties_even(),
        10 => x.floor(),
        11 => x.ceil(),
        _ => x.trunc(),
    }
}

impl<B: Bus> Xtensa<B> {
    pub fn new(bus: B) -> Self {
        let mut sr = [0u32; 256];
        sr[VECBASE as usize] = 0x20000400;
        sr[PS as usize] = 0x1f;
        // CPENABLE: CIX's main() uses the FPU before the scheduler, and only
        // FreeRTOS's task switch writes CPENABLE, so coprocessor 0 must be
        // enabled out of reset (otherwise CIX's firmware stops in _xt_panic).
        sr[CPENABLE as usize] = 1;
        Xtensa {
            bus,
            io: Io::default(),
            ar: [0; 64],
            snap: [0; 64],
            in_bundle: false,
            wb4: 0,
            ws: 1,
            sr,
            ur: [0; 256],
            fr: [0; 16],
            br: 0,
            sar: 0,
            lbeg: 0,
            lend: 0,
            lcount: 0,
            ccompare: [0; 3],
            int_latched: 0,
            intenable: 0,
            pc: 0,
            ram: Vec::new(),
            insns_cache: Vec::new(),
            bundle_ops: Vec::new(),
            cmin: u32::MAX,
            cmax: 0,
            writes: Vec::new(),
            stops: HashMap::new(),
            stubs: HashMap::new(),
            stop_write: None,
            audit: None,
            audit_irq: HashMap::new(),
            watch: None,
            watch_byte: None,
            until: None,
            waiting: false,
            insns: 0,
            div: None,
            exclusive: 0,
            monitor: false,
            symbols: Vec::new(),
            sym_addrs: Vec::new(),
            byname: HashMap::new(),
            objects: HashMap::new(),
            objects_first: HashMap::new(),
            all_syms: HashMap::new(),
            object_list: Vec::new(),
            overlay_window: None,
            overlay_syms: HashMap::new(),
            overlay_cov: BTreeSet::new(),
            entry: 0,
            elf_path: String::new(),
            trail: [0; 64],
            trail_at: 0,
        }
    }

    // --- memory -------------------------------------------------------------

    pub fn add_ram(&mut self, base: u32, size: u32) {
        let n = size as usize;
        self.ram.push(Ram { lo: base, hi: base + size, buf: vec![0; n], code: vec![0; n], mark: vec![0; n], cov: vec![0; n] });
        self.ram.sort_by_key(|r| r.lo);
    }

    #[inline]
    fn ram_at(&self, addr: u32, size: u32) -> Option<(usize, usize)> {
        for (i, r) in self.ram.iter().enumerate() {
            if r.lo <= addr && addr as u64 + size as u64 <= r.hi as u64 {
                return Some((i, (addr - r.lo) as usize));
            }
        }
        None
    }

    #[inline]
    pub fn read(&mut self, addr: u32, size: u32) -> u64 {
        if let Some((i, off)) = self.ram_at(addr, size) {
            let b = &self.ram[i].buf[off..off + size as usize];
            let mut v = 0u64;
            for (k, x) in b.iter().enumerate() {
                v |= (*x as u64) << (8 * k);
            }
            return v;
        }
        let v = self.bus.read(&mut self.io, addr, size);
        if size >= 8 {
            v
        } else {
            v & ((1u64 << (8 * size)) - 1)
        }
    }

    fn write_trap(&mut self, addr: u32, size: u32, value: u64) -> Result<(), Trap> {
        let value = if size >= 8 { value } else { value & ((1u64 << (8 * size)) - 1) };
        if let Some((i, off)) = self.ram_at(addr, size) {
            if let Some((lo, hi)) = self.watch {
                let wanted = self.watch_byte.map_or(true, |b| (0..size).all(|k| (value >> (8 * k)) as u8 == b));
                if lo <= addr && addr < hi && wanted {
                    self.watch = None;
                    return Err(Trap::Stop(format!("write of {value:#x} to {addr:#x}")));
                }
            }
            let r = &mut self.ram[i];
            for k in 0..size as usize {
                r.buf[off + k] = (value >> (8 * k)) as u8;
            }
            if self.cmin <= addr.wrapping_add(size) && addr <= self.cmax {
                let lo = off.saturating_sub(7);
                for c in &mut r.code[lo..off + size as usize] {
                    *c = 0;
                }
            }
            return Ok(());
        }
        self.writes.push(Write { pc: self.pc, addr, size: size as u8, value });
        self.io.nwrites = self.writes.len();
        self.bus.write(&mut self.io, addr, size, value);
        if self.stop_write == Some((addr, value)) {
            return Err(Trap::Stop(format!("write {value:#x} to {addr:#x}")));
        }
        Ok(())
    }

    /// A write as the firmware would make it (recorded when it reaches the bus).
    pub fn write(&mut self, addr: u32, size: u32, value: u64) {
        let _ = self.write_trap(addr, size, value);
    }

    pub fn read_bytes(&mut self, addr: u32, n: u32) -> Vec<u8> {
        (0..n).map(|i| self.read(addr.wrapping_add(i), 1) as u8).collect()
    }

    pub fn write_bytes(&mut self, addr: u32, data: &[u8]) {
        for (i, b) in data.iter().enumerate() {
            self.write(addr.wrapping_add(i as u32), 1, *b as u64);
        }
    }

    pub fn read32(&mut self, addr: u32) -> u32 {
        self.read(addr, 4) as u32
    }

    /// RAM contents without side effects (None outside RAM).
    pub fn peek(&self, addr: u32, n: u32) -> Option<&[u8]> {
        self.ram_at(addr, n).map(|(i, off)| &self.ram[i].buf[off..off + n as usize])
    }

    pub fn load_elf(&mut self, path: &str) -> Result<(), String> {
        let elf = Elf::load(path)?;
        self.elf_path = path.to_string();
        for seg in &elf.segments {
            if !seg.data.is_empty() {
                self.write_bytes(seg.paddr, &seg.data);
            }
        }
        // code overlays (.overlayN.text) share one window: their symbols
        // are kept per overlay and named by the one _ovly_id says is mapped
        let mut ovl: HashMap<u16, u32> = HashMap::new();
        for (i, sec) in elf.sections.iter().enumerate() {
            if sec.name.starts_with(".overlay") && sec.name.ends_with(".text") {
                if let Ok(n) = sec.name[8..sec.name.len() - 5].parse::<u32>() {
                    ovl.insert(i as u16, n);
                    let end = (sec.addr + sec.size).max(self.overlay_window.map_or(0, |w| w.1));
                    self.overlay_window = Some((sec.addr, end));
                }
            }
        }
        let mut syms = Vec::new();
        for s in &elf.symbols {
            if s.name.is_empty() {
                continue;
            }
            self.all_syms.entry(s.name.clone()).or_insert((s.value, s.size));
            if s.kind == STT_OBJECT {
                self.object_list.push((s.name.clone(), s.value, s.size));
                self.objects.insert(s.name.clone(), (s.value, s.size));
                self.objects_first.entry(s.name.clone()).or_insert(s.value);
            }
            if (s.kind == STT_FUNC || s.kind == STT_NOTYPE) && s.value != 0 {
                match ovl.get(&s.shndx) {
                    None => syms.push((s.value, s.name.clone(), s.size)),
                    Some(n) => self.overlay_syms.entry(*n).or_default().push((s.value, s.name.clone())),
                }
            }
        }
        self.entry = elf.entry;
        syms.sort();
        for v in self.overlay_syms.values_mut() {
            v.sort();
        }
        self.sym_addrs = syms.iter().map(|s| s.0).collect();
        self.byname = syms.iter().map(|s| (s.1.clone(), s.0)).collect();
        let mut ids: Vec<_> = self.overlay_syms.keys().copied().collect();
        ids.sort();
        for n in ids {
            for (a, name) in &self.overlay_syms[&n] {
                self.byname.entry(name.clone()).or_insert(*a);
            }
        }
        self.symbols = syms;
        Ok(())
    }

    /// Address of a function or data symbol.
    pub fn addr(&self, name: &str) -> u32 {
        self.sym(name).unwrap_or_else(|| panic!("{}: no symbol {name}", self.elf_path))
    }

    pub fn sym(&self, name: &str) -> Option<u32> {
        self.byname.get(name).copied().or_else(|| self.objects.get(name).map(|o| o.0))
    }

    fn overlay_id(&self) -> Option<u32> {
        let a = *self.byname.get("_ovly_id")?;
        let b = self.peek(a, 4)?;
        Some(u32::from_le_bytes([b[0], b[1], b[2], b[3]]) & 0xffff) // high half: 0xffff in CIX's image
    }

    /// "function+0xoffset" of an address.
    pub fn func(&self, pc: u32) -> String {
        if let Some((lo, hi)) = self.overlay_window {
            if lo <= pc && pc < hi {
                if let Some(n) = self.overlay_id() {
                    if let Some(v) = self.overlay_syms.get(&n) {
                        let j = v.partition_point(|s| s.0 <= pc);
                        if j > 0 {
                            return format!("{}+{:#x} (overlay {n})", v[j - 1].1, pc - v[j - 1].0);
                        }
                    }
                }
            }
        }
        let i = self.sym_addrs.partition_point(|a| *a <= pc);
        if i > 0 {
            format!("{}+{:#x}", self.symbols[i - 1].1, pc - self.symbols[i - 1].0)
        } else {
            format!("{pc:#x}")
        }
    }

    /// The function's name alone.
    pub fn func_name(&self, pc: u32) -> String {
        let f = self.func(pc);
        f.split('+').next().unwrap_or("").to_string()
    }

    /// Return addresses of the windowed call chain, innermost first: live
    /// frames from the register file, spilled ones from their save areas
    /// (a caller's a0/a1 at its callee's sp - 16/- 12).
    pub fn backtrace(&self, depth: usize) -> Vec<u32> {
        let mut pcs = vec![self.pc];
        let (mut wb, mut a0, mut a1, mut live) = (self.wb4 >> 2, self.a(0), self.a(1), true);
        if self.peek(self.pc, 1).map_or(false, |b| b[0] == 0x36) {
            // at an ENTRY: the frame is the caller's
            let inc = (self.sr[PS as usize] >> PS_CALLINC_SHIFT) & 3;
            pcs.push((self.a(4 * inc) & 0x3fffffff) | (self.pc & 0xc0000000));
        }
        while pcs.len() < depth {
            let inc = a0 >> 30;
            if inc == 0 {
                break;
            }
            pcs.push((a0 & 0x3fffffff) | (pcs[pcs.len() - 1] & 0xc0000000));
            let caller = wb.wrapping_sub(inc) & 15;
            if live && (self.ws >> caller) & 1 != 0 {
                a0 = self.ar[(caller * 4) as usize];
                a1 = self.ar[(caller * 4 + 1) as usize];
            } else {
                live = false;
                let w = |a: u32| self.peek(a, 4).map(|b| u32::from_le_bytes([b[0], b[1], b[2], b[3]]));
                match (w(a1.wrapping_sub(16)), w(a1.wrapping_sub(12))) {
                    (Some(x), Some(y)) => {
                        a0 = x;
                        a1 = y;
                    }
                    _ => break,
                }
            }
            wb = caller;
        }
        pcs
    }

    /// The last instructions run, oldest first, as functions with the
    /// instructions run in each.
    pub fn trail(&self) -> String {
        let mut out: Vec<(String, u32, u32)> = Vec::new();
        for k in 0..64 {
            let pc = self.trail[(self.trail_at + k) & 63];
            if pc == 0 {
                continue;
            }
            let name = self.func_name(pc);
            match out.last_mut() {
                Some(last) if last.0 == name => last.2 = pc,
                _ => out.push((name, pc, pc)),
            }
        }
        out.iter().map(|(n, a, b)| format!("{n}[{a:#x}..{b:#x}]")).collect::<Vec<_>>().join(" > ")
    }

    pub fn backtrace_names(&self, depth: usize) -> String {
        self.backtrace(depth).iter().map(|pc| self.func(*pc)).collect::<Vec<_>>().join(" < ")
    }

    // --- stops and stubs ------------------------------------------------------

    fn set_mark(&mut self, pc: u32) {
        let on = self.stops.contains_key(&pc) || self.stubs.contains_key(&pc);
        if let Some((i, off)) = self.ram_at(pc, 1) {
            self.ram[i].mark[off] = on as u8;
        }
    }

    pub fn set_stop(&mut self, pc: u32, name: &str) {
        self.stops.insert(pc, name.to_string());
        self.set_mark(pc);
    }

    pub fn del_stop(&mut self, pc: u32) {
        self.stops.remove(&pc);
        self.set_mark(pc);
    }

    pub fn has_stop(&self, pc: u32) -> bool {
        self.stops.contains_key(&pc)
    }

    pub fn set_stub(&mut self, pc: u32, f: Stub<B>) {
        self.stubs.insert(pc, f);
        self.set_mark(pc);
    }

    #[allow(dead_code)]
    pub fn del_stub(&mut self, pc: u32) {
        self.stubs.remove(&pc);
        self.set_mark(pc);
    }

    // --- registers ------------------------------------------------------------

    #[inline]
    pub fn a(&self, i: u32) -> u32 {
        self.ar[((self.wb4 + i) & 63) as usize]
    }

    #[inline]
    pub fn set_a(&mut self, i: u32, v: u32) {
        self.ar[((self.wb4 + i) & 63) as usize] = v;
    }

    /// Register read of an instruction: a snapshot inside a bundle.
    #[inline]
    fn r(&self, i: u8) -> u32 {
        let k = ((self.wb4 + i as u32) & 63) as usize;
        if self.in_bundle {
            self.snap[k]
        } else {
            self.ar[k]
        }
    }

    #[inline]
    fn w(&mut self, i: u8, v: u32) {
        self.ar[((self.wb4 + i as u32) & 63) as usize] = v;
    }

    #[inline]
    pub fn ps(&self) -> u32 {
        self.sr[PS as usize]
    }

    pub fn rsr(&self, n: u32) -> u32 {
        match n {
            LBEG => self.lbeg,
            LEND => self.lend,
            LCOUNT => self.lcount,
            SAR => self.sar,
            BR => self.br,
            WINDOWBASE => self.wb4 >> 2,
            WINDOWSTART => self.ws,
            INTSET => self.io.int_lines | self.int_latched,
            INTENABLE => self.intenable,
            CCOUNT => self.io.ccount as u32,
            240..=242 => self.ccompare[(n - 240) as usize],
            _ => self.sr[(n & 255) as usize],
        }
    }

    pub fn wsr(&mut self, n: u32, v: u32) {
        match n {
            LBEG => self.lbeg = v,
            LEND => self.lend = v,
            LCOUNT => self.lcount = v,
            SAR => self.sar = v & 63,
            BR => self.br = v & 0xffff,
            WINDOWBASE => self.wb4 = (v & 15) << 2,
            WINDOWSTART => self.ws = v & 0xffff,
            INTSET => self.int_latched |= v,
            INTCLEAR => self.int_latched &= !v,
            INTENABLE => self.intenable = v,
            CCOUNT => self.io.ccount = v as u64,
            CPENABLE => self.sr[n as usize] = v & 0xff,
            240..=242 => {
                self.ccompare[(n - 240) as usize] = v;
                self.int_latched &= !(1 << TIMER_INT[(n - 240) as usize]);
            }
            _ => self.sr[(n & 255) as usize] = v,
        }
    }

    // --- execution --------------------------------------------------------------

    /// Run up to count instructions; why it stopped, if it did.
    pub fn run(&mut self, count: u64) -> Option<String> {
        let end = self.insns + count;
        while self.insns < end {
            if let Err(e) = self.step() {
                return Some(e);
            }
        }
        None
    }

    pub fn step(&mut self) -> Result<(), String> {
        let pc = self.pc;
        let marked = match self.ram_at(pc, 1) {
            Some((i, off)) => self.ram[i].mark[off] != 0,
            None => true,
        };
        if marked {
            if let Some(name) = self.stops.get(&pc) {
                return Err(name.clone());
            }
            if let Some(stub) = self.stubs.get(&pc).cloned() {
                self.do_stub(stub);
                return Ok(());
            }
        }
        if self.waiting && !self.wake()? {
            return Err("waiti with nothing to wake it".to_string());
        }
        self.interrupts();
        let pc = self.pc;
        self.io.pc = pc;
        self.trail[self.trail_at & 63] = pc;
        self.trail_at = self.trail_at.wrapping_add(1);
        let insn = self.fetch(pc)?;
        let ps = self.sr[PS as usize];
        let r = if insn.fp && self.sr[CPENABLE as usize] & 1 == 0 {
            Err(Trap::Exc(CP0_DISABLED, None))
        } else if insn.maxr >= 4 && ps & (PS_WOE | PS_EXCM) == PS_WOE {
            match self.window_check(insn.maxr as u32) {
                Ok(()) => self.exec(&insn, pc, insn.len as u32),
                Err(t) => Err(t),
            }
        } else {
            self.exec(&insn, pc, insn.len as u32)
        };
        let npc = match r {
            Ok(npc) => npc,
            Err(Trap::Stop(s)) => return Err(s),
            Err(t) => {
                self.exception(t, pc);
                return Ok(());
            }
        };
        self.insns += 1;
        self.io.ccount += 1;
        self.pc = match npc {
            Some(n) => n,
            None => {
                let n = pc.wrapping_add(insn.len as u32);
                if n == self.lend && self.lcount != 0 && self.sr[PS as usize] & PS_EXCM == 0 {
                    self.lcount -= 1;
                    self.lbeg
                } else {
                    n
                }
            }
        };
        Ok(())
    }

    fn do_stub(&mut self, f: Stub<B>) {
        let n = (self.sr[PS as usize] >> PS_CALLINC_SHIFT) & 3;
        let base = 4 * n;
        if let Some(ret) = f(self) {
            self.set_a(base + 2, ret);
        }
        let ra = self.a(base);
        self.pc = if n != 0 { (self.pc & 0xc0000000) | (ra & 0x3fffffff) } else { ra };
        self.insns += 1;
    }

    /// Argument i of a stubbed function (before its ENTRY).
    pub fn arg(&self, i: u32) -> u32 {
        let n = (self.sr[PS as usize] >> PS_CALLINC_SHIFT) & 3;
        self.a(4 * n + 2 + i)
    }

    fn window_check(&mut self, maxr: u32) -> Result<(), Trap> {
        let w = maxr >> 2;
        let wb = self.wb4 >> 2;
        let ws2 = (self.ws | self.ws << 16) as u64;
        let rot = (ws2 >> (wb + 1)) & ((1u64 << w) - 1);
        if rot == 0 {
            return Ok(());
        }
        let n = rot.trailing_zeros() + 1; // windows to the live frame
        let after = (ws2 >> (wb + 1 + n)) & 7;
        let kind = if after & 1 != 0 { 0 } else if after & 2 != 0 { 1 } else { 2 };
        let ps = self.sr[PS as usize];
        self.sr[PS as usize] = (ps & !(15 << PS_OWB_SHIFT)) | wb << PS_OWB_SHIFT | PS_EXCM;
        self.sr[EPC1 as usize] = self.pc;
        self.wb4 = ((wb + n) & 15) << 2;
        Err(Trap::Vector(VEC_OVERFLOW[kind]))
    }

    fn exception(&mut self, t: Trap, pc: u32) {
        let (cause, vaddr) = match t {
            Trap::Vector(off) => {
                self.pc = self.sr[VECBASE as usize].wrapping_add(off);
                return;
            }
            Trap::Exc(cause, vaddr) => (cause, vaddr),
            Trap::Stop(_) => return,
        };
        let ps = self.sr[PS as usize];
        if let Some(v) = vaddr {
            self.sr[EXCVADDR as usize] = v;
        }
        if ps & PS_EXCM != 0 {
            self.sr[DEPC as usize] = pc;
            self.sr[EXCCAUSE as usize] = cause;
            self.pc = self.sr[VECBASE as usize].wrapping_add(VEC_DOUBLE);
            return;
        }
        self.sr[EPC1 as usize] = pc;
        self.sr[EXCCAUSE as usize] = cause;
        self.sr[PS as usize] = ps | PS_EXCM;
        self.pc = self.sr[VECBASE as usize].wrapping_add(if ps & PS_UM != 0 { VEC_USER } else { VEC_KERNEL });
    }

    // interrupts

    pub fn pending(&self) -> u32 {
        (self.io.int_lines | self.int_latched) & self.intenable
    }

    /// Advance CCOUNT by cycles (a busy-wait done at once), latching the
    /// timers it passes; the bus sees the time go by.
    pub fn skip(&mut self, cycles: u64) {
        let c = self.io.ccount as u32;
        for i in 0..3 {
            let d = self.ccompare[i].wrapping_sub(c) as u64;
            if 0 < d && d <= cycles {
                self.int_latched |= 1 << TIMER_INT[i];
            }
        }
        self.io.ccount += cycles;
        self.bus.advance(&mut self.io, cycles);
    }

    #[inline]
    fn timers(&mut self) {
        let c = self.io.ccount as u32;
        for i in 0..3 {
            // CCOMPARE matches when CCOUNT reaches it; checked each instruction
            if self.ccompare[i] == c {
                self.int_latched |= 1 << TIMER_INT[i];
            }
        }
    }

    fn interrupts(&mut self) {
        self.timers();
        let p = self.pending();
        if p == 0 {
            return;
        }
        let ps = self.sr[PS as usize];
        let mut cur = ps & PS_INTLEVEL;
        if ps & PS_EXCM != 0 {
            cur = cur.max(EXCM_LEVEL);
        }
        let level = (0..32).filter(|i| p >> i & 1 != 0).map(|i| INTLEVEL[i] as u32).max().unwrap_or(0);
        if level <= cur {
            return;
        }
        self.waiting = false;
        let pc = self.pc;
        if self.audit.is_some() {
            let regs = self.audit_regs();
            self.audit_irq.insert((pc, self.a(1)), regs);
        }
        if level == 1 {
            self.sr[EPC1 as usize] = pc;
            self.sr[EXCCAUSE as usize] = LEVEL1_INT;
            self.sr[PS as usize] = ps | PS_EXCM;
            self.pc = self.sr[VECBASE as usize].wrapping_add(if ps & PS_UM != 0 { VEC_USER } else { VEC_KERNEL });
        } else {
            self.sr[(EPC1 + level - 1) as usize] = pc;
            self.sr[(EPS2 + level - 2) as usize] = ps;
            self.sr[PS as usize] = (ps & !PS_INTLEVEL) | level | PS_EXCM;
            self.pc = self.sr[VECBASE as usize].wrapping_add(VEC_LEVEL[level as usize]);
        }
    }

    /// WAITI: jump CCOUNT to the next timer match if nothing is pending.
    fn wake(&mut self) -> Result<bool, String> {
        if self.pending() != 0 {
            return Ok(true);
        }
        let c = self.io.ccount as u32;
        let mut best: Option<u64> = None;
        for i in 0..3 {
            if self.intenable >> TIMER_INT[i] & 1 != 0 {
                let d = self.ccompare[i].wrapping_sub(c) as u64;
                if d != 0 && best.map_or(true, |b| d < b) {
                    best = Some(d);
                }
            }
        }
        if let Some(d) = self.bus.next_event(&self.io) {
            if best.map_or(true, |b| d < b) {
                best = Some(d);
            }
        }
        let Some(best) = best else { return Ok(false) };
        if let Some(until) = self.until {
            if self.io.ccount + best > until {
                // asleep at the time limit: stay in WAITI, stop the run there
                let d = until.saturating_sub(self.io.ccount);
                self.io.ccount += d;
                self.bus.advance(&mut self.io, d);
                return Err("until".to_string());
            }
        }
        self.io.ccount += best;
        self.bus.advance(&mut self.io, best);
        self.timers();
        Ok(true)
    }

    // --- decoding -------------------------------------------------------------

    fn fetch(&mut self, pc: u32) -> Result<Insn, String> {
        let at = self.ram_at(pc, 1);
        if let Some((i, off)) = at {
            let k = self.ram[i].code[off];
            if k != 0 {
                return Ok(self.insns_cache[k as usize - 1]);
            }
        }
        let b0 = self.read(pc, 1) as u32;
        let op0 = b0 & 15;
        let mut insn;
        if op0 >= 14 {
            let bytes = self.read_bytes(pc, 8);
            let mut data = [0u8; 8];
            data.copy_from_slice(&bytes);
            let slots = flix::decode_bundle(&data, false).map_err(|e| {
                format!("undecodable bundle at {pc:#x}: {} ({e})", bytes.iter().map(|b| format!("{b:02x}")).collect::<String>())
            })?;
            let mut ops: Vec<Insn> = slots.iter().map(decode::decode_slot).collect();
            // all slots read the snapshot; SAR writes after the slots that
            // read SAR, the control transfer (which may rotate the window) last
            ops.sort_by_key(|o| (o.ctl, o.late));
            let start = self.bundle_ops.len() as u32;
            insn = Insn {
                k: K::Bundle { start, n: ops.len() as u8 },
                maxr: ops.iter().map(|o| o.maxr).max().unwrap_or(0),
                ctl: ops.iter().any(|o| o.ctl),
                fp: ops.iter().any(|o| o.fp),
                late: false,
                len: 8,
            };
            self.bundle_ops.extend(ops);
        } else if op0 >= 8 {
            insn = decode::decode16(self.read(pc, 2) as u32);
            insn.len = 2;
        } else {
            insn = decode::decode24(self.read(pc, 3) as u32);
            insn.len = 3;
        }
        if let Some((i, off)) = at {
            self.insns_cache.push(insn);
            let n = self.insns_cache.len() as u32;
            let r = &mut self.ram[i];
            r.code[off] = n;
            for c in r.cov.iter_mut().skip(off).take(insn.len as usize) {
                *c = 1;
            }
            if let Some((lo, hi)) = self.overlay_window {
                if lo <= pc && pc < hi {
                    if let Some(id) = self.overlay_id() {
                        self.overlay_cov.insert((id, pc));
                    }
                }
            }
        }
        self.cmin = self.cmin.min(pc);
        self.cmax = self.cmax.max(pc.wrapping_add(8));
        Ok(insn)
    }

    /// Mark ranges as executed (what a run that was rolled back covered).
    pub fn mark_covered(&mut self, ranges: &[(u32, u32)]) {
        for (lo, hi) in ranges {
            if let Some((i, off)) = self.ram_at(*lo, hi - lo) {
                for c in &mut self.ram[i].cov[off..off + (hi - lo) as usize] {
                    *c = 1;
                }
            }
        }
    }

    /// Addresses of RAM bytes that held an executed instruction, as
    /// half-open ranges.
    pub fn covered(&self) -> Vec<(u32, u32)> {
        let mut out = Vec::new();
        for r in &self.ram {
            let mut start = None;
            for (i, c) in r.cov.iter().enumerate() {
                match (start, *c != 0) {
                    (None, true) => start = Some(i),
                    (Some(s), false) => {
                        out.push((r.lo + s as u32, r.lo + i as u32));
                        start = None;
                    }
                    _ => {}
                }
            }
            if let Some(s) = start {
                out.push((r.lo + s as u32, r.hi));
            }
        }
        out
    }

    // --- instructions -----------------------------------------------------------

    fn cond(&self, cond: Cond, s: u8, x: i32) -> bool {
        let a = self.r(s);
        match cond {
            Cond::Eqz => a == 0,
            Cond::Nez => a != 0,
            Cond::Ltz => a & 0x80000000 != 0,
            Cond::Gez => a & 0x80000000 == 0,
            Cond::Eqi => a as i32 == x,
            Cond::Nei => a as i32 != x,
            Cond::Lti => (a as i32) < x,
            Cond::Gei => a as i32 >= x,
            Cond::Ltui => a < x as u32,
            Cond::Geui => a >= x as u32,
            Cond::Bbci => (a >> x) & 1 == 0,
            Cond::Bbsi => (a >> x) & 1 != 0,
            Cond::Bf => (self.br >> s) & 1 == 0,
            Cond::Bt => (self.br >> s) & 1 == 1,
            _ => {
                let b = self.r(x as u8);
                match cond {
                    Cond::Bnone => a & b == 0,
                    Cond::Beq => a == b,
                    Cond::Blt => (a as i32) < (b as i32),
                    Cond::Bltu => a < b,
                    Cond::Ball => !a & b == 0,
                    Cond::Bbc => (a >> (b & 31)) & 1 == 0,
                    Cond::Bany => a & b != 0,
                    Cond::Bne => a != b,
                    Cond::Bge => a as i32 >= b as i32,
                    Cond::Bgeu => a >= b,
                    Cond::Bnall => !a & b != 0,
                    _ => (a >> (b & 31)) & 1 != 0, // Bbs
                }
            }
        }
    }

    fn audit_regs(&self) -> Vec<u32> {
        let mut v: Vec<u32> = (0..16).map(|i| self.a(i)).collect();
        v.extend([self.sar, self.lbeg, self.lend, self.lcount, self.br]);
        v
    }

    /// The audit's check of a return from an interrupt to `to`.
    fn audit_return(&mut self, to: u32) -> Result<(), Trap> {
        if self.audit.is_none() {
            return Ok(());
        }
        let Some(was) = self.audit_irq.remove(&(to, self.a(1))) else { return Ok(()) };
        let now = self.audit_regs();
        if let Some(i) = (0..was.len()).find(|i| now[*i] != was[*i]) {
            let name = match i {
                0..=15 => format!("a{i}"),
                16 => "SAR".to_string(),
                17 => "LBEG".to_string(),
                18 => "LEND".to_string(),
                19 => "LCOUNT".to_string(),
                _ => "BR".to_string(),
            };
            return Err(Trap::Stop(format!(
                "interrupt audit: back at {} after an interrupt, {name} is {:#x}, was {:#x} when it was taken",
                self.func(to),
                now[i],
                was[i]
            )));
        }
        Ok(())
    }

    fn call(&mut self, n: u8, tgt: u32, pc: u32, len: u32) -> u32 {
        let n = n as u32;
        if n != 0 && self.audit.is_some() {
            let regs: Vec<u32> = (0..4 * n).map(|i| self.a(i)).collect();
            let key = (pc.wrapping_add(len) & 0x3fffffff, self.a(1));
            self.audit.as_mut().unwrap().insert(key, regs);
        }
        if n == 0 {
            self.w(0, pc.wrapping_add(len));
        } else {
            self.w(4 * n as u8, (n << 30) | (pc.wrapping_add(len) & 0x3fffffff));
            let ps = self.sr[PS as usize];
            self.sr[PS as usize] = (ps & !(3 << PS_CALLINC_SHIFT)) | n << PS_CALLINC_SHIFT;
        }
        tgt
    }

    /// A RETW now would find its caller's window not live.
    fn retw_underflows(&self) -> bool {
        let a0 = self.a(0);
        let k = a0 >> 30;
        k != 0 && (self.ws >> ((self.wb4 >> 2).wrapping_sub(k) & 15)) & 1 == 0
    }

    fn retw(&mut self, pc: u32) -> Result<Option<u32>, Trap> {
        let a0 = self.r(0);
        let k = a0 >> 30;
        if k == 0 {
            return Err(Trap::Exc(ILLEGAL, None));
        }
        let owb = self.wb4 >> 2;
        let nwb = owb.wrapping_sub(k) & 15;
        if (self.ws >> nwb) & 1 != 0 {
            self.ws &= !(1 << owb);
            self.wb4 = nwb << 2;
            if self.audit.is_some() {
                let key = (a0 & 0x3fffffff, self.a(1));
                if let Some(regs) = self.audit.as_mut().unwrap().remove(&key) {
                    if let Some(i) = (0..regs.len() as u32).find(|i| self.a(*i) != regs[*i as usize]) {
                        return Err(Trap::Stop(format!(
                            "window audit: back in {} after the call returning from {}, a{i} is {:#x}, was {:#x} at the call",
                            self.func((pc & 0xc0000000) | (a0 & 0x3fffffff)),
                            self.func(pc),
                            self.a(i),
                            regs[i as usize]
                        )));
                    }
                }
            }
            return Ok(Some((pc & 0xc0000000) | (a0 & 0x3fffffff)));
        }
        // window underflow: the caller's frame is on the stack
        let ps = self.sr[PS as usize];
        self.sr[PS as usize] = (ps & !(15 << PS_OWB_SHIFT)) | owb << PS_OWB_SHIFT | PS_EXCM;
        self.sr[EPC1 as usize] = pc;
        self.wb4 = nwb << 2;
        Ok(Some(self.sr[VECBASE as usize].wrapping_add(VEC_UNDERFLOW[(k - 1) as usize])))
    }

    fn exec(&mut self, insn: &Insn, pc: u32, len: u32) -> Result<Option<u32>, Trap> {
        match insn.k {
            K::Bundle { start, n } => {
                // A bundle is one instruction: when its RETW takes a window
                // underflow, nothing of the bundle has happened, and it runs
                // again after the handler.  (CIX's strlen ends in
                // { retw ; sub a2, a3, a2 }: applied twice, the length would
                // be the string's address again.)
                for i in start..start + n as u32 {
                    if self.bundle_ops[i as usize].k == K::Retw && self.retw_underflows() {
                        return self.retw(pc);
                    }
                }
                self.snap = self.ar;
                self.in_bundle = true;
                let mut npc = None;
                for i in start..start + n as u32 {
                    let op = self.bundle_ops[i as usize];
                    match self.exec(&op, pc, len) {
                        Ok(Some(x)) => npc = Some(x),
                        Ok(None) => {}
                        Err(t) => {
                            self.in_bundle = false;
                            return Err(t);
                        }
                    }
                }
                self.in_bundle = false;
                Ok(npc)
            }
            K::Unknown => Err(Trap::Exc(ILLEGAL, None)),
            K::Nop => Ok(None),
            K::L32r { t, off } => {
                let v = self.read((pc.wrapping_add(3) & !3).wrapping_add(off), 4) as u32;
                self.w(t, v);
                Ok(None)
            }
            K::Call { n, off } => Ok(Some(self.call(n, (pc & !3).wrapping_add(off as u32).wrapping_add(4), pc, len))),
            K::Callx { k, s } => {
                let tgt = self.r(s);
                Ok(Some(self.call(k, tgt, pc, len)))
            }
            K::J { off } => Ok(Some(pc.wrapping_add(4).wrapping_add(off as u32))),
            K::Jx { s } => Ok(Some(self.r(s))),
            K::Ret => Ok(Some(self.r(0))),
            K::Retw => self.retw(pc),
            K::Br { cond, s, x, off } => {
                if self.cond(cond, s, x) {
                    Ok(Some(pc.wrapping_add(4).wrapping_add(off as u32)))
                } else {
                    Ok(None)
                }
            }
            K::Entry { s, frame } => {
                let ps = self.sr[PS as usize];
                let inc = (ps >> PS_CALLINC_SHIFT) & 3;
                if inc != 0 && ps & (PS_WOE | PS_EXCM) == PS_WOE {
                    self.window_check(4 * inc + s as u32)?;
                }
                let v = self.r(s).wrapping_sub(frame);
                self.w((4 * inc) as u8 + s, v);
                self.wb4 = (self.wb4 + 4 * inc) & 63;
                self.ws |= 1 << (self.wb4 >> 2);
                Ok(None)
            }
            K::Loop { kind, s, imm8 } => {
                let cnt = self.r(s);
                self.lbeg = pc.wrapping_add(len);
                self.lend = pc.wrapping_add(4).wrapping_add(imm8);
                self.lcount = cnt.wrapping_sub(1);
                if (kind == 9 && cnt == 0) || (kind == 10 && cnt as i32 <= 0) {
                    self.lcount = 0;
                    return Ok(Some(self.lend));
                }
                Ok(None)
            }
            K::Load { t, s, off, size, signed } => {
                let a = self.r(s).wrapping_add(off) & !(size as u32 - 1);
                let mut v = self.read(a, size as u32) as u32;
                if signed {
                    v = sx(v, 16);
                }
                self.w(t, v);
                Ok(None)
            }
            K::Store { t, s, off, size } => {
                let a = self.r(s).wrapping_add(off) & !(size as u32 - 1);
                let v = self.r(t);
                self.write_trap(a, size as u32, v as u64)?;
                Ok(None)
            }
            K::Movi { t, v } => {
                self.w(t, v);
                Ok(None)
            }
            K::Addi { t, s, v } => {
                let x = self.r(s).wrapping_add(v);
                self.w(t, x);
                Ok(None)
            }
            K::Bin { op, r, s, t } => {
                let (a, b) = (self.r(s), self.r(t));
                let v = match op {
                    Bin::And => a & b,
                    Bin::Or => a | b,
                    Bin::Xor => a ^ b,
                    Bin::Addx(sh) => (a << sh).wrapping_add(b),
                    Bin::Subx(sh) => (a << sh).wrapping_sub(b),
                    Bin::Mul16u => (a & 0xffff) * (b & 0xffff),
                    Bin::Mul16s => (a as i16 as i32).wrapping_mul(b as i16 as i32) as u32,
                    Bin::Saltu => (a < b) as u32,
                    Bin::Salt => ((a as i32) < (b as i32)) as u32,
                    Bin::Mull => a.wrapping_mul(b),
                    Bin::Muluh => ((a as u64 * b as u64) >> 32) as u32,
                    Bin::Mulsh => ((a as i32 as i64 * b as i32 as i64) >> 32) as u32,
                    Bin::Quou | Bin::Quos | Bin::Remu | Bin::Rems if b == 0 => return Err(Trap::Exc(DIVIDE_BY_ZERO, None)),
                    Bin::Quou => a / b,
                    Bin::Quos => (a as i32).wrapping_div(b as i32) as u32,
                    Bin::Remu => a % b,
                    Bin::Rems => (a as i32).wrapping_rem(b as i32) as u32,
                    Bin::Min => if (a as i32) < (b as i32) { a } else { b },
                    Bin::Max => if (a as i32) > (b as i32) { a } else { b },
                    Bin::Minu => a.min(b),
                    Bin::Maxu => a.max(b),
                };
                self.w(r, v);
                Ok(None)
            }
            K::Neg { r, t } => {
                let v = self.r(t).wrapping_neg();
                self.w(r, v);
                Ok(None)
            }
            K::Abs { r, t } => {
                let v = (self.r(t) as i32).wrapping_abs() as u32;
                self.w(r, v);
                Ok(None)
            }
            K::Extui { r, t, sh, mask } => {
                let v = (self.r(t) >> sh) & mask;
                self.w(r, v);
                Ok(None)
            }
            K::Slli { r, s, sh } => {
                let v = ((self.r(s) as u64) << sh) as u32;
                self.w(r, v);
                Ok(None)
            }
            K::Srai { r, t, sh } => {
                let v = ((self.r(t) as i32) >> sh) as u32;
                self.w(r, v);
                Ok(None)
            }
            K::Srli { r, t, sh } => {
                let v = self.r(t) >> sh;
                self.w(r, v);
                Ok(None)
            }
            K::Src { r, s, t } => {
                let v = ((((self.r(s) as u64) << 32) | self.r(t) as u64) >> (self.sar & 63)) as u32;
                self.w(r, v);
                Ok(None)
            }
            K::Srl { r, t } => {
                let v = ((self.r(t) as u64) >> (self.sar & 63)) as u32;
                self.w(r, v);
                Ok(None)
            }
            K::Sll { r, s } => {
                let v = (((self.r(s) as u64) << 32) >> (self.sar & 63)) as u32;
                self.w(r, v);
                Ok(None)
            }
            K::Sra { r, t } => {
                let v = ((self.r(t) as i32 as i64) >> (self.sar & 63)) as u32;
                self.w(r, v);
                Ok(None)
            }
            K::Xsr { sr, t } => {
                let old = self.rsr(sr);
                let v = self.r(t);
                self.wsr(sr, v);
                self.w(t, old);
                Ok(None)
            }
            K::Rsr { sr, t } => {
                let v = self.rsr(sr);
                self.w(t, v);
                Ok(None)
            }
            K::Wsr { sr, t } => {
                let v = self.r(t);
                self.wsr(sr, v);
                Ok(None)
            }
            K::Rur { ur, r } => {
                let v = self.ur[(ur & 255) as usize];
                self.w(r, v);
                Ok(None)
            }
            K::Wur { ur, t } => {
                self.ur[(ur & 255) as usize] = self.r(t);
                Ok(None)
            }
            K::Sext { r, s, bits } => {
                let v = sx(self.r(s), bits as u32);
                self.w(r, v);
                Ok(None)
            }
            K::MovCond { cond, r, s, t } => {
                let v = self.r(t);
                let yes = match cond {
                    0 => v == 0,
                    1 => v != 0,
                    2 => v & 0x80000000 != 0,
                    _ => v & 0x80000000 == 0,
                };
                if yes {
                    let x = self.r(s);
                    self.w(r, x);
                }
                Ok(None)
            }
            K::MovB { want, r, s, t } => {
                if (self.br >> t) & 1 == want as u32 {
                    let x = self.r(s);
                    self.w(r, x);
                }
                Ok(None)
            }
            K::Movsp { s, t } => {
                let wb = self.wb4 >> 2;
                if !(1..=3).any(|k| (self.ws >> (wb.wrapping_sub(k) & 15)) & 1 != 0) {
                    return Err(Trap::Exc(ALLOCA, None));
                }
                let x = self.r(s);
                self.w(t, x);
                Ok(None)
            }
            K::Rfe => {
                self.sr[PS as usize] &= !PS_EXCM;
                let to = self.sr[EPC1 as usize];
                self.audit_return(to)?;
                Ok(Some(to))
            }
            K::Rfde => {
                self.sr[PS as usize] &= !PS_EXCM;
                Ok(Some(self.sr[DEPC as usize]))
            }
            K::Rfw { under } => {
                if under {
                    self.ws |= 1 << (self.wb4 >> 2);
                } else {
                    self.ws &= !(1 << (self.wb4 >> 2));
                }
                let ps = self.sr[PS as usize];
                self.wb4 = ((ps >> PS_OWB_SHIFT) & 15) << 2;
                self.sr[PS as usize] = ps & !PS_EXCM;
                Ok(Some(self.sr[EPC1 as usize]))
            }
            K::Rfi { level } => {
                let l = level as u32;
                self.sr[PS as usize] = self.sr[((EPS2 + l).wrapping_sub(2) & 255) as usize];
                let to = self.sr[((EPC1 + l).wrapping_sub(1) & 255) as usize];
                self.audit_return(to)?;
                Ok(Some(to))
            }
            K::Clrex => {
                self.monitor = false;
                Ok(None)
            }
            K::Getex { t } => {
                let v = self.r(t);
                self.w(t, (v & !1) | self.exclusive);
                self.exclusive = v & 1;
                Ok(None)
            }
            K::Break { s, t } => Err(Trap::Stop(format!("break {s},{t} at {}", self.func(pc)))),
            K::BreakN { s } => Err(Trap::Stop(format!("break.n {s} at {}", self.func(pc)))),
            K::Syscall => Err(Trap::Exc(SYSCALL, None)),
            K::Simcall => Err(Trap::Stop(format!("simcall at {}", self.func(pc)))),
            K::Rsil { t, s } => {
                let ps = self.sr[PS as usize];
                self.w(t, ps);
                self.sr[PS as usize] = (ps & !PS_INTLEVEL) | s as u32;
                Ok(None)
            }
            K::Waiti { s } => {
                self.sr[PS as usize] = (self.sr[PS as usize] & !PS_INTLEVEL) | s as u32;
                self.waiting = true;
                Ok(None)
            }
            K::AnyAll { width, all, s, t } => {
                let mask = (1u32 << width) - 1;
                let v = (self.br >> s) & mask;
                let res = if all { v == mask } else { v != 0 };
                self.br = (self.br & !(1 << t)) | ((res as u32) << t);
                Ok(None)
            }
            K::BoolOp { op, r, s, t } => {
                let (a, b) = ((self.br >> s) & 1, (self.br >> t) & 1);
                let v = match op {
                    0 => a & b,
                    1 => a & !b,
                    2 => a | b,
                    3 => a | (!b & 1),
                    _ => a ^ b,
                } & 1;
                self.br = (self.br & !(1 << r)) | (v << r);
                Ok(None)
            }
            K::Ssr { s } => {
                self.sar = self.r(s) & 31;
                Ok(None)
            }
            K::Ssl { s } => {
                self.sar = 32 - (self.r(s) & 31);
                Ok(None)
            }
            K::Ssa8l { s } => {
                self.sar = (self.r(s) & 3) << 3;
                Ok(None)
            }
            K::Ssa8b { s } => {
                self.sar = 32 - ((self.r(s) & 3) << 3);
                Ok(None)
            }
            K::Ssai { v } => {
                self.sar = v as u32;
                Ok(None)
            }
            K::Rotw { k } => {
                self.wb4 = self.wb4.wrapping_add((4 * k as i32) as u32) & 63;
                Ok(None)
            }
            K::Nsa { s, t } => {
                let mut v = self.r(s) as i32;
                if v < 0 {
                    v = !v;
                }
                self.w(t, (v as u32).leading_zeros() - 1);
                Ok(None)
            }
            K::Nsau { s, t } => {
                let v = self.r(s).leading_zeros();
                self.w(t, v);
                Ok(None)
            }
            K::TlbRead { t } => {
                self.w(t, 0);
                Ok(None)
            }
            K::Fp { g, op, r, s, t, imm } => self.exec_fp(g, op, r, s, t, imm),
        }
    }

    #[inline]
    fn f(&self, i: u8) -> f64 {
        f32::from_bits(self.fr[i as usize] as u32) as f64
    }

    #[inline]
    fn d(&self, i: u8) -> f64 {
        f64::from_bits(self.fr[i as usize])
    }

    fn set_br(&mut self, bit: u8, v: bool) {
        self.br = (self.br & !(1 << bit)) | ((v as u32) << bit);
    }

    fn exec_fp(&mut self, g: FpGroup, op: u8, r: u8, s: u8, t: u8, imm: u32) -> Result<Option<u32>, Trap> {
        let (ri, si, ti) = (r as usize, s as usize, t as usize);
        match g {
            FpGroup::Lsci => {
                let (size, scale, mask) = if op & 1 == 0 { (4, 4 * imm, !3u32) } else { (8, 8 * imm, !7u32) };
                let a = self.r(s);
                match op & !1 {
                    0 => self.fr[ti] = self.read(a.wrapping_add(scale) & mask, size),
                    4 => self.write_trap(a.wrapping_add(scale) & mask, size, self.fr[ti])?,
                    8 => {
                        self.fr[ti] = self.read(a & mask, size);
                        self.w(s, a.wrapping_add(scale));
                    }
                    _ => {
                        self.write_trap(a & mask, size, self.fr[ti])?;
                        self.w(s, a.wrapping_add(scale));
                    }
                }
            }
            FpGroup::Lsx => {
                let (size, mask) = if op & 2 == 0 { (4, !3u32) } else { (8, !7u32) };
                let (a, b) = (self.r(s), self.r(t));
                match op & 5 {
                    0 => self.fr[ri] = self.read(a.wrapping_add(b) & mask, size),
                    1 => {
                        self.fr[ri] = self.read(a & mask, size);
                        self.w(s, a.wrapping_add(b));
                    }
                    4 => self.write_trap(a.wrapping_add(b) & mask, size, self.fr[ri])?,
                    _ => {
                        self.write_trap(a & mask, size, self.fr[ri])?;
                        self.w(s, a.wrapping_add(b));
                    }
                }
            }
            FpGroup::Fp0 => match op {
                0 => self.fr[ri] = f2u(self.f(s) + self.f(t)),
                1 => self.fr[ri] = f2u(self.f(s) - self.f(t)),
                2 => self.fr[ri] = f2u(self.f(s) * self.f(t)),
                4 | 5 | 6 => {
                    let sign = if op == 5 { -1.0 } else { 1.0 };
                    self.fr[ri] = f2u(self.f(r) + sign * self.f(s) * self.f(t));
                }
                7 => {
                    // DIVN.S: the quotient of the sequence
                    self.fr[ri] = match self.div {
                        Some(d) if d.b != 0.0 => f2u(d.a.unwrap_or(d.b) / d.b),
                        _ => 0x7fc00000,
                    };
                    self.div = None;
                }
                8 | 9 | 10 | 11 | 14 => {
                    let x = self.f(s) * (1u32 << t) as f64;
                    let v = if x.is_nan() || x.is_infinite() { to_int(x, op != 14) } else { to_int(round(op, x), op != 14) };
                    self.w(r, v);
                }
                _ => {
                    // FLOAT.S / UFLOAT.S
                    let v = self.r(s);
                    let x = if op == 12 { v as i32 as f64 } else { v as f64 };
                    self.fr[ri] = f2u(x / (1u32 << t) as f64);
                }
            },
            FpGroup::Fp1op => match op {
                0 => self.fr[ri] = self.fr[si],
                1 => self.fr[ri] = self.fr[si] & 0x7fffffff,
                2 => self.fr[ri] = self.f(s).to_bits(), // CVTD.S
                3 => self.fr[ri] = [0x00000000u64, 0x3f800000, 0x40000000, 0x3f000000].get(si).copied().unwrap_or(0),
                4 => {
                    let v = self.fr[si] as u32;
                    self.w(r, v);
                }
                5 => self.fr[ri] = self.r(s) as u64,
                6 => self.fr[ri] = (self.fr[si] & 0xffffffff) ^ 0x80000000,
                7 => {
                    // DIV0.S: start of a divide by fs
                    let b = self.f(s);
                    self.div = Some(Div { b, breg: s, a: None });
                    self.fr[ri] = if b != 0.0 { f2u(1.0 / b) } else { 0x7f800000 };
                }
                11 => {
                    // NEXP01.S: the dividend's turn sets it
                    let x = self.f(s);
                    if let Some(d) = &mut self.div {
                        if s != d.breg {
                            d.a = Some(x);
                        }
                    }
                    self.fr[ri] = self.fr[si];
                }
                _ => {} // MKSADJ/MKDADJ/ADDEXP/ADDEXPM: no-ops here
            },
            FpGroup::Fp1 => match op {
                1..=7 => {
                    let v = fcmp(op, self.f(s), self.f(t));
                    self.set_br(r, v);
                }
                8..=11 => {
                    let v = self.r(t);
                    let yes = match op {
                        8 => v == 0,
                        9 => v != 0,
                        10 => v & 0x80000000 != 0,
                        _ => v & 0x80000000 == 0,
                    };
                    if yes {
                        self.fr[ri] = self.fr[si];
                    }
                }
                _ => {
                    if (self.br >> t) & 1 == (op - 12) as u32 {
                        self.fr[ri] = self.fr[si];
                    }
                }
            },
            FpGroup::Dfp0 => match op {
                0 => self.fr[ri] = (self.d(s) + self.d(t)).to_bits(),
                1 => self.fr[ri] = (self.d(s) - self.d(t)).to_bits(),
                2 => self.fr[ri] = (self.d(s) * self.d(t)).to_bits(),
                4 | 5 | 6 => {
                    let sign = if op == 5 { -1.0 } else { 1.0 };
                    self.fr[ri] = (self.d(r) + sign * self.d(s) * self.d(t)).to_bits();
                }
                7 => {
                    self.fr[ri] = match self.div {
                        Some(d) if d.b != 0.0 => (d.a.unwrap_or(d.b) / d.b).to_bits(),
                        _ => 0x7ff8000000000000,
                    };
                    self.div = None;
                }
                8 | 9 | 10 | 11 | 14 => {
                    let x = self.d(s) * (1u32 << t) as f64;
                    let v = if x.is_nan() || x.is_infinite() { to_int(x, op != 14) } else { to_int(round(op, x), op != 14) };
                    self.w(r, v);
                }
                _ => {
                    let v = self.r(s);
                    let x = if op == 12 { v as i32 as f64 } else { v as f64 };
                    self.fr[ri] = (x / (1u32 << t) as f64).to_bits();
                }
            },
            FpGroup::Dfp0op => match op {
                0 => self.fr[ri] = self.fr[si],
                1 => self.fr[ri] = self.fr[si] & 0x7fffffffffffffff,
                2 => self.fr[ri] = f2u(self.d(s)), // CVTS.D
                3 => self.fr[ri] = [0.0f64, 1.0, 2.0, 0.5].get(si).copied().unwrap_or(0.0).to_bits(),
                4 => {
                    let v = (self.fr[si] >> 32) as u32; // RFRD: high half
                    self.w(r, v);
                }
                6 => self.fr[ri] = self.fr[si] ^ (1 << 63),
                7 => {
                    let b = self.d(s);
                    // CIX's __divdf3 takes a second reciprocal of the
                    // divisor after it has seen the dividend (NEXP01.D):
                    // the same division goes on, the dividend is kept
                    if !matches!(self.div, Some(d) if d.b == b && d.breg == s) {
                        self.div = Some(Div { b, breg: s, a: None });
                    }
                    self.fr[ri] = if b != 0.0 { (1.0 / b).to_bits() } else { 0x7ff0000000000000 };
                }
                11 => {
                    let x = self.d(s);
                    if let Some(d) = &mut self.div {
                        if s != d.breg {
                            d.a = Some(x);
                        }
                    }
                    self.fr[ri] = self.fr[si];
                }
                _ => {}
            },
            FpGroup::Dfp1 => match op {
                1..=7 => {
                    let v = fcmp(op, self.d(s), self.d(t));
                    self.set_br(r, v);
                }
                _ => self.fr[ri] = (self.r(s) as u64) << 32 | self.r(t) as u64, // WFRD
            },
        }
        Ok(None)
    }
}
