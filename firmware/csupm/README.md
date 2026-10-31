# Open CSU_PM firmware (work in progress)

CSU_PM is the power-management processor next to the SE: a Cadence Xtensa
LX7 core that runs from PM SRAM once the SE has loaded and verified it
(bootloader1 image `pm`). It owns DVFS and the OPP tables, PMIC and EDP
limits, PLLs and the clock tree, power domains, thermal and fan control,
DDR frequency changes, suspend/resume and the SCMI channels the AP talks
to. This directory will rebuild it from source; for now it holds the
toolchain, and `scripts/open` has the tools to decode CIX's image, run it on
a model of the SoC and compare ours against it.

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
* exclusive-access state, which the FreeRTOS port saves with `getex`,
  and `clrex` (bytes `20 31 00`: ST0, r 3, s 1, t 2), which CIX's
  image has only where each interrupt dispatcher hands over to the RTOS.
* interrupt levels (1,1,1,1,2,3,1,4,5,4,5,5, then 1, NMI on 31); EXCM
  level 5, so levels 2 to 5 are dispatched in C; the FreeRTOS tick is
  CCOMPARE1 on interrupt 7 (level 4), the context switch the software
  interrupt 9 (level 4), a hardware timer interrupt 8 (level 5).
* coprocessor 0 is enabled at reset: CIX's `main` uses the FPU
  (`pwr_leakage_init`) before anything writes CPENABLE. Our start-up code
  sets CPENABLE itself.

## SoC model

[`scripts/open/pm_emu.py`](../../scripts/open/pm_emu.py) puts the
interpreter on a model of Sky1 as CSU_PM sees it, as `se_emu.py` does for
the SE: registers are plain memory unless a device gives them behaviour.
CSU_PM's local window `0xa0000000` is SoC `0x06510000` (the SE sees the
same blocks at `0x46510000`). What CIX's image needs to boot:

| Block | CSU_PM | Model |
|---|---|---|
| I2C masters 0-2, slave | `0xa0010000`-`0xa0013000` | Cadence I2C (`cdns,i2c-r1p14`, 16-bit registers), transfers complete at once; any address answers as a register file. The three MP5475 PMICs are at `0x60` on masters 0-2 |
| UART (UART4 on the O6) | `0xa0030000` | PL011, output captured; CIX's image logs in binary to a trace buffer instead and keeps the UART quiet |
| clock generator (CGC) | `0xa0060000` | 23 PLLs: +0 feedback divider, +4 fraction and enable, +8 post and reference dividers, +0xc lock; VCO = 24 MHz × (fbdiv + frac/2²⁴) / refdiv, which CIX's image wants between 1.6 and 2.1 GHz. Reset values unknown, the model uses 2 GHz (a read of SoC `0x06570000` on a running board would tell) |
| mailboxes 0-9 | `0xa0080000` + 0x10000 × i | 32 data words, doorbell +0x80, interrupt enables +0xbc (to the AP) and +0xc0 (to the PM), clear +0xc4, status +0xc8. Even mailboxes take AP requests, odd ones carry the PM's |
| SE handshake | `0xa0100090` (mailbox 8) | the SE's side: PM `0x5c5a`, SE `0x55ca`, PM `0x5a5c` (`firmware/se/service/main.c`) |
| interrupt aggregator | `0xa0000500` | lines 12-30 each gather up to 32 sources: raw status at +4 × (line − 12), enable 0x80 above it. Line 25 is the mailboxes (bit = mailbox) |
| busy-waits | `cc_delay` | done at once by advancing CCOUNT |

With that, CIX's image boots to its idle task without an assertion in 3.5
million instructions (8 s) and answers SCMI. The shared memory is in the
mailbox's data registers, laid out as TF-A's `mailbox_mem_t`:

| Mailbox | SoC | Agent's protocols |
|---|---|---|
| 0 | `0x06590000` | base, power domain, system, perf, clock, sensor (the OS) |
| 2 | `0x065b0000` | base, power domain, system (TF-A, `sky1_bl31_setup.c`) |
| 4, 6 | `0x065d0000`, `0x065f0000` | base, power domain, system, perf, sensor |

[`scripts/open/test_pm.py`](../../scripts/open/test_pm.py) is the
differential test, as `test_se.py` is for the SE. It boots each image,
compares register writes per register and the I2C transfers, and with
`--scmi` asks every channel the same 185 questions (discovery, then each
protocol's attributes and domains: 8 perf domains from `gpu_core` to
`dsu_perf`, clocks, power domains, sensors). With CIX's image alone it
prints CIX's answers:

```sh
python3 scripts/open/test_pm.py --scmi                 # CIX's image: reference
python3 scripts/open/test_pm.py OURS.elf --scmi        # ours against it
```

## Plan

| Step | State |
|---|---|
| Toolchain for pm_core_v1, instruction-set check | done |
| FLIX formats of pm_core_v1, inferred from known code (`xtensa_flix.py`) | done |
| Emulator for the core (`xt_emu.py`), running CIX's image into its drivers | done |
| Model of the SoC as CSU_PM sees it (`pm_emu.py`): CIX's image reaches its idle task and answers SCMI | done |
| Differential test harness (`test_pm.py`: register writes, I2C, SCMI answers) | done; suspend/resume to add |
| Start-up, vectors, FreeRTOS port, libc | to do |
| SCP-firmware framework and modules (upstream plus CIX's changes), CIX modules and configuration | to do |
| CIX `Source/common` and `Source/projects/sky1`, without the debug CLI | to do |
| Our image passes `test_pm.py` | to do |
| `build.sh --open` puts it in bootloader1 | to do |
