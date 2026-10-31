#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""GNU toolchain configuration for CSU_PM's Xtensa core (pm_core_v1).

  xtensa_core.py OUT.so [--config firmware/csupm/core/pm_core_v1.json]
                 [--overlays /opt/xtensa/overlays] [--dynconfig /opt/xtensa/dynconfig]

Espressif's GCC and binutils load the description of the target core at run
time from the shared object named by XTENSA_GNU_CONFIG (xtensa-dynconfig).
This builds that object for CSU_PM's core: the instruction tables
(xtensa-modules.c) of the overlay named in the JSON, which are the same for
every core option this compiler can emit, and its xtensa-config.h with the
JSON's overrides applied.  The generated header is kept next to OUT.so.
"""

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def header(text, overrides):
    seen = set()

    def sub(m):
        name = m.group(1)
        if name not in overrides:
            return m.group(0)
        seen.add(name)
        return f"#define {name}{m.group(2)}{overrides[name]['value']}"

    text = re.sub(r"^#define (XCHAL_\w+)(\s+)\S+$", sub, text, flags=re.M)
    missing = set(overrides) - seen
    if missing:
        sys.exit(f"xtensa_core: not in the overlay's xtensa-config.h: {', '.join(sorted(missing))}")
    return text


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--config", default=str(ROOT / "firmware/csupm/core/pm_core_v1.json"))
    ap.add_argument("--overlays", default="/opt/xtensa/overlays")
    ap.add_argument("--dynconfig", default="/opt/xtensa/dynconfig")
    ap.add_argument("--cc", default="cc")
    a = ap.parse_args()
    cfg = json.loads(Path(a.config).read_text())
    ov = Path(a.overlays) / cfg["base_overlay"]
    out = Path(a.out).resolve()
    inc = out.parent / (out.stem + "-include")
    inc.mkdir(parents=True, exist_ok=True)
    src = (ov / "binutils/include/xtensa-config.h").read_text()
    (inc / "xtensa-config.h").write_text(header(src, cfg["overrides"]))
    dyn = Path(a.dynconfig)
    cmd = [a.cc, "-shared", "-fPIC", "-O2", f"-I{inc}", f"-I{dyn}/include", f"-I{ov}/binutils/include",
           str(dyn / "xtensa-config.c"), str(ov / "binutils/bfd/xtensa-modules.c"),
           str(ov / "gdb/gdb/xtensa-config.c"), str(ov / "gdb/gdb/xtensa-xtregs.c"), "-o", str(out)]
    subprocess.run(cmd, check=True)


if __name__ == "__main__":
    main()
