// SPDX-License-Identifier: BSD-2-Clause-Patent
//! The secure element: the Sky1 as the SE sees it, and the tests that compare the open SE firmware with
//! CIX's (the interpreter is ../arm).

pub mod crypto;
pub mod emu;
pub mod pyref;
pub mod soc;
pub mod test;
pub mod timed;

use crate::util::{cix_debug, rooted, Args};

/// The module's lines of `sky1emu`'s usage.
pub const USAGE: &str = "SE (Cortex-M33):\n\
    \x20 se ELF [--flash IMAGE] [--trace] [--stub NAME=R0] [--stop NAME] [--max N]\n\
    \x20                                         run an image, print its bus writes and where it stopped\n\
    \x20 se-test OPEN.elf FLASH.bin [--services] [--suspend] [--crash] [--secure] [--inputs] [--failures] [--paths]\n\
    \x20         [--ddr] [--ddr-suite [--eye]] [--full] [--life-cycle N] [--keys] [--ddr-feature NAME=V]...\n\
    \x20         [--cix CIX.elf] [--only TEXT] [--uart [cix:]TEXT] [--cov FILE] [--audit] [-v]\n\
    \x20                                         compare the open image with CIX's, in step by simulated time\n\
    \x20 se-test OPEN.elf FLASH.bin --pyref [--services] [--suspend] [--ddr] [--secure] [--keys] [--dump PREFIX]\n\
    \x20                                         the comparison as the Python test made it (the port's proof)\n\
    \x20 arm-check ELF...                        decode every instruction of an image";

/// Run `cmd` if it is one of this module's commands; None otherwise.
pub fn command(cmd: &str, rest: &[String]) -> Option<i32> {
    let r = match cmd {
        "se" => run(rest),
        "se-test" => test(rest),
        "arm-check" => check(rest),
        _ => return None,
    };
    Some(r.unwrap_or_else(|e| {
        eprintln!("{e}");
        2
    }))
}

fn run(args: &[String]) -> Result<i32, String> {
    let a = Args::parse(args, &["--flash", "--stub", "--stop", "--max"])?;
    a.check(&["--trace"])?;
    let elf = a.pos.first().ok_or("usage: sky1emu se ELF [--flash IMAGE] [--trace] [--stub NAME=R0] [--stop NAME] [--max N]")?;
    let flash = match a.opt("--flash") {
        Some(p) => std::fs::read(rooted(p)).map_err(|e| format!("{p}: {e}"))?,
        None => Vec::new(),
    };
    let mut stubs = Vec::new();
    let stub_args = a.all("--stub");
    for s in &stub_args {
        let (name, v) = s.split_once('=').ok_or("--stub NAME=R0")?;
        stubs.push((name, emu::constant(crate::util::parse_int(v).ok_or("--stub NAME=R0")? as u32)));
    }
    let stop_args = a.all("--stop");
    let stops: Vec<&str> = stop_args.iter().map(|s| s.as_str()).collect();
    let soc = soc::SkySoc::new(&flash, 4, Default::default(), false);
    let mut se = emu::Se::new(elf, soc, &stubs, &stops)?;
    se.trace = a.flag("--trace");
    se.run(None, a.int("--max").unwrap_or(50_000_000))?;
    for c in &se.calls {
        println!("call {c}");
    }
    for w in &se.cpu.writes {
        println!("w {:32} {:#010x} {} {:#x}", se.cpu.func(w.pc), w.addr, w.size, w.value);
    }
    match &se.poll {
        Some((f, addr, Some(reg))) => println!("polling in {f} at {addr:#x}, last read {reg:#x}"),
        Some((f, addr, None)) => println!("loop in {f} at {addr:#x}"),
        None => println!("stopped at {}", se.why()),
    }
    Ok(0)
}

fn test(args: &[String]) -> Result<i32, String> {
    let a = Args::parse(args, &["--cix", "--life-cycle", "--count", "--ddr-feature", "--dump", "--only", "--cov", "--uart"])?;
    a.check(&["--pyref", "--services", "--suspend", "--keys", "--ddr", "--secure", "-v", "--audit", "--full", "--inputs", "--failures", "--paths", "--crash", "--ddr-suite", "--eye"])?;
    if a.pos.len() != 2 {
        return Err("usage: sky1emu se-test OPEN_se_fw.elf FLASH.bin [--cix CIX_se_fw.elf] [options]".into());
    }
    let open = rooted(&a.pos[0]).display().to_string();
    let cix = match a.opt("--cix") {
        Some(p) => rooted(p).display().to_string(),
        None => cix_debug("debug/se_fw/se_fw.elf")?,
    };
    let flash = std::fs::read(rooted(&a.pos[1])).map_err(|e| format!("{}: {e}", a.pos[1]))?;
    if a.flag("--pyref") {
        pyref::main(&a, &open, &cix, &flash)
    } else {
        test::main(&a, &open, &cix, &flash)
    }
}

/// Decode every instruction of the images (by their mapping symbols): an encoding the interpreter has
/// not got is reported here rather than when a test first runs into it.
fn check(args: &[String]) -> Result<i32, String> {
    let mut bad = 0;
    for path in args {
        let elf = crate::elf::Elf::load(rooted(path))?;
        // $t and $d mark where code and data start
        let mut marks: Vec<(u32, bool)> = elf
            .symbols
            .iter()
            .filter(|s| s.name == "$t" || s.name == "$d" || s.name.starts_with("$t.") || s.name.starts_with("$d."))
            .map(|s| (s.value & !1, s.name.starts_with("$t")))
            .collect();
        marks.sort();
        marks.dedup();
        let text: Vec<(u32, u32)> = elf.sections.iter().filter(|s| s.flags & 4 != 0 && s.kind == 1).map(|s| (s.addr, s.addr + s.size)).collect();
        let mut cpu = crate::arm::Arm::new(crate::arm::NoBus);
        cpu.add_ram(soc::SRAM, soc::SRAM_SIZE);
        for s in &elf.segments {
            cpu.write_bytes(s.paddr, &s.data);
        }
        let (mut n, mut unknown) = (0, 0);
        for (k, (start, code)) in marks.iter().enumerate() {
            if !code {
                continue;
            }
            let Some(sec) = text.iter().find(|(lo, hi)| lo <= start && start < hi) else { continue };
            let end = marks.get(k + 1).map_or(sec.1, |m| m.0.min(sec.1));
            let mut pc = *start;
            while pc < end {
                match cpu.insn_at(pc) {
                    Ok(i) => pc += i.len as u32,
                    Err(e) => {
                        unknown += 1;
                        if unknown <= 20 {
                            println!("  {e}");
                        }
                        pc += if crate::arm::decode::is32(cpu.mem.peek(pc, 2) as u16) { 4 } else { 2 };
                    }
                }
                n += 1;
            }
        }
        println!("{path}: {n} instructions, {unknown} not decoded");
        bad += unknown;
    }
    Ok((bad != 0) as i32)
}
