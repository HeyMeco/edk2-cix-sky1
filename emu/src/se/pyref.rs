// SPDX-License-Identifier: BSD-2-Clause-Patent
//! The differential test of the open SE firmware against CIX's debug build
//! as the Python test this replaces (scripts/open/test_se.py) made it, on
//! the emulation of emu.rs: `se-test --pyref`.  Kept to show that the port
//! reproduces the Python reference to the write (`--dump` writes the same
//! trace files); the timed tests in test.rs go further.
//!
//! Both run on the same Sky1 model and flash image, from reset until the
//! debug service (after BL2 is started), with the DDR driver's entry points
//! stubbed, or with --ddr the DDR driver running on the model of the DDR
//! controllers, PIs and PHYs, and the memory size and channel count it
//! reports compared too.  Compared:
//!
//! * register writes, per register in order (UART data, postcodes and the
//!   log-level scratch register excepted);
//! * the final contents of what the SE writes for later stages: DRAM (the
//!   SYSHUB window: key blocks, BL2, boot records) and the PM's SRAM.

use super::emu::{constant, Se};
use super::soc::{DdrModel, SkySoc, BL1_HEADER, EFUSE, LIFE_CYCLE};
use crate::arm::{Arm, Hook, Stub};
use crate::util::{hkdf_sha256, sha256, u32le, Args};
use std::collections::BTreeMap;
use std::rc::Rc;

pub fn ddr_stubs() -> Vec<(&'static str, Stub<SkySoc>)> {
    vec![
        ("drv_ddr_init", constant(0)),
        ("drv_ddr_postci", constant(0)),
        ("drv_ddr_get_mem_size", constant(0x4000)),
        ("drv_ddr_get_ch_num", constant(4)),
        ("MemGetBootEnv", constant(0)),
    ]
}

/// CIX's fw_km_derive_key without the crypto engine's hash: HKDF-SHA256 of the eFuse key.
pub fn cix_derive_key() -> Stub<SkySoc> {
    Rc::new(|cpu: &mut Arm<SkySoc>| {
        let (src, dst, key, plen) = (cpu.r[0], cpu.r[1], cpu.r[2], cpu.r[3]);
        let lc = cpu.read32(LIFE_CYCLE) & 7;
        let fuse = match (src, lc == 7) {
            (1, true) => 0x64,
            (1, false) => 0xc4,
            (_, true) => 0x54,
            (_, false) => 0xb4,
        };
        let n: u32 = if dst == 3 { 16 } else { 32 };
        cpu.write32(plen, n);
        let k = hkdf_sha256(&cpu.read_bytes(EFUSE + fuse, 16), n as usize);
        cpu.write_bytes(key, &k);
        Some(0)
    })
}

pub const IGNORE: [u32; 7] = [0xa0010000, 0xa0020f00, 0xb0030208, 0xb003020c, 0xe000e010, 0xe000e014, 0xe000e018];
/// RAM log (the messages differ), firmware version string
pub const IGNORE_RANGES: [(u32, u32); 2] = [(0x63da0000, 0x63da4000), (0x63e01000, 0x63e01040)];
pub const MEMORY: [(u32, u32, &str); 2] = [(0x60000000, 0x80000000, "DRAM window"), (0x46a10000, 0x46b18000, "PM SRAM")];
/// --ddr: CIX's memory-configuration write-back (drv_memcfg_flash_write, after the DDR driver updates a
/// block) uses the boot ROM's copy of the bootloader1 header in SRAM as its sector buffer, so CIX's
/// firmware reads the header from flash again before loading BL2; the open one does not.  The QSPI
/// controller's registers are left out and the flash contents compared instead.
pub const DDR_IGNORE_RANGES: [(u32, u32); 1] = [(0xe0044000, 0xe0044100)];

pub fn is_memory(addr: u32) -> bool {
    MEMORY.iter().any(|(lo, hi, _)| *lo <= addr && addr < *hi)
}

/// eFuse with test device and model keys and, for secure boot, the image's OEM and CIX key hashes and
/// the secure-boot bit (life cycle 7).
pub fn keys_efuse(flash: &[u8], secure: bool) -> BTreeMap<u32, Vec<u8>> {
    let mut fuses = BTreeMap::from([
        (0xb4, (0x10..0x20).collect::<Vec<u8>>()),
        (0xc4, (0x40..0x50).collect()),
        (0x54, (0x60..0x70).collect()),
        (0x64, (0x80..0x90).collect()),
    ]);
    if secure {
        let hdr = &flash[BL1_HEADER..BL1_HEADER + 0x1000];
        let (oem_len, cix_len) = (u32le(hdr, 0x18) as usize, u32le(hdr, 0x320) as usize);
        fuses.insert(0x188, sha256(&hdr[0x1c..0x1c + oem_len]).to_vec());
        fuses.insert(0x74, sha256(&hdr[0x324..0x324 + cix_len]).to_vec());
        fuses.insert(0x154, vec![1 << 3 | 1 << 4]); // secure boot; signature algorithm 1 (RSA)
        fuses.insert(0x155, vec![0]);
    }
    fuses
}

/// MEM_CONFIG_FEATURE (MEMORY_INIT_S + 0x23): the DDR features --ddr-feature can override
pub const DDR_FEATURES: [&str; 27] = [
    "WckAlwaysOn",
    "DataMask",
    "AutoPrechargeEn",
    "PbrEn",
    "RfmEn",
    "SelBankInQ",
    "PortPriority",
    "BdwP0",
    "BdwP1",
    "AddressMode",
    "MbistEn",
    "MbistMode",
    "EyeScanEn",
    "BpEn",
    "WrLEcc",
    "RdLEcc",
    "RPriorityP0",
    "WPriorityP0",
    "BdwOvflowP0",
    "RPriorityP1",
    "WPriorityP1",
    "BdwOvflowP1",
    "TrainMode",
    "IEcc",
    "PeriodicTrain",
    "SSC",
    "DfsEn",
];

/// Before each initialisation pass (MemDramFunctionalInit), set DDR features in the driver's
/// MEMORY_INIT_S, as if the memory configuration had them: drives code paths the boards'
/// configurations do not.
pub fn ddr_feature_hook(features: Vec<(usize, u8)>) -> Hook<SkySoc> {
    Rc::new(move |cpu: &mut Arm<SkySoc>| {
        let mem_init = cpu.read32(cpu.addr("gMemInitPtr"));
        for (index, value) in &features {
            cpu.write_bytes(mem_init + 0x23 + *index as u32, &[*value]);
        }
    })
}

pub fn parse_features(list: &[String]) -> Result<Vec<(usize, u8)>, String> {
    list.iter()
        .map(|f| {
            let (name, value) = f.split_once('=').ok_or(format!("--ddr-feature {f}: NAME=VALUE"))?;
            let index = DDR_FEATURES.iter().position(|n| *n == name).ok_or(format!("no DDR feature {name}"))?;
            let value = crate::util::parse_int(value).ok_or(format!("--ddr-feature {f}: not a number"))?;
            Ok((index, value as u8))
        })
        .collect()
}

type Regs = BTreeMap<u32, Vec<u32>>;
type Pages = BTreeMap<u32, Vec<u8>>;

pub struct Opts<'a> {
    pub flash: &'a [u8],
    pub life_cycle: u32,
    pub efuse: BTreeMap<u32, Vec<u8>>,
    pub ddr: bool,
    pub features: Vec<(usize, u8)>,
    pub count: u64,
}

fn boot(elf: &str, cix: bool, o: &Opts) -> Result<(Se, String, Regs, Pages), String> {
    let soc = SkySoc::new(o.flash, o.life_cycle, o.efuse.clone(), o.ddr);
    let mut stubs = if o.ddr { Vec::new() } else { ddr_stubs() };
    if cix {
        stubs.push(("fw_km_derive_key", cix_derive_key()));
    }
    let mut se = Se::new(elf, soc, &stubs, &["service_debug_entry", "service_idle_entry"])?;
    if !o.features.is_empty() {
        se.cpu.set_hook(se.cpu.addr("MemDramFunctionalInit"), ddr_feature_hook(o.features.clone()));
    }
    se.run(None, o.count)?;
    let stopped = se.why();
    let regs = registers(&se, 0, o.ddr);
    let mut mem = Pages::new();
    for (lo, hi, _) in MEMORY {
        for page in se.cpu.mem.mapped() {
            if lo <= page && page < hi {
                let mut data = se.cpu.mem.page_data(page).unwrap().to_vec();
                for (ilo, ihi) in IGNORE_RANGES {
                    for a in ilo.max(page)..ihi.min(page.saturating_add(0x10000)) {
                        data[(a - page) as usize] = 0;
                    }
                }
                mem.insert(page, data);
            }
        }
    }
    Ok((se, stopped, regs, mem))
}

/// The values written to each register from write `mark` on, in order.
fn registers(se: &Se, mark: usize, ddr: bool) -> Regs {
    let mut regs = Regs::new();
    for w in &se.cpu.writes[mark..] {
        let a = w.addr;
        if !IGNORE.contains(&a)
            && !is_memory(a)
            && !IGNORE_RANGES.iter().any(|(lo, hi)| *lo <= a && a < *hi)
            && !(ddr && DDR_IGNORE_RANGES.iter().any(|(lo, hi)| *lo <= a && a < *hi))
        {
            regs.entry(a).or_default().push(w.value);
        }
    }
    regs
}

fn hexes(v: Option<&Vec<u32>>, n: usize) -> String {
    let v: Vec<String> = v.map(|v| v.iter().take(n).map(|x| format!("'{x:#x}'")).collect()).unwrap_or_default();
    format!("[{}]", v.join(", "))
}

/// Requests the AP makes at run time: (name, channel, function ID, parameter words)
const REQUESTS: [(&str, u32, u32, &[u32]); 9] = [
    ("firmware version", 0, 0x8200000f, &[0]),
    ("serial number and chip ID", 0, 0x82000002, &[0, 0x240, 0]),
    ("OPN fuses", 0, 0x82000002, &[1, 0x4dc, 8]),
    ("fuse field", 0, 0x8200000e, &[0x155, 0, 8]),
    ("OEM key hash state", 0, 0x8200001b, &[0]),
    ("MTE setting", 0, 0x82000020, &[0]),
    ("random bytes", 2, 0x82000010, &[16]),
    ("memory repair data", 0, 0x82000006, &[3, 0]),
    ("unknown function", 0, 0x82000099, &[0]),
];

fn mbox(chan: u32) -> (u32, u32) {
    match chan {
        0 => (0xa0050000, 43),
        _ => (0xa0070000, 41),
    }
}

/// Put a request in an AP->SE register channel, take its interrupt, read the answer.
pub fn request(se: &mut Se, chan: u32, fid: u32, words: &[u32]) -> Result<(bool, Vec<u32>), String> {
    let (base, irq) = mbox(chan);
    let mut msg = vec![8 + 4 * words.len() as u32, fid];
    msg.extend_from_slice(words);
    for (i, w) in msg.iter().enumerate() {
        se.cpu.write32(base + 4 * i as u32, *w);
    }
    se.cpu.bus.mbox_pending = Some(base);
    let ok = se.irq(irq, 20_000_000)?;
    se.cpu.bus.mbox_pending = None;
    let n = ((se.cpu.read32(base) & 0x7f) + 3) / 4;
    Ok((ok, (0..n.min(32)).map(|i| se.cpu.read32(base + 4 * i)).collect()))
}

/// Write an image's bus writes as the Python reference's dump has them.
fn dump(se: &Se, path: &str) -> Result<(), String> {
    let mut out = String::new();
    for w in &se.cpu.writes {
        out.push_str(&format!("{} {:#010x} {} {:#x}\n", se.cpu.func(w.pc), w.addr, w.size, w.value));
    }
    let poll = match &se.poll {
        Some((f, a, r)) => format!("('{f}', {a}, {})", r.map_or("None".into(), |r| r.to_string())),
        None => "None".into(),
    };
    out.push_str(&format!(
        "# stopped {} poll {poll} irqs {} wfi {}\n",
        se.stopped.as_deref().unwrap_or("None"),
        se.irqs_taken,
        se.wfi
    ));
    std::fs::write(path, out).map_err(|e| format!("{path}: {e}"))
}

/// The open firmware with secure boot fused for the image's own keys: it must start the AP; and not
/// with a byte of BL2 or of CSU_PM changed.
fn secure_boot(elf: &str, flash: &[u8], dump_to: Option<&str>) -> Result<i32, String> {
    let hdr = &flash[BL1_HEADER..BL1_HEADER + 0x1000];
    let mut imgs: BTreeMap<u32, usize> = BTreeMap::new();
    for i in 0..u32le(hdr, 0x9e8) as usize {
        imgs.insert(u32le(hdr, 0x9ec + 24 * i), u32le(hdr, 0x9ec + 24 * i + 12) as usize);
    }
    let cases = [
        ("as signed", None),
        ("BL2 altered", Some(BL1_HEADER + imgs[&3] + 0x100)),
        ("CSU_PM altered", Some(BL1_HEADER + imgs[&2] + 0x1000)),
    ];
    let mut bad = 0;
    for (k, (name, off)) in cases.iter().enumerate() {
        let mut f = flash.to_vec();
        if let Some(off) = off {
            f[*off] ^= 1;
        }
        let soc = SkySoc::new(&f, 7, keys_efuse(flash, true), false);
        let mut se = Se::new(elf, soc, &ddr_stubs(), &["service_debug_entry", "service_idle_entry"])?;
        se.cpu.bus.stop_write = Some((0x4fa80000, 8)); // AP boot core powered on
        se.poll_limit = 20_000_000;
        se.run(None, 400_000_000)?;
        let started = se.stopped.as_deref() == Some("write 0x8 to 0x4fa80000");
        let ok = started == off.is_none();
        bad += !ok as i32;
        let why = match (&se.stopped, &se.poll) {
            (Some(s), _) => s.clone(),
            (None, Some((f, a, r))) => format!("('{f}', {a}, {})", r.map_or("None".into(), |r| r.to_string())),
            _ => "None".into(),
        };
        println!(
            "{} {name}: AP {} ({why})",
            if ok { "ok  " } else { "FAIL" },
            if started { "started" } else { "not started" }
        );
        if let Some(p) = dump_to {
            dump(&se, &format!("{p}.{k}.trace"))?;
        }
    }
    Ok((bad != 0) as i32)
}

pub fn main(a: &Args, open_elf: &str, cix_elf: &str, flash: &[u8]) -> Result<i32, String> {
    let dump_to = a.opt("--dump");
    if a.flag("--secure") {
        return secure_boot(open_elf, flash, dump_to);
    }
    let ddr = a.flag("--ddr");
    let o = Opts {
        flash,
        life_cycle: a.int("--life-cycle").unwrap_or(4) as u32,
        efuse: if a.flag("--keys") { keys_efuse(flash, false) } else { BTreeMap::new() },
        ddr,
        features: parse_features(&a.all("--ddr-feature"))?,
        count: a.int("--count").unwrap_or(200_000_000),
    };
    let verbose = a.flag("-v");
    let mut sessions = Vec::new();
    let mut results = Vec::new();
    let mut reports = Vec::new();
    for (elf, cix) in [(cix_elf, true), (open_elf, false)] {
        let (mut se, stopped, regs, mem) = boot(elf, cix, &o)?;
        let mut extra = String::new();
        if ddr {
            let ddr_writes = se.cpu.writes.iter().filter(|w| DdrModel::channel(w.addr).is_some()).count();
            let report = (se.call("drv_ddr_get_mem_size", &[], 50_000_000)?, se.call("drv_ddr_get_ch_num", &[], 50_000_000)?);
            reports.push(report);
            extra = format!(
                "; DDR: {ddr_writes} controller/PI/PHY writes, {} resets, {} interrupts, {} MiB on {} channels",
                se.cpu.bus.ddr.as_ref().unwrap().resets,
                se.irqs_taken,
                report.0,
                report.1
            );
        }
        println!("{}: {} writes, stopped at {stopped}{extra}", se.cpu.name(), se.cpu.writes.len());
        sessions.push(se);
        results.push((regs, mem));
    }
    let (ra, ma) = &results[0];
    let (rb, mb) = &results[1];
    let mut bad = 0;
    if ddr {
        if reports[0] != reports[1] {
            bad += 1;
            println!("  memory size and channels: CIX {:?} open {:?}", reports[0], reports[1]);
        }
        let (fa, fb) = (&sessions[0].cpu.bus.flash, &sessions[1].cpu.bus.flash);
        let changed: Vec<usize> = (0..flash.len()).step_by(0x1000).filter(|i| fa[*i..*i + 0x1000] != flash[*i..*i + 0x1000]).collect();
        let list: Vec<String> = changed.iter().take(4).map(|i| format!("{i:#x}")).collect();
        println!(
            "flash: {} sectors written by CIX's firmware{}",
            changed.len(),
            if changed.is_empty() { String::new() } else { format!(" ({})", list.join(", ")) }
        );
        if fa != fb {
            bad += 1;
            let diff: Vec<usize> = (0..fa.len()).filter(|i| fa[*i] != fb[*i]).collect();
            println!("  flash: {} bytes differ, first at {:#x}", diff.len(), diff[0]);
        }
    }
    let all: std::collections::BTreeSet<u32> = ra.keys().chain(rb.keys()).copied().collect();
    for reg in &all {
        if ra.get(reg) != rb.get(reg) {
            bad += 1;
            if verbose || bad <= 40 {
                println!("  {reg:#010x}: CIX {} open {}", hexes(ra.get(reg), 6), hexes(rb.get(reg), 6));
            }
        }
    }
    let zero = vec![0u8; 0x10000];
    let pages: std::collections::BTreeSet<u32> = ma.keys().chain(mb.keys()).copied().collect();
    for page in pages {
        let (pa, pb) = (ma.get(&page).unwrap_or(&zero), mb.get(&page).unwrap_or(&zero));
        if pa != pb {
            let diff: Vec<usize> = (0..0x10000).filter(|i| pa[*i] != pb[*i]).collect();
            bad += 1;
            println!("  memory {page:#010x}: {} bytes differ, first at {:#x}", diff.len(), page as usize + diff[0]);
        }
    }
    println!("{} registers compared", all.len());
    if ddr {
        // a DDR interrupt at run time (inline and link ECC errors, a timeout on channel 1): reported and
        // acknowledged by the driver's handler
        let mut got = Vec::new();
        for se in sessions.iter_mut() {
            let mark = se.cpu.writes.len();
            let cpu = &mut se.cpu;
            let model = cpu.bus.ddr.as_mut().unwrap();
            model.set_status(&mut cpu.io, &cpu.mem, 1, 0x580, 0x803);
            model.set_status(&mut cpu.io, &cpu.mem, 1, 0x57c, 0x10);
            let ok = se.irq(221, 20_000_000)?;
            let writes: Vec<(u32, u32)> =
                se.cpu.writes[mark..].iter().filter(|w| DdrModel::channel(w.addr).is_some()).map(|w| (w.addr, w.value)).collect();
            got.push((ok, writes, se.cpu.bus.ddr.as_ref().unwrap().status[1]));
        }
        let same = got[0] == got[1];
        bad += !same as i32;
        let show = |w: &Vec<(u32, u32)>| {
            let v: Vec<String> = w.iter().map(|(x, y)| format!("('{x:#x}', '{y:#x}')")).collect();
            format!("[{}]", v.join(", "))
        };
        println!(
            "{} DDR interrupt: {}{}",
            if same { "ok  " } else { "FAIL" },
            show(&got[1].1),
            if same { String::new() } else { format!(" (CIX {})", show(&got[0].1)) }
        );
    }
    if a.flag("--services") {
        for (name, chan, fid, words) in REQUESTS {
            let mut got = Vec::new();
            for se in sessions.iter_mut() {
                got.push(request(se, chan, fid, words)?);
            }
            let same = got[0] == got[1];
            bad += !same as i32;
            println!(
                "{} {name}: {}{}",
                if same { "ok  " } else { "FAIL" },
                hexes(Some(&got[1].1), 99),
                if same { String::new() } else { format!(" (CIX {})", hexes(Some(&got[0].1), 99)) }
            );
        }
    }
    if a.flag("--suspend") {
        let mut regs = Vec::new();
        for se in sessions.iter_mut() {
            request(se, 0, 0x82000007, &[0])?; // AP: suspend
            for name in ["service_debug_entry", "service_idle_entry"] {
                if let Some(addr) = se.cpu.sym(name) {
                    se.cpu.del_stop(addr);
                }
            }
            se.cpu.bus.stop_write = Some((0x4661008c, 1)); // SE -> PM: resumed
            let mark = se.cpu.writes.len();
            se.resume(100_000_000)?;
            println!(
                "{}: suspend and resume, {} writes, stopped at {}",
                se.cpu.name(),
                se.cpu.writes.len() - mark,
                match (&se.stopped, &se.poll) {
                    (Some(s), _) => s.clone(),
                    (None, Some((f, a, r))) => format!("('{f}', {a}, {})", r.map_or("None".into(), |r| r.to_string())),
                    _ => "None".into(),
                }
            );
            regs.push(registers(se, mark, false));
        }
        let (ra, rb) = (&regs[0], &regs[1]);
        let all: std::collections::BTreeSet<u32> = ra.keys().chain(rb.keys()).copied().collect();
        let diffs: Vec<u32> = all.iter().filter(|r| ra.get(r) != rb.get(r)).copied().collect();
        for r in diffs.iter().take(40) {
            println!("  {r:#010x}: CIX {} open {}", hexes(ra.get(r), 6), hexes(rb.get(r), 6));
        }
        println!("suspend/resume: {} registers compared, {} differ", all.len(), diffs.len());
        bad += diffs.len() as i32;
    }
    if let Some(p) = dump_to {
        for (i, se) in sessions.iter().enumerate() {
            dump(se, &format!("{p}.{i}.trace"))?;
        }
    }
    println!("{bad} differences");
    Ok((bad != 0) as i32)
}
