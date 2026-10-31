#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Run an SE firmware ELF (CIX's debug build or the open one) in Unicorn.

  se_emu.py SE.elf [--flash IMAGE] [--trace] [--max N]

The Cortex-M33 runs from the reset vector against a model of the SoC as
the SE sees it (Soc below): SRAM, the state the boot ROM leaves in it,
and the peripherals.  Registers without a model behave like memory.  Every
write outside SRAM is recorded with the function that made it; the
differential tests compare these records between CIX's firmware and the
open one.  The run stops at the idle loop (service_idle_entry), at a
function listed in stop=, or when one basic block repeats so often that
the firmware is clearly polling a register the model does not drive
(reported with the register).
"""

import argparse
import struct
import sys
from bisect import bisect_right
from collections import Counter

from elftools.elf.elffile import ELFFile
from elftools.elf.sections import SymbolTableSection
from unicorn import (UC_ARCH_ARM, UC_HOOK_BLOCK, UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_UNMAPPED,
                     UC_HOOK_MEM_WRITE, UC_MODE_MCLASS, UC_MODE_THUMB, Uc, UcError)
from unicorn.arm_const import (UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_R1,
                               UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_SP,
                               UC_CPU_ARM_CORTEX_M33)

SRAM, SRAM_SIZE = 0x20000000, 0x80000
IRQ_RETURN = 0x2007c000          # fake return address for injected interrupts
PAGE = 0x10000


class Elf:
    def __init__(self, path):
        with open(path, "rb") as f:
            elf = ELFFile(f)
            self.segments = [(p["p_paddr"], p.data()) for p in elf.iter_segments()
                             if p["p_type"] == "PT_LOAD" and p["p_filesz"]]
            self.syms = {}
            for s in elf.iter_sections():
                if isinstance(s, SymbolTableSection):
                    for sym in s.iter_symbols():
                        if sym["st_info"]["type"] in ("STT_FUNC", "STT_OBJECT") and sym.name:
                            self.syms[sym.name] = (sym["st_value"], sym["st_size"], sym["st_info"]["type"])
        funcs = sorted((v & ~1, n, name) for name, (v, n, t) in self.syms.items() if t == "STT_FUNC")
        self.func_addrs = [a for a, _, _ in funcs]
        self.funcs = funcs

    def addr(self, name):
        return self.syms[name][0] & ~1

    def func_at(self, pc):
        i = bisect_right(self.func_addrs, pc) - 1
        if i >= 0:
            a, n, name = self.funcs[i]
            if a <= pc < a + max(n, 2):
                return name
        return f"{pc:#x}"


class Soc:
    """Peripherals as the SE sees them.  read() returns the value a read
    gives, or None for "what was last written" (plain memory)."""

    def __init__(self, flash=b""):
        self.flash = flash
        self.se = None

    def attach(self, se):
        self.se = se

    def mem(self, addr):
        return struct.unpack("<I", self.se.uc.mem_read(addr & ~3, 4))[0]

    def read(self, addr, size):
        return None

    def write(self, addr, size, value):
        pass

    def reset_state(self, uc):
        pass


QSPI = 0xe0044000
MEMR_BASES = {0x0fc50000, 0x10000000, 0x14000000, 0x15000000, 0x14250000, 0x07000000, 0x14330000,
              0x14230000, 0x06000000, 0x0d030000, 0x0a000000, 0x08000000, 0x14050000, 0x09310000,
              0x14270000, 0x0b1a0000, 0x0b000000, 0x0b0d0000, 0x09250000, 0x00000000}
BL1_HEADER = 0x188000


class SkySoc(Soc):
    """Enough of Sky1 for the SE to boot: the boot ROM's hand-over in SRAM,
    the QSPI flash, PLLs that lock, power controllers that follow, the
    crypto engine's status, the PM's handshakes.  DRAM training is not
    modelled (the DDR driver's entry points are stubbed by the caller)."""

    def __init__(self, flash, life_cycle=4, boot_reason=2, jedec=(0xef, 0x60, 0x18)):
        super().__init__(flash)
        self.flash = bytearray(flash)
        self.life_cycle = life_cycle
        self.boot_reason = boot_reason
        self.jedec = bytes(jedec)
        self.qspi_fifo = bytearray()
        self.qspi_started = False
        self.qspi_wel = False
        self.qspi_wbuf = bytearray()
        self.qspi_cmd = None
        self.pm_magic = []
        self.rng = 0x12345678
        self.ppu = {}
        self.mbox_pending = None

    def reset_state(self, uc):
        # the boot ROM leaves the bootloader1 header in SRAM
        uc.mem_write(0x2007e800, bytes(self.flash[BL1_HEADER:BL1_HEADER + 0x1000]))

    # QSPI: indirect mode on the flash image
    def qspi_start(self):
        ccr = self.mem(QSPI + 0x0c)
        addr = self.mem(QSPI + 0x18)
        n = self.mem(QSPI + 0x20) + 1
        inst = ccr & 0xff
        data_mode = (ccr >> 18) & 3
        self.qspi_started = True
        self.qspi_fifo = bytearray()
        self.qspi_cmd = (inst, addr, n if data_mode else 0)
        if inst in (0x03, 0x13, 0x0b, 0x3b, 0x3c, 0x6b, 0x6c):
            self.qspi_fifo = bytearray(self.flash[addr:addr + n].ljust(n, b"\xff"))
        elif inst == 0x9f:
            self.qspi_fifo = bytearray(self.jedec[:n])
        elif inst == 0x05:
            self.qspi_fifo = bytearray([2 if self.qspi_wel else 0])
        elif inst == 0x06:
            self.qspi_wel = True
        elif inst == 0x20:
            a = addr & ~0xfff
            self.flash[a:a + 0x1000] = b"\xff" * 0x1000
            self.qspi_wel = False
        elif inst == 0x02:
            self.qspi_wbuf = bytearray()

    def qspi_data_size(self):
        ds = (self.mem(QSPI + 0x0c) >> 20) & 3
        return {3: 4, 1: 2, 0: 1}.get(ds, 1)

    def read(self, addr, size):
        if QSPI <= addr < QSPI + 0x100:
            off = addr - QSPI
            if off == 0x04:
                sr = self.mem(addr) & ~0xf014
                if self.qspi_started:
                    sr |= 4
                    if self.qspi_fifo:
                        sr |= 1 << 12
                    else:
                        sr |= 0x10
                return sr
            if off == 0x1c:
                k = self.qspi_data_size()
                v = int.from_bytes(self.qspi_fifo[:k].ljust(k, b"\0"), "little")
                del self.qspi_fifo[:k]
                return v
            return None
        if 0x46570000 <= addr < 0x46578000 and addr & 0xff == 0x0c:
            return self.mem(addr) | 0x10                    # PLL locked
        if addr == 0x46576100:
            return self.mem(addr) | 0x40                    # PCIe PHY PLL locked
        if addr == 0xe000e010:
            return self.mem(addr) | 0x10000                 # SysTick COUNTFLAG
        if addr == 0xe000ed90:
            return 16 << 8                                  # MPU regions
        if addr == 0xa0010018:
            return 0x10                                     # UART: RX empty, TX free
        if addr == 0xa0020018:
            return 0
        if addr == 0x56000218:
            return self.boot_reason
        if addr == 0xb00300e0:
            return self.life_cycle
        # crypto engine
        if addr == 0xa0040008:
            return 0xfffff
        if addr == 0xa0040014:
            return 1 << 24
        if addr == 0xa0043398:
            return 1
        if addr == 0xa0043304:
            return 0
        if addr == 0xa0043308:
            return 1
        if 0xa0043310 <= addr < 0xa0043330:
            self.rng = (self.rng * 1103515245 + 12345) & 0xffffffff
            return self.rng
        # power controllers follow their requests
        if addr == 0x4f030008 or (0x4f080008 <= addr < 0x50000000 and (addr - 0x4f080008) % 0x100000 == 0):
            return self.mem(addr - 8)
        # CI-700: SLC flush and MTE tag flush done
        if addr & 0xffff == 0x0f68 or addr & 0xffff == 0x0a38:
            return self.mem(addr) | 1
        # SRAM repair controllers: chunk taken, data received, repaired
        if addr - 0x40000010 in MEMR_BASES:
            return (self.mem(addr) & ~0xf0) | 0xe
        if addr == 0x46610090:
            return self.pm_magic[0] if self.pm_magic else self.mem(addr)
        if addr in (0xa00500c8, 0xa00700c8):
            return 1 if self.mbox_pending == addr - 0xc8 else 0
        return None

    def write(self, addr, size, value):
        if addr == QSPI and value & 2:
            self.qspi_start()
        elif addr == QSPI + 0x1c and self.qspi_cmd and self.qspi_cmd[0] == 0x02:
            self.qspi_wbuf.append(value & 0xff)
            inst, a, n = self.qspi_cmd
            if len(self.qspi_wbuf) == n:
                for i, b in enumerate(self.qspi_wbuf):
                    self.flash[a + i] &= b
                self.qspi_wel = False
        elif addr == 0x56000704 and not value & 1:
            self.pm_magic.append(0x5c5a)                    # PM released: it comes up
        elif addr == 0x46610090:
            if value == 0x55ca:
                self.pm_magic.append(0x5a5c)
            elif value == 0 and self.pm_magic:
                self.pm_magic.pop(0)


class Se:
    def __init__(self, elf_path, soc=None, stubs=None, stop=(), trace=False):
        self.elf = Elf(elf_path)
        self.elf_path = elf_path
        self.soc = soc or Soc()
        self.soc.attach(self)
        self.uc = uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M33)
        uc.mem_map(SRAM, SRAM_SIZE)
        for addr, data in self.elf.segments:
            uc.mem_write(addr, data)
        self.soc.reset_state(uc)
        self.mapped = set()
        self.writes = []         # (function, address, size, value)
        self.calls = []          # function entries, in order (with trace)
        self.trace = trace
        self.blocks = Counter()
        self.stubs = {self.elf.addr(n): v for n, v in (stubs or {}).items() if n in self.elf.syms}
        self.stops = {self.elf.addr(n): n for n in stop if n in self.elf.syms}
        if "service_idle_entry" in self.elf.syms:
            self.stops[self.elf.addr("service_idle_entry")] = "service_idle_entry"
        self.entries = {a: name for a, _, name in self.elf.funcs}
        self.stopped = None
        self.poll = None
        self.stop_write = None
        self.wfi = 0
        self.wfi_seen = set()
        self.wfi_hook = None
        uc.hook_add(UC_HOOK_MEM_UNMAPPED, self.on_unmapped)
        for lo, hi in ((0, SRAM - 1), (SRAM + SRAM_SIZE, 0xFFFFFFFF)):
            uc.hook_add(UC_HOOK_MEM_READ, self.on_read, begin=lo, end=hi)
            uc.hook_add(UC_HOOK_MEM_WRITE, self.on_write, begin=lo, end=hi)
        uc.hook_add(UC_HOOK_BLOCK, self.on_block)
        # WFI: the wake-up interrupt comes at once (Unicorn would halt for good)
        for addr, data in self.elf.segments:
            for off in range(0, len(data) - 1, 2):
                if data[off:off + 2] == b"\x30\xbf":
                    uc.hook_add(UC_HOOK_CODE, self.on_wfi, begin=addr + off, end=addr + off)

    # memory ---------------------------------------------------------------
    def on_unmapped(self, uc, access, addr, size, value, user):
        page = addr & ~(PAGE - 1)
        if page in self.mapped:
            return False
        uc.mem_map(page, PAGE)
        self.mapped.add(page)
        return True

    def pc_func(self):
        return self.elf.func_at(self.uc.reg_read(UC_ARM_REG_PC))

    def on_read(self, uc, access, addr, size, value, user):
        v = self.soc.read(addr, size)
        if v is not None:
            uc.mem_write(addr, (v & ((1 << (8 * size)) - 1)).to_bytes(size, "little"))
        self.last_read = addr

    def on_write(self, uc, access, addr, size, value, user):
        value &= (1 << (8 * size)) - 1
        self.writes.append((self.pc_func(), addr, size, value))
        self.soc.write(addr, size, value)
        if (addr, value) == self.stop_write:
            self.stopped = f"write {value:#x} to {addr:#x}"
            uc.emu_stop()

    # execution --------------------------------------------------------------
    def on_block(self, uc, addr, size, user):
        if addr in self.stops:
            self.stopped = self.stops[addr]
            uc.emu_stop()
            return
        if addr in self.stubs:
            stub = self.stubs[addr]
            uc.reg_write(UC_ARM_REG_R0, stub(self) if callable(stub) else stub)
            uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))
            return
        if self.trace and addr in self.entries:
            self.calls.append(self.entries[addr])
        self.blocks[addr] += 1
        if self.blocks[addr] > 200_000:
            self.poll = (self.elf.func_at(addr), addr, getattr(self, "last_read", None))
            uc.emu_stop()

    def on_wfi(self, uc, addr, size, user):
        self.wfi += 1
        if self.wfi_hook:
            self.wfi_hook(self)
        uc.reg_write(UC_ARM_REG_PC, (addr + 2) | 1)

    def irq(self, n, count=20_000_000):
        """Take external interrupt n: run the vector-table handler with IPSR
        set, on a scratch stack, until it returns."""
        from unicorn.arm_const import UC_ARM_REG_IPSR
        handler = self.read32(SRAM + (16 + n) * 4)
        saved = self.uc.context_save()
        self.uc.reg_write(UC_ARM_REG_IPSR, 16 + n)
        self.uc.reg_write(UC_ARM_REG_SP, 0x2006f000)
        self.uc.reg_write(UC_ARM_REG_LR, IRQ_RETURN | 1)
        self.stops[IRQ_RETURN] = "irq return"
        self.stopped = None
        try:
            self.uc.emu_start(handler | 1, 0xFFFFFFFF, count=count)
        except UcError as e:
            pc = self.uc.reg_read(UC_ARM_REG_PC)
            raise SystemExit(f"emulation error at {pc:#x} ({self.elf.func_at(pc)}): {e}")
        finally:
            del self.stops[IRQ_RETURN]
            self.uc.context_restore(saved)
        return self.stopped == "irq return"

    def arg(self, n):
        return self.uc.reg_read((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)[n])

    def read(self, addr, n):
        return bytes(self.uc.mem_read(addr, n))

    def write(self, addr, data):
        self.uc.mem_write(addr, bytes(data))

    def read32(self, addr):
        return struct.unpack("<I", self.uc.mem_read(addr, 4))[0]

    def reset(self):
        sp, pc = self.read32(SRAM), self.read32(SRAM + 4)
        self.uc.reg_write(UC_ARM_REG_SP, sp)
        return pc

    def resume(self, count=50_000_000):
        """Carry on from where the last run stopped."""
        self.stopped = None
        self.poll = None
        self.blocks.clear()
        return self.run(self.uc.reg_read(UC_ARM_REG_PC), count)

    def run(self, pc=None, count=50_000_000):
        pc = self.reset() if pc is None else pc
        try:
            self.uc.emu_start(pc | 1, 0xFFFFFFFF, count=count)
        except UcError as e:
            pc = self.uc.reg_read(UC_ARM_REG_PC)
            raise SystemExit(f"emulation error at {pc:#x} ({self.elf.func_at(pc)}): {e}")
        return self.stopped


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("elf")
    ap.add_argument("--flash")
    ap.add_argument("--trace", action="store_true")
    ap.add_argument("--stub", action="append", default=[], help="NAME=R0: return R0 at once")
    ap.add_argument("--stop", action="append", default=[])
    ap.add_argument("--max", type=int, default=50_000_000)
    a = ap.parse_args()
    soc = Soc(open(a.flash, "rb").read() if a.flash else b"")
    stubs = dict((s.split("=")[0], int(s.split("=")[1], 0)) for s in a.stub)
    se = Se(a.elf, soc, stubs=stubs, stop=a.stop, trace=a.trace)
    stopped = se.run(count=a.max)
    if a.trace:
        for c in se.calls:
            print("call", c)
    for fn, addr, size, value in se.writes:
        print(f"w {fn:32s} {addr:#010x} {size} {value:#x}")
    if se.poll:
        fn, addr, reg = se.poll
        print(f"polling in {fn} at {addr:#x}, last read {reg:#x}" if reg else f"loop in {fn} at {addr:#x}")
    else:
        print("stopped at", stopped or se.pc_func())


if __name__ == "__main__":
    main()
