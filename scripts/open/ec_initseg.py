# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Decode the Keil C51 initialised-data table (?C_INITSEG) of the vendor
EC image: the startup code at 0x00F0 walks records from 0x073F."""
import sys

def parse(img, start=0x073F):
    p = start
    out = []
    while True:
        b = img[p]; p += 1
        if b == 0:
            break
        n = b & 0x3F
        if b & 0x20:
            n = ((b & 0x1F) << 8) | img[p]; p += 1
        kind = b & 0xC0
        if kind == 0x00:                 # idata
            a = img[p]; p += 1
            out.append(('idata', a, img[p:p + n])); p += n
        elif kind == 0xC0 or kind == 0x80:   # bits
            for _ in range(n):
                out.append(('bit', img[p], None)); p += 1
        else:                            # xdata
            a = (img[p] << 8) | img[p + 1]; p += 2
            out.append(('xdata', a, img[p:p + n])); p += n
    return out, p

if __name__ == '__main__':
    img = open(sys.argv[1], 'rb').read()
    recs, end = parse(img)
    for k, a, d in recs:
        print('%-5s %04X %s' % (k, a, d.hex(' ') if d is not None else ''))
    print('end', hex(end))
