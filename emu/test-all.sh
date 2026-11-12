#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause-Patent
#
# Every comparison of every open binary with its vendor's, one after the
# other (each suite runs its own jobs side by side):
#
#   emu/test-all.sh [--build] [--cov] [--audit]
#
# --build rebuilds the open firmware first (emu/build-open.sh: the
# container); without it the images already in out/emu are used.  --cov and
# --audit go to the suites that have them.  Needs CIX's and Radxa's images
# (blobs-cache: scripts/fetch-blobs.py; the boards' builds in output/ and
# dist/ for their PM configurations and flash images).  Logs: out/emu/test/.
# The exit status is 0 when no suite found a difference.
set -u
cd "$(dirname "$0")/.." || exit 2
BUILD=; COV=; AUDIT=
for a in "$@"; do
  case $a in
    --build) BUILD=1 ;;
    --cov) COV=--cov ;;
    --audit) AUDIT=--audit ;;
    *) sed -n '4,15p' "$0" | cut -c3-; exit 2 ;;
  esac
done
if [ -n "$BUILD" ]; then
  emu/build-open.sh || exit 2
fi
cargo build --release --quiet --manifest-path emu/Cargo.toml || exit 2
mkdir -p out/emu/test
bad=0
step() {
  name=$1; shift
  echo "=== $name"
  "$@" || { bad=1; echo "=== $name: FAILED"; }
}
# shellcheck disable=SC2086
step "CSU_PM" emu/test-pm.sh out/emu/csupm/csupm.elf $COV $AUDIT
ec() {
  emu/target/release/sky1emu ec-test --open out/emu/ec/ec_fw.bin > out/emu/test/ec-test.log 2>&1
  st=$?
  tail -2 out/emu/test/ec-test.log
  return $st
}
step "EC (Orion O6)" ec
# shellcheck disable=SC2086
step "SE and SFH" emu/test-se.sh out/emu/se/se_fw.elf out/emu/sfh/sfh_fw.bin $COV $AUDIT
# shellcheck disable=SC2086
step "BL2 and BL31" emu/test-a64.sh $COV --o6
[ $bad = 0 ] && echo "=== all suites: no difference" || echo "=== differences found: see out/emu/test/"
exit $bad
