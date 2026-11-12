// SPDX-License-Identifier: BSD-2-Clause-Patent
//! Coverage of the open EC image by function. The SDCC build has no ELF:
//! the functions come from its listings (`*.rst`: every label in a CODE
//! area, static functions included; labels followed by data are tables,
//! not functions) and from the linker map (`ec.map`, `C:` globals: the C
//! library's routines). A function runs from its label to the next one.
//!
//!   sky1emu ec-cov FILE BUILD_DIR [--all] [--partial] [--lines]
//!
//! --lines: under each function partly run, the C lines (from the
//! listing) of the instructions that never ran.
//!
//! FILE is a coverage file `ec-test --cov FILE` wrote (image "ec_fw.bin";
//! the vendor's runs are kept as "ec_fw.bin.vendor").

use crate::cov::{covered, load, normalize, Ranges};
use std::collections::BTreeMap;

pub struct Func {
    pub name: String,
    pub file: String,
    pub addr: u32,
    pub size: u32,
}

/// Code address -> the C source line the listing gives for it.
pub type Lines = BTreeMap<u32, String>;

/// The functions of the image built in `dir`.
pub fn functions_lines(dir: &str) -> Result<(Vec<Func>, Lines), String> {
    let mut src: Lines = BTreeMap::new();
    // address -> (name, module, is data)
    let mut syms: BTreeMap<u32, (String, String, bool)> = BTreeMap::new();
    let rd = std::fs::read_dir(dir).map_err(|e| format!("{dir}: {e}"))?;
    let mut files: Vec<_> = rd.filter_map(|e| e.ok()).map(|e| e.path()).filter(|p| p.extension().is_some_and(|x| x == "rst")).collect();
    files.sort();
    for path in files {
        let module = path.file_stem().unwrap().to_string_lossy().into_owned();
        let text = std::fs::read_to_string(&path).map_err(|e| format!("{}: {e}", path.display()))?;
        let lines: Vec<&str> = text.lines().collect();
        let mut code = false;
        let mut table = false;
        let mut cline = String::new();
        for (i, l) in lines.iter().enumerate() {
            if let Some(p) = l.find(".area") {
                code = l[p..].contains("CODE");
                table = l[p..].contains("CONST") || l[p..].contains("XINIT");
                continue;
            }
            if !code {
                continue;
            }
            if let Some(p) = l.find(";\tsrc/") {
                cline = l[p + 2..].trim().to_string();
                continue;
            }
            {
                let t: Vec<&str> = l.split_whitespace().collect();
                if t.len() >= 3 && t[0].len() == 6 && t[1].len() == 2 {
                    if let Ok(a) = u32::from_str_radix(t[0], 16) {
                        src.entry(a).or_insert_with(|| cline.clone());
                    }
                }
            }
            // "      0021C3                       1700 _name:" (or "::")
            let t: Vec<&str> = l.split_whitespace().collect();
            if t.len() != 3 || !t[2].ends_with(':') || !t[2].starts_with('_') || t[2].contains('$') {
                continue;
            }
            let Ok(addr) = u32::from_str_radix(t[0], 16) else { continue };
            let name = t[2].trim_end_matches(':').to_string();
            // a table: the next line with code bytes is a .db/.dw
            let data = lines[i + 1..]
                .iter()
                .find(|n| n.split_whitespace().nth(1).is_some_and(|b| b.len() == 2 && u8::from_str_radix(b, 16).is_ok()))
                .is_some_and(|n| n.contains(".db") || n.contains(".dw") || n.contains(".ascii"));
            syms.insert(addr, (name, module.clone(), data || table));
        }
    }
    // the library's routines, from the map
    if let Ok(map) = std::fs::read_to_string(format!("{dir}/ec.map")) {
        for l in map.lines() {
            let t: Vec<&str> = l.split_whitespace().collect();
            if t.len() >= 4 && t[0] == "C:" && !t[2].starts_with("l_") && !t[2].starts_with("s_") {
                if let Ok(addr) = u32::from_str_radix(t[1], 16) {
                    syms.entry(addr).or_insert((t[2].to_string(), t[3].to_string(), false));
                }
            }
        }
    }
    let list: Vec<(u32, (String, String, bool))> = syms.into_iter().collect();
    let mut out = Vec::new();
    for (k, (addr, (name, file, data))) in list.iter().enumerate() {
        if *data {
            continue;
        }
        let end = list.get(k + 1).map_or(addr + 0x100, |n| n.0);
        out.push(Func { name: name.clone(), file: file.clone(), addr: *addr, size: end - addr });
    }
    Ok((out, src))
}

pub fn main(args: &[String]) -> i32 {
    let a = match crate::util::Args::parse(args, &[]) {
        Ok(a) => a,
        Err(e) => {
            eprintln!("{e}");
            return 2;
        }
    };
    if a.pos.len() < 2 {
        eprintln!("usage: sky1emu ec-cov FILE BUILD_DIR [--all] [--partial] [--lines]");
        return 2;
    }
    let cov = load(&a.pos[0]);
    let (fns, src) = match functions_lines(&a.pos[1]) {
        Ok(f) => f,
        Err(e) => {
            eprintln!("{e}");
            return 2;
        }
    };
    let ranges: Ranges = normalize(cov.get("ec_fw.bin").cloned().unwrap_or_default());
    let mut total = 0u64;
    let mut ran = 0u64;
    let mut never = Vec::new();
    let mut rows = Vec::new();
    for f in &fns {
        let r = covered(&ranges, f.addr, f.addr + f.size);
        total += f.size as u64;
        ran += r as u64;
        if r == 0 {
            never.push(f);
        }
        rows.push((f, r));
    }
    let nb: u64 = never.iter().map(|f| f.size as u64).sum();
    println!(
        "ec_fw.bin: {} functions, {total} bytes; ran {ran} bytes ({:.1}%); {} functions never ran ({nb} bytes)",
        fns.len(),
        100.0 * ran as f64 / total.max(1) as f64,
        never.len()
    );
    if a.flag("--all") || a.flag("--partial") {
        for (f, r) in &rows {
            if !a.flag("--all") && (*r == 0 || *r == f.size) {
                continue;
            }
            println!("  {:5.1}%  {:5}  {:#06x}  {}  {}", 100.0 * *r as f64 / f.size as f64, f.size, f.addr, f.name, f.file);
            if a.flag("--lines") && *r != 0 && *r != f.size {
                // the instructions not run, by the C line the listing gives
                let mut last = String::new();
                for (&addr, line) in src.range(f.addr..f.addr + f.size) {
                    if covered(&ranges, addr, addr + 1) == 0 && *line != last {
                        println!("           {addr:#06x}  {line}");
                        last = line.clone();
                    }
                }
            }
        }
    }
    println!("never ran:");
    for f in &never {
        println!("  {:5}  {:#06x}  {}  {}", f.size, f.addr, f.name, f.file);
    }
    0
}
