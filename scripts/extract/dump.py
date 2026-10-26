#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Inspect a CIX SPI image or UEFI FD.

  dump.py layout  IMAGE           firmware header regions
  dump.py modules IMAGE           FFS files of the BL33 FD (GUID, type, name)
  dump.py acpi    IMAGE OUTDIR    write the DSDT/SSDT/static tables as .aml
  dump.py setup   IMAGE [OTHER]   setup form defaults (or the differing ones)

Decompiled vendor tables are vendor material: keep them under out/ (ignored
by git).
"""

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ifr  # noqa: E402
import uefi  # noqa: E402

PLATFORM_ACPI = "C1BB2EAD-C76A-45DD-90B5-D40255170A9C"
SOC_ACPI = "7E374E25-8E01-4FEE-87F2-390C23C606CD"


def fd_offset(img):
    fip = uefi.find_fip(img, 0x100000) if len(img) > 0x100000 else -1
    return uefi.find_fd(img, fip if fip >= 0 else 0)


def cmd_layout(img):
    magic, ver, n = struct.unpack_from("<III", img, 0x100000)
    if magic != 0x55AA55AA:
        sys.exit("no CIX firmware header at 0x100000")
    print(f"firmware header v{ver}, {n} regions")
    for i in range(n):
        t, a, s, _ = struct.unpack_from("<IIII", img, 0x100010 + 16 * i)
        print(f"  type {t:3}  {a:#08x}  {s:#08x}")


def cmd_modules(img):
    for f in uefi.parse_fv(img[fd_offset(img):]):
        print(f"{'  ' * f.fv_depth}{f.guid} {f.type_name:12} {f.ui_name}")


def cmd_acpi(img, out):
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    for f in uefi.parse_fv(img[fd_offset(img):]):
        if f.guid not in (PLATFORM_ACPI, SOC_ACPI):
            continue
        for s in f.walk_sections({uefi.SEC_RAW}):
            if len(s.data) >= 36:
                sig = s.data[:4].decode("latin1")
                length = struct.unpack_from("<I", s.data, 4)[0]
                (out / f"{sig}.aml").write_bytes(s.data[:length])
                print(f"{sig}.aml  {length} bytes")


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    cmd, path = sys.argv[1], sys.argv[2]
    img = Path(path).read_bytes()
    if cmd == "layout":
        cmd_layout(img)
    elif cmd == "modules":
        cmd_modules(img)
    elif cmd == "acpi":
        cmd_acpi(img, sys.argv[3])
    elif cmd == "setup":
        start = hex(fd_offset(img))
        argv = [path, "--start", start]
        if len(sys.argv) > 3:
            other = Path(sys.argv[3]).read_bytes()
            argv += ["--against", sys.argv[3], "--against-start", hex(fd_offset(other))]
        sys.argv = [sys.argv[0]] + argv
        ifr.main()
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
