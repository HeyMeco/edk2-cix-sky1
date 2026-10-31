#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Self-tests for scripts/open/cixfw.py (run in CI; needs openssl).

* every bootloader1 in vendor/ parses and its signatures verify
* re-signing with throwaway keys verifies, keeps the images byte for byte,
  and replacing an image updates the header digest
* corrupting a signed byte is detected
* the flash packer: header layout, address order, bootloader2 version
  stamp, OTA concatenation (the byte-for-byte comparison with
  cix_package_tool is in docs/open-firmware.md; it needs the CIX tool)
"""

import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE))
import cixfw  # noqa: E402

failures = []


def check(cond, what):
    print(f"{'ok  ' if cond else 'FAIL'} {what}")
    if not cond:
        failures.append(what)


def test_vendor_bl1():
    for p in sorted(ROOT.glob("vendor/*/*/bootloader1.img")):
        b = cixfw.Bl1(p.read_bytes())
        res = b.verify()
        check(len(b.images()) == 3 and all(ok for _, ok in res), f"{p.relative_to(ROOT)} verifies")


def test_resign(tmp):
    src = sorted(ROOT.glob("vendor/*/*/bootloader1.img"))[0]
    keys = []
    for i in (1, 2):
        k = tmp / f"k{i}.pem"
        subprocess.run(["openssl", "genrsa", "-out", str(k), "3072"], check=True, capture_output=True)
        keys.append(k)
    orig = cixfw.Bl1(src.read_bytes())
    b = cixfw.Bl1(src.read_bytes())
    b.resign(*keys)
    check(all(ok for _, ok in b.verify()), "re-signed container verifies with the new keys")
    same = all(b.data[o:o + n] == orig.data[o:o + n] for _, _, o, n, _, _ in orig.images())
    check(same, "re-signing leaves the images unchanged")

    _, name, off, size, _, sig = b.images()[2]
    new = bytes(b.data[off:off + size - 0x40])           # a smaller AP image
    b.replace(name, new)
    b.resign(*keys)
    check(all(ok for _, ok in b.verify()) and b.images()[2][3] == len(new), "replaced image verifies")

    bad = cixfw.Bl1(bytes(b.data))
    bad.data[off + 0x100] ^= 1
    check(not dict(bad.verify())[name], "a flipped image byte is detected")
    bad = cixfw.Bl1(bytes(b.data))
    bad.data[0x810] ^= 1
    check(not dict(bad.verify())["header"], "a flipped header byte is detected")


def test_pack(tmp):
    f = tmp / "F"
    f.mkdir()
    (f / "a.bin").write_bytes(b"A" * 100)
    (f / "fip.bin").write_bytes(struct.pack("<II", cixfw.FIP_TOC_MAGIC, 0x12345678) + b"F" * 40)
    (f / "ec.bin").write_bytes(b"E" * 16)
    cfg = {"cfs_version": 1, "fip_version": 1234, "align_size": 4096, "flash_size": "0x100000",
           "ec_addr": "0x1000", "firmware_header_addr": "0x8000", "sec_debug_addr": "0x9000",
           "xip_addr": "0xA000", "ec_file": "F/ec.bin", "sec_debug_file": "", "xip_file": "",
           "image_count": 2, "image_header_groups": [
               {"image_type": 2, "address": "0x20000", "size": "0x1000", "file": "F/fip.bin"},
               {"image_type": 3, "address": "0x10000", "size": "0x1000", "file": "F/a.bin"}]}
    img = cixfw.pack_all(cfg, tmp)
    check(len(img) == 0x20000 + 48, "full image ends after the last region")
    magic, ver, count, _ = struct.unpack_from("<IIII", img, 0x8000)
    check((magic, ver, count) == (0x55AA55AA, 1, 2), "firmware header")
    e = [struct.unpack_from("<IIII", img, 0x8010 + 16 * i) for i in range(2)]
    check(e == [(3, 0x10000, 100, 0), (2, 0x20000, 48, 0)], "header entries sorted by address")
    check(struct.unpack_from("<I", img, 0x20004)[0] == 1234, "bootloader2 carries fip_version")
    check(img[0x1000:0x1010] == b"E" * 16 and img[0x2000:0x8000] == bytes(0x6000), "EC image, zero fill")
    ota = dict(cfg, ota_flags="0x80000000")
    o = cixfw.pack_ota(ota, tmp)
    check(struct.unpack_from("<IIII", o, 0) == (0x55AA55AA, 1, 2, 0x80000000), "OTA header")
    check(o[0x30:0x30 + 48][:4] == struct.pack("<I", cixfw.FIP_TOC_MAGIC) and o[0x30 + 48:] == b"A" * 100,
          "OTA images back to back in layout order")
    cfg["image_header_groups"][1]["size"] = "0x10"
    try:
        cixfw.pack_all(cfg, tmp)
        check(False, "oversized file rejected")
    except cixfw.PackError:
        check(True, "oversized file rejected")


def main():
    test_vendor_bl1()
    with tempfile.TemporaryDirectory() as td:
        test_resign(Path(td))
        test_pack(Path(td))
    if failures:
        sys.exit(f"{len(failures)} failure(s)")


if __name__ == "__main__":
    main()
