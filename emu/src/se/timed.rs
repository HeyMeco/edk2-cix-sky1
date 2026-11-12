// SPDX-License-Identifier: BSD-2-Clause-Patent
//! Two SE images side by side on the model with time (soc.rs, `Timed`):
//! real exception entry, SysTick and WFI, mailboxes with interrupt lines,
//! the watchdog, the PM's and the SFH's handshakes after a delay.
//!
//! Everything is driven by simulated time.  A `Pair` holds CIX's image and
//! the open one; a phase is something done to both (a boot, a request, a
//! wake-up) and what each did meanwhile: how it ended, the registers it
//! wrote (values in order, per register and across registers), the
//! registers it read, its answers, the model's events, the memory it left
//! for later stages.  After each step both are run to the same time on a
//! grid, so the next finds their clocks equal; when a step ends one of
//! them (a reset, a lock-up, no answer) both are put back as they were
//! before it.  `compare` lists what differs between the two images'
//! phases and looks each difference up in the table of expected ones.

use super::soc::*;
use crate::arm::{Arm, Stop, Stub};
use std::collections::{BTreeMap, BTreeSet};
use std::rc::Rc;
use std::sync::Mutex;

/// What one image runs on.
#[derive(Clone)]
pub struct Config {
    pub flash: Rc<Vec<u8>>,
    pub life_cycle: u32,
    pub efuse: BTreeMap<u32, Vec<u8>>,
    /// the DDR driver runs on the model of the controllers (else its entry points are stubbed)
    pub ddr: bool,
    /// DDR features set in the driver's MEMORY_INIT_S: (index, value)
    pub features: Vec<(usize, u8)>,
    pub boot_reason: u32,
    pub jedec: [u8; 3],
    /// the SE's clock divider as the boot ROM left it (core clock 800 MHz / (n + 1))
    pub clkdiv: u32,
    /// SysTick's clock source as the boot ROM left it (true: the core's clock)
    pub systick_core: bool,
    /// changes to the model before the image starts
    pub tweak: Option<Rc<dyn Fn(&mut SkySoc)>>,
    /// memory as it is at reset, besides what the boot ROM leaves: (address, bytes)
    pub preload: Vec<(u32, Vec<u8>)>,
    /// instructions a run may take before the image is given up as spinning (a firmware waiting on a
    /// register for ever costs the host this much for each image)
    pub budget: u64,
    /// the interpreter checks itself on the code it runs (the registers a callee must keep, at every return)
    pub audit: bool,
    /// functions of both images answered by a constant instead of run: (name, value)
    pub constants: Vec<(&'static str, u32)>,
}

impl Config {
    pub fn new(flash: &[u8]) -> Config {
        Config {
            flash: Rc::new(flash.to_vec()),
            life_cycle: 4,
            efuse: BTreeMap::new(),
            ddr: false,
            features: Vec::new(),
            boot_reason: 2,
            jedec: [0xef, 0x60, 0x18],
            clkdiv: 0,
            systick_core: false,
            tweak: None,
            preload: Vec::new(),
            budget: 4_000_000_000,
            audit: false,
            constants: Vec::new(),
        }
    }
}

/// Registers left out of the comparison: UART data, the log-level and postcode-lock scratch registers
pub const IGNORE: [u32; 3] = [0xa0010000, 0xb0030208, 0xb003020c];
/// RAM log (the messages differ), firmware version string
/// and the boot record's times in ms (SE phase and the next: start and end)
pub const IGNORE_RANGES: [(u32, u32); 4] =
    [(0x63da0000, 0x63da4000), (0x63e01000, 0x63e01040), (0x63e04000, 0x63e04010), (0x63e04020, 0x63e04030)];
/// what the SE writes for later stages: compared by its final contents
pub const MEMORY: [(u32, u32); 2] = [(0x60000000, 0xa0000000), (0x46a10000, 0x46b18000)];
/// written on a count of the idle loop's passes, which take the two images different times: only the
/// values are compared (the watchdog's reload)
pub const PERIODIC: [u32; 1] = [0xb0020000];

/// read once for every pass of the idle loop, which the two images make at different speeds: the idle request
/// (the UART's flags, read for every character of logs that differ, are left out altogether)
pub const READ_POLLED: [u32; 1] = [0xb003022c];

/// Written, and read, once in every pass of the idle loop while the RAM log is off (a pass is then a delay
/// of 1 ms): a phase may end a pass earlier in one image than in the other.  (register, how many more)
pub const TICK_WRITES: [(u32, usize); 3] = [(0xe000e010, 2), (0xe000e014, 1), (0xe000e018, 1)];
pub const TICK_READS: [(u32, u32); 3] = [(0xe000e010, 8), (0xb0030004, 2), (0x4661008c, 1)];

pub fn is_memory(addr: u32) -> bool {
    MEMORY.iter().any(|(lo, hi)| *lo <= addr && addr < *hi)
}

fn ignored(addr: u32) -> bool {
    IGNORE.contains(&addr) || is_memory(addr)
}

pub struct Sim {
    pub cpu: Arm<SkySoc>,
    pub cix: bool,
    /// why the image runs no more (a reset it asked for, a lock-up)
    pub dead: Option<String>,
    /// the register read last when the budget ran out
    pub stuck_on: Option<u32>,
    budget: u64,
    // where the phase being recorded started
    mark_log: usize,
    mark_sent: usize,
    mark_uart: usize,
    mark_post: usize,
}

impl Clone for Sim {
    fn clone(&self) -> Sim {
        Sim {
            cpu: self.cpu.clone(),
            cix: self.cix,
            dead: self.dead.clone(),
            stuck_on: self.stuck_on,
            budget: self.budget,
            mark_log: self.mark_log,
            mark_sent: self.mark_sent,
            mark_uart: self.mark_uart,
            mark_post: self.mark_post,
        }
    }
}

impl Sim {
    pub fn new(elf: &str, cix: bool, cfg: &Config, stubs: &[(&str, Stub<SkySoc>)]) -> Result<Sim, String> {
        let mut soc = SkySoc::new(&cfg.flash, cfg.life_cycle, cfg.efuse.clone(), cfg.ddr);
        soc.flash = cfg.flash.clone();
        soc.boot_reason = cfg.boot_reason;
        soc.jedec = cfg.jedec;
        soc.t.on = true;
        if let Some(f) = &cfg.tweak {
            f(&mut soc);
        }
        let mut cpu = Arm::new(soc);
        cpu.add_ram(SRAM, SRAM_SIZE);
        cpu.load_elf(elf)?;
        cpu.spin_sleeps = true;
        cpu.audit = cfg.audit;
        cpu.reads = Some(Default::default());
        cpu.reads_by_caller = std::env::var("SKY1_READS_BY_CALLER").is_ok();
        cpu.io.tick = 3 * (cfg.clkdiv + 1);
        cpu.io.systick_unit = [cpu.io.tick, REF_TICK as u32];
        let soc = cpu.bus.clone();
        soc.reset_state(&mut cpu.mem);
        // what the boot ROM leaves in registers the firmware reads
        for (addr, data) in &cfg.preload {
            cpu.write_bytes(*addr, data);
        }
        cpu.mem.write(0xb0030004, 4, cfg.clkdiv);
        cpu.mem.write(0xe000e010, 4, if cfg.systick_core { 4 } else { 0 });
        for (name, f) in stubs {
            if let Some(a) = cpu.sym(name) {
                cpu.set_stub(a, f.clone());
            }
        }
        if !cfg.features.is_empty() {
            cpu.set_hook(cpu.addr("MemDramFunctionalInit"), super::pyref::ddr_feature_hook(cfg.features.clone()));
        }
        cpu.reset(SRAM);
        Ok(Sim { cpu, cix, dead: None, stuck_on: None, budget: cfg.budget, mark_log: 0, mark_sent: 0, mark_uart: 0, mark_post: 0 })
    }

    pub fn time(&self) -> u64 {
        self.cpu.io.time
    }

    pub fn soc(&mut self) -> &mut SkySoc {
        &mut self.cpu.bus
    }

    /// Run until time `t`, or until a stop address (returned), or until the image is dead.
    pub fn run_to(&mut self, t: u64) -> Option<String> {
        if self.dead.is_some() {
            self.cpu.io.time = self.cpu.io.time.max(t);
            return None;
        }
        // an instruction budget too: a firmware spinning on a register costs host time, not simulated time alone
        match self.cpu.run(t, self.budget) {
            Stop::Limit => {
                if self.cpu.io.time < t {
                    // (which function of a loop it is in at that instant says nothing: the register it waits on does)
                    self.stuck_on = self.cpu.last_read;
                    self.dead = Some("no end (still running when its instruction budget was spent)".to_string());
                    self.cpu.io.time = t;
                }
                None
            }
            Stop::Wfi | Stop::Spin => {
                self.cpu.io.time = self.cpu.io.time.max(t);
                None
            }
            Stop::At(name) => Some(name),
            s => {
                self.dead = Some(match s {
                    Stop::Bus(why) => why,
                    s => format!("{s} at {:#x} ({})", self.cpu.r[15], self.cpu.func(self.cpu.r[15])),
                });
                self.cpu.io.time = self.cpu.io.time.max(t);
                None
            }
        }
    }

    /// Run until the stop called `name` is reached; false if time `limit` came first.
    pub fn run_to_stop(&mut self, name: &str, limit: u64) -> bool {
        loop {
            match self.run_to(limit) {
                Some(n) if n == name => return true,
                Some(_) => self.cpu.pass_stop = true,
                None => return false,
            }
        }
    }

    /// What the image is doing: for the end of a phase.
    /// Messages the SE has sent since the phase began.
    pub fn sent(&self) -> usize {
        self.cpu.bus.t.sent.len() - self.mark_sent
    }

    /// True if the core is asleep in WFI.
    pub fn asleep(&self) -> bool {
        self.dead.is_none() && self.cpu.sleeping() == Some(&Stop::Wfi) && !self.cpu.wake_pending()
    }

    pub fn state(&self) -> String {
        if let Some(d) = &self.dead {
            return d.clone();
        }
        let pc = self.cpu.r[15];
        let f = self.cpu.func(pc);
        let mode = if self.cpu.in_handler() { format!(", in exception {}", self.cpu.ipsr) } else { String::new() };
        if self.cpu.sleeping() == Some(&Stop::Wfi) {
            format!("asleep (WFI){mode}")
        } else if self.cpu.sleeping() == Some(&Stop::Spin) {
            format!("stopped in {f}{mode}")
        } else if self.cpu.stops.contains_key(&pc) {
            format!("at {f}{mode}")
        } else {
            // (where a running image is at a given instant says nothing)
            format!("running{mode}")
        }
    }

    /// Start recording at reset (later phases start where the one before ended).
    pub fn mark_if_new(&mut self) {
        if self.cpu.count == 0 {
            self.mark();
        }
    }

    /// Start recording a phase.
    pub fn mark(&mut self) {
        self.cpu.writes.clear();
        if let Some(r) = &mut self.cpu.reads {
            r.clear();
        }
        self.cpu.read_order.clear();
        self.mark_log = self.cpu.bus.t.log.len();
        self.mark_sent = self.cpu.bus.t.sent.len();
        self.mark_uart = self.cpu.bus.t.uart.len();
        self.mark_post = self.cpu.bus.t.postcodes.len();
    }

    /// The phase recorded since `mark`.
    pub fn phase(&mut self, name: &str, memory: bool) -> Phase {
        let mut p = Phase { name: name.to_string(), end: self.state(), ..Default::default() };
        for (k, w) in self.cpu.writes.iter().enumerate() {
            if !ignored(w.addr) && !IGNORE_RANGES.iter().any(|(lo, hi)| *lo <= w.addr && w.addr < *hi) {
                let mut v = w.value;
                // SysTick's control register is written back as it was read, COUNTFLAG (read-only) included:
                // whether the count had run out between two delays depends on how long the code between took
                if w.addr == 0xe000e010 {
                    v &= !0x10000;
                }
                // an error's postcode (the lock follows it) has the source line in its low seven bits: the
                // open firmware's lines are its own
                let lock_follows = || {
                    let next = self.cpu.writes[k + 1..].iter().find(|x| x.addr == 0xb003020c || !(ignored(x.addr) || x.addr & 0xfffff000 == 0xe000e000));
                    next.map_or(false, |x| x.addr == 0xb003020c)
                };
                if w.addr == 0xa0020f00 && lock_follows() {
                    v &= !0x7f00;
                    p.events.push(format!("error postcode {:#x} (line bits left out)", v >> 8));
                }
                p.writes.push((w.addr, v));
            }
        }
        p.nwrites = self.cpu.writes.len();
        let reads = self.cpu.reads.as_ref().unwrap();
        for addr in &self.cpu.read_order {
            if ignored(*addr) || *addr == 0xa0010018 {
                continue;
            }
            let r = &reads[addr];
            p.reads.push(*addr);
            p.read_counts.insert(*addr, r.count);
            p.read_by.insert(*addr, self.cpu.func(r.by[0].0));
            for (pc, n) in &r.by {
                *p.read_fns.entry(*addr).or_default().entry(self.cpu.func(*pc)).or_default() += n;
            }
        }
        let t = &self.cpu.bus.t;
        p.events.extend(t.log[self.mark_log..].iter().map(|(_, e)| e.clone()));
        p.sent = t.sent[self.mark_sent..].to_vec();
        // the UART's lines, each with the time (ms) it started
        let mut starts: Vec<(u64, usize)> = t.uart_lines.iter().filter(|l| l.1 >= self.mark_uart).copied().collect();
        starts.push((0, t.uart.len()));
        for w in starts.windows(2) {
            let line = String::from_utf8_lossy(&t.uart[w[0].1..w[1].1]);
            p.uart.push_str(&format!("{:9.3} {}\n", w[0].0 as f64 / MS as f64, line.trim_end()));
        }
        p.postcodes = t.postcodes[self.mark_post..].to_vec();
        p.time = self.time();
        if memory {
            for key in self.cpu.mem.keys() {
                let own = key <= u32::MAX as u64;
                if own && !is_memory(key as u32) {
                    continue;
                }
                let mut data = self.cpu.mem.key_data(key).unwrap().to_vec();
                if own {
                    for (lo, hi) in IGNORE_RANGES {
                        for a in lo.max(key as u32)..hi.min((key as u32).saturating_add(0x10000)) {
                            data[(a - key as u32) as usize] = 0;
                        }
                    }
                }
                if data.iter().any(|b| *b != 0) {
                    p.mem.insert(key, data);
                }
            }
            p.flash = Some(self.cpu.bus.flash.clone());
        }
        self.mark();
        p
    }

    /// Put a request in an AP->SE register channel (0: non-secure, 2: TEE), raise its interrupt and wait
    /// for the acknowledgement; the answer's words.  The message's words are as given (the first has the
    /// size).
    pub fn request_raw(&mut self, chan: usize, msg: &[u32], limit: u64) -> String {
        if self.dead.is_some() {
            return format!("dead: {}", self.dead.as_ref().unwrap());
        }
        let base = MBOX_BASE + 0x10000 * chan as u32;
        // the AP writes the whole message area: the size in the first word, the function, the parameters
        for i in 0..32u32 {
            self.cpu.mem.write(base + 4 * i, 4, 0);
        }
        for (i, w) in msg.iter().enumerate().take(32) {
            self.cpu.mem.write(base + 4 * i as u32, 4, *w);
        }
        self.cpu.bus.t.mbox_acked[chan] = false;
        let cpu = &mut self.cpu;
        cpu.bus.mbox_raise(&mut cpu.io, &cpu.mem, chan, 1);
        let end = self.time() + limit;
        loop {
            let t = (self.time() + 10 * US).min(end);
            self.run_to(t);
            if let Some(d) = &self.dead {
                return format!("dead: {d}");
            }
            if self.cpu.bus.t.mbox_acked[chan] && !self.cpu.in_handler() {
                break;
            }
            if self.time() >= end {
                // take the request back, as an AP giving up would
                self.cpu.bus.t.mbox_status[chan] &= !1;
                let cpu = &mut self.cpu;
                cpu.bus.mbox_raise(&mut cpu.io, &cpu.mem, chan, 0);
                return format!("no answer ({})", self.state());
            }
        }
        let n = ((self.cpu.read32(base) & 0x7f) + 3) / 4;
        let v: Vec<String> = (0..n.min(32)).map(|i| format!("{:x}", self.cpu.read32(base + 4 * i))).collect();
        v.join(" ")
    }
}

/// What an image did in a phase.
#[derive(Clone, Default)]
pub struct Phase {
    pub name: String,
    pub end: String,
    /// registers written (the ignored ones left out), in order
    pub writes: Vec<(u32, u32)>,
    /// all bus writes, for the report
    pub nwrites: usize,
    /// registers read, in the order they were first read, and the function that read each first
    pub reads: Vec<u32>,
    pub read_by: BTreeMap<u32, String>,
    /// how often each was read, and by which functions
    pub read_counts: BTreeMap<u32, u32>,
    pub read_fns: BTreeMap<u32, BTreeMap<String, u32>>,
    /// (question, answer)
    pub answers: Vec<(String, String)>,
    pub events: Vec<String>,
    pub sent: Vec<(usize, Vec<u32>)>,
    pub uart: String,
    pub postcodes: Vec<u32>,
    pub time: u64,
    /// page key -> contents (pages of all zeros left out)
    pub mem: BTreeMap<u64, Vec<u8>>,
    pub flash: Option<Rc<Vec<u8>>>,
}

/// A difference between the two images in a phase.
#[derive(Clone, Debug)]
pub struct Diff {
    pub phase: String,
    pub what: String,
    pub cix: String,
    pub open: String,
}

/// An expected difference: a phase whose name has `phase`, a difference whose `what` has `what`, and the
/// two sides having `cix` and `open` (empty: anything).
pub struct Expected {
    pub phase: &'static str,
    pub what: &'static str,
    pub cix: &'static str,
    pub open: &'static str,
    pub why: &'static str,
}

fn per_register(w: &[(u32, u32)]) -> BTreeMap<u32, Vec<u32>> {
    let mut m: BTreeMap<u32, Vec<u32>> = BTreeMap::new();
    for (a, v) in w {
        m.entry(*a).or_default().push(*v);
    }
    m
}

fn hexes(v: &[u32], n: usize) -> String {
    let n = if std::env::var("SKY1_FULL").is_ok() { usize::MAX } else { n };
    let mut s: Vec<String> = v.iter().take(n).map(|x| format!("{x:x}")).collect();
    if v.len() > n {
        s.push(format!("... ({})", v.len()));
    }
    s.join(" ")
}

/// What differs between CIX's phase and the open image's.
pub fn compare(a: &Phase, b: &Phase) -> Vec<Diff> {
    let mut out = Vec::new();
    let mut add = |what: String, cix: String, open: String| out.push(Diff { phase: a.name.clone(), what, cix, open });
    if a.end != b.end {
        add("end".into(), a.end.clone(), b.end.clone());
    }
    // both cut off in a loop that would not end (a wait for something the model does not give): how far
    // each got in the same number of instructions says nothing; what they wrote is compared as far as
    // both got, what they read by the registers alone
    let spinning = a.end.starts_with("no end") && b.end.starts_with("no end");
    let (ra, rb) = (per_register(&a.writes), per_register(&b.writes));
    let regs: BTreeSet<u32> = ra.keys().chain(rb.keys()).copied().collect();
    let mut same = true;
    let mut ticked = false;
    for r in &regs {
        let (mut va, mut vb) = (ra.get(r).cloned().unwrap_or_default(), rb.get(r).cloned().unwrap_or_default());
        if spinning && !va.is_empty() && !vb.is_empty() {
            let n = va.len().min(vb.len());
            va.truncate(n);
            vb.truncate(n);
        }
        let tick = TICK_WRITES.iter().find(|t| t.0 == *r).map(|t| t.1);
        let n = va.len().min(vb.len());
        if va != vb && tick.map_or(false, |t| va[..n] == vb[..n] && va.len().abs_diff(vb.len()) <= t) {
            ticked = true;
            continue;
        }
        if PERIODIC.contains(r) {
            let (sa, sb): (BTreeSet<u32>, BTreeSet<u32>) = (va.iter().copied().collect(), vb.iter().copied().collect());
            if sa != sb && !va.is_empty() && !vb.is_empty() {
                add(format!("register {r:#010x} (periodic)"), format!("{sa:x?}"), format!("{sb:x?}"));
            }
        } else if va != vb {
            same = false;
            add(format!("register {r:#010x}"), hexes(&va, 24), hexes(&vb, 24));
        }
    }
    if same && !spinning {
        // the same values to every register: the order across registers
        let strip = |w: &[(u32, u32)]| {
            w.iter()
                .filter(|x| !PERIODIC.contains(&x.0) && !(ticked && TICK_WRITES.iter().any(|t| t.0 == x.0)))
                .copied()
                .collect::<Vec<_>>()
        };
        let (wa, wb) = (strip(&a.writes), strip(&b.writes));
        if wa != wb {
            let i = wa.iter().zip(&wb).position(|(x, y)| x != y).unwrap_or(0);
            let show = |w: &[(u32, u32)]| {
                w[i..].iter().take(4).map(|(a, v)| format!("{a:x}={v:x}")).collect::<Vec<_>>().join(" ")
            };
            let moved = crate::util::diff(&wa, &wb).iter().filter(|e| !matches!(e, crate::util::Edit::Same(..))).count();
            add(format!("order of writes (from write {i}, {moved} out of place)"), show(&wa), show(&wb));
        }
    }
    let (sa, sb): (BTreeSet<u32>, BTreeSet<u32>) = (a.reads.iter().copied().collect(), b.reads.iter().copied().collect());
    // registers only one of them read: runs of neighbours read by the same function as one
    let by = |p: &Phase, r: &u32| p.read_by.get(r).map_or("not read".to_string(), |f| format!("read by {f}"));
    let only: Vec<u32> = sa.symmetric_difference(&sb).copied().collect();
    let mut i = 0;
    while i < only.len() {
        let mut j = i;
        while j + 1 < only.len() && only[j + 1] - only[j] <= 4 && by(a, &only[j + 1]) == by(a, &only[i]) && by(b, &only[j + 1]) == by(b, &only[i]) {
            j += 1;
        }
        let what = if j > i { format!("read of {:#010x}..{:#010x} ({} addresses)", only[i], only[j], j - i + 1) } else { format!("read of {:#010x}", only[i]) };
        add(what, by(a, &only[i]), by(b, &only[i]));
        i = j + 1;
    }
    for r in sa.intersection(&sb) {
        let tick = TICK_READS.iter().find(|t| t.0 == *r).map_or(0, |t| t.1);
        if a.read_counts[r].abs_diff(b.read_counts[r]) > tick && !READ_POLLED.contains(r) && !spinning {
            let by = |p: &Phase| {
                let v: Vec<String> = p.read_fns[r].iter().map(|(f, n)| format!("{f} {n}")).collect();
                format!("{} ({})", p.read_counts[r], v.join(", "))
            };
            add(format!("reads of {r:#010x}"), by(a), by(b));
        }
    }
    if sa == sb && a.reads != b.reads && !spinning {
        let i = a.reads.iter().zip(&b.reads).position(|(x, y)| x != y).unwrap_or(0);
        add(format!("order of first reads (from {i})"), hexes(&a.reads[i..], 4), hexes(&b.reads[i..], 4));
    }
    for (k, (q, x)) in a.answers.iter().enumerate() {
        match b.answers.get(k) {
            Some((_, y)) if x == y => {}
            Some((_, y)) => add(format!("answer to {q}"), x.clone(), y.clone()),
            None => add(format!("answer to {q}"), x.clone(), "(not asked)".into()),
        }
    }
    if a.events != b.events {
        let i = a.events.iter().zip(&b.events).position(|(x, y)| x != y).unwrap_or(a.events.len().min(b.events.len()));
        let show = |e: &[String]| e.get(i).cloned().unwrap_or_else(|| "(none)".into());
        add(format!("event {i}"), show(&a.events), show(&b.events));
    }
    let keys: BTreeSet<u64> = a.mem.keys().chain(b.mem.keys()).copied().collect();
    let zero = vec![0u8; 0x10000];
    for k in keys {
        let (pa, pb) = (a.mem.get(&k).unwrap_or(&zero), b.mem.get(&k).unwrap_or(&zero));
        if pa != pb {
            let d: Vec<usize> = (0..0x10000).filter(|i| pa[*i] != pb[*i]).collect();
            let at = k + d[0] as u64;
            let show = |p: &[u8]| crate::util::hex(&p[d[0]..(d[0] + 8).min(0x10000)]);
            add(format!("memory {at:#x} ({} bytes of page {k:#x})", d.len()), show(pa), show(pb));
        }
    }
    if let (Some(fa), Some(fb)) = (&a.flash, &b.flash) {
        if fa != fb {
            let d: Vec<usize> = (0..fa.len()).filter(|i| fa[*i] != fb[*i]).collect();
            add(format!("flash {:#x} ({} bytes)", d[0], d.len()), format!("{:02x}", fa[d[0]]), format!("{:02x}", fb[d[0]]));
        }
    }
    out
}

/// CIX's image and the open one, in step.
pub struct Pair {
    pub a: Sim,
    pub b: Sim,
    /// (CIX's phase, the open image's) so far
    pub phases: Vec<(Phase, Phase)>,
    saved: Option<(Sim, Sim)>,
    /// coverage of copies thrown away: per image
    pub cov: [Vec<(u32, u32)>; 2],
    answers: (Vec<(String, String)>, Vec<(String, String)>),
    pub grid: u64,
    /// each image's time at the last `sync`
    t0: (u64, u64),
    /// go back to the last `save` when the phase is closed
    scratch: bool,
}

impl Pair {
    pub fn new(a: Sim, b: Sim) -> Pair {
        Pair { a, b, phases: Vec::new(), saved: None, cov: Default::default(), answers: Default::default(), grid: 100 * US, t0: (0, 0), scratch: false }
    }

    pub fn each(&mut self, mut f: impl FnMut(&mut Sim)) {
        f(&mut self.a);
        f(&mut self.b);
    }

    /// The time each has run since `sync`.
    fn elapsed(&self) -> (u64, u64) {
        (self.a.time() - self.t0.0, self.b.time() - self.t0.1)
    }

    /// From here on the two count time together (their clocks need not be equal: an image that took
    /// longer to get here is not made to wait for it).
    pub fn sync(&mut self) {
        self.t0 = (self.a.time(), self.b.time());
    }

    /// Both to the next time on the grid, by the time they share: the one that was faster waits.
    pub fn align(&mut self) {
        let (ea, eb) = self.elapsed();
        let t = (ea.max(eb) / self.grid + 1) * self.grid;
        let (ta, tb) = (self.t0.0 + t, self.t0.1 + t);
        while self.a.run_to(ta).is_some() {
            self.a.cpu.pass_stop = true;
        }
        while self.b.run_to(tb).is_some() {
            self.b.cpu.pass_stop = true;
        }
    }

    /// Let `t` pass in both.
    pub fn settle(&mut self, t: u64) {
        let (ea, eb) = self.elapsed();
        let end = ea.max(eb) + t;
        let (ta, tb) = (self.t0.0 + end, self.t0.1 + end);
        self.a.run_to(ta);
        self.b.run_to(tb);
        self.align();
    }

    /// Run both until `done` says so of each, for at most `limit`; then to the grid.
    pub fn until(&mut self, limit: u64, done: impl Fn(&Sim) -> bool) {
        for s in [&mut self.a, &mut self.b] {
            let end = s.time() + limit;
            while !done(s) && s.dead.is_none() && s.time() < end {
                let t = (s.time() + 20 * US).min(end);
                if s.run_to(t).is_some() {
                    s.cpu.pass_stop = true;
                }
            }
        }
        self.align();
    }

    /// Both as they were at `base` (a suite starts from the booted images, whatever the one before did).
    pub fn reset(&mut self, base: &(Sim, Sim)) {
        self.cov[0].extend(self.a.cpu.covered());
        self.cov[1].extend(self.b.cpu.covered());
        self.a = base.0.clone();
        self.b = base.1.clone();
        self.a.mark();
        self.b.mark();
        self.t0 = (self.a.time(), self.b.time());
        self.saved = None;
    }

    /// Keep both as they are now, to come back to.
    pub fn save(&mut self) {
        self.saved = Some((self.a.clone(), self.b.clone()));
    }

    /// Back to the last `save` (the coverage of what is thrown away is kept).
    pub fn restore(&mut self) {
        if let Some((a, b)) = &self.saved {
            self.cov[0].extend(self.a.cpu.covered());
            self.cov[1].extend(self.b.cpu.covered());
            self.a = a.clone();
            self.b = b.clone();
        }
    }

    /// Ask both the same thing; when it ends one of them, both go back to where they were.
    pub fn ask(&mut self, name: &str, chan: usize, fid: u32, words: &[u32]) {
        let mut msg = vec![8 + 4 * words.len() as u32, fid];
        msg.extend_from_slice(words);
        self.ask_raw(name, chan, &msg)
    }

    pub fn ask_raw(&mut self, name: &str, chan: usize, msg: &[u32]) {
        self.save();
        let (x, y) = (self.a.request_raw(chan, msg, 300 * MS), self.b.request_raw(chan, msg, 300 * MS));
        let bad = |s: &str| s.starts_with("dead") || s.starts_with("no answer");
        let undo = bad(&x) || bad(&y);
        self.answers.0.push((name.to_string(), x));
        self.answers.1.push((name.to_string(), y));
        if undo {
            self.restore();
        } else {
            self.align();
        }
    }

    /// Ask, and put both back as they were whatever the answer (for requests that may leave an image
    /// running but damaged).
    pub fn ask_scratch(&mut self, name: &str, chan: usize, fid: u32, words: &[u32]) {
        self.ask(name, chan, fid, words);
        self.scratch = true;
    }

    /// Something found out about each image, to be compared like an answer.
    pub fn note(&mut self, name: &str, f: impl Fn(&Sim) -> String) {
        self.answers.0.push((name.to_string(), f(&self.a)));
        self.answers.1.push((name.to_string(), f(&self.b)));
    }

    /// Close the phase: what both did since the last one.
    pub fn phase(&mut self, name: &str, memory: bool) {
        let (mut pa, mut pb) = (self.a.phase(name, memory), self.b.phase(name, memory));
        pa.answers = std::mem::take(&mut self.answers.0);
        pb.answers = std::mem::take(&mut self.answers.1);
        self.phases.push((pa, pb));
        if std::mem::take(&mut self.scratch) {
            self.restore();
            self.a.mark();
            self.b.mark();
        }
    }

    /// What this pair ran of each image, the copies it threw away included.
    fn own_coverage(&self) -> [Vec<(u32, u32)>; 2] {
        let mut c = self.cov.clone();
        c[0].extend(self.a.cpu.covered());
        c[1].extend(self.b.cpu.covered());
        c
    }

    /// What every pair of this run ran of each image: this one and the ones already dropped.
    pub fn coverage(&self) -> [Vec<(u32, u32)>; 2] {
        let mut c = self.own_coverage();
        let d = DROPPED.lock().unwrap();
        c[0].extend(&d[0]);
        c[1].extend(&d[1]);
        c
    }
}

/// The coverage of the pairs that are gone (a suite boots one for each of its cases).
static DROPPED: Mutex<[Vec<(u32, u32)>; 2]> = Mutex::new([Vec::new(), Vec::new()]);

impl Drop for Pair {
    fn drop(&mut self) {
        let c = self.own_coverage();
        let mut d = DROPPED.lock().unwrap();
        for i in 0..2 {
            d[i].extend(&c[i]);
            d[i].sort_unstable();
            d[i].dedup();
        }
    }
}

/// Print the phases' comparison; the number of differences not expected.
pub fn report(phases: &[(Phase, Phase)], expected: &[Expected], verbose: bool, used: &mut BTreeMap<&'static str, usize>) -> usize {
    let mut bad = 0;
    for (a, b) in phases {
        let diffs = compare(a, b);
        let mut lines = Vec::new();
        let mut n_exp = 0;
        // an entry with what "*" is for a phase that is one question: when the two answers are as it says,
        // everything else that differs in the phase follows from them
        // (and one with what "end*" for a phase the two images end differently, as it says)
        let answer = diffs.iter().find(|d| d.what.starts_with("answer to"));
        let end = diffs.iter().find(|d| d.what == "end");
        let find = |d: &Diff, what: &str| {
            expected.iter().find(|e| e.what == what && d.phase.contains(e.phase) && d.cix.contains(e.cix) && d.open.contains(e.open))
        };
        let whole = answer.and_then(|d| find(d, "*")).or_else(|| end.and_then(|d| find(d, "end*")));
        for d in &diffs {
            let e = whole.or_else(|| {
                expected.iter().find(|e| {
                    e.what != "*" && e.what != "end*" && d.phase.contains(e.phase) && d.what.contains(e.what) && d.cix.contains(e.cix) && d.open.contains(e.open)
                })
            });
            match e {
                Some(e) => {
                    n_exp += 1;
                    *used.entry(e.why).or_default() += 1;
                    if verbose {
                        lines.push(format!("     expected: {}: CIX {} | open {}", d.what, d.cix, d.open));
                    }
                }
                None => {
                    bad += 1;
                    lines.push(format!("     {}: CIX {} | open {}", d.what, d.cix, d.open));
                }
            }
        }
        let regs = per_register(&b.writes).len();
        let note = if n_exp != 0 { format!(", {n_exp} expected differences") } else { String::new() };
        if lines.is_empty() && !verbose && a.answers.len() == 1 {
            continue;
        }
        println!(
            "{} {}: {} ({} writes to {regs} registers, {} read, {} answers{note})",
            if lines.iter().any(|l| !l.starts_with("     expected")) { "DIFF" } else { "ok  " },
            a.name,
            b.end,
            b.writes.len(),
            b.reads.len(),
            b.answers.len(),
        );
        if verbose {
            for (q, x) in &b.answers {
                println!("       {q}: {x}");
            }
        }
        for l in lines {
            println!("{l}");
        }
    }
    bad
}
