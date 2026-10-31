#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""CSU_PM's image format in bootloader1 ("XBIN"), from an ELF.

  xbin.py ELF OUT.bin          write the XBIN of ELF
  xbin.py --check XBIN...      parse and rebuild each image; fail unless identical

Layout, as the SE's loader reads it (firmware/se/framework/xloader.c) and as
CIX's images are built:

  0x00  "XBIN", 0, total size, entry point
  0x10  one 20-byte entry per ELF program header with a memory size, in
        program-header order: file offset, address, file size (rounded up
        to 4), memory size, flags (p_flags; bit 31 marks the last entry)
        then the data of each entry, back to back.

The SE copies each entry's data to its address (PM SRAM); entries without
data (.bss) are not cleared, the PM's start-up does that.  At most 30
entries.
"""

import struct
import sys

MAGIC = b"XBIN"
LAST = 1 << 31
MAX_ENTRIES = 30


def pack(segments, entry):
    """segments: [(address, data, memory size, flags)]."""
    if len(segments) > MAX_ENTRIES:
        raise ValueError(f"{len(segments)} segments, the SE loads at most {MAX_ENTRIES}")
    off = 0x10 + 0x14 * len(segments)
    table, blob = b"", b""
    for i, (addr, data, memsz, flags) in enumerate(segments):
        data = data + bytes(-len(data) % 4)
        if i == len(segments) - 1:
            flags |= LAST
        table += struct.pack("<5I", off + len(blob), addr, len(data), memsz, flags)
        blob += data
    total = 0x10 + len(table) + len(blob)
    return MAGIC + struct.pack("<3I", 0, total, entry) + table + blob


def parse(img):
    if img[:4] != MAGIC:
        raise ValueError("not an XBIN image")
    _, total, entry = struct.unpack_from("<3I", img, 4)
    segs = []
    for i in range(MAX_ENTRIES):
        off, addr, size, memsz, flags = struct.unpack_from("<5I", img, 0x10 + 0x14 * i)
        segs.append((addr, img[off:off + size], memsz, flags & ~LAST))
        if flags & LAST:
            break
    return segs, entry, total


def from_elf(path):
    from elftools.elf.elffile import ELFFile
    with open(path, "rb") as fh:
        elf = ELFFile(fh)
        segs = [(s["p_paddr"], s.data(), s["p_memsz"], s["p_flags"])
                for s in elf.iter_segments() if s["p_type"] == "PT_LOAD" and s["p_memsz"]]
        return pack(segs, elf.header["e_entry"])


def check(paths):
    bad = 0
    for p in paths:
        img = open(p, "rb").read()
        if img[:8] == b"CIXBTFF!":                     # a whole bootloader1: take its PM image
            n = struct.unpack_from("<I", img, 0x9e8)[0]
            for i in range(n):
                t, _, _, off, size, _ = struct.unpack_from("<6I", img, 0x9ec + 24 * i)
                if t == 2:
                    img = img[off:off + size]
                    break
        segs, entry, total = parse(img)
        again = pack(segs, entry)
        ok = again == img and total == len(img)
        bad += not ok
        print(f"{'ok  ' if ok else 'FAIL'} {p}: {len(segs)} entries, {total:#x} bytes")
    return 1 if bad else 0


def main():
    if len(sys.argv) >= 3 and sys.argv[1] == "--check":
        return check(sys.argv[2:])
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    open(sys.argv[2], "wb").write(from_elf(sys.argv[1]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
