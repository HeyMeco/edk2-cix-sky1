// SPDX-License-Identifier: BSD-2-Clause-Patent
//! Faults on the PM's I2C buses, open CSU_PM firmware only.
//!
//!   sky1emu pm-faults OPEN.elf [-v]
//!
//! The model's I2C controllers can be told to hang (Fault::Stuck: the bus
//! held, no transfer starts or ends).  On the open firmware's model of the
//! SoC (the default board configuration, then the MS-R1's), this checks
//! that a stuck bus costs only the work that needs that bus:
//!
//! pmic (O6N): the bus of big cluster 0's rail hangs while the OS asks for
//!   the cluster's highest level (a voltage raise first).  Meanwhile SCMI is
//!   answered at once on another channel, the DPM sees the die at 100 C, the
//!   fans speed up; the voltage is given up within the bus's time-outs (no
//!   frequency raised without it); the bus is reset and marked down, its
//!   transfers then fail at once; another cluster's rail on a healthy bus
//!   still changes.  Once the bus is free again the level is set.
//!
//! sensor (MS-R1): the bus of the board's TMP75s hangs; voltage changes go
//!   on (their worker and bus are not the sensors') and so does the DPM.
//!
//! raise: a frequency never goes up on a voltage that failed.  Big cluster 0
//!   asked for its highest level by message and by its fast channel (both
//!   sky1soc_perf's) while its PMIC's bus fails: NACKs; loses the voltage
//!   writes (they reach the regulator, the transfer fails: the CIX driver's
//!   "failed, read back as set"); dies after the page is selected.  On the
//!   MP5475 (O6N), the MP2845 (Orange Pi 6, bucks shared) and an AU4683 (the
//!   Orange Pi 6's configuration with AU4683s, modelled here).  Its PLL is
//!   not written and keeps its registers; once the bus is healthy the same
//!   request writes the voltage, then the PLL.  Then lowers whose voltage
//!   writes were lost (the rail lower than the firmware was told), each
//!   followed by a raise of the cluster or, on a shared buck, of big cluster
//!   1: no PLL write before a voltage write.
//!
//! two owners: big cluster 0 asked by fast channel, message, fast channel
//!   (and fast channel, message): after each the rail holds the voltage the
//!   PLL's setting needs, and the level asked for is the PLL's.
//!
//! unsure: a voltage write that failed (NACKed, or lost: set but failed) is
//!   written again, as recorded, by the next pass before anything else, and
//!   the regulator holds it.  A resume while big0's PMIC NACKs: big0's PLL
//!   at its lowest level's setting and LEVEL_GET says so; the bus healthy, a
//!   raise writes the voltage first.  A boot with big0's PMIC losing every
//!   voltage write (dvfs's boot OPP given up): the first raise writes the
//!   voltage before the PLL.
//!
//! The stacks' high-water marks are checked at the end of each (the tasks'
//! stacks are filled with 0xa5 when created).  A check that fails prints
//! FAIL; the exit status is the number of them.

use super::soc::{self, BusFault, Cpu, Fault, FaultMode, VOUT_LEN};
use super::test::{self, Ask, Opts, Val};
use crate::util::{self, Args};
use std::collections::BTreeMap;

const GPU: u32 = 0; // SCMI performance domains
const LITTLE: u32 = 2;
const BIG0: u32 = 3;
const BIG1: u32 = 4;
/// bytes a stack must have left
const STACK_MARGIN: u32 = 256;
const PLL_BLOCK: u32 = 0x100;
/// big0's PMIC on the O6N
const BOOT_LOST_BUS: usize = 1;

pub struct T {
    pub failures: u32,
    pub verbose: bool,
    pub elf: String,
    pub covered: Vec<(u32, u32)>,
}

type Answer = Option<(Option<i32>, Vec<u32>)>;

impl T {
    pub fn check(&mut self, ok: bool, what: &str) {
        println!("{} {what}", if ok { "ok  " } else { "FAIL" });
        self.failures += !ok as u32;
    }

    fn done(&mut self, cpu: &Cpu) {
        self.covered.extend(cpu.covered());
    }
}

pub fn now_ms(cpu: &Cpu) -> f64 {
    cpu.io.ccount as f64 * 1000.0 / soc::CORE_HZ as f64
}

#[derive(Clone, Copy, Debug, PartialEq, Default)]
pub struct I2cStats {
    pub timeouts: u32,
    pub recoveries: u32,
    pub downs: u32,
    pub ups: u32,
    pub fast_fails: u32,
}

pub fn i2c_stats(cpu: &mut Cpu, bus: usize) -> I2cStats {
    let a = cpu.all_syms.get("i2c_stats").map_or(0, |s| s.0);
    let b = cpu.read_bytes(a + 20 * bus as u32, 20);
    let w = |i: usize| util::u32le(&b, 4 * i);
    I2cStats { timeouts: w(0), recoveries: w(1), downs: w(2), ups: w(3), fast_fails: w(4) }
}

pub fn die_temp(cpu: &mut Cpu) -> f32 {
    test::sym_bytes(cpu, "dpm_main_tab", 0x720).map_or(0.0, |t| f32::from_bits(util::u32le(&t, 0xa4)))
}

// --- SCMI without waiting for the answer -------------------------------------------

pub fn send(cpu: &mut Cpu, mbox: usize, protocol: u32, msg: u32, args: &[u32]) {
    let base = soc::MBOX + mbox as u32 * 0x10000;
    let m = &mut cpu.bus.mem;
    assert!(m.read32(base + soc::SMT_STATUS) & 1 != 0, "channel {mbox} busy");
    m.write32(base + soc::SMT_STATUS, m.read32(base + soc::SMT_STATUS) & !1);
    m.write32(base + soc::SMT_HEADER, (msg & 0xff) | (protocol & 0xff) << 10);
    for (i, a) in args.iter().enumerate() {
        m.write32(base + soc::SMT_PAYLOAD + 4 * i as u32, *a);
    }
    m.write32(base + soc::SMT_LEN, 4 + 4 * args.len() as u32);
    m.write32(base + soc::SMT_FLAGS, 0);
    cpu.bus.ring(&mut cpu.io, mbox, 1);
}

/// None while the PM has not answered, else (status, words).
pub fn answer(cpu: &Cpu, mbox: usize) -> Answer {
    let base = soc::MBOX + mbox as u32 * 0x10000;
    let m = &cpu.bus.mem;
    if m.read32(base + soc::SMT_STATUS) & 1 == 0 {
        return None;
    }
    let n = ((m.read32(base + soc::SMT_LEN) as i64 - 4).div_euclid(4)).clamp(0, 25) as u32;
    let w: Vec<u32> = (0..n).map(|i| m.read32(base + soc::SMT_PAYLOAD + 4 * i)).collect();
    Some((w.first().map(|x| *x as i32), w.iter().skip(1).copied().collect()))
}

/// Run until mbox is answered: (elapsed ms, answer), or (None, None).
pub fn wait_answer(cpu: &mut Cpu, mbox: usize, max_ms: f64) -> (Option<f64>, Answer) {
    let t0 = now_ms(cpu);
    while now_ms(cpu) - t0 < max_ms {
        if let Some(a) = answer(cpu, mbox) {
            return (Some(now_ms(cpu) - t0), Some(a));
        }
        if let Some(why) = soc::run_for(cpu, 0.25) {
            println!("     PM stopped: {why} at {}", cpu.func(cpu.pc));
            return (None, None);
        }
    }
    match answer(cpu, mbox) {
        Some(a) => (Some(now_ms(cpu) - t0), Some(a)),
        None => (None, None),
    }
}

pub fn scmi(cpu: &mut Cpu, mbox: usize, protocol: u32, msg: u32, args: &[u32], max_ms: f64) -> (Option<f64>, Answer) {
    send(cpu, mbox, protocol, msg, args);
    wait_answer(cpu, mbox, max_ms)
}

pub fn levels(cpu: &mut Cpu, d: u32) -> Vec<u32> {
    match scmi(cpu, 0, 0x13, 4, &[d, 0], 50.0).1 {
        Some((Some(0), w)) => test::levels_of(&w),
        _ => Vec::new(),
    }
}

pub fn level(cpu: &mut Cpu, d: u32) -> Option<u32> {
    match scmi(cpu, 0, 0x13, 8, &[d], 50.0).1 {
        Some((Some(0), w)) => w.first().copied(),
        _ => None,
    }
}

fn i2c_buses(cpu: &Cpu, since: usize) -> Vec<String> {
    let mut v: Vec<String> =
        cpu.bus.i2c_log[since..].iter().filter(|l| l.what.starts_with("i2c")).map(|l| l.what.split(':').next().unwrap().to_string()).collect();
    v.sort();
    v.dedup();
    v
}

fn bus_no(name: &str) -> usize {
    name[3..].parse().unwrap_or(0)
}

/// Level lv on domain d (OS channel); (ms, status, buses written).
fn set_level(cpu: &mut Cpu, d: u32, lv: u32, max_ms: f64) -> (Option<f64>, Option<i32>, Vec<String>) {
    let n = cpu.bus.i2c_log.len();
    let (t, a) = scmi(cpu, 0, 0x13, 7, &[d, lv], max_ms);
    soc::run_for(cpu, 2.0);
    (t, a.and_then(|a| a.0), i2c_buses(cpu, n))
}

/// Run until bus's stats stop changing for quiet_ms: when that was.
fn settled(cpu: &mut Cpu, bus: usize) -> f64 {
    let (quiet_ms, max_ms) = (25.0, 1000.0);
    let t0 = now_ms(cpu);
    let (mut last, mut since) = (i2c_stats(cpu, bus), now_ms(cpu));
    while now_ms(cpu) - t0 < max_ms {
        soc::run_for(cpu, 0.5);
        let s = i2c_stats(cpu, bus);
        if s != last {
            last = s;
            since = now_ms(cpu);
        } else if now_ms(cpu) - since >= quiet_ms {
            break;
        }
    }
    since
}

/// The tasks' stacks: free bytes below their deepest use.
fn stacks(t: &mut T, cpu: &mut Cpu, name: &str) {
    let sym = |cpu: &Cpu, n: &str| cpu.all_syms.get(n).copied().unwrap_or((0, 0));
    let idle = sym(cpu, "idle_stack");
    let mut regions = vec![("scp", sym(cpu, "_stack_bottom").0, sym(cpu, "_scp_stack_top").0), ("idle", idle.0, idle.0 + idle.1)];
    let (wa, wsize) = sym(cpu, "workers");
    let each = wsize / 2;
    let stack_size = 1024 * 4;
    for (k, w) in ["pmic", "sensor"].iter().enumerate() {
        let end = wa + each * (k as u32 + 1);
        regions.push((w, end - stack_size, end));
    }
    for (task, lo, hi) in regions {
        let mut free = 0;
        while lo + free < hi && cpu.read(lo + free, 4) == 0xa5a5a5a5 {
            free += 4;
        }
        let size = hi - lo;
        t.check(free >= STACK_MARGIN, &format!("{name}: {task} stack: {} of {size} bytes used", size - free));
    }
}

fn fmt_t(t: Option<f64>) -> String {
    t.map_or("never".to_string(), |t| format!("{t:.2} ms"))
}

fn fmt_st(st: Option<i32>) -> String {
    st.map_or("None".to_string(), |s| s.to_string())
}

fn fmt_buses(b: &[String]) -> String {
    format!("[{}]", b.iter().map(|x| format!("'{x}'")).collect::<Vec<_>>().join(", "))
}

// --- the scenarios ------------------------------------------------------------------

fn boot_opts(t: &mut T, opts: &Opts, label: &str) -> Cpu {
    let (pm, why, _) = test::boot(&t.elf, opts).unwrap_or_else(|e| panic!("{e}"));
    let mut cpu = pm.cpu;
    t.check(why.as_deref() == Some("idle"), &format!("boot{label}: {}", why.as_deref().unwrap_or("None")));
    soc::run_for(&mut cpu, 150.0); // pm_ready's services
    cpu
}

pub fn boot(t: &mut T, board: Option<&str>) -> Cpu {
    let opts = Opts { board: board.map(|b| test::board_config(b, &[]).unwrap_or_else(|e| panic!("{e}"))), ..Default::default() };
    boot_opts(t, &opts, &board.map_or(String::new(), |b| format!(" ({b})")))
}

fn pmic_scenario(t: &mut T, board: &str) {
    let mut cpu = boot(t, Some(board));
    let cpu = &mut cpu;
    let lv: BTreeMap<u32, Vec<u32>> = [BIG0, LITTLE, GPU].iter().map(|d| (*d, levels(cpu, *d))).collect();
    if lv.values().any(|l| l.is_empty()) {
        t.check(false, &format!("pmic: levels of the domains {lv:?}"));
        return;
    }
    // where each domain's rail is: its lowest level, then its highest
    let mut rail: BTreeMap<u32, Vec<String>> = BTreeMap::new();
    let mut last_t = None;
    for d in [BIG0, LITTLE, GPU] {
        let l = &lv[&d];
        set_level(cpu, d, l[0], 50.0);
        let (tt, st, buses) = set_level(cpu, d, l[l.len() - 1], 50.0);
        last_t = tt;
        if t.verbose {
            println!("     domain {d}: highest level {} in {}, status {}, I2C {}", l[l.len() - 1], fmt_t(tt), fmt_st(st), fmt_buses(&buses));
        }
        rail.insert(d, buses);
        set_level(cpu, d, l[0], 50.0);
    }
    if rail[&BIG0].is_empty() {
        t.check(false, "pmic: big0's highest level writes no PMIC");
        return;
    }
    let bus = bus_no(&rail[&BIG0][0]);
    let other = [LITTLE, GPU].into_iter().find(|d| !rail[d].is_empty() && rail[d] != rail[&BIG0]);
    t.check(true, &format!("pmic: big0's rail on I2C {bus}; healthy level set in {}", fmt_t(last_t)));

    let big = &lv[&BIG0];
    let fan0 = cpu.bus.fan_speed(0);
    cpu.bus.i2c[bus].fault = Some(Fault::Stuck);
    let before = i2c_stats(cpu, bus);
    let t0 = now_ms(cpu);
    send(cpu, 0, 0x13, 7, &[BIG0, big[big.len() - 1]]);
    // SCMI on TF-A's channel while the PMIC worker waits on the stuck bus
    soc::run_for(cpu, 0.5);
    let (tt, a) = scmi(cpu, 2, 0x10, 0, &[], 50.0);
    t.check(
        tt.map_or(false, |x| x < 2.0) && a.as_ref().map_or(false, |a| a.0 == Some(0)),
        &format!("pmic: bus {bus} stuck: SCMI base version on channel 2 answered in {}", fmt_t(tt)),
    );
    // the DPM and the PVT polling see the die heat up; the fans react
    cpu.bus.set_temps(&mut cpu.io, 100.0);
    soc::run_for(cpu, 20.0);
    let dt = die_temp(cpu);
    t.check(dt > 90.0, &format!("pmic: bus {bus} stuck: the DPM's die temperature {dt:.1} C after 20 ms at 100 C"));
    let (_, a) = wait_answer(cpu, 0, 500.0);
    // (scmi_perf answers a level set before DVFS has done it, as CIX's)
    t.check(a.is_some(), &format!("pmic: big0's level set answered: {}", a.as_ref().map_or("never".to_string(), |a| format!("status {}", fmt_st(a.0)))));
    let took = settled(cpu, bus) - t0;
    t.check(took < 100.0, &format!("pmic: the PMIC worker gave the voltage up {took:.1} ms after the request"));
    let cur = level(cpu, BIG0);
    t.check(cur == Some(big[0]), &format!("pmic: big0 stays at level {} (lowest {})", cur.map_or("None".into(), |c| c.to_string()), big[0]));
    let s = i2c_stats(cpu, bus);
    let d = I2cStats {
        timeouts: s.timeouts - before.timeouts,
        recoveries: s.recoveries - before.recoveries,
        downs: s.downs - before.downs,
        ups: s.ups - before.ups,
        fast_fails: s.fast_fails - before.fast_fails,
    };
    t.check(
        d.timeouts >= 3 && d.recoveries >= 3 && d.downs >= 1,
        &format!("pmic: bus {bus}: {} time-outs, {} resets, down {} times, {} transfers failed at once", d.timeouts, d.recoveries, d.downs, d.fast_fails),
    );
    if let Some(o) = other {
        let l = &lv[&o];
        let (tt, st, buses) = set_level(cpu, o, l[l.len() - 1], 50.0);
        t.check(
            st == Some(0) && tt.map_or(false, |x| x < 5.0),
            &format!("pmic: domain {o} (rail on {}) set meanwhile: status {} in {}, I2C {}", fmt_buses(&rail[&o]), fmt_st(st), fmt_t(tt), fmt_buses(&buses)),
        );
        set_level(cpu, o, l[0], 50.0);
    }
    // fans: their control runs on its own period
    soc::run_for(cpu, 1500.0);
    let fan1 = cpu.bus.fan_speed(0);
    t.check(fan1 > fan0, &format!("pmic: bus {bus} stuck: fan 0 from {fan0} to {fan1} rpm at 100 C"));
    // the bus free again: after its back-off the level is set
    cpu.bus.i2c[bus].fault = None;
    cpu.bus.set_temps(&mut cpu.io, 40.0);
    soc::run_for(cpu, 2100.0);
    let (_, st, buses) = set_level(cpu, BIG0, big[big.len() - 1], 50.0);
    let s = i2c_stats(cpu, bus);
    t.check(
        st == Some(0) && buses.contains(&format!("i2c{bus}")),
        &format!("pmic: bus {bus} free again: big0's highest level set (status {}, I2C {}), bus up again {} times", fmt_st(st), fmt_buses(&buses), s.ups - before.ups),
    );
    let cur = level(cpu, BIG0);
    t.check(cur == Some(big[big.len() - 1]), &format!("pmic: big0 at level {}", cur.map_or("None".into(), |c| c.to_string())));
    stacks(t, cpu, "pmic");
    t.done(cpu);
}

fn sensor_scenario(t: &mut T) {
    let mut cpu = boot(t, Some("ms-r1"));
    let cpu = &mut cpu;
    let n = cpu.bus.i2c_log.len();
    soc::run_for(cpu, 250.0); // a board-sensor read (200 ms)
    let mut sensors: Vec<String> =
        cpu.bus.i2c_log[n..].iter().filter(|l| l.what.contains(":0x4d")).map(|l| l.what.split(':').next().unwrap().to_string()).collect();
    sensors.sort();
    if sensors.is_empty() {
        t.check(false, "sensor: no board-sensor read (0x4d) in 250 ms");
        return;
    }
    let bus = bus_no(&sensors[0]);
    t.check(true, &format!("sensor: the MS-R1's TMP75s on I2C {bus}"));
    let lv = levels(cpu, BIG0);
    set_level(cpu, BIG0, lv[0], 50.0);
    let before = i2c_stats(cpu, bus);
    cpu.bus.i2c[bus].fault = Some(Fault::Stuck);
    soc::run_for(cpu, 450.0); // two board-sensor reads (200 ms)
    let (tt, st, buses) = set_level(cpu, BIG0, lv[lv.len() - 1], 50.0);
    t.check(
        st == Some(0) && tt.map_or(false, |x| x < 5.0),
        &format!("sensor: TMP75 bus {bus} stuck: big0's highest level set in {} (status {}, I2C {})", fmt_t(tt), fmt_st(st), fmt_buses(&buses)),
    );
    cpu.bus.set_temps(&mut cpu.io, 100.0);
    soc::run_for(cpu, 20.0);
    let dt = die_temp(cpu);
    t.check(dt > 90.0, &format!("sensor: bus {bus} stuck: the DPM's die temperature {dt:.1} C"));
    let s = i2c_stats(cpu, bus);
    t.check(
        s.timeouts - before.timeouts >= 3 && s.downs - before.downs >= 1,
        &format!(
            "sensor: bus {bus}: {} time-outs, down {} times, {} transfers failed at once",
            s.timeouts - before.timeouts,
            s.downs - before.downs,
            s.fast_fails - before.fast_fails
        ),
    );
    cpu.bus.i2c[bus].fault = None;
    soc::run_for(cpu, 2500.0);
    let s = i2c_stats(cpu, bus);
    t.check(s.ups > before.ups, &format!("sensor: bus {bus} free again: up again"));
    stacks(t, cpu, "sensor");
    t.done(cpu);
}

// --- raises whose voltage fails ------------------------------------------------------
//
// A frequency must never go up on a rail whose voltage write failed.  The
// requests go two ways: an SCMI PERFORMANCE_LEVEL_SET message and the OS's
// fast channel.  For the CPU clusters and the DSU both end in
// sky1soc_perf's queue (scmi_perf's pass); other domains' messages go to
// dvfs (voltage, then clock), but on these boards only the clusters' rails
// are PMICs'.  What is checked is the clock generator: the writes a healthy
// raise of the cluster makes (its PLL's block), and the PLL's registers.

/// The Orange Pi 6's PM configuration with its MP2845s (type 2, 0x45) as
/// AU4683s (type 3, 0x21): CIX's other paged regulator, its own formats;
/// the bucks shared as on the Orange Pi 6.
fn au4683_config() -> Vec<u8> {
    let mut blk = test::board_config("opi6", &[]).unwrap_or_else(|e| panic!("{e}"));
    for d in 0..8 {
        let off = 0x18 + 0x1e + 8 * d;
        let mut w0 = util::u32le(&blk, off);
        if w0 & 7 == 2 {
            w0 = (w0 & !(7 | 0x7f << 22)) | 3 | 0x21 << 22;
            util::put32(&mut blk, off, w0);
        }
    }
    test::config_sums(&mut blk);
    blk
}

/// boot() with a configuration's bytes (None: the board's by name).
fn boot_cfg(t: &mut T, name: &str, cfg: Option<&Vec<u8>>) -> Cpu {
    match cfg {
        None => boot(t, Some(name)),
        Some(c) => {
            let opts = Opts { board: Some(c.clone()), prepare: Some(|cpu| cpu.bus.au4683 = true), ..Default::default() };
            boot_opts(t, &opts, &format!(" ({name})"))
        }
    }
}

fn fault(cpu: &mut Cpu, bus: usize) -> &mut BusFault {
    cpu.bus.i2c[bus].plan.get_or_insert_with(BusFault::default)
}

fn fault_len(cpu: &Cpu, bus: usize) -> usize {
    cpu.bus.i2c[bus].plan.as_ref().map_or(0, |p| p.log.len())
}

fn cgc_writes(cpu: &Cpu, since: usize, blocks: &[u32]) -> Vec<(u32, u64)> {
    cpu.writes[since..].iter().filter(|w| blocks.contains(&(w.addr & !(PLL_BLOCK - 1)))).map(|w| (w.addr, w.value)).collect()
}

fn pll_regs(cpu: &mut Cpu, blocks: &[u32]) -> Vec<Vec<u32>> {
    blocks.iter().map(|b| (0..PLL_BLOCK).step_by(4).map(|o| cpu.read32(b + o)).collect()).collect()
}

/// Voltage writes logged by the bus's fault plan from entry since:
/// (writes before it, bytes).
fn vout_writes(cpu: &Cpu, bus: usize, since: usize) -> Vec<(usize, Vec<u8>)> {
    cpu.bus.i2c[bus].plan.as_ref().map_or(Vec::new(), |p| {
        p.log[since..].iter().filter(|x| x.write && x.tx.len() >= VOUT_LEN).map(|x| (x.writes, x.tx.clone())).collect()
    })
}

/// Level lv on domain d by path ("msg": the message; "fc": big0's fast channel).
fn request(cpu: &mut Cpu, path: &str, d: u32, lv: u32, ms: f64) {
    if path == "fc" {
        assert_eq!(d, BIG0);
        test::ask(cpu, 0, &Ask::Fc(test::BIG0_FC, lv));
    } else {
        send(cpu, 0, 0x13, 7, &[d, lv]);
        wait_answer(cpu, 0, 50.0);
    }
    soc::run_for(cpu, ms);
}

/// A healthy raise of d from its lowest level to its highest: the bus of
/// its rail, the clock generator blocks it writes.  d is left at its lowest.
fn calibrate(cpu: &mut Cpu, path: &str, d: u32, lv: &[u32]) -> (Option<usize>, Vec<u32>) {
    request(cpu, path, d, lv[0], 60.0);
    let (nw, ni) = (cpu.writes.len(), cpu.bus.i2c_log.len());
    request(cpu, path, d, lv[lv.len() - 1], 60.0);
    let mut blocks: Vec<u32> =
        cpu.writes[nw..].iter().filter(|w| (test::CGC.0..test::CGC.1).contains(&w.addr)).map(|w| w.addr & !(PLL_BLOCK - 1)).collect();
    blocks.sort();
    blocks.dedup();
    let buses = i2c_buses(cpu, ni);
    request(cpu, path, d, lv[0], 60.0);
    (buses.first().map(|b| bus_no(b)), blocks)
}

/// Index in cpu.writes of the first write to blocks from since, or None.
fn first_pll_write(cpu: &Cpu, since: usize, blocks: &[u32]) -> Option<usize> {
    cpu.writes[since..].iter().position(|w| blocks.contains(&(w.addr & !(PLL_BLOCK - 1)))).map(|i| i + since)
}

fn hexes(b: &[u32]) -> String {
    format!("[{}]", b.iter().map(|x| format!("'{x:#x}'")).collect::<Vec<_>>().join(", "))
}

/// d at its lowest asked for its highest level while its PMIC's bus fails
/// (mode, from the after-th transfer): no clock generator write to d's PLL,
/// its registers as they were; once the bus is healthy, the same request
/// sets the voltage and then the frequency.
fn raise_failed(t: &mut T, name: &str, cfg: Option<&Vec<u8>>, path: &str, mode: FaultMode, after: usize) {
    let d = BIG0;
    let mname = if mode == FaultMode::Nack { "nack" } else { "lost" };
    let tag = format!("raise {name} {path} {mname}{}", if after != 0 { format!(" after {after}") } else { String::new() });
    let mut cpu = boot_cfg(t, name, cfg);
    let cpu = &mut cpu;
    let lv = levels(cpu, d);
    let (bus, blocks) = calibrate(cpu, path, d, &lv);
    let Some(bus) = bus.filter(|_| !blocks.is_empty()) else {
        t.check(false, &format!("{tag}: a healthy raise writes I2C {bus:?}, clock generator {}", hexes(&blocks)));
        return;
    };
    let top = lv[lv.len() - 1];
    let (regs0, nw) = (pll_regs(cpu, &blocks), cpu.writes.len());
    fault(cpu, bus).arm(Some(mode), after);
    request(cpu, path, d, top, 200.0);
    let got = cgc_writes(cpu, nw, &blocks);
    let tried = fault_len(cpu, bus);
    let same = pll_regs(cpu, &blocks) == regs0;
    t.check(
        tried > 0 && got.is_empty() && same,
        &format!(
            "{tag}: I2C {bus} failing ({tried} transfers): PLL {} {}",
            hexes(&blocks),
            if got.is_empty() { "untouched".to_string() } else { format!("written {} times, first {:#x}={:#x}", got.len(), got[0].0, got[0].1) }
        ),
    );
    let cur = level(cpu, d);
    t.check(cur != Some(top), &format!("{tag}: level read back {} (asked {top})", cur.map_or("None".into(), |c| c.to_string())));
    fault(cpu, bus).arm(None, 0);
    soc::run_for(cpu, 2100.0); // the bus's back-off, if it went down
    let (nw, nl) = (cpu.writes.len(), fault_len(cpu, bus));
    request(cpu, path, d, top, 60.0);
    let (first, v) = (first_pll_write(cpu, nw, &blocks), vout_writes(cpu, bus, nl));
    let after_v = first.map_or(false, |f| !v.is_empty() && v[0].0 <= f);
    t.check(
        after_v,
        &format!(
            "{tag}: bus healthy again: {} voltage writes, the PLL {}",
            v.len(),
            if first.is_none() { "not written" } else if after_v { "written after them" } else { "written first" }
        ),
    );
    t.done(cpu);
}

/// d at its highest lowered to its lowest while the voltage writes are lost
/// (they reach the regulator, the transfer fails: the rail at the lower
/// voltage, the write failed); the bus healthy again, a level (target:
/// index from the end of the levels) on d, or on domain then (sharing d's
/// buck), from its lowest: no PLL write of the raised domain before a
/// voltage write.
fn lower_lost_then_raise(t: &mut T, name: &str, cfg: Option<&Vec<u8>>, path: &str, target: isize, then: Option<u32>) {
    let d = BIG0;
    let raised = then.unwrap_or(d);
    let tag = format!("lower lost {name} {path}: domain {d} lowered, domain {raised} to its level [{target}]");
    let mut cpu = boot_cfg(t, name, cfg);
    let cpu = &mut cpu;
    let lv = levels(cpu, d);
    let (bus, _) = calibrate(cpu, path, d, &lv);
    let rlv = levels(cpu, raised);
    let rpath = if then.is_some() { "msg" } else { path };
    let (rbus, blocks) = calibrate(cpu, rpath, raised, &rlv);
    let Some(bus) = bus.filter(|b| rbus == Some(*b) && !blocks.is_empty()) else {
        t.check(false, &format!("{tag}: rails on I2C {bus:?} and {rbus:?}, clock generator {}", hexes(&blocks)));
        return;
    };
    fault(cpu, bus);
    request(cpu, path, d, lv[lv.len() - 1], 60.0);
    fault(cpu, bus).arm(Some(FaultMode::Lost), 0);
    let nl = fault_len(cpu, bus);
    request(cpu, path, d, lv[0], 200.0);
    let lost = vout_writes(cpu, bus, nl);
    fault(cpu, bus).arm(None, 0);
    soc::run_for(cpu, 50.0);
    let (nw, nl) = (cpu.writes.len(), fault_len(cpu, bus));
    let want = rlv[py_idx(rlv.len(), target)];
    request(cpu, rpath, raised, want, 60.0);
    let (first, v) = (first_pll_write(cpu, nw, &blocks), vout_writes(cpu, bus, nl));
    let ok = first.map_or(true, |f| !v.is_empty() && v[0].0 <= f);
    let how = match first {
        None => "no PLL write".to_string(),
        Some(f) => format!(
            "PLL written after {}",
            if v.is_empty() { "no voltage write" } else if v[0].0 <= f { "a voltage write" } else { "the PLL, voltage after it" }
        ),
    };
    t.check(!lost.is_empty() && ok, &format!("{tag}: {} voltage writes lost; then {how}", lost.len()));
    t.done(cpu);
}

fn raise_scenarios(t: &mut T) {
    let au = au4683_config();
    // MP5475 (O6N), MP2845 and AU4683 (generic paged driver, shared bucks)
    for (name, cfg) in [("o6n", None), ("opi6", None), ("opi6-au4683", Some(&au))] {
        for path in ["msg", "fc"] {
            raise_failed(t, name, cfg, path, FaultMode::Nack, 0);
        }
    }
    // a write that reaches the regulator but fails: set, not known to be
    raise_failed(t, "o6n", None, "fc", FaultMode::Lost, 0);
    raise_failed(t, "opi6", None, "msg", FaultMode::Lost, 0);
    // the bus dies after the first transfer: the page selected, VOUT not
    raise_failed(t, "opi6", None, "msg", FaultMode::Nack, 1);
    raise_failed(t, "opi6-au4683", Some(&au), "fc", FaultMode::Nack, 1);
    // a lower whose writes were lost, then a raise
    lower_lost_then_raise(t, "o6n", None, "msg", -2, None);
    lower_lost_then_raise(t, "o6n", None, "fc", -1, None);
    lower_lost_then_raise(t, "opi6", None, "msg", -2, None);
    // the buck shared: big 0 lowered with its writes lost, big 1 raised
    lower_lost_then_raise(t, "opi6", None, "msg", -2, Some(BIG1));
}

// --- two owners of a cluster -------------------------------------------------------
//
// Big cluster 0 is asked for levels by SCMI message and by the OS's fast
// channel in turn (CIX's firmware set the first through dvfs and the second
// through sky1soc_perf, each with its own record of where the cluster was;
// ours has sky1soc_perf do both).  No fault: after each request the
// regulator must hold at least the voltage the PLL's setting had when the
// fast channel alone set it, and the level asked for must be applied.

/// An index as Python takes it: negative ones from the end.
fn py_idx(len: usize, i: isize) -> usize {
    if i < 0 {
        (len as isize + i) as usize
    } else {
        i as usize
    }
}

/// An MP5475 buck's VOUT register, mV.
fn vout_mv(cpu: &Cpu, bus: usize, buck: usize) -> u32 {
    let dev = &cpu.bus.i2c[bus].devices[&0x60];
    ((dev.regs[0x13 + 8 * buck] as u32) << 9) + dev.regs[0x14 + 8 * buck] as u32 * 2
}

/// steps: (path, level index) in turn on big0 from its lowest level.
fn two_owners(t: &mut T, steps: &[(&str, isize)]) {
    let name = "o6n";
    let tag = format!("two owners {}", steps.iter().map(|(p, i)| format!("{p}:{i}")).collect::<Vec<_>>().join(" "));
    let mut cpu = boot(t, Some(name));
    let cpu = &mut cpu;
    let lv = levels(cpu, BIG0);
    let (bus, blocks) = calibrate(cpu, "fc", BIG0, &lv);
    let Some(bus) = bus.filter(|_| !blocks.is_empty()) else {
        t.check(false, &format!("{tag}: big0's rail on I2C {bus:?}, clock generator {}", hexes(&blocks)));
        return;
    };
    let ni = cpu.bus.i2c_log.len();
    request(cpu, "fc", BIG0, lv[lv.len() - 1], 60.0);
    let reg = cpu.bus.i2c_log[ni..]
        .iter()
        .find(|l| l.what.contains(" W ") && l.what.contains(":0x60"))
        .and_then(|l| l.what.split_whitespace().nth(2).and_then(|x| u32::from_str_radix(x, 16).ok()))
        .unwrap_or(0x13);
    let buck = ((reg - 0x13) / 8) as usize;
    // each level's PLL setting and voltage, one owner (the fast channel), from the lowest up
    let mut need = Vec::new();
    for lvl in &lv {
        request(cpu, "fc", BIG0, *lvl, 60.0);
        need.push((*lvl, pll_regs(cpu, &blocks), vout_mv(cpu, bus, buck)));
    }
    request(cpu, "fc", BIG0, lv[0], 60.0);
    for (path, i) in steps {
        let asked = lv[py_idx(lv.len(), *i)];
        request(cpu, path, BIG0, asked, 60.0);
        let (regs, mv) = (pll_regs(cpu, &blocks), vout_mv(cpu, bus, buck));
        let at = need.iter().find(|n| n.1 == regs);
        let s = |x: Option<u32>| x.map_or("None".to_string(), |v| v.to_string());
        t.check(
            at.map_or(false, |n| mv >= n.2),
            &format!("{tag}: {path} asked {asked}: PLL at level {} (needs {} mV), rail {mv} mV", s(at.map(|n| n.0)), s(at.map(|n| n.2))),
        );
        t.check(at.map(|n| n.0) == Some(asked), &format!("{tag}: {path} asked {asked}: applied (PLL at level {})", s(at.map(|n| n.0))));
    }
    t.done(cpu);
}

fn two_owner_scenarios(t: &mut T) {
    // the OS's fast channel, then a lower by message (or a TDP cap), then the fast channel
    two_owners(t, &[("fc", -1), ("msg", 1), ("fc", -2)]);
    // a lower by message after the fast channel raised the cluster
    two_owners(t, &[("fc", -1), ("msg", 2)]);
}

// --- unsure voltages: rewritten first; resume; boot -----------------------------------

/// Big0 by fast channel to its highest level while its voltage writes fail
/// (mode), the bus healthy again, then its lowest level asked (where the
/// record has it): the pass writes the voltage it has recorded first, and
/// the regulator then holds it.
fn next_pass_rewrites(t: &mut T, mode: FaultMode) {
    let tag = format!("unsure {}", if mode == FaultMode::Nack { "nack" } else { "lost" });
    let mut cpu = boot(t, Some("o6n"));
    let cpu = &mut cpu;
    let lv = levels(cpu, BIG0);
    let (bus, blocks) = calibrate(cpu, "fc", BIG0, &lv);
    let bus = bus.unwrap_or(1);
    let before = cpu.bus.i2c[bus].devices[&0x60].regs.clone();
    fault(cpu, bus).arm(Some(mode), 0);
    let nl = fault_len(cpu, bus);
    request(cpu, "fc", BIG0, lv[lv.len() - 1], 200.0);
    let failed = vout_writes(cpu, bus, nl);
    fault(cpu, bus).arm(None, 0);
    soc::run_for(cpu, 2100.0);
    if failed.is_empty() {
        t.check(false, &format!("{tag}: no voltage write tried"));
        return;
    }
    let reg = failed[0].1[0] as usize;
    let want = vec![reg as u8, before[reg], before[reg + 1]]; // the lowest level's voltage, as before the fault
    let (nw, nl) = (cpu.writes.len(), fault_len(cpu, bus));
    request(cpu, "fc", BIG0, lv[0], 60.0);
    let (v, first) = (vout_writes(cpu, bus, nl), first_pll_write(cpu, nw, &blocks));
    let held = cpu.bus.i2c[bus].devices[&0x60].regs[reg..reg + 2].to_vec();
    let before_pll = !v.is_empty() && first.map_or(true, |f| v[0].0 <= f);
    t.check(
        before_pll && v[0].1 == want && held == want[1..],
        &format!(
            "{tag}: next pass: {} voltage writes ({}; recorded {}), PLL {}, regulator {}",
            v.len(),
            v.first().map_or("-".to_string(), |x| util::hex_sp(&x.1)),
            util::hex_sp(&want),
            if first.is_none() { "not written" } else if before_pll { "after" } else { "first" },
            util::hex_sp(&held)
        ),
    );
    t.done(cpu);
}

/// TF-A's suspend (test.rs's power suite), then the wake; during_resume()
/// is called before the wake.
pub fn suspend_resume(cpu: &mut Cpu, during_resume: impl FnOnce(&mut Cpu)) -> (String, String) {
    cpu.bus.se_suspend();
    test::pm_msg(cpu, test::TFA, test::STR_BOOT_CORE_NOTIFY, &[0]);
    let _ = soc::scmi(cpu, 0x12, 3, &[0, 4], test::TFA, 3_000_000);
    let asleep = test::run_until(cpu, "asleep", None);
    during_resume(cpu);
    cpu.bus.se_wake();
    cpu.bus.s5_wake(&mut cpu.io);
    (asleep, test::run_until(cpu, "quiet", None))
}

/// Big0 at its highest level by fast channel; suspended; its PMIC's bus
/// NACKs while it resumes, so the rail is not set again: after the resume
/// its PLL is at its lowest level's setting, LEVEL_GET says so; the bus
/// healthy again, its highest level: the voltage, then the PLL.
fn resume_rail_lost(t: &mut T) {
    let tag = "resume, rail lost";
    let mut cpu = boot(t, Some("o6n"));
    let cpu = &mut cpu;
    let lv = levels(cpu, BIG0);
    let (bus, blocks) = calibrate(cpu, "fc", BIG0, &lv);
    let bus = bus.unwrap_or(1);
    request(cpu, "fc", BIG0, lv[0], 60.0);
    let low_regs = pll_regs(cpu, &blocks);
    request(cpu, "fc", BIG0, lv[lv.len() - 1], 60.0);
    let high_regs = pll_regs(cpu, &blocks);
    fault(cpu, bus);
    let (a, r) = suspend_resume(cpu, |cpu| fault(cpu, bus).arm(Some(FaultMode::Nack), 0));
    let regs = pll_regs(cpu, &blocks);
    t.check(
        a == "asleep" && r == "quiet" && regs == low_regs,
        &format!(
            "{tag}: suspend {a}, resume {r}: PLL at {}",
            if regs == low_regs { "its lowest level's setting" } else if regs == high_regs { "the highest level's setting" } else { "another setting" }
        ),
    );
    let cur = level(cpu, BIG0);
    t.check(cur == Some(lv[0]), &format!("{tag}: LEVEL_GET {} (lowest {})", cur.map_or("None".into(), |c| c.to_string()), lv[0]));
    fault(cpu, bus).arm(None, 0);
    soc::run_for(cpu, 2100.0);
    let (nw, nl) = (cpu.writes.len(), fault_len(cpu, bus));
    request(cpu, "fc", BIG0, lv[lv.len() - 2], 60.0);
    request(cpu, "fc", BIG0, lv[lv.len() - 1], 60.0);
    let (first, v) = (first_pll_write(cpu, nw, &blocks), vout_writes(cpu, bus, nl));
    let after_v = first.map_or(false, |f| !v.is_empty() && v[0].0 <= f);
    t.check(
        after_v && pll_regs(cpu, &blocks) == high_regs,
        &format!(
            "{tag}: bus healthy, highest level: {} voltage writes, PLL {}",
            v.len(),
            if first.is_none() { "not written" } else if after_v { "after them" } else { "first" }
        ),
    );
    t.done(cpu);
}

/// Big0's PMIC loses every voltage write from reset (they reach the
/// regulator, the transfer fails): the boot voltages and dvfs's boot OPP
/// fail (dvfs gives up after its retries).  The bus healthy, big0 by fast
/// channel to its lowest, then its highest level: in the raise, a voltage
/// write before the PLL's, and the level set.
fn boot_writes_lost(t: &mut T) {
    let tag = "boot, writes lost";
    let opts = Opts {
        board: Some(test::board_config("o6n", &[]).unwrap_or_else(|e| panic!("{e}"))),
        prepare: Some(|cpu| cpu.bus.i2c[BOOT_LOST_BUS].lose_vout = true),
        ..Default::default()
    };
    let mut cpu = boot_opts(t, &opts, " (o6n)");
    let cpu = &mut cpu;
    cpu.bus.i2c[BOOT_LOST_BUS].lose_vout = false;
    let lv = levels(cpu, BIG0);
    let bus = BOOT_LOST_BUS;
    fault(cpu, bus);
    let nl = fault_len(cpu, bus);
    request(cpu, "fc", BIG0, lv[0], 60.0);
    let v0 = vout_writes(cpu, bus, nl);
    // the raise only: in the lower before it the PLL goes first, rightly
    let (nw, nl) = (cpu.writes.len(), fault_len(cpu, bus));
    let top = lv[lv.len() - 1];
    request(cpu, "fc", BIG0, top, 60.0);
    let mut blocks: Vec<u32> =
        cpu.writes[nw..].iter().filter(|w| (test::CGC.0..test::CGC.1).contains(&w.addr)).map(|w| w.addr & !(PLL_BLOCK - 1)).collect();
    blocks.sort();
    blocks.dedup();
    let (first, v) = (first_pll_write(cpu, nw, &blocks), vout_writes(cpu, bus, nl));
    let after_v = first.map_or(false, |f| !v.is_empty() && v[0].0 <= f);
    t.check(
        !v0.is_empty() && after_v,
        &format!(
            "{tag}: lowest level: {} voltage writes; highest: PLL {}",
            v0.len(),
            if first.is_none() { "not written" } else if after_v { "after a voltage write" } else { "before any voltage write" }
        ),
    );
    let cur = level(cpu, BIG0);
    t.check(cur == Some(top), &format!("{tag}: level {} (asked {top})", cur.map_or("None".into(), |c| c.to_string())));
    t.done(cpu);
}

fn unsure_scenarios(t: &mut T) {
    next_pass_rewrites(t, FaultMode::Nack);
    next_pass_rewrites(t, FaultMode::Lost);
    resume_rail_lost(t);
    boot_writes_lost(t);
}

pub fn main(args: &[String]) -> i32 {
    let a = match Args::parse(args, &["--cov"]) {
        Ok(a) => a,
        Err(e) => {
            eprintln!("{e}");
            return 2;
        }
    };
    let Some(elf) = a.pos.first() else {
        eprintln!("usage: sky1emu pm-faults OPEN.elf [-v] [--cov FILE]");
        return 2;
    };
    let mut t = T { failures: 0, verbose: a.flag("-v"), elf: elf.clone(), covered: Vec::new() };
    pmic_scenario(&mut t, "o6n"); // fans the PM drives
    sensor_scenario(&mut t);
    raise_scenarios(&mut t);
    two_owner_scenarios(&mut t);
    unsure_scenarios(&mut t);
    println!("{} failed", t.failures);
    if let Some(path) = a.opt("--cov") {
        let name = std::path::Path::new(elf).file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default();
        if let Err(e) = crate::cov::save(path, &name, &t.covered) {
            eprintln!("{e}");
        }
    }
    t.failures.min(125) as i32
}
