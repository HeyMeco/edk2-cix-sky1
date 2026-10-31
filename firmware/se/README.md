# Open SE firmware (work in progress)

The SE is the Cortex-M33 security core that the boot ROM starts first. Its
firmware (bootloader1 image `se`, 0x20000000) trains DRAM, distributes eFuse
data, sets up clocks, power, the interconnect and the TZC-400s, verifies and
starts CSU_PM and BL2, and then serves the AP and the PM over the mailbox
(crypto, eFuse, keys, suspend and resume) for as long as the system runs.

This directory rebuilds it from source, except the DDR driver, which is
CIX's code linked in unchanged.

## Reference material

CIX publishes debug builds with full DWARF debug information in
[cixtech/cix_proprietary__cix_firmware](https://github.com/cixtech/cix_proprietary__cix_firmware)
(`debug/se_fw/se_fw.elf`, SE firmware 2.0.2026W01.01, built from
`security/firmware/` in the CIX tree). It names all 1238 functions, their
source files, types and globals; Ghidra decompiles it with those names
(`scripts/open/re/decompile.sh`, `scripts/open/re/dwarfmap.py`). The files
are not redistributed here: `scripts/open/cix_debug.py` fetches them at
the commit pinned in `manifests/cix-debug-firmware.json` and checks their
hashes.

The shipping firmware is 2.0.2026W31.01 (CIX 2026Q3 bootloader1).

## Layout (mirrors CIX's tree)

| CIX directory | Size | What | Here |
|---|---|---|---|
| `driver/ddr` | 44 KB, 192 functions | LPDDR5 controller/PHY init and training | **CIX binary**, relinked from the debug ELF (`ddr/`) |
| `library/ce_lite` | 61 KB | crypto engine (hash, RSA, SM2, ECC, AES, SM4, TRNG, OTP) | to do: software crypto where the result is the same, CE only for TRNG/OTP |
| `service/debug/monitor` | 26 KB | UART debug monitor | not planned (debug only) |
| `hal/common` | 15 KB | log, storage (flash images), crash, crypto glue | to do |
| `framework` | 18 KB | eFuse, key management, image verification, xloader, jumper | to do |
| `driver/*` (other) | 30 KB | CRU, PLL, QSPI/xSPI, CI-700, TZC-400, mailbox, I2C/PMIC, RCSU, … | to do |
| `service` | 9 KB | boot, cold/warm boot, runtime services, STR | to do |
| `platform/sky1` | 4 KB | SoC description, wake-up sources | to do |

## DDR island

`scripts/open/se_ddr_island.py` turns the DDR driver of the debug ELF into
an assembly file: the code bytes as they are, with each call to a function
outside the driver re-emitted as `bl <name>` and each literal-pool word
that points at data or code re-emitted as a `.word <symbol>+<offset>`, plus
the driver's strings, tables and variables. It links into this firmware
like any object. The driver calls 16 functions outside itself (C library,
log, interrupts, flash, eFuse, GPIO, memory-config write) and is entered
through 7 (`drv_ddr_init`, `drv_ddr_postci`, `drv_ddr_get_mem_size`,
`drv_ddr_get_ch_num`, `drv_ddr_set_avail_mem_size`, `MemGetBootEnv`,
`drv_ddr_assert`); it reads memory configuration format 2.0.

## Status

| Step | State |
|---|---|
| Structure, boot path and interfaces mapped (`main` → `fw_init` → `services_init`) | done |
| DDR island generator; with every symbol at its original address it reassembles to CIX's 43 876 code bytes exactly | done |
| Startup, UART, timer, heap, log, RAM log | done |
| Flash access (QSPI and FCH xSPI), firmware table, SE config | done |
| eFuse, key manager, crypto (Mbed TLS for SHA-2, HKDF, RSA; SM2/SM3 here; engine bring-up, TRNG, OTP from CIX's binary) | done |
| SoC init (PLLs, clocks/resets/power, MPU, memory repair, fuse distribution, harvest) | done |
| CI-700, MMHUB, RCSU gaskets (the TZC-400s are set up by the DDR driver) | done |
| Secure boot: load, verify and start CSU_PM and BL2 | done; the signed path is not yet exercised in the emulator |
| Runtime mailbox services, suspend/resume, SFH loading | written; not yet exercised in the emulator |
| Crash information | written; clock and power records are reduced to raw registers |
| Cold boot from reset to AP release, against CIX's firmware in the emulator | identical (831 registers, DRAM and PM SRAM) for life cycles 4, 5 and 7 |

The services keep CIX's interface, with two differences: secret eFuse
fields (device and model keys) are given only to requests on the TEE
mailbox, and the hash service only reads DRAM.  CIX's firmware serves both
to the non-secure side.

`scripts/open/test_se.py` runs CIX's debug build and this firmware in
Unicorn (`scripts/open/se_emu.py`, a model of the SoC as the SE sees it) on
the same flash image and compares every register write and what ends up in
DRAM and the PM's SRAM.
