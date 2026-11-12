// SPDX-License-Identifier: BSD-2-Clause-Patent
//! Drive one CSU_PM image by hand, to look into what a test found.
//!
//!   sky1emu pm-script ELF [--board NAME] [--set NAME=VALUE] [--ddr-dfs] 'COMMAND; COMMAND; ...'
//!
//! The image is booted to idle first.  Commands (numbers as in C):
//!   ms N                    run N ms of simulated time
//!   run N                   run N instructions
//!   scmi CH PROTO MSG [ARGS...]   an SCMI command, its answer
//!   scmi-len LEN CH PROTO MSG [ARGS...]   with a length of its own
//!   msg CH ID [ARGS...]     one of CIX's PM messages
//!   post CH ID [ARGS...]    a PM message sent, not waited for
//!   cpu                     pc, PS, interrupt enables and lines, timers
//!   fc OFF VALUE            write a fast channel of mailbox 0
//!   temp T                  every temperature sensor at T
//!   pvt I T                 PVT sensor I at T
//!   pvtcode CODE            every PVT sensor's raw code
//!   unmask LINE BITS        unmask sources of an interrupt aggregator line
//!   raw LINE BITS           the sources of an aggregator line as the hardware drives them
//!   send CH PROTO MSG [ARGS...]   an SCMI command sent, not waited for
//!   until-sp LO HI          run until the stack pointer enters [LO, HI)
//!   audit                   from now on, check at every windowed return that the caller's
//!                           registers are what they were at the call, and after every
//!                           interrupt that the interrupted code's are
//!   until-arg SYMBOL IDX MIN  run until SYMBOL is entered with argument IDX >= MIN
//!   watch LO HI [BYTE]      stop at the next write into [LO, HI) of RAM (of that byte only)
//!   stop SYMBOL             stop when SYMBOL is reached (then: regs, bt...)
//!   count SYMBOL MS         how often SYMBOL is entered in MS of simulated time
//!   nostop SYMBOL
//!   regs                    pc and a0..a15
//!   win                     the register windows and the exception PCs
//!   bt                      the call chain
//!   trail                   the last instructions run, by function
//!   mem ADDR [WORDS]        memory or registers, as words
//!   str ADDR                a string
//!   poke ADDR VALUE         a word put into the model's memory
//!   save LO HI FILE         memory to a file (for dwarf_dump.py --ram LO:FILE)
//!   sym NAME                a symbol's address
//!   writes [N]              the last N register writes
//!   wrote LO [HI]           every write so far to an address or range
//!   i2c [N]                 the last N I2C transfers
//!   console                 the UART's output so far

use super::soc::{self, Cpu};
use super::test::{self, Opts};
use crate::util::{parse_int, Args};

fn num(s: &str, cpu: &Cpu) -> Result<u32, String> {
    if let Some(v) = parse_int(s) {
        return Ok(v as u32);
    }
    if let Some(h) = s.strip_prefix('a') {
        if let Ok(i) = h.parse::<u32>() {
            return Ok(cpu.a(i));
        }
    }
    cpu.sym(s).or_else(|| cpu.all_syms.get(s).map(|x| x.0)).ok_or(format!("{s}: not a number or a symbol"))
}

fn command(cpu: &mut Cpu, line: &str) -> Result<(), String> {
    let f: Vec<&str> = line.split_whitespace().collect();
    let Some(cmd) = f.first() else { return Ok(()) };
    let n = |i: usize, cpu: &Cpu| -> Result<u32, String> { num(f.get(i).ok_or(format!("{cmd}: argument {i} missing"))?, cpu) };
    let rest = |from: usize, cpu: &Cpu| -> Result<Vec<u32>, String> { f.iter().skip(from).map(|x| num(x, cpu)).collect() };
    match *cmd {
        "ms" => {
            let t: f64 = f.get(1).and_then(|x| x.parse().ok()).ok_or("ms N")?;
            let why = soc::run_for(cpu, t);
            println!("ms {t}: {}", why.unwrap_or_else(|| "ran".into()));
        }
        "run" => {
            // (past the stop the image is at)
            let here = cpu.pc;
            if cpu.has_stop(here) {
                cpu.del_stop(here);
                let _ = cpu.step();
                cpu.set_stop(here, &cpu.func_name(here));
            }
            let why = cpu.run(n(1, cpu)? as u64);
            println!("run: {} at {}", why.unwrap_or_else(|| "ran".into()), cpu.func(cpu.pc));
        }
        "scmi" => {
            let (ch, p, m, args) = (n(1, cpu)?, n(2, cpu)?, n(3, cpu)?, rest(4, cpu)?);
            let t0 = cpu.io.time;
            let v = test::to_val(soc::scmi(cpu, p, m, &args, ch as usize, 3_000_000));
            println!("ch{ch} {p:#04x}/{m}{args:x?}: {v}  ({:.2} ms)", (cpu.io.time - t0) as f64 * 1000.0 / soc::TIME_HZ as f64);
        }
        "scmi-len" => {
            let (len, ch, p, m, args) = (n(1, cpu)?, n(2, cpu)?, n(3, cpu)?, n(4, cpu)?, rest(5, cpu)?);
            let v = test::to_val(soc::scmi_raw(cpu, p, m, &args, ch as usize, 3_000_000, Some(len), 0));
            println!("ch{ch} {p:#04x}/{m}{args:x?} length {len}: {v}");
        }
        "msg" => {
            let (ch, id, args) = (n(1, cpu)?, n(2, cpu)?, rest(3, cpu)?);
            let t0 = cpu.io.time;
            let v = test::pm_msg(cpu, ch as usize, id, &args);
            println!("ch{ch} message {id:#x}{args:x?}: {v}  ({:.2} ms)", (cpu.io.time - t0) as f64 * 1000.0 / soc::TIME_HZ as f64);
        }
        "post" => {
            // a PM message put in the channel and its doorbell rung, not waited for
            let (ch, id, args) = (n(1, cpu)?, n(2, cpu)?, rest(3, cpu)?);
            let base = soc::MBOX + ch * 0x10000;
            let m = &mut cpu.bus.mem;
            m.write32(base + 4, m.read32(base + 4) & !1);
            m.write32(base + 0x18, (m.read32(base + 0x18) & !0xffff) | id);
            m.write32(base, 0x3f | 0x0c << 8);
            m.write32(base + 0x10, 0);
            m.write32(base + 0x14, 4 + 4 * args.len() as u32);
            for (i, a) in args.iter().enumerate() {
                m.write32(base + 0x1c + 4 * i as u32, *a);
            }
            cpu.bus.ring(&mut cpu.io, ch as usize, 1);
        }
        "cpu" => {
            println!(
                "pc {:#010x} {}; waiting {}; ps {:#x}; intenable {:#010x}; lines {:#010x}; latched {:#010x}; time {:.3} ms; ccount {:#x}; ccompare {:x?}",
                cpu.pc, cpu.func(cpu.pc), cpu.waiting, cpu.ps(), cpu.intenable, cpu.io.int_lines, cpu.int_latched, cpu.io.time as f64 * 1000.0 / soc::TIME_HZ as f64, cpu.cc as u32, cpu.ccompare
            );
        }
        "fc" => {
            let (off, v) = (n(1, cpu)?, n(2, cpu)?);
            test::ask(cpu, 0, &test::Ask::Fc(off, v));
        }
        "temp" => {
            let t: f64 = f.get(1).and_then(|x| x.parse().ok()).ok_or("temp T")?;
            test::temps(cpu, t);
        }
        "unmask" => {
            // an aggregator line's sources unmasked: unmask LINE BITS
            test::unmask(cpu, n(1, cpu)?, n(2, cpu)?);
        }
        "raw" => {
            // an aggregator line's sources as the hardware drives them: raw LINE BITS
            test::set_raw(cpu, n(1, cpu)? as usize, n(2, cpu)?);
        }
        "pvt" => {
            // one PVT sensor's temperature: pvt I T
            let i = n(1, cpu)? as usize;
            let t: f64 = f.get(2).and_then(|x| x.parse().ok()).ok_or("pvt I T")?;
            cpu.bus.pvt_temp[i] = Some(t);
            cpu.bus.pvt_update(&mut cpu.io);
        }
        "pvtcode" => {
            // every PVT sensor's raw code, as the hardware could give it
            let code = n(1, cpu)?;
            cpu.bus.pvt_temp = [None; soc::PVT_N];
            for i in 0..soc::PVT_N as u32 {
                cpu.bus.mem.write32(soc::PVT_BASE + 0x1000 * (i + 1) + 4, code | code << 10 | code << 20);
            }
        }
        "stop" => {
            let a = n(1, cpu)?;
            cpu.set_stop(a, f[1]);
        }
        "nostop" => {
            let a = n(1, cpu)?;
            cpu.del_stop(a);
        }
        "until-sp" => {
            // run until the stack pointer is in [LO, HI)
            let (lo, hi) = (n(1, cpu)?, n(2, cpu)?);
            let mut last = String::new();
            for _ in 0..20_000_000u64 {
                let before = (cpu.pc, cpu.a(1));
                if let Err(e) = cpu.step() {
                    println!("until-sp: {e}");
                    break;
                }
                if (lo..hi).contains(&cpu.a(1)) && !(lo..hi).contains(&before.1) {
                    last = format!("sp {:#x} -> {:#x} by the instruction at {:#x} ({})", before.1, cpu.a(1), before.0, cpu.func(before.0));
                    break;
                }
            }
            println!("until-sp: {last}");
        }
        "audit" => {
            cpu.audit = Some(std::collections::HashMap::new());
        }
        "until-arg" => {
            // run until SYMBOL is entered with argument IDX at least MIN
            let (a, idx, min) = (n(1, cpu)?, n(2, cpu)?, n(3, cpu)?);
            let had = cpu.has_stop(a);
            cpu.set_stop(a, f[1]);
            let mut hit = false;
            for _ in 0..200_000 {
                if cpu.pc == a {
                    if cpu.arg(idx) >= min {
                        hit = true;
                        break;
                    }
                    cpu.del_stop(a);
                    let _ = cpu.step();
                    cpu.set_stop(a, f[1]);
                }
                match cpu.run(5_000_000) {
                    Some(w) if w == f[1] => {}
                    other => {
                        println!("until-arg: {}", other.unwrap_or_else(|| "ran".into()));
                        break;
                    }
                }
            }
            if !had {
                cpu.del_stop(a);
            }
            println!("until-arg: {}", if hit { format!("{} with argument {idx} = {:#x}", f[1], cpu.arg(idx)) } else { "not seen".into() });
        }
        "watch" => {
            // stop at the next write into [LO, HI) of RAM (of bytes BYTE only, when given)
            cpu.watch = Some((n(1, cpu)?, n(2, cpu)?));
            cpu.watch_byte = if f.len() > 3 { Some(n(3, cpu)? as u8) } else { None };
        }
        "send" => {
            // an SCMI command sent, not waited for
            let (ch, p, m, args) = (n(1, cpu)?, n(2, cpu)?, n(3, cpu)?, rest(4, cpu)?);
            super::faults::send(cpu, ch as usize, p, m, &args);
        }
        "regs" => {
            println!("pc {:#010x} {}", cpu.pc, cpu.func(cpu.pc));
            for i in 0..16 {
                print!("a{i:<2} {:#010x}{}", cpu.a(i), if i % 4 == 3 { "\n" } else { "  " });
            }
        }
        "win" => {
            // the register windows: base, start bits, each frame's a0 and a1
            println!("windowbase {} windowstart {:#06x} ps {:#x}", cpu.wb4 >> 2, cpu.ws, cpu.ps());
            for k in 0..16 {
                let (a0, a1) = (cpu.ar[k * 4], cpu.ar[k * 4 + 1]);
                println!("  window {k:2}{} a0 {a0:#010x} a1 {a1:#010x}  {}", if cpu.ws >> k & 1 != 0 { "*" } else { " " }, cpu.func((a0 & 0x3fffffff) | 0));
            }
            for l in 1..=7u32 {
                println!("  EPC{l} {:#010x} {}", cpu.sr[(176 + l) as usize], cpu.func(cpu.sr[(176 + l) as usize]));
            }
        }
        "bt" => println!("{}", cpu.backtrace_names(16)),
        "trail" => println!("{}", cpu.trail()),
        "mem" => {
            let a = n(1, cpu)?;
            let words = if f.len() > 2 { n(2, cpu)? } else { 8 };
            for i in 0..words {
                if i % 8 == 0 {
                    print!("{}{:#010x}:", if i == 0 { "" } else { "\n" }, a + 4 * i);
                }
                let v = match cpu.peek(a + 4 * i, 4) {
                    Some(b) => u32::from_le_bytes([b[0], b[1], b[2], b[3]]),
                    None => cpu.bus.mem.read32(a + 4 * i),
                };
                print!(" {v:08x}");
            }
            println!();
        }
        "count" => {
            // how often SYMBOL is entered in MS of simulated time: count SYMBOL MS
            let a = n(1, cpu)?;
            let t: f64 = f.get(2).and_then(|x| x.parse().ok()).ok_or("count SYMBOL MS")?;
            let end = cpu.io.time + (t * soc::TIME_HZ as f64 / 1000.0) as u64;
            cpu.set_stop(a, "count");
            let mut k = 0u32;
            while cpu.io.time < end {
                match soc::run_cycles(cpu, end - cpu.io.time).as_deref() {
                    Some("count") => {
                        k += 1;
                        cpu.del_stop(a);
                        let _ = cpu.step();
                        cpu.set_stop(a, "count");
                    }
                    Some(w) => {
                        println!("count: {w}");
                        break;
                    }
                    None => break,
                }
            }
            cpu.del_stop(a);
            println!("{}: {k} times in {t} ms", f[1]);
        }
        "poke" => {
            // a word written into the model's memory (not seen by devices): poke ADDR VALUE
            let (a, v) = (n(1, cpu)?, n(2, cpu)?);
            cpu.bus.mem.write32(a, v);
        }
        "save" => {
            // memory to a file (dwarf_dump.py --ram LO:FILE): save LO HI FILE
            let (lo, hi) = (n(1, cpu)?, n(2, cpu)?);
            let path = f.get(3).ok_or("save LO HI FILE")?;
            let data: Vec<u8> = (lo..hi).map(|a| cpu.read(a, 1) as u8).collect();
            std::fs::write(path, data).map_err(|e| format!("{path}: {e}"))?;
        }
        "str" => {
            let a = n(1, cpu)?;
            let bytes: Vec<u8> = (0..200).map_while(|i| cpu.peek(a + i, 1).map(|b| b[0]).filter(|b| *b != 0)).collect();
            println!("{:?}{}", String::from_utf8_lossy(&bytes), if bytes.len() == 200 { " ..." } else { "" });
        }
        "sym" => println!("{} = {:#010x}", f.get(1).unwrap_or(&""), n(1, cpu)?),
        "writes" => {
            // (not the hardware mutexes, which are written all the time)
            let k = if f.len() > 1 { n(1, cpu)? as usize } else { 20 };
            let w: Vec<_> = cpu.writes.iter().filter(|w| test::counted(w.addr)).collect();
            for w in &w[w.len().saturating_sub(k)..] {
                println!("  {:#010x} = {:#x}  ({})", w.addr, w.value, cpu.func(w.pc));
            }
        }
        "wrote" => {
            let (lo, hi) = (n(1, cpu)?, if f.len() > 2 { n(2, cpu)? } else { n(1, cpu)? + 4 });
            for (i, w) in cpu.writes.iter().enumerate().filter(|(_, w)| lo <= w.addr && w.addr < hi) {
                println!("  write {i}: {:#010x} = {:#x}  ({})", w.addr, w.value, cpu.func(w.pc));
            }
        }
        "i2c" => {
            let k = if f.len() > 1 { n(1, cpu)? as usize } else { 20 };
            for l in &cpu.bus.i2c_log[cpu.bus.i2c_log.len().saturating_sub(k)..] {
                println!("  {} {}", l.bus, l.what);
            }
        }
        "console" => println!("{}", cpu.bus.console.iter().map(|b| *b as char).collect::<String>()),
        _ => return Err(format!("no command {cmd}")),
    }
    Ok(())
}

pub fn main(args: &[String]) -> i32 {
    let r = (|| -> Result<(), String> {
        let a = Args::parse(args, &["--board", "--set"])?;
        let (elf, script) = match a.pos.as_slice() {
            [e, s] => (e, s),
            _ => return Err("usage: sky1emu pm-script ELF [--board NAME] 'COMMAND; ...'".to_string()),
        };
        let mut opts = Opts::default();
        if let Some(b) = a.opt("--board") {
            opts.board = Some(test::board_config(b, &a.all("--set"))?);
        }
        opts.ddr_dfs = a.flag("--ddr-dfs");
        soc::POLL.with(|p| p.set(2_000));
        let (pm, why, _) = test::boot(elf, &opts)?;
        let mut cpu = pm.cpu;
        println!("boot: {}", why.unwrap_or_else(|| "None".into()));
        for line in script.split(';') {
            command(&mut cpu, line.trim())?;
        }
        Ok(())
    })();
    match r {
        Ok(()) => 0,
        Err(e) => {
            eprintln!("pm-script: {e}");
            2
        }
    }
}
