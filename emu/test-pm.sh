#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause-Patent
#
# Every comparison of the open CSU_PM firmware with CIX's image, on every
# board configuration, and the tests of the open image alone:
#
#   emu/test-pm.sh OPEN.elf [--cov] [--audit]
#
# pm-test (boot, SCMI, power, services, runtime, --full) for the default
# configuration and the five boards, pm-faults and pm-vf --matrix, side by
# side.  Logs go to out/emu/test/; the exit status is 0 when nothing
# differs.  --cov also writes out/emu/test/coverage.txt (how much of each
# image's code the runs executed, cov.rs); --audit runs pm-test with the
# interpreter's window and interrupt audit on.
#
# CIX's image comes from blobs-cache (scripts/fetch-blobs.py).
set -u
cd "$(dirname "$0")/.." || exit 2
[ $# -ge 1 ] || { sed -n '4,16p' "$0" | cut -c3-; exit 2; }
ELF=$1; shift
COV=; AUDIT=
for a in "$@"; do
  case $a in
    --cov) COV=1 ;;
    --audit) AUDIT=--audit ;;
    *) echo "test-pm.sh: unknown option $a"; exit 2 ;;
  esac
done
[ -f "$ELF" ] || { echo "test-pm.sh: no $ELF"; exit 2; }
cargo build --release --quiet --manifest-path emu/Cargo.toml || exit 2
EMU=emu/target/release/sky1emu
OUT=out/emu/test
mkdir -p "$OUT"
rm -f "$OUT"/*.status "$OUT"/*.cov

# name, then the command's arguments
run() {
  name=$1; shift
  cov=
  [ -n "$COV" ] && cov="--cov $OUT/$name.cov"
  # shellcheck disable=SC2086
  "$EMU" "$@" $cov > "$OUT/$name.log" 2>&1
  echo $? > "$OUT/$name.status"
}

BOARDS="o6 o6n opi6 opi6plus ms-r1"
SUITES="--scmi --power --services --runtime 100 --full"
# shellcheck disable=SC2086
run pm-default pm-test "$ELF" $SUITES $AUDIT &
for b in $BOARDS; do
  # shellcheck disable=SC2086
  run "pm-$b" pm-test "$ELF" $SUITES $AUDIT --board "$b" &
done
run pm-faults pm-faults "$ELF" &
run pm-vf pm-vf "$ELF" --matrix &
wait

bad=0
for name in pm-default $(for b in $BOARDS; do echo "pm-$b"; done) pm-faults pm-vf; do
  st=$(cat "$OUT/$name.status" 2>/dev/null || echo missing)
  [ "$st" = 0 ] || bad=1
  printf '%-12s %-4s %s\n' "$name" "$([ "$st" = 0 ] && echo ok || echo FAIL)" \
    "$(tail -2 "$OUT/$name.log" | tr '\n' ' ')"
done
if [ -n "$COV" ]; then
  cat "$OUT"/*.cov > "$OUT/all.cov"
  # (per function: sky1emu cov out/emu/test/all.cov ELF --partial)
  "$EMU" cov "$OUT/all.cov" "$ELF" > "$OUT/coverage.txt" 2>&1
  cat "$OUT/coverage.txt"
fi
exit $bad
