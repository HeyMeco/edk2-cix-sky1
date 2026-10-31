#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Interpreter for CSU_PM's Xtensa core (Cadence LX7 "pm_core_v1").

The core as CIX's firmware uses it (firmware/csupm/README.md): windowed ABI
with 64 address registers, density, zero-overhead loops, MUL16/MUL32/
MUL32_HIGH, DIV32, NSA, MIN/MAX, SEXT, booleans, SALT/SALTU, a single-
precision FPU with the double-precision extension (coprocessor 0; 64-bit
FP registers, single-precision operations use the low half; opcodes as in
QEMU's de233_fpu core description), exclusive-access state (GETEX), XEA2 exceptions and interrupts (32 lines,
levels from Xthal_intlevel, EXCM level 5, NMI 7), three CCOUNT/CCOMPARE
timers, relocatable vectors, and 8-byte FLIX bundles (xtensa_flix.py), which
run as one instruction: all slots read, then write, then transfer control.
MPU, cache and TLB instructions are accepted and do nothing.

  cpu = Xtensa(bus)        bus.read(addr, size) / bus.write(addr, size, value)
  cpu.add_ram(base, size)  RAM the interpreter accesses directly
  cpu.load_elf(path)       load PT_LOAD segments, symbols
  cpu.run(count)           returns the reason it stopped

Memory outside RAM goes to the bus; every bus write is appended to
cpu.writes as (pc, addr, size, value).  cpu.stops (pc -> name) end run();
cpu.stubs (pc -> function(cpu) -> return value or None) replace a function
at its entry, before its ENTRY, with the windowed calling convention.
"""

import struct
import sys
from bisect import bisect_right
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from xtensa_flix import decode_bundle  # noqa: E402

M32 = 0xffffffff
B4CONST = (-1, 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 16, 32, 64, 128, 256)
B4CONSTU = (32768, 65536, 2, 3, 4, 5, 6, 7, 8, 10, 12, 16, 32, 64, 128, 256)

# special registers
LBEG, LEND, LCOUNT, SAR, BR, LITBASE = 0, 1, 2, 3, 4, 5
WINDOWBASE, WINDOWSTART = 72, 73
EPC1, DEPC, EPS2, EXCSAVE1 = 177, 192, 194, 209
CPENABLE, INTSET, INTCLEAR, INTENABLE, PS, VECBASE = 224, 226, 227, 228, 230, 231
EXCCAUSE, CCOUNT, PRID, EXCVADDR, CCOMPARE0 = 232, 234, 235, 238, 240
FCR, FSR = 232, 233          # user registers

# PS fields
PS_INTLEVEL, PS_EXCM, PS_UM, PS_WOE = 0xf, 1 << 4, 1 << 5, 1 << 18
PS_OWB_SHIFT, PS_CALLINC_SHIFT = 8, 16

# exception causes
ILLEGAL, SYSCALL, LEVEL1_INT, ALLOCA, DIVIDE_BY_ZERO, CP0_DISABLED = 0, 1, 4, 5, 6, 32

# vector offsets from VECBASE (CIX's ELF: _WindowOverflow4 is VECBASE)
VEC = {"overflow4": 0x000, "underflow4": 0x040, "overflow8": 0x080, "underflow8": 0x0c0,
       "overflow12": 0x100, "underflow12": 0x140, 2: 0x17c, 3: 0x19c, 4: 0x1bc, 5: 0x1dc,
       6: 0x1fc, 7: 0x21c, "kernel": 0x23c, "user": 0x25c, "double": 0x27c}
INTLEVEL = (1, 1, 1, 1, 2, 3, 1, 4, 5, 4, 5, 5, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 7)
EXCM_LEVEL = 5
TIMER_INT = (6, 7, 13)       # CCOMPARE1 is interrupt 7 (FreeRTOS tick); 0 and 2 unused by CIX


def sx(v, bits):
    v &= (1 << bits) - 1
    return v - (1 << bits) if v >> (bits - 1) else v


def s32(v):
    return v - (1 << 32) if v & 0x80000000 else v


def f2u(f):
    try:
        return struct.unpack("<I", struct.pack("<f", f))[0]
    except OverflowError:
        return 0x7f800000 if f > 0 else 0xff800000


def u2f(u):
    return struct.unpack("<f", struct.pack("<I", u & M32))[0]


def d2u(f):
    return struct.unpack("<Q", struct.pack("<d", f))[0]


def u2d(u):
    return struct.unpack("<d", struct.pack("<Q", u & 0xffffffffffffffff))[0]


class XtException(Exception):
    def __init__(self, cause, vaddr=None):
        super().__init__(cause)
        self.cause, self.vaddr = cause, vaddr


class Stop(Exception):
    pass


class Op:
    """A decoded instruction: fn(cpu, pc, length) -> next pc or None."""
    __slots__ = ("fn", "maxr", "length", "ctl", "fp", "text")

    def __init__(self, fn, maxr=0, ctl=False, fp=False, text=""):
        self.fn, self.maxr, self.ctl, self.fp, self.text = fn, maxr, ctl, fp, text
        self.length = 0


class Xtensa:
    def __init__(self, bus, vecbase=0x20000400):
        self.bus = bus
        self.ar = [0] * 64
        self.rd = self.ar                 # register file reads come from here (bundle snapshot)
        self.wb4 = 0
        self.ws = 1
        # CPENABLE: CIX's main() uses the FPU before the scheduler, and only
        # FreeRTOS's task switch writes CPENABLE, so coprocessor 0 must be
        # enabled out of reset (otherwise CIX's firmware stops in _xt_panic).
        self.sr = {VECBASE: vecbase, PS: 0x1f, PRID: 0, CPENABLE: 1}
        self.ur = {FCR: 0, FSR: 0}
        self.fr = [0] * 16
        self.br = 0
        self.sar = 0
        self.lbeg = self.lend = self.lcount = 0
        self.ccount = 0
        self.ccompare = [0, 0, 0]
        self.int_lines = 0               # level-sensitive lines driven by the SoC model
        self.int_latched = 0             # edge, software and timer interrupts
        self.intenable = 0
        self.pc = 0
        self.ram = []
        self.cache = {}
        self.cmin, self.cmax = M32, 0       # range of decoded code, for invalidation
        self.writes = []
        self.stops = {}
        self.stubs = {}
        self.stop_write = None
        self.waiting = False
        self.insns = 0
        self.symbols = []
        self.sym_addrs = []
        self.div = None                   # operands of an FP divide sequence
        self.exclusive = 0                # exclusive-access monitor result (GETEX)
        self.trace = None

    # --- memory ----------------------------------------------------------

    def add_ram(self, base, size):
        self.ram.append((base, base + size, bytearray(size)))
        self.ram.sort()

    def _ram(self, addr, size):
        for lo, hi, buf in self.ram:
            if lo <= addr and addr + size <= hi:
                return buf, addr - lo
        return None, 0

    def read(self, addr, size):
        addr &= M32
        buf, off = self._ram(addr, size)
        if buf is not None:
            return int.from_bytes(buf[off:off + size], "little")
        return self.bus.read(addr, size) & ((1 << (8 * size)) - 1)

    def write(self, addr, size, value):
        addr &= M32
        value &= (1 << (8 * size)) - 1
        buf, off = self._ram(addr, size)
        if buf is not None:
            buf[off:off + size] = value.to_bytes(size, "little")
            if self.cmin <= addr + size and addr <= self.cmax:
                for a in range(addr - 7, addr + size):
                    self.cache.pop(a, None)
            return
        self.writes.append((self.pc, addr, size, value))
        self.bus.write(addr, size, value)
        if self.stop_write and self.stop_write == (addr, value):
            raise Stop(f"write {value:#x} to {addr:#x}")

    def read_bytes(self, addr, n):
        return bytes(self.read(addr + i, 1) for i in range(n))

    def write_bytes(self, addr, data):
        for i, b in enumerate(data):
            self.write(addr + i, 1, b)

    def load_elf(self, path, segments=True):
        from elftools.elf.elffile import ELFFile
        from elftools.elf.sections import SymbolTableSection
        with open(path, "rb") as fh:
            elf = ELFFile(fh)
            if segments:
                for seg in elf.iter_segments():
                    if seg["p_type"] == "PT_LOAD" and seg["p_filesz"]:
                        self.write_bytes(seg["p_paddr"], seg.data())
            syms = []
            for sec in elf.iter_sections():
                if isinstance(sec, SymbolTableSection):
                    for s in sec.iter_symbols():
                        if s["st_info"]["type"] in ("STT_FUNC", "STT_NOTYPE") and s.name and s["st_value"]:
                            syms.append((s["st_value"], s.name, s["st_size"]))
            self.entry = elf.header["e_entry"]
        syms.sort()
        self.symbols = syms
        self.sym_addrs = [a for a, _, _ in syms]
        self.byname = {n: a for a, n, _ in syms}

    def addr(self, name):
        return self.byname[name]

    def func(self, pc=None):
        pc = self.pc if pc is None else pc
        i = bisect_right(self.sym_addrs, pc) - 1
        return f"{self.symbols[i][1]}+{pc - self.symbols[i][0]:#x}" if i >= 0 else f"{pc:#x}"

    # --- registers -------------------------------------------------------

    def a(self, i):
        return self.ar[(self.wb4 + i) & 63]

    def set_a(self, i, v):
        self.ar[(self.wb4 + i) & 63] = v & M32

    @property
    def ps(self):
        return self.sr[PS]

    @ps.setter
    def ps(self, v):
        self.sr[PS] = v & M32

    def rsr(self, n):
        if n == LBEG: return self.lbeg
        if n == LEND: return self.lend
        if n == LCOUNT: return self.lcount
        if n == SAR: return self.sar
        if n == BR: return self.br
        if n == WINDOWBASE: return self.wb4 >> 2
        if n == WINDOWSTART: return self.ws
        if n == INTSET: return self.int_lines | self.int_latched
        if n == INTENABLE: return self.intenable
        if n == CCOUNT: return self.ccount & M32
        if 240 <= n <= 242: return self.ccompare[n - 240]
        return self.sr.get(n, 0)

    def wsr(self, n, v):
        v &= M32
        if n == LBEG: self.lbeg = v
        elif n == LEND: self.lend = v
        elif n == LCOUNT: self.lcount = v
        elif n == SAR: self.sar = v & 63
        elif n == BR: self.br = v & 0xffff
        elif n == WINDOWBASE: self.wb4 = (v & 15) << 2
        elif n == WINDOWSTART: self.ws = v & 0xffff
        elif n == INTSET: self.int_latched |= v
        elif n == INTCLEAR: self.int_latched &= ~v
        elif n == INTENABLE: self.intenable = v
        elif n == CCOUNT: self.ccount = v
        elif 240 <= n <= 242:
            self.ccompare[n - 240] = v
            self.int_latched &= ~(1 << TIMER_INT[n - 240])
        else: self.sr[n] = v

    # --- execution ---------------------------------------------------------

    def run(self, count=10_000_000):
        """Run up to count instructions; return why it stopped (or None)."""
        end = self.insns + count
        try:
            while self.insns < end:
                self.step()
        except Stop as e:
            return str(e)
        return None

    def step(self):
        pc = self.pc
        if pc in self.stops:
            raise Stop(self.stops[pc])
        stub = self.stubs.get(pc)
        if stub is not None:
            self._stub(stub)
            return
        if self.waiting:
            if not self._wake():
                raise Stop("waiti with nothing to wake it")
        self._interrupts()
        pc = self.pc
        op = self.cache.get(pc)
        if op is None:
            op = self.decode(pc)
        try:
            if op.fp and not self.sr[CPENABLE] & 1:
                raise XtException(CP0_DISABLED)
            if op.maxr >= 4 and (self.sr[PS] & (PS_WOE | PS_EXCM)) == PS_WOE:
                self._window_check(op.maxr)
            npc = op.fn(self, pc, op.length)
        except XtException as e:
            self._exception(e, pc)
            return
        self.insns += 1
        self.ccount += 1
        if npc is None:
            npc = pc + op.length
            if npc == self.lend and self.lcount and not self.sr[PS] & PS_EXCM:
                self.lcount -= 1
                npc = self.lbeg
        self.pc = npc & M32
        if self.trace:
            self.trace(self, pc, op)

    def _stub(self, fn):
        n = (self.sr[PS] >> PS_CALLINC_SHIFT) & 3
        base = 4 * n
        ret = fn(self)
        if ret is not None:
            self.set_a(base + 2, ret)
        ra = self.a(base)
        self.pc = (self.pc & 0xc0000000) | (ra & 0x3fffffff) if n else ra
        self.insns += 1

    def arg(self, i):
        """Argument i of a stubbed function (before its ENTRY)."""
        n = (self.sr[PS] >> PS_CALLINC_SHIFT) & 3
        return self.a(4 * n + 2 + i)

    # windows

    def _window_check(self, maxr):
        w = maxr >> 2
        wb = self.wb4 >> 2
        rot = ((self.ws | self.ws << 16) >> (wb + 1)) & ((1 << w) - 1)
        if not rot:
            return
        n = (rot & -rot).bit_length()               # windows to the live frame
        after = ((self.ws | self.ws << 16) >> (wb + 1 + n)) & 7
        kind = "overflow4" if after & 1 else "overflow8" if after & 2 else "overflow12"
        ps = self.sr[PS]
        self.sr[PS] = (ps & ~(15 << PS_OWB_SHIFT)) | wb << PS_OWB_SHIFT | PS_EXCM
        self.sr[EPC1] = self.pc
        self.wb4 = ((wb + n) & 15) << 2
        raise _Vector(VEC[kind])

    def _exception(self, e, pc):
        if isinstance(e, _Vector):
            self.pc = (self.sr[VECBASE] + e.offset) & M32
            return
        ps = self.sr[PS]
        if e.vaddr is not None:
            self.sr[EXCVADDR] = e.vaddr & M32
        if ps & PS_EXCM:
            self.sr[DEPC] = pc
            self.sr[EXCCAUSE] = e.cause
            self.pc = self.sr[VECBASE] + VEC["double"]
            return
        self.sr[EPC1] = pc
        self.sr[EXCCAUSE] = e.cause
        self.sr[PS] = ps | PS_EXCM
        self.pc = self.sr[VECBASE] + VEC["user" if ps & PS_UM else "kernel"]

    # interrupts

    def pending(self):
        return (self.int_lines | self.int_latched) & self.intenable

    def _timers(self):
        c = self.ccount & M32
        for i in range(3):
            # CCOMPARE matches when CCOUNT reaches it; checked each instruction
            if self.ccompare[i] == c:
                self.int_latched |= 1 << TIMER_INT[i]

    def _interrupts(self):
        self._timers()
        p = self.pending()
        if not p:
            return
        ps = self.sr[PS]
        cur = ps & PS_INTLEVEL
        if ps & PS_EXCM:
            cur = max(cur, EXCM_LEVEL)
        level = max(INTLEVEL[i] for i in range(32) if p >> i & 1)
        if level <= cur:
            return
        self.waiting = False
        pc = self.pc
        if level == 1:
            self.sr[EPC1] = pc
            self.sr[EXCCAUSE] = LEVEL1_INT
            self.sr[PS] = ps | PS_EXCM
            self.pc = self.sr[VECBASE] + VEC["user" if ps & PS_UM else "kernel"]
        else:
            self.sr[EPC1 + level - 1] = pc
            self.sr[EPS2 + level - 2] = ps
            self.sr[PS] = (ps & ~PS_INTLEVEL) | level | PS_EXCM
            self.pc = self.sr[VECBASE] + VEC[level]

    def _wake(self):
        """WAITI: jump CCOUNT to the next timer match if nothing is pending."""
        if self.pending():
            return True
        c = self.ccount & M32
        best = None
        for i in range(3):
            if self.intenable >> TIMER_INT[i] & 1:
                d = (self.ccompare[i] - c) & M32
                if d and (best is None or d < best):
                    best = d
        hook = getattr(self.bus, "next_event", None)
        if hook:
            d = hook(self)
            if d is not None and (best is None or d < best):
                best = d
        if best is None:
            return False
        self.ccount += best
        if hasattr(self.bus, "advance"):
            self.bus.advance(self, best)
        self._timers()
        return True

    # --- decoding --------------------------------------------------------

    def decode(self, pc):
        b0 = self.read(pc, 1)
        op0 = b0 & 15
        if op0 >= 14:
            data = self.read_bytes(pc, 8)
            op = self._bundle(data)
            op.length = 8
        elif op0 >= 8:
            op = decode16(self.read(pc, 2))
            op.length = 2
        else:
            op = decode24(self.read(pc, 3))
            op.length = 3
        self.cache[pc] = op
        self.cmin, self.cmax = min(self.cmin, pc), max(self.cmax, pc + 8)
        return op

    def _bundle(self, data):
        try:
            words = decode_bundle(data)
        except ValueError as e:
            raise RuntimeError(f"undecodable bundle at {self.pc:#x}: {data.hex()} ({e})")
        ops = []
        for w in words:
            if isinstance(w, tuple):
                ops.append(long_branch(*w[1:]))
            elif w & 15 >= 8:
                ops.append(decode16(w))
            else:
                ops.append(decode24(w))
        # all slots read the snapshot; SAR writes after the slots that read SAR,
        # the control transfer (which may rotate the window) last
        ops.sort(key=lambda o: (o.ctl, o.text.startswith("ss")))
        maxr = max((o.maxr for o in ops), default=0)

        def fn(cpu, pc, length):
            cpu.rd = list(cpu.ar)
            npc = None
            try:
                for o in ops:
                    r = o.fn(cpu, pc, length)
                    if r is not None:
                        npc = r
            finally:
                cpu.rd = cpu.ar
            return npc
        return Op(fn, maxr, any(o.ctl for o in ops), any(o.fp for o in ops),
                  "{" + "; ".join(o.text for o in ops) + "}")


class _Vector(XtException):
    def __init__(self, offset):
        super().__init__(-1)
        self.offset = offset


# Register access helpers used by the instruction functions.  Reads go
# through cpu.rd, which is cpu.ar except inside a bundle (a snapshot).

def R(c, i):
    return c.rd[(c.wb4 + i) & 63]


def W(c, i, v):
    c.ar[(c.wb4 + i) & 63] = v & M32


def unknown(w, what):
    def fn(c, pc, n):
        raise XtException(ILLEGAL)
    return Op(fn, 0, text=f"unknown {what} {w:#08x}")


def rrr(fn, r, s, t, text):
    return Op(lambda c, pc, n: W(c, r, fn(R(c, s), R(c, t))), max(r, s, t), text=text)


def decode24(w):
    op0, t, s, r = w & 15, (w >> 4) & 15, (w >> 8) & 15, (w >> 12) & 15
    op1, op2 = (w >> 16) & 15, (w >> 20) & 15
    imm8 = (w >> 16) & 0xff
    if op0 == 0:
        return qrst(w, op1, op2, r, s, t)
    if op0 == 1:                                       # L32R
        off = ((0xffff0000 | (w >> 8)) << 2) & M32

        def l32r(c, pc, n):
            W(c, t, c.read((((pc + 3) & ~3) + off) & M32, 4))
        return Op(l32r, t, text=f"l32r a{t}")
    if op0 == 2:
        return lsai(r, s, t, imm8)
    if op0 == 3:
        return lsci(r, s, t, imm8)
    if op0 == 5:                                       # CALLn
        n = (w >> 4) & 3
        off = sx(w >> 6, 18) << 2

        def call(c, pc, ln):
            tgt = ((pc & ~3) + off + 4) & M32
            if n == 0:
                W(c, 0, pc + ln)
            else:
                W(c, 4 * n, (n << 30) | ((pc + ln) & 0x3fffffff))
                c.sr[PS] = (c.sr[PS] & ~(3 << PS_CALLINC_SHIFT)) | n << PS_CALLINC_SHIFT
            return tgt
        o = Op(call, 4 * n, ctl=True, text=f"call{4 * n}")
        return o
    if op0 == 6:
        return si(w, r, s, t, imm8)
    if op0 == 7:
        return branch_b(r, s, t, sx(imm8, 8))
    return unknown(w, "op0")


def cond_branch(cond, off, maxr, text):
    def fn(c, pc, n):
        if cond(c):
            return (pc + 4 + off) & M32
    return Op(fn, maxr, ctl=True, text=text)


def branch_b(r, s, t, off):
    S = lambda c: R(c, s)
    T = lambda c: R(c, t)
    if r in (6, 7, 14, 15):
        bit = ((r & 1) << 4) | t
        if r < 8:
            return cond_branch(lambda c: not (S(c) >> bit) & 1, off, s, f"bbci a{s},{bit}")
        return cond_branch(lambda c: (S(c) >> bit) & 1, off, s, f"bbsi a{s},{bit}")
    table = {
        0: lambda c: not (S(c) & T(c)),
        1: lambda c: S(c) == T(c),
        2: lambda c: s32(S(c)) < s32(T(c)),
        3: lambda c: S(c) < T(c),
        4: lambda c: not (~S(c) & T(c) & M32),
        5: lambda c: not (S(c) >> (T(c) & 31)) & 1,
        8: lambda c: bool(S(c) & T(c)),
        9: lambda c: S(c) != T(c),
        10: lambda c: s32(S(c)) >= s32(T(c)),
        11: lambda c: S(c) >= T(c),
        12: lambda c: bool(~S(c) & T(c) & M32),
        13: lambda c: (S(c) >> (T(c) & 31)) & 1,
    }
    names = {0: "bnone", 1: "beq", 2: "blt", 3: "bltu", 4: "ball", 5: "bbc", 8: "bany", 9: "bne",
             10: "bge", 11: "bgeu", 12: "bnall", 13: "bbs"}
    return cond_branch(table[r], off, max(s, t), f"{names[r]} a{s},a{t}")


def long_branch(m, s, operand, off):
    """A FLIX branch whose offset does not fit the core encoding."""
    S = lambda c: R(c, s)
    o = operand
    tests = {
        "beqz": lambda c: S(c) == 0, "bnez": lambda c: S(c) != 0,
        "bltz": lambda c: S(c) & 0x80000000, "bgez": lambda c: not S(c) & 0x80000000,
        "beqi": lambda c: s32(S(c)) == o, "bnei": lambda c: s32(S(c)) != o,
        "blti": lambda c: s32(S(c)) < o, "bgei": lambda c: s32(S(c)) >= o,
        "bltui": lambda c: S(c) < o, "bgeui": lambda c: S(c) >= o,
        "bbci": lambda c: not (S(c) >> o) & 1, "bbsi": lambda c: (S(c) >> o) & 1,
    }
    if m in tests:
        maxr = s
        cond = tests[m]
    else:
        T = lambda c: R(c, o)
        cond = {
            "bnone": lambda c: not (S(c) & T(c)), "beq": lambda c: S(c) == T(c),
            "blt": lambda c: s32(S(c)) < s32(T(c)), "bltu": lambda c: S(c) < T(c),
            "ball": lambda c: not (~S(c) & T(c) & M32), "bany": lambda c: bool(S(c) & T(c)),
            "bne": lambda c: S(c) != T(c), "bge": lambda c: s32(S(c)) >= s32(T(c)),
            "bgeu": lambda c: S(c) >= T(c), "bnall": lambda c: bool(~S(c) & T(c) & M32),
            "bbc": lambda c: not (S(c) >> (T(c) & 31)) & 1, "bbs": lambda c: (S(c) >> (T(c) & 31)) & 1,
        }[m]
        maxr = max(s, o)
    return cond_branch(cond, off, maxr, f"{m} (long)")


def si(w, r, s, t, imm8):
    n, m = (w >> 4) & 3, (w >> 6) & 3
    if n == 0:                                         # J
        off = sx(w >> 6, 18)
        return Op(lambda c, pc, ln: (pc + 4 + off) & M32, 0, ctl=True, text="j")
    if n == 1:                                         # BZ
        off = sx(w >> 12, 12)
        cond = (lambda c: R(c, s) == 0, lambda c: R(c, s) != 0,
                lambda c: R(c, s) & 0x80000000, lambda c: not R(c, s) & 0x80000000)[m]
        return cond_branch(cond, off, s, ("beqz", "bnez", "bltz", "bgez")[m] + f" a{s}")
    off = sx(imm8, 8)
    if n == 2:                                         # BI0
        k = B4CONST[r]
        cond = (lambda c: s32(R(c, s)) == k, lambda c: s32(R(c, s)) != k,
                lambda c: s32(R(c, s)) < k, lambda c: s32(R(c, s)) >= k)[m]
        return cond_branch(cond, off, s, ("beqi", "bnei", "blti", "bgei")[m] + f" a{s},{k}")
    if m == 0:                                         # ENTRY
        frame = (w >> 12) << 3

        def entry(c, pc, ln):
            inc = (c.sr[PS] >> PS_CALLINC_SHIFT) & 3
            if inc and (c.sr[PS] & (PS_WOE | PS_EXCM)) == PS_WOE:
                c._window_check(4 * inc + s)
            v = (R(c, s) - frame) & M32
            W(c, 4 * inc + s, v)
            c.wb4 = (c.wb4 + 4 * inc) & 63
            c.ws |= 1 << (c.wb4 >> 2)
        return Op(entry, 0, ctl=True, text=f"entry a{s},{frame}")
    if m == 1:
        if r in (0, 1):                                # BF, BT
            want = r
            return cond_branch(lambda c: ((c.br >> s) & 1) == want, off, 0, f"b{'ft'[r]} b{s}")
        if r in (8, 9, 10):                            # LOOP, LOOPNEZ, LOOPGTZ
            kind = r

            def loop(c, pc, ln):
                cnt = R(c, s)
                c.lbeg = (pc + ln) & M32
                c.lend = (pc + 4 + imm8) & M32
                c.lcount = (cnt - 1) & M32
                if (kind == 9 and cnt == 0) or (kind == 10 and s32(cnt) <= 0):
                    c.lcount = 0
                    return c.lend
            return Op(loop, s, ctl=True, text=("loop", "loopnez", "loopgtz")[r - 8] + f" a{s}")
        return unknown(w, "B1")
    k = B4CONSTU[r]
    if m == 2:
        return cond_branch(lambda c: R(c, s) < k, off, s, f"bltui a{s},{k}")
    return cond_branch(lambda c: R(c, s) >= k, off, s, f"bgeui a{s},{k}")


def lsai(r, s, t, imm8):
    S = s
    if r == 0:
        return Op(lambda c, pc, n: W(c, t, c.read(R(c, S) + imm8, 1)), max(s, t), text="l8ui")
    if r == 1:
        return Op(lambda c, pc, n: W(c, t, c.read((R(c, S) + 2 * imm8) & ~1, 2)), max(s, t), text="l16ui")
    if r == 2:
        return Op(lambda c, pc, n: W(c, t, c.read((R(c, S) + 4 * imm8) & ~3, 4)), max(s, t), text="l32i")
    if r == 9:
        return Op(lambda c, pc, n: W(c, t, sx(c.read((R(c, S) + 2 * imm8) & ~1, 2), 16)), max(s, t),
                  text="l16si")
    if r == 4:
        return Op(lambda c, pc, n: c.write(R(c, S) + imm8, 1, R(c, t)), max(s, t), text="s8i")
    if r == 5:
        return Op(lambda c, pc, n: c.write((R(c, S) + 2 * imm8) & ~1, 2, R(c, t)), max(s, t), text="s16i")
    if r == 6:
        return Op(lambda c, pc, n: c.write((R(c, S) + 4 * imm8) & ~3, 4, R(c, t)), max(s, t), text="s32i")
    if r == 7:                                         # cache operations
        return Op(lambda c, pc, n: None, s, text="cache")
    if r == 10:
        v = sx((s << 8) | imm8, 12) & M32
        return Op(lambda c, pc, n: W(c, t, v), t, text=f"movi a{t},{s32(v)}")
    if r == 11:
        return Op(lambda c, pc, n: W(c, t, c.read((R(c, S) + 4 * imm8) & ~3, 4)), max(s, t), text="l32ai")
    if r == 12:
        v = sx(imm8, 8)
        return Op(lambda c, pc, n: W(c, t, R(c, S) + v), max(s, t), text=f"addi a{t},a{s},{v}")
    if r == 13:
        v = sx(imm8, 8) << 8
        return Op(lambda c, pc, n: W(c, t, R(c, S) + v), max(s, t), text=f"addmi a{t},a{s},{v}")
    if r == 15:
        return Op(lambda c, pc, n: c.write((R(c, S) + 4 * imm8) & ~3, 4, R(c, t)), max(s, t), text="s32ri")
    return unknown(r, "LSAI")


def lsci(r, s, t, imm8):
    off = 4 * imm8

    def lsi(c, pc, n):
        c.fr[t] = c.read((R(c, s) + off) & ~3, 4)

    def ssi(c, pc, n):
        c.write((R(c, s) + off) & ~3, 4, c.fr[t])

    def lsip(c, pc, n):
        a = R(c, s)
        c.fr[t] = c.read(a & ~3, 4)
        W(c, s, a + off)

    def ssip(c, pc, n):
        a = R(c, s)
        c.write(a & ~3, 4, c.fr[t])
        W(c, s, a + off)
    doff = 8 * imm8

    def ldi(c, pc, n):
        c.fr[t] = c.read((R(c, s) + doff) & ~7, 8)

    def sdi(c, pc, n):
        c.write((R(c, s) + doff) & ~7, 8, c.fr[t])

    def ldip(c, pc, n):
        a = R(c, s)
        c.fr[t] = c.read(a & ~7, 8)
        W(c, s, a + doff)

    def sdip(c, pc, n):
        a = R(c, s)
        c.write(a & ~7, 8, c.fr[t])
        W(c, s, a + doff)
    fn = {0: lsi, 4: ssi, 8: lsip, 12: ssip, 1: ldi, 5: sdi, 9: ldip, 13: sdip}.get(r)
    if fn is None:
        return unknown(r, "LSCI")
    return Op(fn, s, fp=True, text={0: "lsi", 4: "ssi", 8: "lsip", 12: "ssip", 1: "ldi", 5: "sdi",
                                    9: "ldip", 13: "sdip"}[r])


def qrst(w, op1, op2, r, s, t):
    if op1 == 0:
        return rst0(w, op2, r, s, t)
    if op1 == 1:
        return rst1(w, op2, r, s, t)
    if op1 == 2:
        return rst2(w, op2, r, s, t)
    if op1 == 3:
        return rst3(w, op2, r, s, t)
    if op1 in (4, 5):                                  # EXTUI
        sh = ((op1 & 1) << 4) | s
        mask = (1 << (op2 + 1)) - 1
        return Op(lambda c, pc, n: W(c, r, (R(c, t) >> sh) & mask), max(r, t), text=f"extui a{r},a{t},{sh},{op2 + 1}")
    if op1 == 8:
        return fp_lsx(op2, r, s, t)
    if op1 == 9:                                       # L32E / S32E
        off = (r << 2) - 64
        if op2 == 0:
            return Op(lambda c, pc, n: W(c, t, c.read((R(c, s) + off) & ~3, 4)), max(s, t), text="l32e")
        if op2 == 4:
            return Op(lambda c, pc, n: c.write((R(c, s) + off) & ~3, 4, R(c, t)), max(s, t), text="s32e")
    if op1 == 10:
        return fp0(w, op2, r, s, t)
    if op1 == 11:
        return fp1(w, op2, r, s, t)
    if op1 == 14:
        return dfp1(w, op2, r, s, t)
    if op1 == 15:
        return dfp0(w, op2, r, s, t)
    return unknown(w, f"QRST op1={op1}")


def rst0(w, op2, r, s, t):
    if op2 == 0:
        return st0(w, r, s, t)
    if op2 in (1, 2, 3):
        f = {1: lambda a, b: a & b, 2: lambda a, b: a | b, 3: lambda a, b: a ^ b}[op2]
        return rrr(f, r, s, t, ("and", "or", "xor")[op2 - 1] + f" a{r},a{s},a{t}")
    if op2 == 4:
        return st1(w, r, s, t)
    if op2 == 5:                                       # TLB / MPU: no effect here
        if r in (3, 7, 11, 15):                        # reads return 0
            return Op(lambda c, pc, n: W(c, t, 0), t, text="rxtlb")
        return Op(lambda c, pc, n: None, max(s, t), text="xtlb")
    if op2 == 6:
        if s == 0:
            return Op(lambda c, pc, n: W(c, r, -R(c, t)), max(r, t), text="neg")
        if s == 1:
            return Op(lambda c, pc, n: W(c, r, abs(s32(R(c, t)))), max(r, t), text="abs")
    if op2 >= 8:
        sh = op2 & 3
        if op2 < 12:
            return rrr(lambda a, b: (a << sh) + b, r, s, t, f"addx{1 << sh}")
        return rrr(lambda a, b: (a << sh) - b, r, s, t, f"subx{1 << sh}")
    return unknown(w, "RST0")


def st0(w, r, s, t):
    if r == 0:
        if t == 0:
            return unknown(w, "ill")
        if t == 8:                                     # RET
            return Op(lambda c, pc, n: R(c, 0), 0, ctl=True, text="ret")
        if t == 9:
            return Op(retw, 0, ctl=True, text="retw")
        if t == 10:
            return Op(lambda c, pc, n: R(c, s), s, ctl=True, text=f"jx a{s}")
        if t >= 12:
            k = t - 12

            def callx(c, pc, ln):
                tgt = R(c, s)
                if k == 0:
                    W(c, 0, pc + ln)
                else:
                    W(c, 4 * k, (k << 30) | ((pc + ln) & 0x3fffffff))
                    c.sr[PS] = (c.sr[PS] & ~(3 << PS_CALLINC_SHIFT)) | k << PS_CALLINC_SHIFT
                return tgt
            return Op(callx, max(s, 4 * k), ctl=True, text=f"callx{4 * k} a{s}")
    if r == 1:                                         # MOVSP at, as
        def movsp(c, pc, n):
            wb = c.wb4 >> 2
            if not any((c.ws >> ((wb - k) & 15)) & 1 for k in (1, 2, 3)):
                raise XtException(ALLOCA)
            W(c, t, R(c, s))
        return Op(movsp, max(s, t), ctl=True, text="movsp")
    if r == 2:                                         # sync group, NOP
        return Op(lambda c, pc, n: None, 0, text={12: "memw", 15: "nop"}.get(t, "sync"))
    if r == 3:
        if t == 0:
            if s == 0:
                def rfe(c, pc, n):
                    c.sr[PS] &= ~PS_EXCM
                    return c.sr[EPC1]
                return Op(rfe, 0, ctl=True, text="rfe")
            if s == 2:
                def rfde(c, pc, n):
                    c.sr[PS] &= ~PS_EXCM
                    return c.sr[DEPC]
                return Op(rfde, 0, ctl=True, text="rfde")
            if s in (4, 5):
                def rfw(c, pc, n):
                    if s == 4:
                        c.ws &= ~(1 << (c.wb4 >> 2))
                    else:
                        c.ws |= 1 << (c.wb4 >> 2)
                    ps = c.sr[PS]
                    c.wb4 = ((ps >> PS_OWB_SHIFT) & 15) << 2
                    c.sr[PS] = ps & ~PS_EXCM
                    return c.sr[EPC1]
                return Op(rfw, 0, ctl=True, text=("rfwo", "rfwu")[s - 4])
        if t == 1:                                     # RFI level
            lvl = s

            def rfi(c, pc, n):
                c.sr[PS] = c.sr.get(EPS2 + lvl - 2, 0)
                return c.sr.get(EPC1 + lvl - 1, 0)
            return Op(rfi, 0, ctl=True, text=f"rfi {s}")
    if r == 4:
        def brk(c, pc, n):
            raise Stop(f"break {s},{t} at {c.func(pc)}")
        return Op(brk, 0, text="break")
    if r == 5:
        if s == 0:
            def syscall(c, pc, n):
                raise XtException(SYSCALL)
            return Op(syscall, 0, text="syscall")

        def simcall(c, pc, n):
            raise Stop(f"simcall at {c.func(pc)}")
        return Op(simcall, 0, text="simcall")
    if r == 6:                                         # RSIL at, level
        def rsil(c, pc, n):
            ps = c.sr[PS]
            W(c, t, ps)
            c.sr[PS] = (ps & ~PS_INTLEVEL) | s
        return Op(rsil, t, text=f"rsil a{t},{s}")
    if r == 7:                                         # WAITI
        def waiti(c, pc, n):
            c.sr[PS] = (c.sr[PS] & ~PS_INTLEVEL) | s
            c.waiting = True
        return Op(waiti, 0, text=f"waiti {s}")
    if r in (8, 9, 10, 11):                            # ANY4 ALL4 ANY8 ALL8
        width = 4 if r < 10 else 8
        want_all = r in (9, 11)
        mask = (1 << width) - 1

        def anyall(c, pc, n):
            v = (c.br >> s) & mask
            res = (v == mask) if want_all else bool(v)
            c.br = (c.br & ~(1 << t)) | (int(res) << t)
        return Op(anyall, 0, text="anyall")
    return unknown(w, "ST0")


def retw(c, pc, n):
    a0 = R(c, 0)
    k = a0 >> 30
    if k == 0:
        raise XtException(ILLEGAL)
    owb = c.wb4 >> 2
    nwb = (owb - k) & 15
    if (c.ws >> nwb) & 1:
        c.ws &= ~(1 << owb)
        c.wb4 = nwb << 2
        return (pc & 0xc0000000) | (a0 & 0x3fffffff)
    # window underflow: the caller's frame is on the stack
    ps = c.sr[PS]
    c.sr[PS] = (ps & ~(15 << PS_OWB_SHIFT)) | owb << PS_OWB_SHIFT | PS_EXCM
    c.sr[EPC1] = pc
    c.wb4 = nwb << 2
    return (c.sr[VECBASE] + VEC[("underflow4", "underflow8", "underflow12")[k - 1]]) & M32


def st1(w, r, s, t):
    if r == 0:
        return Op(lambda c, pc, n: setattr(c, "sar", R(c, s) & 31), s, text="ssr")
    if r == 1:
        return Op(lambda c, pc, n: setattr(c, "sar", 32 - (R(c, s) & 31)), s, text="ssl")
    if r == 2:
        return Op(lambda c, pc, n: setattr(c, "sar", (R(c, s) & 3) << 3), s, text="ssa8l")
    if r == 3:
        return Op(lambda c, pc, n: setattr(c, "sar", 32 - ((R(c, s) & 3) << 3)), s, text="ssa8b")
    if r == 4:
        v = s | ((t & 1) << 4)
        return Op(lambda c, pc, n: setattr(c, "sar", v), 0, text=f"ssai {v}")
    if r == 10:                                        # GETEX at
        def getex(c, pc, n):
            v = R(c, t)
            W(c, t, (v & ~1) | c.exclusive)
            c.exclusive = v & 1
        return Op(getex, t, text=f"getex a{t}")
    if r == 8:                                         # ROTW
        k = sx(t, 4)

        def rotw(c, pc, n):
            c.wb4 = (c.wb4 + 4 * k) & 63
        return Op(rotw, 0, ctl=True, text=f"rotw {k}")
    if r in (14, 15):
        def nsa(v):
            v = s32(v)
            if v < 0:
                v = ~v
            return 31 if v == 0 else 31 - v.bit_length()

        def nsau(v):
            return 32 - v.bit_length() if v else 32
        f = nsa if r == 14 else nsau
        return Op(lambda c, pc, n: W(c, t, f(R(c, s))), max(s, t), text=("nsa", "nsau")[r - 14])
    return unknown(w, "ST1")


def rst1(w, op2, r, s, t):
    if op2 in (0, 1):                                  # SLLI
        sh = 32 - (((op2 & 1) << 4) | t)
        return Op(lambda c, pc, n: W(c, r, R(c, s) << sh), max(r, s), text=f"slli a{r},a{s},{sh}")
    if op2 in (2, 3):                                  # SRAI
        sh = ((op2 & 1) << 4) | s
        return Op(lambda c, pc, n: W(c, r, s32(R(c, t)) >> sh), max(r, t), text=f"srai a{r},a{t},{sh}")
    if op2 == 4:
        return Op(lambda c, pc, n: W(c, r, R(c, t) >> s), max(r, t), text=f"srli a{r},a{t},{s}")
    if op2 == 6:                                       # XSR
        sr = (r << 4) | s

        def xsr(c, pc, n):
            old = c.rsr(sr)
            c.wsr(sr, R(c, t))
            W(c, t, old)
        return Op(xsr, t, ctl=True, text=f"xsr {sr}")
    if op2 == 8:
        return Op(lambda c, pc, n: W(c, r, ((R(c, s) << 32 | R(c, t)) >> c.sar)), max(r, s, t), text="src")
    if op2 == 9:
        return Op(lambda c, pc, n: W(c, r, R(c, t) >> c.sar), max(r, t), text="srl")
    if op2 == 10:
        return Op(lambda c, pc, n: W(c, r, (R(c, s) << 32) >> c.sar), max(r, s), text="sll")
    if op2 == 11:
        return Op(lambda c, pc, n: W(c, r, s32(R(c, t)) >> c.sar), max(r, t), text="sra")
    if op2 == 12:
        return rrr(lambda a, b: (a & 0xffff) * (b & 0xffff), r, s, t, "mul16u")
    if op2 == 13:
        return rrr(lambda a, b: sx(a, 16) * sx(b, 16), r, s, t, "mul16s")
    if op2 == 15:                                      # IMP (cache tag access): nothing here
        return Op(lambda c, pc, n: None, 0, text="imp")
    return unknown(w, "RST1")


def _div(f):
    def g(a, b):
        if b == 0:
            raise XtException(DIVIDE_BY_ZERO)
        return f(a, b)
    return g


def _quos(a, b):
    a, b = s32(a), s32(b)
    q = abs(a) // abs(b)
    return -q if (a < 0) != (b < 0) else q


def _rems(a, b):
    a, b = s32(a), s32(b)
    r = abs(a) % abs(b)
    return -r if a < 0 else r


def rst2(w, op2, r, s, t):
    if op2 <= 4:                                       # boolean ops on br
        f = (lambda a, b: a & b, lambda a, b: a & ~b, lambda a, b: a | b,
             lambda a, b: a | (~b & 1), lambda a, b: a ^ b)[op2]

        def bop(c, pc, n):
            v = f((c.br >> s) & 1, (c.br >> t) & 1) & 1
            c.br = (c.br & ~(1 << r)) | (v << r)
        return Op(bop, 0, text="boolop")
    table = {
        6: (lambda a, b: int(a < b), "saltu"),
        7: (lambda a, b: int(s32(a) < s32(b)), "salt"),
        8: (lambda a, b: a * b, "mull"),
        10: (lambda a, b: (a * b) >> 32, "muluh"),
        11: (lambda a, b: (s32(a) * s32(b)) >> 32, "mulsh"),
        12: (_div(lambda a, b: a // b), "quou"),
        13: (_div(_quos), "quos"),
        14: (_div(lambda a, b: a % b), "remu"),
        15: (_div(_rems), "rems"),
    }
    if op2 in table:
        f, name = table[op2]
        return rrr(f, r, s, t, f"{name} a{r},a{s},a{t}")
    return unknown(w, "RST2")


def rst3(w, op2, r, s, t):
    if op2 == 0:                                       # RSR at, sr
        sr = (r << 4) | s

        def rsr(c, pc, n):
            if sr == CCOUNT:
                W(c, t, c.ccount)
            else:
                W(c, t, c.rsr(sr))
        return Op(rsr, t, text=f"rsr a{t},{sr}")
    if op2 == 1:
        sr = (r << 4) | s
        ctl = sr in (WINDOWBASE, WINDOWSTART, PS, LEND, LBEG, LCOUNT)
        return Op(lambda c, pc, n: c.wsr(sr, R(c, t)), t, ctl=ctl, text=f"wsr a{t},{sr}")
    if op2 == 2:                                       # SEXT
        b = t + 8
        return Op(lambda c, pc, n: W(c, r, sx(R(c, s), b)), max(r, s), text=f"sext a{r},a{s},{t + 7}")
    if op2 in (4, 5, 6, 7):
        f = (lambda a, b: a if s32(a) < s32(b) else b, lambda a, b: a if s32(a) > s32(b) else b,
             lambda a, b: min(a, b), lambda a, b: max(a, b))[op2 - 4]
        return rrr(f, r, s, t, ("min", "max", "minu", "maxu")[op2 - 4])
    if op2 in (8, 9, 10, 11):
        cond = (lambda v: v == 0, lambda v: v != 0, lambda v: v & 0x80000000,
                lambda v: not v & 0x80000000)[op2 - 8]

        def mov(c, pc, n):
            if cond(R(c, t)):
                W(c, r, R(c, s))
        return Op(mov, max(r, s, t), text=("moveqz", "movnez", "movltz", "movgez")[op2 - 8])
    if op2 in (12, 13):
        want = op2 - 12

        def movb(c, pc, n):
            if ((c.br >> t) & 1) == want:
                W(c, r, R(c, s))
        return Op(movb, max(r, s), text=("movf", "movt")[want])
    if op2 == 14:                                      # RUR ar, ur
        ur = (s << 4) | t
        return Op(lambda c, pc, n: W(c, r, c.ur.get(ur, 0)), r, fp=ur in (FCR, FSR), text=f"rur {ur}")
    if op2 == 15:                                      # WUR at, ur
        ur = (r << 4) | s
        return Op(lambda c, pc, n: c.ur.__setitem__(ur, R(c, t)), t, fp=ur in (FCR, FSR), text=f"wur {ur}")
    return unknown(w, "RST3")


# --- floating point (single precision, round to nearest) -------------------

def fp_lsx(op2, r, s, t):
    def lsx(c, pc, n):
        c.fr[r] = c.read((R(c, s) + R(c, t)) & ~3, 4)

    def lsxp(c, pc, n):
        a = R(c, s)
        c.fr[r] = c.read(a & ~3, 4)
        W(c, s, a + R(c, t))

    def ssx(c, pc, n):
        c.write((R(c, s) + R(c, t)) & ~3, 4, c.fr[r])

    def ssxp(c, pc, n):
        a = R(c, s)
        c.write(a & ~3, 4, c.fr[r])
        W(c, s, a + R(c, t))
    def ldx(c, pc, n):
        c.fr[r] = c.read((R(c, s) + R(c, t)) & ~7, 8)

    def ldxp(c, pc, n):
        a = R(c, s)
        c.fr[r] = c.read(a & ~7, 8)
        W(c, s, a + R(c, t))

    def sdx(c, pc, n):
        c.write((R(c, s) + R(c, t)) & ~7, 8, c.fr[r])

    def sdxp(c, pc, n):
        a = R(c, s)
        c.write(a & ~7, 8, c.fr[r])
        W(c, s, a + R(c, t))
    fn = {0: lsx, 1: lsxp, 4: ssx, 5: ssxp, 2: ldx, 3: ldxp, 6: sdx, 7: sdxp}.get(op2)
    if fn is None:
        return unknown(op2, "LSCX")
    return Op(fn, max(s, t), fp=True, text="lsx")


def _fop(fn, text):
    return Op(fn, 0, fp=True, text=text)


def _to_int(f, signed):
    import math
    if f != f:                                         # NaN
        return 0x7fffffff if signed else 0xffffffff
    lo, hi = (-2 ** 31, 2 ** 31 - 1) if signed else (0, 2 ** 32 - 1)
    if f == math.inf or f > hi:
        return hi & M32
    if f == -math.inf or f < lo:
        return lo & M32
    return int(f) & M32


def fp0(w, op2, r, s, t):
    F = lambda c, i: u2f(c.fr[i])                      # low 32 bits
    if op2 in (0, 1, 2):
        f = (lambda a, b: a + b, lambda a, b: a - b, lambda a, b: a * b)[op2]

        def arith(c, pc, n):
            c.fr[r] = f2u(f(F(c, s), F(c, t)))
        return _fop(arith, ("add.s", "sub.s", "mul.s")[op2])
    if op2 in (4, 5, 6):                               # MADD.S, MSUB.S, MADDN.S
        sign = -1 if op2 == 5 else 1

        def madd(c, pc, n):
            c.fr[r] = f2u(F(c, r) + sign * F(c, s) * F(c, t))
        return _fop(madd, ("madd.s", "msub.s", "maddn.s")[op2 - 4])
    if op2 == 7:                                       # DIVN.S: the quotient of the sequence
        def divn(c, pc, n):
            d = c.div
            if d and d["b"] != 0:
                a = d["a"] if d["a"] is not None else d["b"]
                c.fr[r] = f2u(a / d["b"])
            else:
                c.fr[r] = 0x7fc00000
            c.div = None
        return _fop(divn, "divn.s")
    if op2 in (8, 9, 10, 11, 14):                      # ROUND/TRUNC/FLOOR/CEIL/UTRUNC.S ar, fs, scale
        import math
        rnd = {8: lambda x: float(round(x)), 9: math.trunc, 10: math.floor, 11: math.ceil, 14: math.trunc}[op2]

        def cvt(c, pc, n):
            x = F(c, s) * (1 << t)
            if x != x or x in (math.inf, -math.inf):
                W(c, r, _to_int(x, op2 != 14))
            else:
                W(c, r, _to_int(float(rnd(x)), op2 != 14))
        o = Op(cvt, r, fp=True, text="cvt.s")
        return o
    if op2 in (12, 13):                                # FLOAT.S / UFLOAT.S fr, as, scale
        def flt(c, pc, n):
            v = R(c, s)
            v = s32(v) if op2 == 12 else v
            c.fr[r] = f2u(v / (1 << t))
        return Op(flt, s, fp=True, text=("float.s", "ufloat.s")[op2 - 12])
    if op2 == 15:
        return fp1op(w, r, s, t)
    return unknown(w, "FP0")


def fp1op(w, r, s, t):
    if t == 0:
        return _fop(lambda c, pc, n: c.fr.__setitem__(r, c.fr[s]), "mov.s")
    if t == 1:
        return _fop(lambda c, pc, n: c.fr.__setitem__(r, c.fr[s] & 0x7fffffff), "abs.s")
    if t == 2:                                         # CVTD.S fr(double), fs(single)
        return _fop(lambda c, pc, n: c.fr.__setitem__(r, d2u(u2f(c.fr[s]))), "cvtd.s")
    if t == 3:                                         # CONST.S
        consts = {0: 0x00000000, 1: 0x3f800000, 2: 0x40000000, 3: 0x3f000000}
        v = consts.get(s, 0)
        return _fop(lambda c, pc, n: c.fr.__setitem__(r, v), "const.s")
    if t == 4:                                         # RFR ar, fs
        return Op(lambda c, pc, n: W(c, r, c.fr[s] & M32), r, fp=True, text="rfr")
    if t == 5:                                         # WFR fr, as
        return Op(lambda c, pc, n: c.fr.__setitem__(r, R(c, s)), s, fp=True, text="wfr")
    if t == 6:
        return _fop(lambda c, pc, n: c.fr.__setitem__(r, (c.fr[s] & M32) ^ 0x80000000), "neg.s")
    if t == 7:                                         # DIV0.S: start of a divide by fs
        def div0(c, pc, n):
            b = u2f(c.fr[s])
            c.div = {"b": b, "breg": s, "a": None}
            c.fr[r] = f2u(1.0 / b) if b else 0x7f800000
        return _fop(div0, "div0.s")
    if t == 11:                                        # NEXP01.S: the dividend's turn sets it
        def nexp01(c, pc, n):
            if c.div is not None and s != c.div["breg"]:
                c.div["a"] = u2f(c.fr[s])
            c.fr[r] = c.fr[s]
        return _fop(nexp01, "nexp01.s")
    if t in (12, 13, 14, 15):                          # MKSADJ/MKDADJ/ADDEXP/ADDEXPM: no-ops here
        return _fop(lambda c, pc, n: None, "fpadj")
    return unknown(w, "FP1OP")


def fp1(w, op2, r, s, t):
    F = lambda c, i: u2f(c.fr[i])
    if 1 <= op2 <= 7:
        def cmp(a, b):
            un = a != a or b != b
            return {1: un, 2: not un and a == b, 3: un or a == b, 4: not un and a < b,
                    5: un or a < b, 6: not un and a <= b, 7: un or a <= b}[op2]

        def fcmp(c, pc, n):
            v = int(cmp(F(c, s), F(c, t)))
            c.br = (c.br & ~(1 << r)) | (v << r)
        return _fop(fcmp, "fcmp")
    if 8 <= op2 <= 11:
        cond = (lambda v: v == 0, lambda v: v != 0, lambda v: v & 0x80000000,
                lambda v: not v & 0x80000000)[op2 - 8]

        def fmov(c, pc, n):
            if cond(R(c, t)):
                c.fr[r] = c.fr[s]
        return Op(fmov, t, fp=True, text="movxz.s")
    if op2 in (12, 13):
        want = op2 - 12

        def fmovb(c, pc, n):
            if ((c.br >> t) & 1) == want:
                c.fr[r] = c.fr[s]
        return _fop(fmovb, "movb.s")
    return unknown(w, "FP1")


# --- density ------------------------------------------------------------

def decode16(w):
    op0, t, s, r = w & 15, (w >> 4) & 15, (w >> 8) & 15, (w >> 12) & 15
    if op0 == 8:
        return Op(lambda c, pc, n: W(c, t, c.read((R(c, s) + 4 * r) & ~3, 4)), max(s, t), text="l32i.n")
    if op0 == 9:
        return Op(lambda c, pc, n: c.write((R(c, s) + 4 * r) & ~3, 4, R(c, t)), max(s, t), text="s32i.n")
    if op0 == 10:
        return rrr(lambda a, b: a + b, r, s, t, f"add.n a{r},a{s},a{t}")
    if op0 == 11:
        v = -1 if t == 0 else t
        return Op(lambda c, pc, n: W(c, r, R(c, s) + v), max(r, s), text=f"addi.n a{r},a{s},{v}")
    if op0 == 12:
        if not t & 8:                                  # MOVI.N
            v = ((t & 7) << 4) | r
            if v >= 96:
                v -= 128
            return Op(lambda c, pc, n: W(c, s, v), s, text=f"movi.n a{s},{v}")
        off = ((t & 3) << 4) | r
        if t & 4:
            return cond_branch(lambda c: R(c, s) != 0, off, s, f"bnez.n a{s}")
        return cond_branch(lambda c: R(c, s) == 0, off, s, f"beqz.n a{s}")
    if op0 == 13:
        if r == 0:
            return Op(lambda c, pc, n: W(c, t, R(c, s)), max(s, t), text=f"mov.n a{t},a{s}")
        if r == 15:
            if t == 0:
                return Op(lambda c, pc, n: R(c, 0), 0, ctl=True, text="ret.n")
            if t == 1:
                return Op(retw, 0, ctl=True, text="retw.n")
            if t == 2:
                def brk(c, pc, n):
                    raise Stop(f"break.n {s} at {c.func(pc)}")
                return Op(brk, 0, text="break.n")
            if t == 3:
                return Op(lambda c, pc, n: None, 0, text="nop.n")
    return unknown(w, "density")


# --- double precision (op1 15: arithmetic, op1 14: compares and WFRD) --------

def dfp0(w, op2, r, s, t):
    D = lambda c, i: u2d(c.fr[i])
    if op2 in (0, 1, 2):
        f = (lambda a, b: a + b, lambda a, b: a - b, lambda a, b: a * b)[op2]
        return _fop(lambda c, pc, n: c.fr.__setitem__(r, d2u(f(D(c, s), D(c, t)))),
                    ("add.d", "sub.d", "mul.d")[op2])
    if op2 in (4, 5, 6):
        sign = -1 if op2 == 5 else 1
        return _fop(lambda c, pc, n: c.fr.__setitem__(r, d2u(D(c, r) + sign * D(c, s) * D(c, t))),
                    ("madd.d", "msub.d", "maddn.d")[op2 - 4])
    if op2 == 7:
        def divn(c, pc, n):
            d = c.div
            if d and d["b"] != 0:
                a = d["a"] if d["a"] is not None else d["b"]
                c.fr[r] = d2u(a / d["b"])
            else:
                c.fr[r] = 0x7ff8000000000000
            c.div = None
        return _fop(divn, "divn.d")
    if op2 in (8, 9, 10, 11, 14):
        import math
        rnd = {8: lambda x: float(round(x)), 9: math.trunc, 10: math.floor, 11: math.ceil, 14: math.trunc}[op2]

        def cvt(c, pc, n):
            x = D(c, s) * (1 << t)
            if x != x or x in (math.inf, -math.inf):
                W(c, r, _to_int(x, op2 != 14))
            else:
                W(c, r, _to_int(float(rnd(x)), op2 != 14))
        return Op(cvt, r, fp=True, text="cvt.d")
    if op2 in (12, 13):
        def flt(c, pc, n):
            v = R(c, s)
            v = s32(v) if op2 == 12 else v
            c.fr[r] = d2u(v / (1 << t))
        return Op(flt, s, fp=True, text=("float.d", "ufloat.d")[op2 - 12])
    if op2 == 15:
        if t == 0:
            return _fop(lambda c, pc, n: c.fr.__setitem__(r, c.fr[s]), "mov.d")
        if t == 1:
            return _fop(lambda c, pc, n: c.fr.__setitem__(r, c.fr[s] & 0x7fffffffffffffff), "abs.d")
        if t == 2:                                     # CVTS.D fr(single), fs(double)
            return _fop(lambda c, pc, n: c.fr.__setitem__(r, f2u(D(c, s))), "cvts.d")
        if t == 3:
            consts = {0: 0.0, 1: 1.0, 2: 2.0, 3: 0.5}
            v = d2u(consts.get(s, 0.0))
            return _fop(lambda c, pc, n: c.fr.__setitem__(r, v), "const.d")
        if t == 4:                                     # RFRD ar, fs: high half
            return Op(lambda c, pc, n: W(c, r, c.fr[s] >> 32), r, fp=True, text="rfrd")
        if t == 6:
            return _fop(lambda c, pc, n: c.fr.__setitem__(r, c.fr[s] ^ (1 << 63)), "neg.d")
        if t == 7:
            def div0(c, pc, n):
                b = D(c, s)
                c.div = {"b": b, "breg": s, "a": None}
                c.fr[r] = d2u(1.0 / b) if b else 0x7ff0000000000000
            return _fop(div0, "div0.d")
        if t == 11:
            def nexp01(c, pc, n):
                if c.div is not None and s != c.div["breg"]:
                    c.div["a"] = D(c, s)
                c.fr[r] = c.fr[s]
            return _fop(nexp01, "nexp01.d")
        if t in (12, 13, 14, 15):
            return _fop(lambda c, pc, n: None, "dfpadj")
    return unknown(w, "DFP0")


def dfp1(w, op2, r, s, t):
    D = lambda c, i: u2d(c.fr[i])
    if 1 <= op2 <= 7:
        def cmp(a, b):
            un = a != a or b != b
            return {1: un, 2: not un and a == b, 3: un or a == b, 4: not un and a < b,
                    5: un or a < b, 6: not un and a <= b, 7: un or a <= b}[op2]

        def dcmp(c, pc, n):
            v = int(cmp(D(c, s), D(c, t)))
            c.br = (c.br & ~(1 << r)) | (v << r)
        return _fop(dcmp, "dcmp")
    if op2 == 8:                                       # WFRD fr, as (high), at (low)
        return Op(lambda c, pc, n: c.fr.__setitem__(r, R(c, s) << 32 | R(c, t)), max(s, t), fp=True,
                  text=f"wfrd f{r},a{s},a{t}")
    return unknown(w, "DFP1")
