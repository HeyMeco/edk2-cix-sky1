// SPDX-License-Identifier: BSD-2-Clause-Patent
//! The secure element: the Sky1 as the SE sees it, and the tests that compare the open SE firmware with
//! CIX's (the interpreter is ../arm).
//!
//! Not written yet: built only with `--features se`.

/// The module's lines of `sky1emu`'s usage.
pub const USAGE: &str = "SE (Cortex-M33):\n  (nothing yet)";

/// Run `cmd` if it is one of this module's commands; None otherwise.
pub fn command(cmd: &str, rest: &[String]) -> Option<i32> {
    let _ = (cmd, rest);
    None
}
