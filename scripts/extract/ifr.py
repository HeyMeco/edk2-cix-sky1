#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Dump question defaults from the HII forms embedded in a UEFI module.

CIX setup forms take most of their defaults from FixedAtBuild PCDs
(e.g. `default = FixedPcdGetBool (PcdPcieRootPort0Enable)`), so comparing
the defaults of two builds reveals board PCD differences without access to
the original DSC.
"""

import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from uefi import SEC_PE32, find_fd, parse_fv  # noqa: E402

OP_FORM_SET, OP_ONE_OF, OP_CHECKBOX, OP_NUMERIC = 0x0E, 0x05, 0x06, 0x07
OP_ONE_OF_OPTION, OP_DEFAULT, OP_END, OP_VARSTORE = 0x09, 0x5B, 0x29, 0x24
OP_VARSTORE_EFI = 0x26


def strings(pe):
    """Return {id: text} from the first English string package found."""
    out = {}
    pos = pe.find(b"en-US\x00")
    while pos >= 0:
        # EFI_HII_STRING_PACKAGE_HDR: Header(4) HdrSize(4) StringInfoOffset(4)
        # LanguageWindow(32) LanguageName(2) Language(ascii)
        hdr = pos - 46
        if hdr >= 0 and pe[hdr + 3] == 0x04:
            info = struct.unpack_from("<I", pe, hdr + 8)[0]
            p = hdr + info
            sid = 1
            while p < len(pe):
                bt = pe[p]
                if bt == 0x14:                       # SIBT_STRING_UCS2
                    e = p + 1
                    while pe[e:e + 2] != b"\x00\x00":
                        e += 2
                    out[sid] = pe[p + 1:e].decode("utf-16-le", "replace")
                    sid += 1
                    p = e + 2
                elif bt == 0x22:                     # SIBT_SKIP1
                    sid += pe[p + 1]
                    p += 2
                elif bt == 0x21:                     # SIBT_SKIP2
                    sid += struct.unpack_from("<H", pe, p + 1)[0]
                    p += 3
                else:
                    break
            return out
        pos = pe.find(b"en-US\x00", pos + 1)
    return out


def forms(pe):
    """Yield IFR byte strings for each form package in *pe*."""
    i = 0
    while True:
        i = pe.find(bytes([OP_FORM_SET]), i)
        if i < 4:
            if i < 0:
                return
            i += 1
            continue
        hdr = pe[i - 4: i]
        plen = hdr[0] | hdr[1] << 8 | hdr[2] << 16
        if hdr[3] == 0x02 and 0x20 < plen < len(pe):
            yield pe[i: i - 4 + plen]
            i += plen
        else:
            i += 1


def questions(ifr):
    """Yield (varstore, offset, prompt_id, kind, default) per question."""
    varstores = {}
    p = 0
    cur = None
    depth = 0
    while p + 2 <= len(ifr):
        op = ifr[p]
        ln = ifr[p + 1] & 0x7F
        scope = ifr[p + 1] & 0x80
        if ln < 2:
            break
        body = ifr[p + 2: p + ln]
        if op in (OP_VARSTORE, OP_VARSTORE_EFI):
            if op == OP_VARSTORE:
                vid = struct.unpack_from("<H", body, 16)[0]
                name = body[20:].split(b"\x00")[0].decode("ascii", "replace")
            else:
                vid = struct.unpack_from("<H", body, 0)[0]
                name = body[24:].split(b"\x00")[0].decode("ascii", "replace")
            varstores[vid] = name
        if op in (OP_ONE_OF, OP_CHECKBOX, OP_NUMERIC):
            prompt, _help, _qid, vid, voff = struct.unpack_from("<HHHHH", body, 0)
            default = None
            if op == OP_CHECKBOX:
                default = 1 if body[11] & 0x01 else 0
            cur = [varstores.get(vid, vid), voff, prompt, op, default, depth]
        elif op == OP_ONE_OF_OPTION and cur is not None:
            flags, typ = body[2], body[3]
            if flags & 0x10:   # EFI_IFR_OPTION_DEFAULT
                cur[4] = int.from_bytes(body[4:4 + (1 << min(typ, 3))], "little")
        elif op == OP_DEFAULT and cur is not None:
            typ = body[2]
            cur[4] = int.from_bytes(body[3:3 + (1 << min(typ, 3))], "little")
        if scope:
            depth += 1
        if op == OP_END:
            depth -= 1
            if cur is not None and depth == cur[5]:
                yield tuple(cur[:5])
                cur = None
        p += ln


def module_questions(image, start, module):
    img = Path(image).read_bytes()
    fd = find_fd(img, start)
    res = {}
    for f in parse_fv(img[fd:]):
        if f.ui_name != module:
            continue
        for s in f.walk_sections({SEC_PE32}):
            strs = strings(s.data)
            for ifr in forms(s.data):
                for vs, off, prompt, op, dflt in questions(ifr):
                    res[(vs, off)] = (strs.get(prompt, f"#{prompt}"), dflt)
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--start", default="0")
    ap.add_argument("--module", default="SetupManagerDxe")
    ap.add_argument("--against", help="second image; print only differing defaults")
    ap.add_argument("--against-start", default="0")
    a = ap.parse_args()
    q = module_questions(a.image, int(a.start, 0), a.module)
    if not a.against:
        for (vs, off), (name, d) in sorted(q.items(), key=lambda x: (str(x[0][0]), x[0][1])):
            print(f"{vs}+0x{off:03X} {d!s:>6}  {name}")
        return
    r = module_questions(a.against, int(a.against_start, 0), a.module)
    for k in sorted(set(q) | set(r), key=lambda x: (str(x[0]), x[1])):
        a1 = q.get(k, ("-", "absent"))
        b1 = r.get(k, ("-", "absent"))
        if a1[1] != b1[1]:
            print(f"{k[0]}+0x{k[1]:03X}  {a1[1]!s:>6} -> {b1[1]!s:<6}  {a1[0] if a1[0] != '-' else b1[0]}")


if __name__ == "__main__":
    main()
