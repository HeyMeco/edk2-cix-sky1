#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Recover CIX PINMUX_CFG / GPIO_CFG tables from built UEFI modules.

PinMuxTableLib and GpioTableLib are static, packed C arrays, so they appear
verbatim in the data section of the module that links them.  The scanner
looks for long runs of records whose fields all hold legal enum values.
"""

import argparse
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from uefi import SEC_PE32, find_fd, parse_fv  # noqa: E402

PINMUX = struct.Struct("<IIIIIIB")   # Domain Offset FuncSel PU PD ST DrvStren
GPIO = struct.Struct("<IIIII")       # Num Direction OutputVal IntrEn IntrType

ENUM3 = {0, 1, 0xFF}


def ok_pinmux(r):
    dom, off, fn, pu, pd, st, drv = r
    return (dom in (0, 1) and off % 4 == 0 and off < 0x400 and fn in (0, 1, 2, 3)
            and pu in ENUM3 and pd in ENUM3 and st in ENUM3
            and drv in (1, 2, 4, 8, 0xFF))


def ok_gpio(r):
    num, d, v, ie, it = r
    return (num < 160 and d in (0, 1, 0xFF) and v in ENUM3 and ie in ENUM3
            and it in (0, 1, 2, 3, 4, 0xFF))


def ok_next(prev, cur, st):
    # GPIO tables are sorted by pin number; zero-filled memory is not a table.
    if st is GPIO:
        return cur[0] > prev[0]
    return True


def scan(data, st, ok, min_run):
    """Return (offset, records) for every maximal run of >= min_run records."""
    out = []
    start = 0
    n = len(data)
    while start + st.size * min_run <= n:
        run = []
        pos = start
        while pos + st.size <= n:
            r = st.unpack_from(data, pos)
            if not ok(r) or (run and not ok_next(run[-1], r, st)):
                break
            run.append(r)
            pos += st.size
        if len(run) >= min_run:
            out.append((start, run))
            start = pos
        else:
            start += 1
    return out


def io_names(header):
    names = {}
    for m in re.finditer(r"#define\s+(IO_S([05])_\w+)\s+(0x[0-9A-Fa-f]+|\d+)", header):
        names[(0 if m.group(2) == "0" else 1, int(m.group(3), 0))] = m.group(1)
    return names


def modules(path, start):
    img = Path(path).read_bytes()
    fd = find_fd(img, start)
    for f in parse_fv(img[fd:]):
        for s in f.walk_sections({SEC_PE32}):
            yield f.ui_name or f.guid, s.data


def fmt_enum(v, names):
    return names.get(v, f"0x{v:X}")


PU = {0: "PU_DISABLE", 1: "PU_ENABLE", 0xFF: "PU_DEFAULT"}
PD = {0: "PD_DISABLE", 1: "PD_ENABLE", 0xFF: "PD_DEFAULT"}
ST = {0: "ST_DISABLE", 1: "ST_ENABLE", 0xFF: "ST_DEFAULT"}
DRV = {1: "DRV_STREN_0", 2: "DRV_STREN_1", 4: "DRV_STREN_2", 8: "DRV_STREN_3", 0xFF: "DRV_STREN_DEFAULT"}
DOM = {0: "S0_DOMAIN", 1: "S5_DOMAIN"}
DIR = {0: "OUTPUT", 1: "INPUT", 0xFF: "DIRECTION_DEFAULT"}
VAL = {0: "INOUT_LOW", 1: "INOUT_HIGH", 0xFF: "INOUT_VALUE_DEFAULT"}
IE = {0: "INTERRUPT_DISABLE", 1: "INTERRUPT_ENABLE", 0xFF: "INTERRUPT_ENABLE_DEFAULT"}
IT = {0: "LEVEL_HIGH", 1: "LEVEL_LOW", 2: "EDGE_HIGH", 3: "EDGE_LOW", 4: "ANY_EDGE", 0xFF: "INTERRUPT_TYPE_DEFAULT"}


def pinmux_lines(run, names):
    for d, o, fn, u, p, s, dr in run:
        pad = names.get((d, o), f"0x{o:03X}") + ","
        yield (f"  {{ {DOM[d] + ',':<16} {pad:<24} IO_FUNC0{fn},   {PU[u] + ',':<11} "
               f"{PD[p] + ',':<11} {ST[s] + ',':<11} {fmt_enum(dr, DRV)} }},")


def gpio_lines(run):
    for n, d, v, e, t in run:
        yield (f"  {{ {str(n) + ',':<5} {DIR[d] + ',':<18} {VAL[v] + ',':<20} "
               f"{IE[e] + ',':<25} {IT[t]:<22} }},")


def find_tables(image, start=0, module="PlatformDxe"):
    """Return (pinmux_run, gpio_run) found in *module* of *image*."""
    for name, pe in modules(image, start):
        if name != module:
            continue
        pm = scan(pe, PINMUX, ok_pinmux, 12)
        gp = scan(pe, GPIO, ok_gpio, 12)
        if len(pm) != 1 or len(gp) != 1:
            raise SystemExit(f"{module}: expected one table of each kind, found {len(pm)}/{len(gp)}")
        return pm[0][1], gp[0][1]
    raise SystemExit(f"{module} not found")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--start", default="0", help="search for the FD from this offset")
    ap.add_argument("--ioconfig", required=True, help="path to Silicon/CIX/Sky1/Include/IoConfig.h")
    a = ap.parse_args()
    names = io_names(Path(a.ioconfig).read_text())
    for name, pe in modules(a.image, int(a.start, 0)):
        for off, run in scan(pe, PINMUX, ok_pinmux, 12):
            print(f"// {name}: PINMUX_CFG table at 0x{off:X}, {len(run)} entries")
            print("\n".join(pinmux_lines(run, names)))
        for off, run in scan(pe, GPIO, ok_gpio, 12):
            print(f"// {name}: GPIO_CFG table at 0x{off:X}, {len(run)} entries")
            print("\n".join(gpio_lines(run)))


if __name__ == "__main__":
    main()
