#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Split an iasl -d listing into per-device text blocks keyed by ACPI path."""

import re
import sys

_OPEN = re.compile(r'^\s*(Device|Scope|ThermalZone|PowerResource|Processor)\s*\(([^,)]+)')


def _join(scope, name):
    name = name.strip()
    if name.startswith('\\'):
        return name
    while name.startswith('^'):
        scope = scope.rsplit('.', 1)[0]
        name = name[1:]
    return f'{scope}.{name}' if scope else '\\' + name


def devices(text):
    """Return {path: body_text} for every Device; body excludes child devices."""
    lines = [l for l in text.splitlines() if not l.lstrip().startswith(('/*', '*', '//'))]
    stack = []        # (kind, path, depth_at_open)
    depth = 0
    pending = None
    out = {}
    for line in lines:
        m = _OPEN.match(line)
        if m:
            parent = stack[-1][1] if stack else ''
            pending = (m.group(1), _join(parent, m.group(2).strip('"').replace('_SB_', '_SB')))
        opens = line.count('{')
        closes = line.count('}')
        if pending and opens:
            stack.append((pending[0], pending[1], depth))
            if pending[0] == 'Device':
                out.setdefault(pending[1], [])
            pending = None
        elif stack and stack[-1][0] == 'Device':
            out[stack[-1][1]].append(re.sub(r'\s+', ' ', line.strip()))
        depth += opens - closes
        while stack and depth <= stack[-1][2]:
            stack.pop()
    return {k: '\n'.join(v) for k, v in out.items()}


if __name__ == '__main__':
    a = devices(open(sys.argv[1], errors='replace').read())
    b = devices(open(sys.argv[2], errors='replace').read())
    for p in sorted(set(a) - set(b)):
        print('REMOVED', p)
    for p in sorted(set(b) - set(a)):
        print('ADDED  ', p)
    for p in sorted(set(a) & set(b)):
        if a[p] != b[p]:
            print('CHANGED', p)
