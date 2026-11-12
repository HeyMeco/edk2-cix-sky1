// SPDX-License-Identifier: BSD-2-Clause-Patent
//! The Orion O6 embedded controller: an 8051 interpreter, the IT5570 and the board around it, and the
//! tests that compare the open EC firmware with Radxa's.
//!
//! Not written yet: built only with `--features ec`.

/// The module's lines of `sky1emu`'s usage.
pub const USAGE: &str = "EC (IT5570, 8051):\n  (nothing yet)";

/// Run `cmd` if it is one of this module's commands; None otherwise.
pub fn command(cmd: &str, rest: &[String]) -> Option<i32> {
    let _ = (cmd, rest);
    None
}
