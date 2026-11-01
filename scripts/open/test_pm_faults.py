#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Faults on the PM's I2C buses, open CSU_PM firmware only.

  test_pm_faults.py OPEN.elf [-v]

pm_emu's I2C controllers can be told to hang (fault "stuck": the bus held,
no transfer starts or ends).  On the open firmware's model of the SoC
(pm_emu.py; the default board configuration, then the MS-R1's), this
checks that a stuck bus costs only the work that needs that bus:

pmic (O6N): the bus of big cluster 0's rail hangs while the OS asks for the
  cluster's highest level (a voltage raise first).  Meanwhile SCMI is
  answered at once on another channel, the DPM sees the die at 100 C, the
  fans speed up; the voltage is given up within the bus's time-outs (no
  frequency raised without it); the bus is reset and marked
  down, its transfers then fail at once; another cluster's rail on a
  healthy bus still changes.  Once the bus is free again the level is set.

sensor (MS-R1): the bus of the board's TMP75s hangs; voltage changes go on
  (their worker and bus are not the sensors') and so does the DPM.

raise: a frequency never goes up on a voltage that failed.  Big cluster 0
  asked for its highest level by message and by its fast channel (both
  sky1soc_perf's) while its PMIC's bus fails: NACKs; loses the voltage
  writes (they reach the regulator, the transfer fails: the CIX driver's
  "failed, read back as set"); dies after the page is selected.  On the
  MP5475 (O6N), the MP2845 (Orange Pi 6, bucks shared) and an AU4683 (the
  Orange Pi 6's configuration with AU4683s, modelled here).  Its PLL is
  not written and keeps its registers; once the bus is healthy the same
  request writes the voltage, then the PLL.  Then lowers whose voltage
  writes were lost (the rail lower than the firmware was told), each
  followed by a raise of the cluster or, on a shared buck, of big cluster
  1: no PLL write before a voltage write.

two owners: big cluster 0 asked by fast channel, message, fast channel (and
  fast channel, message): after each the rail holds the voltage the PLL's
  setting needs, and the level asked for is the PLL's.

unsure: a voltage write that failed (NACKed, or lost: set but failed) is
  written again, as recorded, by the next pass before anything else, and
  the regulator holds it.  A resume while big0's PMIC NACKs: big0's PLL at
  its lowest level's setting and LEVEL_GET says so; the bus healthy, a
  raise writes the voltage first.  A boot with big0's PMIC losing every
  voltage write (dvfs's boot OPP given up): the first raise writes the
  voltage before the PLL.

The stacks' high-water marks are checked at the end of each (the tasks'
stacks are filled with 0xa5 when created).  A check that fails prints
FAIL; the exit status is the number of them.
"""

import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pm_emu  # noqa: E402
import test_pm  # noqa: E402

GPU, LITTLE, BIG0, BIG1 = 0, 2, 3, 4    # SCMI performance domains (test_pm.PERF_SET_DOMAINS)
SMT_STATUS, SMT_LEN, SMT_PAYLOAD = pm_emu.SMT_STATUS, pm_emu.SMT_LEN, pm_emu.SMT_PAYLOAD
STACK_MARGIN = 256                     # bytes a stack must have left

failures = 0
verbose = False


def check(ok, what):
    global failures
    print(f"{'ok  ' if ok else 'FAIL'} {what}")
    failures += not ok


def now_ms(cpu):
    return cpu.ccount * 1000 / pm_emu.CORE_HZ


def symbols(cpu):
    from elftools.elf.elffile import ELFFile
    if not hasattr(cpu, "_syms"):
        cpu._syms = {}
        with open(cpu.elf_path, "rb") as fh:
            for sy in ELFFile(fh).get_section_by_name(".symtab").iter_symbols():
                cpu._syms.setdefault(sy.name, (sy["st_value"], sy["st_size"]))
    return cpu._syms


def i2c_stats(cpu, bus):
    a, _ = symbols(cpu)["i2c_stats"]
    return dict(zip(("timeouts", "recoveries", "downs", "ups", "fast_fails"),
                    struct.unpack("<5I", cpu.read_bytes(a + 20 * bus, 20))))


def die_temp(cpu):
    return struct.unpack("<f", test_pm.sym_bytes(cpu, "dpm_main_tab", 0x720)[0xa4:0xa8])[0]


# --- SCMI without waiting for the answer -------------------------------------------

def send(cpu, soc, mbox, protocol, msg, args=()):
    base = pm_emu.MBOX + mbox * 0x10000
    rd = lambda off: soc.mem.read(base + off, 4)          # noqa: E731
    wr = lambda off, v: soc.mem.write(base + off, 4, v)   # noqa: E731
    assert rd(SMT_STATUS) & 1, f"channel {mbox} busy"
    wr(SMT_STATUS, rd(SMT_STATUS) & ~1)
    wr(pm_emu.SMT_HEADER, (msg & 0xff) | (protocol & 0xff) << 10)
    for i, a in enumerate(args):
        wr(SMT_PAYLOAD + 4 * i, a & 0xffffffff)
    wr(SMT_LEN, 4 + 4 * len(args))
    wr(pm_emu.SMT_FLAGS, 0)
    soc.mbox[mbox].ring()


def answer(soc, mbox):
    """None while the PM has not answered, else (status, words)."""
    base = pm_emu.MBOX + mbox * 0x10000
    rd = lambda off: soc.mem.read(base + off, 4)          # noqa: E731
    if not rd(SMT_STATUS) & 1:
        return None
    n = max(0, min((rd(SMT_LEN) - 4) // 4, 25))
    w = [rd(SMT_PAYLOAD + 4 * i) for i in range(n)]
    st = w[0] - (1 << 32) if w and w[0] >> 31 else (w[0] if w else None)
    return st, w[1:]


def wait_answer(cpu, soc, mbox, max_ms, step=0.25):
    """Run until mbox is answered: (elapsed ms, answer), or (None, None)."""
    t0 = now_ms(cpu)
    while now_ms(cpu) - t0 < max_ms:
        a = answer(soc, mbox)
        if a is not None:
            return now_ms(cpu) - t0, a
        why = pm_emu.run_for(cpu, soc, step)
        if why:
            print(f"     PM stopped: {why} at {cpu.func(cpu.pc)}")
            return None, None
    a = answer(soc, mbox)
    return (now_ms(cpu) - t0, a) if a is not None else (None, None)


def scmi(cpu, soc, mbox, protocol, msg, args=(), max_ms=50):
    send(cpu, soc, mbox, protocol, msg, args)
    return wait_answer(cpu, soc, mbox, max_ms)


def levels(cpu, soc, d):
    _, (st, w) = scmi(cpu, soc, 0, 0x13, 4, (d, 0))
    n = min(w[0] & 0xfff, (len(w) - 1) // 3) if st == 0 and w else 0
    return [w[1 + 3 * k] for k in range(n)]


def level(cpu, soc, d):
    _, (st, w) = scmi(cpu, soc, 0, 0x13, 8, (d,))
    return w[0] if st == 0 and w else None


def i2c_buses(soc, since):
    return sorted({w.split(":")[0] for _, w in soc.i2c_log[since:] if w.startswith("i2c")})


def set_level(cpu, soc, d, lv, max_ms=50):
    """Level lv on domain d (OS channel); (ms, status, buses written)."""
    n = len(soc.i2c_log)
    t, a = scmi(cpu, soc, 0, 0x13, 7, (d, lv), max_ms=max_ms)
    pm_emu.run_for(cpu, soc, 2)
    return t, (a[0] if a else None), i2c_buses(soc, n)


def settled(cpu, soc, bus, quiet_ms=25, max_ms=1000):
    """Run until bus's stats stop changing for quiet_ms: when that was."""
    t0, last, since = now_ms(cpu), i2c_stats(cpu, bus), now_ms(cpu)
    while now_ms(cpu) - t0 < max_ms:
        pm_emu.run_for(cpu, soc, 0.5)
        s = i2c_stats(cpu, bus)
        if s != last:
            last, since = s, now_ms(cpu)
        elif now_ms(cpu) - since >= quiet_ms:
            break
    return since


def stacks(cpu, name):
    """The tasks' stacks: free bytes below their deepest use."""
    syms = symbols(cpu)
    regions = [("scp", syms["_stack_bottom"][0], syms["_scp_stack_top"][0]),
               ("idle", syms["idle_stack"][0], syms["idle_stack"][0] + syms["idle_stack"][1])]
    wa, wsize = syms["workers"]
    each = wsize // 2
    stack_size = 1024 * 4
    for k, w in enumerate(("pmic", "sensor")):
        end = wa + each * (k + 1)
        regions.append((w, end - stack_size, end))
    out = []
    for task, lo, hi in regions:
        free = 0
        while lo + free < hi and cpu.read(lo + free, 4) == 0xa5a5a5a5:
            free += 4
        out.append((task, free, hi - lo))
    for task, free, size in out:
        check(free >= STACK_MARGIN, f"{name}: {task} stack: {size - free} of {size} bytes used")


# --- the scenarios ------------------------------------------------------------------

def boot(elf, board=None):
    test_pm.BOARD = test_pm.board_config(board) if board else None
    cpu, soc, why, _ = test_pm.boot(elf)
    check(why == "idle", f"boot{' (' + board + ')' if board else ''}: {why}")
    pm_emu.run_for(cpu, soc, 150)        # pm_ready's services
    return cpu, soc


def pmic_scenario(elf, board):
    cpu, soc = boot(elf, board)
    lv = {d: levels(cpu, soc, d) for d in (BIG0, LITTLE, GPU)}
    if not all(lv.values()):
        check(False, f"pmic: levels of the domains {lv}")
        return
    # where each domain's rail is: its lowest level, then its highest
    rail = {}
    for d in (BIG0, LITTLE, GPU):
        set_level(cpu, soc, d, lv[d][0])
        t, st, buses = set_level(cpu, soc, d, lv[d][-1])
        rail[d] = buses
        set_level(cpu, soc, d, lv[d][0])
        if verbose:
            print(f"     domain {d}: highest level {lv[d][-1]} in {t:.2f} ms, status {st}, I2C {buses}")
    if not rail[BIG0]:
        check(False, "pmic: big0's highest level writes no PMIC")
        return
    bus = int(rail[BIG0][0][3:])
    other = next((d for d in (LITTLE, GPU) if rail[d] and rail[d] != rail[BIG0]), None)
    check(True, f"pmic: big0's rail on I2C {bus}; healthy level set in {t:.2f} ms")

    fan0 = soc.fan_speed(0)
    soc.i2c[bus].fault = "stuck"
    before = i2c_stats(cpu, bus)
    t0 = now_ms(cpu)
    send(cpu, soc, 0, 0x13, 7, (BIG0, lv[BIG0][-1]))
    # SCMI on TF-A's channel while the PMIC worker waits on the stuck bus
    pm_emu.run_for(cpu, soc, 0.5)
    t, a = scmi(cpu, soc, 2, 0x10, 0)
    check(t is not None and t < 2 and a[0] == 0,
          f"pmic: bus {bus} stuck: SCMI base version on channel 2 answered in "
          f"{'never' if t is None else f'{t:.2f} ms'}")
    # the DPM and the PVT polling see the die heat up; the fans react
    soc.pvt_temp[:] = [100.0] * len(soc.pvt_temp)
    soc.pvt_update()
    pm_emu.run_for(cpu, soc, 20)
    dt = die_temp(cpu)
    check(dt > 90, f"pmic: bus {bus} stuck: the DPM's die temperature {dt:.1f} C after 20 ms at 100 C")
    t, a = wait_answer(cpu, soc, 0, 500)
    # (scmi_perf answers a level set before DVFS has done it, as CIX's)
    check(a is not None, f"pmic: big0's level set answered: {'never' if a is None else f'status {a[0]}'}")
    took = settled(cpu, soc, bus) - t0
    check(took < 100, f"pmic: the PMIC worker gave the voltage up {took:.1f} ms after the request")
    cur = level(cpu, soc, BIG0)
    check(cur == lv[BIG0][0], f"pmic: big0 stays at level {cur} (lowest {lv[BIG0][0]})")
    s = i2c_stats(cpu, bus)
    d = {k: s[k] - before[k] for k in s}
    check(d["timeouts"] >= 3 and d["recoveries"] >= 3 and d["downs"] >= 1,
          f"pmic: bus {bus}: {d['timeouts']} time-outs, {d['recoveries']} resets, "
          f"down {d['downs']} times, {d['fast_fails']} transfers failed at once")
    if other is not None:
        t, st, buses = set_level(cpu, soc, other, lv[other][-1])
        check(st == 0 and t is not None and t < 5,
              f"pmic: domain {other} (rail on {rail[other]}) set meanwhile: status {st} in "
              f"{'never' if t is None else f'{t:.2f} ms'}, I2C {buses}")
        set_level(cpu, soc, other, lv[other][0])
    # fans: their control runs on its own period
    pm_emu.run_for(cpu, soc, 1500)
    fan1 = soc.fan_speed(0)
    check(fan1 > fan0, f"pmic: bus {bus} stuck: fan 0 from {fan0} to {fan1} rpm at 100 C")
    # the bus free again: after its back-off the level is set
    soc.i2c[bus].fault = None
    soc.pvt_temp[:] = [40.0] * len(soc.pvt_temp)
    soc.pvt_update()
    pm_emu.run_for(cpu, soc, 2100)
    t, st, buses = set_level(cpu, soc, BIG0, lv[BIG0][-1])
    s = i2c_stats(cpu, bus)
    check(st == 0 and f"i2c{bus}" in buses,
          f"pmic: bus {bus} free again: big0's highest level set (status {st}, I2C {buses}), "
          f"bus up again {s['ups'] - before['ups']} times")
    cur = level(cpu, soc, BIG0)
    check(cur == lv[BIG0][-1], f"pmic: big0 at level {cur}")
    stacks(cpu, "pmic")


def sensor_scenario(elf):
    cpu, soc = boot(elf, "ms-r1")
    n = len(soc.i2c_log)
    pm_emu.run_for(cpu, soc, 250)        # a board-sensor read (200 ms)
    sensors = sorted({w.split(":")[0] for _, w in soc.i2c_log[n:] if ":0x4d" in w})
    if not sensors:
        check(False, "sensor: no board-sensor read (0x4d) in 250 ms")
        return
    bus = int(sensors[0][3:])
    check(True, f"sensor: the MS-R1's TMP75s on I2C {bus}")
    lv = levels(cpu, soc, BIG0)
    set_level(cpu, soc, BIG0, lv[0])
    before = i2c_stats(cpu, bus)
    soc.i2c[bus].fault = "stuck"
    pm_emu.run_for(cpu, soc, 450)        # two board-sensor reads (200 ms)
    t, st, buses = set_level(cpu, soc, BIG0, lv[-1])
    check(st == 0 and t is not None and t < 5,
          f"sensor: TMP75 bus {bus} stuck: big0's highest level set in "
          f"{'never' if t is None else f'{t:.2f} ms'} (status {st}, I2C {buses})")
    soc.pvt_temp[:] = [100.0] * len(soc.pvt_temp)
    soc.pvt_update()
    pm_emu.run_for(cpu, soc, 20)
    dt = die_temp(cpu)
    check(dt > 90, f"sensor: bus {bus} stuck: the DPM's die temperature {dt:.1f} C")
    s = i2c_stats(cpu, bus)
    d = {k: s[k] - before[k] for k in s}
    check(d["timeouts"] >= 3 and d["downs"] >= 1,
          f"sensor: bus {bus}: {d['timeouts']} time-outs, down {d['downs']} times, "
          f"{d['fast_fails']} transfers failed at once")
    soc.i2c[bus].fault = None
    pm_emu.run_for(cpu, soc, 2500)
    s = i2c_stats(cpu, bus)
    check(s["ups"] > before["ups"], f"sensor: bus {bus} free again: up again")
    stacks(cpu, "sensor")


# --- raises whose voltage fails ------------------------------------------------------
#
# A frequency must never go up on a rail whose voltage write failed.  The
# requests go two ways: an SCMI PERFORMANCE_LEVEL_SET message and the OS's
# fast channel.  For the CPU clusters and the DSU both end in sky1soc_perf's
# queue (scmi_perf's pass); other domains' messages go to dvfs (voltage,
# then clock), but on these boards only the clusters' rails are PMICs'.
# What is checked is the clock generator: the writes a healthy raise of the
# cluster makes (its PLL's block), and the PLL's registers.

PLL_BLOCK = 0x100
VOUT_LEN = 3                           # a voltage write: register and two bytes (MP5475, VOUT_COMMAND)


class Au4683(pm_emu.Mp2845):
    """An AU4683 as CIX's dev_au4683.c uses it (src/pmic.c): paged like the
    MP2845; VOUT_COMMAND 0x21 ((mV - 245) / 5, 12 bits), READ_VOUT 0x8b in
    PMBus LINEAR11 (exponent -10 here), revision 0x9b.  Modelled, not known."""

    PAGED = (0x21, 0x8b)

    def __init__(self, name):
        super().__init__(name)
        self.regs[0x9b] = 1

    def write(self, byte):
        super().write(byte)
        if self.ptr == 0x21 and self.n == 2:
            r = self.paged[(self.page, 0x21)]
            mv = (r[0] | (r[1] & 0xf) << 8) * 5 + 245
            mant = min(round(mv * 1000 / 977), 0x7ff)
            self.paged[(self.page, 0x8b)] = bytearray((mant & 0xff, (0x16 << 3) | mant >> 8, 0))


def au4683_config():
    """The Orange Pi 6's PM configuration with its MP2845s (type 2, 0x45)
    as AU4683s (type 3, 0x21): CIX's other paged regulator, its own
    formats; the bucks shared as on the Orange Pi 6."""
    blk = bytearray(test_pm.board_config("opi6"))
    for d in range(8):
        off = 0x18 + 0x1e + 8 * d
        w0 = struct.unpack_from("<I", blk, off)[0]
        if w0 & 7 == 2:
            w0 = (w0 & ~(7 | 0x7f << 22)) | 3 | 0x21 << 22
            struct.pack_into("<I", blk, off, w0)
    n = struct.unpack_from("<I", blk, 8)[0]
    blk[0x10:0x18] = bytes(8)
    a = b = 0
    for i in range(0, n, 4):
        a = (a + struct.unpack_from("<I", blk, i)[0]) & 0xffffffff
        b = (b + a) & 0xffffffff
    blk[0x10:0x18] = struct.pack("<II", a, b)
    return bytes(blk)


def boot_cfg(elf, name, cfg):
    """boot() with a configuration's bytes (None: as boot(elf, name))."""
    if cfg is None:
        return boot(elf, name)
    auto = pm_emu.auto_device
    pm_emu.auto_device = lambda n, addr: Au4683(n) if addr == 0x21 else auto(n, addr)
    test_pm.BOARD = cfg
    cpu, soc, why, _ = test_pm.boot(elf)
    check(why == "idle", f"boot ({name}): {why}")
    pm_emu.run_for(cpu, soc, 150)
    return cpu, soc


class BusFault:
    """Transfers on an I2C bus from the after-th on fail: "nack" (no device
    answers), "lost" (a voltage write reaches the regulator, then is NACKed:
    set, but the write failed; reads answer).  Logs each transfer with the
    number of register writes before it (cpu.writes), to order the I2C
    against the clock generator."""

    def __init__(self, cpu, soc, bus):
        self.cpu, self.c = cpu, soc.i2c[bus]
        self.mode, self.after, self.n, self.log = None, 0, 0, []
        self.c.transfer = self.transfer

    def arm(self, mode, after=0):
        self.mode, self.after, self.n = mode, after, 0

    def transfer(self, addr):
        c = self.c
        write = not c.r[pm_emu.CR] & pm_emu.CR_RW
        self.log.append((len(self.cpu.writes), addr, write, bytes(c.tx)))
        n, self.n = self.n, self.n + 1
        if self.mode is None or n < self.after:
            return pm_emu.CadenceI2c.transfer(c, addr)
        if self.mode == "nack":
            c.fault = "nack"
            pm_emu.CadenceI2c.transfer(c, addr)
            c.fault = None
            return None
        vout = write and len(c.tx) >= VOUT_LEN
        pm_emu.CadenceI2c.transfer(c, addr)
        if vout:
            c.isr |= pm_emu.ISR_NACK
        return None


def cgc_writes(cpu, since, blocks):
    return [(a, v) for _, a, _, v in cpu.writes[since:] if a & ~(PLL_BLOCK - 1) in blocks]


def pll_regs(cpu, blocks):
    return {b: tuple(cpu.read(b + o, 4) for o in range(0, PLL_BLOCK, 4)) for b in blocks}


def vout_writes(fault, since=0):
    """Voltage writes logged by fault from entry since: (writes before it, bytes)."""
    return [(w, tx) for w, _, wr, tx in fault.log[since:] if wr and len(tx) >= VOUT_LEN]


def request(cpu, soc, path, d, lv, ms=60):
    """Level lv on domain d by path ("msg": the message; "fc": big0's fast channel)."""
    if path == "fc":
        assert d == BIG0
        test_pm.ask(cpu, soc, 0, None, "fc", (test_pm.BIG0_FC, lv))
    else:
        send(cpu, soc, 0, 0x13, 7, (d, lv))
        wait_answer(cpu, soc, 0, 50)
    pm_emu.run_for(cpu, soc, ms)


def calibrate(cpu, soc, path, d, lv):
    """A healthy raise of d from its lowest level to its highest: the bus of
    its rail, the clock generator blocks it writes.  d is left at its lowest."""
    request(cpu, soc, path, d, lv[0])
    nw, ni = len(cpu.writes), len(soc.i2c_log)
    request(cpu, soc, path, d, lv[-1])
    blocks = sorted({a & ~(PLL_BLOCK - 1) for _, a, _, _ in cpu.writes[nw:]
                     if test_pm.CGC[0] <= a < test_pm.CGC[1]})
    buses = i2c_buses(soc, ni)
    request(cpu, soc, path, d, lv[0])
    return (int(buses[0][3:]) if buses else None), blocks


def raise_failed(elf, name, cfg, path, mode, after=0, d=BIG0):
    """d at its lowest asked for its highest level while its PMIC's bus
    fails (mode, from the after-th transfer): no clock generator write to
    d's PLL, its registers as they were; once the bus is healthy, the same
    request sets the voltage and then the frequency."""
    tag = f"raise {name} {path} {mode}{f' after {after}' if after else ''}"
    cpu, soc = boot_cfg(elf, name, cfg)
    lv = levels(cpu, soc, d)
    bus, blocks = calibrate(cpu, soc, path, d, lv)
    if bus is None or not blocks:
        check(False, f"{tag}: a healthy raise writes I2C {bus}, clock generator {blocks}")
        return
    fault = BusFault(cpu, soc, bus)
    regs0, nw = pll_regs(cpu, blocks), len(cpu.writes)
    fault.arm(mode, after)
    request(cpu, soc, path, d, lv[-1], ms=200)
    got = cgc_writes(cpu, nw, blocks)
    tried = len(fault.log)
    check(tried > 0 and not got and pll_regs(cpu, blocks) == regs0,
          f"{tag}: I2C {bus} failing ({tried} transfers): PLL {[hex(b) for b in blocks]} "
          f"{'untouched' if not got else f'written {len(got)} times, first {got[0][0]:#x}={got[0][1]:#x}'}")
    cur = level(cpu, soc, d)
    check(cur != lv[-1], f"{tag}: level read back {cur} (asked {lv[-1]})")
    fault.arm(None)
    pm_emu.run_for(cpu, soc, 2100)             # the bus's back-off, if it went down
    nw, nl = len(cpu.writes), len(fault.log)
    request(cpu, soc, path, d, lv[-1])
    first, v = first_pll_write(cpu, nw, blocks), vout_writes(fault, nl)
    check(first is not None and bool(v) and v[0][0] <= first,
          f"{tag}: bus healthy again: {len(v)} voltage writes, the PLL "
          + ("not written" if first is None else
             "written after them" if v and v[0][0] <= first else "written first"))


def first_pll_write(cpu, since, blocks):
    """Index in cpu.writes of the first write to blocks from since, or None."""
    return next((i for i, (_, a, _, _) in enumerate(cpu.writes[since:], since)
                 if a & ~(PLL_BLOCK - 1) in blocks), None)


def lower_lost_then_raise(elf, name, cfg, path, target, d=BIG0, then=None):
    """d at its highest lowered to its lowest while the voltage writes are
    lost (they reach the regulator, the transfer fails: the rail at the
    lower voltage, the write failed); the bus healthy again, a level
    (target: index in d's levels) on d, or on domain then (sharing d's
    buck), from its lowest: no PLL write of the raised domain before a
    voltage write."""
    raised = d if then is None else then
    tag = (f"lower lost {name} {path}: domain {d} lowered, "
           f"domain {raised} to its level [{target}]")
    cpu, soc = boot_cfg(elf, name, cfg)
    lv = levels(cpu, soc, d)
    bus, _ = calibrate(cpu, soc, path, d, lv)
    rlv = levels(cpu, soc, raised)
    rbus, blocks = calibrate(cpu, soc, "msg" if then is not None else path, raised, rlv)
    if bus is None or rbus != bus or not blocks:
        check(False, f"{tag}: rails on I2C {bus} and {rbus}, clock generator {blocks}")
        return
    fault = BusFault(cpu, soc, bus)
    request(cpu, soc, path, d, lv[-1])
    fault.arm("lost")
    nl = len(fault.log)
    request(cpu, soc, path, d, lv[0], ms=200)
    lost = vout_writes(fault, nl)
    fault.arm(None)
    pm_emu.run_for(cpu, soc, 50)
    nw, nl = len(cpu.writes), len(fault.log)
    request(cpu, soc, "msg" if then is not None else path, raised, rlv[target])
    first, v = first_pll_write(cpu, nw, blocks), vout_writes(fault, nl)
    ok = first is None or (bool(v) and v[0][0] <= first)
    check(bool(lost) and ok,
          f"{tag}: {len(lost)} voltage writes lost; then "
          + ("no PLL write" if first is None else
             f"PLL written after {'no voltage write' if not v else ('a voltage write' if v[0][0] <= first else 'the PLL, voltage after it')}"))


def raise_scenarios(elf):
    au = au4683_config()
    # MP5475 (O6N), MP2845 and AU4683 (generic paged driver, shared bucks)
    for name, cfg in (("o6n", None), ("opi6", None), ("opi6-au4683", au)):
        for path in ("msg", "fc"):
            raise_failed(elf, name, cfg, path, "nack")
    # a write that reaches the regulator but fails: set, not known to be
    raise_failed(elf, "o6n", None, "fc", "lost")
    raise_failed(elf, "opi6", None, "msg", "lost")
    # the bus dies after the first transfer: the page selected, VOUT not
    raise_failed(elf, "opi6", None, "msg", "nack", after=1)
    raise_failed(elf, "opi6-au4683", au, "fc", "nack", after=1)
    # a lower whose writes were lost, then a raise
    lower_lost_then_raise(elf, "o6n", None, "msg", -2)
    lower_lost_then_raise(elf, "o6n", None, "fc", -1)
    lower_lost_then_raise(elf, "opi6", None, "msg", -2)
    # the buck shared: big 0 lowered with its writes lost, big 1 raised
    lower_lost_then_raise(elf, "opi6", None, "msg", -2, d=BIG0, then=BIG1)


# --- two owners of a cluster -------------------------------------------------------
#
# Big cluster 0 is asked for levels by SCMI message and by the OS's fast
# channel in turn (CIX's firmware set the first through dvfs and the second
# through sky1soc_perf, each with its own record of where the cluster was;
# ours has sky1soc_perf do both).  No fault: after each request the
# regulator must hold at least the voltage the PLL's setting had when the
# fast channel alone set it, and the level asked for must be applied.

def vout_mv(soc, bus, buck):
    """An MP5475 buck's VOUT register, mV."""
    dev = soc.i2c[bus].devices[0x60]
    return (dev.regs[0x13 + 8 * buck] << 9) + dev.regs[0x14 + 8 * buck] * 2


def two_owners(elf, steps, name="o6n"):
    """steps: (path, level index) in turn on big0 from its lowest level."""
    tag = "two owners " + " ".join(f"{p}:{i}" for p, i in steps)
    cpu, soc = boot(elf, name)
    lv = levels(cpu, soc, BIG0)
    bus, blocks = calibrate(cpu, soc, "fc", BIG0, lv)
    if bus is None or not blocks:
        check(False, f"{tag}: big0's rail on I2C {bus}, clock generator {blocks}")
        return
    ni = len(soc.i2c_log)
    request(cpu, soc, "fc", BIG0, lv[-1])
    buck = next(int(w.split()[2], 16) - 0x13 for _, w in soc.i2c_log[ni:]
                if " W " in w and ":0x60" in w) // 8
    # each level's PLL setting and voltage, one owner (the fast channel), from the lowest up
    need = {}
    for lvl in lv:
        request(cpu, soc, "fc", BIG0, lvl)
        need[lvl] = (pll_regs(cpu, blocks), vout_mv(soc, bus, buck))
    request(cpu, soc, "fc", BIG0, lv[0])
    for path, i in steps:
        request(cpu, soc, path, BIG0, lv[i])
        regs, mv = pll_regs(cpu, blocks), vout_mv(soc, bus, buck)
        at = next((lvl for lvl, (r, _) in need.items() if r == regs), None)
        want = need[at][1] if at is not None else None
        check(at is not None and mv >= want,
              f"{tag}: {path} asked {lv[i]}: PLL at level {at} (needs {want} mV), rail {mv} mV")
        check(at == lv[i], f"{tag}: {path} asked {lv[i]}: applied (PLL at level {at})")


def two_owner_scenarios(elf):
    # the OS's fast channel, then a lower by message (or a TDP cap), then the fast channel
    two_owners(elf, (("fc", -1), ("msg", 1), ("fc", -2)))
    # a lower by message after the fast channel raised the cluster
    two_owners(elf, (("fc", -1), ("msg", 2)))


# --- unsure voltages: rewritten first; resume; boot -----------------------------------

def next_pass_rewrites(elf, mode):
    """Big0 by fast channel to its highest level while its voltage writes
    fail (mode), the bus healthy again, then its lowest level asked (where
    the record has it): the pass writes the voltage it has recorded first,
    and the regulator then holds it."""
    tag = f"unsure {mode}"
    cpu, soc = boot(elf, "o6n")
    lv = levels(cpu, soc, BIG0)
    bus, blocks = calibrate(cpu, soc, "fc", BIG0, lv)
    dev = soc.i2c[bus].devices[0x60]
    fault = BusFault(cpu, soc, bus)
    before = bytes(dev.regs)
    fault.arm(mode)
    nl = len(fault.log)
    request(cpu, soc, "fc", BIG0, lv[-1], ms=200)
    failed = [tx for w, a, wr, tx in fault.log[nl:] if wr and len(tx) >= VOUT_LEN]
    fault.arm(None)
    pm_emu.run_for(cpu, soc, 2100)
    if not failed:
        check(False, f"{tag}: no voltage write tried")
        return
    reg = failed[0][0]
    want = bytes([reg]) + before[reg:reg + 2]    # the lowest level's voltage, as before the fault
    nw, nl = len(cpu.writes), len(fault.log)
    request(cpu, soc, "fc", BIG0, lv[0])
    v, first = vout_writes(fault, nl), first_pll_write(cpu, nw, blocks)
    held = bytes(dev.regs[reg:reg + 2])
    check(bool(v) and (first is None or v[0][0] <= first) and v[0][1] == want
          and held == want[1:],
          f"{tag}: next pass: {len(v)} voltage writes "
          f"({v[0][1].hex(' ') if v else '-'}; recorded {want.hex(' ')}), "
          f"PLL {'not written' if first is None else 'after' if v and v[0][0] <= first else 'first'}, "
          f"regulator {held.hex(' ')}")


def suspend_resume(cpu, soc, during_resume=None):
    """TF-A's suspend (test_pm.power_suite), then the wake; during_resume()
    is called before the wake."""
    soc.se.suspend()
    test_pm.pm_msg(cpu, soc, test_pm.TFA, test_pm.STR_BOOT_CORE_NOTIFY, (0,))
    pm_emu.scmi(cpu, soc, 0x12, 3, (0, 4), mbox=test_pm.TFA, budget=3_000_000)
    asleep = test_pm.run_until(cpu, soc, "asleep")
    if during_resume:
        during_resume()
    soc.se.wake()
    soc.s5.wake()
    return asleep, test_pm.run_until(cpu, soc, "quiet")


def resume_rail_lost(elf):
    """Big0 at its highest level by fast channel; suspended; its PMIC's bus
    NACKs while it resumes, so the rail is not set again: after the resume
    its PLL is at its lowest level's setting, LEVEL_GET says so; the bus
    healthy again, its highest level: the voltage, then the PLL."""
    tag = "resume, rail lost"
    cpu, soc = boot(elf, "o6n")
    lv = levels(cpu, soc, BIG0)
    bus, blocks = calibrate(cpu, soc, "fc", BIG0, lv)
    request(cpu, soc, "fc", BIG0, lv[0])
    low_regs = pll_regs(cpu, blocks)
    request(cpu, soc, "fc", BIG0, lv[-1])
    high_regs = pll_regs(cpu, blocks)
    fault = BusFault(cpu, soc, bus)
    a, r = suspend_resume(cpu, soc, lambda: fault.arm("nack"))
    regs = pll_regs(cpu, blocks)
    check(a == "asleep" and r == "quiet" and regs == low_regs,
          f"{tag}: suspend {a}, resume {r}: PLL at "
          + ("its lowest level's setting" if regs == low_regs else
             "the highest level's setting" if regs == high_regs else "another setting"))
    cur = level(cpu, soc, BIG0)
    check(cur == lv[0], f"{tag}: LEVEL_GET {cur} (lowest {lv[0]})")
    fault.arm(None)
    pm_emu.run_for(cpu, soc, 2100)
    nw, nl = len(cpu.writes), len(fault.log)
    request(cpu, soc, "fc", BIG0, lv[-2])
    request(cpu, soc, "fc", BIG0, lv[-1])
    first, v = first_pll_write(cpu, nw, blocks), vout_writes(fault, nl)
    check(first is not None and bool(v) and v[0][0] <= first and pll_regs(cpu, blocks) == high_regs,
          f"{tag}: bus healthy, highest level: {len(v)} voltage writes, PLL "
          + ("not written" if first is None else "after them" if v and v[0][0] <= first else "first"))


def boot_writes_lost(elf):
    """Big0's PMIC loses every voltage write from reset (they reach the
    regulator, the transfer fails): the boot voltages and dvfs's boot OPP
    fail (dvfs gives up after its retries).  The bus healthy, big0 by fast
    channel to its lowest, then its highest level: a voltage write first,
    no PLL write before one, the level set."""
    tag = "boot, writes lost"
    orig = pm_emu.CadenceI2c.transfer
    state = {"cpu": None}

    def transfer(c, addr):
        write = not c.r[pm_emu.CR] & pm_emu.CR_RW
        vout = write and len(c.tx) >= VOUT_LEN and c.name == BOOT_LOST_BUS
        orig(c, addr)
        if vout:
            c.isr |= pm_emu.ISR_NACK
    pm_emu.CadenceI2c.transfer = transfer
    try:
        cpu, soc = boot(elf, "o6n")
    finally:
        pm_emu.CadenceI2c.transfer = orig
    lv = levels(cpu, soc, BIG0)
    bus = int(BOOT_LOST_BUS[3:])
    fault = BusFault(cpu, soc, bus)
    nw, nl = len(cpu.writes), len(fault.log)
    request(cpu, soc, "fc", BIG0, lv[0])
    v0 = vout_writes(fault, nl)
    request(cpu, soc, "fc", BIG0, lv[-1])
    blocks = sorted({a & ~(PLL_BLOCK - 1) for _, a, _, _ in cpu.writes[nw:]
                     if test_pm.CGC[0] <= a < test_pm.CGC[1]})
    first, v = first_pll_write(cpu, nw, blocks), vout_writes(fault, nl)
    check(bool(v0) and bool(v) and first is not None and v[0][0] <= first,
          f"{tag}: lowest level: {len(v0)} voltage writes; highest: PLL "
          + ("not written" if first is None else "after a voltage write" if v and v[0][0] <= first
             else "before any voltage write"))
    cur = level(cpu, soc, BIG0)
    check(cur == lv[-1], f"{tag}: level {cur} (asked {lv[-1]})")


BOOT_LOST_BUS = "i2c1"                  # big0's PMIC on the O6N


def unsure_scenarios(elf):
    next_pass_rewrites(elf, "nack")
    next_pass_rewrites(elf, "lost")
    resume_rail_lost(elf)
    boot_writes_lost(elf)


def main():
    global verbose
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("elf")
    ap.add_argument("-v", action="store_true")
    a = ap.parse_args()
    verbose = a.v
    pmic_scenario(a.elf, "o6n")          # fans the PM drives
    sensor_scenario(a.elf)
    raise_scenarios(a.elf)
    two_owner_scenarios(a.elf)
    unsure_scenarios(a.elf)
    print(f"{failures} failed")
    return min(failures, 125)


if __name__ == "__main__":
    sys.exit(main())
