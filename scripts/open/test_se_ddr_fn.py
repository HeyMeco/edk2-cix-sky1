#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Function-by-function test of the ported DDR driver (firmware/se/ddr)
against CIX's, in Unicorn.

  test_se_ddr_fn.py [FILE.c ...] [--func NAME]... [--random N] [--flash IMG]
                    [--cix ELF] [--out DIR] [--recapture] [-v]

1. Build: the C files (default: agent B's files in firmware/se/ddr that
   exist) are compiled with the SE firmware's flags (in the build
   container when arm-none-eabi-gcc is not on the host) and linked at
   0x30000000 into a test ELF in which every symbol they do not define is
   CIX's (the debug ELF's address), and every variable they define that
   CIX's ELF also has is moved onto CIX's (so both versions share the
   driver's state).  Their initialised data is checked against CIX's,
   pointers by the symbol they point to.
2. Capture: CIX's SE firmware runs in se_emu.py on a flash image (the
   DDR driver itself not stubbed) and the state at the entry of each of
   the driver's functions the C files define is saved: registers, SRAM
   and every page of registers touched so far (a few calls per function,
   distinct arguments first).  Cached in DIR.
3. Compare: from each saved state, CIX's function and the port run with
   the same registers and memory; their return values, the SRAM they
   leave (the stack below the caller excepted), the register writes
   (address, size, value, in order) and the delays and assertions they
   make must be the same.  --random N adds N runs per state with random
   and boundary argument values (by the parameters' DWARF types).

The driver's log (log_print, hal_printf_level) is a no-op in both runs:
the port leaves the debug output out.
"""

import argparse
import hashlib
import os
import pickle
import random
import shutil
import struct
import subprocess
import sys
import zlib
from pathlib import Path

from elftools.elf.elffile import ELFFile
from unicorn import (UC_ARCH_ARM, UC_HOOK_BLOCK, UC_HOOK_CODE, UC_HOOK_MEM_UNMAPPED, UC_HOOK_MEM_WRITE, UC_MODE_MCLASS,
                     UC_MODE_THUMB, Uc, UcError)
from unicorn.arm_const import (UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
                               UC_ARM_REG_R3, UC_ARM_REG_SP, UC_CPU_ARM_CORTEX_M33)

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE))
from se_emu import PAGE, SRAM, SRAM_SIZE, Elf, Se, SkySoc  # noqa: E402

IMAGE = "edk2-cix-build:0e7f129e1a59"
DDR = ROOT / "firmware/se/ddr"
# agent B's part of CIX's driver/ddr
B_FILES = ["MemTiming.c", "MemLpddr5Timing.c", "MemLpddr5ModeRegister.c", "MemModeRegisters.c",
           "MemLpddr5BusCfg.c", "MemAddressMapping.c", "MemFeatCfg.c", "MemFeatures.c", "MemLpddrSpd.c",
           "MemFuseLib.c", "MemRefreshMode.c", "MemRfmMode.c", "MemPortPriority.c", "MemStructLib.c",
           "MemBist.c", "MemDdrcBist.c", "MemDdrpiBist.c", "MemEyeScan.c"]
CFLAGS = ["-mcpu=cortex-m33", "-mthumb", "-mfloat-abi=soft", "-Os", "-g", "-std=gnu11", "-ffreestanding",
          "-fno-builtin", "-ffunction-sections", "-fdata-sections", "-fno-strict-aliasing", "-Wall", "-Wextra",
          "-Werror", "-Wno-unused-parameter", "-Wno-missing-field-initializers"]
TEST_BASE, TEST_SIZE = 0x30000000, 0x100000
RETURN = 0x3fff0000                     # where the called function returns to
SCRATCH = 0x3e000000                    # what random pointer arguments point to
STACK_LO = 0x2005b9f8                   # CIX's .stack section
NOOP = ("log_print", "hal_printf_level", "rlog_printf")
DELAY = ("udelay", "hal_udelay", "mdelay")
ASSERT = "drv_ddr_assert"
STUBS = {"drv_ddr_postci": 0, "drv_ddr_get_mem_size": 0x4000, "drv_ddr_get_ch_num": 4}


# build ------------------------------------------------------------------------
def run_cc(args):
    if shutil.which(args[0]):
        subprocess.run(args, check=True, cwd=ROOT)
        return
    subprocess.run(["docker", "run", "--rm", "-v", f"{ROOT}:/work", "-w", "/work", IMAGE] + args, check=True)


def rel(p):
    return str(Path(p).resolve().relative_to(ROOT))


def symbols(path):
    out = {}
    with open(path, "rb") as f:
        elf = ELFFile(f)
        for s in elf.get_section_by_name(".symtab").iter_symbols():
            t = s["st_info"]["type"]
            if t in ("STT_FUNC", "STT_OBJECT") and s.name and s["st_shndx"] != "SHN_UNDEF":
                out.setdefault(s.name, (s["st_value"], s["st_size"], t, s["st_info"]["bind"]))
    return out


def object_symbols(path):
    """[(name, section name, section type, bind, type)] of an object's data."""
    out = []
    with open(path, "rb") as f:
        elf = ELFFile(f)
        for s in elf.get_section_by_name(".symtab").iter_symbols():
            if s["st_info"]["type"] == "STT_OBJECT" and s.name and isinstance(s["st_shndx"], int):
                sec = elf.get_section(s["st_shndx"])
                out.append((s.name, sec.name, sec["sh_type"], s["st_info"]["bind"]))
    return out


def build(files, out, cix):
    """Test ELF of the C files.  Returns (path, {CIX name: name in the test
    ELF} of the initialised data to check against CIX's)."""
    out.mkdir(parents=True, exist_ok=True)
    inc = ["-I" + rel(ROOT / "firmware/se/include"), "-I" + rel(DDR)]
    tmp_types = out / "inc"
    if not (DDR / "mem_types.h").exists() and tmp_types.exists():
        inc.append("-I" + rel(tmp_types))
    objs = []
    for c in files:
        o = out / (Path(c).stem + ".o")
        run_cc(["arm-none-eabi-gcc"] + CFLAGS + inc + ["-c", "-o", rel(o), rel(c)])
        objs.append(o)
    cixsyms = symbols(cix)
    # function-static variables are NAME.N in both ELFs, N set by each compiler
    base = {}
    for n in cixsyms:
        base.setdefault(n.split(".")[0], []).append(n)
    defined = set()
    place = []             # (CIX address, object, section): the driver's state, on CIX's copy
    check = {}
    for o in objs:
        for n, v in symbols(o).items():
            defined.add(n)
        for n, sec, kind, bind in object_symbols(o):
            cands = [n] if n in cixsyms else base.get(n.split(".")[0], [])
            if len(cands) != 1 or cixsyms[cands[0]][2] != "STT_OBJECT":
                continue
            if kind == "SHT_NOBITS":
                place.append((cixsyms[cands[0]][0], o, sec))
            else:
                check[cands[0]] = n
    lines = ["SECTIONS {"]
    for addr, o, sec in sorted(place):
        name = ".cix" + sec.replace(".", "_")
        lines.append(f"  {name} {addr:#x} (NOLOAD) : {{ {rel(o)}({sec}) }}")
    lines += [f"  .text {TEST_BASE:#x} : {{ *(.text*) }}",
              "  .rodata : { *(.rodata*) }",
              "  .data : { *(.data*) }",
              "  .bss : { *(.bss*) *(COMMON) }",
              "  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.attributes) }",
              "}"]
    for n, (v, size, t, bind) in sorted(cixsyms.items()):
        if n not in defined and n.replace("_", "a").isalnum():
            lines.append(f"PROVIDE({n} = {v:#x});")
    (out / "test.ld").write_text("\n".join(lines) + "\n")
    elf = out / "test.elf"
    run_cc(["arm-none-eabi-gcc", "-nostdlib", "-nostartfiles", "-Wl,--no-warn-rwx-segments",
            "-T", rel(out / "test.ld"), "-o", rel(elf)] + [rel(o) for o in objs] + ["-lgcc"])
    return elf, check


def check_data(names, test_elf, cix):
    """Initialised data the C files define against CIX's: same size, same
    bytes, pointers to the same symbols."""
    a, b = Elf(cix), Elf(str(test_elf))
    ia, ib = Image(cix), Image(str(test_elf))
    bad = 0
    for n in sorted(names):
        sa, sb = a.syms[n], ib.local.get(names[n])
        if sb is None:
            continue
        va, vb = ia.read(sa[0], sa[1]), ib.read(sb[0], sb[1])
        if va is None and vb is None:
            continue
        if sa[1] != sb[1]:
            print(f"FAIL data {n}: size {sb[1]} (CIX {sa[1]})")
            bad += 1
            continue
        if va is None or vb is None:
            ok = (va or bytes(sa[1])) == (vb or bytes(sb[1]))
        else:
            ok = True
            for k in range(0, sa[1] - 3, 4) if sa[1] % 4 == 0 else []:
                wa, wb = struct.unpack_from("<I", va, k)[0], struct.unpack_from("<I", vb, k)[0]
                if wa != wb and ia.name_at(wa) != ib.name_at(wb):
                    ok = False
            if sa[1] % 4:
                ok = va == vb
        print(f"{'ok  ' if ok else 'FAIL'} data {n} ({sa[1]} bytes)")
        bad += not ok
    del a, b
    return bad


class Image:
    def __init__(self, path):
        with open(path, "rb") as f:
            elf = ELFFile(f)
            self.secs = [(s["sh_addr"], s["sh_size"], s["sh_type"] == "SHT_NOBITS" and None or s.data())
                         for s in elf.iter_sections() if s["sh_flags"] & 2 and s["sh_size"]]
            self.local = {}
            self.at = {}
            for s in elf.get_section_by_name(".symtab").iter_symbols():
                if s["st_info"]["type"] in ("STT_FUNC", "STT_OBJECT") and s.name and s["st_shndx"] != "SHN_UNDEF" \
                        and s["st_shndx"] != "SHN_ABS":
                    self.local.setdefault(s.name, (s["st_value"], s["st_size"]))
                    self.at.setdefault(s["st_value"], s.name)

    def read(self, addr, n):
        for a, size, data in self.secs:
            if a <= addr < a + size:
                return None if data is None else data[addr - a:addr - a + n]
        return None

    def name_at(self, v):
        return self.at.get(v, self.at.get(v & ~1, v))


# capture ----------------------------------------------------------------------
class Store:
    def __init__(self):
        self.blobs = {}

    def put(self, data):
        h = hashlib.sha1(data).digest()
        if h not in self.blobs:
            self.blobs[h] = zlib.compress(data, 1)
        return h

    def get(self, h):
        return zlib.decompress(self.blobs[h])


REGS = (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_SP, UC_ARM_REG_LR)


def capture(cix, flash, funcs, per_func=4, count=400_000_000):
    """States at the entry of funcs (name -> address) in a run of CIX's SE."""
    from test_se import CIX_STUBS
    stubs = dict(STUBS)
    stubs.update(CIX_STUBS)
    se = Se(cix, SkySoc(flash), stubs=stubs, stop=("service_debug_entry", "service_idle_entry"))
    se.poll_limit = 200_000
    store = Store()
    snaps = {n: [] for n in funcs}
    seen = {n: set() for n in funcs}
    calls = {n: 0 for n in funcs}
    seq = [0]
    by_addr = {a: n for n, a in funcs.items()}

    def hook(uc, addr, size, user):
        n = by_addr[addr]
        calls[n] += 1
        seq[0] += 1
        regs = tuple(uc.reg_read(r) for r in REGS)
        key = regs[:4]
        k = calls[n]
        if len(snaps[n]) >= per_func or (key in seen[n] and k not in (2, 50, 1000)):
            return
        seen[n].add(key)
        pages = {}
        for base in range(SRAM, SRAM + SRAM_SIZE, PAGE):
            pages[base] = store.put(bytes(uc.mem_read(base, PAGE)))
        for base in sorted(se.mapped):
            pages[base] = store.put(bytes(uc.mem_read(base, PAGE)))
        snaps[n].append({"regs": regs, "pages": pages, "call": k, "seq": seq[0]})

    for a in by_addr:
        se.uc.hook_add(UC_HOOK_CODE, hook, begin=a, end=a)
    stopped = se.run(count=count)
    where = stopped or (f"polling in {se.poll[0]} ({se.poll[2]:#x})" if se.poll and se.poll[2] else se.pc_func())
    return {"snaps": snaps, "calls": calls, "store": store.blobs, "stopped": where}


# replay -----------------------------------------------------------------------
DDRC, DDRC_STRIDE = 0x4c010000, 0x20000      # controller of channel n (SE view)


def ddrc_model(uc, addr, value):
    """What the functions poll on the DDR controller: a mode register write
    (0x334) or read (0x338) completes at once (interrupt status 0x59c bits
    19 and 18), status bits are cleared by writing them to 0x5c0."""
    if not DDRC <= addr < DDRC + 4 * DDRC_STRIDE:
        return
    base, off = addr - (addr - DDRC) % DDRC_STRIDE, (addr - DDRC) % DDRC_STRIDE
    status = struct.unpack("<I", uc.mem_read(base + 0x59c, 4))[0]
    def rmw(reg, set_bits=0, clear_bits=0):
        v = struct.unpack("<I", uc.mem_read(base + reg, 4))[0]
        uc.mem_write(base + reg, struct.pack("<I", (v | set_bits) & ~clear_bits & 0xffffffff))

    if off == 0x334:
        status |= 1 << 19
    elif off == 0x338:
        status |= 1 << 18
    elif off == 0x5c0:
        status &= ~value
    elif off == 0x3cc and value & 1:          # controller BIST go: done (358 bit 0), retention reached
        rmw(0x598, 1)
        rmw(0x40c, 1)
        return
    elif off == 0x5bc:                        # controller interrupt acknowledge
        rmw(0x598, 0, value)
        return
    elif off == 0x2510 and value & 1:         # PI BIST go: done (PI 296 bit 16)
        rmw(0x24a0, 0x10000)
        return
    elif off == 0x24a8:                       # PI interrupt acknowledge
        rmw(0x24a0, 0, value)
        return
    else:
        return
    uc.mem_write(base + 0x59c, struct.pack("<I", status & 0xffffffff))


class Run:
    """One call of a function from a saved state."""

    def __init__(self, cix_elf, test_elf, store):
        self.cix = cix_elf
        self.test = test_elf
        self.store = store

    def call(self, snap, entry, args=None, limit=2_000_000, reads=None):
        uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M33)
        mapped = set()
        for base, h in snap["pages"].items():
            uc.mem_map(base, PAGE)
            uc.mem_write(base, self.store.get(h))
            mapped.add(base)
        uc.mem_map(TEST_BASE, TEST_SIZE)
        for addr, data in self.test.segments:
            if TEST_BASE <= addr < TEST_BASE + TEST_SIZE:
                uc.mem_write(addr, data)
        uc.mem_map(RETURN, PAGE)
        events = []
        state = {"assert": None, "runaway": None}

        def unmapped(uc, access, addr, size, value, user):
            page = addr & ~(PAGE - 1)
            if page in mapped:
                return False
            uc.mem_map(page, PAGE)
            mapped.add(page)
            return True

        def write(uc, access, addr, size, value, user):
            if not (SRAM <= addr < SRAM + SRAM_SIZE or TEST_BASE <= addr < TEST_BASE + TEST_SIZE):
                value &= (1 << (8 * size)) - 1
                if not SCRATCH <= addr < SCRATCH + PAGE:     # memory: compared at the end
                    events.append(("w", addr, size, value))
                ddrc_model(uc, addr, value)

        def ret(uc, value):
            uc.reg_write(UC_ARM_REG_R0, value)
            uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

        def code(uc, addr, size, user):
            if addr == RETURN:
                uc.emu_stop()
                return
            kind = special.get(addr)
            if kind == "noop":
                ret(uc, 0)
            elif kind == "delay":
                events.append(("delay", uc.reg_read(UC_ARM_REG_R0)))
                ret(uc, 0)
            elif kind == "assert":
                state["assert"] = (uc.reg_read(UC_ARM_REG_R0), uc.reg_read(UC_ARM_REG_R1))
                events.append(("assert",) + state["assert"])
                uc.emu_stop()
            elif kind == "read":
                pass

        special = {}
        for n in NOOP:
            if n in self.cix.syms:
                special[self.cix.addr(n)] = "noop"
        for n in DELAY:
            if n in self.cix.syms:
                special[self.cix.addr(n)] = "delay"
        special[self.cix.addr(ASSERT)] = "assert"
        blocks = [0]

        def block(uc, addr, size, user):
            blocks[0] += 1
            if blocks[0] > limit:
                state["runaway"] = addr
                uc.emu_stop()

        uc.hook_add(UC_HOOK_BLOCK, block)
        uc.hook_add(UC_HOOK_MEM_UNMAPPED, unmapped)
        # write hooks only outside SRAM and the test image: with a write hook on
        # it, Unicorn 2.1 mishandles conditional stores in IT blocks (the IT
        # state outlives the block and the next instructions are skipped)
        for lo, hi in ((0, SRAM - 1), (SRAM + SRAM_SIZE, TEST_BASE - 1), (TEST_BASE + TEST_SIZE, 0xffffffff)):
            uc.hook_add(UC_HOOK_MEM_WRITE, write, begin=lo, end=hi)
        for a in list(special) + [RETURN]:
            uc.hook_add(UC_HOOK_CODE, code, begin=a, end=a)
        if reads:
            reads(uc)
        regs = list(snap["regs"])
        if args is not None:
            regs[:min(len(args), 4)] = args[:4]
            for i, v in enumerate(args[4:]):         # the rest on the stack
                uc.mem_write(regs[4] + 4 * i, struct.pack("<I", v & 0xffffffff))
        for r, v in zip(REGS, regs):
            uc.reg_write(r, v)
        uc.reg_write(UC_ARM_REG_LR, RETURN | 1)
        status = "returned"
        try:
            # no instruction count: Unicorn's count splits IT blocks and then
            # skips instructions after them; basic blocks are counted instead
            uc.emu_start(entry | 1, RETURN)
        except UcError as e:
            status = f"error {e} at {uc.reg_read(UC_ARM_REG_PC):#x}"
        pc = uc.reg_read(UC_ARM_REG_PC)
        if state["assert"]:
            status = "assert"
        elif state["runaway"] is not None:
            status = f"still running after {limit} blocks (at {state['runaway']:#x})"
        elif status == "returned" and pc != RETURN:
            status = f"did not return (pc {pc:#x})"
        r0, r1 = uc.reg_read(UC_ARM_REG_R0), uc.reg_read(UC_ARM_REG_R1)
        sp = regs[4]
        sram = {}
        for base in range(SRAM, SRAM + SRAM_SIZE, PAGE):
            before = self.store.get(snap["pages"][base])
            after = bytes(uc.mem_read(base, PAGE))
            if before != after:
                for i in range(PAGE):
                    if before[i] != after[i] and not (STACK_LO <= base + i < sp):
                        sram[base + i] = after[i]
        regs_after = {}
        for base in sorted(mapped):
            if base >= SRAM + SRAM_SIZE or base < SRAM:
                if TEST_BASE <= base < TEST_BASE + TEST_SIZE or base == RETURN:
                    continue
                data = bytes(uc.mem_read(base, PAGE))
                if base not in snap["pages"] or self.store.get(snap["pages"][base]) != data:
                    regs_after[base] = hashlib.sha1(data).hexdigest()
        return {"status": status, "r0": r0, "r1": r1, "sram": sram, "events": events, "pages": regs_after}


def patched(snap, patch, store, cix):
    """A saved state with bytes changed: patch maps "SYMBOL[+off]" (CIX's
    symbols) or "init+off" (the MEMORY_INIT_S gMemInitPtr points to) to
    bytes or an int (one byte)."""
    pages = dict(snap["pages"])
    cache = {}

    def page(base):
        if base not in cache:
            cache[base] = bytearray(store.get(pages[base]))
        return cache[base]

    def rd32(a):
        base = a & ~(PAGE - 1)
        return struct.unpack_from("<I", page(base), a - base)[0]

    def put(a, off, data):
        for i, b in enumerate(data):
            base = (a + off + i) & ~(PAGE - 1)
            if base not in pages:
                pages[base] = store.put(bytes(PAGE))
            page(base)[a + off + i - base] = b

    def rd(a, n):
        base = a & ~(PAGE - 1)
        return int.from_bytes(page(base)[a - base:a - base + n], "little")

    for key, data in patch.items():
        name, _, off = key.partition("+")
        off = int(off, 0) if off else 0
        if isinstance(data, int):
            data = bytes([data])
        if name.startswith("cdcb:"):
            # every block of the memory configuration with this ID (CIX's
            # Cdcb.c: entries {id, offset}, blocks at image + offset + 0x20)
            guid, img = int(name[5:], 0), rd32(cix.addr("buf.0"))
            entries, count = rd32(cix.addr("mCdcbBlockEntry")), rd(cix.addr("mCdcbBlockCount"), 1)
            addrs = [img + rd(entries + 4 * k + 2, 2) + 0x20 for k in range(count) if rd(entries + 4 * k, 2) == guid]
        elif name == "init":
            addrs = [rd32(cix.addr("gMemInitPtr"))]
        elif name.startswith("0x"):
            addrs = [int(name, 16)]
        else:
            addrs = [cix.addr(name)]
        for a in addrs:
            put(a, off, data)
            if name.startswith("cdcb:"):
                # keep the block's bytes summing to 0xff (BlockChecksum, +0xa)
                size = rd(a + 8, 2)
                total = sum(rd(a + i, 1) for i in range(size)) & 0xff
                put(a, 0xa, bytes([(rd(a + 0xa, 1) + 0xff - total) & 0xff]))
    for base, data in cache.items():
        pages[base] = store.put(bytes(data))
    return dict(snap, pages=pages)


# explicit calls: (state changes, arguments; past the fourth on the stack)
DDRC_PASS = {f"{DDRC + ch * DDRC_STRIDE + 0x3cc:#x}": bytes([0, 1, 0, 0]) for ch in range(4)}
PI_PASS = {f"{DDRC + ch * DDRC_STRIDE + 0x2510:#x}": bytes([0, 1, 0, 0]) for ch in range(4)}
PI_MIXED = {f"{DDRC + ch * DDRC_STRIDE + 0x2510:#x}": bytes([0, ch & 1, 0, 0]) for ch in range(4)}
# read and write DQ delays of every channel and slice (PHY 113-120 and 107-110), varied
DQ_DELAYS = {f"{DDRC + ch * DDRC_STRIDE + 0x4000 + s * 0x400 + (113 + d) * 4:#x}":
             struct.pack("<HH", 0x20 + 7 * d + 3 * s + ch, 0x2c + 5 * d + s) for ch in range(4) for s in range(4)
             for d in range(8)}
DQ_DELAYS.update({f"{DDRC + ch * DDRC_STRIDE + 0x4000 + s * 0x400 + (107 + d) * 4:#x}":
                  struct.pack("<HH", 0x100 + 9 * d + s, 0x0f0 + 3 * d + ch) for ch in range(4) for s in range(4)
                  for d in range(4)})
CALLS = {
    "MemDdrcBistTest": [({}, [0xf, 0, 0x20000000, 0, 30, 0]), (DDRC_PASS, [0xf, 0xffff0000, 0x40000000, 1, 30, 1]),
                        (DDRC_PASS, [0x5, 0x12345678, 0, 0, 20, 2]), ({}, [0xc, 0, 0x80000000, 0, 31, 3]),
                        (DDRC_PASS, [0xf, 0, 0, 0, 30, 4]), ({}, [0xf, 0, 0, 0, 30, 5])],
    "MemAdjustDqDelay": [(PI_PASS, [0xf]), ({}, [0x5]), ({"init+3": 2, **PI_PASS}, [0x3]),
                         ({**PI_MIXED, **DQ_DELAYS, "BootEnv.0": 1}, [0xf]), ({**PI_PASS, **DQ_DELAYS, "BootEnv.0": 1}, [0xf])],
    "MemDdrcSaveDqDelay": [({**DQ_DELAYS, "BootEnv.0": 1}, [1, SCRATCH, t]) for t in (0, 1)],
    "MemShiftRdDqDelay": [({**DQ_DELAYS, "BootEnv.0": 1}, [0xf, 3]), ({**DQ_DELAYS, "BootEnv.0": 1}, [0xf, 140]), ({}, [0xf, 3]), ({}, [0xf, 103]), ({"init+3": 2}, [0x6, 100]), ({}, [0x1, 250])],
    "MemDataEyeBistRun": [(PI_PASS, [0xf, 3, 5, 12]), ({"init+3": 2}, [0x3, 2, 0xff, 8])],
    "MemDdrpiBistDataCheck": [({}, [0xf, 0, 0, 0, 20, 0]), (PI_PASS, [0xf, 0xff00ff00, 1, 0x1000, 17, 2]),
                              (PI_PASS, [0x3, 0, 0, 0, 20, 4]), ({}, [0xf, 0, 0, 0, 20, 5])],
}

# extra states: the saved ones with these changes (per function; "*": all)
MRR_DATA = [f"{DDRC + ch * DDRC_STRIDE + 0x33c:#x}" for ch in range(4)]
VARIANTS = {
    "*": [{"BootEnv.0": 0}, {"BootEnv.0": 1}],
    "MemRfmModeCfg": [{"init+0x27": 1, **{r: bytes([v, 0, 0, 0]) for r in MRR_DATA}}
                      for v in (0x00, 0x01, 0x3f, 0xc1, 0xff, 0x5b)],
    "MemRefreshModeCfg": [{"init+0x26": 0}, {"init+0x26": 1}, {"init+0x26": 1, "init+0": 0}],
    "MemPortPriorityCfg": [{"init+0x29": v, "init+0x2a": 0x7f, "init+0x2b": 0x23, "init+0x33": 0xff,
                            "init+0x34": 5, "init+0x35": 1, "init+0x36": 3, "init+0x37": 6, "init+0x38": 1}
                           for v in (0, 1, 2)],
    "MemFeatConfig": [{"init+0x24": 0, "init+0x3a": 0}, {"init+0x24": 0, "init+0x3a": 1},
                      {"init+0x24": 1, "init+0x28": 1, "init+0x25": 1}],
    "MemAddressMapping": [{"init+0x2c": 0, "init+0x3a": 0}, {"init+0x2c": 1, "init+0x3a": 0},
                          {"init+0x3a": 1}, {"init+1": 2, "init+3": 1}, {"init+1": 1, "init+0x3a": 1},
                          {"init+0xa7": 0}, {"init+0xa7": 0, "init+3": 2, "init+9": 17}],
    "MemPlatformLookupBusConfigLp5": [
        {**{f"cdcb:0x1005+{0x10 + 16 * k + 0xc}": bytes([0x23, 1, 0x50, 1]) for k in range(4)}},
        {"init+3": 2, "init+0x22": 1}, {"init+3": 1, "init+4": 8},
        {**{f"cdcb:0x1005+{0x10 + 16 * k + 4}": bytes([0x34, 0, 0x12, 0x2f]) for k in range(4)}, "init+3": 2},
        {"init+3": 4}, {"init+4": 0x40}, {"cdcb:0x1005+0x10": bytes(2)}],
    "MemDramFunctionalInit": [{"init+3": 1}, {"init+3": 2, "init+4": 8}, {"init+0x1059": 0},
                              {"init+0x1059": 0, "init+0": 0}, {"init+0x3a": 1},
                              {"cdcb:0x1006+0x10": bytes(2)}, {"cdcb:0x1001+0x12": 0x3}],
    "MemLpddr5TimingInit": [{"init+0xa7": 2, "init+0xcd": 2, "init+0xf3": 2}, {"init+0xa7": 0, "init+0xcd": 0},
                            {"init+0x22": 1}, {"init+0xb5": 1, "init+0xdb": 2, "init+0x101": 1, "init+0xda": 1},
                            {"init+0xb0": bytes(4), "init+0xd6": bytes([0x30, 1, 0x60, 0])},
                            {"init+0x31": 1, "init+0x32": 1, "BootEnv.0": 1}, {"init+0xf0": bytes([0x40, 6])},
                            {"init+0xca": bytes([0x10, 3]), "init+0xcc": 2}, {"init+3": 2, "init+0x2a": 1}],
    "MemDdrcTimingCfg": [{"init+0xa7": 2, "init+0xcd": 2, "init+0xf3": 2}, {"init+0x22": 1},
                         {"init+0x31": 1, "init+0x32": 1, "BootEnv.0": 1}],
    "MemPiTimingCfg": [{"init+0xcc": 2, "init+0xf2": 2}, {"init+0x22": 1}],
    "MemBistDataEye": [{**PI_PASS}, {"init+3": 2, **PI_PASS}, {}, {**PI_MIXED, **DQ_DELAYS, "BootEnv.0": 1}],
    "MemAdjustDqDelay": [],
    "RunMemBistTest": [{"init+0x2d": 1, "init+0x2e": m, **DDRC_PASS} for m in (0, 1, 2, 4)] +
                      [{"init+0x2d": 2, "init+0x2e": m, **PI_PASS} for m in (0, 2, 3)] + [{"init+0x2d": 1, "init+1": 2}],
    "MemInlineEccCfg": [{"init+0x3a": 1}, {"init+0x3a": 0}],
    "MemSpdAnalyze": [{"init+0x1058": 0x10, "init+4": 16, "init+3": 1}, {"init+0x1058": 0x40},
                      {"init+0x1058": 4, "init+4": 8, "init+3": 2}, {"init+4": 4}],
}


def compare(a, b, ret_bytes):
    out = []
    if a["status"].startswith("still running") and b["status"].startswith("still running"):
        return out                  # both loop for ever (polling a register no model sets)
    if a["status"] != b["status"]:
        out.append(f"status CIX {a['status']}, port {b['status']}")
    mask = (1 << (8 * ret_bytes)) - 1 if ret_bytes else 0
    if ret_bytes and (a["r0"] & mask) != (b["r0"] & mask):
        out.append(f"returns CIX {a['r0'] & mask:#x}, port {b['r0'] & mask:#x}")
    if ret_bytes == 8 and a["r1"] != b["r1"]:
        out.append(f"returns (high) CIX {a['r1']:#x}, port {b['r1']:#x}")
    if a["sram"] != b["sram"]:
        diff = sorted(k for k in set(a["sram"]) | set(b["sram"]) if a["sram"].get(k) != b["sram"].get(k))
        out.append(f"SRAM: {len(diff)} bytes differ, first at {diff[0]:#x} "
                   f"(CIX {a['sram'].get(diff[0])}, port {b['sram'].get(diff[0])})")
    if a["pages"] != b["pages"]:
        out.append("register pages differ at the end: " +
                   ", ".join(f"{p:#x}" for p in sorted(set(a["pages"]) | set(b["pages"]))
                             if a["pages"].get(p) != b["pages"].get(p)))
    if a["events"] != b["events"]:
        ea, eb = a["events"], b["events"]
        i = next((i for i in range(min(len(ea), len(eb))) if ea[i] != eb[i]), min(len(ea), len(eb)))
        fmt = lambda e: "end" if e is None else " ".join(f"{x:#x}" if isinstance(x, int) else x for x in e)
        out.append(f"events: {len(ea)} vs {len(eb)}, first difference #{i}: CIX {fmt(ea[i] if i < len(ea) else None)}"
                   f", port {fmt(eb[i] if i < len(eb) else None)}")
    return out


# DWARF: parameter and return types --------------------------------------------
def signatures(cix):
    """name -> ([parameter byte sizes], return byte size) for functions."""
    out = {}
    with open(cix, "rb") as f:
        dw = ELFFile(f).get_dwarf_info()
        for cu in dw.iter_CUs():
            for d in cu.get_top_DIE().iter_children():
                if d.tag != "DW_TAG_subprogram" or "DW_AT_name" not in d.attributes \
                        or "DW_AT_low_pc" not in d.attributes:
                    continue
                params = []
                for p in d.iter_children():
                    if p.tag == "DW_TAG_formal_parameter":
                        params.append(type_info(p.get_DIE_from_attribute("DW_AT_type")) if "DW_AT_type" in p.attributes
                                      else (4, False))
                rt = type_info(d.get_DIE_from_attribute("DW_AT_type")) if "DW_AT_type" in d.attributes else (0, False)
                out[d.attributes["DW_AT_name"].value.decode()] = (params, rt[0])
    return out


def type_info(t):
    """(byte size, is pointer)"""
    while t is not None and t.tag in ("DW_TAG_typedef", "DW_TAG_const_type", "DW_TAG_volatile_type"):
        t = t.get_DIE_from_attribute("DW_AT_type") if "DW_AT_type" in t.attributes else None
    if t is None:
        return (4, False)
    if t.tag == "DW_TAG_pointer_type":
        return (4, True)
    return (t.attributes["DW_AT_byte_size"].value if "DW_AT_byte_size" in t.attributes else 4, False)


def random_args(params, rng, k):
    vals = []
    for size, ptr in params[:4]:
        if ptr:
            vals.append(None)
            continue
        top = (1 << (8 * min(size, 4))) - 1
        choices = [0, 1, 2, 3, top, top >> 1]
        vals.append(choices[k % len(choices)] if k < 6 else
                    (rng.randrange(0, 8) if rng.random() < 0.5 else rng.randrange(0, top + 1)))
    return vals


# main -------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="*")
    ap.add_argument("--func", action="append", default=[])
    ap.add_argument("--random", type=int, default=0)
    ap.add_argument("--flash", default=str(ROOT / "dist/orion-o6_2026Q3_open.bin"))
    ap.add_argument("--cix")
    ap.add_argument("--out", default=str(ROOT / "out/se-ddr-fn"))
    ap.add_argument("--per-func", type=int, default=4)
    ap.add_argument("--recapture", action="store_true")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("-v", action="store_true")
    a = ap.parse_args()
    cix = a.cix
    if cix is None:
        from cix_debug import fetch
        cix = str(fetch())
    files = a.files or [str(DDR / f) for f in B_FILES if (DDR / f).exists()]
    if not files:
        sys.exit("no C files to test")
    out = Path(a.out)
    test_elf, data_names = build(files, out, cix)
    bad = check_data(data_names, test_elf, cix)

    cix_elf, t_elf = Elf(cix), Elf(str(test_elf))
    tsyms = Image(str(test_elf)).local
    sigs = signatures(cix)
    funcs = {}
    for n, (v, size, t) in t_elf.syms.items():
        if t == "STT_FUNC" and n in cix_elf.syms and TEST_BASE <= v < TEST_BASE + TEST_SIZE:
            funcs[n] = cix_elf.addr(n)
    for n, (v, size) in tsyms.items():           # static functions too
        if n not in funcs and n in cix_elf.syms and cix_elf.syms[n][2] == "STT_FUNC" \
                and TEST_BASE <= v < TEST_BASE + TEST_SIZE:
            funcs[n] = cix_elf.addr(n)
    if a.func:
        funcs = {n: v for n, v in funcs.items() if n in a.func}

    flash = open(a.flash, "rb").read()
    key = hashlib.sha1(open(cix, "rb").read() + flash + str(a.per_func).encode()).hexdigest()[:12]
    cache = out / f"capture-{key}.pkl"
    cap = None
    if cache.exists() and not a.recapture:
        cap = pickle.load(open(cache, "rb"))
        if not set(funcs) <= set(cap["snaps"]):
            cap = None
    if cap is None:
        allf = {n: cix_elf.addr(n) for n in funcs}
        if cache.exists() and not a.recapture:
            allf.update({n: cix_elf.addr(n) for n in pickle.load(open(cache, "rb"))["snaps"]})
        print(f"capturing {len(allf)} functions in a run of CIX's SE ...", flush=True)
        cap = capture(cix, flash, allf, a.per_func)
        pickle.dump(cap, open(cache, "wb"))
    print(f"capture: CIX's SE stopped at {cap['stopped']}")

    store = Store()
    store.blobs = cap["store"]
    runner = Run(cix_elf, t_elf, store)
    rng = random.Random(a.seed)
    last = max((s for v in cap["snaps"].values() for s in v), key=lambda s: s.get("seq", 0))
    total = fails = untested = 0
    for n in sorted(funcs):
        snaps = cap["snaps"].get(n, [])
        params, rbytes = sigs.get(n, ([], 4))
        borrowed = False
        if not snaps:
            # not called before the capture stopped: from the last state saved, with
            # boundary and random arguments
            untested += 1
            snaps = [last]
            borrowed = True
        mine = t_elf.syms[n][0] if n in t_elf.syms else tsyms[n][0]
        cases = [(s, None) for s in snaps]
        if borrowed:
            cases = [(last, None)] if not params else []
        nstates = len(cases)
        for v in VARIANTS["*"] + VARIANTS.get(n, []):
            for s in snaps[:2]:
                cases.append((patched(s, v, store, cix_elf), None))
        for s in snaps[:1]:
            for k in range(max(a.random, 8 if borrowed and params else 0)):
                args = random_args(params, rng, k)
                if not params:
                    continue
                st = s
                if any(v is None for v in args):
                    # pointers: to random bytes outside SRAM
                    st = patched(s, {f"{SCRATCH:#x}": bytes(rng.randrange(256) for _ in range(0x800))},
                                 store, cix_elf)
                    args = [SCRATCH + 0x200 * i if v is None else v for i, v in enumerate(args)]
                cases.append((st, args))
        for v, args in CALLS.get(n, []):
            for s in snaps[:1]:
                cases.append((patched(s, v, store, cix_elf), args))
        nbad = 0
        most = (0, 0)
        for s, args in cases:
            ra = runner.call(s, funcs[n], args)
            most = (max(most[0], len(ra["events"])), max(most[1], len(ra["sram"])))
            rb = runner.call(s, mine & ~1, args)
            diff = compare(ra, rb, rbytes)
            total += 1
            if diff:
                nbad += 1
                if nbad <= 3 or a.v:
                    what = f"args {[hex(x) for x in args]}" if args else f"call {s['call']}"
                    print(f"FAIL {n} ({what}): " + "; ".join(diff))
        fails += nbad
        print(f"{'ok  ' if not nbad else 'FAIL'} {n}: {len(cases) - nbad}/{len(cases)} "
              f"({'not reached, last state' if borrowed else f'{len(snaps)} states'}, "
              f"{len(cases) - nstates} variants and random; up to {most[0]} events, {most[1]} SRAM bytes)")
    print(f"{len(funcs)} functions, {total} runs, {fails} failing, {untested} not reached in the capture (run from its last state), "
          f"{bad} data differences")
    sys.exit(1 if fails or bad else 0)


if __name__ == "__main__":
    main()
