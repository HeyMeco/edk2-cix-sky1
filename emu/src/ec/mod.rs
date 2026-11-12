// SPDX-License-Identifier: BSD-2-Clause-Patent
//! The Orion O6 embedded controller: an 8051 interpreter (cpu.rs), the
//! IT5570 (chip.rs) and the board around it (board.rs), and the tests
//! that compare the open EC firmware with Radxa's (test.rs: the port of
//! the former scripts/open/test_ec.py; gaps.rs: what that left out;
//! seqmatch.rs: Python's SequenceMatcher), coverage by the SDCC listings
//! (cov.rs), the NTC table (ntc.rs). See emu/README.md, "EC (Orion O6)".

mod board;
mod chip;
mod cov;
mod cpu;
mod gaps;
mod ntc;
mod seqmatch;
mod test;

/// The module's lines of `sky1emu`'s usage.
pub const USAGE: &str = "EC (IT5570, 8051; Orion O6):\n\
\x20 ec IMG [MS] [SPI]                       run an EC image on the O6 model, print what it did\n\
\x20 ec-test [--open IMG] [--vendor IMG] [--flash SPI] [-v] [--port] [--list] [--dump DIR] [--cov FILE] [scenario...]\n\
\x20                                         compare the open EC firmware with Radxa's\n\
\x20 ec-probe IMG SCENARIO [--x ADDR]... [--from MS]   one image through a scenario: its events, and\n\
\x20                                         every write to the external RAM addresses given\n\
\x20 ec-cov FILE BUILD_DIR [--partial] [--all] [--lines]  the open image's functions the runs in FILE executed\n\
\x20 ec-ntc-table VENDOR.bin OUT.h          firmware/ec's NTC table, from the vendor's own conversion";

/// Run `cmd` if it is one of this module's commands; None otherwise.
pub fn command(cmd: &str, rest: &[String]) -> Option<i32> {
    Some(match cmd {
        "ec" => run_one(rest),
        "ec-test" => test::main(rest),
        "ec-probe" => test::probe(rest),
        "ec-cov" => cov::main(rest),
        "ec-ntc-table" => ntc::main(rest),
        _ => return None,
    })
}

/// ec_emu.py's __main__: one image on the O6 board, its events.
fn run_one(args: &[String]) -> i32 {
    let Some(img) = args.first() else {
        eprintln!("usage: sky1emu ec IMG [MS] [SPI]");
        return 2;
    };
    let img = match std::fs::read(crate::util::rooted(img)) {
        Ok(d) => d,
        Err(e) => {
            eprintln!("{img}: {e}");
            return 2;
        }
    };
    let ms: f64 = args.get(1).and_then(|s| s.parse().ok()).unwrap_or(2000.0);
    let fl = args.get(2).and_then(|p| test::load_flash(p));
    let mut ec = chip::Ec::new(&img, true, false, fl.as_deref());
    ec.run(ms);
    if let Some(h) = &ec.halted {
        println!("halt {h}");
    }
    for e in &ec.io.log.events {
        println!("{:10.3} {:<4} {}", e.ms, e.kind, e.detail);
    }
    println!("pc {:04X} bank {}", ec.cpu.pc, ec.cpu.bank());
    0
}
