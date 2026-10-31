#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Differential test of the open CSU_PM firmware against CIX's.

  test_pm.py [OPEN.elf] [--cix CIX.elf] [--scmi] [--no-cache] [-v]

Each image runs in pm_emu.py on the same Sky1 model from reset until it
idles with the SE handshake done (0x5c5a, 0x55ca, 0x5a5c), without an
assertion.  Compared:

* register writes, per register in order (UART and I2C controllers
  excepted: the I2C transfers are compared instead);
* the final contents of the shared memory it writes (the hub window at
  0x60000000: PCC tables, logs);
* with --scmi, the answers to the same SCMI commands on every channel
  (base protocol discovery, then each protocol's attributes and its domains).

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
# shared memory, compared by its final contents: the hub window
# (CIX's _memmap_mem_sram_hub, DRAM and SRAM seen through a window)
MEMORY = [(0x60000000, 0xa0000000, "hub window")]


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
    return out


def run(elf, questions=None, suite=False):
    """Boot an image; ask the questions ([(question, answer)] from CIX's
    run) or, with suite, the SCMI suite."""
    cpu, soc, why, regs = boot(elf)
    r = {"name": Path(elf).name, "why": why, "insns": cpu.insns, "se": list(soc.se.log),
         "regs": dict(regs), "i2c": [w for _, w in soc.i2c_log], "memory": memory(soc),
         "backtrace": " < ".join(cpu.func(pc) for pc in cpu.backtrace(8)), "scmi": None}
    if questions is not None:
        r["scmi"] = [(qn, ask(cpu, soc, *qn[:3], qn[3]) if why == "idle" else None)
                     for qn, _ in questions]
    elif suite and why == "idle":
        r["scmi"] = scmi_suite(cpu, soc)
    return r


def cached(elf, refresh):
    """CIX's run, from out/test_pm-cache when the image and the emulator
    are unchanged."""
    here = Path(__file__).resolve().parent
    h = hashlib.sha256(Path(elf).read_bytes())
    for f in ("pm_emu.py", "xt_emu.py", "xtensa_flix.py", "test_pm.py"):
        h.update((here / f).read_bytes())
    path = here.parent.parent / "out" / "test_pm-cache" / f"{h.hexdigest()[:16]}.pickle"
    if path.exists() and not refresh:
        return pickle.loads(path.read_bytes())
    r = run(elf, suite=True)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(pickle.dumps(r))
    return r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("open_elf", nargs="?")
    ap.add_argument("--cix")
    ap.add_argument("--scmi", action="store_true", help="then compare SCMI answers")
    ap.add_argument("--no-cache", action="store_true", help="run CIX's image again")
    ap.add_argument("-v", action="store_true")
    a = ap.parse_args()
    cix_elf = a.cix
    if cix_elf is None:
        from cix_debug import fetch
        cix_elf = fetch(CIX_PM)
    bad = 0
    ref = cached(cix_elf, a.no_cache)
    runs = [ref] + ([run(a.open_elf, ref["scmi"] if a.scmi else None)] if a.open_elf else [])
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
        if len(answers) == 2:
            print(f"{same_n} of {len(answers[0])} SCMI answers the same")
        else:
            print(f"{len(answers[0])} SCMI commands")
    if len(runs) == 2 or bad:
        print(f"{bad} differences" if len(runs) == 2 else "CIX's image did not boot in the model")
    sys.exit(1 if bad else 0)


def fmt(ans):
    if ans is None:
        return "-"
    st, w = ans
    if st == "error":
        return f"error ({w})"
    return f"{st} [{' '.join(f'{x:#x}' for x in w)}]"


if __name__ == "__main__":
    main()
