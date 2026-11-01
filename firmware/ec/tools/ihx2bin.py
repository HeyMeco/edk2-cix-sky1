#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Intel HEX -> binary of a fixed size, 0xFF where nothing is linked."""
import sys

src, dst, size = sys.argv[1], sys.argv[2], int(sys.argv[3], 0)
img = bytearray(b'\xff' * size)
base = 0
for line in open(src):
    line = line.strip()
    if not line.startswith(':'):
        continue
    b = bytes.fromhex(line[1:])
    n, a, t = b[0], (b[1] << 8) | b[2], b[3]
    if t == 0:
        a += base
        if a + n > size:
            sys.exit('%s: data past %#x' % (src, size))
        img[a:a + n] = b[4:4 + n]
    elif t == 4:
        base = ((b[4] << 8) | b[5]) << 16
    elif t == 1:
        break
open(dst, 'wb').write(img)
