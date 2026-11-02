# Open SE firmware (work in progress)

The SE is the Cortex-M33 security core that the boot ROM starts first. Its
firmware (bootloader1 image `se`, 0x20000000) trains DRAM, distributes eFuse
data, sets up clocks, power, the interconnect and the TZC-400s, verifies and
starts CSU_PM and BL2, and then serves the AP and the PM over the mailbox
(crypto, eFuse, keys, suspend and resume) for as long as the system runs.

This directory rebuilds all of it from source, CIX's DDR driver (DRAM
training) included.

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
| `driver/ddr` | 44 KB, 195 functions | LPDDR5 controller/PHY/PI init and training, BIST, eye scan | done (`ddr/`, CIX's file names) |
| `driver/vf_sel` | 11 KB data | PVT table (V/F selection by wafer and die position) | generated source (`driver/pvt_data.c`) |
| `library/ce_lite` | 61 KB | crypto engine (hash, RSA, SM2, ECC, AES, SM4, TRNG, OTP) | done: Mbed TLS for SHA-2, HKDF, RSA; SM2/SM3 here; engine bring-up, TRNG, OTP |
| `service/debug/monitor` | 26 KB | UART debug monitor | not planned (debug only) |
| `hal/common` | 15 KB | log, storage (flash images), crash, crypto glue | done |
| `framework` | 18 KB | eFuse, key management, image verification, xloader, jumper | done |
| `driver/*` (other) | 30 KB | CRU, PLL, QSPI/xSPI, CI-700, TZC-400, mailbox, I2C/PMIC, RCSU, … | done |
| `service` | 9 KB | boot, cold/warm boot, runtime services, STR | done |
| `platform/sky1` | 4 KB | SoC description, wake-up sources | done |

## DDR driver

[`ddr/`](ddr) is CIX's DDR driver translated function by function from the
debug ELF, one file per CIX source file, with CIX's function names and
its `drv_ddr_assert` file codes and line numbers; CIX's oddities are kept
and commented (e.g. `CdcbUpdateBlockBuf` writes back as many bytes as the
new checksum's value), its training report and register dumps left out.
Generated from the ELF by scripts, with `--check` against CIX's data:
`MemLpddr5RegData.c` (the LPDDR5 register tables, 1679 entries:
`scripts/open/se_ddr_tables.py`), `mem_types.h` (the driver's types,
`scripts/open/se_ddr_header.py`) and `driver/pvt_data.c` (the PVT table,
2668 entries, CIX's 10 672 bytes exactly: `scripts/open/se_pvt_table.py`).

How it was checked, against CIX's code in the emulator:

* `scripts/open/test_se_ddr_fn.py` calls each function of CIX's and of the
  port from the same captured state (from a run of CIX's SE) with the same
  arguments, random and boundary ones too, and compares return values,
  memory, register writes and asserts (115 functions, 0 failing);
* `test_se.py --ddr` runs both SEs with the driver live on a model of the
  controllers, PHYs and PI blocks (`se_emu.py` `DdrModel`, inferred from
  what CIX's driver writes and waits for: the PI's frequency-change
  requests to the PM, INIT_COMP, mode registers as written, BIST passing)
  and compares every register write, the memory, the size and channels
  reported and the memory configuration written back to flash: identical
  on the O6 (9739 registers) and O6N (9745), and with each driver feature
  switched (DFS, inline and link ECC, BIST, SSC, train mode, RFM, PBR,
  address mode, port priority; `--ddr-feature`), the eye scan too.

What only real DRAM shows: training itself (the PI does it in hardware:
frequency order, failures and retries), the parts' mode-register values,
real BIST and eye-scan results, the interrupt masks' reset values.

`scripts/open/se_ddr_island.py` (CIX's driver as relinkable assembly; it
reassembles to CIX's 43 876 code bytes exactly) built the firmware while
the port was under way and is kept for tests: `make CIX_ELF=...` links
CIX's code for every function `ddr/` does not define, and
`make DDR=cix CIX_ELF=...` leaves `ddr/` out and links CIX's whole driver
(`./build.sh <board> --open --cix-ddr`: on hardware, tells a fault in the
port from one elsewhere in the SE).

## Status

| Step | State |
|---|---|
| Structure, boot path and interfaces mapped (`main` → `fw_init` → `services_init`) | done |
| DDR island generator; with every symbol at its original address it reassembles to CIX's 43 876 code bytes exactly | done (kept for tests) |
| DDR driver translated (`ddr/`), PVT table as generated source; DDR model and `test_se.py --ddr` | done: identical to CIX's with the driver live, O6 and O6N, every feature switch |
| Startup, UART, timer, heap, log, RAM log | done |
| Flash access (QSPI and FCH xSPI), firmware table, SE config | done |
| eFuse, key manager, crypto (Mbed TLS for SHA-2, HKDF, RSA; SM2/SM3 here; engine bring-up, TRNG, OTP from CIX's binary) | done |
| SoC init (PLLs, clocks/resets/power, MPU, memory repair, fuse distribution, harvest) | done |
| CI-700, MMHUB, RCSU gaskets (the TZC-400s are set up by the DDR driver) | done |
| Secure boot: load, verify and start CSU_PM and BL2 | done; with secure boot fused for its keys, the open O6 image boots in the emulator, and does not with one byte of BL2 or CSU_PM changed |
| Runtime mailbox services, suspend/resume | done; answers to run-time requests and everything from a suspend request to the resume message match CIX's (804 registers) |
| SFH loading during suspend | written; not yet exercised in the emulator |
| Crash information | written; clock and power records are reduced to raw registers |
| Cold boot from reset to AP release, against CIX's firmware in the emulator | identical (831 registers, DRAM and PM SRAM) for life cycles 4, 5 and 7, with and without device keys |
| `build.sh --open` puts it in bootloader1 (re-signed with the chain-A keys) | done; not yet boot-tested on hardware |

The services keep CIX's interface, with three differences: secret eFuse
fields (device and model keys) are given only to requests on the TEE
mailbox, the hash service only reads DRAM, and the request to burn the
OEM root key hash into the eFuse is refused: nothing here programs eFuses.
CIX's firmware does all three for the non-secure side.

Elsewhere: no debug monitor; the memory-configuration block lookup stays
inside its table (CIX's reads past it); a write enable per erased flash
sector; in the DDR driver, no training report or register dumps, the
BIST's pass/fail arrays (only printed) dropped with the BIST runs kept,
and the density table padded to 16 entries because CIX's lookup reads past
its 9. The full list for every stage is in
[docs/open-firmware.md](../../docs/open-firmware.md#how-the-open-firmware-differs-from-cixs).

`scripts/open/test_se.py` runs CIX's debug build and this firmware in
Unicorn (`scripts/open/se_emu.py`, a model of the SoC as the SE sees it) on
the same flash image and compares every register write and what ends up in
DRAM and the PM's SRAM.
