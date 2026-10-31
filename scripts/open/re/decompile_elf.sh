#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause-Patent
# Decompile an ELF that carries its own symbols and DWARF with Ghidra.
#   decompile_elf.sh FIRMWARE.elf OUTPREFIX [PROCESSOR]
# PROCESSOR defaults to Xtensa:LE:32:default (CSU_PM).  Ghidra does not
# know the core's FLIX bundles (xtensa_flix.py), so functions with them
# come out cut short.
set -e
GHIDRA=${GHIDRA:-$(ls -d /opt/homebrew/Cellar/ghidra/*/libexec 2>/dev/null | tail -1)}
export JAVA_HOME=${JAVA_HOME:-/opt/homebrew/opt/openjdk@25/libexec/openjdk.jdk/Contents/Home}
HERE=$(cd "$(dirname "$0")" && pwd)
PROJ=$(mktemp -d)
OUT="$(cd "$(dirname "$2")" && pwd)/$(basename "$2")"
"$GHIDRA/support/analyzeHeadless" "$PROJ" re -import "$1" -processor "${3:-Xtensa:LE:32:default}" \
  -scriptPath "$HERE/ghidra" -postScript DecompileAll.java "$OUT" -deleteProject >/dev/null 2>&1 || true
rm -rf "$PROJ"
ls -la "$OUT".c
