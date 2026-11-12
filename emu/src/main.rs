// SPDX-License-Identifier: BSD-2-Clause-Patent
//! Emulators for the Sky1 boot firmware and the tests that compare the
//! open firmware with the vendors' (see README.md).

#[cfg(feature = "a64")]
mod a64;
#[cfg(feature = "arm")]
mod arm;
mod cov;
#[cfg(feature = "ec")]
mod ec;
mod elf;
mod pm;
#[cfg(feature = "se")]
mod se;
#[cfg(feature = "sfh")]
mod sfh;
mod util;
mod xtensa;

/// The commands of the emulators built in by a feature: each module's
/// `command` answers for its own (None: not one of mine) and its `USAGE`
/// lists them.
fn other(cmd: &str, rest: &[String]) -> Option<i32> {
    let _ = (cmd, rest);
    #[cfg(feature = "ec")]
    if let Some(code) = ec::command(cmd, rest) {
        return Some(code);
    }
    #[cfg(feature = "se")]
    if let Some(code) = se::command(cmd, rest) {
        return Some(code);
    }
    #[cfg(feature = "sfh")]
    if let Some(code) = sfh::command(cmd, rest) {
        return Some(code);
    }
    #[cfg(feature = "a64")]
    if let Some(code) = a64::command(cmd, rest) {
        return Some(code);
    }
    None
}

fn usage() -> i32 {
    eprintln!(
        "usage: sky1emu COMMAND ...\n\
         \n\
         CSU_PM (Xtensa):\n\
         \x20 pm ELF [--count N] [--until SYMBOL]     run an image, print console, I2C, where it stopped\n\
         \x20 pm-test [OPEN.elf] [--scmi] [--power] [--services] [--runtime MS] [--full] [--board B] ...\n\
         \x20                                         compare the open image with CIX's\n\
         \x20 pm-faults OPEN.elf                      the open image on failing I2C buses\n\
         \x20 pm-vf OPEN.elf [--matrix] ...           V/F tables on simulated parts\n\
         \x20 pm-script ELF 'COMMAND; ...'            drive one image by hand\n\
         \x20 cov FILE ELF... [--all] [--partial]     what the tests ran of an image (--cov FILE)"
    );
    #[cfg(feature = "ec")]
    eprintln!("{}", ec::USAGE);
    #[cfg(feature = "se")]
    eprintln!("{}", se::USAGE);
    #[cfg(feature = "sfh")]
    eprintln!("{}", sfh::USAGE);
    #[cfg(feature = "a64")]
    eprintln!("{}", a64::USAGE);
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
        _ => other(cmd, rest).unwrap_or_else(usage),
    };
    std::process::exit(code);
}
