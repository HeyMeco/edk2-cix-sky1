# SPDX-License-Identifier: BSD-2-Clause-Patent
"""MCS-51 interpreter for the EC firmware (ITE IT5570).

Code space: 0x0000-0x7FFF is the common area (flash 0x0000-0x7FFF);
0x8000-0xFFFF is the bank selected by P1[2:0] (bank n maps flash
0x8000*n), as CIX's Keil build switches banks (?B_SWITCH, P1.0-P1.2).
External data (MOVX) goes to self.xram except where xread/xwrite hooks
claim the address (the IT5570's registers, 0x1000-0x2FFF).
"""

from i8051 import decode

ACC, B_, PSW, SP, DPL, DPH, IE, IP, P1 = 0xE0, 0xF0, 0xD0, 0x81, 0x82, 0x83, 0xA8, 0xB8, 0x90


class Halt(Exception):
    pass


class CPU:
    def __init__(self, flash):
        self.flash = bytes(flash) + bytes(max(0, 0x20000 - len(flash)))
        self.iram = bytearray(256)       # 0x00-0x7F, and 0x80-0xFF indirect
        self.sfr = bytearray(256)        # 0x80-0xFF direct
        self.xram = bytearray(0x10000)
        self.pc = 0
        self.sfr[SP] = 7
        self.sfr[P1] = 0xFF
        self.xread = None                # f(addr) -> value or None
        self.xwrite = None               # f(addr, value) -> True if handled
        self.sfr_write = None            # f(addr, value)
        self.sfr_read = None             # f(addr) -> value or None
        self.cycles = 0
        self.in_isr = []                 # priority levels being serviced
        self.pending = set()             # pending interrupt vectors
        self.trace_pc = None

    # ---- code ----
    def bank(self):
        return self.sfr[P1] & 7

    code_override = None                 # f(physical address) -> byte

    def code(self, a):
        a &= 0xFFFF
        p = a if a < 0x8000 else (self.bank() * 0x8000 + (a - 0x8000)) & 0x1FFFF
        if self.code_override is not None:
            return self.code_override(p)
        return self.flash[p]

    def code_view(self, pc):
        """bytes for decode() at pc"""
        return bytes(self.code(pc + i) for i in range(3))

    # ---- data ----
    def rd(self, a):                       # direct address
        if a < 0x80:
            return self.iram[a]
        if a == ACC or a == B_ or a == SP or a == DPL or a == DPH:
            return self.sfr[a]
        if a == PSW:
            return self._psw()
        if self.sfr_read:
            v = self.sfr_read(a)
            if v is not None:
                return v
        return self.sfr[a]

    def wr(self, a, v):
        v &= 0xFF
        if a < 0x80:
            self.iram[a] = v
            return
        self.sfr[a] = v
        if self.sfr_write and a not in (ACC, B_, SP, DPL, DPH, PSW):
            self.sfr_write(a, v)

    def _psw(self):
        p = self.sfr[PSW] & 0xFE
        a = self.sfr[ACC]
        par = bin(a).count('1') & 1
        return p | par

    def ird(self, a):
        return self.iram[a & 0xFF]

    def iwr(self, a, v):
        self.iram[a & 0xFF] = v & 0xFF

    def xrd(self, a):
        if self.xread:
            v = self.xread(a)
            if v is not None:
                return v & 0xFF
        return self.xram[a]

    def xwr(self, a, v):
        v &= 0xFF
        if self.xwrite and self.xwrite(a, v):
            return
        self.xram[a] = v

    def bit_rd(self, b):
        if b < 0x80:
            return (self.iram[0x20 + (b >> 3)] >> (b & 7)) & 1
        return (self.rd(b & 0xF8) >> (b & 7)) & 1

    def bit_wr(self, b, v):
        if b < 0x80:
            a = 0x20 + (b >> 3)
            m = 1 << (b & 7)
            self.iram[a] = (self.iram[a] | m) if v else (self.iram[a] & ~m)
            return
        a = b & 0xF8
        m = 1 << (b & 7)
        # read-modify-write of ports uses the latch (sfr), not the pins
        cur = self.sfr[a] if a != PSW else self._psw()
        self.wr(a, (cur | m) if v else (cur & ~m))

    @property
    def a(self):
        return self.sfr[ACC]

    @a.setter
    def a(self, v):
        self.sfr[ACC] = v & 0xFF

    def regaddr(self, n):
        return (self.sfr[PSW] & 0x18) + n

    def R(self, n):
        return self.iram[self.regaddr(n)]

    def setR(self, n, v):
        self.iram[self.regaddr(n)] = v & 0xFF

    @property
    def cy(self):
        return self.sfr[PSW] >> 7

    @cy.setter
    def cy(self, v):
        self.sfr[PSW] = (self.sfr[PSW] & 0x7F) | (0x80 if v else 0)

    @property
    def dptr(self):
        return (self.sfr[DPH] << 8) | self.sfr[DPL]

    @dptr.setter
    def dptr(self, v):
        self.sfr[DPH] = (v >> 8) & 0xFF
        self.sfr[DPL] = v & 0xFF

    def push(self, v):
        sp = (self.sfr[SP] + 1) & 0xFF
        self.sfr[SP] = sp
        self.iram[sp] = v & 0xFF

    def pop(self):
        sp = self.sfr[SP]
        v = self.iram[sp]
        self.sfr[SP] = (sp - 1) & 0xFF
        return v

    # ---- arithmetic helpers ----
    def _add(self, v, c=0):
        a = self.a
        r = a + v + c
        psw = self.sfr[PSW] & 0x3B
        if r > 0xFF:
            psw |= 0x80
        if (a & 0xF) + (v & 0xF) + c > 0xF:
            psw |= 0x40
        if ((a ^ r) & (v ^ r) & 0x80):
            psw |= 0x04
        self.sfr[PSW] = psw
        self.a = r

    def _subb(self, v):
        a = self.a
        c = self.cy
        r = a - v - c
        psw = self.sfr[PSW] & 0x3B
        if r < 0:
            psw |= 0x80
        if (a & 0xF) - (v & 0xF) - c < 0:
            psw |= 0x40
        if ((a ^ v) & (a ^ r) & 0x80):
            psw |= 0x04
        self.sfr[PSW] = psw
        self.a = r & 0xFF

    # ---- interrupts ----
    def irq(self, vector):
        self.pending.add(vector)

    VECTORS = [(0x03, 0), (0x0B, 1), (0x13, 2), (0x1B, 3), (0x23, 4), (0x2B, 5)]

    def check_irq(self):
        if not self.pending:
            return
        ie = self.sfr[IE]
        if not ie & 0x80:
            return
        ip = self.sfr[IP]
        cur = self.in_isr[-1] if self.in_isr else -1
        best = None
        for vec, bit in self.VECTORS:
            if vec in self.pending and ie & (1 << bit):
                lvl = (ip >> bit) & 1
                if lvl > cur and (best is None or lvl > best[1]):
                    best = (vec, lvl)
        if best is None:
            return
        vec, lvl = best
        self.pending.discard(vec)
        self.push(self.pc & 0xFF)
        self.push(self.pc >> 8)
        self.in_isr.append(lvl)
        self.pc = vec

    # ---- execution ----
    def step(self):
        pc = self.pc
        if self.trace_pc:
            self.trace_pc(pc)
        op = self.code(pc)
        b1 = self.code(pc + 1)
        b2 = self.code(pc + 2)
        self.cycles += 1
        lo = op & 0x0F
        hi = op >> 4
        # register-direct group (low nibble 8-F)
        if lo >= 8:
            n = op & 7
            ra = (self.sfr[PSW] & 0x18) + n
            r = self.iram[ra]
            if hi == 0x0:
                self.iram[ra] = (r + 1) & 0xFF; self.pc = pc + 1
            elif hi == 0x1:
                self.iram[ra] = (r - 1) & 0xFF; self.pc = pc + 1
            elif hi == 0x2:
                self._add(r); self.pc = pc + 1
            elif hi == 0x3:
                self._add(r, self.cy); self.pc = pc + 1
            elif hi == 0x4:
                self.a = self.a | r; self.pc = pc + 1
            elif hi == 0x5:
                self.a = self.a & r; self.pc = pc + 1
            elif hi == 0x6:
                self.a = self.a ^ r; self.pc = pc + 1
            elif hi == 0x7:
                self.iram[ra] = b1; self.pc = pc + 2
            elif hi == 0x8:
                self.wr(b1, r); self.pc = pc + 2
            elif hi == 0x9:
                self._subb(r); self.pc = pc + 1
            elif hi == 0xA:
                self.iram[ra] = self.rd(b1); self.pc = pc + 2
            elif hi == 0xB:
                self.cy = r < b1
                self.pc = (pc + 3 + (b2 - 256 if b2 > 127 else b2)) & 0xFFFF if r != b1 else pc + 3
            elif hi == 0xC:
                self.iram[ra], self.a = self.a, r; self.pc = pc + 1
            elif hi == 0xD:
                r = (r - 1) & 0xFF
                self.iram[ra] = r
                self.pc = (pc + 2 + (b1 - 256 if b1 > 127 else b1)) & 0xFFFF if r else pc + 2
            elif hi == 0xE:
                self.a = r; self.pc = pc + 1
            else:
                self.iram[ra] = self.a; self.pc = pc + 1
            return
        # @Ri group (low nibble 6,7)
        if lo in (6, 7) and hi not in (0x8, 0xA):
            ia = self.iram[(self.sfr[PSW] & 0x18) + (op & 1)]
            v = self.iram[ia]
            if hi == 0x0:
                self.iram[ia] = (v + 1) & 0xFF
            elif hi == 0x1:
                self.iram[ia] = (v - 1) & 0xFF
            elif hi == 0x2:
                self._add(v)
            elif hi == 0x3:
                self._add(v, self.cy)
            elif hi == 0x4:
                self.a = self.a | v
            elif hi == 0x5:
                self.a = self.a & v
            elif hi == 0x6:
                self.a = self.a ^ v
            elif hi == 0x7:
                self.iram[ia] = b1; self.pc = pc + 2; return
            elif hi == 0x9:
                self._subb(v)
            elif hi == 0xB:
                self.cy = v < b1
                self.pc = (pc + 3 + (b2 - 256 if b2 > 127 else b2)) & 0xFFFF if v != b1 else pc + 3
                return
            elif hi == 0xC:
                self.iram[ia], self.a = self.a, v
            elif hi == 0xD:
                a = self.a
                self.a = (a & 0xF0) | (v & 0x0F)
                self.iram[ia] = (v & 0xF0) | (a & 0x0F)
            elif hi == 0xE:
                self.a = v
            else:
                self.iram[ia] = self.a
            self.pc = pc + 1
            return
        if lo == 1:                       # AJMP / ACALL
            t = ((pc + 2) & 0xF800) | ((op & 0xE0) << 3) | b1
            if op & 0x10:
                ret = pc + 2
                self.push(ret & 0xFF)
                self.push(ret >> 8)
            self.pc = t
            return
        f = getattr(self, '_op%02X' % op)
        f(pc, b1, b2)

    def _rel(self, pc, ln, off):
        return (pc + ln + (off - 256 if off > 127 else off)) & 0xFFFF

    # explicit opcodes
    def _op00(self, pc, b1, b2): self.pc = pc + 1
    def _op02(self, pc, b1, b2): self.pc = (b1 << 8) | b2
    def _op03(self, pc, b1, b2):
        a = self.a; self.a = (a >> 1) | ((a & 1) << 7); self.pc = pc + 1
    def _op04(self, pc, b1, b2): self.a = self.a + 1; self.pc = pc + 1
    def _op05(self, pc, b1, b2): self.wr(b1, self.rd(b1) + 1); self.pc = pc + 2
    def _op10(self, pc, b1, b2):
        if self.bit_rd(b1):
            self.bit_wr(b1, 0); self.pc = self._rel(pc, 3, b2)
        else:
            self.pc = pc + 3
    def _op12(self, pc, b1, b2):
        ret = pc + 3
        self.push(ret & 0xFF); self.push(ret >> 8)
        self.pc = (b1 << 8) | b2
    def _op13(self, pc, b1, b2):
        a = self.a; c = self.cy; self.cy = a & 1; self.a = (a >> 1) | (c << 7); self.pc = pc + 1
    def _op14(self, pc, b1, b2): self.a = self.a - 1; self.pc = pc + 1
    def _op15(self, pc, b1, b2): self.wr(b1, self.rd(b1) - 1); self.pc = pc + 2
    def _op20(self, pc, b1, b2):
        self.pc = self._rel(pc, 3, b2) if self.bit_rd(b1) else pc + 3
    def _op22(self, pc, b1, b2):
        hi = self.pop(); lo = self.pop(); self.pc = (hi << 8) | lo
    def _op23(self, pc, b1, b2):
        a = self.a; self.a = ((a << 1) | (a >> 7)); self.pc = pc + 1
    def _op24(self, pc, b1, b2): self._add(b1); self.pc = pc + 2
    def _op25(self, pc, b1, b2): self._add(self.rd(b1)); self.pc = pc + 2
    def _op30(self, pc, b1, b2):
        self.pc = self._rel(pc, 3, b2) if not self.bit_rd(b1) else pc + 3
    def _op32(self, pc, b1, b2):
        hi = self.pop(); lo = self.pop(); self.pc = (hi << 8) | lo
        if self.in_isr:
            self.in_isr.pop()
    def _op33(self, pc, b1, b2):
        a = self.a; c = self.cy; self.cy = a >> 7; self.a = (a << 1) | c; self.pc = pc + 1
    def _op34(self, pc, b1, b2): self._add(b1, self.cy); self.pc = pc + 2
    def _op35(self, pc, b1, b2): self._add(self.rd(b1), self.cy); self.pc = pc + 2
    def _op40(self, pc, b1, b2): self.pc = self._rel(pc, 2, b1) if self.cy else pc + 2
    def _op42(self, pc, b1, b2): self.wr(b1, self.sfr_latch(b1) | self.a); self.pc = pc + 2
    def _op43(self, pc, b1, b2): self.wr(b1, self.sfr_latch(b1) | b2); self.pc = pc + 3
    def _op44(self, pc, b1, b2): self.a = self.a | b1; self.pc = pc + 2
    def _op45(self, pc, b1, b2): self.a = self.a | self.rd(b1); self.pc = pc + 2
    def _op50(self, pc, b1, b2): self.pc = self._rel(pc, 2, b1) if not self.cy else pc + 2
    def _op52(self, pc, b1, b2): self.wr(b1, self.sfr_latch(b1) & self.a); self.pc = pc + 2
    def _op53(self, pc, b1, b2): self.wr(b1, self.sfr_latch(b1) & b2); self.pc = pc + 3
    def _op54(self, pc, b1, b2): self.a = self.a & b1; self.pc = pc + 2
    def _op55(self, pc, b1, b2): self.a = self.a & self.rd(b1); self.pc = pc + 2
    def _op60(self, pc, b1, b2): self.pc = self._rel(pc, 2, b1) if self.a == 0 else pc + 2
    def _op62(self, pc, b1, b2): self.wr(b1, self.sfr_latch(b1) ^ self.a); self.pc = pc + 2
    def _op63(self, pc, b1, b2): self.wr(b1, self.sfr_latch(b1) ^ b2); self.pc = pc + 3
    def _op64(self, pc, b1, b2): self.a = self.a ^ b1; self.pc = pc + 2
    def _op65(self, pc, b1, b2): self.a = self.a ^ self.rd(b1); self.pc = pc + 2
    def _op70(self, pc, b1, b2): self.pc = self._rel(pc, 2, b1) if self.a != 0 else pc + 2
    def _op72(self, pc, b1, b2): self.cy = self.cy | self.bit_rd(b1); self.pc = pc + 2
    def _op73(self, pc, b1, b2): self.pc = (self.a + self.dptr) & 0xFFFF
    def _op74(self, pc, b1, b2): self.a = b1; self.pc = pc + 2
    def _op75(self, pc, b1, b2): self.wr(b1, b2); self.pc = pc + 3
    def _op80(self, pc, b1, b2): self.pc = self._rel(pc, 2, b1)
    def _op82(self, pc, b1, b2): self.cy = self.cy & self.bit_rd(b1); self.pc = pc + 2
    def _op83(self, pc, b1, b2): self.a = self.code(pc + 1 + self.a); self.pc = pc + 1
    def _op84(self, pc, b1, b2):
        a, b = self.a, self.sfr[B_]
        psw = self.sfr[PSW] & 0x7B
        if b == 0:
            psw |= 0x04
        else:
            self.a = a // b; self.sfr[B_] = a % b
        self.sfr[PSW] = psw; self.pc = pc + 1
    def _op85(self, pc, b1, b2): self.wr(b2, self.rd(b1)); self.pc = pc + 3
    def _op86(self, pc, b1, b2): self.wr(b1, self.iram[self.R(0)]); self.pc = pc + 2
    def _op87(self, pc, b1, b2): self.wr(b1, self.iram[self.R(1)]); self.pc = pc + 2
    def _op90(self, pc, b1, b2): self.dptr = (b1 << 8) | b2; self.pc = pc + 3
    def _op92(self, pc, b1, b2): self.bit_wr(b1, self.cy); self.pc = pc + 2
    def _op93(self, pc, b1, b2): self.a = self.code(self.a + self.dptr); self.pc = pc + 1
    def _op94(self, pc, b1, b2): self._subb(b1); self.pc = pc + 2
    def _op95(self, pc, b1, b2): self._subb(self.rd(b1)); self.pc = pc + 2
    def _opA0(self, pc, b1, b2): self.cy = self.cy | (1 - self.bit_rd(b1)); self.pc = pc + 2
    def _opA2(self, pc, b1, b2): self.cy = self.bit_rd(b1); self.pc = pc + 2
    def _opA3(self, pc, b1, b2): self.dptr = (self.dptr + 1) & 0xFFFF; self.pc = pc + 1
    def _opA4(self, pc, b1, b2):
        r = self.a * self.sfr[B_]
        self.a = r & 0xFF; self.sfr[B_] = r >> 8
        psw = self.sfr[PSW] & 0x7B
        if r > 0xFF:
            psw |= 0x04
        self.sfr[PSW] = psw; self.pc = pc + 1
    def _opA5(self, pc, b1, b2): raise Halt('undefined opcode A5 at %04X' % pc)
    def _opA6(self, pc, b1, b2): self.iram[self.R(0)] = self.rd(b1); self.pc = pc + 2
    def _opA7(self, pc, b1, b2): self.iram[self.R(1)] = self.rd(b1); self.pc = pc + 2
    def _opB0(self, pc, b1, b2): self.cy = self.cy & (1 - self.bit_rd(b1)); self.pc = pc + 2
    def _opB2(self, pc, b1, b2): self.bit_wr(b1, 1 - self.bit_latch(b1)); self.pc = pc + 2
    def _opB3(self, pc, b1, b2): self.cy = 1 - self.cy; self.pc = pc + 1
    def _opB4(self, pc, b1, b2):
        a = self.a; self.cy = a < b1
        self.pc = self._rel(pc, 3, b2) if a != b1 else pc + 3
    def _opB5(self, pc, b1, b2):
        a = self.a; v = self.rd(b1); self.cy = a < v
        self.pc = self._rel(pc, 3, b2) if a != v else pc + 3
    def _opC0(self, pc, b1, b2): self.push(self.rd(b1)); self.pc = pc + 2
    def _opC2(self, pc, b1, b2): self.bit_wr(b1, 0); self.pc = pc + 2
    def _opC3(self, pc, b1, b2): self.cy = 0; self.pc = pc + 1
    def _opC4(self, pc, b1, b2):
        a = self.a; self.a = ((a << 4) | (a >> 4)); self.pc = pc + 1
    def _opC5(self, pc, b1, b2):
        v = self.rd(b1); self.wr(b1, self.a); self.a = v; self.pc = pc + 2
    def _opD0(self, pc, b1, b2): self.wr(b1, self.pop()); self.pc = pc + 2
    def _opD2(self, pc, b1, b2): self.bit_wr(b1, 1); self.pc = pc + 2
    def _opD3(self, pc, b1, b2): self.cy = 1; self.pc = pc + 1
    def _opD4(self, pc, b1, b2):
        a = self.a; c = self.cy
        if (a & 0xF) > 9 or self.sfr[PSW] & 0x40:
            a += 6
        if (a >> 4) > 9 or c or a > 0xFF:
            a += 0x60
        if a > 0xFF:
            c = 1
        self.a = a & 0xFF; self.cy = c; self.pc = pc + 1
    def _opD5(self, pc, b1, b2):
        v = (self.rd(b1) - 1) & 0xFF; self.wr(b1, v)
        self.pc = self._rel(pc, 3, b2) if v else pc + 3
    def _opE0(self, pc, b1, b2): self.a = self.xrd(self.dptr); self.pc = pc + 1
    def _opE2(self, pc, b1, b2): self.a = self.xrd(self.R(0) | (self.sfr[0xA0] << 8)); self.pc = pc + 1
    def _opE3(self, pc, b1, b2): self.a = self.xrd(self.R(1) | (self.sfr[0xA0] << 8)); self.pc = pc + 1
    def _opE4(self, pc, b1, b2): self.a = 0; self.pc = pc + 1
    def _opE5(self, pc, b1, b2): self.a = self.rd(b1); self.pc = pc + 2
    def _opF0(self, pc, b1, b2): self.xwr(self.dptr, self.a); self.pc = pc + 1
    def _opF2(self, pc, b1, b2): self.xwr(self.R(0) | (self.sfr[0xA0] << 8), self.a); self.pc = pc + 1
    def _opF3(self, pc, b1, b2): self.xwr(self.R(1) | (self.sfr[0xA0] << 8), self.a); self.pc = pc + 1
    def _opF4(self, pc, b1, b2): self.a = ~self.a; self.pc = pc + 1
    def _opF5(self, pc, b1, b2): self.wr(b1, self.a); self.pc = pc + 2

    def sfr_latch(self, a):
        # read-modify-write instructions read the port latch, not the pins
        if a < 0x80:
            return self.iram[a]
        if a == PSW:
            return self._psw()
        if a in (0x80, 0x90, 0xA0, 0xB0):
            return self.sfr[a]
        return self.rd(a)

    def bit_latch(self, b):
        if b < 0x80:
            return self.bit_rd(b)
        a = b & 0xF8
        return (self.sfr_latch(a) >> (b & 7)) & 1
