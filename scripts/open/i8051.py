# SPDX-License-Identifier: BSD-2-Clause-Patent
"""MCS-51 (8051) instruction decoder and disassembler.

Used for the EC (ITE IT5570) firmware.  decode(code, pc) returns
(length, mnemonic, operands, targets) where targets are the static code
addresses the instruction can transfer to (branches and calls).
"""

REG = ['R%d' % i for i in range(8)]

SFR_NAMES = {
    0x80: 'P0', 0x81: 'SP', 0x82: 'DPL', 0x83: 'DPH', 0x87: 'PCON',
    0x88: 'TCON', 0x89: 'TMOD', 0x8A: 'TL0', 0x8B: 'TL1', 0x8C: 'TH0',
    0x8D: 'TH1', 0x90: 'P1', 0x98: 'SCON', 0x99: 'SBUF', 0xA0: 'P2',
    0xA8: 'IE', 0xB0: 'P3', 0xB8: 'IP', 0xC8: 'T2CON', 0xC9: 'T2MOD',
    0xCA: 'RCAP2L', 0xCB: 'RCAP2H', 0xCC: 'TL2', 0xCD: 'TH2', 0xD0: 'PSW',
    0xE0: 'ACC', 0xF0: 'B',
}

BIT_NAMES = {
    0x88: 'IT0', 0x89: 'IE0', 0x8A: 'IT1', 0x8B: 'IE1', 0x8C: 'TR0',
    0x8D: 'TF0', 0x8E: 'TR1', 0x8F: 'TF1', 0x98: 'RI', 0x99: 'TI',
    0xA8: 'EX0', 0xA9: 'ET0', 0xAA: 'EX1', 0xAB: 'ET1', 0xAC: 'ES',
    0xAD: 'ET2', 0xAF: 'EA', 0xC8: 'CP_RL2', 0xCA: 'TR2', 0xCF: 'TF2',
    0xD0: 'P', 0xD2: 'OV', 0xD3: 'RS0', 0xD4: 'RS1', 0xD5: 'F0',
    0xD6: 'AC', 0xD7: 'CY',
}


def direct(a):
    if a >= 0x80:
        return SFR_NAMES.get(a, '0x%02X' % a)
    return '0x%02X' % a


def bitaddr(b):
    if b in BIT_NAMES:
        return BIT_NAMES[b]
    if b < 0x80:
        return '0x%02X.%d' % (0x20 + (b >> 3), b & 7)
    base = b & 0xF8
    return '%s.%d' % (direct(base), b & 7)


def rel(pc, length, off):
    if off >= 0x80:
        off -= 0x100
    return (pc + length + off) & 0xFFFF


# (mnemonic, length, operand format); formats are decoded in decode()
_OPS = {}


def _op(code, mn, ln, fmt=''):
    _OPS[code] = (mn, ln, fmt)


_op(0x00, 'NOP', 1)
_op(0x02, 'LJMP', 3, 'a16')
_op(0x03, 'RR', 1, 'A')
_op(0x04, 'INC', 1, 'A')
_op(0x05, 'INC', 2, 'd')
_op(0x06, 'INC', 1, '@R0')
_op(0x07, 'INC', 1, '@R1')
_op(0x10, 'JBC', 3, 'b,r')
_op(0x12, 'LCALL', 3, 'a16')
_op(0x13, 'RRC', 1, 'A')
_op(0x14, 'DEC', 1, 'A')
_op(0x15, 'DEC', 2, 'd')
_op(0x16, 'DEC', 1, '@R0')
_op(0x17, 'DEC', 1, '@R1')
_op(0x20, 'JB', 3, 'b,r')
_op(0x22, 'RET', 1)
_op(0x23, 'RL', 1, 'A')
_op(0x24, 'ADD', 2, 'A,#')
_op(0x25, 'ADD', 2, 'A,d')
_op(0x26, 'ADD', 1, 'A,@R0')
_op(0x27, 'ADD', 1, 'A,@R1')
_op(0x30, 'JNB', 3, 'b,r')
_op(0x32, 'RETI', 1)
_op(0x33, 'RLC', 1, 'A')
_op(0x34, 'ADDC', 2, 'A,#')
_op(0x35, 'ADDC', 2, 'A,d')
_op(0x36, 'ADDC', 1, 'A,@R0')
_op(0x37, 'ADDC', 1, 'A,@R1')
_op(0x40, 'JC', 2, 'r')
_op(0x42, 'ORL', 2, 'd,A')
_op(0x43, 'ORL', 3, 'd,#')
_op(0x44, 'ORL', 2, 'A,#')
_op(0x45, 'ORL', 2, 'A,d')
_op(0x46, 'ORL', 1, 'A,@R0')
_op(0x47, 'ORL', 1, 'A,@R1')
_op(0x50, 'JNC', 2, 'r')
_op(0x52, 'ANL', 2, 'd,A')
_op(0x53, 'ANL', 3, 'd,#')
_op(0x54, 'ANL', 2, 'A,#')
_op(0x55, 'ANL', 2, 'A,d')
_op(0x56, 'ANL', 1, 'A,@R0')
_op(0x57, 'ANL', 1, 'A,@R1')
_op(0x60, 'JZ', 2, 'r')
_op(0x62, 'XRL', 2, 'd,A')
_op(0x63, 'XRL', 3, 'd,#')
_op(0x64, 'XRL', 2, 'A,#')
_op(0x65, 'XRL', 2, 'A,d')
_op(0x66, 'XRL', 1, 'A,@R0')
_op(0x67, 'XRL', 1, 'A,@R1')
_op(0x70, 'JNZ', 2, 'r')
_op(0x72, 'ORL', 2, 'C,b')
_op(0x73, 'JMP', 1, '@A+DPTR')
_op(0x74, 'MOV', 2, 'A,#')
_op(0x75, 'MOV', 3, 'd,#')
_op(0x76, 'MOV', 2, '@R0,#')
_op(0x77, 'MOV', 2, '@R1,#')
_op(0x80, 'SJMP', 2, 'r')
_op(0x82, 'ANL', 2, 'C,b')
_op(0x83, 'MOVC', 1, 'A,@A+PC')
_op(0x84, 'DIV', 1, 'AB')
_op(0x85, 'MOV', 3, 'dd')
_op(0x86, 'MOV', 2, 'd,@R0')
_op(0x87, 'MOV', 2, 'd,@R1')
_op(0x90, 'MOV', 3, 'DPTR,#16')
_op(0x92, 'MOV', 2, 'b,C')
_op(0x93, 'MOVC', 1, 'A,@A+DPTR')
_op(0x94, 'SUBB', 2, 'A,#')
_op(0x95, 'SUBB', 2, 'A,d')
_op(0x96, 'SUBB', 1, 'A,@R0')
_op(0x97, 'SUBB', 1, 'A,@R1')
_op(0xA0, 'ORL', 2, 'C,/b')
_op(0xA2, 'MOV', 2, 'C,b')
_op(0xA3, 'INC', 1, 'DPTR')
_op(0xA4, 'MUL', 1, 'AB')
_op(0xA5, '.DB', 1, 'A5')
_op(0xA6, 'MOV', 2, '@R0,d')
_op(0xA7, 'MOV', 2, '@R1,d')
_op(0xB0, 'ANL', 2, 'C,/b')
_op(0xB2, 'CPL', 2, 'b')
_op(0xB3, 'CPL', 1, 'C')
_op(0xB4, 'CJNE', 3, 'A,#,r')
_op(0xB5, 'CJNE', 3, 'A,d,r')
_op(0xB6, 'CJNE', 3, '@R0,#,r')
_op(0xB7, 'CJNE', 3, '@R1,#,r')
_op(0xC0, 'PUSH', 2, 'd')
_op(0xC2, 'CLR', 2, 'b')
_op(0xC3, 'CLR', 1, 'C')
_op(0xC4, 'SWAP', 1, 'A')
_op(0xC5, 'XCH', 2, 'A,d')
_op(0xC6, 'XCH', 1, 'A,@R0')
_op(0xC7, 'XCH', 1, 'A,@R1')
_op(0xD0, 'POP', 2, 'd')
_op(0xD2, 'SETB', 2, 'b')
_op(0xD3, 'SETB', 1, 'C')
_op(0xD4, 'DA', 1, 'A')
_op(0xD5, 'DJNZ', 3, 'd,r')
_op(0xD6, 'XCHD', 1, 'A,@R0')
_op(0xD7, 'XCHD', 1, 'A,@R1')
_op(0xE0, 'MOVX', 1, 'A,@DPTR')
_op(0xE2, 'MOVX', 1, 'A,@R0')
_op(0xE3, 'MOVX', 1, 'A,@R1')
_op(0xE4, 'CLR', 1, 'A')
_op(0xE5, 'MOV', 2, 'A,d')
_op(0xE6, 'MOV', 1, 'A,@R0')
_op(0xE7, 'MOV', 1, 'A,@R1')
_op(0xF0, 'MOVX', 1, '@DPTR,A')
_op(0xF2, 'MOVX', 1, '@R0,A')
_op(0xF3, 'MOVX', 1, '@R1,A')
_op(0xF4, 'CPL', 1, 'A')
_op(0xF5, 'MOV', 2, 'd,A')
_op(0xF6, 'MOV', 1, '@R0,A')
_op(0xF7, 'MOV', 1, '@R1,A')
for _i in range(8):
    _op(0x01 + _i * 0x20, 'AJMP', 2, 'a11')
    _op(0x11 + _i * 0x20, 'ACALL', 2, 'a11')
    _op(0x08 + _i, 'INC', 1, 'Rn')
    _op(0x18 + _i, 'DEC', 1, 'Rn')
    _op(0x28 + _i, 'ADD', 1, 'A,Rn')
    _op(0x38 + _i, 'ADDC', 1, 'A,Rn')
    _op(0x48 + _i, 'ORL', 1, 'A,Rn')
    _op(0x58 + _i, 'ANL', 1, 'A,Rn')
    _op(0x68 + _i, 'XRL', 1, 'A,Rn')
    _op(0x78 + _i, 'MOV', 2, 'Rn,#')
    _op(0x88 + _i, 'MOV', 2, 'd,Rn')
    _op(0x98 + _i, 'SUBB', 1, 'A,Rn')
    _op(0xA8 + _i, 'MOV', 2, 'Rn,d')
    _op(0xB8 + _i, 'CJNE', 3, 'Rn,#,r')
    _op(0xC8 + _i, 'XCH', 1, 'A,Rn')
    _op(0xD8 + _i, 'DJNZ', 2, 'Rn,r')
    _op(0xE8 + _i, 'MOV', 1, 'A,Rn')
    _op(0xF8 + _i, 'MOV', 1, 'Rn,A')
assert len(_OPS) == 256


def decode(code, pc, base=0):
    """Decode the instruction at code address pc; code[pc - base] is its
    first byte."""
    i = pc - base
    op = code[i]
    mn, ln, fmt = _OPS[op]
    b = list(code[i + 1:i + ln]) + [0] * 3
    n = REG[op & 7]
    tg = []
    if fmt == 'a16':
        t = (b[0] << 8) | b[1]
        tg = [t]
        s = '0x%04X' % t
    elif fmt == 'a11':
        t = ((pc + 2) & 0xF800) | ((op & 0xE0) << 3) | b[0]
        tg = [t]
        s = '0x%04X' % t
    elif fmt == 'r':
        t = rel(pc, ln, b[0])
        tg = [t]
        s = '0x%04X' % t
    elif fmt == 'b,r':
        t = rel(pc, ln, b[1])
        tg = [t]
        s = '%s,0x%04X' % (bitaddr(b[0]), t)
    elif fmt == 'd,r':
        t = rel(pc, ln, b[1])
        tg = [t]
        s = '%s,0x%04X' % (direct(b[0]), t)
    elif fmt == 'Rn,r':
        t = rel(pc, ln, b[0])
        tg = [t]
        s = '%s,0x%04X' % (n, t)
    elif fmt.endswith(',#,r'):
        t = rel(pc, ln, b[1])
        tg = [t]
        lhs = fmt.split(',')[0].replace('Rn', n)
        s = '%s,#0x%02X,0x%04X' % (lhs, b[0], t)
    elif fmt == 'A,d,r':
        t = rel(pc, ln, b[1])
        tg = [t]
        s = 'A,%s,0x%04X' % (direct(b[0]), t)
    elif fmt == 'DPTR,#16':
        s = 'DPTR,#0x%04X' % ((b[0] << 8) | b[1])
    elif fmt == 'dd':
        s = '%s,%s' % (direct(b[1]), direct(b[0]))  # MOV dst,src: src first
    elif fmt == 'd,#':
        s = '%s,#0x%02X' % (direct(b[0]), b[1])
    else:
        s = fmt.replace('Rn', n)
        if '/b' in s:
            s = s.replace('/b', '/' + bitaddr(b[0]))
        elif s in ('b', 'C,b', 'b,C'):
            s = s.replace('b', bitaddr(b[0]), 1) if s != 'C,b' else 'C,' + bitaddr(b[0])
        if '#' in s and fmt != 'DPTR,#16':
            s = s.replace('#', '#0x%02X' % b[0])
        if fmt in ('d', 'A,d', 'd,A', 'Rn,d', '@R0,d', '@R1,d', 'd,Rn',
                   'd,@R0', 'd,@R1'):
            s = s.replace('d', direct(b[0]))
    return ln, mn, s, tg


def is_terminal(mn):
    return mn in ('LJMP', 'AJMP', 'SJMP', 'RET', 'RETI', 'JMP')


def disasm(code, start, end, base=0):
    pc = start
    out = []
    while pc < end:
        ln, mn, s, _ = decode(code, pc, base)
        raw = ' '.join('%02X' % x for x in code[pc - base:pc - base + ln])
        out.append('%04X: %-9s %-6s %s' % (pc, raw, mn, s))
        pc += ln
    return out


if __name__ == '__main__':
    import sys
    data = open(sys.argv[1], 'rb').read()
    s = int(sys.argv[2], 16)
    e = int(sys.argv[3], 16)
    base = int(sys.argv[4], 16) if len(sys.argv) > 4 else 0
    print('\n'.join(disasm(data, s, e, base)))
