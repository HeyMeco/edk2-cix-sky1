#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause-Patent
# Decompile a raw AArch64 image with Ghidra, naming the functions that
# funcmatch.py matched against a reference build.
#   decompile.sh IMAGE.bin BASE NAMES.txt OUTPREFIX
set -e
GHIDRA=${GHIDRA:-$(ls -d /opt/homebrew/Cellar/ghidra/*/libexec 2>/dev/null | tail -1)}
export JAVA_HOME=${JAVA_HOME:-/opt/homebrew/opt/openjdk@25/libexec/openjdk.jdk/Contents/Home}   # Ghidra 12 needs JDK 25
HERE=$(cd "$(dirname "$0")" && pwd)
PROJ=$(mktemp -d)
"$GHIDRA/support/analyzeHeadless" "$PROJ" re -import "$1" -processor AARCH64:LE:64:v8A \
  -loader BinaryLoader -loader-baseAddr "$2" -scriptPath "$HERE/ghidra" \
  -postScript NameAndDecompile.java "$(cd "$(dirname "$3")" && pwd)/$(basename "$3")" "$4" -deleteProject >/dev/null 2>&1 || true
rm -rf "$PROJ"
ls -la "$4".c "$4"-unmatched.c
