#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Compare the open Orion O6 EC firmware (firmware/ec) with Radxa's EC V1.07
in the IT5570 + O6 board model (ec_emu.py).

Each scenario runs both images on the same board and compares what the
board sees, in order: pin changes (the EC's outputs), I2C transfers that a
device answered, host-command responses (bytes on the wire), e-flash
programming.  Times must agree within a tolerance; the vendor's probes of
devices the O6 does not have (NACKed) and its debug output are not
compared.

  test_ec.py [--open IMG] [--vendor IMG] [--flash SPI] [-v] [scenario...]

Run it under PyPy (run_ec_tests.sh does: pypy3 on PATH or $PYPY, else
python3): the emulator is ~28x faster there, the suite ~1 min instead of
~25.  This file, ec_emu.py and i8051_emu.py need nothing beyond the
standard library.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from ec_emu import EC, O6Board, ec_request, ec_response   # noqa: E402

ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
VENDOR = os.path.join(ROOT, 'edk2-project/Platform/Radxa/Orion/O6/Firmwares/ec_fw.bin')
OPEN = os.path.join(ROOT, 'firmware/ec/build/ec_fw.bin')
FLASH = os.path.join(ROOT, 'output/orion-o6/RELEASE/cix_flash_all.bin')


class Host:
    """the SoC side of the host interface: commands in order, results kept"""
    def __init__(self, ec):
        self.ec = ec
        self.q = []
        self.res = []

    def at(self, ms, cmds):
        def go(i=0):
            if i >= len(cmds):
                return
            c, d, n = cmds[i]

            def done(raw, c=c, d=d):
                r, data = ec_response(raw)
                self.res.append((round(self.ec.ms), c, bytes(d), r, bytes(data), bytes(raw)))
                self.ec.ev('resp', '%04X %s -> %s' % (c, bytes(d).hex(), bytes(raw).hex(' ')))
                self.ec.after(20, lambda: go(i + 1))
            self.ec.slave['B'].transfer(ec_request(c, d), 10 + n, done)
        self.ec.at(ms, go)


# ---------------------------------------------------------------- scenarios
def sc_boot(ec, b, h):
    """AC applied: standby, PD set-up"""
    return 3000


def sc_poweron(ec, b, h):
    """standby, button, S0, a while in S0"""
    b.press(2000, 300)
    return 16000


def sc_shutdown(ec, b, h):
    """S0, OS shutdown: S3, S5 off, G3"""
    b.press(2000, 300)
    b.soc_off(16000)
    return 21000


def sc_g3_button(ec, b, h):
    """after a shutdown to G3, the button powers on again"""
    b.press(2000, 300)
    b.soc_off(8000)
    b.press(14000, 300)
    return 20000


def sc_short_press(ec, b, h):
    """a short press in S0: mirrored to the SoC, a host event"""
    b.press(2000, 300)
    b.press(9000, 300)
    h.at(10000, [(0x55, b'', 8), (0x55, b'', 8)])
    return 11000


def sc_long_press(ec, b, h):
    """a 6 s hold in S0: the EC holds the SoC's button"""
    b.press(2000, 300)
    b.press(9000, 6000)
    return 16000


def sc_ac_policy_on(ec, b, h):
    """AC-loss policy 1 in the e-flash: powers on by itself"""
    ec.eflash.data[0x1F8FF] = 1
    return 4000


def sc_ac_policy_erased(ec, b, h):
    """AC-loss byte erased (a fresh EC image): powers on by itself too"""
    ec.eflash.data[0x1F8FF] = 0xFF
    return 4000


def sc_fan(ec, b, h):
    """the SoC heats up and cools down: the fan's states"""
    b.press(2000, 300)

    def heat(t):
        def f():
            b.csu.soc_temp = (t, 0)
            b.temps['SOC'] = t
        return f
    for i, t in enumerate([45, 62, 74, 82, 70, 55, 30]):
        ec.at(9000 + i * 10000, heat(t))
    return 80000


def sc_fan_modes(ec, b, h):
    """fan modes from the host (0x52): mute, performance, auto"""
    b.press(2000, 300)
    h.at(5000, [(0x52, b'\x02', 0)])
    h.at(15000, [(0x52, b'\x04', 0)])
    h.at(25000, [(0x52, b'\x01', 0)])
    return 36000


def sc_fan_rpm(ec, b, h):
    """a target fan speed (0x21): the duty corrected from the tachometer,
    the measured speed read back (0x20), then a fixed duty (0x25)"""
    b.press(2000, 300)
    h.at(5000, [(0x21, b'\x00\x00\x0f\xa0', 0), (0x20, b'', 4)])
    h.at(30000, [(0x20, b'', 4), (0x21, b'\x00\x00\x07\xd0', 0)])
    h.at(50000, [(0x20, b'', 4), (0x25, b'\x00\x28', 0), (0x20, b'', 4)])
    return 60000


def sc_overheat(ec, b, h):
    """a board NTC at 90 C: THERMTRIP_EC"""
    b.press(2000, 300)
    ec.at(5000, lambda: b.temps.__setitem__('DRAM', 90))
    return 16000


HOST_CMDS = [
    (0x3FFF, b'', 32), (0x000B, b'', 16), (0x3E01, b'', 4), (0x0052, b'\x01', 0),
    (0x3E0B, b'\x00', 0), (0x0055, b'', 8), (0x0020, b'', 4),
    (0x0021, b'\x00\x00\x0b\xb8', 0), (0x0020, b'', 4), (0x0026, b'\x00', 4),
    (0x0025, b'\x00\x32', 0), (0x0026, b'\x00', 4), (0x0026, b'\x01', 4),
    (0x0022, b'', 4), (0x0023, b'\x10', 0), (0x0022, b'', 4), (0x002C, b'', 4),
    (0x0031, b'\x01\x02\x03\x04\x05', 0), (0x0032, b'', 8), (0x3E03, b'', 4),
    (0x3E06, b'', 4), (0x3E07, b'', 8), (0x3E09, b'', 4), (0x3E0A, b'', 4),
    (0x3E0C, b'', 4), (0x3E0E, b'\x00', 4), (0x3E0E, b'\x01', 4), (0x3E13, b'', 4),
    (0x3E20, b'', 4), (0x3E21, b'\x01', 0), (0x3E22, b'\x01', 0), (0x0600, b'', 4),
    (0x0601, b'', 4), (0x3E02, b'', 4), (0x3E14, b'', 4), (0x1234, b'', 4),
    (0x0093, b'\x4a', 2), (0x0092, b'\x4a\x00', 0), (0x0093, b'\x4a', 2),
    (0x0092, b'\x4a\x01', 0), (0x0093, b'\x4a', 2), (0x0092, b'\xcb\x01', 0),
    (0x0093, b'\x6b', 2), (0x0092, b'\x6b\x01', 0),
]


def sc_host(ec, b, h):
    """every host command the vendor answers, with parameters"""
    b.press(2000, 300)
    h.at(15000, HOST_CMDS)
    return 18000


def sc_host_standby(ec, b, h):
    """host commands in standby (UEFI is not running, but the bus is)"""
    h.at(1500, [(0x3FFF, b'', 32), (0x3E09, b'', 4), (0x3E0C, b'', 4)])
    return 2500


def sc_autoflag(ec, b, h):
    """UEFI sets the AC-loss policy (3E0B): the e-flash is programmed"""
    b.press(2000, 300)
    h.at(6000, [(0x3E0B, b'\x01', 0), (0x3E0B, b'\x01', 0), (0x3E0B, b'\x00', 0)])
    return 7000


def sc_force_reset(ec, b, h):
    """UEFI's platform reset (3E19): flag saved, rails off, mirror reset"""
    b.press(2000, 300)
    h.at(6000, [(0x3E0B, b'\x01', 0), (0x3E19, b'\x00', 0)])
    return 9000


SCENARIOS = [sc_boot, sc_poweron, sc_shutdown, sc_g3_button, sc_short_press,
             sc_long_press, sc_ac_policy_on, sc_ac_policy_erased, sc_fan, sc_fan_modes, sc_fan_rpm, sc_overheat,
             sc_host, sc_host_standby, sc_autoflag, sc_force_reset]


# ---------------------------------------------------------------- running
def run(img, flash, sc):
    b = O6Board()
    ec = EC(img, b, flash=flash, log_regs=True)
    h = Host(ec)
    ec.eflash.data[0x1F8FF] = 0      # AC-loss policy: stay off, unless a scenario sets it
    end = sc(ec, b, h)
    ec.run(end)
    return ec, h


# the vendor's I2C bus recovery toggles the PD bus pins as GPIOs (they are
# in alternate-function mode, so nothing reaches the bus)
IGNORE_PINS = {'A4', 'A5'}


def board_events(ec, start=0):
    """what the board sees, in order: (ms, key)"""
    out = []
    for ms, kind, d in ec.events:
        if ms < start:
            continue
        if kind == 'out':
            pin = d.split()[0]
            if pin in IGNORE_PINS:
                continue
            out.append((ms, 'pin ' + d))
        elif kind == 'i2c' and not d.endswith('NACK') and 'w[ca]' not in d:
            # (w[ca]: the vendor's scan for a part the O6 lacks, which
            # PD2 happens to acknowledge)
            out.append((ms, 'i2c ' + d))
        elif kind == 'resp':
            if d.startswith('3FFF'):        # the version string differs by design
                d = d[:4] + ' <version>'
            out.append((ms, 'host ' + d))
        elif kind in ('eflash', 'spi'):
            out.append((ms, kind + ' ' + d))
    for ms, k, a, v in ec.reglog:
        if ms < start:
            continue
        # the mirror reset: SMFI 1063 = 0x18, the watchdog
        if k == 'W' and (a in (0x1F01, 0x1F07) or (a == 0x1063 and v == 0x18)):
            out.append((ms, 'reg %04X=%02X' % (a, v)))
        if k == 'W' and a == 0x1804:
            out.append((ms, 'fan DCR2=%02X' % v))
    out.sort(key=lambda e: e[0])
    # a register written with the same value again is not an event
    res, last = [], {}
    for ms, key in out:
        if key.startswith('fan '):
            if last.get('fan') == key:
                continue
            last['fan'] = key
        res.append((ms, key))
    return res


def compare(name, ve, oe, tol_ms, verbose):
    import difflib
    vk = [k for _, k in ve]
    ok_ = [k for _, k in oe]
    sm = difflib.SequenceMatcher(a=vk, b=ok_, autojunk=False)
    diffs = []
    late = []
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == 'equal':
            for i, j in zip(range(i1, i2), range(j1, j2)):
                dt = oe[j][0] - ve[i][0]
                if abs(dt) > tol_ms(ve[i][1]):
                    late.append((ve[i][0], oe[j][0], ve[i][1]))
        else:
            for i in range(i1, i2):
                diffs.append(('vendor', ve[i][0], ve[i][1]))
            for j in range(j1, j2):
                diffs.append(('open  ', oe[j][0], oe[j][1]))
    n = len(vk)
    same = sum(i2 - i1 for t, i1, i2, _, _ in sm.get_opcodes() if t == 'equal')
    status = 'same' if not diffs and not late else 'DIFF'
    print('%-16s %-4s %d/%d events in order, %d timing' % (name, status, same, n, len(late)))
    if verbose or status != 'same':
        for who, ms, k in diffs[:60]:
            print('    %s %10.1f %s' % (who, ms, k))
        for v, o, k in late[:20]:
            print('    time  vendor %10.1f open %10.1f  %s' % (v, o, k))
    return status == 'same'


def tolerance(key):
    # what the vendor does in its slow periodic hooks may run up to one
    # period away; sequencer steps and pin mirroring must be close
    # the thermal pass runs in the vendor's slow hook, and what it causes
    # (a trip's e-flash record) with it
    if key.startswith(('i2c C', 'fan ', 'eflash', 'reg 1F01')):
        return 4000
    # done in the vendor's ~2 s hook: within a period of it
    if key.startswith(('pin C6', 'pin I7', 'pin H5 +1.8V_S5_EN=0', 'pin D7 EC_ALW_EN=0')):
        return 2500
    if key.startswith(('i2c F', 'host')):
        return 1500
    return 900


def cached(img, flash, sc):
    """the vendor's events for a scenario, cached by image, model and
    scenario source (out/ec_cache)"""
    import hashlib, inspect, json
    h = hashlib.sha256(img)
    for f in ('ec_emu.py', 'i8051_emu.py', 'test_ec.py'):
        h.update(open(os.path.join(HERE, f), 'rb').read())
    h.update(inspect.getsource(sc).encode())
    if flash:
        h.update(hashlib.sha256(flash).digest())
    d = os.path.join(ROOT, 'out', 'ec_cache')
    os.makedirs(d, exist_ok=True)
    fn = os.path.join(d, '%s-%s.json' % (sc.__name__, h.hexdigest()[:16]))
    if os.path.exists(fn):
        return [tuple(e) for e in json.load(open(fn))]
    v, _ = run(img, flash, sc)
    ev = board_events(v, 600)
    json.dump(ev, open(fn, 'w'))
    return ev


def check_update(src, new, flash, label):
    """UEFI's EC update: the SPI flash holds a new EC image, 3E19 makes the
    running firmware mirror it into the e-flash and reset; the new one must
    boot, bring the board to S0 and answer.  True if it does."""
    fl = bytearray(flash or bytes(0x800000))
    fl[0x10000:0x30000] = new + b'\xff' * (0x20000 - len(new))
    b = O6Board()
    ec = EC(src, b, flash=bytes(fl))
    h = Host(ec)
    ec.eflash.data[0x1F8FF] = 0
    b.press(2000, 300)
    h.at(6000, [(0x3E19, b'\x00', 0)])
    h.at(12500, [(0x3FFF, b'', 32)])
    ec.run(13000)
    import hashlib
    mirrored = any(k == 'reset' and hashlib.sha256(bytes(fl[0x10000:0x30000])).hexdigest()[:12] in d
                   for _, k, d in ec.events)
    s0 = any(k == 'out' and d.startswith('D0 EC_SLP_S3_L=1') and ms > 6100 for ms, k, d in ec.events)
    want = bytes(new[0:0]) or None
    ver = [r for r in h.res if r[1] == 0x3FFF]
    ok = mirrored and s0 and ver and ver[0][3] == 0
    print('%-16s %-4s mirrored %s, S0 after the reset %s, version %s' % (
        label, 'ok' if ok else 'FAIL', mirrored, s0,
        ver[0][4].rstrip(b'\0').decode('latin-1') if ver else None))
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--open', default=OPEN)
    ap.add_argument('--vendor', default=VENDOR)
    ap.add_argument('--flash', default=FLASH)
    ap.add_argument('-v', action='store_true')
    ap.add_argument('scenario', nargs='*')
    a = ap.parse_args()
    flash = None
    if os.path.exists(a.flash):
        flash = open(a.flash, 'rb').read()
        flash += b'\xff' * (0x800000 - len(flash))
    vimg = open(a.vendor, 'rb').read()
    oimg = open(a.open, 'rb').read()
    ok = True
    for sc in SCENARIOS:
        name = sc.__name__[3:]
        if a.scenario and name not in a.scenario:
            continue
        ve = cached(vimg, flash, sc)
        o, oh = run(oimg, flash, sc)
        # the first 600 ms: the vendor's start-up (its init writes every
        # pin; ours too) is compared from the first sequencer step on
        ok &= compare(name, ve, board_events(o, 600), tolerance, a.v)
    if not a.scenario or 'update' in a.scenario:
        ok &= check_update(vimg, oimg, flash, 'update v->open')
        ok &= check_update(oimg, vimg, flash, 'update open->v')
    print('all same' if ok else 'differences')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
