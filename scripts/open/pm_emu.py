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
  * the PL011 at 0xa0030000 (UART4 on the board), whose output is captured;
  * the clock generator's PLLs (they lock at once), the mailboxes and the
    interrupt aggregator, the SE's side of the boot handshake and of
    suspend/resume (SePeer, after firmware/se/service/str.c);
  * for the power sequence (modelled, not known): the S5 power controller
    (sleep type, the PM's wake interrupt on aggregator line 18, warm
    reset), the DSU's and cores' power policy units (status follows the
    policy), the DDR controllers' low-power command/state handshake.
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from xt_emu import EPC1, EXCCAUSE, EXCVADDR, Xtensa  # noqa: E402

# a call to any of these is a failure (CIX: argument 0 is a trace code)
ASSERTS = ("vAssertCalledWithTRC", "vAssertCalled", "_xt_panic", "arch_panic")

# busy-waits on CCOUNT, done at once (argument: cycles)
DELAYS = ("cc_delay", "udelay_cycles")      # CIX's, ours

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


class Mp5475(I2cDevice):
    """An MP5475 (four bucks, VOUT at 0x13 + 8 * buck): a register file
    whose fault register 09 clears the bits written to it (CIX's firmware
    writes 0xff after reading 07-09).  Modelled on what the firmware does
    with it, not on a datasheet; a test raises a fault by setting 07-09."""

    def write(self, byte):
        if not self.first and self.ptr == 0x09:
            self.regs[0x09] &= ~byte & 0xff
            self.ptr = (self.ptr + 1) & 0xff
            return
        super().write(byte)


class Mp2845(I2cDevice):
    """An MP2845 as CIX's dev_paged_pmic.c uses it (PMBus-like): register 0
    selects the page (a buck), VOUT_COMMAND (0x62, two bytes, 5 mV steps in
    bits 9:0) and READ_VOUT (0xa7, the same) are per page, 0x83 the
    revision; the vendor registers the PSI setting writes (0x5f, 0x64,
    0x73) are kept per page too.  Modelled, not known."""

    PAGED = (0x5f, 0x62, 0x64, 0x73, 0xa7)

    def __init__(self, name, revision=0):
        super().__init__(name)
        self.page = 0
        self.paged = {}
        self.regs[0x83], self.regs[0x84] = revision & 0xff, revision >> 8
        self.n = 0

    def start(self, read):
        super().start(read)
        self.n = 0

    def write(self, byte):
        if self.first:
            self.ptr, self.first, self.n = byte, False, 0
            return
        if self.ptr == 0x00:
            self.page = byte & 3
            return
        if self.ptr in self.PAGED:
            r = self.paged.setdefault((self.page, self.ptr), bytearray(3))
            if self.n < 3:
                r[self.n] = byte
            if self.ptr == 0x62 and self.n == 1:
                self.paged[(self.page, 0xa7)] = bytearray(r)
            self.n += 1
            return
        super().write(byte)

    def read(self):
        if self.ptr in self.PAGED:
            r = self.paged.get((self.page, self.ptr), bytearray(3))
            v = r[self.n] if self.n < 3 else 0
            self.n += 1
            return v
        return super().read()


class Tmp75(I2cDevice):
    """A TI TMP75 temperature sensor: register 0 the temperature (whole
    degrees, then sixteenths in the high nibble), as the MS-R1's board
    sensors at 0x4d on I2C 3.  temp is set by a test."""

    def __init__(self, name, temp=25.0):
        super().__init__(name)
        self.set(temp)

    def set(self, temp):
        whole = int(temp) & 0xff
        self.regs[0], self.regs[1] = whole, int((temp - int(temp)) * 16) << 4 & 0xf0
        self.regs[0x23] = self.regs[0]


def auto_device(name, addr):
    """The model for an address nobody attached: Sky1 boards' PMICs by
    their address, a register file otherwise."""
    if addr == 0x60:
        return Mp5475(name)
    if addr == 0x45:
        return Mp2845(name)
    return I2cDevice(name)


# Cadence I2C register offsets and bits (Linux i2c-cadence.c)
CR, SR, ADDR, DATA, ISR, XFER, SLVP, TOUT, IMR, IER, IDR = 0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40
CR_RW, CR_MS, CR_HOLD, CR_CLRFIFO = 0x01, 0x02, 0x10, 0x40
SR_RXDV, SR_TXDV, SR_BA = 0x20, 0x40, 0x100
ISR_COMP, ISR_DATA, ISR_NACK = 0x01, 0x02, 0x04
FIFO = 16


class CadenceI2c:
    """Master side of a Cadence I2C controller.  A write of ADDR starts a
    transfer: for a write, the TX FIFO goes to the slave (and bytes written
    to DATA while the transfer is open follow it); for a read, XFER bytes
    come back through DATA.  Every transfer completes at once.  The
    interrupt status bits not masked (IER/IDR) drive the controller's
    source on aggregator line 24.

    fault (set by a test): "stuck", a bus held low (no transfer ever
    starts or ends, the bus stays busy), or "nack", every address NACKed."""

    def __init__(self, soc, base, name):
        self.soc, self.base, self.name = soc, base, name
        self.devices = {}
        self.fault = None
        self.reset()

    def irq(self):
        return bool(self.isr & ~self.r[IMR] & 0x2ff)

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
            if self.open or self.fault == "stuck":
                sr |= SR_BA
            return sr
        return self.r.get(off, 0)

    def write(self, off, size, value):
        self._write(off, size, value)
        self.soc.update_irqs()

    def _write(self, off, size, value):
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
        if off == ADDR and self.r[CR] & CR_MS:          # a slave's own address starts nothing
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
        if self.fault == "stuck":
            self.log(f"@{addr:#04x} STUCK")
            self.dev = None
            self.tx.clear()
            return
        if self.fault == "nack":
            self.log(f"@{addr:#04x} NACK (fault)")
            self.dev = None
            self.isr |= ISR_NACK
            self.tx.clear()
            return
        self.dev = self.devices.get(addr)
        if self.dev is None and self.soc.auto_i2c:
            self.dev = self.devices[addr] = auto_device(f"{self.name}:{addr:#04x}", addr)
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


PM_FAST_CHANNEL = 0xa010008c         # mailbox 8 fast channel 0, the SE's 0x4661008c

# S5 registers the SE writes around suspend (firmware/se/service/str.c)
S5_SW_SUSPEND, S5_SW_STR, S5_SW_REG6, S5_PM_RUN = 0x5600020c, 0x56000228, 0x56000518, 0x56000704


class SePeer:
    """The SE's side of the boot handshake: the PM announces itself with
    0x5c5a, the SE clears it, finishes its SoC init and writes 0x55ca; the
    PM answers 0x5a5c, which the SE clears before it loads BL2.

    Suspend to RAM (firmware/se/service/str.c): the AP's FFA_SUSPEND makes
    the SE's idle loop flush the system cache, mark the S5 registers and
    write 2 into the PM's fast channel (suspend); here that happens at once
    (suspend()).  On wake-up the SE resumes, writes 1 into the fast channel
    (wake()) and waits for the PM's 0xaacc (start the sensor fusion hub:
    the SE answers 0xccaa) or 0xaabb (stop it: the SE writes 0)."""

    def __init__(self, soc):
        self.soc = soc
        self.log = []

    def pm_wrote(self, addr, value):
        if addr == PM_MAGIC:
            self.log.append(value)
            if value == 0x5c5a:
                return 0x55ca
            if value in (0x5a5c, 0xaabb):
                return 0
            if value == 0xaacc:
                return 0xccaa
        return value

    def suspend(self):
        mem = self.soc.mem
        mem.write(S5_SW_SUSPEND, 4, mem.read(S5_SW_SUSPEND, 4) | 1)
        mem.write(S5_SW_STR, 4, 1)
        mem.write(S5_SW_REG6, 4, mem.read(S5_SW_REG6, 4) | 1)
        mem.write(PM_FAST_CHANNEL, 4, 2)

    def wake(self):
        self.soc.mem.write(S5_PM_RUN, 4, 0)
        self.soc.mem.write(PM_FAST_CHANNEL, 4, 1)


# --- S5 power controller, power policy units, DDR controllers ------------------------

S5_PMCTRL = 0x56000000                # CIX's S5 register block (mclp_retention.c's layout)
S5_SLP_TYP, S5_CSU_PM_INTR, S5_CSU_PM_INTR_EN, S5_WARM_RST = 0x100, 0x220, 0x22c, 0x300
S5_LINE = 18                          # aggregator line 18, source 0: the S5 controller's PM interrupt


class S5PmCtrl:
    """The S5 power controller as the PM uses it: a sleep type (0x100: 1
    suspend, 2 shutdown) after which the S0 domain powers down, the PM's
    wake interrupt (status 0x220, enable 0x22c, aggregator line 18; the PM
    clears it by writing 0x220) and the warm reset (0x300).  The SoC's
    response is only modelled: wake() raises the interrupt as a wake-up
    source would; sleep and reset requests are recorded in events."""

    def __init__(self, soc):
        self.soc = soc
        self.base = S5_PMCTRL

    def read(self, off, size):
        return self.soc.mem.read(self.base + off, size)

    def write(self, off, size, value):
        self.soc.mem.write(self.base + off, size, value)
        if off == S5_SLP_TYP and value:
            self.soc.events.append(("sleep", value))
        elif off == S5_WARM_RST and value & 1:
            self.soc.events.append(("warm reset", value))
        if off in (S5_CSU_PM_INTR, S5_CSU_PM_INTR_EN):
            self.soc.update_irqs()

    def wake(self):
        self.soc.mem.write(self.base + S5_CSU_PM_INTR, 4, 1)
        self.soc.events.append(("wake", 1))
        self.soc.update_irqs()

    def pending(self):
        return self.soc.mem.read(self.base + S5_CSU_PM_INTR, 4) & 1


def ppu_status(addr):
    """Arm PPUs of the DSU (0x4f030000) and the cores (0x4f080000 + core <<
    20): the power status (+8) follows the policy (+0) at once (modelled;
    CIX's power_up_dsu_and_bsp and wait_for_bsp_and_dsu_idle poll it)."""
    if addr & 0xffff != 8 or addr >> 24 != 0x4f:
        return False
    if addr == 0x4f030008:
        return True
    return (addr >> 16) & 0xf == 8 and (addr >> 20) & 0xf < 12


# Cadence (Denali) DDR controllers at 0x4c010000 + 0x20000 * channel: CIX's
# mclp_swif sends a low-power command in LP_CMD (+0x2bc [22:16]) and polls
# LP_STATE (+0x2f8 [14:8], bit 14: the last command completed) for the state
# _s_lp_SwCmdStMap gives the command.  Modelled: the state changes at once.
DDRC = [0x4c010000 + 0x20000 * ch for ch in range(4)]
DDRC_LP_CMD, DDRC_LP_STATE = 0x2bc, 0x2f8
LP_CMD_STATE = {2: 0, 3: 1, 0x23: 2, 5: 3, 0x25: 4, 9: 11, 0x29: 12, 0x11: 13, 0x31: 14, 0x51: 15}


class Ddrc:
    def __init__(self, soc, base):
        self.soc, self.base = soc, base
        self.lp_state = 0x40                            # idle, the last command completed

    def read(self, off, size):
        v = self.soc.mem.read(self.base + off, size)
        if off == DDRC_LP_STATE and size == 4:
            v = (v & ~0x7f00) | self.lp_state << 8
        return v

    def write(self, off, size, value):
        self.soc.mem.write(self.base + off, size, value)
        if off == DDRC_LP_CMD and size == 4:
            self.lp_state = 0x40 | LP_CMD_STATE.get((value >> 16) & 0x7f, 0)


# --- the PM's local watchdog -------------------------------------------------------

WDT = 0xa0050000                      # an Arm SP805 (CIX's dev_wdt.c): the SE's 0x46560000
WDT_LOCK, WDT_UNLOCK = 0xc00, 0x1acce551


class Sp805:
    """The SP805's lock (0xc00: writing 0x1acce551 unlocks, anything else
    locks; reads 1 while locked), which CIX's dev_wdt_set_timeout checks.
    The count itself is not modelled (the boards' 5 s timeout is longer
    than any test runs)."""

    def __init__(self, soc, base):
        self.soc, self.base = soc, base
        self.locked = 1

    def read(self, off, size):
        if off == WDT_LOCK:
            return self.locked
        return self.soc.mem.read(self.base + off, size)

    def write(self, off, size, value):
        if off == WDT_LOCK:
            self.locked = int(value != WDT_UNLOCK)
            return
        self.soc.mem.write(self.base + off, size, value)


# --- counters (fans' PWM and tachometers) and PVT sensors ----------------------------

# CIX's dev_counters.c: three blocks of eight counters (0x1000 apart): 0 and
# 1 the tachometers (aggregator lines 22 and 23), 2 the PWM outputs.
CNTS = (0xa0040000, 0xa0048000, 0x44110000)
CNTS_LINES = (22, 23)
CNT_TICK = 16          # CCOUNT cycles per 25 MHz counter tick


class Counter:
    """A counter node as CIX's fans use it (modelled on dev_counters.c, not
    known).  A tachometer is a pair of nodes: the even one counts the fan's
    pulses (+0x18), the odd one's load (+0x00, 25 MHz ticks) is the
    monitoring interval; started (+0x24 bit 17), it raises its block's line
    every interval, source 13 for the pair 4/5 and 14 for 6/7, until the
    firmware writes 64 to the odd node's +0x34.  A PWM node has its period
    (+0x2c) and high time (+0x28).  The fan on PWM 2:3+k and tachometer
    k >> 1:4+2(k & 1) turns at fan_max_rpm times the duty (or what a test
    sets in soc.fan_rpm[k]), with fan_pulses pulses a turn."""

    def __init__(self, soc, base, inst, node):
        self.soc, self.base, self.inst, self.node = soc, base, inst, node

    def read(self, off, size):
        return self.soc.mem.read(self.base + off, size)

    def write(self, off, size, value):
        self.soc.mem.write(self.base + off, size, value)
        if self.inst < 2 and self.node in (5, 7):
            if off == 0x24:
                self.soc.tach_control(self, value)
            elif off == 0x34 and value & 64:
                self.soc.irq_raw[CNTS_LINES[self.inst]] = \
                    self.soc.irq_raw.get(CNTS_LINES[self.inst], 0) & ~(1 << (13 + (self.node - 5) // 2))
                self.soc.update_irqs()


# PVT sensors (src/pvt.c's layout): sensor i at PVT_BASE + 0x1000 (i + 1)
PVT_BASE, PVT_N = 0xa0070000, 13
PVT_SAFE_LINE, PVT_WARN_LINE = 28, 29


def pvt_code(celsius):
    """A temperature as the sensor's 10-bit code (CIX's
    convert_temperature_to_data)."""
    return int(celsius * celsius * 6.8504e-4 + celsius * 3.584 + 223.3) & 0x3ff


class PvtSensor:
    """A PVT sensor whose reading (+0x04: three 10-bit codes, max, min and
    latest) follows soc.pvt_temp[i] (degrees C) once a test sets it, and
    which raises line 29 (its bit) while the code is at or above its warning
    threshold (+0x0c [9:0], when set) and line 28 below its safe one
    (+0x0c [19:10]).  Without a temperature it is plain memory."""

    def __init__(self, soc, i):
        self.soc, self.i, self.base = soc, i, PVT_BASE + 0x1000 * (i + 1)

    def read(self, off, size):
        t = self.soc.pvt_temp[self.i]
        if off == 0x04 and t is not None:
            c = pvt_code(t)
            return c | c << 10 | c << 20
        return self.soc.mem.read(self.base + off, size)

    def write(self, off, size, value):
        self.soc.mem.write(self.base + off, size, value)
        if off in (0x00, 0x0c):
            self.soc.pvt_update()


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
I2C_LINE = 24                         # bit i: I2C controller i


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
        # 0-2 masters (PMICs); 3 the slave the EC reads, a master for the
        # MS-R1's board sensors (its slave side is not modelled)
        self.i2c = [CadenceI2c(self, 0xa0010000 + 0x1000 * i, f"i2c{i}") for i in range(4)]
        for dev in self.i2c:
            self.blocks[dev.base] = dev
        self.blocks[0xa0030000] = Pl011(self, 0xa0030000)
        self.se = SePeer(self)
        self.mbox = [Mailbox(self, i) for i in range(MBOX_N)]
        for mb in self.mbox:
            self.blocks[mb.base] = mb
        self.events = []                                # sleep, wake, warm reset
        self.s5 = S5PmCtrl(self)
        self.blocks[S5_PMCTRL] = self.s5
        for base in DDRC:
            self.blocks[base] = Ddrc(self, base)
        self.blocks[WDT] = Sp805(self, WDT)
        for inst, base in enumerate(CNTS):
            for node in range(8):
                self.blocks[base + 0x1000 * node] = Counter(self, base + 0x1000 * node, inst, node)
        self.tachs = {}                                 # (inst, odd node) -> next interrupt, period
        self.fan_max_rpm, self.fan_pulses, self.fan_rpm = 6000, 2, {}
        self.pvt_temp = [None] * PVT_N
        for i in range(PVT_N):
            self.blocks[PVT_BASE + 0x1000 * (i + 1)] = PvtSensor(self, i)
        # aggregator lines' sources raised by models and tests: line -> bits
        self.irq_raw = {}
        cgc_reset(self.mem)
        for addr, v in SE_HANDOFF.items():
            self.mem.write(addr, 4, v)

    def agg_raw(self, line):
        if line == MBOX_LINE:
            return sum(1 << mb.i for mb in self.mbox if mb.pending())
        if line == I2C_LINE:
            return sum(1 << i for i, c in enumerate(self.i2c) if c.irq()) | self.irq_raw.get(line, 0)
        if line == S5_LINE:
            return self.s5.pending()
        return self.irq_raw.get(line, 0)

    # --- timed models: tachometers --------------------------------------------------

    def tach_control(self, cnt, value):
        key = (cnt.inst, cnt.node)
        if value & 0x20000:
            period = max(1, self.mem.read(cnt.base, 4)) * CNT_TICK
            now = self.cpu.ccount if self.cpu else 0
            self.tachs[key] = [now + period, period]
        else:
            self.tachs.pop(key, None)

    def fan_speed(self, k):
        if k in self.fan_rpm:
            return self.fan_rpm[k]
        pwm = CNTS[2] + 0x1000 * (3 + k)
        period, high = self.mem.read(pwm + 0x2c, 4), self.mem.read(pwm + 0x28, 4)
        return self.fan_max_rpm * min(high, period) // period if period else 0

    def tach_fire(self, inst, node):
        k = 2 * inst + (node - 5) // 2
        due, period = self.tachs[(inst, node)]
        even = CNTS[inst] + 0x1000 * (node - 1)
        pulses = self.fan_speed(k) * self.fan_pulses * period // (CORE_HZ * 60)
        self.mem.write(even + 0x18, 4, (self.mem.read(even + 0x18, 4) + pulses) & 0xffffffff)
        line = CNTS_LINES[inst]
        self.irq_raw[line] = self.irq_raw.get(line, 0) | 1 << (13 + (node - 5) // 2)
        self.tachs[(inst, node)][0] = due + period
        self.update_irqs()

    def next_event(self, cpu):
        dues = [due for due, _ in self.tachs.values()]
        return max(1, min(dues) - cpu.ccount) if dues else None

    def advance(self, cpu, cycles):
        for key in sorted(self.tachs, key=lambda k: self.tachs[k][0]):
            while key in self.tachs and self.tachs[key][0] <= cpu.ccount:
                self.tach_fire(*key)

    def pvt_update(self):
        warn = safe = 0
        for i in range(PVT_N):
            t = self.pvt_temp[i]
            base = PVT_BASE + 0x1000 * (i + 1)
            if t is None or not self.mem.read(base, 4) & 1:
                continue
            thr, c = self.mem.read(base + 0x0c, 4), pvt_code(t)
            if thr & 0x3ff and c >= thr & 0x3ff:
                warn |= 1 << i
            if c < (thr >> 10) & 0x3ff:
                safe |= 1 << i
        self.irq_raw[PVT_WARN_LINE], self.irq_raw[PVT_SAFE_LINE] = warn, safe
        self.update_irqs()

    def update_irqs(self):
        if self.cpu is None:
            return
        lines = 0
        for line in {MBOX_LINE, S5_LINE, I2C_LINE} | set(self.irq_raw):
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
        if ppu_status(addr):
            return (self.mem.read(addr, size) & ~0xf) | (self.mem.read(addr - 8, 4) & 0xf)
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
    # a PM without a running timer sleeps in WAITI until the doorbell
    asleep = lambda why: why is not None and str(why).startswith("waiti with nothing")  # noqa: E731
    while not rd(SMT_STATUS) & SMT_FREE:
        if cpu.insns >= end:
            raise ScmiError(f"channel {mbox} never free")
        why = cpu.run(chunk)
        if asleep(why):
            raise ScmiError(f"channel {mbox} never free")
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
        if asleep(why) and not rd(SMT_STATUS) & SMT_FREE:
            raise ScmiError(f"no answer to protocol {protocol:#x} message {msg} (the PM sleeps)")
        if why is not None and not asleep(why):
            raise ScmiError(f"stopped: {why} at {cpu.func(cpu.pc)}")
    if rd(SMT_STATUS) & SMT_ERROR:
        raise ScmiError("channel error")
    n = (rd(SMT_LEN) - 4) // 4
    words = [rd(SMT_PAYLOAD + 4 * i) for i in range(max(0, min(n, 25)))]
    status = words[0] - (1 << 32) if words and words[0] >> 31 else (words[0] if words else None)
    return status, words[1:]


CORE_HZ = 400_000_000                 # the core clock both images set (LCRU divider 1)


def run_for(cpu, soc, ms, chunk=50_000):
    """Run the PM for ms milliseconds of simulated time (CCOUNT at CORE_HZ).
    Time asleep in WAITI goes by at once: CCOUNT jumps to the next enabled
    CCOMPARE (xt_emu), never past the end; a PM with nothing to wake it
    sleeps out the rest.  Deterministic: the same image and inputs give
    the same run.  Returns None, or why the PM stopped (an assertion...)."""
    end = cpu.ccount + int(ms * CORE_HZ // 1000)
    cpu.until = end
    try:
        while cpu.ccount < end:
            why = cpu.run(max(1, min(chunk, end - cpu.ccount)))   # a cycle or more per instruction
            if why is None:
                continue
            if why == "until":
                break
            if str(why).startswith("waiti with nothing"):
                cpu.skip(end - cpu.ccount)
                break
            return why
    finally:
        cpu.until = None
    return None


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
