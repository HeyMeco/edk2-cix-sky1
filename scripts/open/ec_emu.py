# SPDX-License-Identifier: BSD-2-Clause-Patent
"""ITE IT5570 model and Orion O6 board model for the EC firmware.

Runs an EC image (Radxa's EC V1.07, or firmware/ec) on i8051_emu with:
  * 8051 timers 0/1/2, idle (PCON.0) skipping to the next event
  * the IT5570 interrupt controller (ISR/IER/IVECT -> 8051 INT1)
  * GPIO data/control/mirror registers, with board-level pin events
  * UART1 (16550 at 0x2700) captured as text
  * an event queue for board stimuli (power button, power goods, ...)
What the firmware does is recorded as `events`: (ms, kind, detail).
Peripherals not modelled read back what was written.
"""

import heapq

from i8051_emu import CPU, Halt
from ec_o6_pins import PINS

MC_PER_MS = 768          # 8051 machine cycles per ms (9.2 MHz / 12)

_CYC = [1] * 256
for _op in ([0x02, 0x12, 0x22, 0x32, 0x80, 0x73, 0x40, 0x50, 0x60, 0x70,
             0x20, 0x30, 0x10, 0xD5, 0xE0, 0xE2, 0xE3, 0xF0, 0xF2, 0xF3,
             0x83, 0x93, 0x90, 0xA3, 0x85, 0x75, 0x86, 0x87, 0xA6, 0xA7,
             0x43, 0x53, 0x63, 0x72, 0x82, 0xA0, 0xB0, 0x92, 0xC0, 0xD0]
            + list(range(0xB4, 0xC0)) + list(range(0xD8, 0xE0))
            + list(range(0x88, 0x90)) + list(range(0xA8, 0xB0))
            + [0x01 + 0x20 * i for i in range(8)] + [0x11 + 0x20 * i for i in range(8)]):
    _CYC[_op] = 2
_CYC[0xA4] = _CYC[0x84] = 4

PORTS = 'ABCDEFGHIJ'
GPDR = {p: 0x1601 + i for i, p in enumerate(PORTS)}
GPDR['M'] = 0x160D
GPCR = {p: 0x1610 + 8 * i for i, p in enumerate(PORTS)}
GPCR['M'] = 0x16A0
GPDMR = {p: 0x1661 + i for i, p in enumerate(PORTS)}
GPDMR['M'] = 0x166D
_GPDR_ADDR = {v: k for k, v in GPDR.items()}
_GPDMR_ADDR = {v: k for k, v in GPDMR.items()}



# pin -> (WU number, INTC number): ITE's numbering as in Zephyr's
# it8xxx2.dtsi / ite-intc.h (same family as the IT5570); the pins the O6
# EC enables (D1, H0, H2, F5, H4, H6, A6, A7, B3, B5, J3, ...) match.
PIN_WU = {
    'A0': (91, 96),
    'A1': (92, 97),
    'A2': (93, 98),
    'A3': (80, 88),
    'A4': (81, 89),
    'A5': (82, 90),
    'A6': (83, 91),
    'A7': (100, 105),
    'B0': (101, 106),
    'B1': (102, 107),
    'B2': (84, 92),
    'B3': (103, 108),
    'B4': (94, 99),
    'B5': (104, 109),
    'B6': (105, 110),
    'B7': (106, 111),
    'C0': (85, 93),
    'C1': (107, 112),
    'C2': (95, 100),
    'C3': (108, 113),
    'C4': (22, 21),
    'C5': (109, 114),
    'C6': (23, 6),
    'C7': (86, 94),
    'D0': (20, 1),
    'D1': (21, 31),
    'D2': (24, 17),
    'D3': (110, 115),
    'D4': (111, 116),
    'D5': (112, 117),
    'D6': (113, 118),
    'D7': (87, 95),
    'E0': (70, 72),
    'E1': (71, 73),
    'E2': (72, 74),
    'E3': (73, 75),
    'E4': (114, 119),
    'E5': (40, 176),
    'E6': (45, 177),
    'E7': (46, 178),
    'F0': (96, 101),
    'F1': (97, 102),
    'F2': (98, 103),
    'F3': (99, 104),
    'F4': (64, 52),
    'F5': (65, 53),
    'F6': (66, 54),
    'F7': (67, 55),
    'G0': (115, 120),
    'G1': (116, 121),
    'G2': (117, 122),
    'G3': (123, 144),
    'G4': (124, 145),
    'G5': (125, 146),
    'G6': (118, 123),
    'G7': (126, 147),
    'H0': (60, 48),
    'H1': (61, 49),
    'H2': (62, 50),
    'H3': (63, 51),
    'H4': (88, 85),
    'H5': (89, 86),
    'H6': (90, 87),
    'I0': (119, 124),
    'I1': (120, 125),
    'I2': (121, 126),
    'I3': (122, 127),
    'I4': (74, 76),
    'I5': (75, 77),
    'I6': (76, 78),
    'I7': (77, 79),
    'J0': (128, 128),
    'J1': (129, 129),
    'J2': (130, 130),
    'J3': (131, 131),
    'J4': (132, 132),
    'J5': (133, 133),
    'J6': (134, 134),
    'J7': (135, 135),
}


def wu_reg(n):
    """(WUEMR, WUESR) addresses and bit of wake-up input n"""
    if n < 80:
        g, b = n // 10, n % 10
    else:
        g, b = 8 + (n - 80) // 8, (n - 80) % 8
    if g <= 4:
        return 0x1B00 + g - 1, 0x1B04 + g - 1, b
    if g == 5:
        return 0x1B0C, 0x1B0D, b
    return 0x1B10 + 4 * (g - 6), 0x1B11 + 4 * (g - 6), b


class EC:
    def __init__(self, image, board=None, log_regs=False, flash=None):
        self.t = 0                       # machine cycles
        self.q = []                      # (time, seq, fn)
        self.seq = 0
        self.events = []
        self.uart = bytearray()
        self.uart_lines = []
        self.idle = False
        self.log_regs = log_regs
        self.reglog = []
        self.pins = {}                   # 'B3' -> level driven by the board
        self.board = board
        self.scar = None
        self.spi = SpiNor(self, flash)
        # the IT5570E's internal e-flash (the running image; ECINDAR3 bit 6)
        self.eflash = SpiNor(self, bytes(image) + b'\xff' * (0x20000 - len(image)), 'eflash')
        self.smb = {n: SmbusMaster(self, n) for n in SMB_CH}
        self.slave = {n: SmbusSlave(self, n) for n in SMB_SLAVE}
        self._smb_addr = {}
        for m in self.smb.values():
            for a in (m.hosta, m.hoctl, m.hobdb, m.pctl):
                self._smb_addr[a] = m
        for sl in self.slave.values():
            self._smb_addr[sl.slda] = sl
            self._smb_addr[sl.slsta] = sl
        self._boot(image, 0x01)
        if board:
            board.attach(self)

    def _boot(self, image, rsts):
        """the chip out of reset, running image (RAM and registers cleared)"""
        self.cpu = c = CPU(image)
        self.x = c.xram
        self.idle = False
        self.scar = None
        c.xread = self._xread
        c.xwrite = self._xwrite
        c.sfr_write = self._sfr_write
        self.x[0x2000] = 0x55            # ECHIPID1
        self.x[0x2001] = 0x70            # ECHIPID2
        self.x[0x2002] = 0x03            # ECHIPVER
        self.x[0x2006] = rsts            # RSTS: power-on reset / watchdog
        for p in GPCR:
            for b in range(8):
                self.x[GPCR[p] + b] = 0x84

    EC_SPI_BASE = 0x10000                # the O6 flash layout's ec_addr

    def _watchdog_reset(self):
        """a wrong key in EWDKEYR resets the chip; with the mirror bits in
        SMFI 0x1063 the e-flash is first loaded from the SPI flash's EC
        image (how UEFI's 3E19 installs a new EC firmware)"""
        mirror = self.x[0x1063] & 0x18
        if mirror:
            img = bytes(self.spi.data[self.EC_SPI_BASE:self.EC_SPI_BASE + 0x20000])
            self.eflash.data[:0x20000] = img
            self.ev('reset', 'watchdog, mirrored %s' % __import__('hashlib').sha256(img).hexdigest()[:12])
        else:
            self.ev('reset', 'watchdog')
        before = {p: self._snapshot(p) for p in GPCR}
        self._boot(bytes(self.eflash.data[:0x20000]), 0x02)
        for p in GPCR:                    # outputs released by the reset
            for name, lvl in before[p].items():
                if lvl is not None:
                    self.ev('out', '%s %s=Z' % (name, PINS.get(name, ('?',))[0]))

    # ---- time ----
    @property
    def ms(self):
        return self.t / MC_PER_MS

    def at(self, ms, fn):
        heapq.heappush(self.q, (int(ms * MC_PER_MS), self.seq, fn))
        self.seq += 1

    def after(self, ms, fn):
        self.at(self.ms + ms, fn)

    def ev(self, kind, detail):
        self.events.append((round(self.ms, 3), kind, detail))

    # ---- pins ----
    def is_output(self, port, bit):
        return (self.x[GPCR[port] + bit] & 0xC0) == 0x40

    def out_level(self, port, bit):
        return (self.x[GPDR[port]] >> bit) & 1

    def pin(self, name):
        """level on a pin: the EC's output, or what the board drives"""
        port, bit = name[0], int(name[1])
        if self.is_output(port, bit):
            return self.out_level(port, bit)
        return self.pins.get(name, 1 if self.x[GPCR[port] + bit] & 0x04 else 0)

    def drive(self, name, level):
        """board drives an EC input"""
        old = self.pin(name)
        self.pins[name] = level
        new = self.pin(name)
        if old != new:
            self.ev('in', '%s %s=%d' % (name, PINS.get(name, ('?',))[0], new))
            self._wuc_edge(name, old, new)

    def _wuc_edge(self, name, old, new):
        if name in PIN_WU:
            n, intn = PIN_WU[name]
            emr, esr, b = wu_reg(n)
            falling = (self.x[emr] >> b) & 1
            # PWRSW (GPB3, WU103) interrupts on both edges: the vendor's
            # handler reads the pin for press/release and never re-arms
            # the edge (it does for every other pin), and a short press in
            # S0 does not turn an O6 off, so the release must be seen
            both = name == 'B3'
            if both or (falling and old == 1 and new == 0) or (not falling and old == 0 and new == 1):
                self.x[esr] |= 1 << b
                self.int_raise(intn)
        if self.board:
            self.board.wuc_edge(name, old, new)

    def _gpio_read(self, port):
        v = 0
        for b in range(8):
            v |= self.pin('%s%d' % (port, b)) << b
        return v

    def _gpio_changed(self, port, before):
        for b in range(8):
            name = '%s%d' % (port, b)
            new = self.pin(name) if self.is_output(port, b) else None
            old = before.get(name)
            if new is not None and new != old:
                self.ev('out', '%s %s=%d' % (name, PINS.get(name, ('?',))[0], new))
                if self.board:
                    self.board.output(name, new)

    def _snapshot(self, port):
        return {'%s%d' % (port, b): (self.out_level(port, b) if self.is_output(port, b) else None)
                for b in range(8)}

    # ---- scratch ROM and the e-flash's busy time ----
    SCRATCH = 0x400

    def _scar(self, v):
        """SCAR0H written: bit 7 off maps SCRATCH bytes of the e-flash at
        SCAR0 into scratch SRAM (copied now)"""
        if v & 0x80:
            self.scar = None
        else:
            a = ((v & 3) << 16) | (self.x[0x1041] << 8) | self.x[0x1040]
            self.scar = (a, bytes(self.cpu.flash[a:a + self.SCRATCH]))
        self._fetch_watch()

    def _fetch_watch(self):
        self.cpu.code_override = self._fetch if (self.scar or self.eflash.busy()) else None

    def _fetch(self, phys):
        sc = self.scar
        if sc and sc[0] <= phys < sc[0] + self.SCRATCH:
            return sc[1][phys - sc[0]]
        if self.eflash.busy():
            raise Halt('code fetch from the e-flash at %05X while it is busy' % phys)
        if not sc:
            self.cpu.code_override = None
        return self.cpu.flash[phys]

    def _fm(self):
        """follow mode target: ECINDAR3 bit 6 set = internal e-flash"""
        return self.eflash if self.x[0x103E] & 0x40 else self.spi

    # ---- interrupt controller ----
    # ISRn/IERn: groups 0-3 at 0x1100+n/0x1104+n, groups 4.. at 0x1114+4(n-4)
    @staticmethod
    def _isr_addr(g):
        return 0x1100 + g if g < 4 else 0x1114 + 4 * (g - 4)

    @staticmethod
    def _ier_addr(g):
        return 0x1104 + g if g < 4 else 0x1115 + 4 * (g - 4)

    def int_raise(self, n):
        a = self._isr_addr(n >> 3)
        self.x[a] |= 1 << (n & 7)

    def int_pending(self):
        for g in range(24):
            p = self.x[self._isr_addr(g)] & self.x[self._ier_addr(g)]
            if p:
                for b in range(8):
                    if p >> b & 1:
                        return g * 8 + b
        return None

    # ---- register access ----
    def _xread(self, a):
        if a < 0x1000 or a >= 0x3000:
            return None
        if a in _GPDR_ADDR:
            v = self._gpio_read(_GPDR_ADDR[a])
        elif a in _GPDMR_ADDR:
            v = self._gpio_read(_GPDMR_ADDR[a])
        elif a == 0x1110:                # IVECT
            n = self.int_pending()
            v = 0x10 + n if n is not None else 0x10
        elif a == 0x103F and self.x[0x103C] == 0xFD:   # follow mode read
            v = self._fm().xfer_read()
        elif a == 0x103F and self.x[0x103C] != 0xFE:   # indirect memory read
            fa = (self.x[0x103D] << 16) | (self.x[0x103C] << 8) | self.x[0x103B]
            d = self._fm().data
            v = d[fa % len(d)]
        elif a in self._smb_addr and self._smb_addr[a].read(a) is not None:
            v = self._smb_addr[a].read(a)
        elif a == 0x2705:                # UART1 LSR
            v = 0x60
        elif self.board and self.board.read(a) is not None:
            v = self.board.read(a)
        else:
            v = self.x[a]
        if self.log_regs:
            self.reglog.append((self.ms, 'R', a, v))
        return v

    def _xwrite(self, a, v):
        if a < 0x1000 or a >= 0x3000:
            return False
        if self.log_regs:
            self.reglog.append((self.ms, 'W', a, v))
        if a in _GPDR_ADDR:
            p = _GPDR_ADDR[a]
            snap = self._snapshot(p)
            self.x[a] = v
            self._gpio_changed(p, snap)
            return True
        if 0x1610 <= a < 0x1660 or 0x16A0 <= a < 0x16A8:
            p = 'M' if a >= 0x16A0 else PORTS[(a - 0x1610) >> 3]
            snap = self._snapshot(p)
            self.x[a] = v
            self._gpio_changed(p, snap)
            return True
        if 0x1B04 <= a <= 0x1B07 or (0x1B0D <= a <= 0x1B35 and (a - 0x1B0D) % 4 == 0) \
                or a in (0x1B0D,):
            self.x[a] &= ~v & 0xFF           # WUESR: write 1 to clear
            for pin, (n, intn) in PIN_WU.items():   # the INTC follows WUESR
                emr, esr, b = wu_reg(n)
                if esr == a and v >> b & 1:
                    self.x[self._isr_addr(intn >> 3)] &= ~(1 << (intn & 7)) & 0xFF
            return True
        if 0x1100 <= a < 0x1180:        # ISR: write 1 to clear
            g = None
            for gg in range(24):
                if self._isr_addr(gg) == a:
                    g = gg
            if g is not None:
                self.x[a] &= ~v & 0xFF
                return True
        if a == 0x2700 and not self.x[0x2703] & 0x80:   # UART1 THR
            if v == 0x0A:
                line = self.uart.decode('latin-1').rstrip('\r')
                self.uart_lines.append((round(self.ms, 3), line))
                self.ev('uart', line)
                self.uart = bytearray()
            elif v != 0x0D:
                self.uart.append(v)
            return True
        if a in self._smb_addr and self._smb_addr[a].write(a, v):
            return True
        if a == 0x1042:
            self.x[a] = v
            self._scar(v)
            return True
        if a == 0x1F07 and v != 0x5C:    # EWDKEYR: a wrong key
            self.x[a] = v
            self.after(0.1, self._watchdog_reset)
            return True
        if a == 0x103F and self.x[0x103C] in (0xFD, 0xFE):
            self.x[a] = v
            self._fm().xfer_write(self.x[0x103C], v)
            return True
        if self.board and self.board.write(a, v):
            return True
        self.x[a] = v
        return True

    def _sfr_write(self, a, v):
        if a == 0x87 and v & 1:          # PCON.IDL
            self.idle = True
        elif a == 0x87 and v & 2:        # PCON.PD: wake is immediate here
            pass

    # ---- timers ----
    def _timers(self, n):
        s = self.cpu.sfr
        tcon, tmod = s[0x88], s[0x89]
        for t, tr, tf, tl, th, sh in ((0, 0x10, 0x20, 0x8A, 0x8C, 0), (1, 0x40, 0x80, 0x8B, 0x8D, 4)):
            if not tcon & tr:
                continue
            mode = (tmod >> sh) & 3
            if mode == 2:
                v = s[tl] + n
                while v > 0xFF:
                    v = v - 0x100 + s[th]
                    s[0x88] |= tf
                s[tl] = v & 0xFF
            else:
                v = ((s[th] << 8) | s[tl]) + n
                if v > 0xFFFF:
                    s[0x88] |= tf
                    v &= 0xFFFF
                s[th], s[tl] = v >> 8, v & 0xFF
        if s[0xC8] & 0x04:               # T2CON.TR2, auto-reload
            v = ((s[0xCD] << 8) | s[0xCC]) + n
            while v > 0xFFFF:
                v = v - 0x10000 + ((s[0xCB] << 8) | s[0xCA])
                s[0xC8] |= 0x80
            s[0xCD], s[0xCC] = v >> 8, v & 0xFF

    def _next_timer(self):
        """machine cycles until the next timer overflow (or None)"""
        s = self.cpu.sfr
        best = None
        tcon, tmod = s[0x88], s[0x89]
        for tr, tl, th, sh in ((0x10, 0x8A, 0x8C, 0), (0x40, 0x8B, 0x8D, 4)):
            if tcon & tr:
                mode = (tmod >> sh) & 3
                d = (0x100 - s[tl]) if mode == 2 else (0x10000 - ((s[th] << 8) | s[tl]))
                best = d if best is None else min(best, d)
        if s[0xC8] & 0x04:
            d = 0x10000 - ((s[0xCD] << 8) | s[0xCC])
            best = d if best is None else min(best, d)
        return best

    def _irqs(self):
        c = self.cpu
        s = c.sfr
        if s[0x88] & 0x20:
            c.pending.add(0x0B)
        if s[0x88] & 0x80:
            c.pending.add(0x1B)
        if s[0xC8] & 0x80:
            c.pending.add(0x2B)
        else:
            c.pending.discard(0x2B)
        if self.int_pending() is not None:
            c.pending.add(0x13)
        else:
            c.pending.discard(0x13)

    # ---- run ----
    def run(self, ms):
        c = self.cpu
        end = int(ms * MC_PER_MS)
        while self.t < end:
            while self.q and self.q[0][0] <= self.t:
                _, _, fn = heapq.heappop(self.q)
                fn()
                c = self.cpu              # a reset replaces the CPU
            if self.idle:
                self._irqs()
                if c.pending and c.sfr[0xA8] & 0x80 and self._enabled_pending():
                    self.idle = False
                else:
                    n = self._next_timer()
                    tq = self.q[0][0] - self.t if self.q else None
                    cand = [v for v in (n, tq, end - self.t) if v is not None and v > 0]
                    step = min(cand) if cand else 1
                    self._timers(step)
                    self.t += step
                    continue
            op = c.code(c.pc)
            pc = c.pc
            c.step()
            n = _CYC[op]
            self._timers(n)
            self.t += n
            self._irqs()
            if c.pending:
                before = len(c.in_isr)
                c.check_irq()
                if len(c.in_isr) > before:
                    v = c.pc
                    if v == 0x0B:
                        c.sfr[0x88] &= ~0x20
                    elif v == 0x1B:
                        c.sfr[0x88] &= ~0x80

    def _enabled_pending(self):
        ie = self.cpu.sfr[0xA8]
        bits = {0x03: 0, 0x0B: 1, 0x13: 2, 0x1B: 3, 0x23: 4, 0x2B: 5}
        return any(ie >> bits[v] & 1 for v in self.cpu.pending)


SMB_CH = {
    # name: (HOSTA, HOCTL, HOCMD, TRASLA, D0, D1, HOBDB, SMBPCTL, HOCTL2)
    'A': (0x1C00, 0x1C01, 0x1C02, 0x1C03, 0x1C04, 0x1C05, 0x1C06, 0x1C0A, 0x1C10),
    'B': (0x1C11, 0x1C12, 0x1C13, 0x1C14, 0x1C15, 0x1C16, 0x1C17, 0x1C1B, 0x1C21),
    'C': (0x1C29, 0x1C2A, 0x1C2B, 0x1C2C, 0x1C2D, 0x1C2E, 0x1C2F, 0x1C31, 0x1C32),
    'D': (0x1C35, 0x1C36, 0x1C37, 0x1C38, 0x1C39, 0x1C3A, 0x1C3B, 0x1C3D, 0x1C3E),
    'E': (0x1CA0, 0x1CA1, 0x1CA2, 0x1CA3, 0x1CA4, 0x1CA6, 0x1CA7, 0x1CA9, 0x1CAA),
    'F': (0x1CB0, 0x1CB1, 0x1CB2, 0x1CB3, 0x1CB4, 0x1CB6, 0x1CB7, 0x1CB9, 0x1CBA),
}


class SmbusMaster:
    """An IT5570 SMBus host channel.  Devices: {addr7: obj} with
    obj.xfer(wr: bytes, rd_len: int) -> bytes or None (NACK).
    SMBus protocols (HOCTL[4:2]) run to completion; block transfers and
    the I2C-compatible mode (protocol 7) move a byte per BYTE_DONE."""
    def __init__(self, ec, name, devices=None):
        self.ec = ec
        self.name = name
        (self.hosta, self.hoctl, self.hocmd, self.trasla, self.d0, self.d1,
         self.hobdb, self.pctl, self.hoctl2) = SMB_CH[name]
        self.devices = devices if devices is not None else {}
        self.cur = None

    def owns(self, a):
        return a in (self.hosta, self.hoctl, self.hobdb, self.pctl)

    def read(self, a):
        x = self.ec.x
        if a == self.pctl:
            return (x[a] & ~3) | 3       # SMCLK, SMDAT high: bus idle
        if a == self.hobdb and self.cur and self.cur['rd']:
            return x[a]
        return None

    def _dev(self, addr):
        return self.devices.get(addr)

    def _log(self, addr, wr, rd, ok):
        self.ec.ev('i2c', '%s %02X w[%s] r[%s]%s' % (
            self.name, addr, bytes(wr).hex(' '), bytes(rd or b'').hex(' '),
            '' if ok else ' NACK'))

    def _finish(self, status):
        x = self.ec.x
        x[self.hosta] = status
        self.cur = None

    def write(self, a, v):
        x = self.ec.x
        if a == self.hosta:              # write 1 to clear
            x[a] &= ~v & 0xFF
            c = self.cur
            if c and (v & 0x80):
                self._next_byte()
            return True
        if a == self.hoctl:
            x[a] = v & ~0x40
            if v & 0x02 and self.cur:        # KILL
                self._finish(0x10)
            if v & 0x40:
                self._start(v)
            return True
        if a == self.hobdb:
            x[a] = v
            return True
        return False

    def _start(self, ctl):
        x = self.ec.x
        prot = (ctl >> 2) & 7
        sa = x[self.trasla]
        addr, rd = sa >> 1, sa & 1
        cmd = x[self.hocmd]
        dev = self._dev(addr)
        if prot in (5, 7):
            self.cur = {'prot': prot, 'addr': addr, 'rd': rd, 'cmd': cmd,
                        'wr': [cmd] if prot == 5 else [], 'rdbuf': None,
                        'n': x[self.d0], 'i': 0, 'dev': dev}
            if dev is None:
                self._log(addr, [], None, False)
                self._finish(0x04)
                return
            if rd:
                data = dev.xfer(bytes(self.cur['wr']), 255 if prot == 7 else 33)
                if data is None:
                    self._log(addr, self.cur['wr'], None, False)
                    self._finish(0x04)
                    return
                got0 = bytearray()
                if prot == 5:
                    n = data[0]
                    x[self.d0] = n
                    got0 = bytearray(data[:1])   # the count is on the wire too
                    data = data[1:1 + n]
                    self.cur['n'] = n
                self.cur['rdbuf'] = bytearray(data)
                self.cur['got'] = got0
                self._feed()
            else:
                self.cur['wr'].append(x[self.hobdb]) if prot == 7 else None
                if prot == 5:
                    self.cur['wr'].append(x[self.d0])
                    self.cur['wr'].append(x[self.hobdb])
                    self.cur['i'] = 1
                x[self.hosta] = 0x81
                if prot == 5 and self.cur['i'] >= self.cur['n']:
                    self._end_write()
            return
        wr = []
        rdn = 0
        if prot == 0:
            pass
        elif prot == 1:
            if rd:
                rdn = 1
            else:
                wr = [cmd]
        elif prot == 2:
            wr = [cmd] + ([] if rd else [x[self.d0]])
            rdn = 1 if rd else 0
        elif prot == 3:
            wr = [cmd] + ([] if rd else [x[self.d0], x[self.d1]])
            rdn = 2 if rd else 0
        elif prot == 4:
            wr = [cmd, x[self.d0], x[self.d1]]
            rdn = 2
        else:
            wr = [cmd]
        if dev is None:
            self._log(addr, wr, None, False)
            self._finish(0x04)
            return
        data = dev.xfer(bytes(wr), rdn)
        if data is None:
            self._log(addr, wr, None, False)
            self._finish(0x04)
            return
        if len(data) < rdn:
            self.ec.ev('note', '%s %02X short read %d<%d' % (self.name, addr, len(data), rdn))
            data = bytes(data) + bytes(rdn - len(data))
        self._log(addr, wr, data, True)
        if rdn >= 1:
            x[self.d0] = data[0]
        if rdn >= 2:
            x[self.d1] = data[1]
        self._finish(0x02)
        self._irq()

    def _irq(self):
        pass

    def _feed(self):
        c = self.cur
        x = self.ec.x
        if c['rdbuf']:
            x[self.hobdb] = c['rdbuf'].pop(0)
        else:
            x[self.hobdb] = 0xFF
        c['got'].append(x[self.hobdb])
        c['i'] += 1
        x[self.hosta] = 0x81

    def _next_byte(self):
        c = self.cur
        x = self.ec.x
        stop = c['prot'] == 7 and not x[self.hoctl2] & 0x02
        if c['rd']:
            last = (c['prot'] == 5 and c['i'] >= c['n']) or stop or c.get('lastfed')
            if last:
                self._log(c['addr'], c['wr'], c['got'], True)
                self._finish(0x02)
                return
            self._feed()
            if c['prot'] == 7 and x[self.hoctl] & 0x20:   # last byte: NACK, STOP
                c['lastfed'] = True
                x[self.hosta] |= 0x02
            return
        if stop:                         # I2C mode off: STOP after the bytes sent
            self._end_write()
            return
        if c['prot'] == 7 and x[self.hoctl2] & 0x04:
            # direction switch: repeated start, then read
            data = c['dev'].xfer(bytes(c['wr']), 255)
            c['rd'] = 1
            c['rdbuf'] = bytearray(data or b'')
            c['got'] = bytearray()
            self._feed()
            return
        c['wr'].append(x[self.hobdb])
        c['i'] += 1
        if c['prot'] == 5 and c['i'] >= c['n']:
            self._end_write()
            return
        x[self.hosta] = 0x81

    def _end_write(self):
        c = self.cur
        ok = c['dev'].xfer(bytes(c['wr']), 0)
        self._log(c['addr'], c['wr'], b'', ok is not None)
        self._finish(0x02 if ok is not None else 0x04)


SMB_SLAVE = {
    # name: (SLDA, SLSTA, INTC number)
    'A': (0x1C09, 0x1C0B, 9),
    'B': (0x1C1A, 0x1C1C, 10),
}


class SmbusSlave:
    """The IT5570's SMBus slave on channel A or B, driven by a bus master
    (the SoC).  A transfer is a list of steps; each data byte raises the
    channel's interrupt with SLSTA bit 1 (data) and bit 3 (master reads)
    set, and the next byte waits until the firmware clears bit 1; a STOP
    sets bit 5.  As the vendor firmware's slave handler expects."""
    BYTE_MS = 0.1                        # a byte at 100 kHz

    def __init__(self, ec, name):
        self.ec = ec
        self.name = name
        self.slda, self.slsta, self.intn = SMB_SLAVE[name]
        self.steps = []
        self.cur = None
        self.rx = bytearray()
        self.done = None

    def transfer(self, wr, rdn, done=None):
        """master writes wr, then (repeated start) reads rdn bytes"""
        # a read starts with the address-match event, whose byte is not
        # sent (UEFI's GetEcInfo layout only fits that way)
        self.steps = [('w', b) for b in wr] + [('a', None)] + [('r', None)] * rdn + [('p', None)]
        self.rx = bytearray()
        self.done = done
        self.wr = bytes(wr)
        self.ec.after(self.BYTE_MS, self._next)

    def _next(self):
        ec = self.ec
        if not self.steps:
            return
        kind, b = self.steps.pop(0)
        self.cur = kind
        if kind == 'w':
            ec.x[self.slda] = b
            ec.x[self.slsta] |= 0x12
            ec.x[self.slsta] &= ~0x08 & 0xFF
        elif kind in ('r', 'a'):
            ec.x[self.slsta] |= 0x1A
        else:
            ec.x[self.slsta] |= 0x20
            ec.ev('host', '%s w[%s] r[%s]' % (self.name, self.wr.hex(' '), bytes(self.rx).hex(' ')))
            if self.done:
                ec.after(0, lambda: self.done(bytes(self.rx)))
        ec.int_raise(self.intn)

    def read(self, a):
        return None

    def write(self, a, v):
        x = self.ec.x
        if a == self.slsta:
            x[a] &= ~v & 0xFF
            if v & 0x02 and self.cur in ('w', 'r', 'a'):
                if self.cur == 'r':
                    self.rx.append(x[self.slda])
                self.cur = None
                self.ec.after(self.BYTE_MS, self._next)
            return True
        if a == self.slda:
            x[a] = v
            return True
        return False


def ec_request(cmd, data=b'', version=0):
    """CIX's EC host request (protocol 3 over I2C), as UEFI's GetEcInfo
    builds it: 0xDA, then the header with the command and the length
    byte-swapped (big-endian on the wire), checksum making the sum 0"""
    hdr = bytearray([3, 0, cmd >> 8, cmd & 0xFF, version, 0,
                     len(data) >> 8, len(data) & 0xFF]) + bytes(data)
    hdr[1] = (-sum(hdr)) & 0xFF
    return bytes([0xDA]) + bytes(hdr)


def ec_response(raw):
    """(result, data) from the bytes read back: Result, PacketLength,
    then the 8-byte header (version, checksum, result, length BE,
    reserved) and the data"""
    if len(raw) < 10:
        return None, raw
    h = raw[2:10]
    n = (h[4] << 8) | h[5]
    return (h[2] << 8) | h[3], raw[10:10 + n]


class SpiNor:
    """the shared SPI NOR, as the EC sees it in SMFI follow mode
    (ECINDAR 0xFFFFFExx: 0xFE00 ends a transfer, 0xFD00 shifts a byte)"""
    JEDEC = bytes([0xC8, 0x40, 0x17])    # 8 MiB; the model's choice

    def __init__(self, ec, data=None, name='spi'):
        self.ec = ec
        self.name = name
        self.data = bytearray(data or bytes(0x800000))
        self.cmd = []
        self.out = bytearray()
        self.status = 0
        self.wel = False
        self.log = []
        self.busy_until = 0               # write in progress until (cycles)

    ERASE_MS = {0x20: 45, 0x52: 120, 0xD8: 150, 0xD7: 5}
    PROG_MS = 0.03

    def busy(self):
        return self.ec.t < self.busy_until

    def _busy_for(self, ms):
        self.busy_until = self.ec.t + int(ms * MC_PER_MS)
        if self.name == 'eflash':
            self.ec._fetch_watch()

    def xfer_write(self, sel, v):
        if sel == 0xFE:                  # chip select released
            if self.cmd:
                self._done()
            self.cmd = []
            self.out = bytearray()
            return
        self.cmd.append(v)
        self._start()

    def _addr(self):
        c = self.cmd
        return (c[1] << 16) | (c[2] << 8) | c[3]

    def _start(self):
        c = self.cmd
        op = c[0]
        if len(c) == 1:
            if op == 0x9F:
                self.out = bytearray(self.JEDEC)
            elif op == 0x05:
                self.out = None           # status, read live
            elif op == 0x06:
                self.wel = True
            elif op == 0x04:
                self.wel = False
        if op in (0x03, 0x0B) and len(c) == 4:
            a = self._addr()
            self.out = bytearray(self.data[a:a + 0x1000])
            if op == 0x0B:
                self.out = bytearray(1) + self.out

    def xfer_read(self):
        if self.out is None:              # RDSR: WIP, WEL
            return self.status | (1 if self.busy() else 0) | (2 if self.wel else 0)
        if self.out:
            return self.out.pop(0)
        return 0xFF

    def _done(self):
        c = self.cmd
        op = c[0]
        if self.busy() and op not in (0x05,):
            self.ec.ev(self.name, 'BUSY cmd %s' % bytes(c[:4]).hex())
            return
        if op in (0x20, 0xD8, 0x52, 0xD7) and len(c) >= 4 and self.wel:
            a = self._addr()
            n = {0x20: 0x1000, 0x52: 0x8000, 0xD8: 0x10000, 0xD7: 0x400}[op]
            a &= ~(n - 1)
            self.data[a:a + n] = b'\xff' * n
            self.log.append(('erase', a, n))
            self.ec.ev(self.name, 'erase %06X+%X' % (a, n))
            self.wel = False
            self._busy_for(self.ERASE_MS[op])
        elif op == 0x02 and len(c) >= 4 and self.wel:
            a = self._addr()
            for i, b in enumerate(c[4:]):
                self.data[a + i] &= b
            self.log.append(('prog', a, bytes(c[4:])))
            self.ec.ev(self.name, 'program %06X %s' % (a, bytes(c[4:]).hex()))
            self.wel = False
            self._busy_for(self.PROG_MS * max(1, len(c) - 4))
        elif op == 0x01 and len(c) >= 2:  # WRSR
            self.ec.ev(self.name, 'cmd %s' % bytes(c).hex())
            self.wel = False
        elif op not in (0x05, 0x06, 0x04, 0x9F, 0x03, 0x0B):
            self.ec.ev(self.name, 'cmd %s' % bytes(c).hex())


class NullBoard:
    def attach(self, ec):
        self.ec = ec

    def output(self, name, level):
        pass

    def wuc_edge(self, name, old, new):
        pass

    def read(self, a):
        return None

    def write(self, a, v):
        return False


class Rts5453(object):
    """Realtek RTS5453H PD controller, SMBus side (as in Chromium EC's
    rts54xx driver): a command is a block write (cmd, len, data...);
    receive-byte reads the ping status (bits 1:0: 1 = complete, bits 7:2:
    data length); block read 0x80 returns the command's data."""
    def __init__(self, ec, name='PD'):
        self.ec = ec
        self.name = name
        self.reply = b''
        self.cmds = []
        self.fw_version = b'\x01\x09' if name == 'PD1' else b'\x01\x0a'

    def answer(self, wr):
        cmd = wr[0]
        data = wr[2:]
        if cmd == 0x01:                  # vendor command enable
            return b''
        if cmd == 0x3A:                  # GET_IC_STATUS: length in byte 4
            n = wr[4] if len(wr) > 4 else 0x14
            st = bytearray(n)
            st[3:5] = self.fw_version         # firmware version
            return bytes(st)
        return b''

    def xfer(self, wr, rdn):
        if wr:
            if wr[0] == 0x80 and rdn:    # read data
                d = self.reply
                return bytes([len(d)]) + d + bytes(max(0, rdn - 1 - len(d)))
            self.cmds.append(bytes(wr))
            self.reply = self.answer(wr)
            return b''
        if rdn:                          # ping status
            return bytes([0x01 | (len(self.reply) << 2) & 0xFC]) + bytes(rdn - 1)
        return b''


class CsuPm(object):
    """CSU_PM's I2C slave (controller 3, address 4): a write's first two
    bytes select a CUS_REG register (big-endian index), two more are
    written to it; a read returns its two bytes (firmware/csupm
    src/board/i2c_slave.c)"""
    def __init__(self):
        self.regs = {}
        self.reg = 0
        self.soc_temp = (45, 0)          # CUS_TEMPT_REG1: degrees, hundredths

    def xfer(self, wr, rdn):
        if len(wr) >= 2:
            self.reg = (wr[0] << 8) | wr[1]
            if len(wr) >= 4:
                self.regs[self.reg] = bytes(wr[2:4])
        if rdn:
            if self.reg == 32:
                v = bytes(self.soc_temp)
            else:
                v = self.regs.get(self.reg, b'\0\0')
            return (v + bytes(rdn))[:rdn]
        return b''


# ADC data registers (low, high) of the virtual channels the O6 EC uses
ADC_DATA = {0: (0x1918, 0x1919), 1: (0x1907, 0x1908), 2: (0x190A, 0x190B),
            3: (0x190D, 0x190E), 4: (0x193A, 0x1939), 5: (0x193D, 0x193C)}
NTC_CH = {'SOC': 0, 'BRDEG': 1, 'HP': 2, 'DRAM': 3, 'USBC': 4, 'PMIC': 5}


def ntc_code(temp_c, beta=4250.0, r25=100.0, rup=24.0):
    """10-bit ADC code of a 100K NTC to ground under a 24K pull-up from
    3.3 V (R96248/R96249, ...) at temp_c"""
    import math
    r = r25 * math.exp(beta * (1.0 / (temp_c + 273.15) - 1.0 / 298.15))
    return max(0, min(1023, int(round(1023 * r / (r + rup)))))


class O6Board(NullBoard):
    """Orion O6 power tree and SoC, as the EC sees them.

    Rails follow their enables after a short delay; the SoC leaves reset
    after RSM_RST, and a PWRBTN_EC pulse moves it S5 -> S0 (SLP_S5_L and
    SLP_S3_L high, which the inverters Q3114/Q3112 turn into low
    SOC_SLP5/SOC_SLP3 at the EC).  `delays` are in ms."""
    RAIL_MS = 3

    def __init__(self, soc=True):
        self.soc = soc
        self.state = 'G3'

    def attach(self, ec):
        self.ec = ec
        self.csu = CsuPm()
        ec.smb['C'].devices[0x04] = self.csu
        self.temps = {'SOC': 40, 'BRDEG': 35, 'HP': None, 'DRAM': 38, 'USBC': 33, 'PMIC': 36}
        self.fan_max_rpm = 5000
        self.pd = Rts5453(ec, 'PD1')
        self.pd2 = Rts5453(ec, 'PD2')
        ec.smb['F'].devices[0x67] = self.pd
        ec.smb['F'].devices[0x68] = self.pd2
        d = ec.drive
        ec.pins.update({'B3': 1, 'B0': 0, 'B1': 1, 'C0': 1, 'D1': 0,
                        'D4': 1, 'F5': 1, 'H0': 1, 'B5': 1, 'H2': 1,
                        'H4': 0, 'H6': 0, 'J3': 0, 'H7': 1, 'A3': 1,
                        'A6': 1, 'A7': 1, 'E1': 0, 'G2': 0})

    def output(self, name, level):
        ec = self.ec
        if name == 'D7':                     # EC_ALW_EN -> SYS_ALW_PG
            ec.after(self.RAIL_MS, lambda: ec.drive('J3', level))
        elif name == 'J6':                   # EC_SLP_S5_L -> S3 rails
            ec.after(self.RAIL_MS, lambda: ec.drive('H6', level))
        elif name == 'D0':                   # EC_SLP_S3_L -> S0 rails
            ec.after(self.RAIL_MS, lambda: ec.drive('H4', level))
        elif name == 'J4' and self.soc:      # PWRBTN_EC: the SoC's power button
            # the SoC (its always-on domain) needs the ALW rails and RSM_RST released
            powered = ec.pin('D7') and ec.pin('H5') and not ec.pin('E2')
            if level == 1 and powered and ec.pin('B5') == 1:
                ec.after(20, self.soc_on)

    def fan_rpm(self):
        """the fan: speed proportional to the PWM2 duty (DCR2 of 256)"""
        return self.fan_max_rpm * self.ec.x[0x1804] // 255

    def read(self, a):
        if a in (0x181E, 0x181F):        # fan 1 tachometer (F1TLRR, F1TMRR)
            r = self.fan_rpm()
            t = min(0xFFFF, 2156250 // r) if r else 0
            return t & 0xFF if a == 0x181E else t >> 8
        for name, ch in NTC_CH.items():
            lo, hi = ADC_DATA[ch]
            if a in (lo, hi):
                t = self.temps.get(name)
                code = 0 if t is None else ntc_code(t)
                return code & 0xFF if a == lo else code >> 8
        return None

    def soc_on(self):
        ec = self.ec
        ec.drive('B5', 0)                    # SLP_S5_L high
        ec.after(5, lambda: ec.drive('H0', 0))   # SLP_S3_L high
        ec.after(30, lambda: ec.drive('D1', 1))  # reset released

    def soc_off(self, at_ms):
        """the OS shuts down: SLP_S3_L then SLP_S5_L go low"""
        ec = self.ec
        ec.at(at_ms, lambda: ec.drive('D1', 0))
        ec.at(at_ms + 5, lambda: ec.drive('H0', 1))
        ec.at(at_ms + 10, lambda: ec.drive('B5', 1))

    def soc_suspend(self, at_ms):
        ec = self.ec
        ec.at(at_ms, lambda: ec.drive('H0', 1))

    def press(self, at_ms, hold_ms=200):
        ec = self.ec
        ec.at(at_ms, lambda: ec.drive('B3', 0))
        ec.at(at_ms + hold_ms, lambda: ec.drive('B3', 1))


if __name__ == '__main__':
    import sys
    img = open(sys.argv[1], 'rb').read()
    ms = float(sys.argv[2]) if len(sys.argv) > 2 else 2000
    fl = open(sys.argv[3], 'rb').read() if len(sys.argv) > 3 else None
    if fl:
        fl = fl + b'\xff' * (0x800000 - len(fl))
    ec = EC(img, O6Board(), flash=fl)
    try:
        ec.run(ms)
    except Halt as e:
        print('halt', e)
    for e in ec.events:
        print('%10.3f %-4s %s' % e)
    print('pc %04X bank %d' % (ec.cpu.pc, ec.cpu.bank()))
