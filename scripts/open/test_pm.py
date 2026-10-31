#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Differential test of the open CSU_PM firmware against CIX's.

  test_pm.py [OPEN.elf] [--cix CIX.elf] [--scmi] [--power] [--no-cache] [-v]

Each image runs in pm_emu.py on the same Sky1 model from reset until it
idles with the SE handshake done (0x5c5a, 0x55ca, 0x5a5c), without an
assertion.  Compared:

* register writes, per register in order (UART and I2C controllers
  excepted: the I2C transfers are compared instead);
* the final contents of the shared memory it writes (the hub window at
  0x60000000: PCC tables, logs);
* with --scmi, the answers to the same SCMI commands on every channel
  (base protocol discovery, then each protocol's attributes and its domains),
  then performance levels and limits set on the OS's channel and read back
  (each set followed by the same number of instructions run on both), and
  levels written to the OS's performance fast channels (a write raises the
  channel's bit in the mailbox status), the level-get channel read back;
  and the I2C transfers and clock generator writes (PMIC voltages, PLLs)
  the SCMI commands lead to;
* with --power, after boot (POWER_PHASES): CIX's PM messages as UEFI,
  Linux and TF-A send them (answers compared), TF-A's suspend on channel 2
  (PM message 0x42 with the boot core, then SCMI SYSTEM_POWER_STATE_SET
  suspend; the SE's side as firmware/se does it, pm_emu.SePeer), the PM
  asleep until a wake-up, the resume back to idle, SCMI answered again;
  then shutdown, and warm reset from a copy of the booted state.  Per
  phase: register writes per register, I2C transfers, the SE handshake
  words, the S5 controller's events (sleep, wake, reset).

With CIX's image alone it prints what CIX's firmware does: the reference.
The open image is asked the questions CIX's answers led to.  CIX's run is
cached under out/test_pm-cache, keyed by the image and the emulator's
sources.
"""

import argparse
import hashlib
import pickle
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pm_emu  # noqa: E402

CIX_PM = "release/pm_fw/Release/csupm_sky1"
IDLE = ("vApplicationIdleHook", "arch_irq_wait")    # CIX's (FreeRTOS), ours
BOOT_BUDGET = 8_000_000
CHANNELS = (0, 2, 4, 6)               # AP -> PM mailboxes with an SMT channel
# not compared register by register: UART data, I2C controllers (the
# transfers are compared)
IGNORE_BLOCKS = {0xa0030000, 0xa0010000, 0xa0011000, 0xa0012000, 0xa0013000}
IGNORE_RANGES = []
MAX_DOMAINS = 8                       # per protocol, to keep the run short
PERF_SET_DOMAINS = (0, 2, 3, 7)       # GPU core, little, big 0, DSU
SETTLE = 400_000                      # instructions run after a level or limits set
# the OS's performance fast channels: domain, offset in mailbox 0 (level
# set) and mailbox 1 (level get); a write to channel c sets status bit 9 + c
PERF_FC = ((0, 0x8c), (2, 0x94), (3, 0x98))
# shared memory, compared by its final contents: the hub window
# (CIX's _memmap_mem_sram_hub, DRAM and SRAM seen through a window)
MEMORY = [(0x60000000, 0xa0000000, "hub window")]
CGC = (0xa0060000, 0xa0070000)        # clock generator: PLLs, compared during SCMI


def is_memory(addr):
    return any(lo <= addr < hi for lo, hi, _ in MEMORY)


def ignored(addr):
    return (addr & ~0xfff) in IGNORE_BLOCKS or any(lo <= addr < hi for lo, hi in IGNORE_RANGES)


def boot(elf):
    cpu, soc = pm_emu.boot(elf)
    for name in pm_emu.ASSERTS:
        if name in cpu.byname:
            cpu.stops[cpu.addr(name)] = name
    idle = next(cpu.addr(n) for n in IDLE if n in cpu.byname)
    cpu.stops[idle] = "idle"
    why = cpu.run(BOOT_BUDGET)
    del cpu.stops[idle]
    # the PM answers the SE's 0x55ca from a task, after it first idles
    while why == "idle" and soc.se.log[-1:] != [0x5a5c] and cpu.insns < BOOT_BUDGET:
        why = cpu.run(50_000) or "idle"
    regs = defaultdict(list)
    for pc, addr, size, value in cpu.writes:
        if not ignored(addr) and not is_memory(addr) and \
                not (pm_emu.RAM[0] <= addr < pm_emu.RAM[0] + pm_emu.RAM[1]):
            regs[addr].append(value)
    return cpu, soc, why, regs


def memory(soc):
    """Pages of the shared memory the run wrote: page -> bytes."""
    return {p << 12: bytes(soc.mem.pages[p][:0x1000]) for p in soc.mem.pages if is_memory(p << 12)}


def ask(cpu, soc, chan, proto, msg, args=()):
    if proto is None:                 # not a command
        base = pm_emu.MBOX + chan * 0x10000
        if msg == "fc":               # the AP writes a fast channel
            off, value = args
            soc.mem.write(base + off, 4, value)
            soc.mbox[chan].ring(1 << (9 + (off - 0x8c) // 4))
            return ("written", [])
        if msg == "read":             # the AP reads a word of a mailbox
            return (0, [soc.mem.read(pm_emu.MBOX + args[0], 4)])
        cpu.run(msg)                  # let the PM run (a set completing)
        return ("settled", [])
    try:
        return pm_emu.scmi(cpu, soc, proto, msg, args, mbox=chan, budget=3_000_000)
    except pm_emu.ScmiError as e:
        return ("error", str(e))


def scmi_suite(cpu, soc):
    """(question, answer) pairs, the questions chosen from the answers."""
    out = []

    def q(chan, proto, msg, args=()):
        a = ask(cpu, soc, chan, proto, msg, args)
        out.append(((chan, proto, msg, tuple(args)), a))
        return a

    for chan in CHANNELS:
        for msg in (0, 1, 3, 4, 5):
            q(chan, 0x10, msg)
        st, w = q(chan, 0x10, 6, (0,))
        protos = [(x >> 8 * k) & 0xff for x in w[1:] for k in range(4)][:w[0]] if st == 0 and w else []
        for p in protos:
            q(chan, p, 0)
            st, w = q(chan, p, 1)
            n = (w[0] & 0xffff) if st == 0 and w else 0
            if p == 0x11:                               # power: attributes, state
                for d in range(min(n, MAX_DOMAINS)):
                    q(chan, p, 3, (d,))
                    q(chan, p, 5, (d,))
            elif p == 0x13:                             # perf: attributes, levels, limits, level
                for d in range(min(n, MAX_DOMAINS)):
                    q(chan, p, 3, (d,))
                    q(chan, p, 4, (d, 0))
                    q(chan, p, 6, (d,))
                    q(chan, p, 8, (d,))
            elif p == 0x14:                             # clock: attributes, rate
                for d in range(min(n, MAX_DOMAINS)):
                    q(chan, p, 3, (d,))
                    q(chan, p, 6, (d,))
            elif p == 0x15:                             # sensor: descriptions
                q(chan, p, 3, (0,))
    # performance levels set on the OS's channel, from its first and last
    # level, and read back; limits (CIX lets the OS set none) likewise
    levels = {qn[3][0]: a[1] for qn, a in out
              if qn[:3] == (0, 0x13, 4) and isinstance(a[0], int) and a[0] == 0 and a[1]}
    for d in PERF_SET_DOMAINS:
        w = levels.get(d, [])
        lv = [w[1 + 3 * k] for k in range(min(w[0] & 0xfff, (len(w) - 1) // 3))] if w else []
        for level in (lv[:1] + lv[-1:]) if lv else (1,):
            q(0, 0x13, 7, (d, level))
            q(0, None, SETTLE)
            q(0, 0x13, 8, (d,))
        if lv:
            q(0, 0x13, 5, (d, lv[-1], lv[0]))
            q(0, None, SETTLE)
            q(0, 0x13, 6, (d,))
    for d, off in PERF_FC:
        w = levels.get(d, [])
        lv = [w[1 + 3 * k] for k in range(min(w[0] & 0xfff, (len(w) - 1) // 3))] if w else []
        for level in lv[:1] + lv[-1:]:
            q(0, None, "fc", (off, level))
            q(0, None, SETTLE)
            q(0, None, "read", (0x10000 + off,))
    return out


def run(elf, questions=None, suite=False, power=False):
    """Boot an image; ask the questions ([(question, answer)] from CIX's
    run) or, with suite, the SCMI suite; with power, then the power phases
    (on a copy of the booted state, so the SCMI ones are not disturbed)."""
    cpu, soc, why, regs = boot(elf)
    booted = None
    if power and why == "idle":
        import copy
        booted = copy.deepcopy(cpu)
    r = {"name": Path(elf).name, "why": why, "insns": cpu.insns, "se": list(soc.se.log),
         "regs": dict(regs), "i2c": [w for _, w in soc.i2c_log], "memory": memory(soc),
         "backtrace": " < ".join(cpu.func(pc) for pc in cpu.backtrace(8)), "scmi": None}
    n_i2c, n_writes = len(soc.i2c_log), len(cpu.writes)
    if questions is not None:
        r["scmi"] = [(qn, ask(cpu, soc, *qn[:3], qn[3]) if why == "idle" else None)
                     for qn, _ in questions]
    elif suite and why == "idle":
        r["scmi"] = scmi_suite(cpu, soc)
    r["i2c_scmi"] = [w for _, w in soc.i2c_log[n_i2c:]]
    r["cgc_scmi"] = [(a, v) for _, a, _, v in cpu.writes[n_writes:] if CGC[0] <= a < CGC[1]]
    r["power"] = power_suite(booted, booted.bus) if booted is not None else None
    return r


# --- power phases (--power) -----------------------------------------------------

# the hardware mutexes (0xa0000900 + 4 i) CIX's tasks take and release all
# the time; not compared during the power phases
POWER_IGNORE = [(0xa0000900, 0xa0000a90)]
SETTLE_BOOT = 3_000_000               # instructions after the handshake (CIX's pm_ready)
CHUNK, QUIET, POWER_BUDGET = 500_000, 2, 40_000_000
TFA = 2                               # TF-A's channel
STR_BOOT_CORE_NOTIFY = 0x42
PM_MSG_ATTR = 0x3f | 0x0c << 8        # scmi_send_msg_to_pm: buf_len 0x3f, msg_route 0x0c
# S3 boot script (UEFI's S3SaveStateDxe, BootScriptInternalFormat.h) at a
# DRAM address, seen by the PM through TLB entry 5 (0x60000000 + 5 << 27)
S3SCRIPT_DRAM = 0x8f000000
S3SCRIPT_HUB = 0x60000000 + (5 << 27) + (S3SCRIPT_DRAM & 0x7ffffff)
# CIX's PM messages (_portX_msg): channel, id, payload, who sends it
PM_MESSAGES = [
    (0, 0x00, (1, 2), "channel test"),
    (0, 0x01, (), "firmware version (UEFI CsuPmMsgGetFirmwareVersion)"),
    (0, 0x02, (1000,), "deferred notify (test)"),
    (0, 0x40, (3,), "cpu online (Linux)"),
    (0, 0x41, (3,), "cpu offline (Linux)"),
    (0, 0x50, (0x05040100,), "SMN read (test)"),
    (0, 0x50, (0x40000000,), "SMN read, out of range"),
    (0, 0x60, (), "platform info (UEFI options)"),
    (0, 0x61, (0x1000,), "SCMI clock reference, no such clock (UEFI)"),
    (0, 0x62, (S3SCRIPT_DRAM, 0, 0, 0), "register the S3 script (UEFI S3SaveStateDxe)"),
    (0, 0x63, (), "PMIC versions (UEFI SetupManager)"),
    (0, 0x400, (), "PMIC events (Linux)"),
    (0, 0x800, (), "fan count (Linux)"),
    (0, 0x801, (0,), "fan status (Linux)"),
    (0, 0x801, (5,), "fan status, no such fan"),
    (0, 0x804, (), "EDP voltages (Linux)"),
    (0, 0x805, (), "EDP set voltages (Linux)"),
    (0, 0x1234, (), "no such message"),
    (2, 0x01, (), "firmware version on TF-A's channel"),
    (4, 0x01, (), "firmware version on channel 4"),
]


def s3script_table():
    """A script UEFI could leave: a 32-bit write and a read-modify-write of
    a scratch register in the S5 block, then the terminator."""
    ops = bytes([0x00, 16, 2, 1]) + (0x16000804).to_bytes(8, "little") + (0x5a5a0001).to_bytes(4, "little")
    ops += bytes([0x01, 19, 2]) + (0x16000808).to_bytes(8, "little") + \
        (0x00000100).to_bytes(4, "little") + (0xffff00ff).to_bytes(4, "little")
    ops += bytes([0xff, 2])
    header = bytes([0xaa, 12]) + (1).to_bytes(2, "little") + (12 + len(ops)).to_bytes(4, "little") + bytes(4)
    return header + ops


def pm_msg(cpu, soc, chan, msg_id, args=(), budget=3_000_000):
    """Send one of CIX's PM messages as TF-A's scmi_send_msg_to_pm does:
    msgAttr (word 0) says it is one, the header has the message id, the
    answer is the status code (word 2), the length and the payload from
    word 12 of the payload on.  Returns (status, [words]) or ("error", why)."""
    base = pm_emu.MBOX + chan * 0x10000
    rd = lambda off: soc.mem.read(base + off, 4)          # noqa: E731
    wr = lambda off, v: soc.mem.write(base + off, 4, v)   # noqa: E731
    end = cpu.insns + budget
    while not rd(4) & 1:
        if cpu.insns >= end:
            return ("error", "channel never free")
        if cpu.run(20_000) is not None:
            return ("error", "stopped")
    wr(4, rd(4) & ~1)
    wr(0x18, (rd(0x18) & ~0xffff) | msg_id)
    wr(0, PM_MSG_ATTR)
    wr(0x10, 0)
    wr(0x14, 4 + 4 * len(args))
    for i, a in enumerate(args):
        wr(0x1c + 4 * i, a & 0xffffffff)
    soc.mbox[chan].ring()
    while not rd(4) & 1:
        if cpu.insns >= end:
            return ("error", "no answer")
        why = cpu.run(20_000)
        if why is not None and not str(why).startswith("waiti with nothing"):
            return ("error", f"stopped: {why}")
        if why is not None and not rd(4) & 1:
            return ("error", "no answer (the PM sleeps)")
    n = max(0, min((rd(0x14) - 4) // 4, 13))
    return (rd(8), [rd(0) & 0xff00, rd(0x18) & 0xffff] + [rd(0x4c + 4 * i) for i in range(n)])


def counted(addr):
    """A register write the power phases compare: not the shared memory,
    the PM's RAM, the hardware mutexes, nor the data words of the AP's
    request channels (their answers are compared instead)."""
    if (addr >> 16) - (pm_emu.MBOX >> 16) in CHANNELS and addr & 0xffff < 0x80:
        return False
    return not ignored(addr) and not is_memory(addr) and \
        not (pm_emu.RAM[0] <= addr < pm_emu.RAM[0] + pm_emu.RAM[1]) and \
        not any(lo <= addr < hi for lo, hi in POWER_IGNORE)


def run_until(cpu, soc, until, budget=POWER_BUDGET):
    """Run until the PM sleeps with nothing to wake it ("asleep"), writes
    the warm reset ("reset"), or (until="quiet") writes no register for
    QUIET chunks in a row; or the budget runs out."""
    end, quiet = cpu.insns + budget, 0
    if until == "reset" and ("warm reset", 1) in cpu.bus.events:
        return "reset"                  # while the answer was awaited
    cpu.stop_write = (pm_emu.S5_PMCTRL + pm_emu.S5_WARM_RST, 1)
    try:
        while cpu.insns < end:
            n = len(cpu.writes)
            why = cpu.run(CHUNK)
            if why is not None:
                if str(why).startswith("waiti with nothing"):
                    return "quiet" if until == "quiet" else "asleep"
                if str(why).startswith("write"):
                    return "reset"
                return f"stopped: {why} at {cpu.func(cpu.pc)}"
            if until == "quiet":
                quiet = 0 if any(counted(a) for _, a, _, _ in cpu.writes[n:]) else quiet + 1
                if quiet >= QUIET:
                    return "quiet"
        return "budget"
    finally:
        cpu.stop_write = None


def phase(cpu, soc, name, action):
    """Run action(); what the PM did meanwhile: registers written, I2C, SE
    handshake words, S5 events, answers."""
    n_w, n_i, n_s, n_e = len(cpu.writes), len(soc.i2c_log), len(soc.se.log), len(soc.events)
    i0 = cpu.insns
    end, answers = action()
    regs = defaultdict(list)
    for _, addr, _, value in cpu.writes[n_w:]:
        if counted(addr):
            regs[addr].append(value)
    return {"name": name, "end": end, "insns": cpu.insns - i0, "regs": dict(regs),
            "i2c": [w for _, w in soc.i2c_log[n_i:]], "se": list(soc.se.log[n_s:]),
            "events": list(soc.events[n_e:]), "answers": answers}


def power_suite(cpu, soc):
    import copy
    out = []
    cpu.run(SETTLE_BOOT)                    # CIX's pm_ready finishes after the handshake
    forked = copy.deepcopy(cpu)             # for the warm reset

    def messages():
        for i, b in enumerate(s3script_table()):
            soc.mem.write(S3SCRIPT_HUB + i, 1, b)
        ans = [((ch, mid, tuple(a)), pm_msg(cpu, soc, ch, mid, a)) for ch, mid, a, _ in PM_MESSAGES]
        return run_until(cpu, soc, "quiet", 4 * CHUNK), ans
    out.append(phase(cpu, soc, "PM messages", messages))

    def suspend():
        soc.se.suspend()                     # TF-A's FFA_SUSPEND (mbox_set_suspend) first
        ans = [("0x42", pm_msg(cpu, soc, TFA, STR_BOOT_CORE_NOTIFY, (0,)))]
        try:
            ans.append(("suspend", pm_emu.scmi(cpu, soc, 0x12, 3, (0, 4), mbox=TFA, budget=3_000_000)))
        except pm_emu.ScmiError as e:
            ans.append(("suspend", ("error", str(e))))
        return run_until(cpu, soc, "asleep"), ans
    out.append(phase(cpu, soc, "suspend", suspend))

    def resume():
        soc.se.wake()                        # the SE resumes from ROM, then the PM's wake interrupt
        soc.s5.wake()
        return run_until(cpu, soc, "quiet"), []
    out.append(phase(cpu, soc, "resume", resume))

    def again():
        ans = []
        for chan, proto, msg, args in ((TFA, 0x10, 0, ()), (TFA, 0x12, 1, ()), (0, 0x13, 8, (0,)),
                                       (0, 0x15, 1, ()), (0, 0x14, 6, (0,))):
            try:
                ans.append(((chan, proto, msg), pm_emu.scmi(cpu, soc, proto, msg, args, mbox=chan)))
            except pm_emu.ScmiError as e:
                ans.append(((chan, proto, msg), ("error", str(e))))
        ans.append(("0x42", pm_msg(cpu, soc, TFA, STR_BOOT_CORE_NOTIFY, (0,))))
        return run_until(cpu, soc, "quiet", 4 * CHUNK), ans
    out.append(phase(cpu, soc, "after resume", again))

    def shutdown():
        try:
            ans = [("shutdown", pm_emu.scmi(cpu, soc, 0x12, 3, (0, 0), mbox=TFA, budget=3_000_000))]
        except pm_emu.ScmiError as e:
            ans = [("shutdown", ("error", str(e)))]
        return run_until(cpu, soc, "quiet"), ans
    out.append(phase(cpu, soc, "shutdown", shutdown))

    cpu, soc = forked, forked.bus

    def reset():
        try:
            ans = [("warm reset", pm_emu.scmi(cpu, soc, 0x12, 3, (0, 2), mbox=TFA, budget=3_000_000))]
        except pm_emu.ScmiError as e:
            ans = [("warm reset", ("error", str(e)))]
        return run_until(cpu, soc, "reset"), ans
    out.append(phase(cpu, soc, "warm reset", reset))
    return out


def cached(elf, refresh, power=False):
    """CIX's run, from out/test_pm-cache when the image and the emulator
    are unchanged."""
    here = Path(__file__).resolve().parent
    h = hashlib.sha256(Path(elf).read_bytes())
    for f in ("pm_emu.py", "xt_emu.py", "xtensa_flix.py", "test_pm.py"):
        h.update((here / f).read_bytes())
    h.update(b"power" if power else b"")
    path = here.parent.parent / "out" / "test_pm-cache" / f"{h.hexdigest()[:16]}.pickle"
    if path.exists() and not refresh:
        return pickle.loads(path.read_bytes())
    r = run(elf, suite=True, power=power)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(pickle.dumps(r))
    return r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("open_elf", nargs="?")
    ap.add_argument("--cix")
    ap.add_argument("--scmi", action="store_true", help="then compare SCMI answers")
    ap.add_argument("--power", action="store_true",
                    help="then compare PM messages, suspend/resume, shutdown, warm reset")
    ap.add_argument("--no-cache", action="store_true", help="run CIX's image again")
    ap.add_argument("-v", action="store_true")
    a = ap.parse_args()
    cix_elf = a.cix
    if cix_elf is None:
        from cix_debug import fetch
        cix_elf = fetch(CIX_PM)
    bad = 0
    ref = cached(cix_elf, a.no_cache, a.power)
    runs = [ref] + ([run(a.open_elf, ref["scmi"] if a.scmi else None, power=a.power)] if a.open_elf else [])
    for r in runs:
        ok = r["why"] == "idle" and r["se"][-1:] == [0x5a5c]
        bad += not ok
        print(f"{'ok  ' if ok else 'FAIL'} {r['name']}: {r['why']} after {r['insns']} instructions, "
              f"{len(r['regs'])} registers written, {len(r['i2c'])} I2C transfers, "
              f"SE handshake {' '.join(f'{v:#x}' for v in r['se'])}")
        if r["why"] != "idle":
            print("     " + r["backtrace"])
    if len(runs) == 2:
        ra, rb = runs[0]["regs"], runs[1]["regs"]
        diffs = [r for r in sorted(set(ra) | set(rb)) if ra.get(r) != rb.get(r)]
        for r in diffs[:None if a.v else 40]:
            print(f"  {r:#010x}: CIX {[hex(v) for v in ra.get(r, [])][:6]} open {[hex(v) for v in rb.get(r, [])][:6]}")
        print(f"{len(set(ra) | set(rb))} registers compared, {len(diffs)} differ")
        ma, mb = runs[0]["memory"], runs[1]["memory"]
        mdiff = 0
        for page in sorted(set(ma) | set(mb)):
            pa, pb = ma.get(page, bytes(0x1000)), mb.get(page, bytes(0x1000))
            if pa != pb:
                d = [i for i in range(0x1000) if pa[i] != pb[i]]
                mdiff += 1
                if a.v or mdiff <= 20:
                    print(f"  memory {page:#010x}: {len(d)} bytes differ, first at {page + d[0]:#x}")
        print(f"{len(set(ma) | set(mb))} pages of shared memory compared, {mdiff} differ")
        bad += mdiff
        ia, ib = runs[0]["i2c"], runs[1]["i2c"]
        print(f"I2C: {'same' if ia == ib else 'differ'} ({len(ia)} transfers)")
        bad += len(diffs) + (ia != ib)
    if a.scmi:
        answers = [r["scmi"] for r in runs]
        same_n = 0
        for i, (qn, ans) in enumerate(answers[0]):
            chan, proto, msg, args = qn
            if proto is None and msg != "read":
                continue
            if proto is None:
                line = f"ch{chan} read {pm_emu.MBOX + args[0]:#x}: {fmt(ans)}"
            else:
                line = f"ch{chan} {proto:#04x}/{msg}{list(args) if args else ''}: {fmt(ans)}"
            if len(answers) == 2:
                other = answers[1][i][1]
                same = other == ans
                same_n += same
                bad += not same
                if a.v or not same:
                    print(f"{'ok  ' if same else 'FAIL'} {line}" + ("" if same else f" (open {fmt(other)})"))
            else:
                print(line)
        n = sum(qn[1] is not None or qn[2] == "read" for qn, _ in answers[0])
        if len(answers) == 2:
            print(f"{same_n} of {n} SCMI answers the same")
            for key, what in (("i2c_scmi", "I2C transfers"), ("cgc_scmi", "clock generator writes")):
                xa, xb = runs[0].get(key), runs[1].get(key)
                print(f"{what} during SCMI: {'same' if xa == xb else 'differ'} ({len(xa or [])})")
                bad += xa != xb
        else:
            print(f"{n} SCMI commands")
    if a.power:
        bad += report_power(runs, a.v)
    if len(runs) == 2 or bad:
        print(f"{bad} differences" if len(runs) == 2 else "CIX's image did not boot in the model")
    sys.exit(1 if bad else 0)


def comparable(q, ans):
    """An answer as compared: the firmware version's build date and time
    (message 1, words 3 and 4) are each image's own."""
    if isinstance(q, tuple) and len(q) == 3 and q[1] == 1 and isinstance(ans, tuple) and \
            isinstance(ans[1], list) and len(ans[1]) == 5:
        return (ans[0], ans[1][:3])
    return ans


def report_power(runs, verbose):
    """Print the power phases (one run: CIX's; two: compared).  Returns the
    number of differences."""
    bad = 0
    phases = [r.get("power") or [] for r in runs]
    if any(not p for p in phases):
        print("power phases: did not run (no idle after boot)")
        return 1
    for i, pa in enumerate(phases[0]):
        pb = phases[1][i] if len(phases) == 2 else None
        line = f"power: {pa['name']}: {pa['end']} after {pa['insns']} instructions, " \
               f"{len(pa['regs'])} registers, {len(pa['i2c'])} I2C, SE {' '.join(f'{v:#x}' for v in pa['se'])}, " \
               f"events {pa['events']}"
        if pb is None:
            print(line)
            for q, ans in pa["answers"]:
                print(f"  {q}: {fmt(ans) if isinstance(ans, tuple) and len(ans) == 2 else ans}")
            continue
        ra, rb = pa["regs"], pb["regs"]
        diffs = [r for r in sorted(set(ra) | set(rb)) if ra.get(r) != rb.get(r)]
        ans_bad = [(q, x, y) for (q, x), (_, y) in zip(pa["answers"], pb["answers"])
                   if comparable(q, x) != comparable(q, y)]
        same = {k: pa[k] == pb[k] for k in ("end", "i2c", "se", "events")}
        n = len(diffs) + len(ans_bad) + sum(not v for v in same.values())
        bad += n
        print(f"{'ok  ' if not n else 'FAIL'} {line}")
        print(f"     open: {pb['end']} after {pb['insns']} instructions; {len(set(ra) | set(rb))} registers "
              f"compared, {len(diffs)} differ; I2C {'same' if same['i2c'] else 'differ'} "
              f"({len(pa['i2c'])}/{len(pb['i2c'])}); SE {'same' if same['se'] else 'differs'}; events "
              f"{'same' if same['events'] else 'differ'}; {len(pa['answers']) - len(ans_bad)} of "
              f"{len(pa['answers'])} answers the same")
        for r in diffs[:None if verbose else 30]:
            print(f"  {r:#010x}: CIX {[hex(v) for v in ra.get(r, [])][:6]} open {[hex(v) for v in rb.get(r, [])][:6]}")
        for q, x, y in ans_bad:
            print(f"  answer {q}: CIX {x} open {y}")
        if not same["i2c"]:
            for k, (x, y) in enumerate(zip(pa["i2c"] + [None] * len(pb["i2c"]), pb["i2c"] + [None] * len(pa["i2c"]))):
                if x != y:
                    print(f"  I2C {k}: CIX {x} open {y}")
                    break
        if not same["se"] or not same["events"] or not same["end"]:
            print(f"  SE CIX {pa['se']} open {pb['se']}; events CIX {pa['events']} open {pb['events']}; "
                  f"end CIX {pa['end']} open {pb['end']}")
    return bad


def fmt(ans):
    if ans is None:
        return "-"
    st, w = ans
    if st == "error":
        return f"error ({w})"
    return f"{st} [{' '.join(f'{x:#x}' for x in w)}]"


if __name__ == "__main__":
    main()
