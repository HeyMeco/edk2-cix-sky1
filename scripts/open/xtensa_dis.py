#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Disassemble a pm_core_v1 ELF (CIX's CSU_PM image) with its FLIX bundles.

  xtensa_dis.py ELF > ELF.dis

objdump does not know the core's 8-byte FLIX bundles and loses the
instruction stream at each.  This runs objdump on a copy of the ELF in
which every bundle (found by walking the .xt.prop instruction ranges) is
replaced by "nop; nop; nop.n" of the same size, so the rest stays in
step (and so is each 24-bit instruction objdump does not know: the
double-precision FPU, GETEX, CLREX, named by xt_emu.py), then on copies with one slot's operation (xtensa_flix.py) at the
bundle's address, so PC-relative operands come out right (a slot's
branch, jump, L32R and LOOP use the bundle's address as the core
encodings do), and prints the listing with each bundle as
"{ op ; op ; op }".  Needs XTENSA_GNU_CONFIG (xtensa_core.py) and
OBJDUMP (default xtensa-esp-elf-objdump), as in the build container.
"""

import os
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from xtensa_flix import decode_bundle  # noqa: E402

NOPS = bytes.fromhex("f02000") + bytes.fromhex("f02000") + bytes.fromhex("3df0")
LENGTHS = [3] * 8 + [2] * 6 + [8, 8]


def walk(elf_bytes):
    """[(section, address, file offset, bytes)] of every instruction in the
    .xt.prop instruction ranges (overlay sections share addresses)."""
    from io import BytesIO
    from elftools.elf.elffile import ELFFile
    elf = ELFFile(BytesIO(elf_bytes))
    secs = [s for s in elf.iter_sections() if s["sh_flags"] & 4 and s["sh_type"] == "SHT_PROGBITS"]
    out = []
    for prop in elf.iter_sections():
        if not prop.name.startswith(".xt.prop"):
            continue
        # .xt.prop.X describes section X (overlays share addresses)
        target = prop.name[len(".xt.prop"):] or None
        data = prop.data()
        if target is None:
            cands = [s for s in secs if not s.name.startswith(".overlay")]
        else:
            cands = [s for s in secs if s.name == target]
        for i in range(0, len(data), 12):
            addr, size, flags = struct.unpack_from("<III", data, i)
            if not flags & 2 or not size:
                continue
            for s in cands:
                if s["sh_addr"] <= addr < s["sh_addr"] + s["sh_size"]:
                    break
            else:
                continue
            p = addr - s["sh_addr"]
            code = s.data()
            while p < addr - s["sh_addr"] + size:
                n = LENGTHS[code[p] & 15]
                out.append((s.name, s["sh_addr"] + p, s["sh_offset"] + p, code[p:p + n]))
                p += n
    return out


def objdump(path):
    exe = os.environ.get("OBJDUMP", "xtensa-esp-elf-objdump")
    return subprocess.run([exe, "-d", path], check=True, capture_output=True, text=True).stdout


LINE = re.compile(r"^\s*([0-9a-f]+):\t([0-9a-f ]+?)\s*\t(.*)$")
SECTION = re.compile(r"^Disassembly of section (\S+):$")


def by_address(listing):
    """(section, address) -> instruction text."""
    out, sec = {}, None
    for line in listing.splitlines():
        m = SECTION.match(line)
        if m:
            sec = m.group(1)
            continue
        m = LINE.match(line)
        if m:
            out.setdefault((sec, int(m.group(1), 16)), m.group(3).strip())
    return out


def fmt_branch(op, pc):
    _, mn, s, operand, off = op
    tgt = (pc + 4 + off) & 0xffffffff
    if operand is None:
        return f"{mn} a{s}, {tgt:x}"
    if mn in ("bbci", "bbsi") or mn.endswith("i"):
        return f"{mn} a{s}, {operand}, {tgt:x}"
    return f"{mn} a{s}, a{operand}, {tgt:x}"


def emu_text(word):
    """The interpreter's name for a 24-bit instruction objdump does not know
    (pm_core_v1's double-precision FPU, exclusive access)."""
    from xt_emu import decode24
    r, s, t = (word >> 12) & 15, (word >> 8) & 15, (word >> 4) & 15
    try:
        name = decode24(word).text
    except Exception:                                   # noqa: BLE001
        name = "?"
    return f"{name}\t; r {r} s {s} t {t} (not in GNU's description)"


NOP3, NOP2 = bytes.fromhex("f02000"), bytes.fromhex("3df0")


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    elf = Path(sys.argv[1]).read_bytes()
    insns = walk(elf)
    found = [i for i in insns if len(i[3]) == 8]
    ops = {}
    for sec, addr, off, data in found:
        try:
            ops[(sec, addr, off)] = decode_bundle(bytes(data), inferred=True)
        except ValueError as e:
            ops[(sec, addr, off)] = e
    unknown = {}                                        # (sec, addr) -> text
    with tempfile.TemporaryDirectory() as td:
        base = bytearray(elf)
        for sec, addr, off, data in found:
            base[off:off + 8] = NOPS
        # objdump loses the stream at instructions it does not know: replace
        # them by NOPs of their length until every boundary shows up
        for _ in range(8):
            p = Path(td) / "base.elf"
            p.write_bytes(base)
            listing = objdump(str(p))
            seen = by_address(listing)
            new = 0
            for sec, addr, off, data in insns:
                if len(data) == 8 or (sec, addr) in unknown:
                    continue
                t = seen.get((sec, addr))
                if t is not None and t.startswith(".byte"):    # missing ones follow from these
                    word = int.from_bytes(data, "little")
                    unknown[(sec, addr)] = (emu_text(word) if len(data) == 3 else "?", bytes(data).hex())
                    base[off:off + len(data)] = NOP3 if len(data) == 3 else NOP2
                    new += 1
            if not new:
                break
        slots = {}
        for k in range(3):
            img = bytearray(base)
            any_slot = False
            for (sec, addr, off), o in ops.items():
                if isinstance(o, list) and k < len(o) and not isinstance(o[k], tuple):
                    img[off:off + 3] = o[k].to_bytes(3, "little")
                    any_slot = True
            if not any_slot:
                continue
            q = Path(td) / f"slot{k}.elf"
            q.write_bytes(img)
            slots[k] = by_address(objdump(str(q)))
    text = {}
    for (sec, addr, off), o in ops.items():
        if isinstance(o, ValueError):
            text[(sec, addr)] = f"{{ ?? {o} }}"
            continue
        parts = []
        for k, op in enumerate(o):
            if isinstance(op, tuple):
                parts.append(fmt_branch(op, addr))
            else:
                t = slots.get(k, {}).get((sec, addr), "?")
                parts.append(emu_text(op).split("\t")[0] if t.startswith((".byte", "?")) else t)
        text[(sec, addr)] = "{ " + " ; ".join(parts or ["nop"]) + " }"
    raw = {(sec, addr): bytes(data).hex() for sec, addr, _, data in found}
    skip, sec = 0, None
    for line in listing.splitlines():
        m = SECTION.match(line)
        if m:
            sec = m.group(1)
        m = LINE.match(line)
        if m and skip:
            skip -= 1
            continue
        key = (sec, int(m.group(1), 16)) if m else None
        if key in text and m.group(2).replace(" ", "") == "0020f0":
            print(f"{key[1]:8x}:\t{raw[key]}\t{text[key]}")
            skip = 2                                    # the second nop and the nop.n
            continue
        if key in unknown:
            print(f"{key[1]:8x}:\t{unknown[key][1]}\t{unknown[key][0]}")
            continue
        print(line)


if __name__ == "__main__":
    main()
