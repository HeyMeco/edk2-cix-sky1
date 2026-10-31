#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Differential test of the open SE firmware against CIX's (debug build).

  test_se.py OPEN_se_fw.elf FLASH.bin [--cix CIX_se_fw.elf] [--life-cycle N] [-v]

Both run in se_emu.py on the same Sky1 model and flash image, from reset
until the debug service (after BL2 is started), with the DDR driver's entry
points stubbed (it is the same CIX code in both).  Compared:

* register writes, per register in order (UART data, postcodes and the
  log-level scratch register excepted);
* the final contents of what the SE writes for later stages: DRAM (the
  SYSHUB window: key blocks, BL2, boot records) and the PM's SRAM.
"""

import argparse
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from se_emu import Se, SkySoc  # noqa: E402

DDR_STUBS = {
    "drv_ddr_init": 0, "drv_ddr_postci": 0, "drv_ddr_get_mem_size": 0x4000,
    "drv_ddr_get_ch_num": 4, "MemGetBootEnv": 0,
}
def hkdf_sha256(ikm, n):
    import hashlib
    import hmac
    prk = hmac.new(b"\0" * 32, ikm, hashlib.sha256).digest()
    out, t, i = b"", b"", 1
    while len(out) < n:
        t = hmac.new(prk, t + bytes([i]), hashlib.sha256).digest()
        out += t
        i += 1
    return out[:n]


def cix_derive_key(se):
    """CIX's fw_km_derive_key without the crypto engine's hash: HKDF-SHA256 of the eFuse key."""
    src, dst, key, plen = (se.arg(i) for i in range(4))
    lc = se.read32(0xb00300e0) & 7
    fuse = {1: 0x64 if lc == 7 else 0xc4, 0: 0x54 if lc == 7 else 0xb4}[src]
    n = {3: 16, 4: 32}[dst]
    se.write(plen, n.to_bytes(4, "little"))
    se.write(key, hkdf_sha256(se.read(0xb0040000 + fuse, 16), n))
    return 0


CIX_STUBS = {"fw_km_derive_key": cix_derive_key}
IGNORE = {0xa0010000, 0xa0020f00, 0xb0030208, 0xb003020c, 0xe000e010, 0xe000e014, 0xe000e018}
IGNORE_RANGES = [(0x63da0000, 0x63da4000),    # RAM log: the messages differ
                 (0x63e01000, 0x63e01040)]    # firmware version string
MEMORY = [(0x60000000, 0x80000000, "DRAM window"), (0x46a10000, 0x46b18000, "PM SRAM")]


def is_memory(addr):
    return any(lo <= addr < hi for lo, hi, _ in MEMORY)


def keys_efuse(flash, secure=False):
    """eFuse with test device and model keys and, for secure boot, the image's
    OEM and CIX key hashes and the secure-boot bit (life cycle 7)."""
    import hashlib
    import struct
    fuses = {0xb4: bytes(range(0x10, 0x20)), 0xc4: bytes(range(0x40, 0x50)),
             0x54: bytes(range(0x60, 0x70)), 0x64: bytes(range(0x80, 0x90))}
    if secure:
        hdr = flash[0x188000:0x189000]
        oem_len, cix_len = struct.unpack_from("<I", hdr, 0x18)[0], struct.unpack_from("<I", hdr, 0x320)[0]
        fuses[0x188] = hashlib.sha256(hdr[0x1c:0x1c + oem_len]).digest()
        fuses[0x74] = hashlib.sha256(hdr[0x324:0x324 + cix_len]).digest()
        fuses[0x154] = bytes([1 << 3 | 1 << 4])        # secure boot; signature algorithm 1 (RSA)
        fuses[0x155] = bytes([0])
    return fuses


def run(elf, flash, life_cycle, efuse=None):
    soc = SkySoc(flash, life_cycle=life_cycle, efuse=efuse)
    stubs = dict(DDR_STUBS)
    if "cix" in str(elf):
        stubs.update(CIX_STUBS)
    se = Se(elf, soc, stubs=stubs, stop=("service_debug_entry", "service_idle_entry"))
    stopped = se.run(count=200_000_000)
    regs = defaultdict(list)
    for fn, addr, size, value in se.writes:
        if addr not in IGNORE and not is_memory(addr) and \
                not any(lo <= addr < hi for lo, hi in IGNORE_RANGES):
            regs[addr].append(value)
    mem = {}
    for lo, hi, name in MEMORY:
        for page in sorted(se.mapped):
            if lo <= page < hi:
                data = bytearray(se.uc.mem_read(page, 0x10000))
                for ilo, ihi in IGNORE_RANGES:
                    for a in range(max(ilo, page), min(ihi, page + 0x10000)):
                        data[a - page] = 0
                mem[page] = bytes(data)
    return se, stopped, regs, mem


# Requests the AP makes at run time: (name, channel, function ID, parameter words)
REQUESTS = [
    ("firmware version", 0, 0x8200000f, [0]),
    ("serial number and chip ID", 0, 0x82000002, [0, 0x240, 0]),
    ("OPN fuses", 0, 0x82000002, [1, 0x4dc, 8]),
    ("fuse field", 0, 0x8200000e, [0x155, 0, 8]),
    ("OEM key hash state", 0, 0x8200001b, [0]),
    ("MTE setting", 0, 0x82000020, [0]),
    ("random bytes", 2, 0x82000010, [16]),
    ("memory repair data", 0, 0x82000006, [3, 0]),
    ("unknown function", 0, 0x82000099, [0]),
]
MBOX = {0: 0xa0050000, 2: 0xa0070000}
MBOX_IRQ = {0: 43, 2: 41}


def request(se, chan, fid, words):
    """Put a request in an AP->SE register channel, take its interrupt, read the answer."""
    base = MBOX[chan]
    msg = [8 + 4 * len(words), fid] + words
    for i, w in enumerate(msg):
        se.write(base + 4 * i, w.to_bytes(4, "little"))
    se.soc.mbox_pending = base
    ok = se.irq(MBOX_IRQ[chan])
    se.soc.mbox_pending = None
    n = ((se.read32(base) & 0x7f) + 3) // 4
    return ok, [se.read32(base + 4 * i) for i in range(min(n, 32))]


def secure_boot(elf, flash):
    """The open firmware with secure boot fused for the image's own keys:
    it must start the AP; and not with a byte of BL2 or of CSU_PM changed."""
    import struct
    hdr = flash[0x188000:0x189000]
    imgs = {struct.unpack_from("<I", hdr, 0x9ec + 24 * i)[0]: struct.unpack_from("<6I", hdr, 0x9ec + 24 * i)
            for i in range(struct.unpack_from("<I", hdr, 0x9e8)[0])}
    cases = [("as signed", None), ("BL2 altered", 0x188000 + imgs[3][3] + 0x100),
             ("CSU_PM altered", 0x188000 + imgs[2][3] + 0x1000)]
    bad = 0
    for name, off in cases:
        f = bytearray(flash)
        if off is not None:
            f[off] ^= 1
        soc = SkySoc(bytes(f), life_cycle=7, efuse=keys_efuse(flash, secure=True))
        se = Se(elf, soc, stubs=DDR_STUBS, stop=("service_debug_entry", "service_idle_entry"))
        se.stop_write = (0x4fa80000, 8)                 # AP boot core powered on
        se.poll_limit = 20_000_000
        se.run(count=400_000_000)
        started = se.stopped == "write 0x8 to 0x4fa80000"
        ok = started == (off is None)
        bad += not ok
        print(f"{'ok  ' if ok else 'FAIL'} {name}: AP {'started' if started else 'not started'}"
              f" ({se.stopped or se.poll})")
    sys.exit(1 if bad else 0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("open_elf")
    ap.add_argument("flash")
    ap.add_argument("--cix")
    ap.add_argument("--life-cycle", type=int, default=4)
    ap.add_argument("-v", action="store_true")
    ap.add_argument("--services", action="store_true", help="then compare answers to run-time requests")
    ap.add_argument("--suspend", action="store_true", help="then compare a suspend and resume")
    ap.add_argument("--keys", action="store_true", help="with device and model keys in the eFuse")
    ap.add_argument("--secure", action="store_true",
                    help="open firmware alone, secure boot fused: boots the image as it is and refuses "
                         "it with BL2 or CSU_PM altered")
    a = ap.parse_args()
    cix_elf = a.cix
    if cix_elf is None:
        from cix_debug import fetch
        cix_elf = fetch()
    flash = open(a.flash, "rb").read()
    if a.secure:
        return secure_boot(a.open_elf, flash)
    efuse = keys_efuse(flash) if a.keys else None
    results, sessions = [], []
    for elf in (cix_elf, a.open_elf):
        se, stopped, regs, mem = run(elf, flash, a.life_cycle, efuse)
        sessions.append(se)
        where = stopped or (f"polling in {se.poll[0]}" if se.poll else se.pc_func())
        print(f"{Path(elf).name}: {len(se.writes)} writes, stopped at {where}")
        results.append((regs, mem))
    (ra, ma), (rb, mb) = results
    bad = 0
    for reg in sorted(set(ra) | set(rb)):
        if ra.get(reg) != rb.get(reg):
            bad += 1
            if a.v or bad <= 40:
                print(f"  {reg:#010x}: CIX {[hex(v) for v in ra.get(reg, [])][:6]}"
                      f" open {[hex(v) for v in rb.get(reg, [])][:6]}")
    for page in sorted(set(ma) | set(mb)):
        pa, pb = ma.get(page, bytes(0x10000)), mb.get(page, bytes(0x10000))
        if pa != pb:
            diff = [i for i in range(0x10000) if pa[i] != pb[i]]
            bad += 1
            print(f"  memory {page:#010x}: {len(diff)} bytes differ, first at {page + diff[0]:#x}")
    print(f"{len(set(ra) | set(rb))} registers compared")
    if a.services:
        for name, chan, fid, words in REQUESTS:
            got = [request(se, chan, fid, list(words)) for se in sessions]
            same = got[0] == got[1]
            bad += not same
            print(f"{'ok  ' if same else 'FAIL'} {name}: {[hex(w) for w in got[1][1]]}"
                  + ("" if same else f" (CIX {[hex(w) for w in got[0][1]]})"))
    if a.suspend:
        regs = []
        for se in sessions:
            request(se, 0, 0x82000007, [0])            # AP: suspend
            se.stops.pop(se.elf.addr("service_debug_entry"), None)
            se.stops.pop(se.elf.addr("service_idle_entry"), None)
            se.stop_write = (0x4661008c, 1)             # SE -> PM: resumed
            mark = len(se.writes)
            se.resume(count=100_000_000)
            print(f"{Path(se.elf_path).name}: suspend and resume, {len(se.writes) - mark} writes, "
                  f"stopped at {se.stopped or se.poll}")
            per = defaultdict(list)
            for fn, addr, size, value in se.writes[mark:]:
                if addr not in IGNORE and not is_memory(addr) and \
                        not any(lo <= addr < hi for lo, hi in IGNORE_RANGES):
                    per[addr].append(value)
            regs.append(per)
        ra, rb = regs
        diffs = [r for r in sorted(set(ra) | set(rb)) if ra.get(r) != rb.get(r)]
        for r in diffs[:40]:
            print(f"  {r:#010x}: CIX {[hex(v) for v in ra.get(r, [])][:6]} open {[hex(v) for v in rb.get(r, [])][:6]}")
        print(f"suspend/resume: {len(set(ra) | set(rb))} registers compared, {len(diffs)} differ")
        bad += len(diffs)
    print(f"{bad} differences")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
