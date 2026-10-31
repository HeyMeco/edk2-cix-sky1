#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Sky1 as CSU_PM sees it, around the xt_emu interpreter.

  pm_emu.py ELF [--count N] [--trace-mmio]

runs a CSU_PM image (CIX's csupm_sky1 or ours) from its reset vector and
prints its console, the I2C traffic and where it stopped.  As se_emu.py
does for the SE, the model is only as much of the SoC as the firmware needs
to make progress: registers are plain memory unless a device below gives
them behaviour.

Devices:
  * Cadence I2C controllers (cdns,i2c-r1p14; 16-bit registers) at
    0xa0010000, 0xa0011000, 0xa0012000 (masters) and 0xa0013000 (slave),
    with I2cDevice slaves attached by 7-bit address;
  * the PL011 at 0xa0030000 (UART4 on the board), whose output is captured.
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from xt_emu import EPC1, EXCCAUSE, EXCVADDR, Xtensa  # noqa: E402

# a call to any of these is a failure (CIX: argument 0 is a trace code)
ASSERTS = ("vAssertCalledWithTRC", "vAssertCalled", "_xt_panic", "arch_panic")

# busy-waits on CCOUNT, done at once (argument: cycles)
DELAYS = ("cc_delay",)

RAM = (0x20000000, 0x110000)          # IRAM/DRAM, overlay storage at 0x20100000
RESET = 0x20000000


class Mem:
    """Sparse little-endian memory: registers nothing models."""

    def __init__(self):
        self.pages = {}

    def read(self, addr, size):
        page = self.pages.get(addr >> 12)
        if page is None:
            return 0
        o = addr & 0xfff
        return int.from_bytes(page[o:o + size], "little")

    def write(self, addr, size, value):
        page = self.pages.setdefault(addr >> 12, bytearray(0x1000 + 8))
        o = addr & 0xfff
        page[o:o + size] = (value & ((1 << 8 * size) - 1)).to_bytes(size, "little")


# --- I2C ---------------------------------------------------------------------

class I2cDevice:
    """A slave with an 8-bit register pointer: a write's first byte selects
    the register, the rest are written from there; reads continue from the
    pointer.  Most PMICs and sensors on Sky1 boards behave this way."""

    def __init__(self, name, regs=None):
        self.name = name
        self.regs = bytearray(256)
        for r, v in (regs or {}).items():
            self.regs[r] = v
        self.ptr = 0
        self.first = True

    def start(self, read):
        self.first = not read

    def write(self, byte):
        if self.first:
            self.ptr, self.first = byte, False
        else:
            self.regs[self.ptr] = byte
            self.ptr = (self.ptr + 1) & 0xff

    def read(self):
        v = self.regs[self.ptr]
        self.ptr = (self.ptr + 1) & 0xff
        return v


# Cadence I2C register offsets and bits (Linux i2c-cadence.c)
CR, SR, ADDR, DATA, ISR, XFER, SLVP, TOUT, IMR, IER, IDR = 0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40
CR_RW, CR_HOLD, CR_CLRFIFO = 0x01, 0x10, 0x40
SR_RXDV, SR_TXDV, SR_BA = 0x20, 0x40, 0x100
ISR_COMP, ISR_DATA, ISR_NACK = 0x01, 0x02, 0x04
FIFO = 16


class CadenceI2c:
    """Master side of a Cadence I2C controller.  A write of ADDR starts a
    transfer: for a write, the TX FIFO goes to the slave (and bytes written
    to DATA while the transfer is open follow it); for a read, XFER bytes
    come back through DATA.  Every transfer completes at once."""

    def __init__(self, soc, base, name):
        self.soc, self.base, self.name = soc, base, name
        self.devices = {}
        self.reset()

    def reset(self):
        self.r = {CR: 0, SR: 0, ADDR: 0, XFER: 0, IMR: 0x2ff, TOUT: 0x1f}
        self.isr = 0
        self.tx = bytearray()
        self.rx = bytearray()
        self.dev = None
        self.open = False

    def log(self, what):
        self.soc.i2c_log.append((self.name, what))

    def read(self, off, size):
        if off == DATA:
            return self.rx.pop(0) if self.rx else 0
        if off == ISR:
            return self.isr
        if off == SR:
            sr = 0
            if self.rx:
                sr |= SR_RXDV
            if self.open:
                sr |= SR_BA
            return sr
        return self.r.get(off, 0)

    def write(self, off, size, value):
        if off == ISR:
            self.isr &= ~value
            return
        if off == IER:
            self.r[IMR] &= ~value
            return
        if off == IDR:
            self.r[IMR] |= value
            return
        if off == DATA:
            if self.open and self.dev is not None and not self.reading:
                self.dev.write(value & 0xff)
                self.sent.append(value & 0xff)
                self.isr |= ISR_COMP
            else:
                self.tx.append(value & 0xff)
            return
        if off == CR:
            if value & CR_CLRFIFO:
                self.tx.clear()
                self.rx.clear()
                value &= ~CR_CLRFIFO
            self.r[CR] = value
            if not value & CR_HOLD:
                self.finish()
            return
        self.r[off] = value
        if off == ADDR:
            self.transfer(value & 0x7f)

    def finish(self):
        if self.open and self.dev is not None:
            kind = "R" if self.reading else "W"
            data = self.got if self.reading else self.sent
            self.log(f"{self.dev.name} {kind} {data.hex(' ')}")
        self.open = False

    def transfer(self, addr):
        self.finish()
        self.addr = addr
        self.dev = self.devices.get(addr)
        if self.dev is None and self.soc.auto_i2c:
            self.dev = self.devices[addr] = I2cDevice(f"{self.name}:{addr:#04x}")
        if self.dev is None:
            cpu = self.soc.cpu
            where = " < ".join(cpu.func(pc) for pc in cpu.backtrace(6)) if cpu else ""
            self.log(f"@{addr:#04x} NACK  {where}")
            self.isr |= ISR_NACK
            self.tx.clear()
            return
        read = self.reading = bool(self.r[CR] & CR_RW)
        self.dev.start(read)
        self.open = True
        if read:
            self.got = bytes(self.dev.read() for _ in range(self.r[XFER] or 1))
            self.rx = bytearray(self.got)
            self.r[XFER] = 0                            # everything received
            self.isr |= ISR_COMP | ISR_DATA
        else:
            self.sent = bytearray()
            for b in self.tx:
                self.dev.write(b)
                self.sent.append(b)
            self.tx.clear()
            self.isr |= ISR_COMP
        if not self.r[CR] & CR_HOLD:
            self.finish()


# --- PL011 ---------------------------------------------------------------------

class Pl011:
    def __init__(self, soc, base):
        self.soc, self.base = soc, base
        self.r = {}

    def read(self, off, size):
        if off == 0x18:
            return 0x90                                 # FR: TX empty, RX empty
        return self.r.get(off, 0)

    def write(self, off, size, value):
        if off == 0:
            self.soc.console.append(value & 0xff)
        self.r[off] = value


# --- clock generator ---------------------------------------------------------------

CGC = 0xa0060000                      # the SE's 0x46570000, SoC 0x06570000
PLLS = ([CGC + 0x100 * i for i in range(7)] + [CGC + 0x1000 + 0x100 * i for i in range(5)] +
        [CGC + 0x2000, CGC + 0x2100, CGC + 0x3000, CGC + 0x3100, CGC + 0x4000] +
        [CGC + 0x5000 + 0x100 * i for i in range(4)] + [CGC + 0x6000, CGC + 0x7000])
# PLL registers as CSU_PM reads them (pll_ctrl_*): +0x00 [11:0] feedback
# divider, +0x04 [23:0] fraction and bit 24 output enable, +0x08 [2:0] and
# [6:4] post dividers, [13:8] reference divider, +0x0c bit 4 locked.
# FoutVCO = 24 MHz * (fbdiv + frac / 2^24) / refdiv, which CIX's firmware
# expects between 1.6 and 2.1 GHz.  The reset values are not known (a dump
# of SoC 0x06570000 from a running board would tell); these give 2 GHz.
PLL_RESET = {0x00: 83, 0x04: 0x1555555, 0x08: 0x111, 0x0c: 0x10}


def cgc_reset(mem):
    for base in PLLS:
        for off, v in PLL_RESET.items():
            mem.write(base + off, 4, v)


PLL_LOCK = {base + 0x0c for base in PLLS}


# --- the SE, as far as CSU_PM sees it ------------------------------------------

PM_MAGIC = 0xa0100090                 # the SE's 0x46610090 (firmware/se/service/main.c)


class SePeer:
    """The SE's side of the boot handshake: the PM announces itself with
    0x5c5a, the SE clears it, finishes its SoC init and writes 0x55ca; the
    PM answers 0x5a5c, which the SE clears before it loads BL2."""

    def __init__(self, soc):
        self.soc = soc
        self.log = []

    def pm_wrote(self, addr, value):
        if addr == PM_MAGIC:
            self.log.append(value)
            if value == 0x5c5a:
                return 0x55ca
            if value == 0x5a5c:
                return 0
        return value


# --- mailboxes and the interrupt aggregator ----------------------------------------

MBOX = 0xa0080000                     # mailbox i at MBOX + i * 0x10000 (SoC 0x06590000 + ...)
MBOX_N = 10
MBOX_DOORBELL, MBOX_TO_AP_EN, MBOX_TO_PM_EN, MBOX_CLEAR, MBOX_STATUS = 0x80, 0xbc, 0xc0, 0xc4, 0xc8
# Mailbox registers as CSU_PM uses them (dev_mbox_*): 32 data words (the
# SCMI shared memory, in TF-A's layout), +0x80 doorbell (the sender writes
# it), +0x84..+0xa8 identification and fast channels, +0xbc/+0xc0 which
# status bits interrupt the AP / the PM, +0xc4 clear, +0xc8 status.
# Even mailboxes carry AP -> PM requests, odd ones PM -> AP; TF-A's SCMI
# channel is SoC 0x065b0000, mailbox 2.

AGG = 0xa0000500                      # line L (12..30): +4*(L-12) raw, +0x80 enable
MBOX_LINE = 25                        # bit i: mailbox i


class Mailbox:
    def __init__(self, soc, i):
        self.soc, self.i, self.base = soc, i, MBOX + i * 0x10000
        self.status = 0
        self.rang = []                                  # PM -> AP doorbells

    def read(self, off, size):
        if off == MBOX_STATUS:
            return self.status
        return self.soc.mem.read(self.base + off, size)

    def write(self, off, size, value):
        if off == MBOX_CLEAR:
            self.status &= ~value
            self.soc.update_irqs()
            return
        if off == MBOX_DOORBELL:
            self.rang.append(value)
        if self.base + off == PM_MAGIC:
            value = self.soc.se.pm_wrote(self.base + off, value)
        self.soc.mem.write(self.base + off, size, value)
        if off in (MBOX_TO_PM_EN, MBOX_TO_AP_EN):
            self.soc.update_irqs()

    def ring(self, bits=1):
        """The AP writes the doorbell."""
        self.status |= bits
        self.soc.update_irqs()

    def pending(self):
        return self.status & self.soc.mem.read(self.base + MBOX_TO_PM_EN, 4)


# --- what the SE leaves for the PM ------------------------------------------------

# Written by CIX's SE firmware before it starts the PM, as se_emu.py runs it
# on the Orion O6 image with blank eFuses (test_se.py): the PM strap
# (drv_memcfg_set_pm_strap_info: checksum, board, V/F version, DVFS mode,
# PVT average), the harvest word (cores and blocks off, boot core) and the
# CPU frequency mode.
SE_HANDOFF = {
    0xa0000800: 0x020fffef,           # PM strap word 0 (the SE's 0x46510800)
    0xa0000804: 0x00000000,           # PM strap word 1
    0x56000508: 0x0000a000,           # S5_SW_HARVEST
    0x5600050c: 0x00000000,           # S5_SW_CPU_FREQ
}


# --- the SoC -----------------------------------------------------------------------

class PmSoc:
    """Bus for xt_emu: devices by 4 KiB block, plain memory elsewhere."""

    def __init__(self, auto_i2c=True):
        self.auto_i2c = auto_i2c                        # unknown I2C addresses answer as register files
        self.mem = Mem()
        self.console = bytearray()
        self.i2c_log = []
        self.blocks = {}
        self.touched = {}                               # block -> first function
        self.cpu = None
        self.i2c = [CadenceI2c(self, 0xa0010000 + 0x1000 * i, f"i2c{i}") for i in range(3)]
        for dev in self.i2c:
            self.blocks[dev.base] = dev
        self.blocks[0xa0030000] = Pl011(self, 0xa0030000)
        self.se = SePeer(self)
        self.mbox = [Mailbox(self, i) for i in range(MBOX_N)]
        for mb in self.mbox:
            self.blocks[mb.base] = mb
        cgc_reset(self.mem)
        for addr, v in SE_HANDOFF.items():
            self.mem.write(addr, 4, v)

    def agg_raw(self, line):
        if line == MBOX_LINE:
            return sum(1 << mb.i for mb in self.mbox if mb.pending())
        return 0

    def update_irqs(self):
        if self.cpu is None:
            return
        lines = 0
        for line in (MBOX_LINE,):
            if self.agg_raw(line) & self.mem.read(AGG + 0x80 + 4 * (line - 12), 4):
                lines |= 1 << line
        self.cpu.int_lines = lines

    def note(self, addr):
        b = addr & ~0xfff
        if b not in self.touched:
            self.touched[b] = self.cpu.func(self.cpu.pc) if self.cpu else "?"

    def read(self, addr, size):
        self.note(addr)
        dev = self.blocks.get(addr & ~0xfff)
        if dev is not None:
            return dev.read(addr & 0xfff, size)
        if addr in PLL_LOCK:
            return self.mem.read(addr, size) | 0x10     # PLLs lock at once
        if AGG <= addr < AGG + 0x4c:
            return self.agg_raw(12 + (addr - AGG) // 4)
        return self.mem.read(addr, size)

    def write(self, addr, size, value):
        self.note(addr)
        dev = self.blocks.get(addr & ~0xfff)
        if dev is not None:
            dev.write(addr & 0xfff, size, value)
            return
        self.mem.write(addr, size, value)
        if AGG + 0x80 <= addr < AGG + 0xcc:
            self.update_irqs()


# --- the AP's side of SCMI ---------------------------------------------------------

SMT_STATUS, SMT_FLAGS, SMT_LEN, SMT_HEADER, SMT_PAYLOAD = 0x04, 0x10, 0x14, 0x18, 0x1c
SMT_FREE, SMT_ERROR = 1, 2


class ScmiError(Exception):
    pass


def scmi(cpu, soc, protocol, msg, args=(), mbox=2, token=0, budget=5_000_000, chunk=20_000):
    """Send one SCMI command the way TF-A does (drivers/arm/css/scmi with
    sky1_bl31_setup.c's channel): wait for the channel to be free, write
    header and payload into the shared memory, ring the doorbell, run the
    PM until it frees the channel.  Returns (status, [payload words])."""
    base = MBOX + mbox * 0x10000
    rd = lambda off: soc.mem.read(base + off, 4)          # noqa: E731
    wr = lambda off, v: soc.mem.write(base + off, 4, v)   # noqa: E731
    end = cpu.insns + budget
    while not rd(SMT_STATUS) & SMT_FREE:
        if cpu.insns >= end:
            raise ScmiError(f"channel {mbox} never free")
        why = cpu.run(chunk)
        if why is not None:
            raise ScmiError(f"stopped: {why}")
    header = (msg & 0xff) | (protocol & 0xff) << 10 | (token & 0x3ff) << 18
    wr(SMT_STATUS, rd(SMT_STATUS) & ~SMT_FREE)
    wr(SMT_HEADER, header)
    for i, a in enumerate(args):
        wr(SMT_PAYLOAD + 4 * i, a & 0xffffffff)
    wr(SMT_LEN, 4 + 4 * len(args))
    wr(SMT_FLAGS, 0)
    soc.mbox[mbox].ring()
    while not rd(SMT_STATUS) & SMT_FREE:
        if cpu.insns >= end:
            raise ScmiError(f"no answer to protocol {protocol:#x} message {msg}")
        why = cpu.run(chunk)
        if why is not None:
            raise ScmiError(f"stopped: {why} at {cpu.func(cpu.pc)}")
    if rd(SMT_STATUS) & SMT_ERROR:
        raise ScmiError("channel error")
    n = (rd(SMT_LEN) - 4) // 4
    words = [rd(SMT_PAYLOAD + 4 * i) for i in range(max(0, min(n, 25)))]
    status = words[0] - (1 << 32) if words and words[0] >> 31 else (words[0] if words else None)
    return status, words[1:]


def boot(elf, soc=None):
    soc = soc or PmSoc()
    cpu = Xtensa(soc)
    soc.cpu = cpu
    cpu.add_ram(*RAM)
    cpu.load_elf(elf)
    for name in DELAYS:
        if name in cpu.byname:
            cpu.stubs[cpu.addr(name)] = lambda c: c.skip(c.arg(0))
    cpu.pc = RESET
    return cpu, soc


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("elf")
    ap.add_argument("--count", type=int, default=5_000_000)
    ap.add_argument("--until", metavar="SYMBOL", help="stop when SYMBOL is reached")
    args = ap.parse_args()
    cpu, soc = boot(args.elf)
    for name in ASSERTS:
        if name in cpu.byname:
            cpu.stops[cpu.addr(name)] = name
    if args.until:
        cpu.stops[cpu.addr(args.until)] = args.until
    why = cpu.run(args.count)
    print(f"stopped: {why} after {cpu.insns} instructions "
          f"({cpu.ccount / 1e6:.1f} M cycles), in")
    for pc in cpu.backtrace():
        print(f"  {cpu.func(pc)}")
    if "panic" in cpu.func(cpu.pc):
        epc = cpu.sr.get(EPC1, 0)
        print(f"exception: cause {cpu.sr.get(EXCCAUSE, 0)} at {epc:#x} {cpu.func(epc)}, "
              f"vaddr {cpu.sr.get(EXCVADDR, 0):#x}")
    print("console:")
    print(soc.console.decode("latin-1"))
    print("i2c:")
    for name, what in soc.i2c_log[-40:]:
        print(f"  {name} {what}")
    print("handshake:", " ".join(f"{v:#x}" for v in soc.se.log))
    print("blocks:")
    for b, f in soc.touched.items():
        print(f"  {b:#010x} first in {f}")


if __name__ == "__main__":
    main()
