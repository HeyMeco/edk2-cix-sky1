// SPDX-License-Identifier: BSD-2-Clause-Patent
//! Coverage: which code of an image the tests ran.
//!
//! Each test can add the address ranges its runs executed to a coverage
//! file (--cov FILE; one line per range: image, start, end).  The report
//! (`sky1emu cov FILE ELF...`) lists, for each image, how much of every
//! function ran and the functions no test reached: code nothing compares.

use crate::elf::Elf;
use std::collections::BTreeMap;

pub type Ranges = Vec<(u32, u32)>;

/// Sorted, merged ranges.
pub fn normalize(mut r: Ranges) -> Ranges {
    r.sort();
    let mut out: Ranges = Vec::new();
    for (lo, hi) in r {
        match out.last_mut() {
            Some(last) if lo <= last.1 => last.1 = last.1.max(hi),
            _ => out.push((lo, hi)),
        }
    }
    out
}

pub fn load(path: &str) -> BTreeMap<String, Ranges> {
    let mut m: BTreeMap<String, Ranges> = BTreeMap::new();
    if let Ok(text) = std::fs::read_to_string(path) {
        for line in text.lines() {
            let f: Vec<&str> = line.split('\t').collect();
            if f.len() == 3 {
                if let (Ok(lo), Ok(hi)) = (u32::from_str_radix(f[1], 16), u32::from_str_radix(f[2], 16)) {
                    m.entry(f[0].to_string()).or_default().push((lo, hi));
                }
            }
        }
    }
    m
}

/// Add an image's executed ranges to the coverage file.
pub fn save(path: &str, image: &str, ranges: &[(u32, u32)]) -> Result<(), String> {
    let mut m = load(path);
    m.entry(image.to_string()).or_default().extend_from_slice(ranges);
    let mut text = String::new();
    for (name, r) in m {
        for (lo, hi) in normalize(r) {
            text.push_str(&format!("{name}\t{lo:x}\t{hi:x}\n"));
        }
    }
    std::fs::write(path, text).map_err(|e| format!("{path}: {e}"))
}

/// Bytes of [lo, hi) the ranges cover.
pub fn covered(ranges: &Ranges, lo: u32, hi: u32) -> u32 {
    let start = ranges.partition_point(|r| r.1 <= lo);
    let mut n = 0;
    for (a, b) in &ranges[start..] {
        if *a >= hi {
            break;
        }
        n += (*b).min(hi) - (*a).max(lo);
    }
    n
}

pub struct FnCov {
    pub name: String,
    pub file: String,
    pub addr: u32,
    pub size: u32,
    pub ran: u32,
}

/// Per function: bytes run.  Functions of size 0 and the names in `skip`
/// (prefixes) are left out.
pub fn functions(elf: &Elf, ranges: &Ranges) -> Vec<FnCov> {
    let thumb = elf.machine == 40;
    let mut seen = std::collections::BTreeSet::new();
    let mut out = Vec::new();
    for s in &elf.symbols {
        if s.kind != crate::elf::STT_FUNC || s.size == 0 || s.name.is_empty() {
            continue;
        }
        let addr = if thumb { s.value & !1 } else { s.value };
        if !seen.insert((addr, s.size)) {
            continue;
        }
        let file = s.file.map(|i| elf.files[i as usize].clone()).unwrap_or_default();
        out.push(FnCov { name: s.name.clone(), file, addr, size: s.size, ran: covered(ranges, addr, addr + s.size) });
    }
    out.sort_by_key(|f| f.addr);
    out
}

pub fn main(args: &[String]) -> i32 {
    let a = match crate::util::Args::parse(args, &["--min", "--only"]) {
        Ok(a) => a,
        Err(e) => {
            eprintln!("{e}");
            return 2;
        }
    };
    if a.pos.len() < 2 {
        eprintln!("usage: sky1emu cov FILE ELF... [--all] [--partial] [--only PREFIX]");
        return 2;
    }
    let cov = load(&a.pos[0]);
    for path in &a.pos[1..] {
        let elf = match Elf::load(path) {
            Ok(e) => e,
            Err(e) => {
                eprintln!("{e}");
                return 2;
            }
        };
        let name = std::path::Path::new(path).file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default();
        let ranges = normalize(cov.get(&name).cloned().unwrap_or_default());
        let mut fns = functions(&elf, &ranges);
        if let Some(p) = a.opt("--only") {
            fns.retain(|f| f.name.starts_with(p) || f.file.starts_with(p));
        }
        let (total, ran): (u64, u64) = fns.iter().fold((0, 0), |(t, r), f| (t + f.size as u64, r + f.ran as u64));
        let never: Vec<&FnCov> = fns.iter().filter(|f| f.ran == 0).collect();
        let never_bytes: u64 = never.iter().map(|f| f.size as u64).sum();
        println!(
            "{name}: {} functions, {total} bytes; ran {ran} bytes ({:.1}%); {} functions never ran ({never_bytes} bytes, {:.1}%)",
            fns.len(),
            100.0 * ran as f64 / total.max(1) as f64,
            never.len(),
            100.0 * never_bytes as f64 / total.max(1) as f64
        );
        if a.flag("--all") || a.flag("--partial") {
            for f in &fns {
                if f.ran == 0 && !a.flag("--all") {
                    continue;
                }
                if f.ran == f.size && !a.flag("--all") {
                    continue;
                }
                println!("  {:5.1}%  {:6}  {:#010x}  {}  {}", 100.0 * f.ran as f64 / f.size as f64, f.size, f.addr, f.name, f.file);
            }
        }
        if !a.flag("--summary") {
            println!("never ran:");
            for f in &never {
                println!("  {:6}  {:#010x}  {}  {}", f.size, f.addr, f.name, f.file);
            }
        }
    }
    0
}
