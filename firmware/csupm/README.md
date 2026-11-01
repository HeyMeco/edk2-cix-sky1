# Open CSU_PM firmware (work in progress)

CSU_PM is the power-management processor next to the SE: a Cadence Xtensa
LX7 core that runs from PM SRAM once the SE has loaded and verified it
(bootloader1 image `pm`). It owns DVFS and the OPP tables, PMIC and EDP
limits, PLLs and the clock tree, power domains, thermal and fan control,
DDR frequency changes, suspend/resume and the SCMI channels the AP talks
to. This directory rebuilds it from source, a part at a time;
`scripts/open` has the tools to decode CIX's image, run it on a model of
the SoC and compare ours against it.

## Reference material

CIX's `release/pm_fw/Release/csupm_sky1` (CSU_PM 00.12.001, in
[cixtech/cix_proprietary__cix_firmware](https://github.com/cixtech/cix_proprietary__cix_firmware),
pinned in `manifests/cix-debug-firmware.json` and fetched by
`scripts/open/cix_debug.py`, not redistributed) is a `RelWithDebInfo` ELF
with full DWARF: 306 compile units, 2005 functions, types, globals and
location lists. Built with XtensaTools 14.10 (`xt-clang` 10.0.1, RI-2022.10).

| Part | Code | Functions | Source |
|---|---|---|---|
| CIX `Source/common` (DPM, clock tree and PLLs, CI-700/NI-700 PMU, I2C, PMIC, fans, PVT, mailbox, S3 script, logging) | 71 KB | 876 | translated, apart from what only the CLI reaches and what is replaced |
| CIX `Source/projects/sky1` (V/F fuses and tables, power model, PLL policy, HSM, messages, debug CLI 14 KB) | 46 KB | 377 | translated (no CLI) |
| SCP-firmware modules, upstream (SCMI, perf, clock, power domain, PPU v1, DVFS, sensors, PSU) | 32 KB | 360 | public (BSD-3-Clause), with CIX's changes (`scp/module/*/README`) |
| SCP-firmware CIX modules (`sky1soc_perf`, timer, `clock_lite`, `clkpll`, RCSU, PMIC, `locked_perf`) | 8 KB | 95 | translated (`clock_lite` by upstream `clock`) |
| SCP-firmware framework and sky1 product configuration | 11 KB | 228 | public framework; CIX's configuration (`scp/config`) |
| FreeRTOS kernel, Xtensa port, CLI | 11 KB | 175 | replaced: no RTOS (SCP-firmware events and alarms) |
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
| UART (UART4 on the O6) | `0xa0030000` | PL011, output captured; CIX's image logs to a RAM log in DRAM instead and keeps the UART quiet |
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
`dsu_perf`, clocks, power domains, sensors), then sets performance levels
and limits on the OS's channel and writes levels into its fast channels,
reading them back (215 answers in all), and compares the I2C transfers and
clock generator writes these lead to. With CIX's image alone it prints
CIX's answers:

```sh
python3 scripts/open/test_pm.py --scmi                 # CIX's image: reference
python3 scripts/open/test_pm.py OURS.elf --scmi        # ours against it
```

## Firmware

Built in the container with `make` (`make check` also runs the
instruction-set check on the image); SCP-firmware is the submodule
`SCP-firmware` (v2.11.0):

```sh
git submodule update --init firmware/csupm/SCP-firmware
make -C firmware/csupm O=/tmp/csupm all check
```

What runs, in CIX's order:

1. Power-on (CIX's `ss_por_enter`, [`src/main.c`](src/main.c)): core
   clock, AxPROT, local watchdog off, mailbox reset, debug UART, the I2C
   controllers, the PMICs ([`src/pmic.c`](src/pmic.c): the board found by
   probing as CIX does, each EDP rail at its boot voltage), the V/F
   tables ([`src/dpm`](src/dpm): the BIOS's PM configuration, the part
   number and V/F fuses, the table CIX picks for the part from them and the
   PM strap, the minimum voltages computed from the fuses as CIX computes
   them, the OPPs of the 12 DVFS domains), then `0x5c5a` to the SE and a
   wait for its `0x55ca`; then the RTT window into DRAM
   ([`src/rtt`](src/rtt): the TLB window, CIX's channels), the RAM log
   ([`src/log`](src/log): CIX's text records in DRAM; `0xaa55` tells the SE
   it is on) and the version for the AP (ours, in CIX's layout).
2. Init (`ss_init_enter`): the clock tree at its power-on settings
   ([`src/clk`](src/clk)), the interconnect counters stopped and cleared
   ([`src/pmu.c`](src/pmu.c): NI-700 hubs, CI-700 DTC and XP watchpoints,
   DFD trace, AMU, MPMM gears; static layouts from CIX's topology tables),
   each DVFS domain at its sustained OPP (clock, then rail voltage), the
   DDR controllers' low-power interface ([`src/mclp`](src/mclp)).
3. Run (`ss_run_enter`): the counters again, the PVT sensors
   ([`src/pvt.c`](src/pvt.c): eFuse trims, weights, thresholds), CIX's
   message ports on the mailboxes ([`src/msg`](src/msg)), the PCC tables
   for ACPI CPPC ([`src/pcc`](src/pcc)), GPIO.
4. SCP-firmware's modules ([`scp/`](scp)): initialised, bound and
   started, then CIX's `pm_ready` sends `0x5a5c`, which tells the SE to
   start the AP, and starts the run-time services: the DPM, the
   performance streams, fans, PVT polling, board sensors, the watchdog;
   then the PM serves interrupts, alarms and events, sleeping in WAITI.

At run time ([`src/dpm`](src/dpm), [`src/perf`](src/perf), CIX's
`dpm_*.c`, `cfg_*.c`, `sky1_pwr_model.c`, `act_streams.c`, the NI-700 and
CI-700 counters): every 1 ms the performance streams are sampled (AMU,
DDR, NPU, interconnect counters; a 50 µs fine tick on CCOMPARE0) and CIX's
DPM iteration runs: the dashboard, power from the streams and CIX's power
model (in double precision where CIX's is: `src/dpm/dfloat.c`, software
IEEE double, bit-exact), the expected OPPs, the skin and die temperature
arbiters (PID), the OPP limits, and their export to `scmi_perf` (TDP caps,
fast channels) and the AP's data board. Board services
([`src/board`](src/board), [`src/fan`](src/fan), CIX's `dev_fan.c`,
`dev_pvt.c`, `app_pvt.c`, `config_sensor.c`, `intr_mgmt.c`, `dev_gpio.c`,
`dpdpu.c`, `i2c_slave_mode.c`, `dev_pmic.c` and its MP5475, MP2845 and
AU4683 drivers, `pwr_rail_bind.c`, `pm_config.c`) do what the board's PM
configuration (from the BIOS) asks: fans on curves from the PVT and board
sensors (TMP75s or EC registers), PVT polling and thresholds, PROCHOT
throttling, the watchdog, the EC's reads over the I2C slave on bus 3, DDR
DFS only when the display allows it, custom rail layouts (rails sharing a
buck), the heartbeat. CIX's FreeRTOS tasks and its timer, event and job
framework are replaced by SCP-firmware alarms and events (`dpm` and
`pm_svc` modules) with CIX's periods.

The system power sequence ([`src/hsm`](src/hsm), CIX's `subsys_hsm.c` and
`pwr_hsm.c`) runs CIX's state machine: TF-A's suspend (PM message 0x42
with the boot core, then SCMI suspend on its channel) saves the mailboxes,
waits for the boot core and the DSU to idle, puts DRAM in self-refresh
with the controllers' state kept in borrowed PM RAM (`ddr_s3_enter`, its
previous contents saved to DRAM first) and hands over to the SE; on wake
(the SE's `0xaabb`) DRAM comes back (`ddr_s3_exit`, checksummed), the rails
get their voltages again, UEFI's S3 boot script runs
([`src/s3script`](src/s3script)), the run phase is replayed and the boot
core and DSU powered up. Shutdown and warm reset go to the S5 controller
as CIX's do.

CIX's PM messages (a CIX format in the SCMI channels' memory, told apart
by a route field in the header; [`src/msg`](src/msg), CIX's `md_msg.c`,
`app_msginit.c` and handlers) are answered on mailboxes 0, 2, 4 and 6:
firmware version, platform information, the S3 script's registration,
PMIC versions and events, core online/offline and the boot core, locked
performance, SMN register access, performance streams, DSU DVFS mode,
EDP voltages, fans (count, status, mode). CIX never sends the AP a PMIC
error notification (its PM-to-AP send is empty and the report is only
built from its CLI); the AP reads PMIC faults with message 0x400.

SCP-firmware is built as its CMake would build it (the module index and
list are generated from `SCP_MODULES` in the Makefile) with
`BUILD_HAS_SUB_SYSTEM_MODE`, so `fwk_arch_init` returns once the modules
have started. Interrupt vectors ([`arch/vectors.S`](arch/vectors.S)) only
mask a pending line and note it; [`scp/src/arch_interrupt.c`](scp/src/arch_interrupt.c)
runs the handlers from the loop (lines 32 and up exist only there, for
drivers that hand an interrupt on). Modules, upstream and ours:

| Module | From | What |
|---|---|---|
| `transport`, `scmi` | upstream (`transport` with CIX's header writes) | SCMI over the mailboxes' data words (TF-A's `mailbox_mem_t`) |
| `cix_mbox` | ours | the mailboxes' doorbells under `transport` (CIX's `ipc_arbiter` merges both); writes to the OS's performance fast channels to `scmi_perf` |
| `power_domain`, `scmi_power_domain`, `scmi_system_power` | upstream (`scmi_system_power` with CIX's change) | ISP0, GPUTOP under SYSTOP; shutdown, reset and suspend to the system power sequence |
| `rcsu` | ours, CIX's `extmodules/rcsu` | the RCSU power controls (SRAM repair, power bits) |
| `timer` | upstream | alarms |
| `xt_timer` | ours | CCOUNT extended to 64 bits, CCOMPARE2 |
| `clkpll`, `clock`, `scmi_clock` | ours (CIX's `clkpll`), upstream, upstream with CIX's changes | the 273 clocks of CIX's tree the OS sees |
| `sensor`, `scmi_sensor` | upstream | 20 temperatures |
| `soc_sensor` | ours, CIX's changed `reg_sensor` | the PVT sensors and the board's temperatures |
| `resource_perms` | upstream | which agent may use which protocol |
| `dvfs`, `scmi_perf` | upstream, CIX's changes (`scp/module/*/README`) | 12 DVFS domains, SCMI performance with fast channels and plugins |
| `psu` | upstream, CIX's change | supplies over `pmic` |
| `pmic` | ours, CIX's `extmodules/pmic` | the EDP rails through the DPM (`dpm_edp_set_voltage`) |
| `sky1soc_perf` | ours, CIX's `extmodules/plugin_sky1soc` | plugin: CPU clusters and DSU stepped together within 200 mV, TDP caps |
| `locked_perf` | ours, CIX's `extmodules/plugin_lockedperf` | plugin: levels and limits pinned by the platform |
| `dpm` | ours | the DPM's 1 ms tick (alarm), its work (software interrupt 33), the counters' interrupts |
| `pm_svc` | ours | CIX's timer list and jobs for the board services, on one alarm |

The configurations in [`scp/config`](scp/config) are CIX's, read from its
image with [`scripts/open/dwarf_dump.py`](../../scripts/open/dwarf_dump.py)
(a variable as a C initializer, from the DWARF; values from a RAM image
saved by `pm_emu` for tables CIX builds at run time). Where CIX's
configuration is odd it is kept and says so: its power-domain table makes
ISP0 the parent of ISP0 and GPUTOP, so CIX's firmware answers DENIED to
powering either on, and so does ours.

[`lib`](lib) is the C library the firmware and SCP-firmware need (string
functions, a heap, `vsnprintf`, 64-bit division: the toolchain's libgcc is
built for the other endianness). `csupm.xbin` is the bootloader1 image
("pm"): [`scripts/open/xbin.py`](../../scripts/open/xbin.py) writes CIX's
XBIN format (header, one entry per program header, data), checked by
rebuilding the images in both vendor bootloader1 files byte for byte.

Against CIX's image in `test_pm.py` (CIX's boot: 1329 registers, 8 pages
of shared memory, 33 I2C transfers, then 215 SCMI answers): it boots and
completes the handshake; all 1329 registers get the same values in the
same order; the 33 I2C transfers are the same, in order; all 215 SCMI
answers are the same (base, power domains, system power, sensors, the 273
clocks, performance on 12 domains, levels set directly and through fast
channels, with the same PLL writes and I2C transfers as CIX's). Of the
shared memory, the RTT control block and the PCC tables are CIX's byte for
byte; three pages differ: the version (ours), and the RAM log (CIX's text
records, but CIX's line buffer fills early in boot and is never flushed,
so CIX's log holds bytes from past it; ours holds its messages).

With `--power` it then sends the PM messages UEFI, Linux and TF-A send,
runs TF-A's suspend, the SE's wake, SCMI after resume, shutdown and warm
reset; with `--services` the run-time services after `pm_ready` (fans at
two temperatures, Linux's fan modes, a PVT threshold, PROCHOT, a PMIC
fault, a DDR DFS check); with `--runtime MS` the DPM and streams for MS
simulated milliseconds, a high level asked on big0, then the die at 100 C
with PROCHOT (pm_emu fast-forwards time while the PM sleeps). `--board
o6|o6n|ms-r1|opi6|opi6plus|PATH` loads a board's PM configuration into
both images, `--set NAME=VALUE` changes one of its words. Every phase is
compared register by register, with the I2C transfers, the SE's and the
S5 controller's events, the answers and, for the run time, the DPM's
state and the AP's data board:

```sh
python3 scripts/open/test_pm.py OURS.elf --scmi --power --services --runtime 100 [--board o6n]
```

All of it is the same as CIX's, with no board configuration and with each
of the five boards' (O6, O6N, MS-R1, Orange Pi 6 with its MP2845 and
shared bucks, Orange Pi 6 Plus), apart from the version and RAM log pages
and, on the MS-R1, where its 200 ms board-sensor read falls among the
SCMI questions (paced by instructions, not time; the transfers are the
same). A run with every phase takes several minutes (CIX's reference is
cached in `out/test_pm-cache`). What pm_emu models of devices (the S5
controller, power policy units, DDR controllers, the SE's side of
suspend, PMICs and TMP75s, tachometers, the watchdog's lock) is in its
source: modelled on what CIX's image does with them, not known from
documentation.

[`scripts/open/test_vf.py`](../../scripts/open/test_vf.py) checks the V/F
tables beyond the emulator's blank fuses: it gives both images the same
simulated part (PM strap, random V/F fuses for an ATE phase, part number,
the BIOS's configuration, Phecda's PMICs, GB1's boost) and compares the 12
domains' OPPs, the table selectors and the I2C transfers:

```sh
python3 scripts/open/test_vf.py OURS.elf --strap 0x000c0000:0 --fuses 7 --phase 4
```

The tables match in every case tried.

Not ported, on purpose: the debug CLI and what only it reaches (CI-700
event collections and DFD trace, GPIO interrupt helpers, forced operating
points), logging and dumps, CIX's interrupt-driven I2C path (the polled
driver moves the same bytes). Where the open firmware differs from CIX's
on purpose: the heartbeat timer is created once (CIX's adds another on
every resume), and the services start 1 ms after the handshake (CIX's
from its event worker once it first idles). What the emulator cannot
show: real fans (pulses per turn, polarity, stall), the EC's reads over
the I2C slave, MP2845/AU4683 read-back formats, the watchdog's count. CIX
never unmasks interrupt lines 28-30 (PVT alarms, PROCHOT), whose reset
masks are unknown; the test unmasks them to exercise those paths.

RAM: DRAM0 (64 KB: read-only data, initialised data) has about 2 KB left;
the DPM's and board's initialised tables are in DRAM1 (`.dram1.data`, as
in CIX's image), and the heap leaves 53 KB beyond what DDR retention
borrows during suspend (`pm.ld` checks it).

### What CIX's image does before it idles

From `pm_emu` with a backtrace on every device write (registers written,
in order; the shared-memory ones counted by 4 KiB page):

| Phase | What | Registers |
|---|---|---|
| `main`, `ss_por_enter` (overlay) | core clock, AxPROT, local watchdog off, mailbox reset, debug UART, I2C masters, handshake `0x5c5a` | ~15 |
| `sky1_rtt_init`, `RTT_Init` | TLB window from the hub to DRAM, RTT block | ~40 |
| `rlog_init`, `md_version_broadcast` | RAM log in shared memory, version | 3 pages |
| `ss_init_enter` | boot reason; clock tree (`clk_nodes_init`, 33 registers); perf streams (`perf_stream_init`: 755 CI-700/NI-700 PMU registers); DVFS sustained levels (PMIC voltages over I2C, PLLs, 83 registers) | ~870 |
| `ss_run_enter` | data board; perf streams restored; PVT sensors (41); counters; interrupt aggregator; the ten mailboxes cleared and routed (406); PCC tables for the AP (shared memory); GPIO | ~480 |
| SCP-firmware task (`scp_fw_main`) | `fwk_arch_init` (77 registers), module init, clock-PLL jobs (6) | ~85 |

## Plan

How much of CIX's code the port covers (by function size; translation, not
equivalence, which `test_pm.py` checks):

```sh
python3 scripts/open/csupm_coverage.py OURS.elf [--units]
```

| Step | State |
|---|---|
| Toolchain for pm_core_v1, instruction-set check | done |
| FLIX formats of pm_core_v1, inferred from known code (`xtensa_flix.py`) | done |
| Emulator for the core (`xt_emu.py`), running CIX's image into its drivers | done |
| Model of the SoC as CSU_PM sees it (`pm_emu.py`): CIX's image reaches its idle task and answers SCMI | done |
| Differential test harness (`test_pm.py`: register writes, I2C, SCMI answers, PM messages, suspend/resume, shutdown, reset) | done |
| Start-up, vectors, libc, XBIN image; stage 1 boots and does the SE handshake | done |
| Power-on: clock and reset block, watchdog, UART, I2C, PMICs | done |
| Interconnect counters, PVT sensors, mailboxes | done |
| Interrupts (aggregator, mailboxes), timer, idle with WAITI | done |
| SCP-firmware framework and modules: transport, SCMI base, power domains, system power, sensors, permissions | done |
| Clock tree and PLLs, SCMI clocks | done |
| V/F tables from the fuses, boot DVFS levels (`test_vf.py`) | done |
| DVFS: PMIC, PSU, DVFS, performance plugins, SCMI perf | done |
| DPM runtime: performance streams, power model, arbiters, TDP/EDP limits (`--runtime`) | done |
| Board services: fans, PVT, board sensors, PROCHOT, watchdog, MP2845/AU4683, rail layouts, I2C slave, DDR DFS, with the boards' PM configurations (`--services`, `--board`) | done |
| RTT, RAM log, version, PCC tables | done |
| System power sequence (shutdown, reset, suspend/resume, DDR retention, S3 script), PM messages | done |
| CIX `Source/common` and `Source/projects/sky1`, without the debug CLI | 72% and 96% of CIX's code; what is left (`csupm_coverage.py --units`) is replaced (FreeRTOS, CIX's timer/event/job framework, `ipc_arbiter`), CLI-only, or CIX's interrupt-driven I2C path |
| Our image passes `test_pm.py` | done: every phase, six configurations; the version and RAM log pages differ by design |
| `build.sh --open` puts it in bootloader1 (`--vendor-pm` keeps CIX's); the open SE verifies it in secure boot and both SEs load it identically (`test_se.py`) | done |
| Boot test on hardware (UART4, SCMI from Linux, fans, suspend/resume) | to do |
