// SPDX-License-Identifier: BSD-2-Clause-Patent
//! Differential test of the open CSU_PM firmware against CIX's.
//!
//!   sky1emu pm-test [OPEN.elf] [--cix CIX.elf] [--scmi] [--power] [--services]
//!                   [--runtime MS] [--full] [--board NAME|PATH] [--set NAME=VALUE] [-v]
//!
//! Each image runs on the same Sky1 model (soc.rs) from reset until it
//! idles with the SE handshake done (0x5c5a, 0x55ca, 0x5a5c), without an
//! assertion.  Compared:
//!
//! * register writes, per register in order (UART and I2C controllers
//!   excepted: the I2C transfers are compared instead);
//! * the final contents of the shared memory it writes (the hub window at
//!   0x60000000: PCC tables, logs);
//! * with --scmi, the answers to the same SCMI commands on every channel
//!   (base protocol discovery, then each protocol's attributes and its
//!   domains), then performance levels and limits set on the OS's channel
//!   and read back (each set followed by the same number of instructions
//!   run on both), and levels written to the OS's performance fast
//!   channels (a write raises the channel's bit in the mailbox status), the
//!   level-get channel read back; and the I2C transfers and clock
//!   generator writes (PMIC voltages, PLLs) the SCMI commands lead to;
//! * with --power, after boot: CIX's PM messages as UEFI, Linux and TF-A
//!   send them (answers compared), TF-A's suspend on channel 2 (PM message
//!   0x42 with the boot core, then SCMI SYSTEM_POWER_STATE_SET suspend; the
//!   SE's side as firmware/se does it), the PM asleep until a wake-up, the
//!   resume back to idle, SCMI answered again; then shutdown, and warm
//!   reset from a copy of the booted state.  Per phase: register writes per
//!   register, I2C transfers, the SE handshake words, the S5 controller's
//!   events (sleep, wake, reset);
//! * with --services and --runtime MS, the board services and the DPM's
//!   runtime on simulated time;
//! * with --full (full.rs), what the suites above leave out.
//!
//! With CIX's image alone it prints what CIX's firmware does: the
//! reference.  The open image is asked the questions CIX's answers led to.

use super::soc::{self, Cpu, DevKind, Event, PmSoc};
use crate::util::{self, Args};
use std::collections::BTreeMap;

pub const CIX_PM: &str = "release/pm_fw/Release/csupm_sky1";
/// ours (SCP-firmware's task sleeps itself), CIX's (FreeRTOS)
pub const IDLE: [&str; 2] = ["arch_irq_wait", "vApplicationIdleHook"];
pub const BOOT_BUDGET: u64 = 8_000_000;
/// AP -> PM mailboxes with an SMT channel
pub const CHANNELS: [usize; 4] = [0, 2, 4, 6];
/// per protocol, in the short suite
const MAX_DOMAINS: u32 = 8;
/// GPU core, little, big 0, DSU
const PERF_SET_DOMAINS: [u32; 4] = [0, 2, 3, 7];
/// instructions run after a level or limits set
const SETTLE: u64 = 400_000;
/// the OS's performance fast channels: domain, offset in mailbox 0 (level
/// set) and mailbox 1 (level get); a write to channel c sets status bit 9 + c
const PERF_FC: [(u32, u32); 3] = [(0, 0x8c), (2, 0x94), (3, 0x98)];
/// clock generator: PLLs, compared during SCMI
pub const CGC: (u32, u32) = (0xa0060000, 0xa0070000);
/// the board's PM configuration (csu_pm_config.bin), which the SE copies
/// to PM RAM before the PM starts
pub const BIOS_CFG: u32 = 0x200bf000;

pub const BOARDS: [(&str, &str); 5] = [
    ("o6", "output/orion-o6/RELEASE/pr/Firmwares/csu_pm_config.bin"),
    ("o6n", "output/orion-o6n/RELEASE/pr/Firmwares/csu_pm_config.bin"),
    ("ms-r1", "output/ms-r1/RELEASE/pr/Firmwares/csu_pm_config.bin"),
    ("opi6", "vendor/orangepi/opi6/csu_pm_config.bin"),
    ("opi6plus", "output/opi6plus/RELEASE/pr/Firmwares/csu_pm_config.bin"),
];

/// --set NAME=VALUE: a config_data_t of pm_config_t (offsets in it) set to
/// a valid VALUE, the block's sums redone (CIX's do_pm_config_crc_check)
const CFG_WORDS: [(&str, usize); 8] = [
    ("gpio0", 0xd37),
    ("gpio1", 0xd3b),
    ("gpio2", 0xd3f),
    ("op_mode", 0xd43),
    ("psi7", 0xd47),
    ("wdt", 0xd2f),
    ("noc_idle", 0xcf3),
    ("trip", 0x5e),
];

pub type Regs = BTreeMap<u32, Vec<u64>>;

/// Pages of the shared memory that are each image's own: the RAM log
/// (CIX's holds bytes from past its line buffer, which is never flushed;
/// ours the messages logged) and the version page.
const OWN_PAGES: [(u32, &str); 3] = [
    (0x63db8000, "the RAM log"),
    (0x63db9000, "the RAM log"),
    (0x63e01000, "the firmware's version"),
];
/// The DSU's PLL (clock generator block 5): a DSU level by message sets
/// only its post-divider in the open firmware (README: one owner), CIX's
/// DVFS programs it.
const DSU_PLL: (u32, u32) = (0xa0060500, 0xa0060600);

/// shared memory, compared by its final contents: the hub window (CIX's
/// _memmap_mem_sram_hub, DRAM and SRAM seen through a window)
pub fn is_memory(addr: u32) -> bool {
    (0x60000000..0xa0000000).contains(&addr)
}

/// not compared register by register: UART data, I2C controllers (the
/// transfers are compared) and the aggregator's I2C source enables (line
/// 24: the open firmware's masters wait for their interrupts, CIX's poll)
pub fn ignored(addr: u32) -> bool {
    matches!(addr & !0xfff, 0xa0030000 | 0xa0010000 | 0xa0011000 | 0xa0012000 | 0xa0013000)
        || (0xa00005b0..0xa00005b4).contains(&addr)
}

fn in_ram(addr: u32) -> bool {
    (soc::RAM.0..soc::RAM.0 + soc::RAM.1).contains(&addr)
}

/// The block's two sums over its words (CIX's do_pm_config_crc_check).
pub fn config_sums(blk: &mut [u8]) {
    let n = util::u32le(blk, 8) as usize;
    blk[0x10..0x18].fill(0);
    let (mut a, mut b) = (0u32, 0u32);
    for i in (0..n).step_by(4) {
        a = a.wrapping_add(util::u32le(blk, i));
        b = b.wrapping_add(a);
    }
    util::put32(blk, 0x10, a);
    util::put32(blk, 0x14, b);
}

/// --board PATH or NAME: the configuration's bytes, with --set's words.
pub fn board_config(arg: &str, sets: &[String]) -> Result<Vec<u8>, String> {
    let path = if std::path::Path::new(arg).exists() {
        std::path::PathBuf::from(arg)
    } else {
        let lower = arg.to_lowercase();
        util::root().join(BOARDS.iter().find(|b| b.0 == lower).map_or(arg, |b| b.1))
    };
    let mut blk = std::fs::read(&path).map_err(|e| format!("{}: {e}", path.display()))?;
    for item in sets {
        let (name, value) = item.split_once('=').ok_or(format!("--set {item}: NAME=VALUE"))?;
        let off = CFG_WORDS.iter().find(|w| w.0 == name).ok_or(format!("--set: no word {name}"))?.1;
        let v = util::parse_int(value).ok_or(format!("--set {item}: not a number"))? as u32;
        util::put32(&mut blk, 0x18 + off, v << 1);
    }
    if !sets.is_empty() {
        config_sums(&mut blk);
    }
    Ok(blk)
}

/// What a test run is told: the board, which suites.
#[derive(Clone, Default)]
pub struct Opts {
    pub board: Option<Vec<u8>>,
    pub services: bool,
    pub runtime: u32,
    pub full: bool,
    /// a model set-up before the image starts
    pub prepare: Option<fn(&mut Cpu)>,
    /// --only TEXT: the --full phases whose name has TEXT
    pub only: Option<String>,
    /// --audit: the interpreter's window and interrupt audit on (a check
    /// of the interpreter itself: an image it stops was run wrongly)
    pub audit: bool,
}

/// A booted image.
#[derive(Clone)]
pub struct Pm {
    pub cpu: Cpu,
    /// pm_ready's work starts here (after it, in CIX's task)
    pub handshake_write: usize,
}

pub fn boot(elf: &str, opts: &Opts) -> Result<(Pm, Option<String>, Regs), String> {
    let mut cpu = soc::boot(elf, None)?;
    cpu.bus.count_reads = true;
    if opts.audit {
        cpu.audit = Some(std::collections::HashMap::new());
    }
    if let Some(b) = &opts.board {
        cpu.write_bytes(BIOS_CFG, b);
    }
    if let Some(f) = opts.prepare {
        f(&mut cpu);
    }
    for name in soc::ASSERTS {
        if let Some(a) = cpu.byname.get(name).copied() {
            cpu.set_stop(a, name);
        }
    }
    let idle = IDLE.iter().find_map(|n| cpu.byname.get(*n).copied()).ok_or("no idle function in the image")?;
    cpu.set_stop(idle, "idle");
    let mut why = cpu.run(BOOT_BUDGET);
    cpu.del_stop(idle);
    // the PM answers the SE's 0x55ca from a task, after it first idles
    while why.as_deref() == Some("idle") && cpu.bus.se_log.last() != Some(&0x5a5c) && cpu.insns < BOOT_BUDGET {
        why = Some(cpu.run(50_000).unwrap_or_else(|| "idle".to_string()));
    }
    // the boot compared ends with the handshake word: CIX's pm_ready writes
    // it, then (in its task, after the boot capture) starts the DPM's
    // streams and the board's services, which --runtime compares
    let end = cpu.writes.iter().position(|w| w.addr == soc::PM_MAGIC && w.value == 0x5a5c).map_or(cpu.writes.len(), |i| i + 1);
    let mut regs = Regs::new();
    for w in &cpu.writes[..end] {
        if !ignored(w.addr) && !is_memory(w.addr) && !in_ram(w.addr) {
            regs.entry(w.addr).or_default().push(w.value);
        }
    }
    Ok((Pm { cpu, handshake_write: end }, why, regs))
}

/// Pages of the shared memory the run wrote: page -> bytes.
pub fn memory(soc: &PmSoc) -> BTreeMap<u32, Vec<u8>> {
    soc.mem.pages.iter().filter(|(p, _)| is_memory(*p << 12)).map(|(p, d)| (p << 12, d[..0x1000].to_vec())).collect()
}

/// What the AP does, on a channel.
#[derive(Clone, Debug, PartialEq)]
pub enum Ask {
    Scmi(u32, u32, Vec<u32>),
    /// an SCMI command whose length field is not its arguments'
    ScmiLen(u32, u32, Vec<u32>, u32),
    /// an SCMI command answered with an interrupt (the channel's flag set)
    ScmiIntr(u32, u32, Vec<u32>),
    /// the AP writes a fast channel
    Fc(u32, u32),
    /// the AP reads a word of a mailbox (offset from MBOX)
    Read(u32),
    /// let the PM run (a set completing)
    Settle(u64),
}

/// An answer, or a value read from the image after a step.
#[derive(Clone, Debug, PartialEq)]
pub enum Val {
    /// not asked (the image did not idle)
    Nothing,
    Scmi(Option<i32>, Vec<u32>),
    /// a PM message's status and words
    Msg(u32, Vec<u32>),
    Error(String),
    Written,
    Settled,
    Bytes(Option<Vec<u8>>),
    U32s(Option<Vec<u32>>),
    I32s(Option<Vec<i32>>),
    F32s(Option<Vec<f32>>),
    Levels(Option<i32>, u32, u32),
    Text(String),
}

fn words(w: &[u32]) -> String {
    w.iter().map(|x| format!("{x:#x}")).collect::<Vec<_>>().join(" ")
}

impl std::fmt::Display for Val {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        match self {
            Val::Nothing => write!(f, "-"),
            Val::Scmi(st, w) => match st {
                Some(s) => write!(f, "{s} [{}]", words(w)),
                None => write!(f, "None [{}]", words(w)),
            },
            Val::Msg(st, w) => write!(f, "{st} [{}]", words(w)),
            Val::Error(e) => write!(f, "error ({e})"),
            Val::Written => write!(f, "written []"),
            Val::Settled => write!(f, "settled []"),
            Val::Bytes(None) | Val::U32s(None) | Val::I32s(None) | Val::F32s(None) => write!(f, "None"),
            Val::Bytes(Some(b)) => write!(f, "{}", util::hex(b)),
            Val::U32s(Some(v)) => write!(f, "{v:?}"),
            Val::I32s(Some(v)) => write!(f, "{v:?}"),
            Val::F32s(Some(v)) => write!(f, "{v:?}"),
            Val::Levels(st, n, level) => write!(f, "({st:?}, {n}, {level})"),
            Val::Text(s) => write!(f, "{s}"),
        }
    }
}

pub fn to_val(a: soc::Answer) -> Val {
    match a {
        Ok((st, w)) => Val::Scmi(st, w),
        // the PM's answer to a message it cannot take: the channel's error bit
        Err(e) if e == "channel error" => Val::Text(e),
        Err(e) => Val::Error(e),
    }
}

pub fn ask(cpu: &mut Cpu, chan: usize, q: &Ask) -> Val {
    let base = soc::MBOX + chan as u32 * 0x10000;
    match q {
        Ask::Fc(off, value) => {
            cpu.bus.mem.write32(base + off, *value);
            cpu.bus.ring(&mut cpu.io, chan, 1 << (9 + (off - 0x8c) / 4));
            Val::Written
        }
        Ask::Read(off) => Val::Scmi(Some(0), vec![cpu.bus.mem.read32(soc::MBOX + off)]),
        Ask::Settle(n) => {
            cpu.run(*n);
            Val::Settled
        }
        Ask::Scmi(proto, msg, args) => to_val(soc::scmi(cpu, *proto, *msg, args, chan, 3_000_000)),
        Ask::ScmiLen(proto, msg, args, len) => to_val(soc::scmi_raw(cpu, *proto, *msg, args, chan, 3_000_000, Some(*len), 0)),
        Ask::ScmiIntr(proto, msg, args) => to_val(soc::scmi_raw(cpu, *proto, *msg, args, chan, 3_000_000, None, 1)),
    }
}

pub type Asked = Vec<((usize, Ask), Val)>;

/// The levels PERFORMANCE_DESCRIBE_LEVELS gave (words of an answer).
pub fn levels_of(w: &[u32]) -> Vec<u32> {
    if w.is_empty() {
        return Vec::new();
    }
    let n = ((w[0] & 0xfff) as usize).min((w.len() - 1) / 3);
    (0..n).map(|k| w[1 + 3 * k]).collect()
}

/// (question, answer) pairs, the questions chosen from the answers.
fn scmi_suite(cpu: &mut Cpu) -> Asked {
    let mut out: Asked = Vec::new();
    let q = |cpu: &mut Cpu, out: &mut Asked, chan: usize, a: Ask| -> Val {
        let v = ask(cpu, chan, &a);
        out.push(((chan, a), v.clone()));
        v
    };
    let scmi = |p: u32, m: u32, a: &[u32]| Ask::Scmi(p, m, a.to_vec());
    for chan in CHANNELS {
        for msg in [0, 1, 3, 4, 5] {
            q(cpu, &mut out, chan, scmi(0x10, msg, &[]));
        }
        let mut protos = Vec::new();
        if let Val::Scmi(Some(0), w) = q(cpu, &mut out, chan, scmi(0x10, 6, &[0])) {
            if !w.is_empty() {
                protos = w[1..].iter().flat_map(|x| (0..4).map(move |k| (x >> (8 * k)) & 0xff)).take(w[0] as usize).collect();
            }
        }
        for p in protos {
            q(cpu, &mut out, chan, scmi(p, 0, &[]));
            let n = match q(cpu, &mut out, chan, scmi(p, 1, &[])) {
                Val::Scmi(Some(0), w) if !w.is_empty() => w[0] & 0xffff,
                _ => 0,
            };
            match p {
                0x11 => {
                    // power: attributes, state
                    for d in 0..n.min(MAX_DOMAINS) {
                        q(cpu, &mut out, chan, scmi(p, 3, &[d]));
                        q(cpu, &mut out, chan, scmi(p, 5, &[d]));
                    }
                }
                0x13 => {
                    // perf: attributes, levels, limits, level
                    for d in 0..n.min(MAX_DOMAINS) {
                        q(cpu, &mut out, chan, scmi(p, 3, &[d]));
                        q(cpu, &mut out, chan, scmi(p, 4, &[d, 0]));
                        q(cpu, &mut out, chan, scmi(p, 6, &[d]));
                        q(cpu, &mut out, chan, scmi(p, 8, &[d]));
                    }
                }
                0x14 => {
                    // clock: attributes, rate
                    for d in 0..n.min(MAX_DOMAINS) {
                        q(cpu, &mut out, chan, scmi(p, 3, &[d]));
                        q(cpu, &mut out, chan, scmi(p, 6, &[d]));
                    }
                }
                0x15 => {
                    q(cpu, &mut out, chan, scmi(p, 3, &[0])); // sensor: descriptions
                }
                _ => {}
            }
        }
    }
    // performance levels set on the OS's channel, from its first and last
    // level, and read back; limits (CIX lets the OS set none) likewise
    let mut levels: BTreeMap<u32, Vec<u32>> = BTreeMap::new();
    for ((chan, a), v) in &out {
        if let (0, Ask::Scmi(0x13, 4, args), Val::Scmi(Some(0), w)) = (*chan, a, v) {
            if !w.is_empty() {
                levels.insert(args[0], w.clone());
            }
        }
    }
    let lv_of = |d: u32| levels.get(&d).map(|w| levels_of(w)).unwrap_or_default();
    for d in PERF_SET_DOMAINS {
        let lv = lv_of(d);
        let ends: Vec<u32> = if lv.is_empty() { vec![1] } else { vec![lv[0], lv[lv.len() - 1]] };
        for level in ends {
            q(cpu, &mut out, 0, scmi(0x13, 7, &[d, level]));
            q(cpu, &mut out, 0, Ask::Settle(SETTLE));
            q(cpu, &mut out, 0, scmi(0x13, 8, &[d]));
        }
        if !lv.is_empty() {
            q(cpu, &mut out, 0, scmi(0x13, 5, &[d, lv[lv.len() - 1], lv[0]]));
            q(cpu, &mut out, 0, Ask::Settle(SETTLE));
            q(cpu, &mut out, 0, scmi(0x13, 6, &[d]));
        }
    }
    for (d, off) in PERF_FC {
        let lv = lv_of(d);
        if lv.is_empty() {
            continue;
        }
        for level in [lv[0], lv[lv.len() - 1]] {
            q(cpu, &mut out, 0, Ask::Fc(off, level));
            q(cpu, &mut out, 0, Ask::Settle(SETTLE));
            q(cpu, &mut out, 0, Ask::Read(0x10000 + off));
        }
    }
    out
}

/// What a phase compares besides the registers.
#[derive(Clone, Debug)]
pub struct Phase {
    pub name: String,
    pub end: String,
    pub insns: u64,
    /// simulated time the phase took
    pub ms: f64,
    pub regs: Regs,
    pub i2c: Vec<String>,
    pub se: Vec<u32>,
    pub events: Vec<Event>,
    /// doorbells the PM rang towards the AP
    pub doorbells: Vec<(usize, u32)>,
    pub answers: Vec<(String, Val)>,
    /// the registers' writes in the order they were made
    #[allow(dead_code)]
    pub order: Vec<(u32, u64)>,
    /// what happened to an image a question killed (not compared)
    pub notes: Vec<String>,
}

#[derive(Clone)]
pub struct Run {
    pub name: String,
    pub why: Option<String>,
    pub insns: u64,
    pub se: Vec<u32>,
    pub regs: Regs,
    /// the registers the boot read
    pub reads: Vec<u32>,
    pub i2c: Vec<String>,
    pub memory: BTreeMap<u32, Vec<u8>>,
    pub backtrace: String,
    pub scmi: Option<Asked>,
    pub i2c_scmi: Vec<String>,
    pub cgc_scmi: Vec<(u32, u64)>,
    pub power: Option<Vec<Phase>>,
    pub services: Option<Vec<Phase>>,
    pub runtime: Option<Vec<Phase>>,
    pub full: Option<Vec<Phase>>,
    /// the booted image, for the phases of --full
    pub full_base: Option<Pm>,
    /// executed code, for the coverage report
    pub covered: Vec<(u32, u32)>,
}

fn merge_cov(into: &mut Vec<(u32, u32)>, cpu: &Cpu) {
    into.extend(cpu.covered());
}

/// Boot an image; ask the questions (from CIX's run) or, with suite, the
/// SCMI suite; with power, then the power phases (on a copy of the booted
/// state, so the SCMI ones are not disturbed).
pub fn run(elf: &str, questions: Option<&Asked>, suite: bool, power: bool, opts: &Opts) -> Result<Run, String> {
    let (mut pm, why, regs) = boot(elf, opts)?;
    let idle = why.as_deref() == Some("idle");
    let booted = (power && idle).then(|| pm.clone());
    let svc = (opts.services && idle).then(|| pm.clone());
    let full = (opts.full && idle).then(|| pm.clone());
    let cpu = &mut pm.cpu;
    let mut reads: Vec<u32> = cpu.bus.reads.keys().copied().filter(|a| !ignored(*a) && !is_memory(*a)).collect();
    reads.sort();
    let mut r = Run {
        name: std::path::Path::new(elf).file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default(),
        why,
        insns: cpu.insns,
        se: cpu.bus.se_log.clone(),
        regs,
        reads,
        i2c: cpu.bus.i2c_log.iter().map(|l| l.what.clone()).collect(),
        memory: memory(&cpu.bus),
        backtrace: cpu.backtrace_names(8),
        scmi: None,
        i2c_scmi: Vec::new(),
        cgc_scmi: Vec::new(),
        power: None,
        services: None,
        runtime: None,
        full: None,
        full_base: full,
        covered: Vec::new(),
    };
    let (n_i2c, n_writes) = (cpu.bus.i2c_log.len(), cpu.writes.len());
    if let Some(qs) = questions {
        r.scmi = Some(qs.iter().map(|((chan, a), _)| ((*chan, a.clone()), if idle { ask(cpu, *chan, a) } else { Val::Nothing })).collect());
    } else if suite && idle {
        r.scmi = Some(scmi_suite(cpu));
    }
    r.i2c_scmi = cpu.bus.i2c_log[n_i2c..].iter().map(|l| l.what.clone()).collect();
    r.cgc_scmi = cpu.writes[n_writes..].iter().filter(|w| (CGC.0..CGC.1).contains(&w.addr)).map(|w| (w.addr, w.value)).collect();
    if let Some(mut b) = booted {
        r.power = Some(power_suite(&mut b, &mut r.covered));
    }
    if let Some(mut s) = svc {
        r.services = Some(services_suite(&mut s));
        merge_cov(&mut r.covered, &s.cpu);
    }
    if opts.runtime != 0 && idle {
        r.runtime = Some(runtime_suite(&mut pm, opts.runtime));
    }
    merge_cov(&mut r.covered, &pm.cpu);
    Ok(r)
}

// --- power phases (--power) -----------------------------------------------------

const SETTLE_BOOT: u64 = 3_000_000; // instructions after the handshake (CIX's pm_ready)
pub const CHUNK: u64 = 500_000;
const QUIET: u32 = 2;
const POWER_BUDGET: u64 = 40_000_000;
/// TF-A's channel
pub const TFA: usize = 2;
pub const STR_BOOT_CORE_NOTIFY: u32 = 0x42;
/// scmi_send_msg_to_pm: buf_len 0x3f, msg_route 0x0c
const PM_MSG_ATTR: u32 = 0x3f | 0x0c << 8;
/// S3 boot script (UEFI's S3SaveStateDxe, BootScriptInternalFormat.h) at a
/// DRAM address, seen by the PM through TLB entry 5 (0x60000000 + 5 << 27)
const S3SCRIPT_DRAM: u32 = 0x8f000000;
const S3SCRIPT_HUB: u32 = 0x60000000 + (5 << 27) + (S3SCRIPT_DRAM & 0x7ffffff);

/// CIX's PM messages (_portX_msg): channel, id, payload, who sends it
const PM_MESSAGES: [(usize, u32, &[u32], &str); 20] = [
    (0, 0x00, &[1, 2], "channel test"),
    (0, 0x01, &[], "firmware version (UEFI CsuPmMsgGetFirmwareVersion)"),
    (0, 0x02, &[1000], "deferred notify (test)"),
    (0, 0x40, &[3], "cpu online (Linux)"),
    (0, 0x41, &[3], "cpu offline (Linux)"),
    (0, 0x50, &[0x05040100], "SMN read (test)"),
    (0, 0x50, &[0x40000000], "SMN read, out of range"),
    (0, 0x60, &[], "platform info (UEFI options)"),
    (0, 0x61, &[0x1000], "SCMI clock reference, no such clock (UEFI)"),
    (0, 0x62, &[S3SCRIPT_DRAM, 0, 0, 0], "register the S3 script (UEFI S3SaveStateDxe)"),
    (0, 0x63, &[], "PMIC versions (UEFI SetupManager)"),
    (0, 0x400, &[], "PMIC events (Linux)"),
    (0, 0x800, &[], "fan count (Linux)"),
    (0, 0x801, &[0], "fan status (Linux)"),
    (0, 0x801, &[5], "fan status, no such fan"),
    (0, 0x804, &[], "EDP voltages (Linux)"),
    (0, 0x805, &[], "EDP set voltages (Linux)"),
    (0, 0x1234, &[], "no such message"),
    (2, 0x01, &[], "firmware version on TF-A's channel"),
    (4, 0x01, &[], "firmware version on channel 4"),
];

/// A script UEFI could leave: a 32-bit write and a read-modify-write of a
/// scratch register in the S5 block, then the terminator.
fn s3script_table() -> Vec<u8> {
    let mut ops = vec![0x00, 16, 2, 1];
    ops.extend_from_slice(&0x16000804u64.to_le_bytes());
    ops.extend_from_slice(&0x5a5a0001u32.to_le_bytes());
    ops.extend_from_slice(&[0x01, 19, 2]);
    ops.extend_from_slice(&0x16000808u64.to_le_bytes());
    ops.extend_from_slice(&0x00000100u32.to_le_bytes());
    ops.extend_from_slice(&0xffff00ffu32.to_le_bytes());
    ops.extend_from_slice(&[0xff, 2]);
    let mut t = vec![0xaa, 12];
    t.extend_from_slice(&1u16.to_le_bytes());
    t.extend_from_slice(&(12 + ops.len() as u32).to_le_bytes());
    t.extend_from_slice(&[0; 4]);
    t.extend(ops);
    t
}

/// Send one of CIX's PM messages as TF-A's scmi_send_msg_to_pm does:
/// msgAttr (word 0) says it is one, the header has the message id, the
/// answer is the status code (word 2), the length and the payload from
/// word 12 of the payload on.
pub fn pm_msg(cpu: &mut Cpu, chan: usize, msg_id: u32, args: &[u32]) -> Val {
    let base = soc::MBOX + chan as u32 * 0x10000;
    let end = cpu.insns + 3_000_000;
    let late = cpu.io.ccount + soc::ANSWER_MS * soc::CORE_HZ / 1000;
    while cpu.bus.mem.read32(base + 4) & 1 == 0 {
        if cpu.insns >= end || cpu.io.ccount >= late {
            return Val::Error("channel never free".into());
        }
        if soc::poll_run(cpu).is_some() {
            return Val::Error("stopped".into());
        }
    }
    let m = &mut cpu.bus.mem;
    m.write32(base + 4, m.read32(base + 4) & !1);
    m.write32(base + 0x18, (m.read32(base + 0x18) & !0xffff) | msg_id);
    m.write32(base, PM_MSG_ATTR);
    m.write32(base + 0x10, 0);
    m.write32(base + 0x14, 4 + 4 * args.len() as u32);
    for (i, a) in args.iter().enumerate() {
        m.write32(base + 0x1c + 4 * i as u32, *a);
    }
    cpu.bus.ring(&mut cpu.io, chan, 1);
    while cpu.bus.mem.read32(base + 4) & 1 == 0 {
        if cpu.insns >= end || cpu.io.ccount >= late {
            return Val::Error("no answer".into());
        }
        let why = soc::poll_run(cpu);
        let free = cpu.bus.mem.read32(base + 4) & 1 != 0;
        if let Some(w) = &why {
            if !w.starts_with("waiti with nothing") {
                return Val::Error(format!("stopped: {w}"));
            }
            if !free {
                return Val::Error("no answer (the PM sleeps)".into());
            }
        }
    }
    let m = &cpu.bus.mem;
    let n = ((m.read32(base + 0x14) as i64 - 4).div_euclid(4)).clamp(0, 13) as u32;
    let mut w = vec![m.read32(base) & 0xff00, m.read32(base + 0x18) & 0xffff];
    w.extend((0..n).map(|i| m.read32(base + 0x4c + 4 * i)));
    Val::Msg(m.read32(base + 8), w)
}

/// A register write the power phases compare: not the shared memory, the
/// PM's RAM, the hardware mutexes (0xa0000900 + 4 i, which CIX's tasks take
/// and release all the time), nor the data words of the AP's request
/// channels (their answers are compared instead).
pub fn counted(addr: u32) -> bool {
    let ch = (addr >> 16) as i64 - (soc::MBOX >> 16) as i64;
    if (0..8).contains(&ch) && CHANNELS.contains(&(ch as usize)) && addr & 0xffff < 0x80 {
        return false;
    }
    !ignored(addr) && !is_memory(addr) && !in_ram(addr) && !(0xa0000900..0xa0000a90).contains(&addr)
}

/// Run until the PM sleeps with nothing to wake it ("asleep"), writes the
/// warm reset ("reset"), or (until = "quiet") writes no register for QUIET
/// chunks in a row; or the budget runs out.
pub fn run_until(cpu: &mut Cpu, until: &str, budget: Option<u64>) -> String {
    let end = cpu.insns + budget.unwrap_or(POWER_BUDGET);
    let mut quiet = 0;
    if until == "reset" && cpu.bus.events.contains(&Event::WarmReset(1)) {
        return "reset".into(); // while the answer was awaited
    }
    cpu.stop_write = Some((soc::S5_PMCTRL + soc::S5_WARM_RST, 1));
    let mut out = "budget".to_string();
    while cpu.insns < end {
        let n = cpu.writes.len();
        if let Some(why) = cpu.run(CHUNK) {
            out = if why.starts_with("waiti with nothing") {
                if until == "quiet" { "quiet".into() } else { "asleep".into() }
            } else if why.starts_with("write") {
                "reset".into()
            } else {
                format!("stopped: {why} at {}", cpu.func(cpu.pc))
            };
            break;
        }
        if until == "quiet" {
            quiet = if cpu.writes[n..].iter().any(|w| counted(w.addr)) { 0 } else { quiet + 1 };
            if quiet >= QUIET {
                out = "quiet".into();
                break;
            }
        }
    }
    cpu.stop_write = None;
    out
}

/// Where a phase starts in an image's logs.
pub struct PhaseStart {
    writes: usize,
    i2c: usize,
    se: usize,
    events: usize,
    doorbells: usize,
    insns: u64,
    ccount: u64,
}

impl PhaseStart {
    /// since: the write the phase's registers are counted from, when not now.
    pub fn new(pm: &Pm, since: Option<usize>) -> Self {
        let cpu = &pm.cpu;
        PhaseStart {
            writes: since.unwrap_or(cpu.writes.len()),
            i2c: cpu.bus.i2c_log.len(),
            se: cpu.bus.se_log.len(),
            events: cpu.bus.events.len(),
            doorbells: cpu.bus.doorbells.len(),
            insns: cpu.insns,
            ccount: cpu.io.ccount,
        }
    }

    /// What the PM did since: registers written, I2C, SE handshake words,
    /// S5 events, doorbells.
    pub fn finish(self, pm: &Pm, name: &str, end: String, answers: Vec<(String, Val)>) -> Phase {
        let cpu = &pm.cpu;
        let mut regs = Regs::new();
        let mut order = Vec::new();
        for w in &cpu.writes[self.writes.min(cpu.writes.len())..] {
            if counted(w.addr) {
                regs.entry(w.addr).or_default().push(w.value);
                order.push((w.addr, w.value));
            }
        }
        Phase {
            name: name.to_string(),
            end,
            insns: cpu.insns - self.insns,
            ms: (cpu.io.ccount - self.ccount) as f64 * 1000.0 / soc::CORE_HZ as f64,
            regs,
            order,
            i2c: cpu.bus.i2c_log[self.i2c..].iter().map(|l| l.what.clone()).collect(),
            se: cpu.bus.se_log[self.se..].to_vec(),
            events: cpu.bus.events[self.events..].to_vec(),
            doorbells: cpu.bus.doorbells[self.doorbells..].to_vec(),
            answers,
            notes: Vec::new(),
        }
    }
}

/// Run action(); what the PM did meanwhile: registers written (from
/// writes[since] when given), I2C, SE handshake words, S5 events, answers.
pub fn phase(pm: &mut Pm, name: &str, since: Option<usize>, action: impl FnOnce(&mut Pm) -> (String, Vec<(String, Val)>)) -> Phase {
    let start = PhaseStart::new(pm, since);
    let (end, answers) = action(pm);
    start.finish(pm, name, end, answers)
}

fn tfa_scmi(cpu: &mut Cpu, name: &str, proto: u32, msg: u32, args: &[u32]) -> (String, Val) {
    (name.to_string(), to_val(soc::scmi(cpu, proto, msg, args, TFA, 3_000_000)))
}

fn power_suite(pm: &mut Pm, covered: &mut Vec<(u32, u32)>) -> Vec<Phase> {
    let mut out = Vec::new();
    pm.cpu.run(SETTLE_BOOT); // CIX's pm_ready finishes after the handshake
    let mut forked = pm.clone(); // for the warm reset

    out.push(phase(pm, "PM messages", None, |pm| {
        let cpu = &mut pm.cpu;
        for (i, b) in s3script_table().iter().enumerate() {
            cpu.bus.mem.write(S3SCRIPT_HUB + i as u32, 1, *b as u64);
        }
        let ans = PM_MESSAGES.iter().map(|(ch, mid, a, _)| (format!("({ch}, {mid:#x}, {a:?})"), pm_msg(cpu, *ch, *mid, a))).collect();
        (run_until(cpu, "quiet", Some(4 * CHUNK)), ans)
    }));

    out.push(phase(pm, "suspend", None, |pm| {
        let cpu = &mut pm.cpu;
        cpu.bus.se_suspend(); // TF-A's FFA_SUSPEND (mbox_set_suspend) first
        let ans = vec![
            ("0x42".to_string(), pm_msg(cpu, TFA, STR_BOOT_CORE_NOTIFY, &[0])),
            tfa_scmi(cpu, "suspend", 0x12, 3, &[0, 4]),
        ];
        (run_until(cpu, "asleep", None), ans)
    }));

    out.push(phase(pm, "resume", None, |pm| {
        let cpu = &mut pm.cpu;
        cpu.bus.se_wake(); // the SE resumes from ROM, then the PM's wake interrupt
        cpu.bus.s5_wake(&mut cpu.io);
        (run_until(cpu, "quiet", None), Vec::new())
    }));

    out.push(phase(pm, "after resume", None, |pm| {
        let cpu = &mut pm.cpu;
        let mut ans = Vec::new();
        let qs: [(usize, u32, u32, &[u32]); 5] = [(TFA, 0x10, 0, &[]), (TFA, 0x12, 1, &[]), (0, 0x13, 8, &[0]), (0, 0x15, 1, &[]), (0, 0x14, 6, &[0])];
        for (chan, proto, msg, args) in qs {
            ans.push((format!("({chan}, {proto:#x}, {msg})"), to_val(soc::scmi(cpu, proto, msg, args, chan, 5_000_000))));
        }
        ans.push(("0x42".to_string(), pm_msg(cpu, TFA, STR_BOOT_CORE_NOTIFY, &[0])));
        (run_until(cpu, "quiet", Some(4 * CHUNK)), ans)
    }));

    out.push(phase(pm, "shutdown", None, |pm| {
        let ans = vec![tfa_scmi(&mut pm.cpu, "shutdown", 0x12, 3, &[0, 0])];
        (run_until(&mut pm.cpu, "quiet", None), ans)
    }));
    merge_cov(covered, &pm.cpu);

    out.push(phase(&mut forked, "warm reset", None, |pm| {
        let ans = vec![tfa_scmi(&mut pm.cpu, "warm reset", 0x12, 3, &[0, 2])];
        (run_until(&mut pm.cpu, "reset", None), ans)
    }));
    merge_cov(covered, &forked.cpu);
    out
}

// --- board services (--services) ---------------------------------------------------

/// The bytes of a variable both images name alike (CIX's names).
pub fn sym_bytes(cpu: &mut Cpu, name: &str, size: u32) -> Option<Vec<u8>> {
    let a = cpu.objects_first.get(name).copied()?;
    Some(cpu.read_bytes(a, size))
}

pub fn ms(cpu: &mut Cpu, n: f64) -> String {
    soc::run_for(cpu, n).unwrap_or_else(|| "ran".to_string())
}

pub fn temps(cpu: &mut Cpu, t: f64) {
    for bus in cpu.bus.i2c.iter_mut() {
        for dev in bus.devices.values_mut() {
            if dev.kind == DevKind::Tmp75 {
                dev.set_temp(t);
            }
        }
    }
    cpu.bus.set_temps(&mut cpu.io, t);
}

/// CIX's firmware never enables aggregator lines 28-30 (it writes 28's
/// mask 0; their reset values are not known): as if they were on.
pub fn unmask(cpu: &mut Cpu, line: u32, bits: u32) {
    let a = soc::AGG + 0x80 + 4 * (line - 12);
    let v = cpu.bus.mem.read32(a) | bits;
    cpu.bus.mem.write32(a, v);
    cpu.bus.update_irqs(&mut cpu.io);
}

pub fn set_raw(cpu: &mut Cpu, line: usize, v: u32) {
    cpu.bus.irq_raw[line] = v;
    cpu.bus.update_irqs(&mut cpu.io);
}

fn fans_status(cpu: &mut Cpu) -> Vec<(String, Val)> {
    (0..3).map(|f| (format!("('0x801', {f})"), pm_msg(cpu, 0, 0x801, &[f]))).collect()
}

/// The board services CIX's firmware runs, on simulated time (run_for):
/// what pm_ready starts; fans at two temperatures and in Linux's modes
/// (PM messages 0x801 and 0x802; the fan's speed follows its PWM duty in
/// the model); a PVT sensor over its warning threshold; PROCHOT asserted
/// then released; a PMIC fault read by PM message 0x400; the DDR DFS check
/// after PM message 0x803 turns the SoC idle switch on.  Per phase,
/// compared as the power phases are.
fn services_suite(pm: &mut Pm) -> Vec<Phase> {
    let mut out = Vec::new();
    // pm_ready's work from the handshake on, wherever each image does it
    let since = Some(pm.handshake_write);
    out.push(phase(pm, "pm_ready (fans, PVT polling, NoC idle, DPM)", since, |pm| (ms(&mut pm.cpu, 100.0), Vec::new())));

    out.push(phase(pm, "fans at 35 C", None, |pm| {
        temps(&mut pm.cpu, 35.0);
        (ms(&mut pm.cpu, 650.0), fans_status(&mut pm.cpu))
    }));

    out.push(phase(pm, "fans at 75 C", None, |pm| {
        temps(&mut pm.cpu, 75.0);
        (ms(&mut pm.cpu, 500.0), fans_status(&mut pm.cpu))
    }));

    out.push(phase(pm, "fan modes (Linux)", None, |pm| {
        let cpu = &mut pm.cpu;
        let modes: [&[u32]; 6] =
            [&[2, 0, 0, 0], &[4, 1, 2000, 0], &[5, 2, 40, 0], &[3, 0, 1, 3, 0x00000a05, 0x0bb82823, 0x13883c37], &[1, 5, 0, 0], &[7, 0, 0, 0]];
        let mut ans: Vec<(String, Val)> = modes.iter().map(|a| (format!("('0x802', {a:?})"), pm_msg(cpu, 0, 0x802, a))).collect();
        let end = ms(cpu, 500.0);
        ans.extend(fans_status(cpu));
        (end, ans)
    }));

    // The two alarm phases each run on a copy that is dropped after it:
    // CIX's firmware leaves these lines masked, and does not survive them
    // (full.rs, ALARM_LINES); the phases after find every image as it was.
    let mut copy = pm.clone();
    out.push(phase(&mut copy, "PVT sensor 3 over its threshold (line 29 unmasked)", None, |pm| {
        let cpu = &mut pm.cpu;
        unmask(cpu, 29, 0x1fff);
        cpu.bus.pvt_temp[3] = Some(110.0);
        cpu.bus.pvt_update(&mut cpu.io);
        let end = ms(cpu, 5.0);
        cpu.bus.pvt_temp[3] = Some(40.0);
        cpu.bus.pvt_update(&mut cpu.io);
        (if end == "ran" { ms(cpu, 5.0) } else { end }, Vec::new())
    }));

    let mut copy = pm.clone();
    out.push(phase(&mut copy, "PROCHOT asserted 3 ms (lines 29, 30 unmasked)", None, |pm| {
        let cpu = &mut pm.cpu;
        unmask(cpu, 29, 0x1fff);
        unmask(cpu, 30, 1);
        set_raw(cpu, 30, 1);
        let end = ms(cpu, 3.0);
        set_raw(cpu, 30, 0);
        (if end == "ran" { ms(cpu, 40.0) } else { end }, Vec::new())
    }));

    out.push(phase(pm, "PMIC fault (PM message 0x400)", None, |pm| {
        let cpu = &mut pm.cpu;
        for bus in cpu.bus.i2c.iter_mut().take(3) {
            if let Some(dev) = bus.devices.get_mut(&0x60) {
                if dev.kind == DevKind::Mp5475 {
                    dev.regs[0x07] = 0x01;
                    dev.regs[0x08] = 0x02;
                    dev.regs[0x09] = 0x10;
                    break;
                }
            }
        }
        let ans = vec![
            ("0x400".to_string(), pm_msg(cpu, 0, 0x400, &[])),
            ("0x400 again".to_string(), pm_msg(cpu, 0, 0x400, &[])),
            ("0x63".to_string(), pm_msg(cpu, 0, 0x63, &[])),
            ("0x804".to_string(), pm_msg(cpu, 0, 0x804, &[])),
        ];
        (ms(cpu, 5.0), ans)
    }));

    out.push(phase(pm, "DDR DFS check (PM message 0x803)", None, |pm| {
        let cpu = &mut pm.cpu;
        let mut ans = vec![("0x803 on".to_string(), pm_msg(cpu, 0, 0x803, &[3, 3]))];
        let end = ms(cpu, 250.0);
        ans.push(("is_ddr_dfs_compatible".to_string(), Val::Bytes(sym_bytes(cpu, "is_ddr_dfs_compatible", 5))));
        ans.push(("0x803 off".to_string(), pm_msg(cpu, 0, 0x803, &[1, 0])));
        (end, ans)
    }));
    out
}

// --- the DPM's runtime (--runtime MS) ----------------------------------------------

/// what the AP sees of the DPM: dpm_main_tab's fields (both images lay it
/// out as CIX's DWARF says): name, offset, words, format
const DPM_OUTPUTS: [(&str, usize, usize, char); 13] = [
    ("opp_run", 0x538, 13, 'I'),
    ("opp_grant", 0x49c, 13, 'I'),
    ("exp_dxs_clk_khz", 0x278, 39, 'I'),
    ("edp_limit", 0x3e4, 8, 'I'),
    ("delivered_dxs_freq_khz", 0x50, 13, 'I'),
    ("delivered_vot_mV", 0x84, 8, 'I'),
    ("die_temp", 0xa4, 1, 'f'),
    ("util_dxs", 0xfc, 13, 'f'),
    ("power_dxs_mW", 0x1d0, 13, 'i'),
    ("power_edp_mW", 0x204, 9, 'i'),
    ("power_edp_leakage_mW", 0x228, 9, 'i'),
    ("socket_power", 0x270, 1, 'f'),
    ("ccx_power", 0x274, 1, 'f'),
];
/// the OS's level-set fast channel of big core cluster 0
pub const BIG0_FC: u32 = 0x98;
/// its SCMI performance domain
pub const BIG0: u32 = 3;
/// shared memory the DPM's runtime writes for the AP, through the hub
/// window: the power data board (DRAM 0x83bf0300) and the PCC tables
/// (0x83bf1280).  (CIX's debug CLI also polls its RTT channel, writing the
/// RTT block at 0x63bf3000; the CLI is not ported.)
const RUNTIME_PAGES: [u32; 2] = [0x63bf0000, 0x63bf1000];

/// The DPM's exported state: dpm_main_tab's fields and the die arbiter's
/// PID controller (spt_pids[3]: CIX's float state).
pub fn dpm_outputs(cpu: &mut Cpu) -> Vec<(String, Val)> {
    let mut out = Vec::new();
    let tab = sym_bytes(cpu, "dpm_main_tab", 0x720);
    for (name, off, n, fmt) in DPM_OUTPUTS {
        let w = tab.as_ref().map(|t| (0..n).map(|k| util::u32le(t, off + 4 * k)).collect::<Vec<u32>>());
        let v = match fmt {
            'I' => Val::U32s(w),
            'i' => Val::I32s(w.map(|w| w.iter().map(|x| *x as i32).collect())),
            _ => Val::F32s(w.map(|w| w.iter().map(|x| f32::from_bits(*x)).collect())),
        };
        out.push((format!("dpm_main_tab.{name}"), v));
    }
    // the die arbiter's PID: CurInput, SetPoint, SetAmend, FeedBack,
    // in_alpha, fb_scale, err, err_last, (integral left out: with Ki 0 it
    // only counts the iterations since pm_ready, which the images reach at
    // different simulated times), u_sum, input_last, Kp, Ki, Kd
    let pid = sym_bytes(cpu, "spt_pids", 240).map(|p| {
        let v: Vec<f32> = (0..14).map(|k| f32::from_bits(util::u32le(&p, 0xb4 + 4 + 4 * k))).collect();
        v[..8].iter().chain(v[9..].iter()).copied().collect()
    });
    out.push(("spt_pids[3]".to_string(), Val::F32s(pid)));
    out
}

/// phase() with the shared memory the AP reads of the DPM (final contents
/// of RUNTIME_PAGES) and the DPM's outputs after it among the answers.
fn runtime_phase(pm: &mut Pm, name: &str, action: impl FnOnce(&mut Pm) -> (String, Vec<(String, Val)>)) -> Phase {
    phase(pm, name, None, |pm| {
        let (end, mut ans) = action(pm);
        let cpu = &mut pm.cpu;
        for p in RUNTIME_PAGES {
            let bytes = (0..0x1000).map(|i| cpu.bus.mem.read(p + i, 1) as u8).collect();
            ans.push((format!("memory {p:#x}"), Val::Bytes(Some(bytes))));
        }
        ans.extend(dpm_outputs(cpu));
        (end, ans)
    })
}

/// The PM left running for ms of simulated time (run_for: time asleep goes
/// by at once), each phase from where the last ended: what pm_ready starts
/// (the DPM's performance streams: AMU, DDR and NPU counters; the DPM
/// every millisecond, its power data board for the AP; the board's
/// services); then the OS asking big cluster 0's highest level on its fast
/// channel (the DPM reads the requests); then the die at 100 C with
/// PROCHOT asserted (the DPM caps the CPU clusters: its TDP frequencies,
/// which scmi_perf and sky1soc_perf apply), and both gone.  Compared per
/// phase as the power phases are, plus the hub window's pages written and
/// the DPM's exported state (dpm_outputs).
fn runtime_suite(pm: &mut Pm, t: u32) -> Vec<Phase> {
    let mut out = Vec::new();
    let t = t as f64;
    out.push(runtime_phase(pm, &format!("DPM and streams {t} ms"), |pm| (ms(&mut pm.cpu, t), Vec::new())));

    out.push(runtime_phase(pm, &format!("big0's highest level asked, {t} ms"), |pm| {
        let cpu = &mut pm.cpu;
        let (st, w) = match soc::scmi(cpu, 0x13, 4, &[BIG0, 0], 0, 3_000_000) {
            Ok(a) => a,
            Err(e) => return (format!("stopped: {e}"), Vec::new()),
        };
        let lv = if st == Some(0) { levels_of(&w) } else { Vec::new() };
        let level = lv.last().copied().unwrap_or(0);
        ask(cpu, 0, &Ask::Fc(BIG0_FC, level));
        (ms(cpu, t), vec![("big0 levels".to_string(), Val::Levels(st, lv.len() as u32, level))])
    }));

    out.push(runtime_phase(pm, &format!("die at 100 C, PROCHOT {t} ms, then neither {t} ms"), |pm| {
        let cpu = &mut pm.cpu;
        cpu.bus.set_temps(&mut cpu.io, 100.0);
        set_raw(cpu, 30, 1);
        let end = ms(cpu, t);
        set_raw(cpu, 30, 0);
        cpu.bus.set_temps(&mut cpu.io, 40.0);
        (if end == "ran" { ms(cpu, t) } else { end }, Vec::new())
    }));
    out
}

// --- reports -----------------------------------------------------------------------

fn hexes(v: &[u64], n: usize) -> String {
    format!("[{}]", v.iter().take(n).map(|x| format!("'{x:#x}'")).collect::<Vec<_>>().join(", "))
}

/// Two lists of values from where they first differ, with their lengths.
fn from_first_difference(a: &[u64], b: &[u64]) -> String {
    let k = a.iter().zip(b.iter()).take_while(|(x, y)| x == y).count();
    if k < 6 {
        return format!("CIX {} open {}", hexes(a, 6), hexes(b, 6));
    }
    format!("{} and {} writes, the same up to write {k}: CIX {} open {}", a.len(), b.len(), hexes(&a[k..], 4), hexes(&b[k..], 4))
}

pub fn reg_diffs(ra: &Regs, rb: &Regs) -> Vec<u32> {
    let mut all: Vec<u32> = ra.keys().chain(rb.keys()).copied().collect();
    all.sort();
    all.dedup();
    all.into_iter().filter(|r| ra.get(r) != rb.get(r)).collect()
}

pub fn reg_count(ra: &Regs, rb: &Regs) -> usize {
    let mut all: Vec<u32> = ra.keys().chain(rb.keys()).copied().collect();
    all.sort();
    all.dedup();
    all.len()
}

pub fn print_reg_diffs(ra: &Regs, rb: &Regs, diffs: &[u32], limit: Option<usize>) {
    let empty = Vec::new();
    for r in diffs.iter().take(limit.unwrap_or(usize::MAX)) {
        println!("  {r:#010x}: {}", from_first_difference(ra.get(r).unwrap_or(&empty), rb.get(r).unwrap_or(&empty)));
    }
}

/// An answer as compared: the firmware version's build date and time
/// (message 1, words 3 and 4) are each image's own.
fn comparable(q: &str, v: &Val) -> Val {
    if let Val::Msg(st, w) = v {
        if w.len() == 5 && (q.contains(", 0x1, ") || q.contains("message 0x1[")) {
            return Val::Msg(*st, w[..3].to_vec());
        }
        // a message refused: its payload is what the buffer held
        if *st != 0 && w.len() > 2 && !q.contains("0x800") {
            return Val::Msg(*st, w[..2].to_vec());
        }
    }
    v.clone()
}

fn fmt_se(v: &[u32]) -> String {
    v.iter().map(|x| format!("{x:#x}")).collect::<Vec<_>>().join(" ")
}

fn fmt_events(v: &[Event]) -> String {
    format!("[{}]", v.iter().map(|e| e.to_string()).collect::<Vec<_>>().join(", "))
}

/// A difference between the images that is known and meant, or that is
/// CIX's image failing: where (the phase's name and the question, register
/// or part of the phase have these texts in them), what each image gives
/// (texts in them), and why it is not counted.
pub struct Expected {
    pub phase: &'static str,
    /// not in the phases whose name has this text (when it is not empty)
    pub except: &'static str,
    pub what: &'static str,
    pub cix: &'static str,
    pub open: &'static str,
    pub why: &'static str,
}

/// A register written on a timer (a strobe, a fan's duty): both images
/// write the same sequence, but a phase cuts it at an instant that is not
/// the same point of the period in both, so one may have up to `delta`
/// writes more at either end.
pub struct Periodic {
    pub lo: u32,
    pub hi: u32,
    pub delta: usize,
}

fn expected<'a>(table: &'a [Expected], phase: &str, what: &str, cix: &str, open: &str) -> Option<&'a Expected> {
    table.iter().find(|e| {
        phase.contains(e.phase)
            && (e.except.is_empty() || !phase.contains(e.except))
            && what.contains(e.what)
            && cix.contains(e.cix)
            && open.contains(e.open)
    })
}

/// Two views of one sequence: after leaving out up to delta writes at the
/// start of either, one is the other's beginning, and at most delta longer.
fn windows<T: PartialEq>(a: &[T], b: &[T], delta: usize) -> bool {
    (0..=delta.min(a.len())).any(|da| {
        (0..=delta.min(b.len())).any(|db| {
            let (x, y) = (&a[da..], &b[db..]);
            let n = x.len().min(y.len());
            x[..n] == y[..n] && x.len().abs_diff(y.len()) <= delta
        })
    })
}

fn periodic(table: &[Periodic], addr: u32, a: &[u64], b: &[u64]) -> bool {
    table.iter().any(|p| p.lo <= addr && addr < p.hi && windows(a, b, p.delta))
}

/// A device the PM reads on a timer of its own (MS-R1's TMP75, every
/// 200 ms): the two images' timers do not start at the same instant after
/// boot, so a phase may hold one read more or less at either end in one
/// image; `delta` transfers (one read's) are allowed for.
const POLLED: &[(&str, usize)] = &[("i2c3:0x4d", 6)];

/// The transfers differ only by a polled device's reads at the phase's ends.
fn polled_only(a: &[String], b: &[String]) -> bool {
    let is = |l: &&String| POLLED.iter().any(|(d, _)| l.starts_with(d));
    let rest = |log: &[String]| log.iter().filter(|l| !is(l)).cloned().collect::<Vec<_>>();
    if by_bus(&rest(a)) != by_bus(&rest(b)) {
        return false;
    }
    POLLED.iter().all(|(d, delta)| {
        let of = |log: &[String]| log.iter().filter(|l| l.starts_with(d)).cloned().collect::<Vec<_>>();
        windows(&of(a), &of(b), *delta)
    })
}

/// I2C transfers by bus ("i2c1:0x60 W ..." is bus i2c1; a refused address
/// has none), each bus's in order.
fn by_bus(log: &[String]) -> BTreeMap<String, Vec<&String>> {
    let mut m: BTreeMap<String, Vec<&String>> = BTreeMap::new();
    for l in log {
        let bus = if l.starts_with("i2c") { l.split(':').next().unwrap_or("") } else { "" };
        m.entry(bus.to_string()).or_default().push(l);
    }
    m
}

/// Print the phases (one run: CIX's; two: compared).  Returns the number
/// of differences.
pub fn report_phases(key: &str, phases: &[Option<&Vec<Phase>>], verbose: bool) -> usize {
    report_phases_expecting(key, phases, verbose, super::full::EXPECTED, super::full::PERIODIC)
}

/// report_phases() with the differences that are expected set apart.
pub fn report_phases_expecting(key: &str, phases: &[Option<&Vec<Phase>>], verbose: bool, table: &[Expected], timers: &[Periodic]) -> usize {
    let mut bad = 0;
    let mut seen: BTreeMap<&str, usize> = BTreeMap::new();
    if phases.iter().any(|p| p.map_or(true, |p| p.is_empty())) {
        println!("{key} phases: did not run (no idle after boot)");
        return 1;
    }
    let pa_all = phases[0].unwrap();
    for (i, pa) in pa_all.iter().enumerate() {
        let line = format!(
            "{key}: {}: {} after {} instructions, {} registers, {} I2C, SE {}, events {}",
            pa.name,
            pa.end,
            pa.insns,
            pa.regs.len(),
            pa.i2c.len(),
            fmt_se(&pa.se),
            fmt_events(&pa.events)
        );
        if phases.len() < 2 {
            println!("{line}");
            for (q, ans) in &pa.answers {
                println!("  {q}: {ans}");
            }
            continue;
        }
        let Some(pb) = phases[1].unwrap().get(i) else {
            println!("FAIL {line}\n     open: phase missing");
            bad += 1;
            continue;
        };
        let (ra, rb) = (&pa.regs, &pb.regs);
        let empty: Vec<u64> = Vec::new();
        let mut known: BTreeMap<&str, usize> = BTreeMap::new();
        let mut diffs = reg_diffs(ra, rb);
        diffs.retain(|r| {
            let (a, b) = (ra.get(r).unwrap_or(&empty), rb.get(r).unwrap_or(&empty));
            if periodic(timers, *r, a, b) {
                *known.entry("written on a timer: as often within a period").or_insert(0) += 1;
                return false;
            }
            match expected(table, &pa.name, &format!("register {r:#010x}"), &hexes(a, 6), &hexes(b, 6)) {
                Some(e) => {
                    *known.entry(e.why).or_insert(0) += 1;
                    false
                }
                None => true,
            }
        });
        let mut ans_bad: Vec<_> = pa
            .answers
            .iter()
            .zip(pb.answers.iter())
            .filter(|((q, x), (_, y))| comparable(q, x) != comparable(q, y))
            .map(|((q, x), (_, y))| (q, x, y))
            .collect();
        ans_bad.retain(|(q, x, y)| match expected(table, &pa.name, q, &x.to_string(), &y.to_string()) {
            Some(e) => {
                *known.entry(e.why).or_insert(0) += 1;
                false
            }
            None => true,
        });
        // I2C: each bus's transfers in order (a sensor read on a timer
        // falls between another bus's transfers at a different point)
        let mut same = [pa.end == pb.end, by_bus(&pa.i2c) == by_bus(&pb.i2c), pa.se == pb.se, pa.events == pb.events, pa.doorbells == pb.doorbells];
        let parts = [
            ("end", pa.end.clone(), pb.end.clone()),
            ("I2C", format!("{} transfers", pa.i2c.len()), format!("{} transfers", pb.i2c.len())),
            ("SE", fmt_se(&pa.se), fmt_se(&pb.se)),
            ("events", fmt_events(&pa.events), fmt_events(&pb.events)),
            ("doorbells", format!("{:?}", pa.doorbells), format!("{:?}", pb.doorbells)),
        ];
        if !same[1] && polled_only(&pa.i2c, &pb.i2c) {
            *known.entry("read on a timer: as often within a period").or_insert(0) += 1;
            same[1] = true;
        }
        for (k, (what, x, y)) in parts.iter().enumerate() {
            if !same[k] {
                if let Some(e) = expected(table, &pa.name, what, x, y) {
                    *known.entry(e.why).or_insert(0) += 1;
                    same[k] = true;
                }
            }
        }
        let n = diffs.len() + ans_bad.len() + same.iter().filter(|s| !**s).count();
        bad += n;
        for (why, k) in &known {
            *seen.entry(why).or_insert(0) += k;
        }
        println!("{} {line}", if n == 0 { "ok  " } else { "FAIL" });
        println!(
            "     open: {} after {} instructions ({:.1} ms, CIX {:.1} ms); {} registers compared, {} differ; I2C {} ({}/{}); SE {}; events {}; \
             doorbells {} ({}/{}); {} of {} answers the same",
            pb.end,
            pb.insns,
            pb.ms,
            pa.ms,
            reg_count(ra, rb),
            diffs.len(),
            if same[1] { "same" } else { "differ" },
            pa.i2c.len(),
            pb.i2c.len(),
            if same[2] { "same" } else { "differs" },
            if same[3] { "same" } else { "differ" },
            if same[4] { "same" } else { "differ" },
            pa.doorbells.len(),
            pb.doorbells.len(),
            pa.answers.len() - ans_bad.len(),
            pa.answers.len()
        );
        print_reg_diffs(ra, rb, &diffs, if verbose { None } else { Some(30) });
        for (q, x, y) in &ans_bad {
            match (x, y) {
                (Val::Bytes(Some(x)), Val::Bytes(Some(y))) if x.len() == y.len() => {
                    let d: Vec<usize> = (0..x.len()).filter(|i| x[*i] != y[*i]).collect();
                    let at = d[0] & !3;
                    let end = (at + 16).min(x.len());
                    println!(
                        "  {q}: {} bytes differ, first at +{:#x}: CIX {} open {}",
                        d.len(),
                        d[0],
                        util::hex(&x[at..end]),
                        util::hex(&y[at..end])
                    );
                }
                _ => println!("  answer {q}: CIX {x} open {y}"),
            }
        }
        if !same[1] {
            let (ba, bb) = (by_bus(&pa.i2c), by_bus(&pb.i2c));
            let buses: std::collections::BTreeSet<&String> = ba.keys().chain(bb.keys()).collect();
            let none = Vec::new();
            for bus in buses {
                let (x, y) = (ba.get(bus).unwrap_or(&none), bb.get(bus).unwrap_or(&none));
                if let Some(k) = (0..x.len().max(y.len())).find(|k| x.get(*k) != y.get(*k)) {
                    let s = |v: Option<&&String>| v.map_or("None".to_string(), |t| (*t).clone());
                    println!("  I2C {bus} transfer {k} ({} and {}): CIX {} open {}", x.len(), y.len(), s(x.get(k)), s(y.get(k)));
                }
            }
        }
        if !same[2] || !same[3] || !same[0] {
            println!(
                "  SE CIX [{}] open [{}]; events CIX {} open {}; end CIX {} open {}",
                fmt_se(&pa.se),
                fmt_se(&pb.se),
                fmt_events(&pa.events),
                fmt_events(&pb.events),
                pa.end,
                pb.end
            );
        }
        if !same[4] {
            println!("  doorbells CIX {:?} open {:?}", pa.doorbells, pb.doorbells);
        }
        for (why, k) in &known {
            println!("  expected ({k}): {why}");
        }
        if verbose {
            for note in &pa.notes {
                println!("  note: {note}");
            }
        }
    }
    if key == "full" {
        let total: usize = seen.values().sum();
        println!("{key}: {bad} differences, {total} expected ones in {} kinds", seen.len());
    }
    bad
}

fn ask_line(chan: usize, a: &Ask, ans: &Val) -> Option<String> {
    Some(match a {
        Ask::Read(off) => format!("ch{chan} read {:#x}: {ans}", soc::MBOX + off),
        Ask::Scmi(proto, msg, args) => {
            let a = if args.is_empty() { String::new() } else { format!("{args:?}") };
            format!("ch{chan} {proto:#04x}/{msg}{a}: {ans}")
        }
        Ask::ScmiLen(proto, msg, args, len) => format!("ch{chan} {proto:#04x}/{msg}{args:?} length {len}: {ans}"),
        Ask::ScmiIntr(proto, msg, args) => format!("ch{chan} {proto:#04x}/{msg}{args:?} by interrupt: {ans}"),
        _ => return None,
    })
}

/// Compare (or print) the SCMI answers of the runs; the number that differ.
pub fn report_asked(answers: &[&Asked], verbose: bool) -> usize {
    let mut bad = 0;
    let mut same_n = 0;
    let mut n = 0;
    for (i, ((chan, a), ans)) in answers[0].iter().enumerate() {
        let Some(line) = ask_line(*chan, a, ans) else { continue };
        n += 1;
        if answers.len() == 2 {
            let other = answers[1].get(i).map_or(Val::Nothing, |x| x.1.clone());
            let same = other == *ans;
            same_n += same as usize;
            bad += !same as usize;
            if verbose || !same {
                println!("{} {line}{}", if same { "ok  " } else { "FAIL" }, if same { String::new() } else { format!(" (open {other})") });
            }
        } else {
            println!("{line}");
        }
    }
    if answers.len() == 2 {
        println!("{same_n} of {n} SCMI answers the same");
    } else {
        println!("{n} SCMI commands");
    }
    bad
}

pub fn main(args: &[String]) -> i32 {
    let a = match Args::parse(args, &["--cix", "--board", "--set", "--runtime", "--cov", "--only"]) {
        Ok(a) => a,
        Err(e) => {
            eprintln!("{e}");
            return 2;
        }
    };
    if let Err(e) = a.check(&["--scmi", "--power", "--services", "--full", "-v", "--no-cache", "--order", "--audit"]) {
        eprintln!("{e}");
        return 2;
    }
    match main_run(&a) {
        Ok(bad) => (bad != 0) as i32,
        Err(e) => {
            eprintln!("pm-test: {e}");
            2
        }
    }
}

fn main_run(a: &Args) -> Result<usize, String> {
    let mut opts = Opts::default();
    if let Some(b) = a.opt("--board") {
        opts.board = Some(board_config(b, &a.all("--set"))?);
    }
    opts.services = a.flag("--services");
    opts.runtime = a.int("--runtime").unwrap_or(0) as u32;
    opts.full = a.flag("--full");
    opts.audit = a.flag("--audit");
    opts.only = a.opt("--only").map(|s| s.to_string());
    let (scmi, power, verbose) = (a.flag("--scmi"), a.flag("--power"), a.flag("-v"));
    let cix_elf = match a.opt("--cix") {
        Some(c) => c.to_string(),
        None => util::cix_debug(CIX_PM)?,
    };
    let open_elf = a.pos.first();
    let mut bad = 0;
    let reference = run(&cix_elf, None, true, power, &opts)?;
    let mut runs = vec![reference];
    if let Some(elf) = open_elf {
        let qs = if scmi || opts.runtime != 0 { runs[0].scmi.clone() } else { None };
        runs.push(run(elf, qs.as_ref(), false, power, &opts)?);
    }
    if opts.full {
        // the --full phases: both images asked in step
        let mut bases: Vec<Pm> = runs.iter_mut().filter_map(|r| r.full_base.take()).collect();
        if bases.len() == runs.len() {
            let mut covs: Vec<Vec<(u32, u32)>> = runs.iter().map(|_| Vec::new()).collect();
            let phases = super::full::suite(&mut bases, &opts, &mut covs);
            for ((r, p), c) in runs.iter_mut().zip(phases).zip(covs) {
                r.full = Some(p);
                r.covered.extend(c);
            }
        }
    }
    for r in &runs {
        let ok = r.why.as_deref() == Some("idle") && r.se.last() == Some(&0x5a5c);
        bad += !ok as usize;
        println!(
            "{} {}: {} after {} instructions, {} registers written, {} I2C transfers, SE handshake {}",
            if ok { "ok  " } else { "FAIL" },
            r.name,
            r.why.as_deref().unwrap_or("None"),
            r.insns,
            r.regs.len(),
            r.i2c.len(),
            fmt_se(&r.se)
        );
        if r.why.as_deref() != Some("idle") {
            println!("     {}", r.backtrace);
        }
    }
    if runs.len() == 2 {
        let (ra, rb) = (&runs[0].regs, &runs[1].regs);
        let diffs = reg_diffs(ra, rb);
        print_reg_diffs(ra, rb, &diffs, if verbose { None } else { Some(40) });
        println!("{} registers compared, {} differ", reg_count(ra, rb), diffs.len());
        let (ma, mb) = (&runs[0].memory, &runs[1].memory);
        let mut pages: Vec<u32> = ma.keys().chain(mb.keys()).copied().collect();
        pages.sort();
        pages.dedup();
        let zero = vec![0u8; 0x1000];
        let (mut mdiff, mut by_design) = (0, 0);
        for page in &pages {
            let (pa, pb) = (ma.get(page).unwrap_or(&zero), mb.get(page).unwrap_or(&zero));
            if pa != pb {
                let d: Vec<usize> = (0..0x1000).filter(|i| pa[*i] != pb[*i]).collect();
                let own = OWN_PAGES.iter().find(|p| p.0 == *page);
                match own {
                    Some(_) => by_design += 1,
                    None => mdiff += 1,
                }
                if verbose || mdiff <= 20 {
                    println!(
                        "  memory {page:#010x}: {} bytes differ, first at {:#x}{}",
                        d.len(),
                        page + d[0] as u32,
                        own.map_or(String::new(), |p| format!(" (expected: {})", p.1))
                    );
                }
            }
        }
        println!("{} pages of shared memory compared, {mdiff} differ ({by_design} more as expected)", pages.len());
        bad += mdiff;
        let (ia, ib) = (&runs[0].i2c, &runs[1].i2c);
        println!("I2C: {} ({} transfers)", if ia == ib { "same" } else { "differ" }, ia.len());
        bad += diffs.len() + (ia != ib) as usize;
        // the registers the boot read: one image reading a register the
        // other never looks at
        let (xa, xb) = (&runs[0].reads, &runs[1].reads);
        let only_a: Vec<_> = xa.iter().filter(|r| !xb.contains(r)).collect();
        let only_b: Vec<_> = xb.iter().filter(|r| !xa.contains(r)).collect();
        println!("registers read at boot: CIX {}, open {}; only CIX {}, only open {}", xa.len(), xb.len(), only_a.len(), only_b.len());
        if verbose {
            for r in &only_a {
                println!("  read only by CIX's: {r:#010x}");
            }
            for r in &only_b {
                println!("  read only by the open one: {r:#010x}");
            }
        }
    }
    if scmi {
        let answers: Vec<&Asked> = runs.iter().filter_map(|r| r.scmi.as_ref()).collect();
        if answers.len() == runs.len() {
            bad += report_asked(&answers, verbose);
        } else {
            println!("SCMI: not asked (no idle after boot)");
            bad += 1;
        }
        if runs.len() == 2 {
            let (xa, xb) = (&runs[0].i2c_scmi, &runs[1].i2c_scmi);
            println!("I2C transfers during SCMI: {} ({})", if xa == xb { "same" } else { "differ" }, xa.len());
            bad += (xa != xb) as usize;
            // (the DSU's PLL apart: written differently by design)
            let other = |x: &Vec<(u32, u64)>| x.iter().filter(|w| !(DSU_PLL.0..DSU_PLL.1).contains(&w.0)).copied().collect::<Vec<_>>();
            let dsu = |x: &Vec<(u32, u64)>| x.len() - other(x).len();
            let (xa, xb) = (&other(&runs[0].cgc_scmi), &other(&runs[1].cgc_scmi));
            println!(
                "clock generator writes during SCMI: {} ({}; the DSU's PLL, by design: CIX {}, open {})",
                if xa == xb { "same" } else { "differ" },
                xa.len(),
                dsu(&runs[0].cgc_scmi),
                dsu(&runs[1].cgc_scmi)
            );
            bad += (xa != xb) as usize;
            if xa != xb && verbose {
                for k in 0..xa.len().max(xb.len()) {
                    if xa.get(k) != xb.get(k) {
                        println!("  clock generator write {k}: CIX {:x?} open {:x?}", xa.get(k), xb.get(k));
                    }
                }
            }
        }
    }
    let key = |f: fn(&Run) -> Option<&Vec<Phase>>| -> Vec<Option<&Vec<Phase>>> { runs.iter().map(f).collect() };
    if power {
        bad += report_phases("power", &key(|r| r.power.as_ref()), verbose);
    }
    if opts.services {
        bad += report_phases("services", &key(|r| r.services.as_ref()), verbose);
    }
    if opts.runtime != 0 {
        bad += report_phases("runtime", &key(|r| r.runtime.as_ref()), verbose);
    }
    if opts.full {
        bad += report_phases_expecting("full", &key(|r| r.full.as_ref()), verbose, super::full::EXPECTED, super::full::PERIODIC);
    }
    if let Some(path) = a.opt("--cov") {
        for r in &runs {
            crate::cov::save(path, &r.name, &r.covered)?;
        }
    }
    if runs.len() == 2 || bad != 0 {
        if runs.len() == 2 {
            println!("{bad} differences");
        } else {
            println!("CIX's image did not boot in the model");
        }
    }
    Ok(bad)
}
