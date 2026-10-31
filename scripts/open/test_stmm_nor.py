#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Host test of the StandaloneMM NOR flash driver
(edk2-project/Platform/CIX/Sky1/StandaloneMm/Test/NorFlashTest.c): compiles
SpiNor.c and NorFlashFvb.c with the host C compiler against the EDK2
headers and runs them on a simulated SPI NOR.  Needs the edk2 and
edk2-platforms submodules (scripts/prepare.py)."""

import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
MM = ROOT / "edk2-project/Platform/CIX/Sky1/StandaloneMm"
DRV = MM / "Drivers/NorFlashStandaloneMm"


def main():
    arch = "AArch64" if os.uname().machine in ("arm64", "aarch64") else "X64"
    includes = [DRV, ROOT / "edk2/MdePkg/Include", ROOT / f"edk2/MdePkg/Include/{arch}",
                ROOT / "edk2/MdeModulePkg/Include", ROOT / "edk2/StandaloneMmPkg/Include",
                ROOT / "edk2-platforms/Silicon/CIX/Sky1/Include",
                ROOT / "edk2-platforms/Silicon/CIX/Sky1/Include/Library", MM / "Include"]
    with tempfile.TemporaryDirectory() as td:
        exe = Path(td) / "nortest"
        cc = os.environ.get("CC", "cc")
        cmd = [cc, "-O1", "-g", "-fshort-wchar", "-fno-strict-aliasing", "-w", "-DMDEPKG_NDEBUG",
               *[f"-I{p}" for p in includes], "-o", str(exe),
               str(MM / "Test/NorFlashTest.c"), str(DRV / "SpiNor.c"), str(DRV / "NorFlashFvb.c")]
        subprocess.run(cmd, check=True)
        sys.exit(subprocess.run([str(exe)]).returncode)


if __name__ == "__main__":
    main()
