# Open CSU_PM firmware (work in progress)

CSU_PM is the power-management processor next to the SE: a Cadence Xtensa
LX7 core that runs from PM SRAM once the SE has loaded and verified it
(bootloader1 image `pm`). It owns DVFS and the OPP tables, PMIC and EDP
limits, PLLs and the clock tree, power domains, thermal and fan control,
DDR frequency changes, suspend/resume and the SCMI channels the AP talks
to. This directory will rebuild it from source; for now it holds the
toolchain, and `scripts/open` has the tools to decode and run CIX's image.

## Reference material

CIX's `release/pm_fw/Release/csupm_sky1` (CSU_PM 00.12.001, in
[cixtech/cix_proprietary__cix_firmware](https://github.com/cixtech/cix_proprietary__cix_firmware),
pinned in `manifests/cix-debug-firmware.json` and fetched by
`scripts/open/cix_debug.py`, not redistributed) is a `RelWithDebInfo` ELF
with full DWARF: 306 compile units, 2005 functions, types, globals and
location lists. Built with XtensaTools 14.10 (`xt-clang` 10.0.1, RI-2022.10).

| Part | Code | Functions | Source |
|---|---|---|---|
| CIX `Source/common` (DPM, clock tree and PLLs, CI-700/NI-700 PMU, I2C, PMIC, fans, PVT, mailbox, S3 script, logging) | 71 KB | 876 | to write |
| CIX `Source/projects/sky1` (V/F fuses and tables, power model, PLL policy, HSM, messages, debug CLI 14 KB) | 46 KB | 377 | to write (no CLI) |
| SCP-firmware modules, upstream (SCMI, perf, clock, power domain, PPU v1, DVFS, sensors, PSU) | 32 KB | 360 | public (BSD-3-Clause), CIX changes to find |
| SCP-firmware CIX modules (`sky1soc_perf`, timer, `clock_lite`, `clkpll`, RCSU, PMIC, `locked_perf`) | 8 KB | 95 | to write |
| SCP-firmware framework and sky1 product configuration | 11 KB | 228 | public framework; configuration to write |
| FreeRTOS kernel, Xtensa port, CLI | 11 KB | 175 | public (MIT) |
| Cadence runtime (`xclib`, libgcc, HAL, XTOS, overlay manager) | 18 KB | 121 | replaced (own libc, GCC's libgcc) |

Twenty code overlays (32 KB, paged into one window at `0x2002f224`) hold
init-once, self-test and CLI code.

## Toolchain

The core (`CORE_NAME="pm_core_v1"`, LX7.1.10, HW_CONFIGID
`0xc000b286`/`0x2a8a7d6a`) has no user TIE (all TIE checksums zero), so
GNU GCC can target it: Espressif's `xtensa-esp-elf` GCC 14.4 loads the core
description at run time from the object named by `XTENSA_GNU_CONFIG`
(xtensa-dynconfig). [`scripts/open/xtensa_core.py`](../../scripts/open/xtensa_core.py)
builds that object from Espressif's ESP32-S3 overlay (LX7, the closest
public configuration) with the overrides in
[`core/pm_core_v1.json`](core/pm_core_v1.json). The container
(`docker/Dockerfile`) has the compiler and the overlay sources, pinned.

What the core has was read from CIX's ELF with
[`scripts/open/xtensa_isa.py`](../../scripts/open/xtensa_isa.py)` census`,
which decodes the instruction ranges of the ELF's property table:
windowed ABI, density, zero-overhead loops, MUL16, MUL32 and MUL32_HIGH,
DIV32, NSA, MIN/MAX, SEXT, booleans, LX7's SALT/SALTU and a
single-precision FPU with divide steps. CIX's compiler never emits MAC16,
CLAMPS, S32C1I or L32AI/S32RI, and the configuration turns them off.
`xtensa_isa.py check` disassembles a build and fails on any instruction
outside that set; `make check` runs it on a test file at `-O0`, `-O2` and
`-Os` (CI does this for the open builds):

```sh
make -C firmware/csupm O=/tmp/csupm check      # in the build container
```

The core also has 8-byte FLIX bundles (op0 14 and 15; `Xthal_op0_format_lengths`).
GCC never emits them. CIX's compiler does: 2695 of about 64 000
instructions, half of them in the C library and libgcc. Their formats are
part of the (unpublished) core configuration;
[`scripts/open/xtensa_flix.py`](../../scripts/open/xtensa_flix.py) has them,
worked out from code whose source is known: op0 14 is three slots (the
first is the core's 24-bit encoding with op0 replaced; in `memcpy`,
`7e 23 03 …` is `l32i a7, a3, 12`), op0 15 is that slot plus a conditional
branch with an 18-bit offset. All 3016 bundles of CIX's image, the overlays
included, decode, and every branch target is an instruction start. The two
uncertain encodings (a rare slot-0 `loopnez`/`loopgtz`, a slot-1 signed or
unsigned 16-bit multiply) are marked in the file.

## Emulator

[`scripts/open/xt_emu.py`](../../scripts/open/xt_emu.py) is an interpreter
for the core (about 450 000 instructions per second): windowed registers
with overflow and underflow exceptions, zero-overhead loops, XEA2
exceptions and interrupts, the three CCOUNT/CCOMPARE timers, FLIX bundles
(all slots read, then write, then the one control transfer), code that
overlays copy in at run time, and a log of every bus write. It runs CIX's
image from the reset vector through `_start` and `main` into the drivers.
What that showed about the core, beyond the census:

* a double-precision FPU (64-bit FP registers; opcodes as in QEMU's
  public `de233_fpu` core description; `pwr_leakage_init` loads 10000.0
  with `wfrd`). Our GCC configuration does not use it.
* exclusive-access state, which the FreeRTOS port saves with `getex`.
* interrupt levels (1,1,1,1,2,3,1,4,5,4,5,5, then 1, NMI on 31); EXCM
  level 5, so levels 2 to 5 are dispatched in C; the FreeRTOS tick is
  CCOMPARE1 on interrupt 7.
* coprocessor 0 is enabled at reset: CIX's `main` uses the FPU
  (`pwr_leakage_init`) before anything writes CPENABLE. Our start-up code
  sets CPENABLE itself.

With every device register reading 0, CIX's image ends up polling an I2C
controller (`i2c_controller_write_polled`, `0xa0012000`) forever. Blocks it
touches before that: the core clock block (`0xb0010000`), the local
watchdog, UART and I2C controllers (`0xa0000000` to `0xa0013000`), the
platform ID (`0x45040000`) and `0x56000000`. The SoC model that answers
them is the next step.

## Plan

| Step | State |
|---|---|
| Toolchain for pm_core_v1, instruction-set check | done |
| FLIX formats of pm_core_v1, inferred from known code (`xtensa_flix.py`) | done |
| Emulator for the core (`xt_emu.py`), running CIX's image into its drivers | done |
| Model of the SoC as CSU_PM sees it (as `se_emu.py` for the SE): I2C and PMIC, clocks, mailboxes, power controllers; CIX's image reaches its idle loop and answers SCMI | to do |
| Start-up, vectors, FreeRTOS port, libc | to do |
| SCP-firmware framework and modules (upstream plus CIX's changes), CIX modules and configuration | to do |
| CIX `Source/common` and `Source/projects/sky1`, without the debug CLI | to do |
| Differential test against CIX's image (register writes, SCMI answers, suspend/resume) | to do |
| `build.sh --open` puts it in bootloader1 | to do |
