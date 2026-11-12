# sky1emu

Emulators for the Sky1 boot firmware, and the tests that compare the open
firmware with the vendors' images in them. One Rust crate, no
dependencies, run on the host (not in the build container).

For now it has the CSU_PM: an interpreter for its Xtensa LX7 core
(`pm_core_v1`), a model of the SoC as the PM sees it, and the tests that
run the open firmware ([firmware/csupm](../firmware/csupm)) and CIX's image
side by side. It replaces the Python `xt_emu.py`, `pm_emu.py`, `test_pm.py`,
`test_pm_faults.py` and `test_vf.py`, which are gone. The Orion O6's EC
is here too ([below](#ec-orion-o6); it replaces
`i8051_emu.py`, `ec_emu.py` and `test_ec.py`). The SE and the sensor
fusion hub are here as well ([below](#se-and-sfh-cortex-m33); they replace
`se_emu.py`, `test_se.py`, `test_se_ddr_fn.py` and `test_sfh.py`).

## Build

```sh
cargo build --release --manifest-path emu/Cargo.toml    # emu/target/release/sky1emu
cargo test --release --manifest-path emu/Cargo.toml
```

The open image is `csupm.elf` from `make -C firmware/csupm O=DIR all`.
CIX's image is its debug build (`csupm_sky1`, full DWARF), pinned in
[manifests/cix-debug-firmware.json](../manifests/cix-debug-firmware.json)
and read from `blobs-cache/`; when it is not there yet the tool fetches it
with `scripts/open/cix_debug.py`. `--cix CIX.elf` takes another. The
boards' PM configurations come from the build output
(`output/BOARD/RELEASE/pr/Firmwares/csu_pm_config.bin`) and, for the
Orange Pi 6, from `vendor/orangepi/opi6`; build a board first to test with
its configuration.

## Commands

| Command | What it does |
|---|---|
| `pm ELF [--count N] [--until SYMBOL]` | boot an image on the model; print its console, the I2C traffic and where it stopped |
| `pm-test [OPEN.elf] [options]` | the differential test: CIX's image alone (the reference), or the open image against it |
| `pm-faults OPEN.elf [-v]` | I2C buses made to hang or fail, open image alone |
| `pm-vf OPEN.elf [options]` | the DVFS tables both images build for a simulated part |
| `pm-script ELF [--board B] [--set N=V] [--ddr-dfs] 'CMD; ...'` | drive one image by hand, to look into a finding |
| `cov FILE ELF... [--partial] [--all] [--only PREFIX] [--summary]` | which code of each image the runs recorded in FILE executed |

`pm-test` options: `--scmi`, `--power`, `--services`, `--runtime MS`,
`--full` (the suites, below), `--board NAME|PATH`, `--set NAME=VALUE` (one
word of the board's configuration, its sums redone: `gpio0`..`gpio2`,
`op_mode`, `psi7`, `wdt`, `trip`, the SoC idle switch's `noc_idle`,
`noc_idle1`..`noc_idle4`, the skin temperature controllers' `spt_mask`,
`spt_set0`..`spt_set3`, `spt_margin`, `spt_kh`, `spt_ka`, `spt_c`,
`spt_alpha`; `CFG_WORDS` in [test.rs](src/pm/test.rs)), `--ddr-dfs` (the
DDR controllers report both frequency copies trained), `--only TEXT` (the
`--full` phases whose name has TEXT), `--audit`, `--cov FILE`, `-v`.
`pm-vf` options: `--strap LO:HI`, `--fuses SEED`, `--phase N`,
`--opn HI:LO`, `--vmin`, `--phecda`, `--bios ITEM,...`, `--gb1boost`,
`--no-i2c`, `--matrix`. The `//!` header of each source file has the
details.

```sh
E=emu/target/release/sky1emu
$E pm-test --scmi                                          # CIX's answers
$E pm-test OPEN.elf --scmi --power --services --runtime 100 --full --board o6n
$E pm-test OPEN.elf --full --only 'thermal sweep' -v
$E pm-faults OPEN.elf
$E pm-vf OPEN.elf --strap 0x000c0000:0 --fuses 7 --phase 4
$E pm-vf OPEN.elf --matrix
$E pm-script OPEN.elf --board ms-r1 'scmi 0 0x13 7 3 6931; ms 5; i2c 8; writes 10'
emu/test-pm.sh OPEN.elf --cov                              # everything
```

[`test-pm.sh`](test-pm.sh) `OPEN.elf [--cov] [--audit]` runs every
comparison side by side: `pm-test` with every suite in seven
configurations (the default, the five boards', and `options`, below),
`pm-faults` and `pm-vf --matrix`. Logs go to `out/emu/test/`; the exit status is 0 when
nothing differs. `--cov` also writes `out/emu/test/coverage.txt`, `--audit`
runs `pm-test` with the interpreter's audit. On eight cores, with `--cov`,
it takes five to ten minutes. There is no result cache.

## Method

Both images run in the same model and are asked the same things in
lock-step. Everything is driven by simulated time, not by instruction
counts. Time is kept apart from the core's cycle counter, in cycles of the
800 MHz clock the core's is divided from (`TIME_HZ` in
[soc.rs](src/pm/soc.rs)); the model follows the divider the firmware sets:
400 MHz normally, 200 MHz while the SoC idle switch has the SoC idle,
100 MHz suspended. So a period counted in a fixed number of cycles (CIX's
FreeRTOS tick) lasts longer at a lower clock, as it would on the chip. In
the comparison:

- each question takes the same simulated time in both images: it is padded
  to a 2 ms slot, and the faster image waits for the slower;
- before the model is changed (temperatures, lines) the clocks are aligned
  to a 20 ms grid; in the fan phases, to the fans' loop's 500 ms period;
- an agent waiting for an answer looks at the channel every 2000
  instructions; a PM that answers and goes to sleep stays asleep to the
  end of that interval (time does not jump to its next timer);
- when a question kills one image (an assertion, a crash, no answer), both
  are put back as they were before it, so later questions find them in
  the same state; the answer is still compared.

Compared per phase: how it ended, every register written and the values in
order, the I2C transfers per bus, the SE handshake words, the events (sleep,
wake, resets), the doorbells to the AP and every answer; the shared memory
by its final contents.

## Suites

- boot (always): reset to idle with the SE handshake done, without an
  assertion;
- `--scmi`: every channel's protocols and domains, performance levels and
  limits set and read back, the OS's fast channels, and the I2C transfers
  and clock generator writes they lead to;
- `--power`: CIX's PM messages as UEFI, Linux and TF-A send them, TF-A's
  suspend, the wake, the resume, shutdown, warm reset;
- `--services` and `--runtime MS`: the board services (fans, PVT, board
  sensors, PROCHOT, PMIC faults, DDR DFS) and the DPM's run time; with the
  skin temperature controllers on, also "a level by message while the
  DPM's cap falls" (the cap comes down over the first 132 ms after the
  image idles; with them on, the `--scmi` questions wait 150 ms for it to
  settle);
- `--full` ([full.rs](src/pm/full.rs), `STEPS`: 27 phases): what the
  others leave out, each phase from a copy of the image 100 ms after it
  idles: every SCMI message of every protocol on every channel, every
  domain and level, notifications, malformed messages, every PM message id
  and its arguments, PCC commands by the fast channel, the I2C slave the
  EC reads (and its transfers going wrong: lost arbitration, a time-out
  with the bus held busy, FIFO overflows), the fans by time (every mode;
  the automatic loop from 30 to 100 C and at the sensors' raw codes), a
  temperature sweep from 30 to 130 C and back with the PVT and PROCHOT
  lines masked and unmasked, interrupt lines by themselves (PVT safe,
  PROCHOT for 3 ms and held 3 s, the local GPIO), the SoC idle switch and
  the DDR's frequency, the watchdog with its interrupt on then off, a
  suspend by PM message 3 and a second suspend;
- `pm-faults` ([faults.rs](src/pm/faults.rs)): a stuck or failing bus
  costs only the work that needs it, a frequency never goes up on a
  voltage that failed, the stacks' high-water marks;
- `pm-vf` ([vf.rs](src/pm/vf.rs)): straps, fuses, ATE phases, part
  numbers, BIOS configurations, Phecda's PMICs, GB1's boost.

Board configurations: the default (none loaded), `o6`, `o6n`, `opi6`,
`opi6plus` and `ms-r1`, or a file. The PM drives the fans on the O6N, the
MS-R1 and the Orange Pi 6; on the O6 and Orange Pi 6 Plus the EC does.
`test-pm.sh` adds `options`: the O6N's configuration with every option of
the BIOS's PM configuration block that no shipping board sets (the
watchdog at 1 s, the heartbeat and EC GPIOs, the SoC idle switch's
thresholds with `--ddr-dfs`, the four skin temperature controllers on).

The model's devices do what CIX's image expects of them, not what a
document says; the DDR controllers' frequency-switch handshake, for one,
is inferred from what CIX's code waits for.

## Expected differences

A difference that is known and meant, or that is CIX's image failing, is
an entry of `EXPECTED` in [full.rs](src/pm/full.rs): the phase, what
differs, what each image gives, and the reason (string constants above
the table). Registers written on a timer may be cut one period
differently at a phase's ends (`PERIODIC` there), and so may a sensor read
on a timer (`POLLED` in [test.rs](src/pm/test.rs): the MS-R1's TMP75).
An entry marked `capped: true` counts only in a configuration whose DPM
caps the CPU domains from boot (the skin temperature controllers on:
`set_capped`, i.e. the `options` configuration). Anything else is a
failure. The kinds now:

- CIX's image dies on any SCMI message of a protocol it does not have
  (0x16 and up): its response logger indexes a table of six. Ours answers
  `NOT_SUPPORTED`.
- CIX's clock protocol answers `CLOCK_NAME_GET` and the rate
  notifications; ours has not got them (no clock offers either).
- The DSU's level by message: ours sets the PLL's post-divider, CIX's DVFS
  programs the PLL ("one owner" in
  [firmware/csupm/README.md](../firmware/csupm/README.md)).
- `PERFORMANCE_LEVEL_GET` of a cluster or the DSU whose level was not set
  by a message (the OS's fast channel, or the DPM's cap: PROCHOT, a skin
  temperature controller): ours answers the level that runs, CIX's the
  level its DVFS last had by message, which is not the PLL's.
- (capped) A level set by message while the DPM's cap comes down: ours
  follows each step of the cap, voltage with it; CIX's stays at the
  message's level until the cap is below `sky1soc_perf`'s own record. Both
  end at the cap.
- (capped) Under the cap, CIX's DVFS still has the last message's level:
  the next message for the domain makes it "set" the level already
  running and notify it (no PLL or voltage write). Ours notifies the
  changes that happen.
- (capped) `dpm_main_tab.opp_grant`'s DSU entry, set and cleared again
  every DPM period, sampled at a phase's end (what is exported is the
  same).
- CIX's image asserts on `SYSTEM_POWER_STATE_SET` with a state that does
  not exist (0x80000000); ours answers `INVALID_PARAMETERS`.
- CIX's PVT warning handler (`dev_pvt_ht_warning_isr`, through
  `convert_temperature_to_data`) computes in floating point: taken while a
  task without coprocessor state runs (the idle task), FreeRTOS's
  coprocessor exception handler panics. CIX leaves that line masked, so it
  does not happen as shipped. Ours takes the line.
- CIX's fan message 0x802 refuses a user table of nine points but has
  already set the table's count to 0: with the fan in the user's mode, its
  fans' loop asserts at the next pass (`adjust_fan_rpm`). Ours refuses and
  keeps the table. (Boards whose fans the PM drives.)
- Fan timing between questions (fan boards): the loop runs on its own
  period; the fans are compared by time in the `fans: ...` phases instead.
- The die arbiter's PID term, sampled at a phase's end.

Results now: in all seven configurations no unexplained difference
(about 705 to 730 expected ones, in six to ten kinds, per standard
configuration; 827 in ten kinds in `options`); `pm-faults`: none failed;
`pm-vf --matrix`: 78 cases, no difference; a full run under `--audit`
reports nothing.

## Looking into a finding

`pm-test -v` prints every difference in full and the answers that agree
too (without it, long lists are cut); `--only TEXT`
runs just the `--full` phases named so. `pm-script` then boots one image
to idle and runs commands on it: SCMI commands and PM messages (`scmi`,
`scmi-len`, `msg`, `post`, `send`), time (`ms`, `run`), the model
(`temp`, `pvt`, `pvtcode`, `unmask`, `raw`, `fc`, `poke`), stops (`stop SYMBOL`,
`until-arg`, `until-sp`, `watch`, `count SYMBOL MS`) and what to look at (`regs`, `win`, `bt`,
`trail`, `mem`, `str`, `writes`, `wrote`, `i2c`, `console`, `cpu`); it
takes `--board`, `--set` and `--ddr-dfs` as `pm-test` does. `save
LO HI FILE` writes memory for `scripts/open/dwarf_dump.py --ram LO:FILE`
(tables CIX's image builds at run time). The full list is in
[script.rs](src/pm/script.rs).

The audit (`pm-test --audit`, or `audit` in a script) checks the
interpreter itself: at every windowed return the caller's registers are
what they were at the call, and after every interrupt the interrupted
code's are. A full run under it reports nothing.

## Coverage

`--cov FILE` (on `pm-test`, `pm-faults`, `pm-vf`) adds the address ranges
each run executed to FILE; `sky1emu cov FILE ELF...` reports, per image,
how much of each function ran and the functions no test reached: code
nothing compares. `--partial` lists the functions partly run, `--all`
every function. All suites together run 74.0% of the open image's code
bytes; 182 of its 1339 functions never run (292 before `--full`). Most of
those are never run in CIX's image either under the same tests: code no
agent can reach as configured, or scenarios not written yet.

## What CIX's image does wrong

Found by the comparison, each confirmed in its disassembly (and kept as
expected differences, above):

- any SCMI message of a protocol it has not got kills it (its response
  logger indexes a table of six protocols);
- `SYSTEM_POWER_STATE_SET` with a state that does not exist asserts;
- its PVT warning handler uses the FPU in interrupt context and panics
  when taken in the idle task; shipped with the line masked;
- fan message 0x802 empties the user's fan table when it refuses one, and
  the fans' loop then asserts;
- under a cap of the DPM's, `PERFORMANCE_LEVEL_GET` answers a level that
  does not run (its own level-get fast channel and PLL say otherwise), and
  the next message for the domain brings a notification of a level that
  already runs;
- while the SoC idle switch has the core at 200 MHz its FreeRTOS tick
  keeps its cycle count, so the tick and what counts in ticks (its DPM
  ticker, the heartbeat, timers) run at half speed.

What once looked like CIX's image dying under back-to-back requests, or
asserting on a fan that does not exist, was the interpreter's fault.

The same suites found ten faults in the open firmware, fixed since (see
[firmware/csupm/README.md](../firmware/csupm/README.md)).

## Lessons

Faults of the interpreter and the harness, each of which once looked like
a fault of one image:

- A FLIX bundle (CIX's image has 8-byte bundles of several operations) is
  one instruction: when its `retw` takes a window underflow, nothing of
  the bundle may have happened yet. The Python interpreter, and the first
  Rust port, applied the other slots, took the underflow, and ran the
  bundle again. CIX's `strlen` ends in `{ retw ; sub a2, a3, a2 }`:
  whenever an interrupt had spilled its caller's window it returned
  garbage. The audit found it.
- A timer compare that matches at the very cycle the core executes
  `WAITI` was missed: the alarm then waited for CCOUNT to wrap, about
  10.7 s.
- A sleep that passed a compare whose interrupt was masked did not latch
  it.
- An agent's wait for an answer let a sleeping PM jump to its next timer,
  and billed that time to the answer.
- Time was taken to be the cycle count: a core at 200 MHz ran its timers
  as fast as at 400 MHz.

## Layout

| File | What |
|---|---|
| [src/main.rs](src/main.rs) | the commands |
| [src/util.rs](src/util.rs) | SHA-256, the repository's root, CIX's debug builds, arguments, a sequence diff |
| [src/elf.rs](src/elf.rs) | 32-bit ELF reader: segments, sections, symbols |
| [src/cov.rs](src/cov.rs) | coverage files and the report |
| [src/xtensa/mod.rs](src/xtensa/mod.rs) | the interpreter for `pm_core_v1`: windows, loops, FPU, exceptions, interrupts, timers |
| [src/xtensa/decode.rs](src/xtensa/decode.rs) | 24-bit, density and bundle-slot decoding |
| [src/xtensa/flix.rs](src/xtensa/flix.rs) | FLIX bundle decoder (formats documented in `scripts/open/xtensa_flix.py`) |
| [src/pm/soc.rs](src/pm/soc.rs) | the SoC as the PM sees it: I2C and its slaves, UART, PLLs, mailboxes, interrupt aggregator, SE side, S5, power policy, DDR and its frequency switch, watchdog, GPIO, counters, PVT, the core's clock divider and simulated time; `sky1emu pm` |
| [src/pm/test.rs](src/pm/test.rs) | `pm-test`: boot, `--scmi`, `--power`, `--services`, `--runtime`; the comparison, `POLLED`, `CFG_WORDS` |
| [src/pm/full.rs](src/pm/full.rs) | `--full` (`STEPS`), `EXPECTED`, `PERIODIC` |
| [src/pm/faults.rs](src/pm/faults.rs) | `pm-faults` |
| [src/pm/vf.rs](src/pm/vf.rs) | `pm-vf` |
| [src/pm/script.rs](src/pm/script.rs) | `pm-script` |
| [test-pm.sh](test-pm.sh) | every suite on every configuration, in parallel |

Each emulator besides the CSU_PM's is a module behind a Cargo feature (so
that one being worked on cannot stop the others' builds:
`--no-default-features --features ec` builds one alone); all are in
`default`. Each adds its own commands to `sky1emu` and its section to
this file:

| Feature | Module | What |
|---|---|---|
| `ec` | [src/ec](src/ec/mod.rs) | the Orion O6's EC (IT5570, 8051) |
| `se` | [src/se](src/se/mod.rs) | the SE (turns on `arm`) |
| `sfh` | [src/sfh](src/sfh/mod.rs) | the sensor fusion hub (turns on `arm`) |
| `arm` | [src/arm](src/arm/mod.rs) | an ARMv8-M interpreter for the SE and the SFH |
| `a64` | [src/a64](src/a64/mod.rs) | AArch64: BL2 and BL31 |

CI builds the crate and runs its unit tests; the comparisons need CIX's
image, which CI has not got.

## EC (Orion O6)

Feature `ec`, [src/ec](src/ec/mod.rs): the Orion O6's embedded controller
(ITE IT5570E, an 8051), the open firmware ([firmware/ec](../firmware/ec))
against Radxa's EC V1.07
(`edk2-project/Platform/Radxa/Orion/O6/Firmwares/ec_fw.bin`). The SPI
flash image (`output/orion-o6/RELEASE/cix_flash_all.bin`, `--flash`) is
where the update check takes the EC image from.

```sh
cargo build --release --manifest-path emu/Cargo.toml
E=emu/target/release/sky1emu
$E ec-test --open firmware/ec/build/ec_fw.bin              # everything, ~35 s
$E ec-test --open IMG -v s3_button host_bad                # some scenarios, every difference
$E ec-test --open IMG --port                               # the Python test_ec.py, exactly
$E ec-test --open IMG --cov ec.cov && $E ec-cov ec.cov firmware/ec/build --partial --lines
$E ec IMG 8000 output/orion-o6/RELEASE/cix_flash_all.bin  # one image, 8 s, every event
$E ec-probe IMG s3_button --x 0x9247 --pc 0xCB71 --stack "i2c C" --from 7000
$E ec-ntc-table VENDOR.bin firmware/ec/src/ntc_table.h
```

| Command | What it does |
|---|---|
| `ec IMG [MS] [SPI]` | run an image on the O6 model; print every event (pins, I2C, host, UART, e-flash) and where it stopped |
| `ec-test [--open IMG] [--vendor IMG] [--flash SPI] [-v] [--port] [--dump DIR] [--cov FILE] [scenario...]` | the comparison: both images through every scenario, then each installed over the other as UEFI does (3E19) |
| `ec-probe IMG SCENARIO [--x ADDR]... [--pc ADDR]... [--stack TEXT] [--from MS]` | one image through one scenario: its events, the writes to external RAM addresses, the registers at code addresses, the stack when an event's text matches |
| `ec-cov FILE BUILD_DIR [--partial] [--all] [--lines]` | the open image's functions (from the SDCC listings `*.rst` and `ec.map`; there is no ELF) the runs recorded in FILE executed; `--lines`: the C lines of the instructions never run |
| `ec-ntc-table VENDOR.bin OUT.h` | firmware/ec's NTC table: the vendor's own conversion routine (bank 1 0xDD46) run for every ADC code |

**The port.** `cpu.rs` (the 8051), `chip.rs` (the IT5570: timers, idle,
the interrupt and wake-up controllers, GPIO, UART1, the SMBus channels as
masters and the slave the host talks to, the SMFI follow mode to the SPI
flash and the e-flash, scratch ROM, the watchdog's mirror reset),
`board.rs` (rails and their power goods, the SoC's sleep signals,
CSU_PM, the two RTS5453H, NTCs, fan and tachometer, the pins' nets) and
`test.rs` (the 16 scenarios of `test_ec.py`, the update check) are the
Python emulator line for line; `seqmatch.rs` is Python's
`difflib.SequenceMatcher`, so that the counts are the same. With
`--port`, `ec-test` printed what `test_ec.py` printed, byte for byte, and
`--dump DIR` wrote every event and every register write (with its cycle)
of all 16 scenarios on both images exactly as the Python model did
(checked 2026-11-12 on firmware/ec at 9bad500; out/checkpoints/ec-rs.md).
It runs the suite in 6 s where PyPy took a minute.

**What the port did not compare** (`gaps.rs`, 45 more scenarios, with
the extended model: `--port` turns it off): suspend to RAM and the wakes
(button, the SoC itself), shutdown from S3, a long S3; AC lost in S0, S3
and standby (the EC unpowered: its pins float, then a power-on reset);
the always-on, S0 and S3 power goods never coming or lost; the SoC's
THERMTRIP; a fan that does not turn; NTCs open and shorted; CSU_PM and
the PD controllers NACKing or their buses hung (`SmbusMaster::hang`); the
PD controllers' interrupt lines; an e-flash that takes no write; an NTC
trip and the external watchdog it arms (EWDCNTLLR starts it, 1.024 kHz or
32.768 kHz by ETWCFG bit 4, EWDKEYR 0x5C restarts it); every fan mode and
back; the button in SD, held in G3 and standby; every command id of the
vendor's table and its neighbours in standby, S0 and S3; bad checksums,
versions, prefixes, lengths, a request cut short, bytes read past an
answer; commands 1 to 10 ms apart; runs of minutes (the open firmware's
16-bit millisecond count wraps); the 0x25 duty for every percent; target
speeds out of reach. CSU_PM answers as both PM firmwares do (`sky1emu
pm-test --full`, "I2C slave"), and only while the SoC's always-on part is
powered.

**How it compares.** What the board sees: pin changes, I2C transfers a
device answered (the vendor's probes of parts the O6 lacks are NACKed and
left out), host answers (bytes on the wire), e-flash commands, the
watchdog's registers, the fan's duty as it changes, the LED (on, off,
dim). In the extended mode each part is a stream of its own, in order:
the power sequence and the e-flash, the thermal pass (CSU_PM and the fan),
the PD bus, the host interface, the LED; events the table below covers
for both sides are one more. Times must agree within a tolerance (0.9 s
for the sequencer and pins, 1.5 s for the PD bus and the host, 2.5 s for
what the vendor does in its ~2 s hook, 3 s for the LED, 4 s for the
thermal pass); after a reset in both images the first 2 s are timed from
it. The host sends its requests at fixed times (50 ms apart, the GPIO
commands 250 ms) so both images get each at the same moment. A run ends
with "N differences, M expected in K kinds"; `EXPECTED` in
[gaps.rs](src/ec/gaps.rs) has each kind, matched by scenario, side and
the event's text, and [firmware/ec/differences.md](../firmware/ec/differences.md)
the evidence. Today: 0 differences; every function of the open image runs
(99 % of its code bytes).

**Looking into a finding.** `ec-probe` shows one image's events with
`--x ADDR` (writes to an external RAM address: the vendor's state is at
0x9247, the fan mode 0x93A8, the applied fan state 0x93E4, the pass
count 0x9237; the open image's are in `ec.map`), `--pc ADDR` (the
registers when a code address runs, any bank) and `--stack TEXT` (the
stack when an event whose text contains TEXT is logged: return addresses
are pairs low, high; Keil's bank switches go through 0x2Axx).
`scripts/open/i8051.py` disassembles (`decode(code, pc, base)`).

| File | What |
|---|---|
| [src/ec/mod.rs](src/ec/mod.rs) | the commands |
| [src/ec/cpu.rs](src/ec/cpu.rs) | the 8051 interpreter, bank switching by P1, cycle table |
| [src/ec/chip.rs](src/ec/chip.rs) | the IT5570, the event queue, the run loop, AC loss |
| [src/ec/board.rs](src/ec/board.rs) | the O6 around the EC, the pins' nets, the faults to inject |
| [src/ec/test.rs](src/ec/test.rs) | the port's scenarios, the host, the comparison, `ec-probe` |
| [src/ec/gaps.rs](src/ec/gaps.rs) | the scenarios the port left out, `EXPECTED` |
| [src/ec/seqmatch.rs](src/ec/seqmatch.rs) | `difflib.SequenceMatcher` |
| [src/ec/cov.rs](src/ec/cov.rs) | `ec-cov`: functions from the SDCC listings |
| [src/ec/ntc.rs](src/ec/ntc.rs) | `ec-ntc-table` |

## BL2 and BL31 (AArch64)

Feature `a64`, [src/a64](src/a64/mod.rs): the two TF-A stages built from
`arm-trusted-firmware` and its patch queue, against the vendor's, from
BL2's entry to the jump to UEFI and then every SMC. There was no emulator
for these stages before; what the comparison found is in
[docs/differences-bl2-bl31.md](../docs/differences-bl2-bl31.md).

```sh
emu/build-open.sh bl2 bl31                    # the open BL2 and BL31 (release, in the container) -> out/emu/a64
cargo build --release --manifest-path emu/Cargo.toml
E=emu/target/release/sky1emu
$E a64-test                               # everything: 0 differences, N expected in K kinds
$E a64-test bl31 --only 'SUSPEND' -v      # some phases, every item and answer
emu/test-a64.sh --cov --o6                # self-check, audit, both vendor builds, coverage
```

**Images.** The open ones are `bl2.elf` and `bl31.elf` as
`scripts/open/trusted.py` builds them (`--bl2`, `--bl31` take others).
The vendor's are taken out of its bootloaders: BL2 from bootloader1's
"CIXBTFF!" container, BL31 from bootloader2's FIP. The default is CIX's
own build (`edk2-non-osi/.../PackageTool/Firmwares`, TF-A
`v2.7-33527c31abbe` of 2026-07-15), which the patches were written from;
`--cix-bl1 FILE --cix-fip FILE` take another vendor's, and the table of
expected differences has a part for Radxa's Orion O6 bootloaders (a month
older, `ed9772217e49`). Both sides boot the same flash image
(`dist/orion-o6_2026Q3_open.bin`, `--flash`): one whose certificates are
all under one root, which the open BL2 needs. `out/emu/a64-re.sh DIR`
names a vendor's functions (`funcmatch.py`, Ghidra) for the messages and
for reading; without it the vendor's code is addresses.

| Command | What it does |
|---|---|
| `a64-test [bl2] [bl31] [--flash F] [--dram MB] [--core N] [--only TEXT] [--audit] [--cov FILE] [-v]` | the comparison |
| `a64 IMAGE [--base ADDR] [--names SYMS] [--flash F] [--secure] [--until ADDR] [--calls FN --depth N] [--io] [--audit]` | run one stage from its entry: console, where it stopped, registers the model did not know; `--calls` prints the calls made inside a function with arguments and results |
| `a64-selfcheck IMAGE... [--rounds N]` | the interpreter against the host's CPU |
| `a64-cov FILE IMAGE... [--names SYMS --as NAME] [--partial] [--all] [--summary]` | what the runs recorded with `--cov FILE` executed (cov.rs's own report reads 32-bit ELF only) |

`SKY1EMU_WATCH=0xADDR` before any of them prints who reads and writes a
device register (image, function, caller): how most findings were traced.

**The interpreter** ([cpu.rs](src/a64/cpu.rs)) has the integer
instruction set (all four images are built without FP and SIMD), the
system registers the images name, and the EL3 stage-1 translation walked
for real once the firmware turns the MMU on: an address the firmware did
not map, or a write to what it mapped read-only, is a fault, as on the
chip. An encoding or a system register it has not got stops the run. A
fault stops the run too and is reported with its function; it is not
delivered to the firmware's vector. Lower exception levels do not run:
an ERET below EL3 stops, and the harness plays them.

Two checks of the interpreter itself, since there is no second emulator
to compare it with:

- `a64-selfcheck`: on an AArch64 host, every distinct instruction word of
  the images that user mode may execute (data processing, loads and
  stores, branches) is run on the CPU and by the interpreter from the
  same random registers, flags and memory, and everything it can change
  is compared ([native.rs](src/a64/native.rs)). All four images: no
  difference (8151 distinct words of the open images, 11939 of CIX's
  with their data taken for code; its first run found an unallocated
  load-pair form the decoder accepted). System instructions and branches
  through a register are not run this way.
- `--audit`: at every return to where a call left, with the call's stack
  pointer, x19 to x29 must be what they were. Calls on SP_EL3 are not
  followed (the vectors' assembly that saves and loads the lower level's
  registers; TF-A's C code runs on SP_EL0). A full run: 17.8 million
  returns, none flagged.

**The model** ([soc.rs](src/a64/soc.rs)) is the Sky1 as TF-A drives it,
inferred from the drivers: a register with no behaviour reads back what
was written. With behaviour: the Cadence xSPI controller and a SPI NOR
(the flash image; erase and program work), the UART, the system counter,
four TZC-400, the GICv3 (redistributor frames assigned through
`GICD_RDOFFR` and `GICR_MPIDR` as CIX's code programs them, the CPU
interface's pending, running priority and end of interrupt), the cores'
power policy units and reset vectors, the AP's mailbox to the PM with an
SCMI responder, its mailbox to the SE, and what the SE leaves in DRAM for
BL2 (the key block at 0x801ff000, the DRAM description at 0x83c00000, its
harvest word). Cores 0 to 3 are Cortex-A520, 4 to 11 Cortex-A720, with
the identification registers of the cores' manuals; the SE's default boot
core is 10.

**Time** is simulated nanoseconds and not an instruction count: it passes
a microsecond whenever the firmware reads the counter (so delays end and
time-outs expire) and when the harness lets it (a core asleep, a
suspend); before every question both machines' clocks are set to the
same millisecond. What the firmware derives from time (the boot record,
the counter rewritten after a suspend) is then compared exactly, and
differs only if the two read the clock a different number of times.

**What is compared**, item by item ([test.rs](src/a64/test.rs)): every
device register's writes in order, the SPI transactions, the messages to
the PM and the SE, core power requests, SGIs, interrupts taken and ended,
the console and RAM log (the version and build time apart), the registers
and system registers each next stage is entered with, the images in DRAM
by hash, every other DRAM page a stage changed; per SMC the registers the
caller gets back (x0 to x3, any other that changed, where it finds
itself) and what OP-TEE was entered with on the way.

- BL2 ([bl2.rs](src/a64/bl2.rs)), 34 configurations: DRAM of 4 to
  64 GiB, DDR channel masks, the TZC debug regions, boot cores, flashes
  in and out of bootloader1's table (single and quad reads), no firmware
  header, the boot ROM's second copy, a flash controller that never gets
  idle, a byte changed in each entry of both FIPs, the vendor's layout.
- BL31 ([bl31.rs](src/a64/bl31.rs)): the cold boot behind each side's
  own BL2 (the stage under test put in BL31's place at the jump), then
  from copies of the booted machines: SMCCC and PSCI queries, every SiP
  call with its arguments, every owning entity and function number, calls
  for OP-TEE and OP-TEE's own calls, every core on, off and on again,
  CPU_SUSPEND (standby and power down, with the AMU's virtual offsets in
  use), SYSTEM_SUSPEND and resume (also with the audio domain's resume
  pending), SYSTEM_OFF and RESET, SDEI, interrupts and exceptions taken
  to EL3, the SPI NOR and SCMI by SiP call, the power domains with memory
  repair, the PDC's wake-up sources, deferred errors; and some 35 cold
  boots that differ from the hand-over on (TrustZone configuration files
  of every kind, no SE, no PM, cores the eFuses leave out, other DRAM,
  boot cores and flashes), each followed by a few questions.

A question that kills an image (a fault, a panic, a hang, no return) puts
both machines back as they were before it. Expected differences are
`EXPECTED` in [expected.rs](src/a64/expected.rs), matched by phase, item
and both values; `O6_JUNE` there is what Radxa's older build lacks of
CIX's. Results now: 121 phases, 0 differences, 1656 expected in 16 kinds
against CIX's build (under three minutes); 0 differences, 1725 expected
in 19 kinds against the Orion O6's with its part of the table.

**Coverage.** All phases together run 81 % of the open BL31's code bytes
(69 of its 653 functions never run) and 51 % of the open BL2's (152 of
450 never run, 103 of them Mbed TLS functions the chain of trust does not
use); what is left and why is in
[docs/differences-bl2-bl31.md](../docs/differences-bl2-bl31.md#what-no-test-reaches).

Not run: CIX's BL2 with secure boot fused on. It verifies with a
hardware crypto engine (0x05050000) that the model has not got, so its
authenticating path stops at the first certificate; with the fuses of the
boards sold it authenticates nothing, which is the path compared.

| File | What |
|---|---|
| [src/a64/mod.rs](src/a64/mod.rs) | the commands, `a64`, `a64-cov` |
| [src/a64/cpu.rs](src/a64/cpu.rs) | the AArch64 interpreter, system registers, translation, the audit |
| [src/a64/native.rs](src/a64/native.rs) | `a64-selfcheck`: instruction words on the host's CPU |
| [src/a64/elf64.rs](src/a64/elf64.rs) | ELF64 and raw images, names |
| [src/a64/images.rs](src/a64/images.rs) | BL2 out of bootloader1, BL31 out of a FIP, the flash's header |
| [src/a64/soc.rs](src/a64/soc.rs) | the Sky1 as TF-A sees it, simulated time |
| [src/a64/machine.rs](src/a64/machine.rs) | a machine with an image; the configuration a board gives |
| [src/a64/test.rs](src/a64/test.rs) | `a64-test`: the harness (the lower levels, OP-TEE), items, the report |
| [src/a64/bl2.rs](src/a64/bl2.rs), [bl31.rs](src/a64/bl31.rs) | the scenarios |
| [src/a64/expected.rs](src/a64/expected.rs) | `EXPECTED`, `O6_JUNE` |
| [test-a64.sh](test-a64.sh) | all of it in one go |

## SE and SFH (Cortex-M33)

Features `se` and `sfh` (each turns on `arm`): [src/arm](src/arm/mod.rs),
an interpreter for ARMv8-M Thumb-2; [src/se](src/se/mod.rs), the open SE
firmware ([firmware/se](../firmware/se)) against CIX's debug build of its
own (`debug/se_fw/se_fw.elf`, pinned in
[manifests/cix-debug-firmware.json](../manifests/cix-debug-firmware.json)
and read from `blobs-cache/` like the PM's); [src/sfh](src/sfh/mod.rs), the
open sensor fusion hub firmware ([firmware/sfh](../firmware/sfh)) against
CIX's `sfh_fw.bin`
(`edk2-non-osi/Platform/CIX/Sky1/PackageTool/Firmwares`, no symbols). What
the comparisons found is in
[firmware/se/differences.md](../firmware/se/differences.md) and
[firmware/sfh/differences.md](../firmware/sfh/differences.md).

```sh
emu/build-open.sh se sfh                      # out/emu/se/se_fw.elf, out/emu/sfh/sfh_fw.bin
cargo build --release --manifest-path emu/Cargo.toml
E=emu/target/release/sky1emu
emu/test-se.sh out/emu/se/se_fw.elf out/emu/sfh/sfh_fw.bin --cov     # everything, 2 min on eight cores
$E se-test out/emu/se/se_fw.elf dist/orion-o6_2026Q3_open.bin --services --suspend
$E se-test OPEN.elf FLASH.bin --secure --only 'CSU_PM altered' -v --uart cix:'CSU_PM altered'
$E se-test OPEN.elf dist/orion-o6n_2026Q3.bin --ddr
$E sfh-test out/emu/sfh/sfh_fw.bin -v
$E arm-check out/emu/se/se_fw.elf             # every instruction of an image decodes
```

| Command | What it does |
|---|---|
| `se-test OPEN.elf FLASH.bin [suites] [options]` | the open SE and CIX's on a flash image (a build in `dist/`), from reset to the idle loop, then the suites asked for |
| `se-test OPEN.elf FLASH.bin --pyref [--services] [--suspend] [--ddr] [--secure] [--keys] [--dump PREFIX]` | the comparison exactly as the Python test made it (below) |
| `se ELF [--flash IMAGE] [--trace] [--stub NAME=R0] [--stop NAME] [--max N]` | one SE image on the model: its bus writes and where it stopped |
| `sfh-test OPEN.bin [CIX.bin] [--elf OPEN.elf] [--only TEXT] [--cov FILE] [-v] [--pyref]` | the open SFH image and CIX's through 162 phases |
| `arm-check ELF...` | decode every instruction of the images' functions; anything the interpreter does not know is an error |

`se-test` suites: `--services` (every request `0x82000001`…`20`, about
130 questions on each of the two mailboxes: valid, boundary and invalid
arguments), `--suspend` (five suspends: the sensor hub started, left,
stopped, every wake-up source, a PM that says nothing; the hub image the
SE loaded is started in the SFH model), `--crash` (exception area, hang
record, the three watchdogs' interrupts, the SE watchdog not fed, every
fault vector, the RCSU violation interrupts, a bus fault in a service),
`--secure` (20 cases with the secure-boot fuses set: altered images,
headers, signatures, digests, keys, key hashes, life cycles), `--inputs`
(boot reasons, life cycles, flash parts, clocks, 24 damaged pieces of the
flash image), `--failures` (PLLs that never lock, a PM or sensor hub that
does not answer or answers late or wrong), `--paths` (what no board's
image takes: the flash behind the FCH's xSPI controller, trims and repair
data in the fuses, debug certificates, two DDR channels, the emulation
platform, MTE on; and the open firmware's SM2 check against the
standard's example), `--ddr-suite` (the DDR driver live: 19 feature
settings, 7 kinds of parts, 9 failures, every interrupt status bit, a
suspend; `--eye` adds the eye scan), `--full` (all of them). Options:
`--ddr` (the DDR driver runs on the model of the controllers; without it
its entry points are stubbed), `--life-cycle N`, `--keys` (device and
model keys fused), `--ddr-feature NAME=VALUE`, `--cix CIX.elf`,
`--only TEXT` (the phases whose name has TEXT; boots whose phases cannot
have it are skipped), `--uart TEXT` or `--uart cix:TEXT` (the UART log of
those phases), `--cov FILE`, `--audit`, `-v`.

[`test-se.sh`](test-se.sh) `OPEN_se_fw.elf OPEN_sfh_fw.bin [--cov] [--audit]`
runs all of it side by side: every suite on the O6's open flash image, the
boot with the DDR driver live on every board's image in `dist/`, life
cycles 5 and 7 with keys, `sfh-test` and `arm-check`. Logs go to
`out/emu/test/se-*.log`; the exit status is 0 when nothing differs.

**Method.** As for the PM ([above](#method)): both images run on one
model, driven by simulated time (the SE's core at 800 MHz over its
divider, SysTick from the 24 MHz reference or the core; the hub's core at
an assumed 100 MHz, its timers at 25 MHz), brought to the same instant
before each question; an image that a question kills is put back for the
next one, and what it ran still counts for coverage. A phase compares the
state at its end, every write outside the image's own memory in order
across all registers, the registers read (which, how often, by which
function, in which order first), the answers and messages, the events
(interrupts, resets, postcodes), DRAM, the PM's and the hub's SRAM by
content, and the flash. `EXPECTED` in [src/se/test.rs](src/se/test.rs) and
[src/sfh/test.rs](src/sfh/test.rs) lists the differences that stay, each
with its reason and matched to the phase, the register and both values;
anything else fails the run. CIX's crypto library (the engine's hash and
public-key units are not modelled) is replaced at `c_mbedtls_sha*`,
`c_mbedtls_hkdf` and `sec_rsa_verify` by the same functions on the host
([src/se/crypto.rs](src/se/crypto.rs)), and the monitor of its debug build
(`service_monitor_entry`: a prompt on the UART) by a return.

**The port's proof.** `--pyref` is the Python emulator and test line for
line: the interpreter stops at the block boundaries QEMU's translator
makes, hooks and stubs act where Unicorn's did, interrupts are calls on a
scratch stack. Before the Python files were removed their output was
recorded (PyPy, Unicorn 2.1.4) for 11 cases with every bus write of both
images (3.1 million writes in 23 traces) and for 12 DDR feature settings;
`se-test --pyref` and `sfh-test --pyref` reproduce all of it byte for
byte. The recordings are not in the repository (CIX's image is not
redistributable); the Python files are in the history up to the commit
that removed them. `test_se_ddr_fn.py` (each function of the DDR driver
called alone with captured arguments) was not ported: the driver now runs
whole with every feature, part and failure above, on both images.

**Coverage.** `test-se.sh --cov` writes `out/emu/test/se-coverage.txt`:
the runs execute 80.8 % of the open SE's code bytes; 22 of its 657
functions never run, none of them reachable in this firmware
([why](../firmware/se/differences.md#what-no-test-reaches)). Of the open
SFH every function runs but libgcc's division-by-zero stub. Of CIX's SE
image the same runs execute 37 %: its monitor, its crypto library and
most of its clock and power framework are stubbed or never asked.

| File | What |
|---|---|
| [src/arm/mod.rs](src/arm/mod.rs) | the core: exceptions on the stack, NVIC, SysTick, WFI, faults and lockup, stack limits, memory with remapped windows, stops, stubs, hooks, the audit, coverage |
| [src/arm/decode.rs](src/arm/decode.rs) | Thumb-2 decoding (an unknown encoding is an error) |
| [src/se/soc.rs](src/se/soc.rs) | the Sky1 as the SE sees it: boot ROM hand-over, QSPI flash, the FCH's xSPI controller, PLLs, power controllers, mailboxes, watchdog, timers, PM and hub handshakes, the DDR controllers, PHYs and PI (`DdrModel`), simulated time (`Timed`) |
| [src/se/timed.rs](src/se/timed.rs) | `Sim`, `Pair`, phases, the comparison and the report |
| [src/se/test.rs](src/se/test.rs) | the suites, `EXPECTED` |
| [src/se/crypto.rs](src/se/crypto.rs) | SHA-2, SM3, HKDF, RSA-PSS on the host, for CIX's image |
| [src/se/emu.rs](src/se/emu.rs), [pyref.rs](src/se/pyref.rs) | the Python emulator's way of driving an image, and its test |
| [src/sfh/test.rs](src/sfh/test.rs) | the hub's model (timers, mailboxes, control block), the phases, `EXPECTED` |
| [src/sfh/pyref.rs](src/sfh/pyref.rs) | the Python test of the hub |
| [test-se.sh](test-se.sh) | all of it in one go |
