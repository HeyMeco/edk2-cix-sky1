// SPDX-License-Identifier: BSD-2-Clause-Patent
//! The sensor fusion hub: its model and the tests of the open SFH firmware (the interpreter is ../arm).
//!
//! Not written yet: built only with `--features sfh`.

/// The module's lines of `sky1emu`'s usage.
pub const USAGE: &str = "SFH (Cortex-M33):\n  (nothing yet)";

/// Run `cmd` if it is one of this module's commands; None otherwise.
pub fn command(cmd: &str, rest: &[String]) -> Option<i32> {
    let _ = (cmd, rest);
    None
}
