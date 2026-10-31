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


def run(elf, flash, life_cycle):
    soc = SkySoc(flash, life_cycle=life_cycle)
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("open_elf")
    ap.add_argument("flash")
    ap.add_argument("--cix")
    ap.add_argument("--life-cycle", type=int, default=4)
    ap.add_argument("-v", action="store_true")
    a = ap.parse_args()
    cix_elf = a.cix
    if cix_elf is None:
        from cix_debug import fetch
        cix_elf = fetch()
    flash = open(a.flash, "rb").read()
    results = []
    for elf in (cix_elf, a.open_elf):
        se, stopped, regs, mem = run(elf, flash, a.life_cycle)
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
    print(f"{len(set(ra) | set(rb))} registers compared, {bad} differences")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
