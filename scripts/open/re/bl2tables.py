#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Print the platform tables of the CIX Sky1 BL2 (TF-A v2.7 layouts).

  bl2tables.py BL2.bin BASE DESC_PTR COUNT_ADDR POLICY_ADDR POLICY_COUNT MMAP_ADDR

DESC_PTR: address of the pointer to the bl_mem_params_node_t array,
COUNT_ADDR: its element count (u32), POLICY_ADDR: struct plat_io_policy
array (indexed by image id), MMAP_ADDR: mmap_region_t array.
"""
import struct, sys, uuid
d = open(sys.argv[1], 'rb').read(); base = int(sys.argv[2], 0)
o = lambda a: a - base
q = lambda a: struct.unpack_from('<Q', d, o(a))[0]
u = lambda a: struct.unpack_from('<I', d, o(a))[0]
FIP = {'5ff9ec0b': 'BL31?'}
UU = {'47d4086d-4cfe-9846-9b95-2950cbbd5a00': 'soc-fw (BL31)', '05d0e189-53dc-1347-8d2b-500a4b7a3e38': 'tos-fw (BL32)',
      '0b70c29b-2a5a-7840-9f65-0a5682738288': 'tos-fw-extra1', '8ea87bb1-cfa2-3f4d-85fd-e7bba50220d9': 'tos-fw-extra2',
      'd6d0eea7-fcea-d54b-9782-9934f234b6e4': 'nt-fw (BL33)', '827ee890-f860-e411-a1b4-777a21b4f94c': 'trusted-key-cert',
      '8ab8becc-f960-e411-9ad0-eb4822d8dcf8': 'soc-fw-key-cert', '9477d603-fb60-e411-85dd-b7105b8cee04': 'tos-fw-key-cert',
      '8ad5832a-fb60-e411-8aaf-df30bbc49859': 'nt-fw-key-cert', 'e2b20c20-5e63-e411-9ce8-abccf92bb666': 'soc-fw-cert',
      'a49f4411-5e63-e411-8728-3f05722af33d': 'tos-fw-cert', '8ec4c1f3-5d63-e411-a7a9-87ee40b23fa7': 'nt-fw-cert',
      '9979814b-0376-fb46-8c8e-8d267f7859e0': 'soc-fw-config', '26257c1a-dbc6-7f47-8d96-c4c4b0248021': 'tos-fw-config',
      '28da9815-93e8-7e44-ac66-1aaf801550f9': 'nt-fw-config', '1a8f9c58-0b48-a54a-8b81-6bd8e0ea4c55': 'tb-fw-config?'}
desc, n = q(int(sys.argv[3], 0)), u(int(sys.argv[4], 0))
print(f'bl_mem_params_desc: {n} entries at {desc:#x}')
for i in range(n):
    e = desc + i * 0xb8
    iid = u(e)
    ih_attr = u(e + 0x8 + 4); ibase = q(e + 0x10); imax = u(e + 0x1c)
    ep_attr = u(e + 0x20 + 4); pc = q(e + 0x28); spsr = u(e + 0x30); args = [q(e + 0x38 + 8 * k) for k in range(8)]
    nxt = struct.unpack_from('<i', d, o(e + 0x78))[0]
    print(f'  id {iid:2}: image attr {ih_attr:#x} base {ibase:#x} max {imax:#x} | ep attr {ep_attr:#x} pc {pc:#x} spsr {spsr:#x} args {[hex(a) for a in args if a]} | next {nxt}')
pol, pn = int(sys.argv[5], 0), int(sys.argv[6], 0)
print('io policies (by image id):')
for i in range(pn):
    dev, spec, chk = q(pol + 0x18 * i), q(pol + 0x18 * i + 8), q(pol + 0x18 * i + 16)
    if dev == 0 and spec == 0:
        continue
    raw = d[o(spec):o(spec) + 16] if base <= spec < base + len(d) else b''
    us = str(uuid.UUID(bytes=raw)) if raw else ''
    blk = f'offset {q(spec):#x} len {q(spec + 8):#x}' if raw else ''
    print(f'  id {i:2}: dev {dev:#x} spec {spec:#x} {UU.get(us, blk if us not in UU else us)} check {chk:#x}')
mm = int(sys.argv[7], 0)
print('plat mmap:')
while True:
    pa, va, size, attr, gran = struct.unpack_from('<QQQIxxxxQ', d, o(mm))
    if size == 0:
        break
    print(f'  pa {pa:#x} va {va:#x} size {size:#x} attr {attr:#x}')
    mm += 0x28
