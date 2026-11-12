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

How it was checked, against CIX's code in the emulator
([`sky1emu se-test`](../../emu/README.md#se-and-sfh-cortex-m33)):

* while the port was under way, `test_se_ddr_fn.py` (Python, retired with
  the Python emulator) called each function of CIX's and of the port from
  the same captured state with the same arguments, random and boundary
  ones too, and compared return values, memory, register writes and
  asserts (115 functions, 0 failing);
* `se-test --ddr` runs both SEs with the driver live on a model of the
  controllers, PHYs and PI blocks (`DdrModel` in
  [emu/src/se/soc.rs](../../emu/src/se/soc.rs), inferred from
  what CIX's driver writes and waits for: the PI's frequency-change
  requests to the PM, INIT_COMP, mode registers as written, BIST passing)
  and compares every register write in order, the registers read, the
  memory, the size and channels reported and the memory configuration
  written back to flash: the same on the O6 (51 634 writes to 9759
  registers), the O6N, the Orange Pi 6 and the 6 Plus (the MS-R1's
  memory configuration is format 1.6, which both drivers refuse with the
  same assertion);
* `se-test --ddr-suite --eye` does the same with 19 feature settings (DFS,
  inline and link ECC, BIST, SSC, train mode, RFM, PBR, address mode, port
  priority, periodic training, the eye scan), 7 kinds of parts as their
  mode registers 5 and 8 would answer, 9 failures (a channel that never
  completes, DFI and PI errors, a failing BIST), every interrupt status
  bit of every group on every channel, and a suspend.

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
| DDR driver translated (`ddr/`), PVT table as generated source; DDR model and `se-test --ddr`, `--ddr-suite` | done: identical to CIX's with the driver live, four boards' memory configurations, every feature switch, parts, failures and interrupts |
| Startup, UART, timer, heap, log, RAM log | done |
| Flash access (QSPI and FCH xSPI), firmware table, SE config | done; both controllers compared with CIX's (the xSPI one on a model after the public Linux driver) |
| eFuse, key manager, crypto (Mbed TLS for SHA-2, HKDF, RSA; SM2/SM3 here; engine bring-up, TRNG, OTP from CIX's binary) | done; SM2 checked against the standard's example (no board's image uses it) |
| SoC init (PLLs, clocks/resets/power, MPU, memory repair, fuse distribution, harvest) | done |
| CI-700, MMHUB, RCSU gaskets (the TZC-400s are set up by the DDR driver) | done |
| Secure boot: load, verify and start CSU_PM and BL2 | done; 20 cases against CIX's: the same but that CIX's starts a CSU_PM that fails its check |
| Runtime mailbox services, suspend/resume | done; every request on both mailboxes and five suspends match CIX's but for the listed differences |
| SFH loading during suspend | done; the image each SE loads starts in the sensor hub's model |
| Crash information | done; compared with CIX's (exception area, hang record, the watchdogs); clock and power records are raw registers under names of their own |
| Cold boot from reset to the idle loop, against CIX's firmware in the emulator | identical (2039 writes to 844 registers in order, the registers read, DRAM and PM SRAM) for every life cycle, with and without device keys |
| `build.sh --open` puts it in bootloader1 (re-signed with the chain-A keys) | done; not yet boot-tested on hardware |

Every way this firmware differs from CIX's, with the test that shows it
and the place in CIX's image it comes from, is in
[differences.md](differences.md). In short: secret eFuse fields (device
and model keys) are given only to requests on the TEE mailbox, the hash
service only reads DRAM, the request to burn the OEM root key hash into
the eFuse is refused (nothing here programs eFuses), and a CSU_PM image
that fails its check under secure boot stops the boot (CIX's starts it).
No debug monitor; the DDR driver's training report and register dumps are
left out; the hang record's clock and power parts are raw registers.

[`sky1emu se-test`](../../emu/README.md#se-and-sfh-cortex-m33) runs CIX's
debug build and this firmware on the same flash image and model of the
SoC, in step, and compares every register write and read, every answer,
and what ends up in DRAM, the PM's SRAM, the sensor hub's SRAM and the
flash; `emu/test-se.sh` runs all of its suites. They execute 81 % of this
firmware's code; the 22 functions never run cannot be reached
([differences.md](differences.md#what-no-test-reaches)). Nothing has run
on hardware.
