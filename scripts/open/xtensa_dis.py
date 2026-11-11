#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Disassemble a pm_core_v1 ELF (CIX's CSU_PM image) with its FLIX bundles.

  xtensa_dis.py ELF > ELF.dis

objdump does not know the core's 8-byte FLIX bundles and loses the
instruction stream at each.  This runs objdump on a copy of the ELF in
which every bundle (found by walking the .xt.prop instruction ranges) is
replaced by "nop; nop; nop.n" of the same size, so the rest stays in
step (and so is each 24-bit instruction objdump does not know: the
double-precision FPU, GETEX, CLREX, named by fp_name), then on copies with one slot's operation (xtensa_flix.py) at the
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


# pm_core_v1's instructions GNU's description of the core has not got: the
# double-precision FPU (and the single-precision one, for a build without
# it), GETEX and CLREX.  Their names by the encoding's fields.
FP_BY_OP = {
    (8, 0): "lsx", (8, 1): "lsx", (8, 2): "lsx", (8, 3): "lsx", (8, 4): "lsx", (8, 5): "lsx", (8, 6): "lsx",
    (8, 7): "lsx",
    (10, 0): "add.s", (10, 1): "sub.s", (10, 2): "mul.s", (10, 4): "madd.s", (10, 5): "msub.s",
    (10, 6): "maddn.s", (10, 7): "divn.s", (10, 8): "cvt.s", (10, 9): "cvt.s", (10, 10): "cvt.s",
    (10, 11): "cvt.s", (10, 12): "float.s", (10, 13): "ufloat.s", (10, 14): "cvt.s",
    (11, 1): "fcmp", (11, 2): "fcmp", (11, 3): "fcmp", (11, 4): "fcmp", (11, 5): "fcmp", (11, 6): "fcmp",
    (11, 7): "fcmp", (11, 8): "movxz.s", (11, 9): "movxz.s", (11, 10): "movxz.s", (11, 11): "movxz.s",
    (11, 12): "movb.s", (11, 13): "movb.s",
    (14, 1): "dcmp", (14, 2): "dcmp", (14, 3): "dcmp", (14, 4): "dcmp", (14, 5): "dcmp", (14, 6): "dcmp",
    (14, 7): "dcmp",
    (15, 0): "add.d", (15, 1): "sub.d", (15, 2): "mul.d", (15, 4): "madd.d", (15, 5): "msub.d",
    (15, 6): "maddn.d", (15, 7): "divn.d", (15, 8): "cvt.d", (15, 9): "cvt.d", (15, 10): "cvt.d",
    (15, 11): "cvt.d", (15, 12): "float.d", (15, 13): "ufloat.d", (15, 14): "cvt.d",
}
# op2 15 of the two groups: one operand, the operation in t
FP_1OP = {
    10: {0: "mov.s", 1: "abs.s", 2: "cvtd.s", 3: "const.s", 4: "rfr", 5: "wfr", 6: "neg.s", 7: "div0.s",
         11: "nexp01.s", 12: "fpadj", 13: "fpadj", 14: "fpadj", 15: "fpadj"},
    15: {0: "mov.d", 1: "abs.d", 2: "cvts.d", 3: "const.d", 4: "rfrd", 6: "neg.d", 7: "div0.d",
         11: "nexp01.d", 12: "dfpadj", 13: "dfpadj", 14: "dfpadj", 15: "dfpadj"},
}
# op0 3: loads and stores of the FP registers, by r
FP_LSCI = {0: "lsi", 1: "ldi", 4: "ssi", 5: "sdi", 8: "lsip", 9: "ldip", 12: "ssip", 13: "sdip"}


def fp_name(word):
    op0, t, s, r = word & 15, (word >> 4) & 15, (word >> 8) & 15, (word >> 12) & 15
    op1, op2 = (word >> 16) & 15, (word >> 20) & 15
    if op0 == 3:
        return FP_LSCI.get(r, "?")
    if op0 != 0:
        return "?"
    if (op1, op2, r, s, t) == (0, 0, 3, 1, 2):
        return "clrex"
    if (op1, op2, r) == (0, 4, 10):
        return f"getex a{t}"
    if (op1, op2) == (14, 8):
        return f"wfrd f{r},a{s},a{t}"
    if op2 == 15 and op1 in FP_1OP:
        return FP_1OP[op1].get(t, "?")
    return FP_BY_OP.get((op1, op2), "?")


def emu_text(word):
    """The name of a 24-bit instruction objdump does not know (pm_core_v1's
    double-precision FPU, exclusive access)."""
    r, s, t = (word >> 12) & 15, (word >> 8) & 15, (word >> 4) & 15
    return f"{fp_name(word)}\t; r {r} s {s} t {t} (not in GNU's description)"


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
