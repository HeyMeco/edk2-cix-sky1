# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Cross references in the vendor EC image: where a string (Keil generic
pointer R3=0xFF, R2:R1 = code address) or a code address is used."""
import sys
from i8051 import decode

def load(path):
    return open(path, 'rb').read()

def flash_off(bank, addr):
    return addr if addr < 0x8000 else bank * 0x8000 + (addr - 0x8000)

def find_string(img, s):
    i = img.find(s.encode())
    return i

def string_refs(img, off):
    # the string is addressed as seen from its bank
    if off < 0x8000:
        addrs = [off]
    else:
        addrs = [0x8000 + (off & 0x7FFF)]
    out = []
    for a in addrs:
        pat = bytes([0x7A, a >> 8, 0x79, a & 0xFF])
        j = img.find(pat)
        while j >= 0:
            out.append(j)
            j = img.find(pat, j + 1)
    return out

def callers(img, target_addr):
    """LCALL/LJMP to target (code address) anywhere, plus bank stubs"""
    pat = [bytes([0x12, target_addr >> 8, target_addr & 0xFF]), bytes([0x02, target_addr >> 8, target_addr & 0xFF])]
    out = []
    for p in pat:
        j = img.find(p)
        while j >= 0:
            out.append(j)
            j = img.find(p, j + 1)
    return out

def stub_for(img, bank_addr):
    """bank stub (0x2AA0..0x3436) that jumps to bank_addr"""
    pc = 0x2AA0
    res = []
    while img[pc] == 0x90 and img[pc + 3] == 0x02 and img[pc + 4] == 0x2A:
        if (img[pc + 1] << 8 | img[pc + 2]) == bank_addr:
            res.append((pc, {0: 1, 20: 2, 40: 3}.get(img[pc + 5])))
        pc += 6
    return res

if __name__ == '__main__':
    img = load(sys.argv[1])
    for s in sys.argv[2:]:
        off = find_string(img, s)
        print(s, hex(off), [hex(r) for r in string_refs(img, off)])
