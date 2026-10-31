#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Print decompiled functions by address: show.py DECOMPILED.c ADDR..."""
import re, sys
funcs = re.split(r'\n(?=// [0-9a-f]{8} )', open(sys.argv[1]).read())
for a in sys.argv[2:]:
    a = a.lower().removeprefix('0x').zfill(8)
    for f in funcs:
        if f.startswith('// ' + a) or ('\n// ' + a) in f[:20]:
            print(f.strip(), '\n')
