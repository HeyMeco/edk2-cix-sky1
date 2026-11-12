// SPDX-License-Identifier: BSD-2-Clause-Patent
//! BL31: its cold boot behind each side's own BL2, then every SMC.
//!
//! Every phase starts from copies of the two booted machines (in the
//! normal world, at UEFI's entry, on the boot core).  The harness is the
//! lower exception levels: `q.smc` is UEFI or the OS calling, OP-TEE's
//! entries are answered in test.rs (`resume`), a core BL31 powers up is
//! started at the reset vector BL31 programmed.

use super::machine::Config;
use super::soc;
use super::test::{audit_add, cold_boot, enter_bl31, run_bl2, wanted, Coverage, Images, Opts, Phase, Q, World, NS_ENTRY};

/// Both sides booted to the normal world: (open, CIX), with the cold boot's phases.
fn boot(imgs: &Images, o: &Opts, cfg: &Config, cov: &mut Coverage, name: &str) -> Option<(World, World, Phase, Phase)> {
    let (_, wo) = run_bl2(&imgs.open_bl2, cfg, o, cov, name);
    let (_, wc) = run_bl2(&imgs.cix_bl2, cfg, o, cov, name);
    let (mut wo, mut wc) = (wo?, wc?);
    enter_bl31(&mut wo, &imgs.open_bl31);
    enter_bl31(&mut wc, &imgs.cix_bl31);
    let core = cfg.boot_core;
    let (po, _) = cold_boot(&mut wo, core, name);
    let (pc, _) = cold_boot(&mut wc, core, name);
    cov.add(&wo.m);
    cov.add(&wc.m);
    Some((wo, wc, po, pc))
}

type Run = fn(&mut Q);

const PHASES: &[(&str, Run)] = &[
    ("BL31: SMCCC and PSCI queries", queries),
    ("BL31: SiP calls", sip),
    ("BL31: every service and function number", sweep),
    ("BL31: OP-TEE", optee),
    ("BL31: cores on and off", cores),
    ("BL31: CPU_SUSPEND", cpu_suspend),
    ("BL31: SYSTEM_SUSPEND and resume", system_suspend),
    ("BL31: SYSTEM_OFF and SYSTEM_RESET", system_off),
    ("BL31: SDEI", sdei),
    ("BL31: interrupts and exceptions taken to EL3", interrupts),
    ("BL31: SPI NOR by SiP call", nor),
    ("BL31: SCMI by SiP call", scmi),
    ("BL31: power domains with memory repair, and the PDC's wake-up sources", repair),
    ("BL31: OP-TEE's interrupts, deferred errors, masked events", more_interrupts),
];

/// A TrustZone configuration file as the trustzone_config tool writes it: `length` 40 (13 bytes of
/// configuration) or the older 36 (12, no recovery flag).
fn tz_config(spe: u8, kind: u8, addr: u32, size: u32, recovery: u8, length: u32) -> Vec<u8> {
    let mut b = vec![0xffu8; 0x40];
    b[..0x28].fill(0);
    b[0..2].copy_from_slice(&1u16.to_le_bytes());
    b[4..8].copy_from_slice(&0x0907_8e7du32.to_le_bytes());
    b[8..12].copy_from_slice(&length.to_le_bytes());
    b[12..16].copy_from_slice(&0x4643_5a54u32.to_le_bytes());
    b[24] = 1;
    b[25] = if length >= 40 { 13 } else { 12 };
    b[26] = spe;
    b[27] = kind;
    b[28..32].copy_from_slice(&addr.to_le_bytes());
    b[32..36].copy_from_slice(&size.to_le_bytes());
    if length >= 40 {
        b[36] = recovery;
    }
    tz_sums(&mut b);
    b
}

/// The file's two sums over its first `length` bytes as 32-bit words, the sums' own words as 0.
fn tz_sums(b: &mut [u8]) {
    let length = crate::util::u32le(b, 8).min(0x40) as usize;
    let (mut x, mut y) = (0u32, 0u32);
    for i in 0..length / 4 {
        if i != 4 && i != 5 {
            x = x.wrapping_add(crate::util::u32le(b, 4 * i));
        }
        y = y.wrapping_add(x);
    }
    b[16..20].copy_from_slice(&x.to_le_bytes());
    b[20..24].copy_from_slice(&y.to_le_bytes());
}

type Change = Box<dyn Fn(&mut World)>;

/// Put a TrustZone configuration where the flash's header says it is (0xf0000).
fn with_tz(file: Vec<u8>) -> Change {
    Box::new(move |w: &mut World| {
        let f = std::rc::Rc::make_mut(&mut w.m.soc.flash);
        f[0xf_0000..0xf_0000 + file.len()].copy_from_slice(&file);
    })
}

/// BL31's cold boot on a machine changed at the hand-over from BL2: what the flash holds for it, what
/// answers it.
fn handover_variants() -> Vec<(String, Change)> {
    let mut v: Vec<(String, Change)> = Vec::new();
    for (what, spe) in [("off", 0u8), ("0xff", 0xff), ("2", 2)] {
        v.push((format!("TrustZone configuration with SPE {what}"), with_tz(tz_config(spe, 0, 0, 0, 0, 40))));
    }
    v.push(("TrustZone configuration with secure storage in free flash".into(), with_tz(tz_config(1, 1, 0x70_0000, 0x8_0000, 0, 40))));
    v.push(("TrustZone configuration with secure storage over bootloader3".into(), with_tz(tz_config(1, 1, 0x60_0000, 0x20_0000, 0, 40))));
    v.push(("TrustZone configuration with secure storage over the firmware header".into(), with_tz(tz_config(1, 1, 0x10_0000, 0x1000, 0, 40))));
    v.push(("TrustZone configuration with secure storage of no size".into(), with_tz(tz_config(1, 1, 0x70_0000, 0, 0, 40))));
    v.push(("TrustZone configuration with secure storage past the flash's end".into(), with_tz(tz_config(1, 1, 0x7f_0000, 0x10_0000, 0, 40))));
    v.push(("TrustZone configuration with secure storage of another type".into(), with_tz(tz_config(1, 2, 0x70_0000, 0x8_0000, 0, 40))));
    v.push(("TrustZone configuration with bootloader3 recovery on".into(), with_tz(tz_config(1, 0, 0, 0, 1, 40))));
    v.push(("TrustZone configuration of the older tool (36 bytes)".into(), with_tz(tz_config(1, 0, 0, 0, 0, 36))));
    v.push(("TrustZone configuration of the older tool with secure storage over bootloader3 (Radxa's file)".into(), with_tz(tz_config(1, 1, 0x60_0000, 0x20_0000, 0, 36))));
    for (what, at, val) in [("a wrong signature", 12usize, 0x58u8), ("version 2.0", 0, 2), ("version 1.1", 2, 1), ("version ID 2", 24, 2), ("a wrong sum", 16, 0), ("a length of 20", 8, 20), ("a length of 42", 8, 42), ("a length of 68", 8, 68)] {
        let mut f = tz_config(1, 0, 0, 0, 0, 40);
        f[at] = val;
        if at < 12 || at == 24 {
            tz_sums(&mut f);
        }
        v.push((format!("TrustZone configuration with {what}"), with_tz(f)));
    }
    for (what, n) in [("1", 1u8), ("11", 11), ("14", 14), ("40", 40)] {
        let mut f = tz_config(1, 1, 0x70_0000, 0x8_0000, 1, 40);
        f[25] = n;
        tz_sums(&mut f);
        v.push((format!("TrustZone configuration with a configuration length of {what}"), with_tz(f)));
    }
    v.push((
        "no TrustZone configuration in the flash's header".into(),
        Box::new(|w: &mut World| {
            let f = std::rc::Rc::make_mut(&mut w.m.soc.flash);
            // the entry of type 9 becomes one of a type nothing asks for
            for i in 0..16 {
                let e = 0x10_0010 + 16 * i;
                if crate::util::u32le(f, e) == 9 {
                    f[e] = 0x63;
                }
            }
        }),
    ));
    v.push(("an erased TrustZone configuration".into(), with_tz(vec![0xff; 0x40])));
    v.push(("the SE does not answer".into(), Box::new(|w: &mut World| w.m.soc.se_alive = false)));
    v.push(("the PM does not answer its first messages".into(), Box::new(|w: &mut World| w.m.soc.set_reg(soc::PM_MBOX + 4, 0))));
    v.push(("cores 0 to 3 left out by the eFuses".into(), Box::new(|w: &mut World| w.m.soc.set_reg(0x1600_0508, 0xa00f))));
    v.push(("cores 4 and 11 left out by the eFuses".into(), Box::new(|w: &mut World| w.m.soc.set_reg(0x1600_0508, 0xa810))));
    v.push(("the reboot reason left by the last boot".into(), Box::new(|w: &mut World| w.m.soc.set_reg(0x1600_0500, 0x1234_5678))));
    v
}

/// The same from BL2's entry: what the SE reports and what the flash is.
fn config_variants() -> Vec<(String, Box<dyn Fn(&mut Config)>)> {
    let mut v: Vec<(String, Box<dyn Fn(&mut Config)>)> = Vec::new();
    for mb in [4096u32, 65536] {
        v.push((format!("{} GiB of DRAM", mb / 1024), Box::new(move |c: &mut Config| c.dram_mb = mb)));
    }
    for core in [0usize, 4] {
        v.push((format!("boot core {core}"), Box::new(move |c: &mut Config| c.boot_core = core)));
    }
    v.push(("a flash with quad reads (JEDEC ID ef6017)".into(), Box::new(|c: &mut Config| c.jedec = [0xef, 0x60, 0x17])));
    v
}

/// A few questions after a cold boot in another configuration: enough to see that what was
/// configured holds at run time.
fn after_boot(q: &mut Q) {
    q.smc(&[PSCI_VERSION]);
    for core in [0u64, 3, 4, 10, 11] {
        q.smc(&[AFFINITY_INFO, core << 8, 0]);
    }
    let on = if q.core == 4 { 5 } else { 4 };
    cpu_on(q, on, NS_ENTRY + 0x4000);
    if q.core != 0 {
        cpu_on(q, 0, NS_ENTRY);
    }
    nor_call(q, "JEDEC ID", 0x9f, 0, 0, 1, 1, 0, 0x9100_0000, 3);
    nor_call(q, "read", 0x03, 0xf_0000, 3, 1, 1, 0, 0x9100_0000, 0x20);
    nor_call(q, "quad read", 0x6b, 0xf_0000, 3, 1, 4, 8, 0x9100_0000, 0x20);
    q.smc(&[0xc200_0005]);
    q.smc(&[0xc200_000d]);
    q.smc(&[0x8000_0001, 0x8000_0002]);
}

pub fn run(imgs: &Images, o: &Opts, cov: &mut Coverage, pairs: &mut Vec<(Phase, Phase)>, audit: &mut (u64, Vec<String>)) {
    let cold = "BL31: cold boot";
    let Some((wo, wc, po, pc)) = boot(imgs, o, &o.cfg, cov, cold) else {
        eprintln!("a64-test: a BL2 did not reach BL31 (see a64-test bl2)");
        pairs.push((Phase { name: cold.into(), items: vec![("end".into(), "no BL31".into())], insns: 0 }, Phase::new(cold)));
        return;
    };
    if wanted(o, cold) {
        pairs.push((po, pc));
    }
    let core = o.cfg.boot_core;
    for (name, f) in PHASES {
        if !wanted(o, name) {
            continue;
        }
        let mut q = Q { open: wo.clone(), cix: wc.clone(), phases: (Phase::new(name), Phase::new(name)), cov, o, core };
        if o.verbose {
            println!("  {name}");
        }
        f(&mut q);
        audit_add(audit, &q.open.m);
        audit_add(audit, &q.cix.m);
        pairs.push(q.phases);
    }
    // cold boots that differ from the hand-over on: BL2's result is the same, so it is run once
    let any = |v: &[String]| v.iter().any(|n| wanted(o, &format!("BL31: cold boot, {n}")) || wanted(o, &format!("BL31: after the cold boot, {n}")));
    let hv = handover_variants();
    if any(&hv.iter().map(|x| x.0.clone()).collect::<Vec<_>>()) {
        let (_, wo) = run_bl2(&imgs.open_bl2, &o.cfg, o, cov, "");
        let (_, wc) = run_bl2(&imgs.cix_bl2, &o.cfg, o, cov, "");
        if let (Some(wo), Some(wc)) = (wo, wc) {
            for (name, change) in &hv {
                let (cold, after) = (format!("BL31: cold boot, {name}"), format!("BL31: after the cold boot, {name}"));
                if !wanted(o, &cold) && !wanted(o, &after) {
                    continue;
                }
                let (mut a, mut b) = (wo.clone(), wc.clone());
                change(&mut a);
                change(&mut b);
                enter_bl31(&mut a, &imgs.open_bl31);
                enter_bl31(&mut b, &imgs.cix_bl31);
                let (pa, oka) = cold_boot(&mut a, core, &cold);
                let (pb, okb) = cold_boot(&mut b, core, &cold);
                cov.add(&a.m);
                cov.add(&b.m);
                pairs.push((pa, pb));
                if oka && okb && wanted(o, &after) {
                    let mut q = Q { open: a, cix: b, phases: (Phase::new(&after), Phase::new(&after)), cov, o, core };
                    after_boot(&mut q);
                    audit_add(audit, &q.open.m);
                    audit_add(audit, &q.cix.m);
                    pairs.push(q.phases);
                }
            }
        }
    }
    for (name, change) in config_variants() {
        let (cold, after) = (format!("BL31: cold boot, {name}"), format!("BL31: after the cold boot, {name}"));
        if !wanted(o, &cold) && !wanted(o, &after) {
            continue;
        }
        let mut cfg = o.cfg.clone();
        change(&mut cfg);
        let Some((a, b, pa, pb)) = boot(imgs, o, &cfg, cov, &cold) else { continue };
        pairs.push((pa, pb));
        if wanted(o, &after) {
            let mut q = Q { open: a, cix: b, phases: (Phase::new(&after), Phase::new(&after)), cov, o, core: cfg.boot_core };
            after_boot(&mut q);
            audit_add(audit, &q.open.m);
            audit_add(audit, &q.cix.m);
            pairs.push(q.phases);
        }
    }
}

const PSCI_VERSION: u64 = 0x8400_0000;
const CPU_SUSPEND: u64 = 0xc400_0001;
const CPU_OFF: u64 = 0x8400_0002;
const CPU_ON: u64 = 0xc400_0003;
const AFFINITY_INFO: u64 = 0xc400_0004;
const SYSTEM_OFF: u64 = 0x8400_0008;
const SYSTEM_RESET: u64 = 0x8400_0009;
const PSCI_FEATURES: u64 = 0x8400_000a;
const SYSTEM_SUSPEND: u64 = 0xc400_000e;
const SYSTEM_RESET2: u64 = 0xc400_0012;

fn queries(q: &mut Q) {
    // SMCCC: version, features of everything, the SoC's ID, the workarounds
    q.smc(&[0x8000_0000]);
    for f in [0x8000_0000u64, 0x8000_0001, 0x8000_0002, 0x8000_8000, 0x8000_7fff, 0x8000_3fff, 0x8000_0003, PSCI_VERSION, CPU_ON, 0xc200_0001] {
        q.smc(&[0x8000_0001, f]);
    }
    for a in [0u64, 1, 2] {
        q.smc(&[0x8000_0002, a]);
    }
    for f in [0x8000_8000u64, 0x8000_7fff, 0x8000_3fff] {
        q.smc(&[f, 1]);
    }
    // PSCI
    q.smc(&[PSCI_VERSION]);
    for n in 0..0x18u64 {
        q.smc(&[PSCI_FEATURES, 0x8400_0000 | n]);
        q.smc(&[PSCI_FEATURES, 0xc400_0000 | n]);
    }
    q.smc(&[PSCI_FEATURES, 0x8000_0000]);
    for core in 0..13u64 {
        for level in 0..3u64 {
            q.smc(&[AFFINITY_INFO, core << 8, level]);
        }
    }
    q.smc(&[AFFINITY_INFO, 0x8100_0a00, 0]);
    q.smc(&[AFFINITY_INFO, 0x1_0000, 0]);
    q.smc(&[AFFINITY_INFO, 0xa00, 3]);
    q.smc(&[0x8400_0004, 0xa00, 0]);
    for f in [0x8400_0005u64, 0xc400_0005, 0x8400_0006, 0x8400_0007, 0xc400_0007] {
        q.smc(&[f, 0xa00]);
    }
    for (a, b) in [(0xa00u64, 0u64), (0xa00, 1), (0xa00, 2), (0x100, 0), (0xc00, 0), (0, 3)] {
        q.smc(&[0xc400_000d, a, b]); // NODE_HW_STATE
    }
    for f in [0xc400_0010u64, 0xc400_0011, 0x8400_0010, 0x8400_0011] {
        for (a, b) in [(0xa00u64, 0u64), (0xa00, 0x1_0000), (0x100, 0x1_0000), (0xa00, 0x0101_0000), (0xa00, 0x0201_0000), (0xf00, 0)] {
            q.smc(&[f, a, b]); // STAT_RESIDENCY, STAT_COUNT
        }
    }
    q.smc(&[0x8400_000f, 0]); // SET_SUSPEND_MODE
    q.smc(&[0x8400_000f, 1]);
    q.smc(&[0x8400_000f, 2]);
    q.smc(&[0x8400_0013, 1]); // MEM_PROTECT
    q.smc(&[0x8400_0013, 0]);
    q.smc(&[0xc400_0014, 0x9000_0000, 0x1000]); // MEM_PROTECT_CHECK_RANGE
    q.smc(&[0x8400_0014, 0x9000_0000, 0x1000]);
    // the calls that take an entry point, with ones they must refuse
    for entry in [0u64, 0x8020_0000, 0x8050_0000, 0x824f_f000, 0x8250_0000, 0x4_7fff_fffc, 0x4_8000_0000, 0x80_0000_0000, 0x100_0000_0000, 0x1000] {
        q.smc(&[CPU_ON, 0x100, entry, 0x1234]);
    }
    for mpidr in [0xa00u64, 0xc00, 0x1_0100, 0x101, 0xffff_ffff_ffff_ffff, 0x8100_0100, 0x100_0000_0100] {
        q.smc(&[CPU_ON, mpidr, NS_ENTRY, 0x1234]);
    }
    for state in [0x1u64, 0x100_0000, 0x200_0000, 0x300_0000, 0x1_0001, 0x4000_0000, 0x8001_0000, 0xffff_ffff] {
        q.smc(&[CPU_SUSPEND, state, NS_ENTRY, 0x55]);
    }
    q.smc(&[CPU_SUSPEND, 0x1_0000, 0x8020_0000, 0x55]);
    q.smc(&[SYSTEM_SUSPEND, 0x8020_0000, 0x55]);
    q.smc(&[0x8400_000e, 0x8020_0000, 0x55]);
    q.smc(&[0x8400_0012, 0, 0]);
    // the services' own queries
    for f in [0x8400_ff00u64, 0x8400_ff01, 0x8400_ff02, 0x8400_ff03, 0x8200_ff00, 0x8200_ff01, 0x8200_ff02, 0x8200_ff03, 0x8000_ff00, 0x8000_ff01, 0x8000_ff03, 0x8100_ff00, 0x8100_ff01, 0x8100_ff03, 0x8300_ff00, 0x8300_ff01, 0x8300_ff03, 0xbf00_ff00, 0xbf00_ff01, 0xbf00_ff03] {
        q.smc(&[f]);
    }
    // the same from EL1, and a 32-bit function number with rubbish above it
    q.each(&|w| {
        let c = &mut w.m.cpus[10];
        c.el = 1;
    });
    q.smc_as("from EL1: SMC", &[PSCI_VERSION]);
    q.smc_as("from EL1: SMC", &[0x8000_0000]);
    q.each(&|w| {
        let c = &mut w.m.cpus[10];
        c.el = 2;
    });
    q.smc(&[0xdead_beef_8400_0000]);
    q.smc(&[0xdead_beef_c200_0002, 0x77]);
}

fn sip(q: &mut Q) {
    // the reboot reason
    for v in [0u64, 1, 0x5a5a_5a5a, 0xffff_ffff_ffff_ffff] {
        q.smc(&[0xc200_0002, v]);
    }
    // the DSU's power controls
    for v in [0u64, 1, 2, 3, 7, 0xff] {
        q.smc(&[0xc200_0003, v]);
        q.smc(&[0xc200_0004, v]);
    }
    q.smc(&[0xc200_0005]);
    for (a, b, c, d, e) in [(0u64, 0u64, 0u64, 0u64, 0u64), (1, 2, 3, 4, 5), (0xffff, 0xffff, 0xffff, 0xffff, 0xffff), (0x10, 0x100, 0x1000, 0x1_0000, 0x10_0000)] {
        q.smc(&[0xc200_0006, a, b, c, d, e]);
    }
    q.smc(&[0xc200_0007]);
    q.smc(&[0xc200_0008]);
    // the PDC's wake-up sources
    for a in 0..6u64 {
        for b in [0u64, 1, 0x20, 0xffff_ffff] {
            q.smc(&[0xc200_0009, a, b, 0, 0]);
        }
    }
    q.smc(&[0xc200_0009, 1, 5, 1, 1]);
    q.smc(&[0xc200_0009, 2, 5, 0x100, 3]);
    // power domain use counts: every domain on, off, off again; then out of range
    for flag in [1u64, 1, 0, 0, 0] {
        for id in 0..40u64 {
            q.smc(&[0xc200_000a, id, flag]);
        }
    }
    for id in [40u64, 41, 64, 0x100, 0xffff_ffff, 0x8000_0000, 0xffff_ffff_ffff_ffff] {
        q.smc(&[0xc200_000a, id, 1]);
        q.smc(&[0xc200_000a, id, 0]);
    }
    for v in [0u64, 1, 2, 3, 0xff] {
        q.smc(&[0xc200_000c, v]); // SMMU for the GOP
        q.smc(&[0xc200_000f, v, 0]); // DP for the GOP
        q.smc(&[0xc200_000f, v, 1]);
        q.smc(&[0xc200_0010, v]); // DDR low power
        q.smc(&[0xc200_0011, v]); // CPU boost trigger
    }
    q.smc(&[0xc200_000d]); // cpufreq support (board ID)
    q.each(&|w| w.m.soc.set_reg(0x1600_050c, 0x7));
    q.smc(&[0xc200_000d]);
    q.smc(&[0xc200_000e]); // PI test
    // the debug commands
    for cmd in 0..0x80u64 {
        q.smc(&[0xc200_0012, cmd, 0x9000_0000, 0x10, 1]);
    }
    q.smc(&[0xc200_0012, 0x61, 0, 0, 0]);
    q.smc(&[0xc200_0012, 0x161, 0x8020_0000, 0x10, 1]);
    // the OEM key hash calls of CIX's BL31
    for (a, b) in [(0u64, 0u64), (0x9000_0000, 0x20), (0x8020_0000, 0x20), (0x9000_0000, 0x1000)] {
        q.smc(&[0xc200_0030, a, b]);
        q.smc(&[0xc200_0031, a, b]);
    }
    // the execution state switch
    q.smc(&[0x8200_0020, 0, 0x9000_0000, 0, 0x1234]);
    q.smc(&[0x8200_0020, 1, 0x9000_0000, 0, 0x1234]);
    q.smc(&[0xc200_0020, 0, 0x9000_0000, 0, 0x1234]);
    // PMF, debugfs, Ethos-N numbers
    for f in [0x8200_0010u64, 0xc200_0010, 0x8200_0030, 0x8200_0050, 0xc200_0050, 0xc200_0051] {
        q.smc(&[f, 0, 0, 0]);
    }
}

/// Every owning entity and the low function numbers of each, fast and
/// yielding, 32 and 64 bit, with arguments that are not addresses of
/// anything.
fn sweep(q: &mut Q) {
    for top in [0x80u64, 0xc0, 0x00, 0x40] {
        for oen in 0..64u64 {
            let numbers: Vec<u64> = if top & 0x80 != 0 { (0..0x34u64).chain([0x50, 0x51, 0x60, 0x7f, 0x80, 0x100, 0x3fff, 0x7fff, 0x8000, 0xff00, 0xff01, 0xff02, 0xff03, 0xffff]).collect() } else { vec![0, 1, 2, 3, 4, 0xff00, 0xffff] };
            for n in numbers {
                let fid = ((top | oen) << 24) | n;
                // what ends the machine or needs a caller's memory is asked in its own phase
                let apart = matches!(fid, 0x8400_0002 | 0x8400_0008 | 0x8400_0009 | 0x8400_0012 | 0xc400_0012 | 0x8400_0001 | 0xc400_0001 | 0x8400_0003 | 0xc400_0003 | 0x8400_000e | 0xc400_000e);
                if apart {
                    continue;
                }
                q.smc(&[fid, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77]);
            }
        }
    }
    // reserved bits of the function number
    for fid in [0x8401_0000u64, 0x84ff_0000, 0x8400_0000 | 0x80_0000, 0xc2ff_0002, 0x8280_0002] {
        q.smc(&[fid, 1, 2, 3]);
    }
}

fn optee(q: &mut Q) {
    // calls for OP-TEE: yielding and fast, every trusted OS and application entity
    for fid in [0x3200_0000u64, 0x3200_0001, 0x3200_0003, 0x3200_0004, 0x3200_ffff, 0xb200_0000, 0xb200_0001, 0xb200_0009, 0xb200_000c, 0xb200_ff01, 0x3000_0000, 0x3100_0000, 0xb000_0000, 0xb100_0000, 0x3e00_0000, 0xbe00_0000, 0xbe00_0005, 0x3f00_0000, 0xbf00_0000, 0x7200_0000, 0xf200_0000] {
        q.smc(&[fid, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7]);
    }
    // what OP-TEE itself asks of BL31 while it runs a call
    let own: &[&[u64]] = &[
        &[0x8400_0000],
        &[0x8000_0000],
        &[0x8000_0001, 0x8000_8000],
        &[0xc200_0002, 0x42],
        &[0xc200_000b, 0x9f, 0, 1, 0x3_824f_f000],
        &[0xc200_000b, 0x9f, 0, 1, 0x3_9000_0000],
        &[0xc200_000a, 3, 1],
        &[0xc200_0001],
        &[0xbe00_0009, 1, 2, 3],
        &[0xbe00_000b, 1, 2, 3],
        &[0xbe00_0001, 0],
        &[0xbe00_0006, 0],
        &[0x3200_0004, 1, 2, 3],
        &[0xc400_0004, 0x100, 0],
        &[0xc400_0003, 0x100, NS_ENTRY, 0],
        &[0x8400_0002],
    ];
    for args in own {
        let args = args.to_vec();
        let label = format!("OP-TEE's own SMC {}", args.iter().map(|a| format!("{a:#x}")).collect::<Vec<_>>().join(" "));
        q.ask(&label, &move |w, _p, _prefix| {
            if !w.into_tee(10) {
                return "died: the yielding SMC did not enter OP-TEE".into();
            }
            let r = w.secure_smc(10, &args);
            if r.starts_with("x0-x3") && !r.contains("normal world") {
                let end = w.out_of_tee(10);
                format!("{r}; then OP-TEE's return: {end}")
            } else {
                r
            }
        });
    }
    // OP-TEE's panic call
    q.ask("OP-TEE's own SMC 0xbe00000a (it panics)", &|w, _p, _prefix| {
        if !w.into_tee(10) {
            return "died: the yielding SMC did not enter OP-TEE".into();
        }
        w.secure_smc(10, &[0xbe00_000a, 0xdead, 2, 3])
    });
}

/// CPU_ON of a core and its start out of reset.
fn cpu_on(q: &mut Q, target: usize, entry: u64) {
    let core = q.core;
    q.ask(&format!("CPU_ON core {target} at {entry:#x}"), &move |w, p, prefix| {
        let mut tee = Vec::new();
        let end = w.smc(core, &[CPU_ON, (target as u64) << 8, entry, 0xc0ffee00 + target as u64], &mut tee);
        if end != "returned" {
            return end;
        }
        let r = w.result(core, 4);
        let asked = w.m.soc.power.iter().rev().find(|p| p.0 as usize == target).map(|p| p.1 & 0xf);
        if w.m.cpus[core].x[0] != 0 || asked != Some(8) {
            return format!("{r}; core {target}'s power policy: {asked:?}");
        }
        let s = w.start(target, &mut tee);
        for (i, t) in tee.iter().enumerate() {
            p.put(format!("{prefix}OP-TEE entered ({})", i + 1), t.clone());
        }
        format!("{r}; {s}")
    });
}

/// An SMC on another core than the phase's.
fn smc_on(q: &mut Q, core: usize, what: &str, args: &[u64]) {
    let args = args.to_vec();
    let label = format!("core {core}: {what} {}", args.iter().map(|a| format!("{a:#x}")).collect::<Vec<_>>().join(" "));
    q.ask(&label, &move |w, p, prefix| {
        let mut tee = Vec::new();
        let end = w.smc(core, &args, &mut tee);
        for (i, t) in tee.iter().enumerate() {
            p.put(format!("{prefix}OP-TEE entered ({})", i + 1), t.clone());
        }
        if end == "returned" {
            w.result_at(core, args.len().max(4))
        } else if end == "wfi" {
            w.off[core] = true;
            "does not return: the core waits in EL3 (wfi)".into()
        } else {
            end
        }
    });
}

fn cores(q: &mut Q) {
    let all: Vec<usize> = (0..soc::CORES).filter(|c| *c != q.core).collect();
    for &c in &all {
        cpu_on(q, c, NS_ENTRY + 0x1000 * c as u64);
    }
    for c in 0..12u64 {
        q.smc(&[AFFINITY_INFO, c << 8, 0]);
    }
    // a core that is on already, and one being asked twice
    q.smc(&[CPU_ON, 0x100, NS_ENTRY, 0]);
    q.smc(&[CPU_ON, 0xa00, NS_ENTRY, 0]);
    // each core answers for itself
    for &c in &all {
        smc_on(q, c, "SMC", &[PSCI_VERSION]);
        smc_on(q, c, "SMC", &[AFFINITY_INFO, (c as u64) << 8, 0]);
        smc_on(q, c, "SMC", &[0xc200_0005]);
    }
    // off, seen off, on again at another address
    for &c in &all {
        smc_on(q, c, "CPU_OFF", &[CPU_OFF]);
        q.smc(&[AFFINITY_INFO, (c as u64) << 8, 0]);
    }
    q.smc(&[AFFINITY_INFO, 0, 1]);
    q.smc(&[AFFINITY_INFO, 0, 2]);
    for &c in all.iter().rev() {
        cpu_on(q, c, NS_ENTRY + 0x10_0000 + 0x1000 * c as u64);
    }
    // the boot core goes off and another brings it back
    smc_on(q, 10, "CPU_OFF", &[CPU_OFF]);
    q.core = 4;
    q.smc(&[AFFINITY_INFO, 0xa00, 0]);
    cpu_on(q, 10, NS_ENTRY + 0x20_0000);
    q.smc(&[AFFINITY_INFO, 0xa00, 0]);
    // a core the eFuses leave out
    q.core = 10;
    smc_on(q, 3, "CPU_OFF", &[CPU_OFF]);
    q.each(&|w| {
        let v = w.m.soc.reg(0x1600_0508);
        w.m.soc.set_reg(0x1600_0508, v | 8);
    });
    q.smc(&[CPU_ON, 0x300, NS_ENTRY, 0]);
}

fn cpu_suspend(q: &mut Q) {
    cpu_on(q, 4, NS_ENTRY + 0x4000);
    cpu_on(q, 0, NS_ENTRY);
    for core in [10usize, 4, 0] {
        // standby: the core waits and goes on where it was
        let label = format!("core {core}: CPU_SUSPEND standby, then an interrupt");
        q.ask(&label, &move |w, _p, _prefix| {
            let mut tee = Vec::new();
            let end = w.smc(core, &[CPU_SUSPEND, 0, 0, 0], &mut tee);
            if end != "wfi" {
                return format!("{end}: {}", w.result_at(core, 4));
            }
            let end = w.resume(core, &mut tee);
            format!("waits; then {end}: {}", w.result_at(core, 4))
        });
        // power down at each level: the core waits, and comes back through its reset vector
        for state in [0x1_0000u64, 0x101_0000, 0x201_0000] {
            let label = format!("core {core}: CPU_SUSPEND {state:#x} (power down), then a wake-up");
            q.ask(&label, &move |w, p, prefix| {
                let mut tee = Vec::new();
                let end = w.smc(core, &[CPU_SUSPEND, state, NS_ENTRY + 0x8000 + 0x100 * core as u64, 0x5a5a + core as u64], &mut tee);
                if end != "wfi" {
                    return format!("{end}: {}", w.result_at(core, 4));
                }
                let mut parts = Phase::new("");
                w.drain(&mut parts, "going down: ");
                for (k, v) in parts.items {
                    p.put(format!("{prefix}{k}"), v);
                }
                let s = w.start(core, &mut tee);
                for (i, t) in tee.iter().enumerate() {
                    p.put(format!("{prefix}OP-TEE entered ({})", i + 1), t.clone());
                }
                format!("waits; then {s}")
            });
        }
        smc_on(q, core, "SMC", &[PSCI_VERSION]);
    }
    // with the hypervisor using the activity monitors' virtual offsets (HCR_EL2.AMVOFFEN): saved and restored
    q.each(&|w| {
        let c = &mut w.m.cpus[4];
        let hcr = super::cpu::key(3, 4, 1, 1, 0);
        let v = c.sysreg(hcr);
        c.sys.insert(hcr, v | (1 << 51));
        for i in 0..4 {
            c.sys.insert(super::cpu::key(3, 4, 13, 8, i), 0x1000 + i as u64);
            c.sys.insert(super::cpu::key(3, 3, 13, 4, i), 0x2000 + i as u64);
        }
        for i in 0..3 {
            c.sys.insert(super::cpu::key(3, 4, 13, 10, i), 0x3000 + i as u64);
            c.sys.insert(super::cpu::key(3, 3, 13, 12, i), 0x4000 + i as u64);
        }
    });
    let label = "core 4: CPU_SUSPEND 0x10000 with AMU virtual offsets in use, then a wake-up";
    q.ask(label, &|w, _p, _prefix| {
        let mut tee = Vec::new();
        let end = w.smc(4, &[CPU_SUSPEND, 0x1_0000, NS_ENTRY + 0x9000, 0x77], &mut tee);
        if end != "wfi" {
            return format!("{end}: {}", w.result_at(4, 4));
        }
        format!("waits; then {}", w.start(4, &mut tee))
    });
    // CPU_DEFAULT_SUSPEND and the 32-bit call
    smc_on(q, 4, "SMC", &[0xc400_000c, NS_ENTRY, 0]);
    smc_on(q, 4, "SMC", &[0x8400_0001, 0, 0, 0]);
}

/// SYSTEM_SUSPEND on the phase's core, the wake-up and the resume.
fn suspend_resume(q: &mut Q, what: &str) {
    let core = q.core;
    q.ask(&format!("SYSTEM_SUSPEND{what}"), &move |w, p, prefix| {
        let mut tee = Vec::new();
        let end = w.smc(core, &[SYSTEM_SUSPEND, NS_ENTRY + 0x7000, 0x5057], &mut tee);
        if end != "wfi" {
            return format!("{end}: {}", w.result_at(core, 4));
        }
        let mut parts = Phase::new("");
        w.drain(&mut parts, "going down: ");
        for (k, v) in parts.items {
            p.put(format!("{prefix}{k}"), v);
        }
        // the address BL31 left for the SE to resume the AP at, against the reset vector's
        let left = w.m.soc.reg(0x1600_051c) as u64;
        let rvb = w.m.soc.reg(soc::CPU_RVB + 8 * core as u64) as u64;
        // (the SE and the PM bring the SoC back: the time that passed, the counter going on)
        w.m.soc.time += 3_000_000_000;
        let s = w.start(core, &mut tee);
        for (i, t) in tee.iter().enumerate() {
            p.put(format!("{prefix}OP-TEE entered ({})", i + 1), t.clone());
        }
        format!("the system sleeps (resume address {} the reset vector's); then {s}", if left == rvb { "is" } else { "is not" })
    });
}

fn system_suspend(q: &mut Q) {
    // refused while another core is on
    cpu_on(q, 4, NS_ENTRY + 0x4000);
    q.smc(&[SYSTEM_SUSPEND, NS_ENTRY, 0]);
    smc_on(q, 4, "CPU_OFF", &[CPU_OFF]);
    suspend_resume(q, "");
    q.smc(&[PSCI_VERSION]);
    q.smc(&[0xc200_0005]);
    cpu_on(q, 4, NS_ENTRY + 0x4000);
    smc_on(q, 4, "CPU_OFF", &[CPU_OFF]);
    // a second time
    suspend_resume(q, " (again)");
    // with the audio domain's resume pending: its power state machine answers, then its mailbox too
    q.each(&|w| w.m.soc.set_reg(0x0700_0020, (1 << 23) | (3 << 12)));
    suspend_resume(q, " (audio resume pending)");
    q.each(&|w| {
        w.m.soc.set_reg(0x0700_0020, (1 << 23) | (3 << 12));
        w.m.soc.set_reg(0x0711_0004, 0x2022_0000);
        w.m.soc.set_reg(0x1600_0404, 2);
    });
    suspend_resume(q, " (audio resume pending, its mailbox there)");
    q.each(&|w| w.m.soc.set_reg(0x0700_0020, 1 << 23));
    suspend_resume(q, " (audio resume pending, the domain does not power up)");
    // from another core
    q.each(&|w| w.m.soc.set_reg(0x0700_0020, 0));
    cpu_on(q, 0, NS_ENTRY);
    smc_on(q, 10, "CPU_OFF", &[CPU_OFF]);
    q.core = 0;
    suspend_resume(q, " (from core 0)");
    q.smc(&[PSCI_VERSION]);
}

fn system_off(q: &mut Q) {
    for (name, args) in [("SYSTEM_OFF", vec![SYSTEM_OFF]), ("SYSTEM_RESET", vec![SYSTEM_RESET]), ("SYSTEM_RESET2 (warm)", vec![SYSTEM_RESET2, 0, 0]), ("SYSTEM_RESET2 (vendor)", vec![SYSTEM_RESET2, 0x8000_0001, 7]), ("SYSTEM_RESET2 (32-bit)", vec![0x8400_0012, 0, 0])] {
        let core = q.core;
        q.ask(name, &move |w, _p, _prefix| {
            let mut tee = Vec::new();
            let end = w.smc(core, &args, &mut tee);
            if end == "returned" {
                w.result(core, 4)
            } else if end == "wfi" {
                // (reported as an answer of its own, so that both go back as they were)
                "died: does not return, the core waits in EL3 (wfi)".into()
            } else {
                end
            }
        });
    }
    // with the OS's memory registered for the cache flush before the power goes (the debug commands)
    for cmd in 0..0x80u64 {
        q.smc(&[0xc200_0012, cmd, 0x9000_0000, 0x2000, 1]);
    }
    for (name, f) in [("SYSTEM_OFF with the OS's memory registered", SYSTEM_OFF), ("SYSTEM_RESET with the OS's memory registered", SYSTEM_RESET)] {
        let core = q.core;
        q.ask(name, &move |w, _p, _prefix| {
            let mut tee = Vec::new();
            let end = w.smc(core, &[f], &mut tee);
            if end == "wfi" {
                "died: does not return, the core waits in EL3 (wfi)".into()
            } else {
                format!("{end}: {}", w.result_at(core, 4))
            }
        });
    }
    // with other cores on
    cpu_on(q, 4, NS_ENTRY + 0x4000);
    cpu_on(q, 0, NS_ENTRY);
    for (name, f) in [("SYSTEM_OFF with cores 0 and 4 on", SYSTEM_OFF), ("SYSTEM_RESET with cores 0 and 4 on", SYSTEM_RESET)] {
        q.ask(name, &move |w, _p, _prefix| {
            let mut tee = Vec::new();
            let end = w.smc(4, &[f], &mut tee);
            if end == "wfi" {
                "died: does not return, the core waits in EL3 (wfi)".into()
            } else {
                format!("{end}: {}", w.result_at(4, 4))
            }
        });
    }
    // without the PM
    q.each(&|w| w.m.soc.pm_alive = false);
    q.smc_as("the PM does not answer: SMC", &[0xc200_0005]);
}

fn sdei(q: &mut Q) {
    let s = |n: u64| 0xc400_0020 + n;
    q.smc(&[s(0)]); // VERSION
    for f in 0..0x14u64 {
        q.smc(&[s(0x10), f]); // FEATURES
    }
    for ev in [0u64, 100, 150, 151, 153, 157, 161, 173, 200, 207, 210, 213, 0xfe, 0xff, 5, 1000, 0x4000_0000] {
        for info in 0..5u64 {
            q.smc(&[s(9), ev, info]); // EVENT_GET_INFO
        }
        q.smc(&[s(8), ev]); // EVENT_STATUS
    }
    q.smc(&[s(0xb)]); // PE_MASK
    q.smc(&[s(0xc)]); // PE_UNMASK
    // register, enable, signal, complete
    for ev in [0u64, 100, 0xff, 0xfe, 210, 150, 200, 5] {
        q.smc(&[s(1), ev, NS_ENTRY + 0x100, 0xa5a5, 0, 0]); // EVENT_REGISTER
        q.smc(&[s(8), ev]);
        q.smc(&[s(2), ev]); // EVENT_ENABLE
        q.smc(&[s(8), ev]);
    }
    q.smc(&[s(1), 0, NS_ENTRY + 0x100, 0xa5a5, 0, 0]);
    q.smc(&[s(1), 100, 0x8020_0000, 0xa5a5, 0, 0]);
    q.smc(&[s(1), 100, NS_ENTRY, 0, 1, 0xa00]);
    q.smc(&[s(0xa), 100, 1, 0x100]); // ROUTING_SET
    q.smc(&[s(0xf), 0xa00]); // EVENT_SIGNAL of event 0 to this core
    q.smc(&[s(4), 0]); // EVENT_CONTEXT
    q.smc(&[s(4), 17]);
    q.smc(&[s(4), 18]);
    q.smc(&[s(5), 0]); // EVENT_COMPLETE
    q.smc(&[s(6), NS_ENTRY + 0x200]); // COMPLETE_AND_RESUME
    q.smc(&[s(0xf), 0x100]);
    q.smc(&[s(0xf), 0xffff]);
    // bind an interrupt, and what cannot be bound
    for intr in [50u64, 408, 33, 16, 8, 1020, 5000] {
        q.smc(&[s(0xd), intr]); // INTERRUPT_BIND
    }
    q.smc(&[s(0xe), 0x4000_0000]); // INTERRUPT_RELEASE
    for ev in [0u64, 100, 0xff, 210] {
        q.smc(&[s(3), ev]); // EVENT_DISABLE
        q.smc(&[s(7), ev]); // EVENT_UNREGISTER
        q.smc(&[s(8), ev]);
    }
    q.smc(&[s(0x11)]); // PRIVATE_RESET
    q.smc(&[s(0x12)]); // SHARED_RESET
    q.smc(&[s(0x13)]);
    q.smc(&[0x8400_0020]);
}

/// An interrupt of group 0 while the normal world runs, and what the world sees after.
fn fiq(q: &mut Q, what: &str, intid: u32) {
    let core = q.core;
    q.ask(&format!("interrupt {intid}{what}"), &move |w, p, prefix| {
        let mut tee = Vec::new();
        let end = w.interrupt(core, intid, &mut tee);
        for (i, t) in tee.iter().enumerate() {
            p.put(format!("{prefix}OP-TEE entered ({})", i + 1), t.clone());
        }
        if end == "returned" {
            w.result_at(core, 0)
        } else {
            end
        }
    });
}

fn interrupts(q: &mut Q) {
    let s = |n: u64| 0xc400_0020 + n;
    // nothing registered: every interrupt BL31 owns, and ones it does not
    for id in [9u32, 8, 15, 32, 33, 35, 39, 43, 55, 93, 94, 97, 98, 101, 102, 104, 105, 241, 244, 247, 250, 408, 1023, 1022, 600, 27] {
        fiq(q, "", id);
    }
    // the watchdog's event registered and enabled: the interrupt becomes the event
    q.smc(&[s(1), 100, NS_ENTRY + 0x100, 0xa5a5, 0, 0]);
    q.smc(&[s(2), 100]);
    q.smc(&[s(0xc)]);
    fiq(q, " (the watchdog's, its SDEI event enabled)", 408);
    q.smc(&[s(4), 0]);
    q.smc(&[s(4), 1]);
    q.smc(&[s(5), 0]);
    fiq(q, " (the watchdog's, again)", 408);
    q.smc(&[s(6), NS_ENTRY + 0x300]);
    // the firmware's own exception events, when the OS has a handler for them
    for ev in [0xffu64, 0xfe] {
        q.smc(&[s(1), ev, NS_ENTRY + 0x400, 0x77, 0, 0]);
        q.smc(&[s(2), ev]);
    }
    // synchronous exceptions from the normal world that are not SMCs
    for (name, esr) in [("a trapped system register access", 0x6200_0000u64), ("an SVE access", 0x6600_0000), ("an unknown reason", 0x0200_0000), ("an SMC from AArch32", 0x4e00_0000), ("a data abort", 0x9200_0010), ("an HVC", 0x5a00_0000)] {
        let core = q.core;
        q.ask(&format!("exception to EL3: {name}"), &move |w, _p, _prefix| {
            w.enter(core, 0x400, esr, &[]);
            let mut tee = Vec::new();
            let end = w.resume(core, &mut tee);
            if end == "returned" {
                w.result_at(core, 0)
            } else {
                end
            }
        });
    }
    // the same with SP_EL0 a place the normal world has (BL31's panic handler runs on SP_EL0 as the
    // interrupted level left it): the handler gets as far as its SDEI event for the OS, which the OS
    // completes; then BL31 goes on to its end
    let core = q.core;
    q.each(&move |w| w.m.cpus[core].sp[0] = 0x9300_0000);
    q.ask("exception to EL3 with SP_EL0 in the normal world's memory: a trapped system register access", &move |w, _p, _prefix| {
        w.enter(core, 0x400, 0x6200_0000, &[]);
        let mut tee = Vec::new();
        let end = w.resume(core, &mut tee);
        if end != "returned" {
            return end;
        }
        let first = w.result_at(core, 0);
        let end = w.smc(core, &[0xc400_0025, 0], &mut tee);
        format!("the OS's handler entered: {first}; its SDEI_EVENT_COMPLETE: {end}")
    });
    // an SError and an IRQ routed to EL3
    for (name, vector) in [("SError", 0x580u64), ("IRQ", 0x480)] {
        let core = q.core;
        q.ask(&format!("exception to EL3: {name}"), &move |w, _p, _prefix| {
            w.enter(core, vector, 0xbe00_0000, &[]);
            let mut tee = Vec::new();
            let end = w.resume(core, &mut tee);
            if end == "returned" {
                w.result_at(core, 0)
            } else {
                end
            }
        });
    }
}

/// CIX_SIP_NOR_STORAGE: x1 = command | address << 32, x2 = address bytes | direction << 32, x3 = bus
/// format | dummy cycles << 32, x4 = buffer | length << 32.
fn nor_call(q: &mut Q, what: &str, cmd: u64, addr: u64, abytes: u64, dir: u64, fmt: u64, dummy: u64, buf: u64, len: u64) {
    let core = q.core;
    let label = format!("NOR {what}: command {cmd:#x} at {addr:#x}/{abytes}, direction {dir}, format {fmt}, {dummy} dummy, buffer {buf:#x}+{len:#x}");
    q.ask(&label, &move |w, _p, _prefix| {
        // the buffer as the caller prepared it
        if buf >= 0x8250_0000 && buf + len < w.m.soc.ram.end && len <= 0x1000 {
            let fill: Vec<u8> = (0..len).map(|i| (i as u8) ^ 0x5a).collect();
            w.m.soc.ram.load(buf, &fill);
        }
        let mut tee = Vec::new();
        let end = w.smc(core, &[0xc200_000b, cmd | (addr << 32), abytes | (dir << 32), fmt | (dummy << 32), buf | (len << 32)], &mut tee);
        if end != "returned" {
            return end;
        }
        let data = if buf >= 0x8000_0000 && buf + len < w.m.soc.ram.end && len > 0 { crate::util::hex(&w.m.soc.ram.bytes(buf, len.min(24) as usize)) } else { String::new() };
        format!("{}; buffer now {data}", w.result(core, 5))
    });
}

fn nor(q: &mut Q) {
    let buf = 0x9100_0000u64;
    // data direction: 1 in (flash to memory), 2 out
    nor_call(q, "JEDEC ID", 0x9f, 0, 0, 1, 1, 0, buf, 3);
    nor_call(q, "status", 0x05, 0, 0, 1, 1, 0, buf, 1);
    nor_call(q, "read", 0x03, 0x10_0000, 3, 1, 1, 0, buf, 0x40);
    nor_call(q, "fast read", 0x0b, 0x10_0000, 3, 1, 1, 8, buf, 0x40);
    for fmt in 0..9u64 {
        nor_call(q, "read in each bus format", 0x6b, 0x18_8000, 3, 1, fmt, 8, buf, 0x10);
    }
    nor_call(q, "read across 2 MiB", 0x03, 0x1f_fff0, 3, 1, 1, 0, buf, 0x20);
    nor_call(q, "read of nothing", 0x03, 0x10_0000, 3, 1, 1, 0, buf, 0);
    nor_call(q, "read without an address", 0x03, 0x10_0000, 0, 1, 1, 0, buf, 4);
    // write enable, erase, program, read back: the variable store's area
    nor_call(q, "write enable", 0x06, 0, 0, 0, 1, 0, 0, 0);
    nor_call(q, "sector erase", 0x20, 0x7f_0000, 3, 0, 1, 0, 0, 0);
    nor_call(q, "read erased", 0x03, 0x7f_0000, 3, 1, 1, 0, buf, 0x10);
    nor_call(q, "write enable", 0x06, 0, 0, 0, 1, 0, 0, 0);
    nor_call(q, "program", 0x02, 0x7f_0000, 3, 2, 1, 0, buf, 0x10);
    nor_call(q, "read programmed", 0x03, 0x7f_0000, 3, 1, 1, 0, buf + 0x100, 0x10);
    // buffers that are not the normal world's: BL31, OP-TEE, the shared pages, devices, the end of DRAM
    for (what, b, len) in [
        ("into BL31", 0x8020_0000u64, 0x10u64),
        ("into OP-TEE", 0x8050_0000, 0x10),
        ("into the TrustZone configuration page", 0x8040_0000, 0x10),
        ("into the last secure page", 0x824f_fff0, 0x10),
        ("across the secure boundary", 0x824f_fff8, 0x10),
        ("into a device", 0x0e01_0000, 4),
        ("into BL2's old place", 0x8000_0000, 0x10),
        ("at the end of DRAM", 0x4_7fff_fff0, 0x10),
        ("past the end of DRAM", 0x4_7fff_fff8, 0x10),
    ] {
        nor_call(q, &format!("outside the caller's memory, read {what}"), 0x03, 0x10_0000, 3, 1, 1, 0, b & 0xffff_ffff, len);
    }
    nor_call(q, "outside the caller's memory, program from BL31's memory", 0x02, 0x7f_1000, 3, 2, 1, 0, 0x8020_0000, 0x10);
    nor_call(q, "outside the caller's memory, program from OP-TEE's memory", 0x02, 0x7f_1000, 3, 2, 1, 0, 0x8050_0000, 0x10);
    nor_call(q, "outside the caller's memory, read back what the programs from secure memory left", 0x03, 0x7f_1000, 3, 1, 1, 0, buf, 0x10);
    nor_call(q, "the first page above the secure memory", 0x03, 0x10_0000, 3, 1, 1, 0, 0x8250_0000, 0x10);
    nor_call(q, "a length beyond the controller's", 0x03, 0, 3, 1, 1, 0, buf, 0x100_0000);
}

/// An SCMI message in the normal world's shared memory (0x84380000), then the SiP call that has BL31 take it.
fn scmi_msg(q: &mut Q, proto: u32, id: u32, payload: &[u32]) {
    let core = q.core;
    let payload = payload.to_vec();
    let label = format!("SCMI {proto:#x}/{id}[{}]", payload.iter().map(|x| format!("{x:#x}")).collect::<Vec<_>>().join(" "));
    q.ask(&label, &move |w, _p, _prefix| {
        let shm = 0x8438_0000u64;
        let mut m = vec![0u32; 7 + payload.len()];
        m[1] = 0; // channel busy (the agent owns it)
        m[5] = 4 + 4 * payload.len() as u32;
        m[6] = (proto << 10) | id | (0x155 << 18);
        m[7..].copy_from_slice(&payload);
        let bytes: Vec<u8> = m.iter().flat_map(|x| x.to_le_bytes()).collect();
        w.m.soc.ram.load(shm, &bytes);
        let mut tee = Vec::new();
        let end = w.smc(core, &[0xc200_0001], &mut tee);
        if end != "returned" {
            return end;
        }
        let r = |i: u64| w.m.soc.ram.read(shm + 4 * i, 4) as u32;
        let len = r(5);
        let n = (len.saturating_sub(4) / 4).min(40) as u64;
        let back: Vec<String> = (0..n).map(|i| format!("{:#x}", r(7 + i))).collect();
        format!("x0 {:#x}; channel status {:#x}, header {:#x}, length {len}, answer [{}]", w.m.cpus[core].x[0], r(1), r(6), back.join(" "))
    });
}

fn scmi(q: &mut Q) {
    // base protocol
    for id in 0..12u32 {
        scmi_msg(q, 0x10, id, &[]);
    }
    scmi_msg(q, 0x10, 2, &[0]);
    scmi_msg(q, 0x10, 2, &[6]);
    scmi_msg(q, 0x10, 2, &[99]);
    scmi_msg(q, 0x10, 6, &[0]);
    scmi_msg(q, 0x10, 6, &[1]);
    scmi_msg(q, 0x10, 7, &[0]);
    scmi_msg(q, 0x10, 7, &[5]);
    scmi_msg(q, 0x10, 7, &[0xffff_ffff]);
    // power domains: discovery, then every domain's attributes, state read, on, read, off, read
    for id in 0..10u32 {
        scmi_msg(q, 0x11, id, &[]);
    }
    for id in 0..9u32 {
        scmi_msg(q, 0x11, 2, &[id]);
    }
    for d in 0..42u32 {
        scmi_msg(q, 0x11, 3, &[d]);
        scmi_msg(q, 0x11, 5, &[d]);
        scmi_msg(q, 0x11, 4, &[0, d, 0]);
        scmi_msg(q, 0x11, 5, &[d]);
        scmi_msg(q, 0x11, 4, &[0, d, 0x4000_0000]);
        scmi_msg(q, 0x11, 5, &[d]);
    }
    // on twice, asynchronous, states that do not exist, domains that do not exist
    scmi_msg(q, 0x11, 4, &[0, 3, 0]);
    scmi_msg(q, 0x11, 4, &[0, 3, 0]);
    scmi_msg(q, 0x11, 4, &[1, 3, 0]);
    scmi_msg(q, 0x11, 4, &[0, 3, 1]);
    scmi_msg(q, 0x11, 4, &[0, 3, 0x8000_0000]);
    scmi_msg(q, 0x11, 4, &[0, 0xffff_ffff, 0]);
    scmi_msg(q, 0x11, 4, &[0, 1000, 0x4000_0000]);
    scmi_msg(q, 0x11, 5, &[1000]);
    scmi_msg(q, 0x11, 3, &[1000]);
    scmi_msg(q, 0x11, 4, &[0, 3]);
    scmi_msg(q, 0x11, 4, &[]);
    scmi_msg(q, 0x11, 6, &[3, 1]);
    scmi_msg(q, 0x11, 7, &[3, 1]);
    // protocols BL31 does not serve
    for proto in [0x12u32, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x80, 0x90, 0xff, 0x0f, 0] {
        scmi_msg(q, proto, 0, &[]);
        scmi_msg(q, proto, 1, &[]);
        scmi_msg(q, proto, 3, &[0]);
    }
    // the clock and reset protocols' handlers are in the image though BL31 lists neither
    for proto in [0x14u32, 0x16] {
        for id in 0..12u32 {
            scmi_msg(q, proto, id, &[]);
            scmi_msg(q, proto, id, &[0]);
            scmi_msg(q, proto, id, &[0, 1]);
            scmi_msg(q, proto, id, &[0, 1, 2]);
            scmi_msg(q, proto, id, &[0, 0, 1000, 0]);
        }
    }
    // a message that is not one: lengths the header does not fit
    let core = q.core;
    for (what, len, status) in [("a length of 0", 0u32, 0u32), ("a length of 3", 3, 0), ("a length beyond the area", 0x1000, 0), ("a length of 0xffffffff", 0xffff_ffff, 0), ("the channel marked free", 8, 1), ("the channel in error", 8, 2)] {
        q.ask(&format!("SCMI message with {what}"), &move |w, _p, _prefix| {
            let shm = 0x8438_0000u64;
            let m: [u32; 8] = [0, status, 0, 0, 0, len, (0x10 << 10) | (0x2aa << 18), 0];
            let bytes: Vec<u8> = m.iter().flat_map(|x| x.to_le_bytes()).collect();
            w.m.soc.ram.load(shm, &bytes);
            let mut tee = Vec::new();
            let end = w.smc(core, &[0xc200_0001], &mut tee);
            if end != "returned" {
                return end;
            }
            let r = |i: u64| w.m.soc.ram.read(shm + 4 * i, 4) as u32;
            format!("x0 {:#x}; channel status {:#x}, header {:#x}, length {}, answer [{:#x} {:#x}]", w.m.cpus[core].x[0], r(1), r(6), r(5), r(7), r(8))
        });
    }
}

/// The power domains with the SE's "memory repair valid" bits set (SW_USED_REG1), their RCSUs
/// answering that the repair is done; then the PDC's wake-up sources by SiP call.
fn repair(q: &mut Q) {
    q.each(&|w| {
        w.m.soc.set_reg(0x1600_0504, 0x00ff_ffff);
        // REPAIR_ALL_DONE_PASS (0x10): done and passed; GROUP_REPAIR_DONE_BUSY (0x18): not busy
        for base in [0x0700_0000u64, 0x0a00_0000, 0x0b00_0000, 0x0b1a_0000, 0x0d00_0000, 0x0d03_0000, 0x1400_0000, 0x1407_0000, 0x140e_0000, 0x1415_0000, 0x141c_0000, 0x1423_0000, 0x1425_0000, 0x1433_0000, 0x1500_0000] {
            w.m.soc.set_reg(base + 0x10, 6);
        }
    });
    for d in 0..42u32 {
        scmi_msg(q, 0x11, 4, &[0, d, 0]);
        scmi_msg(q, 0x11, 5, &[d]);
    }
    for d in (0..42u32).rev() {
        scmi_msg(q, 0x11, 4, &[0, d, 0x4000_0000]);
    }
    for id in 0..40u64 {
        q.smc(&[0xc200_000a, id, 1]);
    }
    // SKY1_SIP_CONFIG_GPD_SET_WAKE (2): every interrupt the PDC knows and its neighbours, on and off
    let irqs: Vec<u64> = (238..284u64).chain(370..377).chain([0, 599, 600, 0xffff_ffff]).collect();
    for on in [1u64, 0] {
        for &irq in &irqs {
            q.smc(&[0xc200_0009, 2, irq, on, 0x8000_0000 | irq]);
        }
    }
    q.smc(&[0xc200_0009, 2, 372, 1, 0x10]);
    q.smc(&[0xc200_0009, 2, 373, 1, 0x20]);
    q.smc(&[0xc200_0009, 2, 374, 1, 0x40]);
    // what a suspend then arms
    smc_on(q, 10, "SMC", &[SYSTEM_SUSPEND, 0x8020_0000, 0]);
    suspend_resume(q, " with the wake-up sources set");
}

fn more_interrupts(q: &mut Q) {
    let s = |n: u64| 0xc400_0020 + n;
    let core = q.core;
    // a secure interrupt of OP-TEE's while the normal world runs: BL31 enters OP-TEE for it
    q.ask("an interrupt for OP-TEE (secure group 1) in the normal world", &move |w, p, prefix| {
        w.m.soc.pending_s[core] = true;
        w.enter(core, 0x500, 0, &[]);
        let mut tee = Vec::new();
        let end = w.resume(core, &mut tee);
        w.m.soc.pending_s[core] = false;
        for (i, t) in tee.iter().enumerate() {
            p.put(format!("{prefix}OP-TEE entered ({})", i + 1), t.clone());
        }
        if end == "returned" {
            w.result_at(core, 0)
        } else {
            end
        }
    });
    // an SError that EL3 finds deferred at its entry (the ESB there): with each error record the cores have
    for (what, status, addr, misc) in [("nothing in the record", 0u64, 0u64, 0u64), ("an uncorrected error", 0xe400_0000u64, 0x9000_1000, 0x55), ("a corrected error", 0x8600_0000, 0x9000_2000, 0x1), ("a deferred error", 0x8080_0000, 0x9000_3000, 0)] {
        q.ask(&format!("an SMC with an SError pending ({what})"), &move |w, _p, _prefix| {
            let c = &mut w.m.cpus[core];
            c.serror = Some(0x11);
            c.sys.insert(super::cpu::key(3, 0, 5, 4, 2), status);
            c.sys.insert(super::cpu::key(3, 0, 5, 4, 3), addr);
            c.sys.insert(super::cpu::key(3, 0, 5, 5, 0), misc);
            let mut tee = Vec::new();
            let end = w.smc(core, &[PSCI_VERSION], &mut tee);
            let c = &w.m.cpus[core];
            let left = format!("DISR_EL1 {:#x}, ERXSTATUS_EL1 {:#x}", c.sysreg(super::cpu::key(3, 0, 12, 1, 1)), c.sysreg(super::cpu::key(3, 0, 5, 4, 2)));
            if end == "returned" {
                format!("{}; {left}", w.result(core, 4))
            } else {
                format!("{end}; {left}")
            }
        });
    }
    // the watchdog's event while the OS has events masked, then unmasked
    q.smc(&[s(1), 100, NS_ENTRY + 0x100, 0xa5a5, 0, 0]);
    q.smc(&[s(2), 100]);
    q.smc(&[s(0xb)]);
    fiq(q, " (the watchdog's, events masked on the core)", 408);
    q.smc(&[s(0xc)]);
    fiq(q, " (the watchdog's, events unmasked)", 408);
    q.smc(&[s(5), 0]);
    // event 0 signalled to this core and to another
    q.smc(&[s(1), 0, NS_ENTRY + 0x500, 0x11, 0, 0]);
    q.smc(&[s(2), 0]);
    q.smc(&[s(0xf), 0, 0xa00]);
    q.smc(&[s(0xf), 0, 0x400]);
    q.smc(&[s(0xf), 1, 0xa00]);
    fiq(q, " (the SGI of event 0)", 9);
    q.smc(&[s(5), 0]);
}
