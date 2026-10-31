#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Differential test of the open SFH firmware against CIX's sfh_fw.bin.

  test_sfh.py OPEN_sfh_fw.bin [CIX_sfh_fw.bin]

Both images run in Unicorn (Cortex-M, Thumb) against a model of the SFH
peripherals: the mailbox FIFOs, the timer block (counters read as expired)
and the control block.  Each scenario (boot, then the AP suspend, resume
and PM wake-after messages, then the timer interrupts) is driven through
the image's own interrupt dispatcher (the handler in vector 16), and the
writes each image makes to the hardware are compared.  Debug output and
the order of independent register writes inside one step are not
compared; the values written to each register, in order per register,
are.
"""

import struct
import sys
from collections import defaultdict
from pathlib import Path

from unicorn import (UC_ARCH_ARM, UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE,
                     UC_MODE_MCLASS, UC_MODE_THUMB, Uc, UcError)
from unicorn.arm_const import UC_ARM_REG_IPSR, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_SP

ROOT = Path(__file__).resolve().parent.parent.parent
CIX = ROOT / "edk2-non-osi/Platform/CIX/Sky1/PackageTool/Firmwares/sfh_fw.bin"

SRAM, SRAM_SIZE = 0x20000000, 0x80000
PERIPH, PERIPH_SIZE = 0x50000000, 0x100000
SCS, SCS_SIZE = 0xE0000000, 0x100000
RETURN = 0x2007F000          # fake return address for injected interrupts
MBOX = {0: 0x50080000, 1: 0x50090000, 2: 0x500A0000, 3: 0x500B0000}


class Sfh:
    def __init__(self, image):
        self.uc = uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        uc.mem_map(SRAM, SRAM_SIZE)
        # Address 0 aliases the start of SRAM.  CIX's image logs through a
        # UART pointer that stays 0 once logging is on (after resume): it
        # reads the vector table as the flag register, sees "TX full" and
        # gives up after 10000 polls per character, writing nothing.
        uc.mem_map(0, 0x1000)
        uc.mem_write(0, image[:0x1000])
        uc.mem_map(PERIPH, PERIPH_SIZE)
        uc.mem_map(SCS, SCS_SIZE)
        uc.mem_write(SRAM, image)
        self.dispatch = struct.unpack_from("<I", image, 16 * 4)[0]
        self.writes = []
        self.rx = {}             # channel -> words still in its RX FIFO
        self.int_status = {}     # channel -> interrupt status
        self.idle = None
        self.timer_status = 0
        uc.hook_add(UC_HOOK_MEM_WRITE, self.on_write, begin=PERIPH, end=PERIPH + PERIPH_SIZE - 1)
        uc.hook_add(UC_HOOK_MEM_WRITE, self.on_write, begin=SCS, end=SCS + SCS_SIZE - 1)
        uc.hook_add(UC_HOOK_MEM_READ, self.on_read, begin=PERIPH, end=PERIPH + PERIPH_SIZE - 1)
        uc.hook_add(UC_HOOK_CODE, self.on_code)

    def on_write(self, uc, access, addr, size, value, user):
        self.writes.append((addr, value & 0xFFFFFFFF))

    def on_read(self, uc, access, addr, size, value, user):
        for ch, base in MBOX.items():
            if addr == base + 0xC8:
                self.poke(addr, self.int_status.get(ch, 0))
            elif addr == base + 0xB0:
                words = self.rx.get(ch, [])
                self.poke(addr, words.pop(0) if words else 0)
            elif addr == base + 0xB4:
                self.poke(addr, 0 if self.rx.get(ch) else 4)
        if 0x50010000 <= addr < 0x50018000 and addr & 0xFFF == 0x1C:
            self.poke(addr, 0)   # timer counters: already expired
        if 0x50010000 <= addr < 0x50018000 and addr & 0xFFF == 0x30:
            self.poke(addr, self.timer_status)

    def poke(self, addr, value):
        self.uc.mem_write(addr, struct.pack("<I", value))

    def on_code(self, uc, addr, size, user):
        if addr == RETURN:
            uc.emu_stop()
        # an instruction branching to itself: the idle loop
        if size == 2 and uc.mem_read(addr, 2) == b"\xfe\xe7":
            self.idle = addr
            uc.emu_stop()

    def boot(self):
        image_sp, reset = struct.unpack("<II", bytes(self.uc.mem_read(SRAM, 8)))
        self.uc.reg_write(UC_ARM_REG_SP, image_sp)
        self.run(reset)

    def irq(self, irq):
        self.uc.reg_write(UC_ARM_REG_IPSR, 16 + irq)
        self.uc.reg_write(UC_ARM_REG_SP, 0x2007E000)
        self.uc.reg_write(UC_ARM_REG_LR, RETURN | 1)
        self.run(self.dispatch)
        self.uc.reg_write(UC_ARM_REG_IPSR, 0)

    def run(self, pc):
        try:
            self.uc.emu_start(pc | 1, 0xFFFFFFFF, count=20_000_000)
        except UcError as e:
            pc = self.uc.reg_read(UC_ARM_REG_PC)
            raise SystemExit(f"emulation error at {pc:#x}: {e}")

    def step(self, fn):
        self.writes = []
        fn()
        return self.writes


def scenarios():
    def message(ch, words):
        def go(s):
            s.rx[ch] = list(words)
            s.int_status[ch] = 0x14
            s.irq(23 + ch)
            s.int_status[ch] = 0
        return go

    def timer(n):
        def go(s):
            s.timer_status = 1
            s.irq(n)
            s.timer_status = 0
        return go

    yield "boot", lambda s: s.boot()
    yield "AP: suspend, wake after 2 s", message(1, [0x0C, 0xFFF2, 2])
    yield "AP: resume", message(1, [0x0C, 0xFFF4, 0])
    yield "PM: wake after 5 ms", message(3, [0x0C, 0xFFF7, 5])
    yield "AP: unknown command", message(1, [0x0C, 0xFFF9, 0])
    yield "AP: short message", message(1, [0x08, 0xFFF2])
    for n in range(16, 22):
        yield f"timer interrupt {n}", timer(n)


def per_register(writes):
    regs = defaultdict(list)
    for addr, value in writes:
        regs[addr].append(value)
    return regs


def main():
    open_img = Path(sys.argv[1]).read_bytes()
    cix_img = Path(sys.argv[2] if len(sys.argv) > 2 else CIX).read_bytes()
    a, b = Sfh(cix_img), Sfh(open_img)
    failed = 0
    for name, fn in scenarios():
        wa, wb = a.step(lambda: fn(a)), b.step(lambda: fn(b))
        ra, rb = per_register(wa), per_register(wb)
        if ra == rb:
            order = "same order" if wa == wb else "same writes per register"
            print(f"ok   {name}: {len(wa)} writes, {order}")
            continue
        failed += 1
        print(f"FAIL {name}")
        for reg in sorted(set(ra) | set(rb)):
            if ra.get(reg) != rb.get(reg):
                print(f"     {reg:#010x}: CIX {[hex(v) for v in ra.get(reg, [])][:8]}"
                      f" open {[hex(v) for v in rb.get(reg, [])][:8]}")
    print("all scenarios match" if not failed else f"{failed} scenario(s) differ")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
