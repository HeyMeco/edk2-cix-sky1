// SPDX-License-Identifier: BSD-2-Clause-Patent
//! Compare the DVFS tables the open CSU_PM firmware builds with CIX's.
//!
//!   sky1emu pm-vf OPEN.elf [--cix CIX.elf] [--strap LO:HI] [--fuses SEED]
//!                 [--phase N] [--opn HI:LO] [--vmin] [--phecda] [--bios ITEMS]
//!                 [--gb1boost] [--no-i2c] [--matrix] [-v]
//!
//! Each image boots on the model until it idles; then the dvfs module's
//! element table (CIX's config_dvfs.c element_table: per DVFS domain the
//! sustained OPP, the reference level, the OPPs: level, mV, kHz, mW) and
//! the selectors CIX's subsys_hsm.c picks (g_mp_sel, g_opp_table_selector,
//! g_opp_limit_selector) are read from both and compared, with the I2C
//! transfers (the boot voltages; --no-i2c when the board's regulators are
//! ones the open firmware does not drive yet, as with --phecda).
//!
//! What real parts differ in, the emulator does not have by default; these
//! make both images see it:
//!   --strap LO:HI   the PM strap the SE leaves (0xa0000800, two words; the
//!                   checksum byte, bits 0-7, is made right: bytes sum to 0xff)
//!   --fuses SEED    an eFuse copy (0x200be700) of random V/F curves,
//!                   patches, DSU point and leakage currents, as a part's
//!   --phase N       with --fuses: fuse values that make the ATE phase N
//!                   (0 ES .. 5 MP3)
//!   --opn HI:LO     the part number fuse
//!   --vmin          the fused voltages enabled (what the BIOS configuration's
//!                   vmin magic does: vf_curve_loaded 0 at reset)
//!   --phecda        the board's PMICs are MP2845s at 0x45 on I2C 0 and 1
//!                   (CIX's Phecda layout) rather than the EVB's MP5475s
//!   --bios ITEM,... a valid BIOS PM configuration (0x200bf000) with: vmin
//!                   (the fused voltages enabled), keep100m (the 100 MHz OPPs
//!                   kept), opmode2 (the GPU's budget tables), oppN (DVFS
//!                   element N's OPPs given), oppall (bit 7 of opp_valid
//!                   clear: every domain counts as given)
//!   --gb1boost      the SoC register asking for GB1 at 2.8 GHz set
//!   --matrix        every phase with several fuse seeds, with and without
//!                   vmin, each BIOS item and the boost, in one go

use super::soc::{self, DevKind, I2cDevice, PmSoc};
use super::test;
use crate::util::{self, Args};

const EFUSE: u32 = 0x200be700;
const SELECTORS: [&str; 3] = ["g_mp_sel", "g_opp_table_selector", "g_opp_limit_selector"];

/// A small seeded generator (xorshift64*): the same fuses for both images.
struct Rng(u64);

impl Rng {
    fn new(seed: u64) -> Self {
        Rng(seed.wrapping_mul(0x9e3779b97f4a7c15) ^ 0x2545f4914f6cdd1d)
    }

    fn next(&mut self) -> u64 {
        self.0 ^= self.0 >> 12;
        self.0 ^= self.0 << 25;
        self.0 ^= self.0 >> 27;
        self.0.wrapping_mul(0x2545f4914f6cdd1d) >> 16
    }

    fn range(&mut self, lo: u32, hi: u32) -> u32 {
        lo + (self.next() % (hi - lo) as u64) as u32
    }
}

fn bits_put(buf: &mut [u8], pos: usize, n: usize, v: u32) {
    for i in 0..n {
        let (byte, bit) = ((pos + i) / 8, (pos + i) % 8);
        buf[byte] = (buf[byte] & !(1 << bit)) | (((v >> i) & 1) as u8) << bit;
    }
}

/// An eFuse copy with V/F data: curves of GM0, GM1, GB0, GB1 (bit offsets
/// 0xacf, 0xade, 0xab1, 0xac0; three points 60 bits apart: 5 bits of 100
/// MHz, 10 bits of mV above 400), patches of GB0 and GM1 (0xbdd, 0xc0a, 15
/// bits apart), the DSU point (0x727), leakage currents.
fn efuse_image(seed: u64, phase: u32, opn: Option<(u32, u32)>) -> Vec<u8> {
    let mut r = Rng::new(seed);
    let mut buf = vec![0u8; 0x400];
    for b in buf.iter_mut().take(16) {
        *b = r.range(0, 256) as u8; // not the marker
    }
    fn point(buf: &mut [u8], pos: usize, mhz: u32, mv: u32) {
        bits_put(buf, pos, 5, mhz / 100);
        bits_put(buf, pos + 5, 10, mv - 400);
    }
    let mut tops = [(0xacf, [2300, 2200, 1800]), (0xade, [2200, 2100, 1600]), (0xab1, [2500, 2400, 2000]), (0xac0, [2600, 2500, 2200])];
    match phase {
        2 => tops[3].1 = [2600, 2500, 2300],                                 // PVT: GB1's third point at 2300
        3 => tops[3].1 = [2600, 2500, 2200],                                 // MP: at 2200
        4 | 5 => tops[3].1 = [if phase == 5 { 2200 } else { 2600 }, 2500, 2200], // MP2/MP3: GB1 top at 2200 for MP3
        _ => {}
    }
    for (off, fr) in tops {
        let v = r.range(820, 930);
        for p in 0..3 {
            let d = r.range(0, 10);
            point(&mut buf, off + 60 * p, fr[p], v - 25 * p as u32 - d);
        }
    }
    if phase >= 4 {
        for (off, fr) in [(0xbdd, [2500, 2400, 2000]), (0xc0a, [2200, 2100, 1600])] {
            let v = r.range(820, 930);
            for p in 0..3 {
                point(&mut buf, off + 15 * p, fr[p], v - 20 * p as u32);
            }
        }
    }
    if phase >= 2 {
        let v = r.range(760, 790);
        point(&mut buf, 0x727, 1300, v);
    }
    for off in [0x847, 0x857, 0x827, 0x837, 0x867, 0x877, 0x887, 0x897, 0x927, 0x937, 0x947] {
        let v = r.range(100, 3000);
        bits_put(&mut buf, off, 16, v);
        let v = r.range(100, 3000);
        bits_put(&mut buf, if off < 0x900 { off + 128 } else { off + 80 }, 16, v);
    }
    if let Some((hi, lo)) = opn {
        bits_put(&mut buf, 173 * 32, 32, lo);
        bits_put(&mut buf, 174 * 32, 32, hi);
    }
    buf
}

/// CIX's BIOS PM configuration block: version 3, length, two sums, then
/// pm_config_t (packed; config_data_t valid when bit 0 is clear), the rest
/// erased (0xff).
fn bios_image(items: &[&str]) -> Result<Vec<u8>, String> {
    let mut cfg = vec![0xffu8; 0xdba];
    let mut opp_valid = 0x80u8;
    for it in items {
        match *it {
            "vmin" => util::put32(&mut cfg, 0xceb, 0x67997cda),
            "keep100m" => util::put32(&mut cfg, 0xd33, 0),
            "opmode2" => util::put32(&mut cfg, 0xd43, 2),
            "oppall" => opp_valid = 0,
            x if x.starts_with("opp") => {
                let e: usize = x[3..].parse().map_err(|_| format!("--bios {x}: oppN"))?;
                let base = 0x81 + 212 * e;
                let levels: [[u32; 4]; 3] = [[400, 805, 400000, 0], [900, 815, 900000, 0], [1300, 845, 0, 0]];
                cfg[base..base + 2].copy_from_slice(&(levels.len() as u16).to_le_bytes());
                cfg[base + 2..base + 4].copy_from_slice(&1u16.to_le_bytes());
                for (k, o) in levels.iter().enumerate() {
                    for (j, w) in o.iter().enumerate() {
                        util::put32(&mut cfg, base + 4 + 16 * k + 4 * j, *w);
                    }
                }
            }
            x => return Err(format!("--bios: no item {x}")),
        }
    }
    cfg[0x80] = opp_valid;
    let mut blk = vec![0u8; 0x18];
    blk.extend(cfg);
    while blk.len() % 4 != 0 {
        blk.push(0xff);
    }
    blk[0..2].copy_from_slice(&3u16.to_le_bytes());
    let n = blk.len() as u32;
    util::put32(&mut blk, 8, n);
    test::config_sums(&mut blk);
    Ok(blk)
}

#[derive(Clone, Default, Debug)]
pub struct Case {
    pub strap: Option<(u32, u32)>,
    pub fuses: Option<u64>,
    pub phase: u32,
    pub opn: Option<(u32, u32)>,
    pub vmin: bool,
    pub phecda: bool,
    pub bios: Option<String>,
    pub gb1boost: bool,
}

type Domain = (String, i64, u16, u16, Vec<[u32; 4]>);

pub struct Tables {
    pub why: Option<String>,
    pub selectors: Vec<Option<u8>>,
    pub domains: Vec<Domain>,
    pub i2c: Vec<String>,
    /// where the image was when it stopped
    pub backtrace: String,
    pub covered: Vec<(u32, u32)>,
}

/// Address of an object: by name (and size when names repeat).
fn sym(cpu: &soc::Cpu, name: &str, size: Option<u32>) -> Option<u32> {
    cpu.object_list.iter().find(|o| o.0 == name && size.map_or(true, |s| o.2 == s)).map(|o| o.1)
}

pub fn run(elf: &str, c: &Case) -> Result<Tables, String> {
    let soc = if c.phecda {
        let mut s = PmSoc::new(false);
        for i in 0..2 {
            s.i2c[i].devices.insert(0x45, I2cDevice::new(&format!("i2c{i}:0x45"), DevKind::Plain));
        }
        Some(s)
    } else {
        None
    };
    let mut cpu = soc::boot(elf, soc)?;
    if let Some((lo, hi)) = c.strap {
        let v = lo as u64 | (hi as u64) << 32;
        let rest: u32 = (1..8).map(|k| ((v >> (8 * k)) & 0xff) as u32).sum();
        let lo = (lo & !0xff) | (0xffu32.wrapping_sub(rest) & 0xff);
        cpu.bus.mem.write32(0xa0000800, lo);
        cpu.bus.mem.write32(0xa0000804, hi);
    }
    if let Some(seed) = c.fuses {
        cpu.write_bytes(EFUSE, &efuse_image(seed, c.phase, c.opn));
    }
    if c.vmin {
        if let Some(a) = sym(&cpu, "vf_curve_loaded", None) {
            cpu.write(a, 1, 0);
        }
    }
    if let Some(b) = &c.bios {
        let items: Vec<&str> = b.split(',').collect();
        cpu.write_bytes(test::BIOS_CFG, &bios_image(&items)?);
    }
    if c.gb1boost {
        cpu.bus.mem.write32(0x5600050c, 0x10);
    }
    for name in soc::ASSERTS {
        if let Some(a) = cpu.byname.get(name).copied() {
            cpu.set_stop(a, name);
        }
    }
    let idle = test::IDLE.iter().find_map(|n| cpu.byname.get(*n).copied()).ok_or("no idle function in the image")?;
    cpu.set_stop(idle, "idle");
    let why = cpu.run(test::BOOT_BUDGET);
    let selectors = SELECTORS.iter().map(|s| sym(&cpu, s, None).map(|a| cpu.read(a, 1) as u8)).collect();
    let mut domains = Vec::new();
    if let Some(et) = sym(&cpu, "element_table", Some(13 * 12)) {
        for i in 0..12 {
            let (name_p, data) = (cpu.read32(et + 12 * i), cpu.read32(et + 12 * i + 8));
            let raw_name = cpu.read_bytes(name_p, 16);
            let name = String::from_utf8_lossy(raw_name.split(|b| *b == 0).next().unwrap_or(&[])).into_owned();
            let raw = cpu.read_bytes(data, 28);
            let sus = util::u32le(&raw, 16);
            let (reference, num) = (u16::from_le_bytes([raw[20], raw[21]]), u16::from_le_bytes([raw[22], raw[23]]));
            let opps = util::u32le(&raw, 24);
            let mut rows = Vec::new();
            for j in 0..num.max(1) as u32 + 1 {
                let o = if opps != 0 {
                    let b = cpu.read_bytes(opps + 16 * j, 16);
                    [util::u32le(&b, 0), util::u32le(&b, 4), util::u32le(&b, 8), util::u32le(&b, 12)]
                } else {
                    [0; 4]
                };
                if o == [0; 4] && j >= num as u32 {
                    break;
                }
                rows.push(o);
            }
            domains.push((name, if sus != 0xffffffff { sus as i64 } else { -1 }, reference, num, rows));
        }
    }
    let i2c = cpu.bus.i2c_log.iter().map(|l| format!("{} {}", l.bus, l.what)).collect();
    let backtrace = cpu.backtrace_names(8);
    Ok(Tables { why, selectors, domains, i2c, backtrace, covered: cpu.covered() })
}

/// Compare one case; the number of differences.
pub fn compare(cix: &str, open: &str, c: &Case, no_i2c: bool, verbose: bool, quiet: bool, cov: &mut Vec<Vec<(u32, u32)>>) -> Result<usize, String> {
    let (a, b) = (run(cix, c)?, run(open, c)?);
    cov[0].extend(a.covered.iter());
    cov[1].extend(b.covered.iter());
    let mut bad = 0;
    let s = |w: &Option<String>| w.clone().unwrap_or_else(|| "None".to_string());
    let mut lines = vec![format!("CIX: {}, open: {}", s(&a.why), s(&b.why))];
    // an assertion is one whichever function each image ends it in
    let kind = |w: &Option<String>| match w.as_deref() {
        Some(x) if soc::ASSERTS.contains(&x) => Some("assertion".to_string()),
        _ => w.clone(),
    };
    bad += (kind(&a.why) != kind(&b.why)) as usize;
    for (who, t) in [("CIX", &a), ("open", &b)] {
        if t.why.as_deref() != Some("idle") {
            lines.push(format!("  {who} at: {}", t.backtrace));
        }
    }
    for (i, name) in SELECTORS.iter().enumerate() {
        let same = a.selectors[i] == b.selectors[i];
        bad += !same as usize;
        let f = |x: Option<u8>| x.map_or("None".to_string(), |v| v.to_string());
        lines.push(format!("{name}: CIX {}, open {}{}", f(a.selectors[i]), f(b.selectors[i]), if same { "" } else { "   <-- differs" }));
    }
    if a.domains.len() != b.domains.len() {
        bad += 1;
        lines.push(format!("domains: CIX {}, open {}", a.domains.len(), b.domains.len()));
    }
    for (da, db) in a.domains.iter().zip(b.domains.iter()) {
        let same = da == db;
        bad += !same as usize;
        lines.push(format!("{:10} sustained {:2} ref {:5} n {:2}  {}", da.0, da.1, da.2, da.3, if same { "same" } else { "DIFFERS" }));
        if !same || verbose {
            for k in 0..da.4.len().max(db.4.len()) {
                let (x, y) = (da.4.get(k), db.4.get(k));
                lines.push(format!("    {k:2} CIX {x:?}  open {y:?}{}", if x == y { "" } else { "   <--" }));
            }
            if (da.1, da.2, da.3) != (db.1, db.2, db.3) {
                lines.push(format!("    open: sustained {} ref {} n {}", db.1, db.2, db.3));
            }
        }
    }
    let same = a.i2c == b.i2c;
    lines.push(format!("I2C transfers: CIX {}, open {}, {}", a.i2c.len(), b.i2c.len(), if same { "same" } else { "differ" }));
    if !same && !no_i2c {
        bad += 1;
        let mut shown = 0;
        for k in 0..a.i2c.len().max(b.i2c.len()) {
            let (x, y) = (a.i2c.get(k), b.i2c.get(k));
            if x != y && shown < 10 {
                shown += 1;
                lines.push(format!("    {k:2} CIX {x:?}  open {y:?}"));
            }
        }
    }
    lines.push(format!("tables: {}", if bad == 0 { "same".to_string() } else { format!("{bad} differences") }));
    if !quiet || bad != 0 {
        for l in lines {
            println!("{l}");
        }
    }
    Ok(bad)
}

fn pair(s: &str) -> Result<(u32, u32), String> {
    let (a, b) = s.split_once(':').ok_or(format!("{s}: HEX:HEX"))?;
    let h = |x: &str| u32::from_str_radix(x.trim_start_matches("0x"), 16).map_err(|_| format!("{s}: HEX:HEX"));
    Ok((h(a)?, h(b)?))
}

/// Every phase with several fuse seeds, with and without the fused
/// voltages, each BIOS item, the boost and part numbers.
fn matrix() -> Vec<(String, Case, bool)> {
    let mut out = vec![("default".to_string(), Case { phase: 5, ..Default::default() }, false)];
    for phase in 0..=5 {
        for seed in [1, 2, 3] {
            for vmin in [false, true] {
                out.push((
                    format!("fuses {seed} phase {phase}{}", if vmin { " vmin" } else { "" }),
                    Case { fuses: Some(seed), phase, vmin, ..Default::default() },
                    false,
                ));
            }
        }
    }
    for bios in ["vmin", "keep100m", "opmode2", "oppall", "opp0", "opp3", "opp7", "opp11", "vmin,keep100m,opmode2", "opp0,opp1,opp2,oppall"] {
        out.push((format!("bios {bios}"), Case { phase: 5, bios: Some(bios.to_string()), ..Default::default() }, false));
        out.push((format!("bios {bios} fuses 7"), Case { fuses: Some(7), phase: 5, bios: Some(bios.to_string()), ..Default::default() }, false));
    }
    out.push(("gb1boost".into(), Case { phase: 5, gb1boost: true, ..Default::default() }, false));
    out.push(("gb1boost fuses 4 phase 4".into(), Case { fuses: Some(4), phase: 4, gb1boost: true, ..Default::default() }, false));
    for (hi, lo) in [(0u32, 0u32), (0x43503138, 0x31383000), (0x43503138, 0x31363000), (0xffffffff, 0xffffffff)] {
        out.push((format!("opn {hi:x}:{lo:x}"), Case { fuses: Some(5), phase: 5, opn: Some((hi, lo)), ..Default::default() }, false));
    }
    for strap in [(0x020fff00u32, 0u32), (0x010fff00, 0), (0x030fff00, 0), (0x02000000, 0), (0x020fff00, 0x1), (0xffffff00, 0xffffffff), (0, 0)] {
        out.push((format!("strap {:x}:{:x}", strap.0, strap.1), Case { phase: 5, strap: Some(strap), ..Default::default() }, false));
        out.push((
            format!("strap {:x}:{:x} fuses 6", strap.0, strap.1),
            Case { fuses: Some(6), phase: 5, strap: Some(strap), ..Default::default() },
            false,
        ));
    }
    out.push(("phecda".into(), Case { phase: 5, phecda: true, ..Default::default() }, true));
    out
}

pub fn main(args: &[String]) -> i32 {
    let a = match Args::parse(args, &["--cix", "--strap", "--fuses", "--phase", "--opn", "--bios", "--cov"]) {
        Ok(a) => a,
        Err(e) => {
            eprintln!("{e}");
            return 2;
        }
    };
    let r = (|| -> Result<usize, String> {
        a.check(&["--vmin", "--phecda", "--gb1boost", "--no-i2c", "--matrix", "-v"])?;
        let open = a.pos.first().ok_or("usage: sky1emu pm-vf OPEN.elf [options]")?;
        let cix = match a.opt("--cix") {
            Some(c) => c.to_string(),
            None => util::cix_debug(test::CIX_PM)?,
        };
        let mut cov = vec![Vec::new(), Vec::new()];
        let bad = if a.flag("--matrix") {
            let mut bad = 0;
            let cases = matrix();
            for (name, case, no_i2c) in &cases {
                let n = compare(&cix, open, case, *no_i2c, false, true, &mut cov)?;
                println!("{} {name}", if n == 0 { "ok  " } else { "FAIL" });
                bad += n;
            }
            println!("{} cases, {bad} differences", cases.len());
            bad
        } else {
            let case = Case {
                strap: a.opt("--strap").map(pair).transpose()?,
                fuses: a.int("--fuses"),
                phase: a.int("--phase").unwrap_or(5) as u32,
                opn: a.opt("--opn").map(pair).transpose()?,
                vmin: a.flag("--vmin"),
                phecda: a.flag("--phecda"),
                bios: a.opt("--bios").map(|s| s.to_string()),
                gb1boost: a.flag("--gb1boost"),
            };
            compare(&cix, open, &case, a.flag("--no-i2c"), a.flag("-v"), false, &mut cov)?
        };
        if let Some(path) = a.opt("--cov") {
            let name = |p: &str| std::path::Path::new(p).file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default();
            crate::cov::save(path, &name(&cix), &cov[0])?;
            crate::cov::save(path, &name(open), &cov[1])?;
        }
        Ok(bad)
    })();
    match r {
        Ok(bad) => (bad != 0) as i32,
        Err(e) => {
            eprintln!("pm-vf: {e}");
            2
        }
    }
}
