#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Check an --open flash image the way the boot chain will see it.

  check_open.py IMAGE REF_BOOTLOADER1 BL2.bin [--root ROOT.pem]

From the flash image (dist/<board>_<version>_open.bin or cix_flash_all.bin):

* bootloader1: signed with the chain-A keys, all signatures and digests
  valid; SE and CSU_PM byte-identical to REF_BOOTLOADER1; the AP image is
  BL2.bin (zero-padded) and the container is otherwise unchanged.
* the root of trust built into BL2 is the key that signs both FIPs.
* bootloader2 and bootloader3: their certificate chains verify against
  that root (bootloader3 through its own trusted key certificate, as
  NON_TRUSTED_KEY_CERT_ID in the BL2 chain of trust).
* the TrustZone configuration (image type 9, if the layout has one) passes
  the checks BL31 makes, and its secure storage region, if enabled, stays
  clear of the boot firmware.

Needs openssl and python3-cryptography (the build container has both).
"""
import argparse
import hashlib
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import cixfw  # noqa: E402

DIGESTINFO_SHA256 = bytes.fromhex("3031300d060960864801650304020105000420")


def tz_config(blob):
    """Parse trustzone_config.bin as BL31 does (sky1_tz_config.c): returns
    (problem or None, data fields)."""
    vmaj, vmin, _, length, sig, crc1, crc2 = struct.unpack_from("<HHIIIII", blob)
    if sig != 0x46435A54:
        return f"signature {sig:#x}", None
    if (vmaj, vmin, blob[24]) != (1, 0, 1):
        return f"version {vmaj}.{vmin}/{blob[24]}", None
    if not (36 <= length <= 64 and length % 4 == 0):
        return f"length {length}", None
    words = list(struct.unpack_from(f"<{length // 4}I", blob))
    words[4] = words[5] = 0
    a = b = 0
    for w in words:
        a = (a + w) & 0xFFFFFFFF
        b = (b + a) & 0xFFFFFFFF
    if (a, b) != (crc1, crc2):
        return "checksum", None
    data = blob[26:26 + min(blob[25] - 2, 11)].ljust(11, b"\0")
    spe, stype, saddr, ssize, recovery = struct.unpack("<BBIIB", data)
    return None, {"spe": spe, "storage": stype, "addr": saddr, "size": ssize, "recovery": recovery}


def header_entries(img):
    magic, _, count, _ = struct.unpack_from("<IIII", img, 0x100000)
    if magic != 0x55AA55AA:
        raise SystemExit("no firmware header at 0x100000")
    return {t: (a, n) for t, a, n, _ in (struct.unpack_from("<IIII", img, 0x100010 + 16 * i) for i in range(count))}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image")
    ap.add_argument("ref_bootloader1")
    ap.add_argument("bl2")
    ap.add_argument("--root", help="expected root of trust key (PEM)")
    a = ap.parse_args()
    img = Path(a.image).read_bytes()
    ref = cixfw.Bl1(Path(a.ref_bootloader1).read_bytes())
    bl2 = Path(a.bl2).read_bytes()
    hdr = header_entries(img)
    bad = []

    def check(ok, what):
        print(f"{'ok  ' if ok else 'FAIL'} {what}")
        if not ok:
            bad.append(what)

    # bootloader1
    a1, n1 = hdr[1]
    b1 = cixfw.Bl1(img[a1:a1 + n1])
    ids = tuple(cixfw.key_id(n) for n, _ in b1.keys())
    check(ids == cixfw.CHAINS["A"]["bl1"], f"bootloader1 keys {ids} are chain A")
    check(all(ok for _, ok in b1.verify()), "bootloader1 signatures and digests")
    imgs = {name: (off, size) for _, name, off, size, _, _ in b1.images()}
    rimgs = {name: (off, size) for _, name, off, size, _, _ in ref.images()}
    for name in ("se", "pm"):
        (o, s), (ro, rs) = imgs[name], rimgs[name]
        check(b1.data[o:o + s] == ref.data[ro:ro + rs], f"{name} firmware identical to the reference")
    o, s = imgs["ap"]
    ap_img = bytes(b1.data[o:o + s])
    check(ap_img[:len(bl2)] == bl2 and not ap_img[len(bl2):].strip(b"\0"), "AP image is the built BL2 (zero padded)")
    check(imgs == rimgs, "image offsets and sizes as in the reference")
    # header outside digests/signatures unchanged
    skip = [(0xE00, 0xE00 + 388)] + [(cixfw.BL1_DIGEST + 0x48 * i, cixfw.BL1_DIGEST + 0x48 * i + 32) for i in range(3)]
    hd = [i for i in range(0x1000) if b1.data[i] != ref.data[i] and not any(x <= i < y for x, y in skip)]
    check(not hd, "bootloader1 header unchanged apart from the AP digest and the signature")

    # root of trust in BL2
    i = bl2.find(DIGESTINFO_SHA256)
    rotpk = bl2[i + len(DIGESTINFO_SHA256):i + len(DIGESTINFO_SHA256) + 32] if i >= 0 else b""
    check(len(rotpk) == 32, f"BL2 root of trust hash {rotpk.hex()[:16]}")
    root_spki = cixfw.spki_of(a.root) if a.root else None
    if root_spki:
        check(hashlib.sha256(root_spki).digest() == rotpk, "BL2 root of trust is ROOT")

    # FIPs
    for t, name in ((2, "bootloader2"), (7, "bootloader3")):
        addr, n = hdr[t]
        fip = img[addr:addr + n]
        problems, info = cixfw.verify_fip(fip)
        tk = [fip[o:o + m] for u, o, m, _ in cixfw.fip_entries(fip) if u.startswith("827ee890")]
        signer = hashlib.sha256(cixfw.TbbrCert(tk[0]).spki()).digest() if tk else b""
        check(not problems, f"{name} chain {problems or 'ok'}")
        check(signer == rotpk, f"{name} trusted key certificate signed by the BL2 root of trust")

    # TrustZone configuration (only the Orion O6/O6N layouts have one)
    if 9 in hdr:
        a9, n9 = hdr[9]
        problem, tz = tz_config(img[a9:a9 + n9])
        check(problem is None, f"TrustZone configuration {problem or tz}")
        if tz and tz["storage"] == 1:
            clash = [t for t, (a, n) in hdr.items() if a < tz["addr"] + tz["size"] and tz["addr"] < a + n]
            check(not clash and tz["addr"] >= 0x101010,
                  f"secure storage {tz['addr']:#x}+{tz['size']:#x} clear of the boot firmware {clash or ''}")

    print("image ok" if not bad else f"{len(bad)} check(s) failed")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
