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

GPU, LITTLE, BIG0 = 0, 2, 3            # SCMI performance domains (test_pm.PERF_SET_DOMAINS)
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


def main():
    global verbose
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("elf")
    ap.add_argument("-v", action="store_true")
    a = ap.parse_args()
    verbose = a.v
    pmic_scenario(a.elf, "o6n")          # fans the PM drives
    sensor_scenario(a.elf)
    print(f"{failures} failed")
    return min(failures, 125)


if __name__ == "__main__":
    sys.exit(main())
