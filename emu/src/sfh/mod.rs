// SPDX-License-Identifier: BSD-2-Clause-Patent
//! The sensor fusion hub: its model and the tests of the open SFH firmware (the interpreter is ../arm).

pub mod pyref;
pub mod test;

use crate::util::{rooted, Args};

/// CIX's image, in the vendor tree
const CIX: &str = "edk2-non-osi/Platform/CIX/Sky1/PackageTool/Firmwares/sfh_fw.bin";

/// The module's lines of `sky1emu`'s usage.
pub const USAGE: &str = "SFH (Cortex-M33):\n\
    \x20 sfh-test OPEN_sfh_fw.bin [CIX_sfh_fw.bin] [--elf OPEN.elf] [--only TEXT] [--cov FILE] [-v] [--pyref]\n\
    \x20                                         compare the open image with CIX's";

/// Run `cmd` if it is one of this module's commands; None otherwise.
pub fn command(cmd: &str, rest: &[String]) -> Option<i32> {
    let r = match cmd {
        "sfh-test" => test(rest),
        _ => return None,
    };
    Some(r.unwrap_or_else(|e| {
        eprintln!("{e}");
        2
    }))
}

fn test(args: &[String]) -> Result<i32, String> {
    let a = Args::parse(args, &["--elf", "--only", "--cov"])?;
    a.check(&["--pyref", "-v"])?;
    let open = a.pos.first().ok_or("usage: sky1emu sfh-test OPEN_sfh_fw.bin [CIX_sfh_fw.bin]")?;
    let cix = a.pos.get(1).map(|s| s.as_str()).unwrap_or(CIX);
    let read = |p: &str| std::fs::read(rooted(p)).map_err(|e| format!("{p}: {e}"));
    if a.flag("--pyref") {
        return pyref::main(&read(open)?, &read(cix)?);
    }
    // the open image's ELF (for the coverage report): beside the binary unless said
    let beside = rooted(open).with_extension("elf");
    let elf = a.opt("--elf").map(|s| rooted(s).display().to_string()).or_else(|| beside.exists().then(|| beside.display().to_string()));
    test::main(&read(open)?, elf.as_deref(), &read(cix)?, a.flag("-v"), a.opt("--cov"), a.opt("--only"))
}
