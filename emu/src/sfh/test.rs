// SPDX-License-Identifier: BSD-2-Clause-Patent
//! The differential test of the open SFH firmware against CIX's
//! sfh_fw.bin on a model with time: `sfh-test OPEN_sfh_fw.bin [CIX.bin]`.
//!
//! The model (what the two images make of it, not a data sheet):
//!
//! * SRAM at 0x20000000 with the image at its start (where the SE puts it:
//!   stack pointer and reset vector in its first two words); address 0
//!   shows the first 4 KiB of it (CIX's image logs through a UART pointer
//!   that is 0);
//! * the timer block (0x50010000, eight blocks of 0x1000): a block counts
//!   down from its load value at 25 MHz while enabled (control bit 17),
//!   stops at 0 and raises its status bit 0 and its interrupt (16 and 17
//!   for the two 64-bit pairs, 18 to 21 for the 32-bit ones) until the
//!   status is cleared (0x34);
//! * four mailbox FIFO channels (0x50080000 + n * 0x10000, interrupts 23
//!   to 26): a receiving channel has the words the peer sent (0xb0 takes
//!   one, 0xb4 bit 2: empty), status 0x14 while it has any and 0x80 after
//!   a read of the empty FIFO; a sending one takes words (0xac), gives
//!   them to the peer 20 us after "sent" is asked for (0xbc bit 3) and
//!   then says done (status 8), overflow (0x40) past 32 words.  The
//!   interrupt is up while a status bit is enabled (0xbc, 0xc0);
//! * the control block (0x50070000): memory; the wake-up request (0x80)
//!   and the handshake (0x100) are noted with their time.
//!
//! The core runs at 100 MHz (not known; only the timers' 25 MHz matters to
//! what is compared).  A wait that polls a timer's count passes the time
//! to its end at once.
//!
//! Both images are driven in step, by simulated time: a phase is something
//! done to both (the boot, a message, an interrupt) until both are idle
//! again; compared are every write outside SRAM in order, the registers
//! read and how often, the words sent, and the times of the wake-up
//! requests (to 100 us).  EXPECTED lists the differences that are meant.

use crate::arm::{Arm, Bus, Io, Mem, Stop};
use std::collections::{BTreeMap, VecDeque};

pub const SRAM: u32 = 0x20000000;
pub const SRAM_SIZE: u32 = 0x80000;
/// time units: 5 ns
pub const US: u64 = 200;
pub const MS: u64 = 1000 * US;
pub const SEC: u64 = 1000 * MS;
/// units a count of the 25 MHz timers takes, and an instruction (100 MHz)
const TIMER_TICK: u64 = 8;
const CORE_TICK: u32 = 2;
const TIMERS: u32 = 0x50010000;
const MBOX: u32 = 0x50080000;
const CTRL: u32 = 0x50070000;
const FIFO_DEPTH: usize = 32;

#[derive(Clone, Default)]
struct Timer {
    /// the time the count reaches 0, while it runs
    end: Option<u64>,
    status: u32,
}

#[derive(Clone, Default)]
struct Chan {
    rx: VecDeque<u32>,
    tx: Vec<u32>,
    status: u32,
}

#[derive(Clone, Default)]
pub struct Model {
    timers: [Timer; 8],
    chans: [Chan; 4],
    /// what happens later: (time, channel) the peer takes a sent message
    taken: Vec<(u64, usize)>,
    /// (time, event)
    pub log: Vec<(u64, String)>,
    /// the last read of a timer's count: (instruction, timer), to tell a poll
    poll: (u32, usize, usize),
}

fn timer_irq(n: usize) -> u32 {
    if n < 4 {
        16 + n as u32 / 2
    } else {
        18 + (n as u32 - 4)
    }
}

impl Model {
    fn timer_line(&self, io: &mut Io, n: usize) {
        // a pair of 64-bit halves shares its interrupt
        let up = if n < 4 { self.timers[n & !1].status | self.timers[n | 1].status != 0 } else { self.timers[n].status != 0 };
        io.set_line(timer_irq(n), up);
    }

    fn chan_line(&self, io: &mut Io, mem: &Mem, ch: usize, en: Option<(u32, u32)>) {
        let base = MBOX + 0x10000 * ch as u32;
        let reg = |off: u32| match en {
            Some((o, v)) if o == off => v,
            _ => mem.read32(base + off),
        };
        io.set_line(23 + ch as u32, self.chans[ch].status & (reg(0xbc) | reg(0xc0)) != 0);
    }

    /// The peer puts words in a receiving channel's FIFO.
    pub fn send(&mut self, io: &mut Io, mem: &Mem, ch: usize, words: &[u32]) {
        for w in words {
            if self.chans[ch].rx.len() < FIFO_DEPTH {
                self.chans[ch].rx.push_back(*w);
            } else {
                self.chans[ch].status |= 0x40;
            }
        }
        if !self.chans[ch].rx.is_empty() {
            self.chans[ch].status |= 0x14;
        }
        self.chan_line(io, mem, ch, None);
    }

    /// Raise status bits of a channel, or of a timer, as the hardware would.
    pub fn raise(&mut self, io: &mut Io, mem: &Mem, irq: u32) {
        match irq {
            16 | 17 => {
                self.timers[2 * (irq as usize - 16)].status |= 1;
                self.timer_line(io, 2 * (irq as usize - 16));
            }
            18..=21 => {
                self.timers[irq as usize - 14].status |= 1;
                self.timer_line(io, irq as usize - 14);
            }
            _ => {
                let ch = irq as usize - 23;
                self.chans[ch].status |= if ch % 2 == 0 { 0x48 } else { 0x80 };
                self.chan_line(io, mem, ch, None);
            }
        }
    }
}

impl Bus for Model {
    fn read(&mut self, io: &mut Io, mem: &mut Mem, addr: u32, _size: u32) -> Option<u32> {
        let poll = std::mem::take(&mut self.poll);
        if addr & 0xffff8000 == TIMERS {
            let n = ((addr - TIMERS) >> 12) as usize;
            return match addr & 0xfff {
                0x1c => {
                    let t = &self.timers[n];
                    let Some(end) = t.end else { return Some(0) };
                    // the instruction that read this count last reads it again, nothing else in between: a
                    // wait for it to run out, which it does at once
                    if poll == (io.pc, n, io.nwrites) && end > io.time {
                        io.time = end;
                    }
                    self.poll = (io.pc, n, io.nwrites);
                    Some((end.saturating_sub(io.time)).div_ceil(TIMER_TICK) as u32)
                }
                0x30 => Some(self.timers[n].status),
                _ => None,
            };
        }
        if addr & 0xfffc0000 == MBOX {
            let ch = ((addr - MBOX) >> 16) as usize;
            let c = &mut self.chans[ch];
            return match addr & 0xffff {
                0xb0 => {
                    let w = c.rx.pop_front();
                    if w.is_none() {
                        c.status |= 0x80;
                    }
                    if c.rx.is_empty() {
                        c.status &= !0x14;
                    }
                    self.chan_line(io, mem, ch, None);
                    Some(w.unwrap_or(0))
                }
                0xb4 => Some(if c.rx.is_empty() { 4 } else { 0 }),
                0xc8 => Some(c.status),
                0xc4 => Some(0),
                _ => None,
            };
        }
        None
    }

    fn write(&mut self, io: &mut Io, mem: &mut Mem, addr: u32, _size: u32, value: u32) {
        if addr & 0xffff8000 == TIMERS {
            let n = ((addr - TIMERS) >> 12) as usize;
            match addr & 0xfff {
                0x24 => {
                    let was = mem.read32(addr) & 1 << 17 != 0;
                    if value & 1 << 17 != 0 && !was {
                        let load = mem.read32(TIMERS + 0x1000 * n as u32) as u64;
                        self.timers[n].end = Some(io.time + load * TIMER_TICK);
                    } else if value & 1 << 17 == 0 {
                        self.timers[n].end = None;
                    }
                }
                0x34 => {
                    self.timers[n].status &= !value;
                    self.timer_line(io, n);
                }
                _ => {}
            }
        } else if addr & 0xfffc0000 == MBOX {
            let ch = ((addr - MBOX) >> 16) as usize;
            match addr & 0xffff {
                0xac => {
                    if self.chans[ch].tx.len() < FIFO_DEPTH {
                        self.chans[ch].tx.push(value);
                    } else {
                        self.chans[ch].status |= 0x40;
                    }
                    self.chan_line(io, mem, ch, None);
                }
                0xbc => {
                    if value & 8 != 0 && mem.read32(addr) & 8 == 0 {
                        self.taken.push((io.time + 20 * US, ch));
                    }
                    self.chan_line(io, mem, ch, Some((0xbc, value)));
                }
                0xc0 => self.chan_line(io, mem, ch, Some((0xc0, value))),
                0xc4 => {
                    self.chans[ch].status &= !value;
                    if !self.chans[ch].rx.is_empty() {
                        self.chans[ch].status |= 0x14;
                    }
                    self.chan_line(io, mem, ch, None);
                }
                _ => {}
            }
        } else if addr == CTRL + 0x80 {
            self.log.push((io.time, format!("wake-up request {value:#x}")));
        } else if addr == CTRL + 0x100 {
            self.log.push((io.time, format!("handshake {value:#x}")));
        }
    }

    fn next_event(&mut self, _io: &Io) -> Option<u64> {
        let t = self.timers.iter().filter_map(|t| t.end).chain(self.taken.iter().map(|t| t.0)).min();
        t
    }

    fn event(&mut self, io: &mut Io, mem: &mut Mem) {
        for n in 0..8 {
            if self.timers[n].end.map_or(false, |e| e <= io.time) {
                // (a count that has run out stays at 0: the firmware starts each wait anew)
                self.timers[n].end = None;
                self.timers[n].status |= 1;
                self.timer_line(io, n);
            }
        }
        let due: Vec<usize> = self.taken.iter().filter(|t| t.0 <= io.time).map(|t| t.1).collect();
        self.taken.retain(|t| t.0 > io.time);
        for ch in due {
            let words = std::mem::take(&mut self.chans[ch].tx);
            self.log.push((io.time, format!("channel {ch}: sent {words:x?}")));
            self.chans[ch].status |= 8;
            self.chan_line(io, mem, ch, None);
        }
    }
}

pub struct Sfh {
    pub cpu: Arm<Model>,
    pub dead: Option<String>,
    mark_log: usize,
}

impl Clone for Sfh {
    fn clone(&self) -> Sfh {
        Sfh { cpu: self.cpu.clone(), dead: self.dead.clone(), mark_log: self.mark_log }
    }
}

impl Sfh {
    /// The image at the start of SRAM, the core at its reset vector.
    pub fn new(image: &[u8], elf: Option<&str>) -> Result<Sfh, String> {
        let mut cpu = Arm::new(Model::default());
        cpu.add_ram(SRAM, SRAM_SIZE);
        cpu.mem.add_ram(0, 0x1000);
        if let Some(e) = elf {
            cpu.load_elf(e)?;
        }
        cpu.write_bytes(SRAM, image);
        cpu.write_bytes(0, &image[..image.len().min(0x1000)]);
        cpu.spin_sleeps = true;
        cpu.reads = Some(Default::default());
        cpu.io.tick = CORE_TICK;
        cpu.reset(SRAM);
        Ok(Sfh { cpu, dead: None, mark_log: 0 })
    }

    pub fn time(&self) -> u64 {
        self.cpu.io.time
    }

    pub fn idle(&self) -> bool {
        self.cpu.sleeping().is_some() && !self.cpu.in_handler() && !self.cpu.wake_pending()
    }

    pub fn run_to(&mut self, t: u64) {
        if self.dead.is_some() {
            self.cpu.io.time = self.cpu.io.time.max(t);
            return;
        }
        match self.cpu.run(t, 2_000_000_000) {
            Stop::Limit if self.cpu.io.time < t => {
                self.dead = Some(format!("no end at {:#x} ({})", self.cpu.r[15], self.cpu.func(self.cpu.r[15])));
                self.cpu.io.time = t;
            }
            Stop::Limit | Stop::Wfi | Stop::Spin => self.cpu.io.time = self.cpu.io.time.max(t),
            s => {
                self.dead = Some(format!("{s} at {:#x} ({})", self.cpu.r[15], self.cpu.func(self.cpu.r[15])));
                self.cpu.io.time = self.cpu.io.time.max(t);
            }
        }
    }

    /// Run until the image is idle (or dead), for at most `limit`.
    pub fn settle(&mut self, limit: u64) {
        let end = self.time() + limit;
        while self.dead.is_none() && self.time() < end {
            let t = (self.time() + 50 * US).min(end);
            self.run_to(t);
            if self.idle() && self.cpu.bus.next_event(&self.cpu.io).is_none() {
                break;
            }
        }
    }

    fn state(&self) -> String {
        match (&self.dead, self.cpu.sleeping()) {
            (Some(d), _) => d.clone(),
            (None, Some(_)) if !self.cpu.in_handler() => "idle".into(),
            (None, Some(_)) => format!("stopped in exception {}", self.cpu.ipsr),
            _ if self.cpu.in_handler() => format!("running in exception {}", self.cpu.ipsr),
            _ => "running".into(),
        }
    }

    fn mark(&mut self) {
        self.cpu.writes.clear();
        self.cpu.reads.as_mut().unwrap().clear();
        self.cpu.read_order.clear();
        self.mark_log = self.cpu.bus.log.len();
    }

    fn phase(&mut self, name: &str, t0: u64) -> Phase {
        let mut p = Phase { name: name.to_string(), end: self.state(), ..Default::default() };
        p.writes = self.cpu.writes.iter().map(|w| (w.addr, w.value)).collect();
        for (addr, r) in self.cpu.reads.as_ref().unwrap() {
            p.reads.insert(*addr, r.count);
        }
        p.events = self.cpu.bus.log[self.mark_log..].iter().map(|(t, e)| (t.saturating_sub(t0), e.clone())).collect();
        self.mark();
        p
    }
}

#[derive(Clone, Default)]
pub struct Phase {
    pub name: String,
    pub end: String,
    pub writes: Vec<(u32, u32)>,
    pub reads: BTreeMap<u32, u32>,
    /// (time since the phase began, event)
    pub events: Vec<(u64, String)>,
}

pub struct Expected {
    pub phase: &'static str,
    pub what: &'static str,
    pub why: &'static str,
}

/// Registers written for the log only (the control block's log register, a UART's)
fn log_register(addr: u32) -> bool {
    addr == CTRL + 0x70 || addr < 0x1000
}

/// Polled while a wait runs: the timers' counts (how often they are read is how long the code around takes)
fn polled(addr: u32) -> bool {
    addr & 0xffff8fff == TIMERS + 0x1c
}

fn compare(a: &Phase, b: &Phase) -> Vec<(String, String, String)> {
    let mut out = Vec::new();
    if a.end != b.end {
        out.push(("end".to_string(), a.end.clone(), b.end.clone()));
    }
    let strip = |w: &[(u32, u32)]| w.iter().filter(|x| !log_register(x.0)).copied().collect::<Vec<_>>();
    let (wa, wb) = (strip(&a.writes), strip(&b.writes));
    if wa != wb {
        let i = wa.iter().zip(&wb).position(|(x, y)| x != y).unwrap_or(wa.len().min(wb.len()));
        let show = |w: &[(u32, u32)]| {
            let v: Vec<String> = w[i.min(w.len())..].iter().take(6).map(|(a, v)| format!("{a:x}={v:x}")).collect();
            format!("{} ({} writes)", v.join(" "), w.len())
        };
        out.push((format!("writes (from write {i})"), show(&wa), show(&wb)));
    }
    let regs: std::collections::BTreeSet<u32> = a.reads.keys().chain(b.reads.keys()).copied().collect();
    for r in regs {
        let (x, y) = (a.reads.get(&r).copied().unwrap_or(0), b.reads.get(&r).copied().unwrap_or(0));
        if x != y && !(polled(r) && x != 0 && y != 0) && !log_register(r) {
            out.push((format!("reads of {r:#010x}"), x.to_string(), y.to_string()));
        }
    }
    // the same events at the same times: to 100 us and half a percent (the two images' code takes different
    // times between the hardware's)
    let show = |e: &[(u64, String)]| e.iter().map(|(t, s)| format!("{s} at {:.3} ms", *t as f64 / MS as f64)).collect::<Vec<_>>().join("; ");
    let content = a.events.len() == b.events.len() && a.events.iter().zip(&b.events).all(|(x, y)| x.1 == y.1);
    let near = |x: u64, y: u64| x.abs_diff(y) <= 100 * US + x.max(y) / 200;
    if !content {
        out.push(("events".to_string(), show(&a.events), show(&b.events)));
    } else if !a.events.iter().zip(&b.events).all(|(x, y)| near(x.0, y.0)) {
        // CIX's later, by no more than its logging takes (below)?
        let late = a.events.iter().zip(&b.events).all(|(x, y)| near(x.0, y.0) || (x.0 > y.0 && x.0 - y.0 <= 120 * MS));
        out.push((if late { "event times: CIX's later" } else { "event times" }.to_string(), show(&a.events), show(&b.events)));
    }
    out
}

const NO_VECTOR: &str = "CIX's vector table ends at interrupt 60: the three entries after it are the start of its code, \
    not handlers. Ours has dispatcher entries up to 63";
const LOG_ON: &str = "after a resume CIX's image logs through a UART pointer that is 0 (it reads its own vector table as \
    the UART's flags, sees \"busy\" and gives up after 10000 polls a character): nothing is written, and each message \
    costs it tens of milliseconds (at the 100 MHz assumed for the core) before it does what it was asked: its wake-up \
    requests come that much late. Ours logs nothing and is on time";

pub const EXPECTED: &[Expected] = &[
    Expected { phase: "interrupt 61", what: "", why: NO_VECTOR },
    Expected { phase: "interrupt 62", what: "", why: NO_VECTOR },
    Expected { phase: "interrupt 63", what: "", why: NO_VECTOR },
    Expected { phase: "", what: "reads of 0x00000018", why: LOG_ON },
    Expected { phase: "", what: "event times: CIX's later", why: LOG_ON },
];

pub struct Pair {
    pub a: Sfh,
    pub b: Sfh,
    pub phases: Vec<(Phase, Phase)>,
    t0: (u64, u64),
    /// what copies of the open image that were thrown away had run
    cov: Vec<(u32, u32)>,
}

impl Pair {
    pub fn each(&mut self, mut f: impl FnMut(&mut Sfh)) {
        f(&mut self.a);
        f(&mut self.b);
    }

    /// Both settle (at most `limit`), then wait for the slower: the phase so far is closed under `name`.
    pub fn phase(&mut self, name: &str, limit: u64) {
        self.each(|s| s.settle(limit));
        // the same time in both since the phase began, on a 1 ms grid
        let e = (self.a.time() - self.t0.0).max(self.b.time() - self.t0.1);
        let e = (e / MS + 1) * MS;
        let (ta, tb) = (self.t0.0 + e, self.t0.1 + e);
        self.a.run_to(ta);
        self.b.run_to(tb);
        let (pa, pb) = (self.a.phase(name, self.t0.0), self.b.phase(name, self.t0.1));
        self.phases.push((pa, pb));
        self.t0 = (self.a.time(), self.b.time());
    }

    /// A message from the peer of a receiving channel.
    pub fn message(&mut self, ch: usize, words: &[u32]) {
        self.each(|s| {
            let cpu = &mut s.cpu;
            cpu.bus.send(&mut cpu.io, &cpu.mem, ch, words);
        });
    }

    /// Let `t` pass in both without closing the phase.
    pub fn pass(&mut self, t: u64) {
        self.each(|s| {
            let end = s.time() + t;
            s.run_to(end);
        });
    }
}

fn scenarios(q: &mut Pair) {
    q.phase("boot", 100 * MS);
    // the two services, with real waits
    for secs in [0u32, 1, 2, 10] {
        q.message(1, &[0x0c, 0xfff2, secs]);
        q.phase(&format!("AP: suspend, wake after {secs} s"), (secs as u64 + 2) * SEC);
        q.message(1, &[0x0c, 0xfff4, 0]);
        q.phase("AP: resume", 5 * SEC);
    }
    for ms in [0u32, 1, 5, 250, 1000, 60000] {
        q.message(3, &[0x0c, 0xfff7, ms]);
        q.phase(&format!("PM: wake after {ms} ms"), (ms as u64 + 2000) * MS);
    }
    q.message(1, &[0x0c, 0xfff4, 0]);
    q.phase("AP: resume after the PM's wake", 5 * SEC);
    // a resume that comes while a wake is pending; two wake requests in a row
    q.message(1, &[0x0c, 0xfff2, 3]);
    q.pass(1 * SEC);
    q.message(1, &[0x0c, 0xfff4, 0]);
    q.phase("AP: suspend for 3 s, resume after 1 s", 8 * SEC);
    q.message(3, &[0x0c, 0xfff7, 500]);
    q.pass(100 * MS);
    q.message(3, &[0x0c, 0xfff7, 200]);
    q.phase("PM: wake after 500 ms, and again after 100 ms: wake after 200 ms", 5 * SEC);
    q.message(1, &[0x0c, 0xfff2, 1]);
    q.pass(200 * MS);
    q.message(3, &[0x0c, 0xfff7, 100]);
    q.phase("AP: suspend for 1 s, PM: wake after 100 ms meanwhile", 6 * SEC);
    q.message(1, &[0x0c, 0xfff4, 0]);
    q.phase("AP: resume", 5 * SEC);
    // every command id and its neighbours, on both receiving channels
    for ch in [1usize, 3] {
        for cmd in [0u32, 1, 0xffef, 0xfff0, 0xfff1, 0xfff3, 0xfff5, 0xfff6, 0xfff8, 0xfff9, 0xffff, 0x1fff2, 0xfffffff2] {
            q.message(ch, &[0x0c, cmd, 1]);
            q.phase(&format!("channel {ch}: command {cmd:#x}"), 5 * SEC);
        }
    }
    // the PM's command on the AP's channel and the AP's on the PM's
    q.message(1, &[0x0c, 0xfff7, 5]);
    q.phase("AP's channel: the PM's command (wake after)", 5 * SEC);
    q.message(3, &[0x0c, 0xfff2, 1]);
    q.phase("PM's channel: the AP's command (suspend)", 5 * SEC);
    q.message(3, &[0x0c, 0xfff4, 0]);
    q.phase("PM's channel: the AP's command (resume)", 5 * SEC);
    // every length: the size in the header against the words in the FIFO
    for size in [0u32, 1, 4, 5, 8, 9, 11, 12, 13, 16, 0x40, 0x7c, 0x7f, 0x80, 0x8c, 0xffffff0c] {
        q.message(1, &[size, 0xfff7, 1]);
        q.phase(&format!("AP: header size {size:#x}, three words"), 5 * SEC);
    }
    for n in [1usize, 2, 4, 5, 6, 31, 32, 33, 40] {
        let mut words = vec![0x0c, 0xfff9, 7];
        words.resize(n, 0x1000);
        q.message(1, &words);
        q.phase(&format!("AP: {n} words in the FIFO"), 5 * SEC);
    }
    // two messages before the interrupt is served
    q.message(3, &[0x0c, 0xfff7, 2, 0x0c, 0xfff7, 3]);
    q.phase("PM: two messages in the FIFO at once", 5 * SEC);
    q.message(1, &[0x0c, 0xfff4, 0]);
    q.message(3, &[0x0c, 0xfff7, 2]);
    q.phase("AP: resume and PM: wake after 2 ms, both pending", 5 * SEC);
    q.message(1, &[0x0c, 0xfff4, 0]);
    q.phase("AP: resume", 5 * SEC);
    // every interrupt: with its source's status as the hardware would set it for the ones the firmware
    // uses (timers, the four channels), and each of the others as a bare interrupt
    for irq in (16..=21).chain(23..=26) {
        q.each(|s| {
            let cpu = &mut s.cpu;
            cpu.bus.raise(&mut cpu.io, &cpu.mem, irq);
        });
        q.phase(&format!("interrupt {irq} with its source's status set"), 5 * SEC);
    }
    for irq in 0..64u32 {
        // (from a copy each: an interrupt without a handler may end an image)
        let base = (q.a.clone(), q.b.clone());
        q.each(|s| {
            s.cpu.irq_enable(irq);
            s.cpu.pend(16 + irq);
        });
        q.phase(&format!("interrupt {irq} (no source: pending once)"), 5 * SEC);
        if q.a.dead.is_some() || q.b.dead.is_some() || !q.a.idle() || !q.b.idle() {
            q.cov.extend(q.b.cpu.covered());
            q.a = base.0;
            q.b = base.1;
            q.t0 = (q.a.time(), q.b.time());
        }
    }
    // two at once, and one while another is served
    q.each(|s| {
        let cpu = &mut s.cpu;
        cpu.bus.raise(&mut cpu.io, &cpu.mem, 19);
        cpu.bus.raise(&mut cpu.io, &cpu.mem, 24);
    });
    q.phase("interrupts 19 and 24 at once", 5 * SEC);
    q.message(3, &[0x0c, 0xfff7, 50]);
    q.pass(10 * MS);
    q.each(|s| {
        let cpu = &mut s.cpu;
        cpu.bus.raise(&mut cpu.io, &cpu.mem, 20);
        cpu.bus.raise(&mut cpu.io, &cpu.mem, 16);
    });
    q.phase("interrupts 20 and 16 while the PM's wake waits", 5 * SEC);
}

/// The system exceptions, each from a copy of the idle images (some end an image).
fn exceptions(q: &mut Pair) {
    let base = (q.a.clone(), q.b.clone());
    for (name, n) in [("NMI", 2u32), ("MemManage", 4), ("BusFault", 5), ("UsageFault", 6), ("SecureFault", 7), ("SVCall", 11), ("PendSV", 14), ("SysTick", 15)] {
        q.each(|s| s.cpu.pend(n));
        q.phase(&format!("exception: {name}"), 200 * MS);
        q.cov.extend(q.b.cpu.covered());
        q.a = base.0.clone();
        q.b = base.1.clone();
        q.t0 = (q.a.time(), q.b.time());
    }
    // a bus fault: the mailbox's registers answer with one while a message waits
    q.each(|s| s.cpu.mem.faults.push((MBOX + 0x10000, MBOX + 0x20000)));
    q.message(1, &[0x0c, 0xfff4, 0]);
    q.phase("exception: bus fault in the mailbox handler", 20 * SEC);
    q.cov.extend(q.b.cpu.covered());
    q.a = base.0.clone();
    q.b = base.1.clone();
    q.t0 = (q.a.time(), q.b.time());
}

pub fn main(open_img: &[u8], open_elf: Option<&str>, cix_img: &[u8], verbose: bool, cov: Option<&str>, only: Option<&str>) -> Result<i32, String> {
    let mut q = Pair { a: Sfh::new(cix_img, None)?, b: Sfh::new(open_img, open_elf)?, phases: Vec::new(), t0: (0, 0), cov: Vec::new() };
    let mut ranges: Vec<(u32, u32)> = Vec::new();
    scenarios(&mut q);
    ranges.extend(q.b.cpu.covered());
    // (the exceptions' copies are thrown away: their coverage is taken first)
    let n = q.phases.len();
    exceptions(&mut q);
    let _ = n;
    ranges.extend(q.b.cpu.covered());
    ranges.extend(q.cov.iter().copied());
    let mut bad = 0;
    let mut used: BTreeMap<&str, usize> = BTreeMap::new();
    for (a, b) in &q.phases {
        if only.map_or(false, |o| !a.name.contains(o)) {
            continue;
        }
        let diffs = compare(a, b);
        let mut lines = Vec::new();
        for (what, x, y) in &diffs {
            match EXPECTED.iter().find(|e| a.name.contains(e.phase) && what.contains(e.what)) {
                Some(e) => *used.entry(e.why).or_default() += 1,
                None => {
                    bad += 1;
                    lines.push(format!("     {what}: CIX {x} | open {y}"));
                }
            }
        }
        if !lines.is_empty() || verbose {
            let ev: Vec<String> = b.events.iter().map(|(t, s)| format!("{s} at {:.3} ms", *t as f64 / MS as f64)).collect();
            println!("{} {}: {}, {} writes{}{}", if lines.is_empty() { "ok  " } else { "DIFF" }, a.name, b.end, b.writes.len(), if ev.is_empty() { "" } else { "; " }, ev.join("; "));
        }
        for l in lines {
            println!("{l}");
        }
    }
    if let (Some(path), Some(elf)) = (cov, open_elf) {
        let name = std::path::Path::new(elf).file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default();
        crate::cov::save(path, &name, &ranges)?;
    }
    let n: usize = used.values().sum();
    println!("{} phases; {bad} differences, {n} expected in {} kinds", q.phases.len(), used.len());
    for (why, n) in &used {
        println!("  {n} x {why}");
    }
    Ok((bad != 0) as i32)
}

/// Start an image as the SE leaves it in the SFH's SRAM and say what it does first: its handshake word
/// and where it then is.  (For the SE's test: what it loaded must come up.)
#[cfg_attr(not(feature = "se"), allow(dead_code))]
pub fn starts(image: &[u8]) -> String {
    let Ok(mut s) = Sfh::new(image, None) else { return "not an image".into() };
    s.settle(200 * MS);
    let hs: Vec<String> = s.cpu.bus.log.iter().filter(|e| e.1.starts_with("handshake")).map(|e| e.1.clone()).collect();
    format!("{}; {}", if hs.is_empty() { "no handshake".to_string() } else { hs.join(", ") }, s.state())
}
