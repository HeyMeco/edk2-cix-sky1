#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause-Patent
#
# Build the open firmware at the working tree for the emulators' tests, in
# the build container, into out/emu/<name> (out/ is not tracked):
#
#   emu/build-open.sh [se|csupm|sfh|ec|bl2|bl31]...     (none: all six)
#
#   se     out/emu/se/se_fw.elf        csupm  out/emu/csupm/csupm.elf
#   sfh    out/emu/sfh/sfh_fw.elf      ec     out/emu/ec/ec_fw.bin
#   bl2    out/emu/a64/bl2/sky1/release/bl2/bl2.elf      (arm-trusted-firmware
#   bl31   out/emu/a64/bl31/sky1/release/bl31/bl31.elf    with the patch queue)
#
# The container is the one ./build.sh uses (docker/Dockerfile; its tag is
# the file's hash).  BL2 and BL31 are built as scripts/open/trusted.py
# builds them (release), BL2 against CIX's public root key from blobs-cache.
cd "$(dirname "$0")/.." || exit 1
TAG=$(shasum -a 256 < docker/Dockerfile | cut -c1-12)
EPOCH=$(git log -1 --format=%ct)
STAMP=$(TZ=UTC date -r "$EPOCH" '+%H:%M:%S, %b %d %Y' 2>/dev/null || TZ=UTC date -d "@$EPOCH" '+%H:%M:%S, %b %d %Y')
TFA="make -s -C arm-trusted-firmware -j8 PLAT=sky1 SPD=opteed SMP=1 ENABLE_FEAT_HCX=1 DEBUG=0 CROSS_COMPILE=aarch64-none-elf- BUILD_MESSAGE_TIMESTAMP='\"$STAMP\"'"
[ $# -eq 0 ] && set -- se csupm sfh ec bl2 bl31
for t in "$@"; do
  case $t in
    se) cmd='make -s -C firmware/se O=/work/out/emu/se MBEDTLS_DIR=/work/mbedtls SE_FW_VERSION=beta2.0-emu -j8' ;;
    csupm) cmd='make -s -C firmware/csupm O=/work/out/emu/csupm CSUPM_VERSION=emu all check -j8' ;;
    sfh) cmd='make -s -C firmware/sfh O=/work/out/emu/sfh' ;;
    ec) cmd='make -s -C firmware/ec O=/work/out/emu/ec' ;;
    bl31) cmd="$TFA BUILD_BASE=/work/out/emu/a64/bl31 bl31" ;;
    bl2)
      KEY=$(ls blobs-cache/git/cixtech/edk2-non-osi/*/Platform/CIX/Sky1/PackageTool/Keys/cix_privatekey.pem 2>/dev/null | tail -1)
      [ -n "$KEY" ] || { echo "bl2: no CIX root key in blobs-cache (scripts/fetch-blobs.py)"; exit 1; }
      cmd="$TFA TRUSTED_BOARD_BOOT=1 MBEDTLS_DIR=/work/mbedtls ROT_KEY=/work/$KEY BUILD_BASE=/work/out/emu/a64/bl2 bl2" ;;
    *) echo "build-open.sh: unknown target $t"; exit 2 ;;
  esac
  docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp -e TZ=UTC -e SOURCE_DATE_EPOCH="$EPOCH" \
    -v "$PWD:/work" -w /work "edk2-cix-build:$TAG" \
    bash -c "set -o pipefail; $cmd 2>&1 | { grep -v '^  CC' || true; } | tail -15" || { echo "$t: build failed"; exit 1; }
  echo "$t: built"
done
