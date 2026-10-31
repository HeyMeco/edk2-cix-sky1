#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Compare the DVFS tables the open CSU_PM firmware builds with CIX's.

  test_vf.py OPEN.elf [--cix CIX.elf] [--strap LO:HI] [--fuses SEED]
             [--phase N] [--opn HI:LO] [--vmin] [--phecda] [--bios ITEMS]
             [--gb1boost] [--no-i2c] [-v]

Each image boots in pm_emu.py until it idles; then the dvfs module's
element table (CIX's config_dvfs.c element_table: per DVFS domain the
sustained OPP, the reference level, the OPPs: level, mV, kHz, mW) and the
selectors CIX's subsys_hsm.c picks (g_mp_sel, g_opp_table_selector,
g_opp_limit_selector) are read from both and compared, with the I2C
transfers (the boot voltages; --no-i2c when the board's regulators are
ones the open firmware does not drive yet, as with --phecda).

What real parts differ in, the emulator does not have by default; these
make both images see it:
  --strap LO:HI   the PM strap the SE leaves (0xa0000800, two words; the
                  checksum byte, bits 0-7, is made right: bytes sum to 0xff)
  --fuses SEED    an eFuse copy (0x200be700) of random V/F curves, patches,
                  DSU point and leakage currents, as a part's (seeded)
  --phase N       with --fuses: fuse values that make the ATE phase N
                  (0 ES .. 5 MP3)
  --opn HI:LO     the part number fuse
  --vmin          the fused voltages enabled (what the BIOS configuration's
                  vmin magic does: vf_curve_loaded 0 at reset)
  --phecda        the board's PMICs are MP2845s at 0x45 on I2C 0 and 1
                  (CIX's Phecda layout) rather than the EVB's MP5475s
  --bios ITEM,... a valid BIOS PM configuration (0x200bf000) with: vmin
                  (the fused voltages enabled), keep100m (the 100 MHz OPPs
                  kept), opmode2 (the GPU's budget tables), oppN (DVFS
                  element N's OPPs given), oppall (bit 7 of opp_valid clear:
                  every domain counts as given)
  --gb1boost      the SoC register asking for GB1 at 2.8 GHz set
"""

import argparse
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pm_emu  # noqa: E402
import test_pm  # noqa: E402

EFUSE = 0x200be700
SELECTORS = ("g_mp_sel", "g_opp_table_selector", "g_opp_limit_selector")


def bits_put(buf, pos, n, v):
    for i in range(n):
        b = (v >> i) & 1
        byte, bit = divmod(pos + i, 8)
        buf[byte] = (buf[byte] & ~(1 << bit)) | (b << bit)


def efuse_image(seed, phase, opn):
    """An eFuse copy with V/F data: curves of GM0, GM1, GB0, GB1 (bit
    offsets 0xacf, 0xade, 0xab1, 0xac0; three points 60 bits apart: 5 bits
    of 100 MHz, 10 bits of mV above 400), patches of GB0 and GM1 (0xbdd,
    0xc0a, 15 bits apart), the DSU point (0x727), leakage currents."""
    r = random.Random(seed)
    buf = bytearray(0x400)
    buf[0:16] = bytes(r.randrange(256) for _ in range(16))      # not the marker

    def point(pos, mhz, mv):
        bits_put(buf, pos, 5, mhz // 100)
        bits_put(buf, pos + 5, 10, mv - 400)

    tops = {0xacf: (2300, 2200, 1800), 0xade: (2200, 2100, 1600), 0xab1: (2500, 2400, 2000),
            0xac0: (2600, 2500, 2200)}
    if phase == 2:                      # PVT: GB1's third point at 2300
        tops[0xac0] = (2600, 2500, 2300)
    elif phase == 3:                    # MP: at 2200
        tops[0xac0] = (2600, 2500, 2200)
    elif phase in (4, 5):               # MP2/MP3: GB1 top at 2200 for MP3
        tops[0xac0] = ((2200 if phase == 5 else 2600), 2500, 2200)
    for off, fr in tops.items():
        v = r.randrange(820, 930)
        for p in range(3):
            point(off + 60 * p, fr[p], v - 25 * p - r.randrange(0, 10))
    if phase >= 4:
        for off, fr in ((0xbdd, (2500, 2400, 2000)), (0xc0a, (2200, 2100, 1600))):
            v = r.randrange(820, 930)
            for p in range(3):
                point(off + 15 * p, fr[p], v - 20 * p)
    if phase >= 2:
        point(0x727, 1300, r.randrange(760, 790))
    for off in (0x847, 0x857, 0x827, 0x837, 0x867, 0x877, 0x887, 0x897, 0x927, 0x937, 0x947):
        bits_put(buf, off, 16, r.randrange(100, 3000))
        bits_put(buf, off + 128 if off < 0x900 else off + 80, 16, r.randrange(100, 3000))
    if opn:
        hi, lo = opn
        bits_put(buf, 173 * 32, 32, lo)
        bits_put(buf, 174 * 32, 32, hi)
    return bytes(buf)


def bios_image(items):
    """CIX's BIOS PM configuration block: version 3, length, two sums, then
    pm_config_t (packed; config_data_t valid when bit 0 is clear), the rest
    erased (0xff)."""
    cfg = bytearray(b"\xff" * 0xdba)

    def word(off, v):
        cfg[off:off + 4] = struct.pack("<I", v)
    opp_valid = 0x80
    for it in items:
        if it == "vmin":
            word(0xceb, 0x67997cda)
        elif it == "keep100m":
            word(0xd33, 0)
        elif it == "opmode2":
            word(0xd43, 2)
        elif it == "oppall":
            opp_valid = 0
        elif it.startswith("opp"):
            e = int(it[3:])
            base = 0x81 + 212 * e
            levels = [(400, 805, 400000, 0), (900, 815, 900000, 0), (1300, 845, 0, 0)]
            cfg[base:base + 4] = struct.pack("<HH", len(levels), 1)
            for k, o in enumerate(levels):
                cfg[base + 4 + 16 * k:base + 20 + 16 * k] = struct.pack("<4I", *o)
    cfg[0x80] = opp_valid
    blk = bytearray(0x18) + cfg
    while len(blk) % 4:
        blk.append(0xff)
    blk[0:2] = struct.pack("<H", 3)
    blk[8:12] = struct.pack("<I", len(blk))
    a = b = 0
    for i in range(0, len(blk), 4):
        a = (a + struct.unpack_from("<I", blk, i)[0]) & 0xffffffff
        b = (b + a) & 0xffffffff
    blk[0x10:0x18] = struct.pack("<II", a, b)
    return bytes(blk)


def sym(cpu, name, size=None):
    """Address of an object: by name (and size when names repeat)."""
    from elftools.elf.elffile import ELFFile
    if not hasattr(cpu, "_objs"):
        cpu._objs = []
        with open(cpu._elf, "rb") as fh:
            for s in ELFFile(fh).get_section_by_name(".symtab").iter_symbols():
                if s["st_info"]["type"] == "STT_OBJECT":
                    cpu._objs.append((s.name, s["st_value"], s["st_size"]))
    for n, a, sz in cpu._objs:
        if n == name and (size is None or sz == size):
            return a
    return None


def run(elf, args):
    soc = None
    if args.phecda:
        soc = pm_emu.PmSoc(auto_i2c=False)
        for i in (0, 1):
            soc.i2c[i].devices[0x45] = pm_emu.I2cDevice(f"i2c{i}:0x45")
    cpu, soc = pm_emu.boot(elf, soc)
    cpu._elf = elf
    if args.strap:
        lo, hi = (int(x, 16) for x in args.strap.split(":"))
        rest = sum(((lo | hi << 32) >> (8 * k)) & 0xff for k in range(1, 8))
        lo = (lo & ~0xff) | ((0xff - rest) & 0xff)
        soc.mem.write(0xa0000800, 4, lo)
        soc.mem.write(0xa0000804, 4, hi)
    if args.fuses is not None:
        opn = tuple(int(x, 16) for x in args.opn.split(":")) if args.opn else None
        cpu.write_bytes(EFUSE, efuse_image(args.fuses, args.phase, opn))
    if args.vmin:
        cpu.write(sym(cpu, "vf_curve_loaded"), 1, 0)
    if args.bios:
        cpu.write_bytes(0x200bf000, bios_image(args.bios.split(",")))
    if args.gb1boost:
        soc.mem.write(0x5600050c, 4, 0x10)
    for name in pm_emu.ASSERTS:
        if name in cpu.byname:
            cpu.stops[cpu.addr(name)] = name
    idle = next(cpu.addr(n) for n in test_pm.IDLE if n in cpu.byname)
    cpu.stops[idle] = "idle"
    why = cpu.run(test_pm.BOOT_BUDGET)
    out = {"why": why}
    for s in SELECTORS:
        a = sym(cpu, s)
        out[s] = cpu.read(a, 1) if a else None
    et = sym(cpu, "element_table", 13 * 12)
    doms = []
    for i in range(12):
        name_p, _, data = (cpu.read(et + 12 * i + 4 * k, 4) for k in range(3))
        name = cpu.read_bytes(name_p, 16).split(b"\0")[0].decode()
        raw = cpu.read_bytes(data, 28)
        _, clk, _, retry, lat, sus, ref, num, opps = struct.unpack("<IIIHHIHHI", raw)
        rows = []
        for j in range(max(num, 1) + 1):
            o = struct.unpack("<4I", cpu.read_bytes(opps + 16 * j, 16)) if opps else (0, 0, 0, 0)
            if o == (0, 0, 0, 0) and j >= num:
                break
            rows.append(o)
        doms.append((name, sus if sus != 0xffffffff else -1, ref, num, rows))
    out["domains"] = doms
    i2c = [f"{n} {w}" for n, w in soc.i2c_log]
    out["i2c"] = i2c
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("open_elf")
    ap.add_argument("--cix")
    ap.add_argument("--strap")
    ap.add_argument("--fuses", type=int)
    ap.add_argument("--phase", type=int, default=5)
    ap.add_argument("--opn")
    ap.add_argument("--vmin", action="store_true")
    ap.add_argument("--phecda", action="store_true")
    ap.add_argument("--bios")
    ap.add_argument("--gb1boost", action="store_true")
    ap.add_argument("--no-i2c", action="store_true", help="do not compare I2C transfers")
    ap.add_argument("-v", action="store_true")
    args = ap.parse_args()
    cix = args.cix
    if cix is None:
        from cix_debug import fetch
        cix = fetch(test_pm.CIX_PM)
    a, b = run(str(cix), args), run(args.open_elf, args)
    bad = 0
    print(f"CIX: {a['why']}, open: {b['why']}")
    for s in SELECTORS:
        flag = "" if a[s] == b[s] else "   <-- differs"
        bad += bool(flag)
        print(f"{s}: CIX {a[s]}, open {b[s]}{flag}")
    for da, db in zip(a["domains"], b["domains"]):
        same = da == db
        bad += not same
        print(f"{da[0]:10s} sustained {da[1]:2d} ref {da[2]:5d} n {da[3]:2d}  "
              f"{'same' if same else 'DIFFERS'}")
        if not same or args.v:
            for k, (x, y) in enumerate(zip(da[4] + [None] * 20, db[4] + [None] * 20)):
                if x is None and y is None:
                    break
                print(f"    {k:2d} CIX {x}  open {y}{'' if x == y else '   <--'}")
            if da[:4] != db[:4]:
                print(f"    open: sustained {db[1]} ref {db[2]} n {db[3]}")
    ia, ib = a["i2c"], b["i2c"]
    print(f"I2C transfers: CIX {len(ia)}, open {len(ib)}, {'same' if ia == ib else 'differ'}")
    if ia != ib and not args.no_i2c:
        bad += 1
        shown = 0
        for k in range(max(len(ia), len(ib))):
            x = ia[k] if k < len(ia) else None
            y = ib[k] if k < len(ib) else None
            if x != y and shown < 10:
                shown += 1
                print(f"    {k:2d} CIX {x}  open {y}")
    print("tables: " + ("same" if not bad else f"{bad} differences"))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
