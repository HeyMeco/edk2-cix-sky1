// SPDX-License-Identifier: BSD-2-Clause-Patent
//! Emulators for the Sky1 boot firmware and the tests that compare the
//! open firmware with the vendors' (see README.md).

mod cov;
mod elf;
mod pm;
mod util;
mod xtensa;

fn usage() -> i32 {
    eprintln!(
        "usage: sky1emu COMMAND ...\n\
         \n\
         CSU_PM (Xtensa):\n\
         \x20 pm ELF [--count N] [--until SYMBOL]     run an image, print console, I2C, where it stopped\n"
    );
    2
}

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let Some(cmd) = args.first() else { std::process::exit(usage()) };
    let rest = &args[1..];
    let code = match cmd.as_str() {
        "pm" => pm::soc::main(rest),
        "pm-test" => pm::test::main(rest),
        "pm-faults" => pm::faults::main(rest),
        "pm-vf" => pm::vf::main(rest),
        "pm-script" => pm::script::main(rest),
        "cov" => cov::main(rest),
        _ => usage(),
    };
    std::process::exit(code);
}
