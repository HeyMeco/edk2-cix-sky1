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
    crypto engine's status, the PM's handshakes.  DRAM controllers: with
    ddr=DdrModel(), what CIX's DDR driver waits for; without, the caller
    stubs the driver's entry points."""

    def __init__(self, flash, life_cycle=4, boot_reason=2, jedec=(0xef, 0x60, 0x18), efuse=None, ddr=None):
        super().__init__(flash)
        self.ddr = ddr                       # DdrModel, or None when the DDR driver is stubbed
        self.efuse = efuse or {}             # byte address -> bytes
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
        self.window = 0

    def attach(self, se):
        self.se = se
        if self.ddr:
            self.ddr.attach(se)

    def reset_state(self, uc):
        # the boot ROM leaves the bootloader1 header in SRAM, and the fuses in the fuse SRAM
        uc.mem_write(0x2007e800, bytes(self.flash[BL1_HEADER:BL1_HEADER + 0x1000]))
        for addr, data in self.efuse.items():
            page = (0xb0040000 + addr) & ~0xffff
            if page not in self.se.mapped:
                uc.mem_map(page, 0x10000)
                self.se.mapped.add(page)
            uc.mem_write(0xb0040000 + addr, bytes(data))

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

    def pm_alias(self, addr):
        """With the SYSHUB window on the PM (SoC 0x1c90000000), its first
        MiB is the PM's SRAM, which the SE also sees at 0x46a10000."""
        if self.window == 0x392 and 0x60000000 <= addr < 0x60108000:
            return addr - 0x60000000 + 0x46a10000
        return None

    def read(self, addr, size):
        alias = self.pm_alias(addr)
        if alias is not None:
            return int.from_bytes(self.se.uc.mem_read(alias, size), "little")
        if self.ddr:
            v = self.ddr.read(addr, size)
            if v is not None:
                return v
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
        if 0x50000000 <= addr < 0x54000000 and addr & 0xffff in (0x0f68, 0x0a38):
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
        alias = self.pm_alias(addr)
        if alias is not None:
            self.se.uc.mem_write(alias, value.to_bytes(size, "little"))
        if self.ddr:
            self.ddr.write(addr, size, value)
        if addr == 0xb0030100:
            self.window = value
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


DDRC = 0x4c010000                # channel ch at DDRC + ch * DDRC_STRIDE (SMN 0x0c010000)
DDRC_STRIDE = 0x20000
DDR_FANOUT = 0x46510600          # PM public DDR fan-out registers, one per channel
DDR_IRQ = (219, 221, 223, 225)   # CIX's ddrc_irq_type
DDR_RESET = 0x56000400           # SoC reset release; bit 0: the DDR subsystem (MemDdrcReset)


class DdrModel:
    """The four LPDDR5 channels (Cadence Denali controller at +0, PI at
    +0x2000, PHY at +0x4000 of each channel) and the PM's DDR fan-out
    registers, as far as CIX's DDR driver waits for them.  Registers are
    memory apart from the status bits below; what they do is read from what
    CIX's driver writes, polls and acknowledges (and the bit names in its
    DWARF), not from Cadence's documentation.  Modelled, not known:

    * reset (DDR_RESET bit 0 from 1 to 0, CIX's MemDdrcReset): every register of
      the four channels reads 0 afterwards and the state below starts over.
    * interrupt groups: status at 0x57c + 4g, acknowledge at 0x5a0 + 4g
      (write 1 to clear), mask at 0x5c4 + 4g (CIX's DdrcIrqTlb gives status
      and acknowledge; the mask offset follows from enable_ddr_init_monitor
      unmasking INIT_COMP at 0x5e4).  The channel's interrupt (DDR_IRQ) is
      up while a status bit is set and not masked; the master mask (0x578)
      is not modelled.
    * START of the controller (CTL_0 bit 0 going from 0 to 1): the PI trains at frequency 0,
      then asks the PM (fan-out register: freq_change_req, type = frequency
      index) for each further frequency in PI_FREQ_MAP (PI_13) in rising
      order, and for PI_INIT_WORK_FREQ (PI_14) last if it is not the last
      one trained; each request is dropped when the SE sets
      freq_change_ack and the next comes when it clears it.  Then INIT_COMP
      (0x59c bit 9) and PI_INT_STATUS bit 0 (PI init done) are set.  No
      training step fails (0x598 DFI errors stay clear) and PI training
      status bits other than bit 0 stay clear.  Without the PI started
      (CIX's boot environment 0 does not write PI registers) INIT_COMP comes
      at once.
    * mode registers: a write (CTL_205 WRITE_MODEREG, bit 25) stores
      CTL_217's MRSINGLE_DATA for the rank (bit 24: all ranks) and sets
      MRW_DONE (0x59c bit 19); a read (CTL_206 READ_MODEREG, bit 24) puts
      the value in PERIPHERAL_MRR_DATA (CTL_207) for every device (16 bits
      each) and sets MRR_DONE (bit 18).  Registers never written read as
      MR_DEFAULTS (manufacturer, revision, density: a 16 Gb x16 LPDDR5
      device by default).
    * low-power commands (CTL_175 LP_CMD): CTL_190 LP_STATE reads
      0x40 | command (bit 6 = valid).  Only the status bits the driver
      polls are ever set (else a status nobody acknowledges would keep the
      interrupt up: the group masks' reset values are not known).
    * BIST: the controller's (0x3cc bit 0) sets BIST_DONE (0x598 bit 0) and
      reads back passed (0x3cc bit 8); data initialisation (0x40c bit 8)
      reads back done (0x40c bit 0); the PI's (PI_324, 0x2510 bit 0) sets
      PI_INT_STATUS bit 16 and passed (0x2510 bit 8).
    * fan-out DFS handshake: lp_ext_ack follows lp_ext_req; the command
      strobe makes the controller ask for a clock change
      (cntrl_freq_change_req) until the SE acknowledges it
      (cntrl_freq_change_ack), after which lp_ext_done reads set until
      lp_ext_req is cleared.
    """

    STATUS, ACK, MASK = 0x57c, 0x5a0, 0x5c4
    GROUPS = 9
    MR_DEFAULTS = {5: 0x01, 6: 0x00, 7: 0x00, 8: 0x18}
    FANOUT_SE = 0x1 | 0xc | 0x3f0 | 0x400 | 0x4000 | 0x80000000      # bits the SE drives

    def __init__(self):
        self.se = None
        self.resets = 0
        self.reset()

    def reset(self):
        """DDR subsystem reset: the channels' state, and their registers read
        as zero (their reset values are not known)."""
        if self.se is not None:
            for ch in range(4):
                page = DDRC + ch * DDRC_STRIDE
                if page in self.se.mapped:
                    self.se.uc.mem_write(page, bytes(0x10000))
                self.se.set_line(DDR_IRQ[ch], False)
        self.status = [[0] * self.GROUPS for _ in range(4)]
        self.pi_status = [0] * 4
        self.started = [False] * 4
        self.freq_req = [[] for _ in range(4)]   # frequencies the PI still asks for
        self.req_up = [False] * 4                # request shown in the fan-out register
        self.req_acked = [False] * 4
        self.lp_state = [0] * 4
        self.bist_pass = [False] * 4
        self.pi_bist_pass = [False] * 4
        self.data_init = [False] * 4
        self.mr = [{} for _ in range(4)]         # (rank, mr) -> value
        self.dfs = [dict(creq=False, done=False) for _ in range(4)]
        self.log = []                            # (channel, event), for reports

    def attach(self, se):
        self.se = se

    def mem(self, addr):
        return struct.unpack("<I", self.se.uc.mem_read(addr & ~3, 4))[0]

    @staticmethod
    def channel(addr):
        if DDRC <= addr < DDRC + 4 * DDRC_STRIDE and (addr - DDRC) % DDRC_STRIDE < 0x10000:
            return (addr - DDRC) // DDRC_STRIDE, (addr - DDRC) % DDRC_STRIDE
        return None, None

    def update_line(self, ch):
        base = DDRC + ch * DDRC_STRIDE
        up = any(self.status[ch][g] & ~self.mem(base + self.MASK + 4 * g) for g in range(self.GROUPS))
        self.se.set_line(DDR_IRQ[ch], up)

    def set_status(self, ch, off, bits):
        self.status[ch][(off - self.STATUS) // 4] |= bits
        self.update_line(ch)

    def read(self, addr, size):
        if DDR_FANOUT <= addr < DDR_FANOUT + 16 and size == 4:
            ch = (addr - DDR_FANOUT) // 4
            v = self.mem(addr) & self.FANOUT_SE
            if v & 1:
                v |= 2                                   # lp_ext_ack
            if self.dfs[ch]["creq"]:
                v |= 1 << 30
            if self.dfs[ch]["done"]:
                v |= 1 << 11
            if self.req_up[ch] and self.freq_req[ch]:
                v |= 1 << 20 | (self.freq_req[ch][0] & 0x1f) << 15
            return v
        ch, off = self.channel(addr)
        if ch is None or size != 4:
            return None
        if self.STATUS <= off < self.STATUS + 4 * self.GROUPS:
            return self.status[ch][(off - self.STATUS) // 4]
        if off == 0x2f8:                                 # CTL_190 LP_STATE
            return (self.mem(addr) & ~0x7f00) | self.lp_state[ch] << 8
        if off == 0x3cc:
            return (self.mem(addr) & ~0x100) | (0x100 if self.bist_pass[ch] else 0)
        if off == 0x40c:
            return (self.mem(addr) & ~1) | (1 if self.data_init[ch] else 0)
        if off == 0x24a0:                                # PI_INT_STATUS (PI_296)
            return self.pi_status[ch]
        if off == 0x2510:
            return (self.mem(addr) & ~0x100) | (0x100 if self.pi_bist_pass[ch] else 0)
        return None

    def write(self, addr, size, value):
        if DDR_FANOUT <= addr < DDR_FANOUT + 16 and size == 4:
            self.fanout_write((addr - DDR_FANOUT) // 4, value)
            return
        if addr == DDR_RESET and self.mem(addr) & 1 and not value & 1:     # DDRC put in reset
            self.resets += 1
            self.reset()
            return
        ch, off = self.channel(addr)
        if ch is None or size != 4:
            return
        base = DDRC + ch * DDRC_STRIDE
        if self.ACK <= off < self.ACK + 4 * self.GROUPS:
            self.status[ch][(off - self.ACK) // 4] &= ~value
            self.update_line(ch)
        elif self.MASK <= off < self.MASK + 4 * self.GROUPS:
            self.update_line(ch)
        elif off == 0 and value & 1 and not self.mem(addr) & 1:      # START, rising
            self.start(ch, base)
        elif off == 0x334 and value & (1 << 25):          # CTL_205 WRITE_MODEREG
            mr, data = value & 0xff, self.mem(base + 0x364) >> 16 & 0xff
            ranks = (0, 1) if value & (1 << 24) else ((value >> 8) & 0xff,)
            for r in ranks:
                self.mr[ch][(r, mr)] = data
            self.set_status(ch, 0x59c, 1 << 19)           # MRW_DONE
        elif off == 0x338 and value & (1 << 24):          # CTL_206 READ_MODEREG
            mr, rank = (value >> 8) & 0xff, (value >> 16) & 0xff
            data = self.mr[ch].get((rank, mr), self.MR_DEFAULTS.get(mr, 0))
            self.se.uc.mem_write(base + 0x33c, (data | data << 16).to_bytes(4, "little"))
            self.set_status(ch, 0x59c, 1 << 18)           # MRR_DONE
        elif off == 0x2bc:                                # CTL_175 LP_CMD
            self.lp_state[ch] = 0x40 | (value >> 16 & 0x3f)
        elif off == 0x3cc:
            if value & 1 and not self.bist_pass[ch]:
                self.bist_pass[ch] = True
                self.set_status(ch, 0x598, 1)             # BIST_DONE
            elif not value & 1:
                self.bist_pass[ch] = False
        elif off == 0x40c:
            self.data_init[ch] = bool(value & 0x100)
        elif off == 0x2510:
            if value & 1 and not self.pi_bist_pass[ch]:
                self.pi_bist_pass[ch] = True
                self.pi_status[ch] |= 1 << 16
            elif not value & 1:
                self.pi_bist_pass[ch] = False
        elif off == 0x24a8:                               # PI_INT_ACK
            self.pi_status[ch] &= ~value

    def start(self, ch, base):
        self.started[ch] = True
        if not self.mem(base + 0x2000) & 1:              # PI not started
            self.log.append((ch, "init"))
            self.init_done(ch)
            return
        fmap, work = self.mem(base + 0x2034), self.mem(base + 0x2038) & 0x1f
        freqs = [f for f in range(1, 32) if fmap >> f & 1]
        if work != (freqs[-1] if freqs else 0):
            freqs.append(work)
        self.freq_req[ch] = freqs
        self.log.append((ch, f"train, frequency changes {freqs}"))
        if freqs:
            self.req_up[ch] = True
        else:
            self.init_done(ch)

    def init_done(self, ch):
        self.pi_status[ch] |= 1
        self.set_status(ch, 0x59c, 1 << 9)                # INIT_COMP

    def fanout_write(self, ch, value):
        d = self.dfs[ch]
        if value & 0x400 and value & 1:
            d["creq"] = True                              # command strobe: clock change wanted
        if value & (1 << 31) and d["creq"]:
            d["creq"], d["done"] = False, True
        if not value & 1:
            d["done"] = False
        if self.freq_req[ch]:
            if value & (1 << 14) and self.req_up[ch]:
                self.req_up[ch], self.req_acked[ch] = False, True
            elif not value & (1 << 14) and self.req_acked[ch]:
                self.req_acked[ch] = False
                self.freq_req[ch].pop(0)
                if self.freq_req[ch]:
                    self.req_up[ch] = True
                else:
                    self.init_done(ch)


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
        self.mapped = set()
        self.soc.reset_state(uc)
        self.writes = []         # (function, address, size, value)
        self.calls = []          # function entries, in order (with trace)
        self.trace = trace
        self.blocks = {}         # block -> (write count when last seen, runs since)
        self.gen = 0
        self.stubs = {self.elf.addr(n): v for n, v in (stubs or {}).items() if n in self.elf.syms}
        self.hooks = {}          # block address -> fn(se), called before the block runs
        self.stops = {self.elf.addr(n): n for n in stop if n in self.elf.syms}
        if "service_idle_entry" in self.elf.syms:
            self.stops[self.elf.addr("service_idle_entry")] = "service_idle_entry"
        self.entries = {a: name for a, _, name in self.elf.funcs}
        self.stopped = None
        self.poll = None
        self.stop_write = None
        self.budget = 0
        self.wfi = 0
        self.wfi_seen = set()
        self.poll_limit = 200_000
        self.wfi_hook = None
        # external interrupt lines the SoC model drives (level), the NVIC's
        # enables as the firmware sets them, and the line to take next
        self.lines = set()
        self.nvic_enabled = set()
        self.in_irq = False
        self.take_irq = None
        self.irqs_taken = 0
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
        self.gen += 1
        if 0xe000e100 <= addr < 0xe000e1c0 and size == 4:       # NVIC ISER / ICER
            base = 0xe000e100 if addr < 0xe000e180 else 0xe000e180
            for b in range(32):
                if value >> b & 1:
                    n = (addr - base) * 8 + b
                    (self.nvic_enabled.add if base == 0xe000e100 else self.nvic_enabled.discard)(n)
        self.soc.write(addr, size, value)
        if (addr, value) == self.stop_write:
            self.stopped = f"write {value:#x} to {addr:#x}"
            uc.emu_stop()

    # execution --------------------------------------------------------------
    def set_line(self, n, level):
        """The SoC model raises (level true) or lowers external interrupt n."""
        (self.lines.add if level else self.lines.discard)(n)

    def start(self, pc, count):
        """emu_start with an instruction budget kept here, not by Unicorn:
        Unicorn 2.1.4's count= (like an unranged memory-write hook) breaks
        Thumb IT blocks with conditional stores, skipping the instructions
        after them.  The budget is counted per block (bytes / 2, Thumb), so
        it bounds a run rather than ending it at an exact instruction."""
        self.budget = count
        self.uc.emu_start(pc | 1, 0xFFFFFFFF)

    def on_block(self, uc, addr, size, user):
        self.budget -= max(size // 2, 1)
        if self.budget < 0:
            uc.emu_stop()
            return
        if self.lines and not self.in_irq:
            from unicorn.arm_const import UC_ARM_REG_PRIMASK
            ready = sorted(n for n in self.lines if n in self.nvic_enabled)
            if ready and not uc.reg_read(UC_ARM_REG_PRIMASK) & 1:
                self.take_irq = ready[0]
                uc.emu_stop()
                return
        if addr in self.stops:
            self.stopped = self.stops[addr]
            uc.emu_stop()
            return
        if addr in self.hooks:
            self.hooks[addr](self)
        if addr in self.stubs:
            stub = self.stubs[addr]
            uc.reg_write(UC_ARM_REG_R0, stub(self) if callable(stub) else stub)
            uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))
            return
        if self.trace and addr in self.entries:
            self.calls.append(self.entries[addr])
        gen, n = self.blocks.get(addr, (self.gen, 0))
        n = n + 1 if gen == self.gen else 1
        self.blocks[addr] = (self.gen, n)
        if n > self.poll_limit:
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
        self.in_irq = True
        try:
            self.start(handler, count)
        except UcError as e:
            pc = self.uc.reg_read(UC_ARM_REG_PC)
            raise SystemExit(f"emulation error at {pc:#x} ({self.elf.func_at(pc)}): {e}")
        finally:
            self.in_irq = False
            del self.stops[IRQ_RETURN]
            self.uc.context_restore(saved)
        return self.stopped == "irq return"

    def call(self, name, *args, count=50_000_000):
        """Call a firmware function with up to four word arguments on a
        scratch stack, and return its r0 (the machine state is restored
        afterwards)."""
        saved = self.uc.context_save()
        stops = dict(self.stops)
        for i, v in enumerate(args):
            self.uc.reg_write((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)[i], v)
        self.uc.reg_write(UC_ARM_REG_SP, 0x2006f000)
        self.uc.reg_write(UC_ARM_REG_LR, IRQ_RETURN | 1)
        self.stops = {IRQ_RETURN: "return"}
        self.stopped = None
        try:
            self.start(self.elf.addr(name), count)
            if self.stopped != "return":
                raise SystemExit(f"{name}() did not return")
            return self.uc.reg_read(UC_ARM_REG_R0)
        finally:
            self.stops = stops
            self.uc.context_restore(saved)

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
        while True:
            try:
                self.start(pc, count)
            except UcError as e:
                pc = self.uc.reg_read(UC_ARM_REG_PC)
                raise SystemExit(f"emulation error at {pc:#x} ({self.elf.func_at(pc)}): {e}")
            if self.take_irq is None:
                return self.stopped
            # an interrupt line came up: take it, then carry on where the code was
            n, self.take_irq = self.take_irq, None
            pc = self.uc.reg_read(UC_ARM_REG_PC)
            self.irqs_taken += 1
            if not self.irq(n):
                raise SystemExit(f"interrupt {n}: the handler did not return ({self.stopped or self.poll})")
            self.stopped = None


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
