#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause-Patent
#
# Every comparison of the open SE and SFH firmware with CIX's images:
#
#   emu/test-se.sh OPEN_se_fw.elf OPEN_sfh_fw.bin [--cov] [--audit]
#
# se-test with every suite on the O6's flash image (boot, services, suspend,
# crash records and watchdogs, secure boot, other inputs, failures, the paths
# no board's image takes, the DDR driver's suite with the eye scan), the boot
# with the DDR driver live on every board's image, life cycles 5 and 7 with
# keys fused, and sfh-test, side by side.  Logs go to out/emu/test/; the exit
# status is 0 when nothing differs.  --cov also writes
# out/emu/test/se-coverage.txt (what the runs executed of each image, cov.rs;
# CIX's SE image last, as se_fw.elf.cix); --audit runs with the
# interpreter's self-check on.
#
# The flash images are the builds in dist/ (./build.sh BOARD [--open]);
# CIX's SE image is its debug build, from blobs-cache (scripts/open/cix_debug.py).
set -u
cd "$(dirname "$0")/.." || exit 2
[ $# -ge 2 ] || { sed -n '4,19p' "$0" | cut -c3-; exit 2; }
ELF=$1; SFH=$2; shift 2
COV=; AUDIT=
for a in "$@"; do
  case $a in
    --cov) COV=1 ;;
    --audit) AUDIT=--audit ;;
    *) echo "test-se.sh: unknown option $a"; exit 2 ;;
  esac
done
[ -f "$ELF" ] || { echo "test-se.sh: no $ELF"; exit 2; }
[ -f "$SFH" ] || { echo "test-se.sh: no $SFH"; exit 2; }
# (SKY1EMU_TARGET: another cargo target directory, for a checkout several builds share)
TARGET=${SKY1EMU_TARGET:-emu/target}
cargo build --release --quiet --manifest-path emu/Cargo.toml --target-dir "$TARGET" || exit 2
EMU=$TARGET/release/sky1emu
OUT=out/emu/test
mkdir -p "$OUT"
rm -f "$OUT"/se-*.status "$OUT"/se-*.cov "$OUT"/sfh*.status "$OUT"/sfh*.cov

# name, then the command's arguments
run() {
  name=$1; shift
  cov=
  [ -n "$COV" ] && cov="--cov $OUT/$name.cov"
  # shellcheck disable=SC2086
  "$EMU" "$@" $cov > "$OUT/$name.log" 2>&1
  echo $? > "$OUT/$name.status"
}

O6=dist/orion-o6_2026Q3_open.bin
[ -f "$O6" ] || { echo "test-se.sh: no $O6 (./build.sh orion-o6 --open)"; exit 2; }
# shellcheck disable=SC2086
{
"$EMU" arm-check "$ELF" > "$OUT/se-decode.log" 2>&1; echo $? > "$OUT/se-decode.status"

run se-services se-test "$ELF" $O6 --services $AUDIT &
run se-suspend se-test "$ELF" $O6 --suspend $AUDIT &
run se-crash se-test "$ELF" $O6 --crash $AUDIT &
run se-secure se-test "$ELF" $O6 --secure $AUDIT &
run se-inputs se-test "$ELF" $O6 --inputs $AUDIT &
run se-failures se-test "$ELF" $O6 --failures $AUDIT &
run se-paths se-test "$ELF" $O6 --paths $AUDIT &
run se-ddr-suite se-test "$ELF" $O6 --ddr-suite --eye $AUDIT &
run se-lc5 se-test "$ELF" $O6 --life-cycle 5 --keys --services --suspend $AUDIT &
run se-lc7 se-test "$ELF" $O6 --life-cycle 7 --keys --services --suspend $AUDIT &
for f in dist/*_2026Q3.bin dist/*_2026Q3_open.bin; do
  [ -f "$f" ] || continue
  b=$(basename "$f" .bin)
  run "se-ddr-$b" se-test "$ELF" "$f" --ddr --suspend $AUDIT &
done
run sfh sfh-test "$SFH" &
wait
}

bad=0
for s in "$OUT"/se-*.status "$OUT"/sfh*.status; do
  name=$(basename "$s" .status)
  st=$(cat "$s")
  printf '%-34s %s\n' "$name" "$(tail -n 40 "$OUT/$name.log" | grep -E 'differences|not decoded' | tail -n 1)"
  [ "$st" = 0 ] || { bad=1; echo "  FAILED (status $st): $OUT/$name.log"; }
done
if [ -n "$COV" ]; then
  cat "$OUT"/se-*.cov "$OUT"/sfh*.cov > "$OUT/se-coverage.cov" 2>/dev/null
  SFHELF=${SFH%.bin}.elf
  # CIX's SE image goes by the open one's file name: its coverage is kept as NAME.cix
  CIXELF=$(find blobs-cache -path '*/debug/se_fw/se_fw.elf' 2>/dev/null | head -n 1)
  CIXCOV=
  if [ -n "$CIXELF" ]; then
    CIXCOV="$OUT/$(basename "$ELF").cix"
    ln -sf "$PWD/$CIXELF" "$CIXCOV"
  fi
  # shellcheck disable=SC2046,SC2086
  "$EMU" cov "$OUT/se-coverage.cov" "$ELF" $([ -f "$SFHELF" ] && echo "$SFHELF") $CIXCOV > "$OUT/se-coverage.txt"
  grep -E 'functions,' "$OUT/se-coverage.txt"
  echo "coverage: $OUT/se-coverage.txt"
fi
exit $bad
