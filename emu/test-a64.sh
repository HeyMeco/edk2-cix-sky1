#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause-Patent
# Everything the BL2/BL31 emulator checks, in one go:
#   emu/test-a64.sh [--cov] [--o6]
# - the interpreter against the host's CPU (a64-selfcheck; AArch64 hosts only),
# - the open BL2 and BL31 against CIX's (a64-test, with the interpreter's audit),
# - with --o6 the same against Radxa's bootloaders for the Orion O6 (a month older),
# - with --cov what the runs executed of the open images (out/emu/test/a64-coverage.txt).
# Needs: emu/build-open.sh bl2 bl31 run (the open stages), dist/orion-o6_2026Q3_open.bin (./build.sh orion-o6 --open),
# and the binary built with: cargo build --release --manifest-path emu/Cargo.toml
cd "$(dirname "$0")/.." || exit 1
E=emu/target/release/sky1emu
O=out/emu/a64
L=out/emu/test
mkdir -p $L
BL2=$O/bl2/sky1/release/bl2/bl2.elf
BL31=$O/bl31/sky1/release/bl31/bl31.elf
CIX=edk2-non-osi/Platform/CIX/Sky1/PackageTool/Firmwares
O6=edk2-project/Platform/Radxa/Orion/O6/Firmwares
cov=; o6=
for a in "$@"; do
  case $a in
    --cov) cov="--cov $L/a64-coverage.txt"; rm -f $L/a64-coverage.txt ;;
    --o6) o6=1 ;;
    *) echo "usage: emu/test-a64.sh [--cov] [--o6]"; exit 2 ;;
  esac
done
status=0
if [ "$(uname -m)" = arm64 ] || [ "$(uname -m)" = aarch64 ]; then
  $E a64-selfcheck $BL2 $BL31 > $L/a64-selfcheck.log 2>&1 || status=1
  tail -1 $L/a64-selfcheck.log
else
  echo "a64-selfcheck: skipped (not an AArch64 host)"
fi
$E a64-test --audit $cov > $L/a64-test.log 2>&1 || status=1
grep -E '^(audit|a64-test):' $L/a64-test.log
if [ -n "$o6" ]; then
  $E a64-test --cix-bl1 $O6/bootloader1.img --cix-fip $O6/bootloader2.img > $L/a64-test-o6.log 2>&1 || status=1
  grep -E '^(reference|a64-test):' $L/a64-test-o6.log
fi
if [ -n "$cov" ]; then
  $E a64-cov $L/a64-coverage.txt $BL2 $BL31 --summary
fi
[ $status = 0 ] || echo "FAILED: see $L/a64-*.log"
exit $status
