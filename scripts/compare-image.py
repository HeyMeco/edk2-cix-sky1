#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Compare two CIX SPI images region by region (firmware header at 0x100000).

Prints each region with its size and whether the contents match; the EC
area at 0x10000 is compared as well.  Use it to check a build against the
vendor image it replaces: everything except bootloader3 (BL33/UEFI) and the
memory/PM configuration built from source is expected to match.

Usage: compare-image.py REFERENCE.bin BUILT.bin
"""

import hashlib
import struct
import sys

NAMES = {1: "bootloader1", 2: "bootloader2", 3: "memory_config", 4: "pm_config", 6: "se_config",
         7: "bootloader3", 8: "sfh_fw", 9: "trustzone_config", 100: "reserved"}


def regions(img):
    magic, _v, n = struct.unpack_from("<III", img, 0x100000)
    assert magic == 0x55AA55AA, "no CIX firmware header"
    out = {}
    for i in range(n):
        t, a, s, _ = struct.unpack_from("<IIII", img, 0x100010 + 16 * i)
        out[t] = (a, s, hashlib.sha256(img[a + 16:a + s]).hexdigest())
    return out


def main():
    ref, new = (open(p, "rb").read() for p in sys.argv[1:3])
    r, b = regions(ref), regions(new)
    for t in sorted(set(r) | set(b)):
        ra, rb = r.get(t), b.get(t)
        if not ra or not rb:
            print(f"{NAMES.get(t, t):17} only in {'reference' if ra else 'build'}")
            continue
        state = "same" if ra[2] == rb[2] and ra[1] == rb[1] else "DIFFERENT"
        print(f"{NAMES.get(t, t):17} ref {ra[0]:#08x}+{ra[1]:#08x}  build {rb[0]:#08x}+{rb[1]:#08x}  {state}")
    ec = "same" if ref[0x10000:0x30000] == new[0x10000:0x30000] else "DIFFERENT"
    print(f"{'ec (0x10000)':17} {ec}")


if __name__ == "__main__":
    main()
