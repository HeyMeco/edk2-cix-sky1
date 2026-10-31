#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Find which decompiled functions reference given strings.

  strref.py IMAGE.bin BASE DECOMPILED.c STRING...

Looks up each string's address in the image and prints the functions of
the Ghidra output (decompile.sh) whose body mentions that address."""
import re, sys
img, base, dec = open(sys.argv[1], 'rb').read(), int(sys.argv[2], 0), open(sys.argv[3]).read()
funcs = re.split(r'\n(?=// [0-9a-f]{8} )', dec)
for s in sys.argv[4:]:
    i = img.find(s.encode())
    while i > 0 and img[i - 1] != 0:          # start of the C string
        i -= 1
    if img.find(s.encode()) < 0:
        print(f'{s!r}: not in image'); continue
    a = base + i
    hits = [f.split('\n', 1)[0] for f in funcs if re.search(r'0x0*%x\b|s_\w*_%x\b|DAT_%x\b' % (a, a, a), f)]
    print(f'{s!r} @ {a:#x}: {hits}')
