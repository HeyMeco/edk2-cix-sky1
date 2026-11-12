// SPDX-License-Identifier: BSD-2-Clause-Patent
//! What the short suites of test.rs leave out (pm-test --full).
//!
//! The short suites ask eight domains per protocol and the commands a boot
//! needs; the coverage report (sky1emu cov) showed what that never runs.
//! Each phase here starts from a copy of the image as it is 100 ms after
//! the handshake, so one phase cannot disturb the next, and is compared as
//! the power phases are (registers, I2C, SE words, events, answers):
//!
//! * SCMI: PROTOCOL_MESSAGE_ATTRIBUTES of every message of every protocol
//!   on every channel, unknown messages and protocols;
//! * SCMI base: agents, device and protocol permissions, agent reset;
//! * SCMI power domains: every domain's attributes and state; every domain
//!   switched off and on as TF-A does for the cores (PSCI CPU_ON/OFF) and
//!   as the OS does for devices; notifications;
//! * SCMI system power: state get, notify, states that do not exist;
//! * SCMI performance: every domain, every level set and read back, limits,
//!   notifications, the fast channels' descriptions;
//! * SCMI clocks: every clock's attributes, rates and rate; each enabled
//!   and disabled; rates set;
//! * SCMI sensors: every description and reading at two temperatures, trip
//!   points;
//! * malformed SCMI: wrong lengths, domains that do not exist;
//! * every PM message id on every channel, with its neighbours that do not
//!   exist: the SMN writes, performance streams, locked levels;
//! * the I2C slave the EC reads (the SoC's temperature for the O6's fan):
//!   every register read and written as the EC does;
//! * a temperature sweep from 30 to 130 C and back: fans, PVT thresholds,
//!   the DPM's limits, the thermal trip;
//! * the watchdog over 6 s (its count modelled): never left to run out;
//! * suspend asked by PM message 3, a second suspend and resume.

use super::soc::{self, Cpu};
use super::test::{self, ask, ms, pm_msg, run_until, to_val, Ask, Expected, Opts, Periodic, Phase, PhaseStart, Pm, Val, CHANNELS, TFA};

const DSU_PLL: &str = "the DSU's level by message: sky1soc_perf sets the PLL's post-divider (one owner, README); CIX's DVFS programs the PLL, so its clock is the level's and ours the PLL's rate over a whole number";
const UNKNOWN_PROTOCOL: &str = "CIX's firmware dies on a message of a protocol it does not have (its response logger calls through a table of six); ours answers NOT_SUPPORTED";
const CLOCK_V2: &str = "CIX's clock protocol answers CLOCK_NAME_GET and the rate notifications; ours has not got them (no clock's attributes offer either, so an OS does not ask)";
const BAD_STATE: &str = "CIX's firmware asserts on a system power state that does not exist; ours answers INVALID_PARAMETERS";
const ALARM_LINES: &str = "CIX's PVT warning handler computes in floating point (dev_pvt_ht_warning_isr, convert_temperature_to_data): taken in a task that has no coprocessor state (the idle task) FreeRTOS panics, taken in one that has, that task's registers are used. CIX leaves the PVT alarm and PROCHOT lines masked, so it does not happen as shipped; ours takes them";

const LEVEL_GET: &str = "PERFORMANCE_LEVEL_GET of a cluster or the DSU whose level was not set by a message (the OS's fast channel, or the DPM's cap: PROCHOT, a skin temperature controller): ours answers the level that runs (sky1soc_perf's, one owner, README), CIX's the level its DVFS last had by message, which is not the PLL's";
const CAP_FOLLOWED: &str = "a level set by message while the DPM's cap comes down (it does over the first 132 ms after the image idles, with the skin temperature controllers on): ours sets the clusters to each step of the cap as it comes (the voltage with it), CIX's stays at the level the message got until the cap is below sky1soc_perf's own record; both end at the cap";
const GRANT_SAMPLE: &str = "dpm_main_tab.opp_grant's entry for the DSU is set by the DPM's limit pass and set to 0 again by its export pass, every period (with the skin temperature controllers on); the phase's end reads it between the two in one image (it is 0 in both but for some 50 us of each ms; what is exported, opp_run, is compared and the same)";
const DVFS_CATCHES_UP: &str = "under a cap of the DPM's, CIX's DVFS still has the level of the last message: the next message for the domain makes it set the level that already runs and notify it (no PLL or voltage write); ours notifies the changes that happen";

const FAN_TIMING: &str = "the fans' loop runs on its own period (boards whose fans the PM drives): between questions the two images are not at the same point of it; compared in the phases by time (fans: ...)";
const PID_SAMPLE: &str = "the die arbiter's PID feedback term is the value at the instant the phase ends, which is not the same point of the DPM's period in both images";
const BAD_TABLE: &str = "CIX's fan message 0x802 refuses a user table of nine points, but has already set the table's count to 0: with the fan in the user's mode its fans' loop asserts at the next pass (adjust_fan_rpm: the point in use is not below the count). Ours refuses the message and keeps the table";

/// What differs between the images and is not counted (see Expected).
pub const EXPECTED: &[Expected] = &[
    Expected { capped: false, except: "fan", phase: "", what: "message 0x801[", cix: "0 [0xc000 0x801", open: "0 [0xc000 0x801", why: FAN_TIMING },
    Expected { capped: false, except: "fan", phase: "", what: "'0x801'", cix: "0 [0xc000 0x801", open: "0 [0xc000 0x801", why: FAN_TIMING },
    Expected { capped: false, except: "fan", phase: "thermal sweep", what: " C: fans", cix: "", open: "", why: FAN_TIMING },
    Expected { capped: false, except: "", phase: "SCMI sensors", what: "register 0x4411", cix: "", open: "", why: FAN_TIMING },
    Expected { capped: false, except: "", phase: "die at 100 C", what: "spt_pids[3]", cix: "", open: "", why: PID_SAMPLE },
    Expected { capped: false, except: "", phase: "", what: "], then 600 ms", cix: "asserted", open: "2 [", why: BAD_TABLE },
    Expected { capped: false, except: "", phase: "interrupt lines by themselves", what: " 0x13/8[3]", cix: "0 [", open: "0 [", why: LEVEL_GET },
    Expected { capped: true, except: "", phase: "", what: " 0x13/8[2]", cix: "0 [", open: "0 [", why: LEVEL_GET },
    Expected { capped: true, except: "", phase: "", what: " 0x13/8[3]", cix: "0 [", open: "0 [", why: LEVEL_GET },
    Expected { capped: true, except: "", phase: "", what: " 0x13/8[4]", cix: "0 [", open: "0 [", why: LEVEL_GET },
    Expected { capped: true, except: "", phase: "", what: " 0x13/8[5]", cix: "0 [", open: "0 [", why: LEVEL_GET },
    Expected { capped: true, except: "", phase: "", what: " 0x13/8[6]", cix: "0 [", open: "0 [", why: LEVEL_GET },
    Expected { capped: true, except: "", phase: "cap falls", what: "register 0xa0060", cix: "", open: "", why: CAP_FOLLOWED },
    Expected { capped: true, except: "", phase: "cap falls", what: "register 0xa00900", cix: "", open: "", why: CAP_FOLLOWED },
    Expected { capped: true, except: "", phase: "die at 100 C", what: "opp_grant", cix: "", open: "", why: GRANT_SAMPLE },
    Expected { capped: true, except: "", phase: "cap falls", what: "I2C", cix: "", open: "", why: CAP_FOLLOWED },
    Expected { capped: true, except: "", phase: "cap falls", what: "clock, kHz", cix: "", open: "", why: CAP_FOLLOWED },
    Expected { capped: true, except: "", phase: "notifications: levels", what: "register 0xa00900", cix: "", open: "", why: DVFS_CATCHES_UP },
    Expected { capped: true, except: "", phase: "notifications: levels", what: "the notification channel", cix: "", open: "", why: DVFS_CATCHES_UP },
    Expected { capped: true, except: "", phase: "notifications: levels", what: "doorbells", cix: "", open: "", why: DVFS_CATCHES_UP },
    Expected { capped: true, except: "", phase: "SCMI performance", what: "register 0xa00900", cix: "", open: "", why: DVFS_CATCHES_UP },
    Expected { capped: false, except: "", phase: "by its fast channel", what: " 0x13/8[3]", cix: "0 [", open: "0 [", why: LEVEL_GET },
    Expected { capped: false, except: "", phase: "DSU stepped", what: " 0x13/8[3]", cix: "0 [", open: "0 [", why: LEVEL_GET },
    Expected { capped: false, except: "", phase: "by message", what: "register 0xa00605", cix: "", open: "", why: DSU_PLL },
    Expected { capped: false, except: "", phase: "by message", what: "the DSU's clock", cix: "", open: "", why: DSU_PLL },
    Expected { capped: false, except: "", phase: "SCMI performance", what: "register 0xa00605", cix: "", open: "", why: DSU_PLL },
    Expected { capped: false, except: "", phase: "", what: " 0x16/", cix: "asserted", open: "-1 []", why: UNKNOWN_PROTOCOL },
    Expected { capped: false, except: "", phase: "", what: " 0x17/", cix: "asserted", open: "-1 []", why: UNKNOWN_PROTOCOL },
    Expected { capped: false, except: "", phase: "", what: " 0x18/", cix: "asserted", open: "-1 []", why: UNKNOWN_PROTOCOL },
    Expected { capped: false, except: "", phase: "", what: " 0x19/", cix: "asserted", open: "-1 []", why: UNKNOWN_PROTOCOL },
    Expected { capped: false, except: "", phase: "", what: " 0x80/", cix: "asserted", open: "-1 []", why: UNKNOWN_PROTOCOL },
    Expected { capped: false, except: "", phase: "", what: " 0x99/", cix: "asserted", open: "-1 []", why: UNKNOWN_PROTOCOL },
    Expected { capped: false, except: "", phase: "message attributes", what: " 0x14/2[8]", cix: "0 [0x0]", open: "-4 []", why: CLOCK_V2 },
    Expected { capped: false, except: "", phase: "message attributes", what: " 0x14/2[9]", cix: "0 [0x0]", open: "-4 []", why: CLOCK_V2 },
    Expected { capped: false, except: "", phase: "message attributes", what: " 0x14/2[a]", cix: "0 [0x0]", open: "-4 []", why: CLOCK_V2 },
    Expected { capped: false, except: "", phase: "every clock read", what: " 0x14/8[", cix: "0 [", open: "-4 []", why: CLOCK_V2 },
    Expected { capped: false, except: "", phase: "every clock read", what: " 0x14/9[", cix: "0 []", open: "-4 []", why: CLOCK_V2 },
    Expected { capped: false, except: "", phase: "every clock read", what: " 0x14/10[", cix: "0 []", open: "-4 []", why: CLOCK_V2 },
    Expected { capped: false, except: "", phase: "system power", what: "ch2 0x12/3[0, 80000000]", cix: "asserted", open: "-2 []", why: BAD_STATE },
    Expected { capped: false, except: "", phase: "unmasked", what: "end", cix: "_xt_panic", open: "ran", why: ALARM_LINES },
    Expected { capped: false, except: "", phase: "unmasked", what: "register 0xa00005c", cix: "", open: "", why: ALARM_LINES },
    Expected { capped: false, except: "", phase: "unmasked", what: "register 0xa0070008", cix: "", open: "", why: ALARM_LINES },
    Expected { capped: false, except: "", phase: "unmasked", what: " C: ", cix: "", open: "", why: ALARM_LINES },
];

/// The NI-700s' counter snapshot strobes, which the performance streams
/// write every period; the fans' PWM duties and the tachometers'
/// acknowledges, written by the fans' loop (the counters, soc.rs).
pub const PERIODIC: &[Periodic] = &[
    Periodic { lo: 0x4d000000, hi: 0x4d200000, delta: 3 },
    Periodic { lo: 0x44110000, hi: 0x44118000, delta: 2 },
    Periodic { lo: 0xa0040000, hi: 0xa0050000, delta: 2 },
];

const PROTOCOLS: [u32; 6] = [0x10, 0x11, 0x12, 0x13, 0x14, 0x15];
/// simulated time a question takes, rounded up to a multiple of this (most
/// take a fraction of it, so both images' clocks stay together and what
/// they do on a timer, the fans' loop for one, is done at the same times)
const SLOT_MS: f64 = 2.0;
/// instructions between two looks at a channel, in these phases
const POLL: u64 = 2_000;
/// The period of the fans' loop in both images.
const FAN_PERIOD_MS: f64 = 500.0;
/// the grid the images' clocks are brought back to before the model
/// changes or time is let pass
const GRID_MS: f64 = 20.0;

fn label(chan: usize, a: &Ask) -> String {
    match a {
        Ask::Scmi(p, m, args) => format!("ch{chan} {p:#04x}/{m}{args:x?}"),
        Ask::ScmiLen(p, m, args, len) => format!("ch{chan} {p:#04x}/{m}{args:x?} length {len}"),
        Ask::ScmiIntr(p, m, args) => format!("ch{chan} {p:#04x}/{m}{args:x?} by interrupt"),
        Ask::Fc(off, v) => format!("ch{chan} fast channel {off:#x} = {v:#x}"),
        Ask::Read(off) => format!("read {off:#x}"),
        Ask::Settle(n) => format!("settle {n}"),
    }
}

/// The images asked in step: CIX's first (its answers choose the next
/// questions), then the open one.
struct Q<'a> {
    cpus: Vec<&'a mut Cpu>,
    outs: Vec<Vec<(String, Val)>>,
    /// what happened to an image a question killed
    notes: Vec<String>,
    /// each image's clock when the phase began
    t0: Vec<u64>,
}

/// A copy of the machine to go back to (the write log is kept apart: it
/// can be long).
struct Snapshot {
    cpu: Cpu,
    writes: usize,
}

fn snapshot(cpu: &mut Cpu) -> Snapshot {
    let w = std::mem::take(&mut cpu.writes);
    let copy = cpu.clone();
    let n = w.len();
    cpu.writes = w;
    Snapshot { cpu: copy, writes: n }
}

fn restore(cpu: &mut Cpu, s: Snapshot) {
    let mut w = std::mem::take(&mut cpu.writes);
    w.truncate(s.writes);
    let covered = cpu.covered();
    *cpu = s.cpu;
    cpu.writes = w;
    cpu.mark_covered(&covered);
}

/// What became of an image, whichever function it ended in.
fn died(e: &str) -> String {
    if e.contains("stopped") {
        if soc::ASSERTS.iter().any(|a| e.contains(a)) && !e.contains("after a channel error") {
            "the firmware asserted".to_string()
        } else {
            "the firmware crashed".to_string()
        }
    } else if e.contains("no answer") || e.contains("never free") {
        "no answer".to_string()
    } else {
        e.to_string()
    }
}

impl Q<'_> {
    /// Ask every image.  When one does not survive the question (an
    /// assertion, a crash, no answer), all are put back as they were
    /// before it, so that the questions after it find them in the same
    /// state; the answers are kept.  Returns the first image's answer.
    fn guarded(&mut self, name: String, f: impl Fn(&mut Cpu) -> Val) -> Val {
        let snaps: Vec<Snapshot> = self.cpus.iter_mut().map(|c| snapshot(c)).collect();
        let mut vals: Vec<Val> = Vec::new();
        let starts: Vec<u64> = self.cpus.iter().map(|c| c.io.time).collect();
        for c in self.cpus.iter_mut() {
            // each question takes a whole number of SLOT_MS of simulated
            // time, so that the images' clocks stay together and what they
            // do on a timer (streams, polling) is done as often in both
            let t0 = c.io.time;
            vals.push(f(c));
            let slot = (SLOT_MS * soc::TIME_HZ as f64 / 1000.0) as u64;
            let used = c.io.time - t0;
            if used > 10 * slot {
                self.notes.push(format!("{}: {name}: took {:.1} ms", image_name(c), used as f64 * 1000.0 / soc::TIME_HZ as f64));
            }
            let pad = (slot - used % slot) % slot;
            if pad != 0 && !matches!(vals.last(), Some(Val::Error(_))) {
                soc::run_cycles(c, pad);
            }
        }
        // ... and as many slots in every image as in the slowest
        if !vals.iter().any(|v| matches!(v, Val::Error(_))) {
            let most = self.cpus.iter().zip(&starts).map(|(c, t)| c.io.time - t).max().unwrap_or(0);
            for (c, t) in self.cpus.iter_mut().zip(&starts) {
                let pad = most - (c.io.time - t);
                if pad != 0 {
                    soc::run_cycles(c, pad);
                }
            }
        }
        // a refusal (the channel's error bit) must leave the image alive:
        // it is asked the base protocol's version, and put back
        for (c, v) in self.cpus.iter_mut().zip(vals.iter_mut()) {
            if matches!(v, Val::Text(t) if t == "channel error") {
                let probe = snapshot(c);
                if let Err(e) = soc::scmi(c, 0x10, 0, &[], 0, 3_000_000) {
                    *v = Val::Error(format!("stopped after a channel error: {e}"));
                } else {
                    restore(c, probe);
                }
            }
        }
        let mut dead = false;
        for (i, v) in vals.iter_mut().enumerate() {
            if let Val::Error(e) = v {
                dead = true;
                let c = &self.cpus[i];
                self.notes.push(format!("{}: {name}: {e}; in {}; trail {}", image_name(c), c.backtrace_names(6), c.trail()));
                *v = Val::Error(died(e));
            }
        }
        if dead {
            for (c, s) in self.cpus.iter_mut().zip(snaps) {
                restore(c, s);
            }
        }
        for (out, v) in self.outs.iter_mut().zip(vals.iter()) {
            out.push((name.clone(), v.clone()));
        }
        vals.swap_remove(0)
    }

    fn ask(&mut self, chan: usize, a: Ask) -> Val {
        self.guarded(label(chan, &a), |cpu| ask(cpu, chan, &a))
    }

    fn scmi(&mut self, chan: usize, p: u32, m: u32, args: &[u32]) -> Val {
        self.ask(chan, Ask::Scmi(p, m, args.to_vec()))
    }

    fn msg(&mut self, chan: usize, id: u32, args: &[u32]) -> Val {
        self.guarded(format!("ch{chan} message {id:#x}{args:x?}"), |cpu| pm_msg(cpu, chan, id, args))
    }

    /// The count a protocol's attributes give (its low bits).
    fn count(&mut self, chan: usize, p: u32, mask: u32) -> u32 {
        match self.scmi(chan, p, 1, &[]) {
            Val::Scmi(Some(0), w) if !w.is_empty() => w[0] & mask,
            _ => 0,
        }
    }

    /// Every image run on to the same multiple of GRID_MS since the phase
    /// began (the next one of the image that is furthest): after questions,
    /// which do not take the two images the same time to the microsecond,
    /// their clocks read the same again.
    fn align(&mut self) {
        self.align_to(GRID_MS);
    }

    fn align_to(&mut self, ms: f64) {
        let grid = (ms * soc::TIME_HZ as f64 / 1000.0) as u64;
        let most = self.cpus.iter().zip(self.t0.iter()).map(|(c, t0)| (c.io.time - t0).div_ceil(grid) * grid).max().unwrap_or(0);
        for (c, t0) in self.cpus.iter_mut().zip(self.t0.iter()) {
            let pad = most - (c.io.time - t0);
            if pad != 0 {
                soc::run_cycles(c, pad);
            }
        }
    }

    /// Something done to every image's model, at the same time in both.
    fn each(&mut self, f: impl Fn(&mut Cpu)) {
        self.align();
        for c in self.cpus.iter_mut() {
            f(c);
        }
    }

    /// each(), at a multiple of the fans' loop's period since the phase
    /// began: the loop is then at the same point of its period at every
    /// change (the questions between two changes take a few milliseconds,
    /// which would otherwise move each change against the loop).
    fn each_fan_period(&mut self, f: impl Fn(&mut Cpu)) {
        self.align_to(FAN_PERIOD_MS);
        self.each(f);
    }

    /// A value read from every image, among the answers.
    fn read(&mut self, name: &str, f: impl Fn(&mut Cpu) -> Val) {
        for (c, out) in self.cpus.iter_mut().zip(self.outs.iter_mut()) {
            out.push((name.to_string(), f(c)));
        }
    }

    /// Every image run for t ms (from the same time); how that ended, per image.
    fn settle(&mut self, t: f64) -> Vec<String> {
        self.align();
        self.cpus.iter_mut().map(|c| ms(c, t)).collect()
    }

    fn until(&mut self, what: &str, budget: Option<u64>) -> Vec<String> {
        self.cpus.iter_mut().map(|c| run_until(c, what, budget)).collect()
    }
}

fn image_name(cpu: &Cpu) -> String {
    std::path::Path::new(&cpu.elf_path).file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default()
}

type Step = fn(&mut Q) -> Vec<String>;

/// The end of a phase: time for what the questions started to finish.
/// (By time, not until no register is written: an image that idles in
/// WAITI and one whose idle task spins do not run the same time for the
/// same number of instructions, and fans on a timer never leave an image
/// quiet.)
fn quiet(q: &mut Q) -> Vec<String> {
    q.settle(40.0)
}

fn message_attributes(q: &mut Q) -> Vec<String> {
    for chan in CHANNELS {
        for p in PROTOCOLS.iter().copied().chain([0x16, 0x17, 0x18, 0x19, 0x80, 0x99]) {
            q.scmi(chan, p, 0, &[]);
            for m in 0..=0x10 {
                q.scmi(chan, p, 2, &[m]);
            }
            // a message the protocol does not have
            q.scmi(chan, p, 0x30, &[0]);
        }
    }
    quiet(q)
}

fn base_agents(q: &mut Q) -> Vec<String> {
    for chan in CHANNELS {
        for agent in [0, 1, 2, 3, 4, 5, 8, 0xffffffff] {
            q.scmi(chan, 0x10, 7, &[agent]);
        }
        q.scmi(chan, 0x10, 8, &[1]); // notify errors
        q.scmi(chan, 0x10, 8, &[0]);
    }
    // permissions: a device, then a protocol of it, denied to an agent and
    // allowed again, by every channel's agent; what the agent then gets
    for chan in CHANNELS {
        for agent in [1, 2, 3] {
            for (dev, proto) in [(0, 0x13), (1, 0x14), (3, 0x11), (0x40, 0x15)] {
                q.scmi(chan, 0x10, 9, &[agent, dev, 0]);
                q.scmi(chan, 0x10, 0xa, &[agent, dev, proto, 0]);
                q.scmi(0, proto, 0, &[]);
                q.scmi(0, 0x13, 8, &[0]);
                q.scmi(0, 0x14, 6, &[1]);
                q.scmi(chan, 0x10, 0xa, &[agent, dev, proto, 1]);
                q.scmi(chan, 0x10, 9, &[agent, dev, 1]);
            }
            q.scmi(chan, 0x10, 0xb, &[agent, 0]);
            q.scmi(chan, 0x10, 0xb, &[agent, 1]);
        }
    }
    quiet(q)
}

/// Commands answered with an interrupt, as Linux asks for them (TF-A
/// polls): the doorbell the PM rings for each, on every channel.
fn by_interrupt(q: &mut Q) -> Vec<String> {
    for chan in CHANNELS {
        let cmds: [(u32, u32, &[u32]); 9] = [
            (0x10, 0, &[]),
            (0x10, 6, &[0]),
            (0x11, 5, &[0]),
            (0x13, 3, &[3]),
            (0x13, 8, &[3]),
            (0x14, 6, &[1]),
            (0x15, 6, &[0, 0]),
            (0x13, 8, &[0xffff]),
            (0x99, 0, &[]),
        ];
        for (p, m, args) in cmds {
            q.ask(chan, Ask::ScmiIntr(p, m, args.to_vec()));
            q.read("doorbells so far", |c| Val::Text(format!("{:?}", c.bus.doorbells)));
        }
        for len in [0, 3, 200] {
            q.ask(chan, Ask::ScmiLen(0x10, 0, vec![], len));
        }
    }
    quiet(q)
}

fn notification_words(q: &mut Q) {
    q.read("the notification channel's words", |c| Val::U32s(Some((0..12).map(|i| c.bus.mem.read32(soc::MBOX + 0x10000 + 4 * i)).collect())));
    // the OS takes the notification: the channel is free again
    q.each(|c| c.bus.mem.write32(soc::MBOX + 0x10000 + 4, 1));
}

fn levels(q: &mut Q, d: u32) -> Vec<u32> {
    match q.scmi(0, 0x13, 4, &[d, 0]) {
        Val::Scmi(Some(0), w) => test::levels_of(&w),
        _ => Vec::new(),
    }
}

fn notifier_doorbells(q: &mut Q) {
    q.read("the notification channels' doorbells", |c| Val::Text(format!("{:?}", c.bus.doorbells.iter().filter(|d| d.0 & 1 == 1).collect::<Vec<_>>())));
}

/// Levels set by message with the level and limits notifications on: what
/// the PM tells the OS on its own channel (mailbox 1), and its doorbell.
/// (A cluster asked by message and by fast channel in turn is CIX's
/// two-owner case, which the fault test covers: not mixed here.)
fn notify_by_message(q: &mut Q) -> Vec<String> {
    for d in [3u32, 0, 2, 7] {
        q.scmi(0, 0x13, 9, &[d, 1]);
        q.scmi(0, 0x13, 0xa, &[d, 1]);
        let lv = levels(q, d);
        for level in lv.iter().rev().take(3) {
            q.ask(0, Ask::ScmiIntr(0x13, 7, vec![d, *level]));
            q.settle(5.0);
            notification_words(q);
            q.read("the DSU's clock, kHz", |c| Val::U32s(Some(vec![pll_khz(c, soc::CGC + 0x500)])));
        }
        if let (Some(lo), Some(hi)) = (lv.first(), lv.last()) {
            for (max, min) in [(*lo, *lo), (*hi, *lo)] {
                q.scmi(0, 0x13, 5, &[d, max, min]);
                q.settle(5.0);
                notification_words(q);
            }
        }
        q.scmi(0, 0x13, 9, &[d, 0]);
        q.scmi(0, 0x13, 0xa, &[d, 0]);
    }
    notifier_doorbells(q);
    quiet(q)
}

/// The same for a cluster asked by its fast channel alone.
fn notify_by_fast_channel(q: &mut Q) -> Vec<String> {
    q.scmi(0, 0x13, 9, &[3, 1]);
    q.scmi(0, 0x13, 0xa, &[3, 1]);
    let lv = levels(q, 3);
    for level in lv.iter().rev().take(3).chain(lv.iter().take(3)) {
        q.ask(0, Ask::Fc(test::BIG0_FC, *level));
        q.settle(5.0);
        notification_words(q);
        q.scmi(0, 0x13, 8, &[3]);
    }
    notifier_doorbells(q);
    quiet(q)
}

/// The DSU stepped with the clusters (PM message 0x803): what a cluster's
/// change by fast channel then does to the DSU, and what the OS is told.
fn dsu_with_clusters(q: &mut Q) -> Vec<String> {
    for d in [3u32, 7] {
        q.scmi(0, 0x13, 0xa, &[d, 1]);
    }
    let lv = levels(q, 3);
    q.msg(0, 0x803, &[2, 2]);
    for level in lv.iter().rev().take(2).chain(lv.iter().take(2)) {
        q.ask(0, Ask::Fc(test::BIG0_FC, *level));
        for _ in 0..3 {
            q.settle(3.0);
            notification_words(q);
        }
        q.scmi(0, 0x13, 8, &[3]);
        q.scmi(0, 0x13, 8, &[7]);
        q.read("the DSU's clock, kHz", |c| Val::U32s(Some(vec![pll_khz(c, soc::CGC + 0x500)])));
    }
    q.msg(0, 0x803, &[2, 0]);
    notifier_doorbells(q);
    quiet(q)
}

/// A PLL's output from its registers (soc.rs: 24 MHz times the feedback
/// divider and fraction, over the reference and the two post dividers).
pub fn pll_khz(cpu: &Cpu, base: u32) -> u32 {
    let m = &cpu.bus.mem;
    let (fb, frac, div) = (m.read32(base) & 0xfff, m.read32(base + 4) & 0xffffff, m.read32(base + 8));
    let (p1, p2, r) = ((div & 7).max(1), ((div >> 4) & 7).max(1), ((div >> 8) & 0x3f).max(1));
    let vco = 24_000.0 * (fb as f64 + frac as f64 / 16777216.0) / r as f64;
    (vco / p1 as f64 / p2 as f64).round() as u32
}

const PD_OFF: u32 = 0x40000000;

fn power_domains(q: &mut Q) -> Vec<String> {
    let n = q.count(TFA, 0x11, 0xffff);
    for chan in [0, TFA] {
        for d in 0..n + 1 {
            q.scmi(chan, 0x11, 3, &[d]);
            q.scmi(chan, 0x11, 5, &[d]);
            q.scmi(chan, 0x11, 6, &[d, 1]);
        }
    }
    // each domain off and on again: TF-A's way (synchronous), then the
    // OS's, with the state read after each
    for chan in [TFA, 0] {
        for d in 0..n {
            for state in [PD_OFF, 0] {
                q.scmi(chan, 0x11, 4, &[0, d, state]);
                q.ask(chan, Ask::Settle(100_000));
                q.scmi(chan, 0x11, 5, &[d]);
            }
        }
    }
    // asynchronous requests, and a state that does not exist
    for d in [0, 1, n.saturating_sub(1)] {
        q.scmi(TFA, 0x11, 4, &[1, d, PD_OFF]);
        q.ask(TFA, Ask::Settle(200_000));
        q.scmi(TFA, 0x11, 4, &[1, d, 0]);
        q.ask(TFA, Ask::Settle(200_000));
        q.scmi(TFA, 0x11, 4, &[0, d, 0x12345]);
        q.scmi(TFA, 0x11, 6, &[d, 0]);
    }
    quiet(q)
}

fn system_power(q: &mut Q) -> Vec<String> {
    for chan in CHANNELS {
        q.scmi(chan, 0x12, 4, &[]);
        q.scmi(chan, 0x12, 5, &[1]);
        q.scmi(chan, 0x12, 5, &[0]);
        // states that do not exist, and the graceful flag
        q.scmi(chan, 0x12, 3, &[0, 5]);
        q.scmi(chan, 0x12, 3, &[0, 0x80000000]);
        q.scmi(chan, 0x12, 3, &[2, 4]);
    }
    quiet(q)
}

fn performance(q: &mut Q) -> Vec<String> {
    let n = q.count(0, 0x13, 0xffff);
    let mut levels = Vec::new();
    for d in 0..n + 1 {
        q.scmi(0, 0x13, 3, &[d]);
        let lv = match q.scmi(0, 0x13, 4, &[d, 0]) {
            Val::Scmi(Some(0), w) => test::levels_of(&w),
            _ => Vec::new(),
        };
        q.scmi(0, 0x13, 4, &[d, 2]);
        q.scmi(0, 0x13, 4, &[d, 100]);
        q.scmi(0, 0x13, 6, &[d]);
        q.scmi(0, 0x13, 8, &[d]);
        for m in 3..=0xb {
            q.scmi(0, 0x13, 0xb, &[d, m]);
        }
        q.scmi(0, 0x13, 9, &[d, 1]);
        q.scmi(0, 0x13, 0xa, &[d, 1]);
        levels.push(lv);
    }
    // every level of every domain, upwards then downwards, and levels
    // between and beyond them
    for (d, lv) in levels.iter().enumerate() {
        let d = d as u32;
        let mut order: Vec<u32> = lv.clone();
        order.extend(lv.iter().rev());
        if let (Some(lo), Some(hi)) = (lv.first(), lv.last()) {
            order.extend([lo.saturating_sub(1), hi + 1, (lo + hi) / 2, 0, 0xffffffff, *lo]);
        }
        for level in order {
            q.scmi(0, 0x13, 7, &[d, level]);
            q.ask(0, Ask::Settle(200_000));
            q.scmi(0, 0x13, 8, &[d]);
        }
        if let (Some(lo), Some(hi)) = (lv.first(), lv.last()) {
            for (max, min) in [(*hi, *lo), (*lo, *lo), (*hi, *hi), (*lo, *hi), (hi + 1, *lo), (*hi, 0)] {
                q.scmi(0, 0x13, 5, &[d, max, min]);
                q.ask(0, Ask::Settle(200_000));
                q.scmi(0, 0x13, 6, &[d]);
                q.scmi(0, 0x13, 8, &[d]);
            }
        }
        q.scmi(0, 0x13, 9, &[d, 0]);
        q.scmi(0, 0x13, 0xa, &[d, 0]);
    }
    // the other channels' agents: what they may ask of a cluster
    for chan in [TFA, 4, 6] {
        for d in [0, 3, 7] {
            q.scmi(chan, 0x13, 3, &[d]);
            q.scmi(chan, 0x13, 8, &[d]);
            if let Some(lo) = levels.get(d as usize).and_then(|l| l.first()) {
                q.scmi(chan, 0x13, 7, &[d, *lo]);
            }
        }
    }
    quiet(q)
}

/// The rates CLOCK_DESCRIBE_RATES gave: (lo, hi) pairs.
fn rates_of(v: &Val) -> Vec<(u32, u32)> {
    match v {
        Val::Scmi(Some(0), w) if !w.is_empty() => {
            let n = (w[0] & 0xfff) as usize;
            (0..n.min((w.len() - 1) / 2)).map(|k| (w[1 + 2 * k], w[2 + 2 * k])).collect()
        }
        _ => Vec::new(),
    }
}

fn clocks_read(q: &mut Q) -> Vec<String> {
    let n = q.count(0, 0x14, 0xffff);
    for c in 0..n + 1 {
        q.scmi(0, 0x14, 3, &[c]);
        q.scmi(0, 0x14, 4, &[c, 0]);
        q.scmi(0, 0x14, 4, &[c, 3]);
        q.scmi(0, 0x14, 6, &[c]);
    }
    for chan in [TFA, 4, 6] {
        for c in [0, 1, n / 2, n.saturating_sub(1)] {
            q.scmi(chan, 0x14, 3, &[c]);
            q.scmi(chan, 0x14, 6, &[c]);
        }
    }
    // the clock protocol's version 2 messages: the long name, the rate
    // notifications
    for c in (0..n + 1).step_by(7) {
        q.scmi(0, 0x14, 8, &[c]);
        q.scmi(0, 0x14, 9, &[c, 1]);
        q.scmi(0, 0x14, 0xa, &[c, 1]);
        q.scmi(0, 0x14, 9, &[c, 0]);
        q.scmi(0, 0x14, 0xa, &[c, 0]);
    }
    quiet(q)
}

fn clocks_enable(q: &mut Q) -> Vec<String> {
    let n = q.count(0, 0x14, 0xffff);
    for c in 0..n {
        q.scmi(0, 0x14, 7, &[c, 1]);
        q.scmi(0, 0x14, 6, &[c]);
    }
    for c in 0..n {
        q.scmi(0, 0x14, 7, &[c, 0]);
        q.scmi(0, 0x14, 6, &[c]);
    }
    // disabled once more than enabled, and enabled again
    for c in [0, 1, n / 2, n.saturating_sub(1)] {
        q.scmi(0, 0x14, 7, &[c, 0]);
        q.scmi(0, 0x14, 7, &[c, 1]);
        q.scmi(0, 0x14, 7, &[c, 3]);
    }
    quiet(q)
}

fn clocks_rates(q: &mut Q) -> Vec<String> {
    let n = q.count(0, 0x14, 0xffff);
    for c in 0..n {
        let r = q.scmi(0, 0x14, 4, &[c, 0]);
        let rates = rates_of(&r);
        let is_range = matches!(&r, Val::Scmi(Some(0), w) if !w.is_empty() && w[0] & 0x1000 != 0);
        let mut set: Vec<(u32, u32)> = Vec::new();
        if let (Some(first), Some(last)) = (rates.first(), rates.last()) {
            set.push(*last);
            set.push(*first);
            if is_range {
                // a rate inside the range, off its step
                set.push((first.0 / 2 + last.0 / 2 + 12345, 0));
            } else if rates.len() > 2 {
                set.push(rates[rates.len() / 2]);
            }
            set.push((last.0.wrapping_add(1000), last.1));
            set.push((1, 0));
        } else {
            set.push((24_000_000, 0));
        }
        q.scmi(0, 0x14, 7, &[c, 1]);
        for (lo, hi) in set {
            for flags in [0, 4, 8] {
                // rounded down, up, to the nearest
                q.scmi(0, 0x14, 5, &[flags, c, lo, hi]);
                q.scmi(0, 0x14, 6, &[c]);
            }
        }
    }
    quiet(q)
}

fn sensors(q: &mut Q) -> Vec<String> {
    let n = q.count(0, 0x15, 0xffff);
    for i in (0..n + 4).step_by(1) {
        q.scmi(0, 0x15, 3, &[i]);
    }
    for t in [35.0, 85.0] {
        q.each(|c| test::temps(c, t));
        q.settle(250.0);
        for s in 0..n + 1 {
            for chan in [0, TFA] {
                q.scmi(chan, 0x15, 6, &[s, 0]);
            }
            q.scmi(0, 0x15, 6, &[s, 1]); // asynchronous
        }
    }
    // trip points: notify, then two thresholds the temperature crosses
    for s in 0..n {
        q.scmi(0, 0x15, 4, &[s, 1]);
        // (flags: the event in bits 1:0, the trip point's number from bit 4)
        q.scmi(0, 0x15, 5, &[s, 0x03, 60000, 0]);
        q.scmi(0, 0x15, 5, &[s, 0x13, 90000, 0]);
        q.scmi(0, 0x15, 5, &[s, 0x23, 100000, 0]);
        q.scmi(0, 0x15, 5, &[s, 0x103, 90000, 0]);
        q.scmi(0, 0x15, 5, &[s, 0x1003, 0, 0]);
        q.scmi(0, 0x15, 5, &[s, 0x01, 0xffffffff, 0x7fffffff]);
    }
    for t in [95.0, 50.0, 70.0] {
        q.each(|c| test::temps(c, t));
        q.settle(250.0);
        for s in 0..n {
            q.scmi(0, 0x15, 6, &[s, 0]);
        }
    }
    for s in 0..n {
        q.scmi(0, 0x15, 4, &[s, 0]);
    }
    // the sensors' codes as the hardware could give them, not only those of
    // a temperature: every sensor's reading for codes over the whole range
    for code in [0u32, 1, 100, 222, 223, 224, 400, 600, 800, 1022, 1023] {
        q.each(|c| pvt_codes(c, code));
        q.settle(250.0);
        for s in 0..n {
            q.scmi(0, 0x15, 6, &[s, 0]);
        }
    }
    quiet(q)
}

fn malformed(q: &mut Q) -> Vec<String> {
    // lengths: the header alone, a word short, a word long, far too long
    let cmds: [(u32, u32, &[u32]); 12] = [
        (0x10, 6, &[0]),
        (0x10, 7, &[0]),
        (0x11, 3, &[0]),
        (0x11, 4, &[0, 0, 0]),
        (0x12, 3, &[0, 5]),
        (0x13, 4, &[0, 0]),
        (0x13, 7, &[3, 1]),
        (0x13, 5, &[3, 1, 1]),
        (0x14, 5, &[0, 0, 1, 0]),
        (0x14, 7, &[0, 1]),
        (0x15, 6, &[0, 0]),
        (0x15, 5, &[0, 0, 0, 0]),
    ];
    for chan in [0, TFA] {
        for (p, m, args) in cmds {
            let right = 4 + 4 * args.len() as u32;
            for len in [4, right - 4, right + 4, 124, 128, 0, 3, 0xffffffff] {
                q.ask(chan, Ask::ScmiLen(p, m, args.to_vec(), len));
            }
            q.scmi(chan, p, m, args);
        }
        // domains that do not exist
        for d in [0x100, 0xffff, 0x10000, 0xffffffff] {
            q.scmi(chan, 0x11, 3, &[d]);
            q.scmi(chan, 0x11, 4, &[0, d, 0]);
            q.scmi(chan, 0x11, 5, &[d]);
            q.scmi(chan, 0x13, 3, &[d]);
            q.scmi(chan, 0x13, 4, &[d, 0]);
            q.scmi(chan, 0x13, 5, &[d, 1, 1]);
            q.scmi(chan, 0x13, 6, &[d]);
            q.scmi(chan, 0x13, 7, &[d, 1]);
            q.scmi(chan, 0x13, 8, &[d]);
            q.scmi(chan, 0x13, 0xb, &[d, 7]);
            q.scmi(chan, 0x14, 3, &[d]);
            q.scmi(chan, 0x14, 4, &[d, 0]);
            q.scmi(chan, 0x14, 5, &[0, d, 1, 0]);
            q.scmi(chan, 0x14, 6, &[d]);
            q.scmi(chan, 0x14, 7, &[d, 1]);
            q.scmi(chan, 0x15, 3, &[d]);
            q.scmi(chan, 0x15, 4, &[d, 1]);
            q.scmi(chan, 0x15, 5, &[d, 0, 0, 0]);
            q.scmi(chan, 0x15, 6, &[d, 0]);
        }
    }
    quiet(q)
}

fn pm_messages(q: &mut Q) -> Vec<String> {
    // every id of CIX's table and the ids around them, on every channel
    let ids: Vec<u32> = (0..=7).chain(0x3f..=0x44).chain(0x4f..=0x54).chain(0x5f..=0x65).chain(0x3ff..=0x402).chain(0x7ff..=0x807).chain([0xffff, 0x8000]).collect();
    for chan in CHANNELS {
        for id in &ids {
            if *id == 3 {
                continue; // suspend: its own phase
            }
            let args: &[u32] = match id {
                0x04 => &[2],
                0x05 => &[0xff, 0, 0, 0],
                0x40 | 0x41 => &[1],
                0x42 => &[0],
                0x50 => &[0x05040100],
                0x51 => &[0x16000810, 0x12345678],
                0x52 => &[0x16000810, 0x00ff0000, 0x00ff00ff],
                0x61 => &[0x1000],
                0x62 => &[0, 0, 0, 0],
                0x801 => &[0],
                0x802 => &[1, 0, 0, 0],
                0x803 => &[0, 0],
                _ => &[],
            };
            q.msg(chan, *id, args);
        }
    }
    quiet(q)
}

fn pm_message_arguments(q: &mut Q) -> Vec<String> {
    // SMN access (CIX's debug path for the AP): read back what was written
    for (addr, v) in [(0x16000810u32, 0xa5a5a5a5u32), (0x16000814, 0), (0x1fffffff, 1), (0x20000000, 1), (0xffffffff, 1), (0, 0)] {
        q.msg(0, 0x51, &[addr, v]);
        q.msg(0, 0x50, &[addr]);
        q.msg(0, 0x52, &[addr, 0xffff0000, 0x00ffff00]);
        q.msg(0, 0x50, &[addr]);
    }
    // performance streams: every type and action
    for ty in 0..8 {
        for ports in [1, 3, 0xff] {
            q.msg(0, 0x04, &[0, ty, ports, 1, 10, 0]);
        }
    }
    q.settle(50.0);
    for id in 0..6 {
        q.msg(0, 0x04, &[1, id]);
    }
    q.msg(0, 0x04, &[3, 1]);
    q.settle(20.0);
    q.msg(0, 0x04, &[4]);
    q.msg(0, 0x04, &[2]);
    q.msg(0, 0x04, &[5]);
    q.msg(0, 0x04, &[0xffffffff]);
    // a domain's level and limits locked, and what SCMI then says
    for d in [0u32, 2, 3, 4, 7, 11, 12, 13, 0xff] {
        for (level, min, max) in [(1u32, 0u32, 0u32), (0, 1, 2), (3, 3, 3), (0, 0, 0), (0xffffffff, 0xffffffff, 0xffffffff)] {
            q.msg(0, 0x05, &[d, level, min, max]);
            q.settle(2.0);
            if d < 12 {
                q.scmi(0, 0x13, 8, &[d]);
                q.scmi(0, 0x13, 6, &[d]);
            }
        }
    }
    // cores on and off line, the boot core
    for core in [0u32, 1, 4, 11, 12, 0xff] {
        q.msg(0, 0x41, &[core]);
        q.msg(0, 0x40, &[core]);
        q.msg(TFA, 0x42, &[core]);
    }
    // the SCMI clock reference of every kind of clock
    for clk in [0u32, 1, 5, 50, 100, 200, 272, 273, 0xffff] {
        q.msg(0, 0x61, &[clk]);
        q.scmi(0, 0x14, 6, &[clk]);
    }
    // fans: every mode with arguments in and out of range
    for fan in 0..4u32 {
        for mode in 0..8u32 {
            q.msg(0, 0x802, &[mode, fan, 3000, 50]);
        }
        q.msg(0, 0x802, &[3, fan, 0, 3, 0x00000a05, 0x0bb82823, 0x13883c37]);
        // a table that is refused, with the fan in the user's mode: the
        // fans' loop (500 ms) has to get past it
        q.guarded(format!("ch0 message 0x802[3, {fan}, 1, 9, 0, 0, 0], then 600 ms"), |cpu| {
            let v = pm_msg(cpu, 0, 0x802, &[3, fan, 1, 9, 0, 0, 0]);
            match (&v, soc::run_for(cpu, 600.0)) {
                (Val::Error(_), _) | (_, None) => v,
                (_, Some(why)) => Val::Error(format!("stopped: {why}")),
            }
        });
        q.msg(0, 0x801, &[fan]);
    }
    for (a, b) in [(1u32, 1u32), (2, 2), (3, 0), (3, 3), (0xffffffff, 0xffffffff), (0, 0)] {
        q.msg(0, 0x803, &[a, b]);
        q.settle(20.0);
    }
    quiet(q)
}

/// One transfer of the EC on the PM's I2C slave: the bytes written, then
/// (when n is given) a read; what the firmware gave.
fn ec_transfer(cpu: &mut Cpu, bus: usize, write: &[u8], read: bool) -> Val {
    cpu.bus.slave_write(&mut cpu.io, bus, write);
    cpu.run(40_000);
    if !read {
        return Val::Written;
    }
    cpu.bus.slave_read_start(&mut cpu.io, bus);
    cpu.run(20_000);
    let got = cpu.bus.slave_read_end(&mut cpu.io, bus);
    cpu.run(20_000);
    Val::Bytes(Some(got))
}

/// The EC's side of the PM's I2C slave (Orion O6: the EC reads the SoC's
/// temperature from register 32 for its fan, and writes the board's NTC
/// temperatures to registers 16 to 21).
fn i2c_slave(q: &mut Q) -> Vec<String> {
    let bus = 3;
    q.read("I2C 3: slave address", |c| Val::Text(c.bus.slave_addr(bus).map_or("a master on this board".to_string(), |a| format!("{a:#04x}"))));
    if q.cpus[0].bus.slave_addr(bus).is_none() {
        return quiet(q);
    }
    for t in [40.0, 72.5] {
        q.each(|c| test::temps(c, t));
        q.settle(250.0);
        for reg in 0..64u8 {
            // the EC: w[00 reg], then a read of two bytes
            q.read(&format!("{t} C: register {reg}"), |c| ec_transfer(c, bus, &[0, reg], true));
        }
    }
    // the EC's writes: NTC temperatures
    for (reg, v) in [(16u8, 30u8), (17, 31), (18, 32), (19, 33), (20, 34), (21, 85), (22, 1), (0, 0xff)] {
        q.read(&format!("the EC writes {v} to register {reg}"), |c| ec_transfer(c, bus, &[0, reg, v, 0], false));
        q.read(&format!("register {reg} then"), |c| ec_transfer(c, bus, &[0, reg], true));
    }
    q.settle(300.0);
    for id in [0x801u32, 0x804] {
        q.msg(0, id, &[0]);
    }
    // a transfer of the EC's that goes wrong: arbitration lost, a time-out
    // with the bus busy 30 ms more, FIFO overflows; the slave has to be
    // there again afterwards
    for (isr, busy_ms) in [(0x200u32, 0u64), (0x08, 30), (0xe0, 0)] {
        q.each(|c| c.bus.slave_error(&mut c.io, bus, isr, busy_ms));
        q.settle(100.0);
        q.read(&format!("slave error {isr:#x}: the controller's interrupt source on"), |c| {
            Val::U32s(Some(vec![c.bus.mem.read32(soc::AGG + 0x80 + 4 * 12) >> bus & 1]))
        });
        q.read(&format!("slave error {isr:#x}: register 32 then"), |c| ec_transfer(c, bus, &[0, 32], true));
    }
    quiet(q)
}

fn sweep(q: &mut Q, lines: u32) -> Vec<String> {
    q.each(|c| {
        if lines >= 2 {
            test::unmask(c, 28, 0x1fff);
        }
        if lines >= 1 {
            test::unmask(c, 29, 0x1fff);
            test::unmask(c, 30, 1);
        }
    });
    let mut end = vec!["ran".to_string()];
    let up = (30..=130).step_by(10);
    let down = (30..=120).rev().step_by(15);
    for t in up.chain(down) {
        q.each(|c| {
            test::temps(c, t as f64);
            test::set_raw(c, 30, (t >= 105) as u32);
        });
        end = q.settle(300.0);
        q.read(&format!("{t} C: fans"), |c| Val::U32s(Some((0..4).map(|k| c.bus.fan_speed(k) as u32).collect())));
        for name in ["die_temp", "opp_run", "edp_limit"] {
            q.read(&format!("{t} C: {name}"), |c| {
                test::dpm_outputs(c).into_iter().find(|(n, _)| n.ends_with(name)).map_or(Val::Nothing, |x| x.1)
            });
        }
        q.read(&format!("{t} C: events"), |c| Val::Text(format!("{:?}", c.bus.events)));
        if end.iter().any(|e| e != "ran") {
            break;
        }
        q.msg(0, 0x801, &[0]);
        q.msg(0, 0x400, &[]);
        q.scmi(0, 0x15, 6, &[0, 0]);
        q.scmi(0, 0x13, 8, &[3]);
    }
    end
}

/// The die from 30 to 130 C and back in steps, the interrupt lines as the
/// firmware leaves them (CIX's masks the PVT alarms and PROCHOT: it polls):
/// the fans, the DPM's limits, and what the PM does past its trip point.
fn thermal_sweep(q: &mut Q) -> Vec<String> {
    sweep(q, 0)
}

/// The same with the PVT warning line (29) and PROCHOT (30) unmasked, as
/// if a board had them on.
fn thermal_sweep_lines(q: &mut Q) -> Vec<String> {
    sweep(q, 1)
}

/// And with the PVT "safe" line (28) too.
fn thermal_sweep_safe_line(q: &mut Q) -> Vec<String> {
    sweep(q, 2)
}

/// The number of fans the PM says it drives (PM message 0x800 answers it
/// as its status).
fn fan_count(q: &mut Q) -> u32 {
    match q.msg(0, 0x800, &[]) {
        Val::Msg(n, _) => n.min(3),
        _ => 0,
    }
}

fn fan_state(q: &mut Q, what: &str, fan: u32) {
    q.msg(0, 0x801, &[fan]);
    q.read(&format!("{what}: fan speeds"), |c| Val::U32s(Some((0..4).map(|k| c.bus.fan_speed(k) as u32).collect())));
}

/// Fans by time: each of Linux's modes on each fan the PM drives, held for
/// a second and a half (the loop's period is 500 ms), with the fan's
/// status and speed after it.  Everything between the messages is
/// simulated time, so both images are at the same point of the loop.
fn fan_modes_by_time(q: &mut Q) -> Vec<String> {
    let n = fan_count(q);
    q.each_fan_period(|c| test::temps(c, 55.0));
    let mut end = q.settle(1500.0);
    // (and the fan after the last, which does not exist)
    for fan in 0..n + 1 {
        for mode in 0..8u32 {
            q.align_to(FAN_PERIOD_MS);
            q.msg(0, 0x802, &[mode, fan, 2000, 40]);
            q.settle(1500.0);
            fan_state(q, &format!("fan {fan} mode {mode}"), fan);
        }
        // a table of the user's (mode 3), then back to automatic
        q.align_to(FAN_PERIOD_MS);
        q.msg(0, 0x802, &[3, fan, 1, 3, 0x00000a05, 0x0bb82823, 0x13883c37]);
        end = q.settle(1500.0);
        fan_state(q, &format!("fan {fan} user table"), fan);
        q.msg(0, 0x802, &[1, fan, 0, 0]);
    }
    end
}

/// Fans by time: the automatic loop from 30 to 100 C and back, three
/// seconds at each temperature, then at the sensors' raw codes.
fn fan_curve_by_time(q: &mut Q) -> Vec<String> {
    let n = fan_count(q);
    let mut end = vec!["ran".to_string()];
    for t in [30.0, 45.0, 55.0, 65.0, 75.0, 85.0, 100.0, 80.0, 60.0, 40.0] {
        q.each_fan_period(|c| test::temps(c, t));
        end = q.settle(3000.0);
        for fan in 0..n.max(1) {
            fan_state(q, &format!("{t} C"), fan);
        }
    }
    // ... and the sensors' codes as the hardware could give them (what the
    // sensors phase asks about between questions), a second and a half each
    for code in [0u32, 1, 100, 222, 223, 224, 400, 600, 800, 1022, 1023] {
        q.each_fan_period(|c| pvt_codes(c, code));
        end = q.settle(1500.0);
        for fan in 0..n.max(1) {
            fan_state(q, &format!("code {code}"), fan);
        }
    }
    q.each_fan_period(|c| test::temps(c, 40.0));
    q.settle(1500.0);
    end
}

/// Every PVT sensor's three codes set as the hardware could give them.
fn pvt_codes(c: &mut Cpu, code: u32) {
    c.bus.pvt_temp = [None; soc::PVT_N];
    for i in 0..soc::PVT_N as u32 {
        c.bus.mem.write32(soc::PVT_BASE + 0x1000 * (i + 1) + 4, code | code << 10 | code << 20);
    }
}

/// The watchdog (counting only when the BIOS's configuration gives it a
/// time-out: --set wdt=MICROSECONDS; armed, then, by a suspend and resume,
/// which sets it up again).  Six seconds with the count running and its
/// interrupt's source in the aggregator as the firmware leaves it (it
/// never enables it: the reset value decides), then with the source on:
/// the PM has to answer each first time-out.
fn watchdog(q: &mut Q) -> Vec<String> {
    let state = |q: &mut Q, what: &str| {
        q.read(&format!("{what}: watchdog load, control"), |c| {
            Val::U32s(Some([0x000, 0x008].iter().map(|o| c.bus.mem.read32(soc::WDT + o)).collect()))
        });
        q.read(&format!("{what}: time-outs not answered"), |c| Val::U32s(Some(vec![c.bus.wdt.expired])));
        q.read(&format!("{what}: time-outs the PM counted"), |c| Val::U32s(Some(vec![c.bus.mem.read32(WDT_FIRED)])));
        q.read(&format!("{what}: events"), |c| Val::Text(format!("{:?}", c.bus.events)));
    };
    // (the count starts with the PM's next write to the watchdog)
    q.each(|c| c.bus.wdt.count = true);
    q.each(|c| test::unmask(c, 21, 1));
    q.each(|c| c.bus.wdt_restart(&c.io));
    let mut end = q.settle(3500.0);
    state(q, "source on");
    q.each(|c| test::mask(c, 21, 1));
    if end.iter().all(|e| e == "ran") {
        end = q.settle(2500.0);
        state(q, "source off");
    }
    end
}

/// CIX's scratch word for the AP: the watchdog's first time-outs.
const WDT_FIRED: u32 = 0x200bef38;

/// The ACPI PCC channel (CPPC): the OS writes a command into the shared
/// block and writes fast channel 1 of mailbox 0.  The block read back
/// after "update", each core's desired level set to its lowest and to its
/// highest and "set desired", then commands that do not exist.
fn pcc(q: &mut Q) -> Vec<String> {
    fn block(cpu: &mut Cpu) -> Option<u32> {
        let sym = cpu.byname.get("_s_pcc_ptr").copied()?;
        let p = cpu.read(sym, 4) as u32;
        (p != 0).then_some(p)
    }
    fn command(q: &mut Q, cmd: u32, desired: Option<u32>) {
        let name = match desired {
            Some(off) => format!("PCC command {cmd:#x}, desired levels from +{off:#x}"),
            None => format!("PCC command {cmd:#x}"),
        };
        q.guarded(name, |cpu| {
            let Some(p) = block(cpu) else { return Val::Nothing };
            if let Some(off) = desired {
                for core in 0..12 {
                    let r = p + 8 + 64 * core;
                    let v = cpu.bus.mem.read32(r + off);
                    cpu.bus.mem.write32(r + 16, v);
                }
            }
            cpu.bus.mem.write32(p + 4, cmd);
            ask(cpu, 0, &Ask::Fc(0x90, 1));
            if let Some(why) = soc::run_for(cpu, 4.0) {
                return Val::Error(format!("stopped: {why}"));
            }
            // the command and status word, then each core's levels (not
            // its counters, which count time)
            let mut w = vec![cpu.bus.mem.read32(p), cpu.bus.mem.read32(p + 4)];
            for core in 0..12 {
                w.extend((4..16).map(|k| cpu.bus.mem.read32(p + 8 + 64 * core + 4 * k)));
            }
            Val::U32s(Some(w))
        });
    }
    command(q, 0, None);
    // lowestPerfLevel is at +32 of a core's registers, highestPerfLevel at +20
    for off in [32, 20, 32] {
        command(q, 1, Some(off));
        q.settle(100.0);
        command(q, 0, None);
        for d in 0..4 {
            q.scmi(0, 0x13, 8, &[d]);
        }
    }
    for cmd in [2, 5, 0xff, 0x100, 0x10001, 0xffffffff] {
        command(q, cmd, None);
    }
    // ... and the fast channel written with nothing asked
    q.guarded("PCC fast channel alone".to_string(), |cpu| ask(cpu, 0, &Ask::Fc(0x90, 0)));
    quiet(q)
}

/// Interrupt lines CIX's firmware has handlers for but that nothing above
/// raises, each by itself (the PVT warning line, which CIX's image does not
/// survive, is in the sweeps): the PVT "safe" line (28) for three of the
/// sensors, PROCHOT (30) for 3 ms and held for 3 s (its timer's steps),
/// the local GPIO's source of line 21.
fn lines_alone(q: &mut Q) -> Vec<String> {
    let state = |q: &mut Q, what: &str| {
        q.read(&format!("{what}: enables of lines 21, 28, 29, 30"), |c| {
            Val::U32s(Some([21u32, 28, 29, 30].iter().map(|l| c.bus.mem.read32(soc::AGG + 0x80 + 4 * (l - 12))).collect()))
        });
        for name in ["opp_run", "edp_limit"] {
            q.read(&format!("{what}: {name}"), |c| {
                test::dpm_outputs(c).into_iter().find(|(n, _)| n.ends_with(name)).map_or(Val::Nothing, |x| x.1)
            });
        }
        q.scmi(0, 0x13, 8, &[3]);
    };
    let mut end = vec!["ran".to_string()];
    q.each(|c| test::unmask(c, 28, 0x1fff));
    for sensor in [0u32, 3, 12] {
        q.each(|c| test::set_raw(c, 28, 1 << sensor));
        q.settle(20.0);
        q.each(|c| test::set_raw(c, 28, 0));
        end = q.settle(20.0);
        state(q, &format!("PVT safe, sensor {sensor}"));
    }
    // (what the safe handler enabled of the warning line goes again: the
    // warning line is not this phase's)
    q.each(|c| {
        test::mask(c, 28, 0x1fff);
        test::mask(c, 29, 0x1fff);
    });
    for (held, after) in [(3.0, 60.0), (3000.0, 500.0), (40.0, 3000.0)] {
        if end.iter().any(|e| e != "ran") {
            break;
        }
        q.each(|c| {
            test::unmask(c, 30, 1);
            test::set_raw(c, 30, 1);
        });
        q.settle(held);
        state(q, &format!("PROCHOT for {held} ms"));
        q.each(|c| test::set_raw(c, 30, 0));
        end = q.settle(after);
        state(q, &format!("{after} ms after PROCHOT"));
    }
    q.each(|c| test::mask(c, 30, 1));
    q.each(|c| {
        test::unmask(c, 21, 4);
        test::set_raw(c, 21, 4);
    });
    if end.iter().all(|e| e == "ran") {
        end = q.settle(20.0);
    }
    state(q, "GPIO");
    q.each(|c| {
        test::set_raw(c, 21, 0);
        test::mask(c, 21, 4);
    });
    end
}

/// The SoC idle switch (PM message 0x803 bit 0).  With the DDR controllers
/// reporting both frequency copies (--ddr-dfs) and the BIOS's thresholds
/// given (--set noc_idle...), the SoC counts as idle (the model's cores
/// count nothing) once it has been busy three seconds, and the PM takes
/// the DDR to its low frequency and its own clock to 200 MHz; without
/// them the switch changes nothing.  On: idle at once (the three seconds
/// are long over); off; on again: idle after three seconds; then a pixel
/// clock is set, which has the display checked again; off.
fn idle_switch(q: &mut Q) -> Vec<String> {
    let state = |q: &mut Q, what: &str| {
        q.read(&format!("{what}: DDR frequency switches"), |c| Val::Text(format!("{:?}", c.bus.dfs_log)));
        q.read(&format!("{what}: time units per cycle of the core"), |c| Val::U32s(Some(vec![c.io.tick])));
        q.scmi(0, 0x13, 8, &[3]);
    };
    q.msg(0, 0x803, &[1, 1]);
    let mut end = q.settle(600.0);
    state(q, "on for 600 ms");
    q.msg(0, 0x803, &[1, 0]);
    q.settle(100.0);
    state(q, "off");
    q.msg(0, 0x803, &[1, 1]);
    q.settle(100.0);
    q.msg(0, 0x803, &[1, 0]);
    q.msg(0, 0x803, &[1, 1]);
    if end.iter().all(|e| e == "ran") {
        end = q.settle(2600.0);
        state(q, "on again for 2.6 s");
        q.settle(800.0);
        state(q, "on again for 3.4 s");
    }
    // a display comes up: a pixel clock set while the switch is on
    q.scmi(0, 0x14, 5, &[0, 148_500_000, 0, 0]);
    q.settle(400.0);
    state(q, "after a clock was set");
    q.msg(0, 0x803, &[1, 0]);
    q.settle(100.0);
    state(q, "off again");
    // what runs on a timer has to go on at its pace after the core's clock
    // went down and came back (the fans' loop, the heartbeat)
    if end.iter().all(|e| e == "ran") {
        end = q.settle(3000.0);
    }
    end
}

fn wake(q: &mut Q) -> Vec<String> {
    q.each(|c| {
        c.bus.se_wake();
        c.bus.s5_wake(&mut c.io);
    });
    q.until("quiet", None)
}

/// Suspend asked by PM message 3 (CIX's trigger_STR), a wake, then a
/// second suspend the usual way and its resume.
fn suspend_twice(q: &mut Q) -> Vec<String> {
    q.each(|c| c.bus.se_suspend());
    q.msg(0, 0x03, &[]);
    let a = q.until("asleep", None);
    q.read("after message 3", |_| Val::Nothing);
    q.notes.push(format!("after message 3: {a:?}"));
    let r = wake(q);
    q.notes.push(format!("resume: {r:?}"));
    q.scmi(TFA, 0x10, 0, &[]);
    q.scmi(0, 0x13, 8, &[3]);
    q.each(|c| c.bus.se_suspend());
    q.msg(TFA, test::STR_BOOT_CORE_NOTIFY, &[0]);
    q.read("suspend", |c| to_val(soc::scmi(c, 0x12, 3, &[0, 4], TFA, 3_000_000)));
    let a = q.until("asleep", None);
    for (out, x) in q.outs.iter_mut().zip(a.iter()) {
        out.push(("second suspend".to_string(), Val::Text(x.clone())));
    }
    let r = wake(q);
    q.scmi(0, 0x13, 8, &[3]);
    r
}

const STEPS: [(&str, Step); 27] = [
    ("SCMI answered by interrupt", by_interrupt),
    ("notifications: levels and limits by message", notify_by_message),
    ("notifications: a cluster by its fast channel", notify_by_fast_channel),
    ("the DSU stepped with the clusters (PM message 0x803)", dsu_with_clusters),
    ("SCMI message attributes, unknown messages and protocols", message_attributes),
    ("SCMI base: agents and permissions", base_agents),
    ("SCMI power domains: all, off and on", power_domains),
    ("SCMI system power: get, notify, bad states", system_power),
    ("SCMI performance: every domain and level, limits, notifications", performance),
    ("SCMI clocks: every clock read", clocks_read),
    ("SCMI clocks: each enabled and disabled", clocks_enable),
    ("SCMI clocks: rates set", clocks_rates),
    ("SCMI sensors: readings and trip points", sensors),
    ("malformed SCMI", malformed),
    ("PM messages: every id on every channel", pm_messages),
    ("PM messages: arguments (SMN, streams, locked levels, cores, modes)", pm_message_arguments),
    ("I2C slave: the EC's reads and writes", i2c_slave),
    ("fans: every mode on each fan, by time", fan_modes_by_time),
    ("fans: the automatic loop from 30 to 100 C and back and at raw codes, by time", fan_curve_by_time),
    ("thermal sweep 30 to 130 C and back", thermal_sweep),
    ("thermal sweep with the PVT warning line and PROCHOT unmasked", thermal_sweep_lines),
    ("thermal sweep with the PVT safe line unmasked too", thermal_sweep_safe_line),
    ("PCC commands by the fast channel", pcc),
    ("interrupt lines by themselves: PVT safe, PROCHOT, GPIO", lines_alone),
    ("the SoC idle switch and the DDR's frequency (PM message 0x803)", idle_switch),
    ("watchdog: time-outs with its interrupt on, then off", watchdog),
    ("suspend by PM message 3, then suspend again", suspend_twice),
];

/// Run the phases on the images (CIX's first), in step.  Returns each
/// image's phases.
pub fn suite(pms: &mut [Pm], opts: &Opts, covered: &mut [Vec<(u32, u32)>]) -> Vec<Vec<Phase>> {
    let mut out: Vec<Vec<Phase>> = pms.iter().map(|_| Vec::new()).collect();
    soc::POLL.with(|p| p.set(POLL));
    for pm in pms.iter_mut() {
        ms(&mut pm.cpu, 100.0);
    }
    for (name, step) in STEPS {
        if let Some(only) = &opts.only {
            if !name.contains(only.as_str()) {
                continue;
            }
        }
        let mut forks: Vec<Pm> = pms.to_vec();
        let starts: Vec<PhaseStart> = forks.iter().map(|f| PhaseStart::new(f, None)).collect();
        let (ends, outs, notes) = {
            let t0 = forks.iter().map(|f| f.cpu.io.time).collect();
            let mut q = Q { cpus: forks.iter_mut().map(|f| &mut f.cpu).collect(), outs: pms.iter().map(|_| Vec::new()).collect(), notes: Vec::new(), t0 };
            let ends = step(&mut q);
            (ends, q.outs, q.notes)
        };
        for (i, ((fork, start), answers)) in forks.iter().zip(starts).zip(outs).enumerate() {
            let mut ph = start.finish(fork, name, ends.get(i).cloned().unwrap_or_default(), answers);
            ph.notes = notes.clone();
            out[i].push(ph);
            covered[i].extend(fork.cpu.covered());
        }
    }
    out
}
