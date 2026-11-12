// SPDX-License-Identifier: BSD-2-Clause-Patent
//! The AArch64 boot stages: an interpreter (cpu.rs), the Sky1 as TF-A sees it (soc.rs) and the tests that
//! compare the open BL2 and BL31 with the vendor's (test.rs, bl2.rs, bl31.rs, expected.rs).
//!
//!   sky1emu a64-test [bl2] [bl31] ...     the comparison
//!   sky1emu a64 IMAGE ...                 one stage from its entry, to look into a finding
//!   sky1emu a64-selfcheck IMAGE...        the interpreter against the host's CPU
//!   sky1emu a64-cov FILE IMAGE...         what the runs executed
//!
//! Built only with `--features a64`.  emu/README.md has the section "BL2 and BL31 (AArch64)".

pub mod bl2;
pub mod bl31;
pub mod cpu;
pub mod elf64;
pub mod expected;
pub mod images;
pub mod machine;
pub mod native;
pub mod soc;
pub mod test;

use crate::util::Args;

/// The module's lines of `sky1emu`'s usage.
pub const USAGE: &str = "BL2, BL31 (AArch64):\n\
\x20 a64 IMAGE [--base ADDR] [--flash FILE] ...  run a boot stage from its entry; print its console and where it stopped\n\
\x20 a64-test [bl2] [bl31] [--flash FILE] ...  compare the open BL2 and BL31 with CIX's\n\
\x20 a64-selfcheck IMAGE... [--rounds N]      every instruction word of the images on the host's CPU and in the interpreter\n\
\x20 a64-cov FILE IMAGE... [--partial] [--all]  what the tests ran of an image (a64-test --cov FILE)";

/// The distinct instruction words of an image's code.
fn words(img: &elf64::Image) -> Vec<u32> {
    let mut out = std::collections::BTreeSet::new();
    for (addr, data) in &img.chunks {
        for (i, w) in data.chunks_exact(4).enumerate() {
            if addr + 4 * i as u64 >= img.text_end {
                break;
            }
            out.insert(u32::from_le_bytes([w[0], w[1], w[2], w[3]]));
        }
    }
    out.into_iter().collect()
}

fn selfcheck(rest: &[String]) -> i32 {
    let a = match Args::parse(rest, &["--rounds"]) {
        Ok(a) => a,
        Err(e) => {
            eprintln!("{e}");
            return 2;
        }
    };
    if a.pos.is_empty() || a.check(&["-v"]).is_err() {
        eprintln!("usage: sky1emu a64-selfcheck IMAGE... [--rounds N] [-v]");
        return 2;
    }
    let mut all = std::collections::BTreeSet::new();
    for p in &a.pos {
        match elf64::Image::load(p, 0) {
            Ok(img) => {
                let w = words(&img);
                println!("{}: {} distinct words", img.name, w.len());
                all.extend(w);
            }
            Err(e) => {
                eprintln!("{e}");
                return 2;
            }
        }
    }
    let all: Vec<u32> = all.into_iter().collect();
    match native::check(&all, a.int("--rounds").unwrap_or(16) as u32, a.flag("-v")) {
        Ok(s) => {
            println!("{s}");
            0
        }
        Err(e) => {
            eprintln!("a64-selfcheck: {e}");
            1
        }
    }
}

/// The default flash image: what build.py made for the Orion O6 with the vendor's boot firmware.
pub const FLASH: &str = "dist/orion-o6_2026Q3.bin";

pub fn config(a: &Args) -> Result<machine::Config, String> {
    config_with(a, FLASH)
}

/// The board the options describe, with `flash` when none is named.
pub fn config_with(a: &Args, flash: &str) -> Result<machine::Config, String> {
    let flash = crate::util::rooted(a.opt("--flash").unwrap_or(flash));
    let flash = std::fs::read(&flash).map_err(|e| format!("{}: {e}", flash.display()))?;
    let mut rotpk = [0u8; 32];
    if let Some(p) = a.opt("--rotpk") {
        let p = crate::util::rooted(p);
        match std::fs::read(&p) {
            Ok(h) if h.len() == 32 => rotpk.copy_from_slice(&h),
            _ => return Err(format!("{}: not a 32-byte root key hash", p.display())),
        }
    } else if let Some(h) = images::rotpk_of_flash(&flash) {
        rotpk = h;
    }
    Ok(machine::Config {
        flash: std::rc::Rc::new(flash),
        dram_mb: a.int("--dram").unwrap_or(16384) as u32,
        secure: a.flag("--secure"),
        rotpk,
        boot_core: a.int("--core").unwrap_or(machine::BOOT_CORE as u64) as usize,
        channel_mask: 0xf,
        jedec: [0xef, 0x40, 0x17],
        presets: Vec::new(),
        pm_alive: true,
        se_alive: true,
        xspi_stuck: false,
    })
}

pub const CONFIG_OPTS: &[&str] = &["--flash", "--rotpk", "--dram", "--core"];

/// Run printing the calls made inside the function `name` (an ELF's symbol or an address), `depth` levels
/// down: arguments on entry, x0 on return.
fn calls(m: &mut machine::Machine, core: usize, name: &str, depth: usize, limit: u64) -> cpu::Stop {
    let Some(root) = m.image.sym(name).or_else(|| crate::util::parse_int(name)) else {
        eprintln!("{name}: not a symbol of the image");
        return cpu::Stop::Limit;
    };
    let starts: std::collections::HashSet<u64> = m.image.functions.iter().map(|f| f.0).collect();
    let mut stack: Vec<(u64, u64, u64)> = Vec::new();
    let end = m.cpus[core].insns + limit;
    while m.cpus[core].insns < end {
        let c = &m.cpus[core];
        let (pc, sp, lr) = (c.pc, c.cur_sp(), c.x[30]);
        if let Some(&(ret, at, f)) = stack.last() {
            if pc == ret && sp == at {
                if stack.len() <= depth {
                    println!("{:w$}<- {} = {:#x}", "", m.image.at(f), c.x[0], w = 2 * stack.len());
                }
                stack.pop();
            }
        }
        if (pc == root || !stack.is_empty()) && (starts.contains(&pc) || pc == root) && stack.last().map(|t| (t.0, t.1)) != Some((lr, sp)) {
            stack.push((lr, sp, pc));
            if stack.len() <= depth {
                println!("{:w$}{}({:#x}, {:#x}, {:#x}, {:#x})", "", m.image.at(pc), c.x[0], c.x[1], c.x[2], c.x[3], w = 2 * stack.len());
            }
        }
        if let Err(stop) = m.cpus[core].step(&mut m.soc) {
            return stop;
        }
    }
    cpu::Stop::Limit
}

fn run(rest: &[String]) -> i32 {
    let mut valued = vec!["--base", "--count", "--until", "--calls", "--depth", "--names"];
    valued.extend_from_slice(CONFIG_OPTS);
    let a = match Args::parse(rest, &valued) {
        Ok(a) => a,
        Err(e) => {
            eprintln!("{e}");
            return 2;
        }
    };
    if a.pos.len() != 1 || a.check(&["--secure", "--trace", "--audit", "--io"]).is_err() {
        eprintln!("usage: sky1emu a64 IMAGE [--base ADDR] [--flash FILE] [--secure] [--dram MB] [--core N] [--count N] [--until ADDR] [--trace] [--io] [--audit]");
        return 2;
    }
    let img = match elf64::Image::load(&a.pos[0], a.int("--base").unwrap_or(machine::BL2_BASE)).and_then(|mut i| {
        if let Some(n) = a.opt("--names") {
            i.add_names(n)?;
        }
        Ok(i)
    }) {
        Ok(i) => std::rc::Rc::new(i),
        Err(e) => {
            eprintln!("{e}");
            return 2;
        }
    };
    let cfg = match config(&a) {
        Ok(c) => c,
        Err(e) => {
            eprintln!("{e}");
            return 2;
        }
    };
    let core = cfg.boot_core;
    let mut m = machine::Machine::new(img, &cfg, a.flag("--audit"));
    m.soc.trace = a.flag("--trace");
    let stops: Vec<u64> = a.all("--until").iter().filter_map(|s| crate::util::parse_int(s)).collect();
    let stop = match a.opt("--calls") {
        Some(name) => calls(&mut m, core, name, a.int("--depth").unwrap_or(3) as usize, a.int("--count").unwrap_or(4_000_000_000)),
        None => m.run(core, a.int("--count").unwrap_or(4_000_000_000), &stops),
    };
    print!("{}", m.soc.console(0));
    println!("\n-- {} after {} instructions, {} ms", m.describe(core, &stop), m.cpus[core].insns, m.soc.time / 1_000_000);
    let c = &m.cpus[core];
    for i in (0..30).step_by(4) {
        println!("   x{i:<2} {:016x} {:016x} {:016x} {:016x}", c.x[i], c.x[i + 1], c.x[(i + 2).min(30)], c.x[(i + 3).min(30)]);
    }
    println!("   sp {:#x} pc {:#x} pstate {:#x}", c.cur_sp(), c.pc, c.pstate());
    if a.flag("--io") {
        for io in &m.soc.io {
            println!("   {} {:#010x} {:#x}", if io.write { "W" } else { "R" }, io.addr, io.val);
        }
    }
    if !m.soc.unmodelled.is_empty() {
        println!("registers read that nothing had written and no model answers (read as 0):");
        for (a, n) in &m.soc.unmodelled {
            println!("   {a:#010x} x{n}");
        }
    }
    if a.flag("--audit") {
        let (n, faults) = m.audit_report();
        println!("audit: {n} returns checked, {} faults", faults.len());
        for f in faults.iter().take(20) {
            println!("   {f}");
        }
    }
    0
}

/// What the runs recorded in a coverage file executed of an image: an ELF (the open stages), or a raw
/// image with a file of names (`--names`, as out/emu/a64-re.sh makes for the vendor's).  cov.rs's own
/// report reads 32-bit ELF files only.
fn coverage(rest: &[String]) -> i32 {
    let a = match Args::parse(rest, &["--names", "--only", "--as"]) {
        Ok(a) => a,
        Err(e) => {
            eprintln!("{e}");
            return 2;
        }
    };
    if a.pos.len() < 2 || a.check(&["--all", "--partial", "--summary"]).is_err() {
        eprintln!("usage: sky1emu a64-cov FILE IMAGE... [--names SYMS --as NAME] [--partial] [--all] [--only PREFIX] [--summary]");
        return 2;
    }
    let cov = crate::cov::load(&a.pos[0]);
    for path in &a.pos[1..] {
        let img = match elf64::Image::load(path, machine::BL2_BASE).and_then(|mut i| {
            if let Some(n) = a.opt("--names") {
                i.add_names(n)?;
            }
            Ok(i)
        }) {
            Ok(i) => i,
            Err(e) => {
                eprintln!("{e}");
                return 2;
            }
        };
        let name = a.opt("--as").unwrap_or(&img.name).to_string();
        let Some(elf) = img.elf32.as_ref() else {
            eprintln!("{path}: no names (an ELF file, or --names)");
            return 2;
        };
        let ranges = crate::cov::normalize(cov.get(&name).cloned().unwrap_or_default());
        let mut fns = crate::cov::functions(elf, &ranges);
        if let Some(p) = a.opt("--only") {
            fns.retain(|f| f.name.starts_with(p) || f.file.starts_with(p));
        }
        let (total, ran): (u64, u64) = fns.iter().fold((0, 0), |(t, r), f| (t + f.size as u64, r + f.ran as u64));
        let never: Vec<&crate::cov::FnCov> = fns.iter().filter(|f| f.ran == 0).collect();
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
                if (f.ran == 0 || f.ran == f.size) && !a.flag("--all") {
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

/// Run `cmd` if it is one of this module's commands; None otherwise.
pub fn command(cmd: &str, rest: &[String]) -> Option<i32> {
    match cmd {
        "a64" => Some(run(rest)),
        "a64-test" => Some(test::main(rest)),
        "a64-selfcheck" => Some(selfcheck(rest)),
        "a64-cov" => Some(coverage(rest)),
        _ => None,
    }
}
