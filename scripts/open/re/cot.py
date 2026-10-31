#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Decode the TF-A chain of trust (cot_desc) of a stripped BL2 image.

  cot.py BL2.bin BASE

Finds the auth_param_type_desc_t entries by the OID strings they point to,
then every auth_img_desc_t that references them, and prints each image's
parent, authentication methods and authenticated data (TF-A v2.7 layout).
"""
import re, struct, sys

d = open(sys.argv[1], 'rb').read()
base = int(sys.argv[2], 0)
q = lambda o: struct.unpack_from('<Q', d, o)[0]
inimg = lambda a: base <= a < base + len(d)
off = lambda a: a - base
PTYPES = {0: 'NONE', 1: 'RAW', 2: 'HASH', 3: 'PUB_KEY', 4: 'SIG', 5: 'SIG_ALG', 6: 'NV_CTR'}
MTYPES = {0: 'HASH', 1: 'SIG', 2: 'NV_CTR'}

# OID strings and the type descriptors {u32 type; void *cookie} pointing at them
oids = {base + m.start(): m.group().decode() for m in re.finditer(rb'1\.3\.6\.1\.4\.1\.4128\.2100\.\d+', d)}
tdesc = {}
for o in range(0, len(d) - 16, 8):
    t, c = struct.unpack_from('<IxxxxQ', d, o)
    if t in PTYPES and (c in oids or (c == 0 and t in (4, 5, 1))):
        tdesc[base + o] = (PTYPES[t], oids.get(c, '-').split('.')[-1])
def td(a): return '%s:%s' % tdesc[a] if a in tdesc else ('NULL' if a == 0 else hex(a))

# auth_img_desc_t {u32 id; u32 type; parent*; methods*; data*}
imgs = {}
for o in range(0, len(d) - 32, 8):
    iid, ity, par, meth, data = struct.unpack_from('<IIQQQ', d, o)
    if iid < 64 and ity < 3 and inimg(meth) and (data == 0 or inimg(data)) and (par == 0 or inimg(par)):
        mt = struct.unpack_from('<I', d, off(meth))[0]
        if mt in MTYPES and q(off(meth) + 8) in tdesc:
            imgs[base + o] = (iid, ity, par, meth, data)
for a, (iid, ity, par, meth, data) in sorted(imgs.items(), key=lambda x: x[1][0]):
    pid = imgs[par][0] if par in imgs else ('-' if par == 0 else hex(par))
    print(f'image {iid:2} ({["RAW", "CERT", "PLAT"][ity]}) at {a:#x}, parent {pid}')
    for i in range(4):                             # AUTH_METHOD_NUM
        mo = off(meth) + i * 40
        mt, p0, p1, p2, p3 = struct.unpack_from('<IxxxxQQQQ', d, mo)
        if (p0, p1) == (0, 0):
            break
        print(f'   method {MTYPES.get(mt, mt)}: {td(p0)} {td(p1)} {td(p2) if mt == 1 else ""}')
    if data:
        for i in range(8):                         # COT_MAX_VERIFIED_PARAMS
            pd = off(data) + i * 24
            t = q(pd)
            if t not in tdesc:
                break
            print(f'   data {td(t)} -> buf {q(pd + 8):#x} len {struct.unpack_from("<I", d, pd + 16)[0]:#x}')
