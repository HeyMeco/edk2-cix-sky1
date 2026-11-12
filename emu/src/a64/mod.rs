// SPDX-License-Identifier: BSD-2-Clause-Patent
//! The AArch64 boot stages: an interpreter and the Sky1 as TF-A sees it, to compare the open BL2 and BL31
//! with CIX's.
//!
//! Not written yet: built only with `--features a64`.

/// The module's lines of `sky1emu`'s usage.
pub const USAGE: &str = "BL2, BL31 (AArch64):\n  (nothing yet)";

/// Run `cmd` if it is one of this module's commands; None otherwise.
pub fn command(cmd: &str, rest: &[String]) -> Option<i32> {
    let _ = (cmd, rest);
    None
}
