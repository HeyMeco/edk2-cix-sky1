# The open CSU_PM and CIX's: differences

How the open CSU_PM image (`csupm.xbin`, built from this directory) differs
from CIX's (`csupm_sky1`, CSU_PM 00.12.001, the image in CIX's 2026Q3
bootloader1). Written for whoever decides which of the two to flash, and
for whoever writes the OS side of SCMI and of CIX's PM messages. Each item
says what CIX's does, what ours does, why, and where it is shown: a test
phase of `sky1emu pm-test` (names as in `STEPS` in
[emu/src/pm/full.rs](../../emu/src/pm/full.rs)), a reason of the table of
expected differences (`EXPECTED`, same file), or a source file.

## 1. Status

**The open CSU_PM has never run on hardware.** It is built from source and
checked only in the emulator. `build.sh --open` puts it in bootloader1;
`--vendor-pm` keeps CIX's.

Both images run on the same model of the SoC (written from what CIX's
image does with each device, not from documentation) and are asked the
same things in lock-step, on simulated time. Every register write, I2C
transfer, SE handshake word, doorbell to the AP and answer is compared
phase by phase; a difference is a failure unless it is in `EXPECTED` with
its reason. The method, the suites and the commands are in
[emu/README.md](../../emu/README.md).

So everything below is behaviour on that model. What the model cannot
show (real fans, PMIC read-back formats, whether the thermal interrupt
lines are live on a part, display timing) is untested.

## 2. What is the same

In seven configurations (none loaded; the O6's, O6N's, MS-R1's, Orange Pi
6's and Orange Pi 6 Plus's; and `options`, the O6N's with every option of
the BIOS's PM configuration block that no shipping board sets), every
phase gives the same register writes, I2C transfers and answers as CIX's,
outside the expected differences (about 705 to 730 per standard
configuration in six to ten kinds, 827 in ten kinds in `options`, all
listed in sections 4 and 5):

- boot: all 1329 register writes in order, the 33 I2C transfers (PMIC
  rails, boot DVFS levels), the SE handshake; in shared memory the RTT
  control block and the PCC tables byte for byte;
- SCMI on every channel: base (agents, permissions), power domains, system
  power, sensors and their trip points, the 273 clocks, performance on 12
  domains (levels and limits by message and by fast channel, the
  level-get fast channels, notifications), malformed messages and domains
  that do not exist, answered by polling and by interrupt;
- CIX's PM messages: every id on every channel, with their arguments (SMN
  access, performance streams, locked levels, cores, DSU and idle modes,
  fans, EDP voltages, PMIC events);
- PCC (ACPI CPPC) commands by the fast channel;
- suspend (by SCMI and by PM message 3), resume, a second suspend, shutdown,
  warm reset, DDR retention and the S3 boot script;
- run time: the DPM (performance streams, power model, arbiters, TDP and
  EDP limits) and its state, PROCHOT, the die at 100 C, a temperature
  sweep from 30 to 130 C and back;
- board services: the fans in every mode and on the automatic curve (also
  at the sensors' raw codes), PVT thresholds, PMIC faults, the board
  sensors, the I2C slave the EC reads, the watchdog, the SoC idle switch
  and the DDR's frequency switch;
- the V/F tables for 78 simulated parts (straps, fuses, ATE phases, part
  numbers, BIOS configurations, Phecda's PMICs, GB1's boost: `pm-vf
  --matrix`).

CIX's changes to SCP-firmware's modules are carried over, each listed in
the module's README ([scp/module](scp/module)): transport, `scmi_perf`
(TDP caps, fast-channel pass, plugins), `scmi_clock` (16-bit clock count,
version 0x20000), `scmi_system_power` (shutdown, reset and suspend go to
the PM's power sequence), `dvfs`, `psu`. The configuration tables in
[scp/config](scp/config) are CIX's, read from its image.

## 3. Structure: what is not CIX's code

None of this is visible to the AP except through the behaviour in section
4; it is what someone reading the two images finds different.

- SCP-firmware is upstream v2.11.0 (submodule) with CIX's changes
  re-applied per module, not CIX's fork. The `transport` and `cix_mbox`
  modules replace CIX's `ipc_arbiter` (README, Modules).
- FreeRTOS-Kernel V11.1.0 (submodule) with our Xtensa port
  ([rtos](rtos)), co-operative: SCP-firmware's loop, a PMIC worker, a
  board-sensor worker and idle ([src/rtos.c](src/rtos.c)). A task runs
  until it waits, so the code written for one event loop runs to
  completion between waits and needs no locks.
- CIX's timer, event and job framework is replaced by SCP-firmware alarms
  and events (`dpm` and `pm_svc` modules), with CIX's periods and order.
- Interrupt vectors only mask and note a line; the handlers run from the
  loop ([arch/vectors.S](arch/vectors.S),
  [scp/src/arch_interrupt.c](scp/src/arch_interrupt.c)).
- I2C: in a worker a transfer waits for the controller's interrupt
  (aggregator line 24) asleep; elsewhere it polls, as CIX's driver does.
  CIX's I2C state machine for tasks is not ported ([src/i2c.c](src/i2c.c)).
- `sky1soc_perf` is the only module that changes the CPU clusters and the
  DSU ("one owner",
  [scp/module/sky1soc_perf/src/mod_sky1soc_perf.c](scp/module/sky1soc_perf/src/mod_sky1soc_perf.c),
  [scp/module/scmi_perf/src/mod_scmi_perf.c](scp/module/scmi_perf/src/mod_scmi_perf.c)).
  In CIX's, SCMI messages, locks and the DPM's TDP changes set them
  through DVFS and the OS's fast channels through `sky1soc_perf`, each
  with its own record of the OPP.
- No code overlays (CIX's has twenty, 32 KB); no Cadence runtime (own libc
  in [lib](lib), GCC's libgcc). Built with Espressif's GCC 14.4 for the
  core description in [core](core), not with XtensaTools.
- The DPM's double-precision arithmetic is software IEEE double
  ([src/dpm/dfloat.c](src/dpm/dfloat.c)), bit-exact with CIX's FPU code;
  our build does not use the core's double-precision FPU.

## 4. Behaviour that differs on purpose

### SCMI

- **Clock protocol messages 8 to 10** (`CLOCK_NAME_GET`, the two rate
  notifications). CIX's answers them. Ours has not got them yet: it
  answers -4 (NOT_FOUND) to them and to `PROTOCOL_MESSAGE_ATTRIBUTES` for
  them. No clock's attributes offer either, so an OS that follows the
  attributes does not ask. Shown: `CLOCK_V2`; phases "SCMI message
  attributes, unknown messages and protocols", "SCMI clocks: every clock
  read"; [scp/module/scmi_clock/README](scp/module/scmi_clock/README).
- **The DSU's level by message.** CIX's DVFS programs the DSU's PLL, and
  leaves `sky1soc_perf` dividing a frequency the PLL no longer has. Ours
  changes the PLL's second post-divider, as a fast channel always did in
  both, so the DSU's clock is the PLL's rate over a whole number rather
  than the level's exact frequency. Why: one owner. Shown: `DSU_PLL`;
  phases "notifications: levels and limits by message", "SCMI
  performance: every domain and level, limits, notifications".
- **`PERFORMANCE_LEVEL_GET` of a cluster or the DSU whose level was not
  set by a message** (set by the OS's fast channel, or lowered by the
  DPM's cap: PROCHOT, a skin temperature controller). CIX's answers the
  level its DVFS last had by message, which its own level-get fast channel
  and PLL contradict. Ours answers the level that runs (`sky1soc_perf`'s).
  Shown: `LEVEL_GET`; phases "notifications: a cluster by its fast
  channel", "the DSU stepped with the clusters (PM message 0x803)",
  "interrupt lines by themselves: PVT safe, PROCHOT, GPIO" (PROCHOT's cap,
  in every configuration), and in `options` most phases that read a level
  (`capped` entries).
- **A message under the DPM's cap.** CIX's DVFS still has the level of the
  last message: the next message for the domain makes it "set" the level
  already running (no PLL or voltage write) and send a
  `PERFORMANCE_LEVEL_CHANGED` for it. Ours sends nothing, as nothing
  changed. Shown: `DVFS_CATCHES_UP` (`capped`); phases "notifications:
  levels and limits by message", "SCMI performance: ...".
- **A level set by message while the DPM's cap is still coming down** (it
  steps down over the first 132 ms after the PM idles, with the skin
  temperature controllers on). Ours follows each step of the cap, voltage
  with the clock. CIX's stays at the message's level until the cap is
  below `sky1soc_perf`'s own record: in the emulator big cluster 0 ran at
  1900 MHz for a DPM period under a 1500 MHz cap. Both end at the cap.
  Shown: `CAP_FOLLOWED` (`capped`); the `--services` phase "a level by
  message while the DPM's cap falls".
- **Whose notification.** A `PERFORMANCE_LEVEL_CHANGED` caused by the
  DPM's cap carries agent 0; only the pass that takes a message's request
  carries that message's agent. This is CIX's behaviour (its
  `sky1soc_perf` reports pass cookie 0), kept; the open firmware had it
  wrong until the cap was tested (`scmi_perf_owned_pass_planned`).
- **Unknown protocols and bad system power states** are answered with an
  error (NOT_SUPPORTED, INVALID_PARAMETERS); CIX's image dies or asserts
  (section 5).

### Time

- **The tick at a lower core clock.** The SoC idle switch (PM message
  0x803 bit 0, with the BIOS's thresholds) runs the core at 200 MHz while
  the SoC idles. CIX's FreeRTOS tick keeps its cycle count, so its tick
  and everything counted in ticks (its DPM ticker, the heartbeat, timers)
  run at half speed meanwhile. Ours keeps 1 ms: the tick is reloaded from
  `core_hz` ([rtos/port/port.c](rtos/port/port.c)), which
  `lcru_core_clock` sets with the divider, and the services' timers count
  microseconds from `arch_getTimeNowUs`, which turns cycles into time at
  the clock they were counted at ([src/clk/clk_port.c](src/clk/clk_port.c),
  [src/lcru.c](src/lcru.c)). Shown: with the emulator's model of the
  core's clock, by counting ticks while the SoC idles (CIX's: 500 ticks
  and 500 passes of the DPM's ticker in a second; ours: 1000 passes):

  ```sh
  sky1emu pm-script ELF --board o6n --ddr-dfs --set noc_idle=3 \
      --set noc_idle1=1000000 --set noc_idle2=2000000 --set noc_idle3=100000 \
      --set noc_idle4=2 'ms 4000; count xTaskIncrementTick 1000; count dpm_ticker 1000'
  ```

  The phase "the SoC idle switch and the DDR's frequency (PM message
  0x803)" keeps the SoC idle for about half a second at a time, in which
  what is written on a timer differs by less than a period, so no entry
  of `EXPECTED` is needed for it.
- **When the services start.** Ours starts the run-time services (fans,
  PVT polling, board sensors, watchdog) 1 ms after the ready handshake
  (`0x5a5c`); CIX's from its event worker once it first idles.
- **The heartbeat.** Ours creates its timer once; CIX's allocates another
  on every resume, so after one resume two timers toggle the LED
  ([src/board/dev_gpio.c](src/board/dev_gpio.c)).

### I2C and failing buses

What a working bus carries is CIX's, byte for byte. What differs is what
happens when a bus fails (README, Tasks and I2C; shown by `sky1emu
pm-faults`, which runs the open image alone):

- a transfer gives up after 10 ms (CIX's waits 600 ms);
- a time-out or lost arbitration resets the controller (CIX's master has
  no recovery); after three in a row the bus is down and its transfers
  fail at once for a back-off that doubles from 100 ms to 2 s;
- a PMIC voltage write stops after three rounds; CIX's retries for ever
  on a dead bus (its tries counter stops the writes only when it is
  exactly -1);
- the masters' interrupt sources on aggregator line 24 are enabled (CIX's
  masters poll; `pm-test` leaves that register out).

So a voltage write can fail where CIX's never returned, and ours makes sure
no frequency is raised on a voltage that was not set:

- `sky1soc_perf` ends its pass at a voltage answer that is an error; CIX's
  queue goes on and would raise the frequency on the old voltage;
- DVFS takes the new OPP as current when a lower voltage after the
  frequency fails, so the next raise sets its voltage first (CIX's and
  upstream's keep the old OPP);
- `dpm_edp_set_voltage` keeps the lower voltage as the rail's after a
  failed lowering (CIX's keeps the old), so that asking for the old one
  writes it;
- a rail whose write failed is marked unsure and rewritten before anything
  else changes ([src/board/pwr_rail.c](src/board/pwr_rail.c),
  [src/dpm/dvfs_cfg.c](src/dpm/dvfs_cfg.c));
- after a resume, the clusters whose rail could not be set go to their
  lowest OPP's frequency once DRAM is back
  ([src/hsm/subsys_hsm.c](src/hsm/subsys_hsm.c)); CIX's never gets past
  restoring the voltages on a dead bus (section 5);
- at boot `sky1soc_perf`'s first pass waits until DVFS has set (or given
  up on) the boot OPPs.

Around the power sequence the workers finish their jobs first
(`rtos_quiesce`); CIX's suspends its tasks.

### What the AP and the operator see besides SCMI

- **The version page** in shared memory holds the build's version (`open`
  unless `CSUPM_VERSION` is set) and git commit, in CIX's layout
  ([src/log/md_fwver.c](src/log/md_fwver.c)); CIX's says `00.12.001`.
- **The RAM log** holds the messages logged, in CIX's text records. CIX's
  line buffer fills early in boot and is never flushed, so its log holds
  bytes from past it. Both pages are each image's own in the comparison
  (`OWN_PAGES` in [emu/src/pm/test.rs](../../emu/src/pm/test.rs)).
- **The UART** (UART4 on the O6): ours prints `CSU_PM: open firmware` at
  start-up ([src/main.c](src/main.c)), and `CSU_PM: RTOS assert` if
  FreeRTOS asserts; CIX's keeps the UART quiet.
- **Thermal interrupt lines.** Lines 28 to 30 (PVT alarms, PROCHOT) stay
  masked as CIX leaves them. When they are unmasked, ours takes them; CIX's
  panics in its PVT warning handler (section 5). Built with `make
  CSUPM_THERMAL_IRQ=1`, ours enables the PVT warnings and PROCHOT itself
  ([src/board/board.c](src/board/board.c)); off by default, as on a board
  whose PROCHOT# is not driven the pin could read asserted and hold the
  CPUs at 500 MHz.

## 5. CIX's faults that the open one does not have

Each shown in the emulator and, where marked, confirmed in CIX's
disassembly. CIX's shipping configurations avoid some of them; they are
listed because an OS or a BIOS setting can reach them.

- **Any SCMI message of a protocol it has not got (0x16 and up) kills it**
  (confirmed): its response logger calls through a table of six
  protocols. Ours answers NOT_SUPPORTED. Shown: `UNKNOWN_PROTOCOL`; phase
  "SCMI message attributes, unknown messages and protocols".
- **`SYSTEM_POWER_STATE_SET` with a state that does not exist
  (0x80000000) asserts** (confirmed). Ours answers INVALID_PARAMETERS.
  Shown: `BAD_STATE`; phase "SCMI system power: get, notify, bad states".
- **Its PVT warning handler computes in floating point**
  (`dev_pvt_ht_warning_isr`, `convert_temperature_to_data`; confirmed):
  taken while a task without coprocessor state runs (the idle task),
  FreeRTOS's coprocessor exception handler panics; taken in one that has
  it, that task's registers are used. CIX leaves the line masked, so it
  does not happen as shipped. Shown: `ALARM_LINES`; phases "thermal sweep
  with the PVT warning line and PROCHOT unmasked", "... safe line unmasked
  too", and `--services` "PVT sensor 3 over its threshold (line 29
  unmasked)".
- **Fan message 0x802 with a user table of nine points** (confirmed) is
  refused, but the table's count has already been set to 0: with the fan
  in the user's mode the fans' loop asserts at its next pass
  (`adjust_fan_rpm`). Ours refuses and keeps the table. Boards whose fans
  the PM drives (O6N, MS-R1, Orange Pi 6). Shown: `BAD_TABLE`; phase "PM
  messages: arguments (SMN, streams, locked levels, cores, modes)".
- **Under a cap of the DPM's, `PERFORMANCE_LEVEL_GET` answers a level that
  does not run, and the next message brings a notification of a level
  that already runs** (confirmed; section 4).
- **A level set while the cap comes down runs above the cap** until the
  cap is below the level `sky1soc_perf` has on record (tens of
  milliseconds in the test; section 4).
- **Its tick and timers run at half speed while the SoC idles at 200 MHz**
  (section 4).
- **Two owners of the CPU clusters.** In CIX's, a fast channel after a
  message could raise a cluster's clock without its voltage: in the
  emulator big cluster 0 at level 6616 on 810 mV (README, one owner; ours
  had it too before `sky1soc_perf` became the one owner). `pm-faults`
  checks after each change that the rail has the voltage the PLL needs.
- **A voltage write reported failed but done.** CIX's MP5475 write reports
  failure after four NACKed tries even when the read-back shows the
  voltage set; with big cluster 0 lowered so and then raised by fast
  channel, CIX's image writes the PLL and no voltage, the regulator at
  810 mV (README, Tasks and I2C).
- **A resume on a dead PMIC bus never completes**: in the emulator, with
  big cluster 0's bus NACKing, CIX's image is still in
  `dpm_edp_restore_voltage` (in a critical section) after 4446 transfers
  (README, Tasks and I2C).
- **A second heartbeat timer on every resume** (section 4).

Two failures once put down to CIX's image were the emulator's: dying under
back-to-back requests (a FLIX bundle whose `retw` took a window underflow
ran twice) and asserting on a fan that does not exist. Neither is CIX's.

## 6. CIX's quirks kept on purpose

Where CIX's code is odd but harmless, or where changing it would change
what the AP sees, the open firmware does what CIX's does and says so in a
comment ("CIX's oddity kept", "as CIX's"). Those checked in the sources:

| Quirk | Where |
|---|---|
| the power-domain table makes ISP0 the parent of ISP0 and GPUTOP, so powering either on is DENIED, in both | [scp/config/config_power_domain.c](scp/config/config_power_domain.c) |
| the PMIC error notification to the AP is never sent (CIX's PM-to-AP send is empty); the AP reads faults with PM message 0x400 | README (Firmware) |
| the PVT alarm and PROCHOT lines left masked | [src/board/board.c](src/board/board.c) |
| the I2C slave keeps one register pointer for all slaves | [src/i2c.c](src/i2c.c) (`slave_reg`) |
| the DPM grants the DSU 0 (no limit); only the first 8 domains and the NPU's and VPU's are exported, the others stay 0; the phase-7 counter counts each domain | [src/dpm/cfg_dpm.c](src/dpm/cfg_dpm.c) (`dpm_export_opp`) |
| the skin arbiter's phase amends the slow socket-power arbiter's PID, not the skin's | [src/dpm/cfg_spt.c](src/dpm/cfg_spt.c) |
| a PID filter that would stick is moved one unit in the last place towards the input | [src/dpm/u_pid.c](src/dpm/u_pid.c) |
| voltage at a frequency past the second-to-last point is the last point's, not interpolated; an element's power above 80 W or below -1 mW is 12 mW | [src/dpm/cfg_pwr.c](src/dpm/cfg_pwr.c) |
| the little cluster reads GM1's minimum-voltage patch as its curve; a patch without a curve gives 0 | [src/dpm/cfg_fuse.c](src/dpm/cfg_fuse.c) (`find_raw_data`) |
| without a data board the GPU's power is left as it was | [src/dpm/sky1_pwr_databoard.c](src/dpm/sky1_pwr_databoard.c) |
| the busiest display port is found but stored only when there is none, so aligning with it never waits | [src/board/dpdpu.c](src/board/dpdpu.c) |
| when board sensor 1 is the TMP75 at 0x4d on controller 3 register 1, sensor 2 becomes the same device's register 0x23 | [src/board/board_sensor.c](src/board/board_sensor.c) |
| a performance stream's type must have one bit set, so only the MM, PCIe and SMN hubs' PMU streams can be asked for | [src/msg/msg_handlers.c](src/msg/msg_handlers.c) (`msg_stream_act`) |
| NI-700 PMU: with no counter left the control's enable and resets are written only when the PMU is off; with a port base the counter number gets it too | [src/perf/ni700_pmu.c](src/perf/ni700_pmu.c) |
| an unknown PCC command sets the error bit and is not marked complete | [src/pcc/pcc.c](src/pcc/pcc.c) |
| the SoC idle switch's configuration is read whether or not the BIOS's block is valid | [src/dpm/pm_config.c](src/dpm/pm_config.c) (`noc_idle_config`) |
| the level-get fast channel is written with the (capped) level when the message is taken, before the change runs | [scp/module/scmi_perf/README](scp/module/scmi_perf/README) |
| `scmi_perf` answers a level set before DVFS has done it, so a failure does not reach the SCMI caller | README (Tasks and I2C) |
| `SYSTEM_POWER_STATE_NOTIFY` answers NOT_SUPPORTED | [scp/module/scmi_system_power/README](scp/module/scmi_system_power/README) |

## 7. Left out, not ported

- The debug CLI and what only it reaches: CI-700 event collections
  ([src/perf/ci700_pmu.c](src/perf/ci700_pmu.c)) and the DFD trace, GPIO
  interrupt helpers, forced operating points, `pmic_stress`,
  `dump_dfs_gen`; log dumps and debug printing.
- The RTT debug console: the control block and its channels are laid out
  as CIX's, but nothing serves down channel 0 or writes up channel 0, and
  the performance streams' log packages (up channel 1) are not written;
  only the PM's log (up channel 2) is ([src/rtt/cfg_rtt.c](src/rtt/cfg_rtt.c),
  [src/perf/act_streams.c](src/perf/act_streams.c)).
- `CLOCK_NAME_GET` and the clock rate notifications (section 4).
- Aligning the DDR's frequency change to the displays' vertical blank
  (CIX's DP/DPU driver): ours changes it without waiting
  ([src/mclp/mclp_hwif.c](src/mclp/mclp_hwif.c)).
- The SCMI sensor notifications CIX's PVT code has a callback for: nothing
  in CIX's image signals it ([src/pvt.c](src/pvt.c)).
- CIX's I2C state machine for tasks, its timer, event and job framework,
  `ipc_arbiter`, its FreeRTOS port, code overlays and Cadence runtime
  (replaced: section 3). What is left of CIX's code untranslated is
  listed by `scripts/open/csupm_coverage.py OURS.elf --units`.

## 8. Known limits and open points

Of the open firmware, from reading the code (not observed in any run;
not fixed, narrow):

- `PERFORMANCE_LEVEL_GET` during a `sky1soc_perf` pass (a few ms of I2C)
  answers the level being set; a pass that is aborted restores it.
- The level-get fast channel, written at message time as CIX's is, keeps
  the asked level if the pass then makes no change to it (a cluster with
  no core online, a pass ended by a voltage error), while
  `PERFORMANCE_LEVEL_GET` answers the true one.
- A DSU message that arrives during a pass is taken by that pass's end, so
  the pass that applies it sends no notification for the DSU.

In both firmwares:

- Mailbox 1 holds one notification: a report made before the OS has taken
  the last one is lost (a pass that changes four clusters delivers the
  first only).
- `scmi_notification_notify` (upstream) sends to every agent subscribed
  for any domain, so a subscription for one domain also gets the others'
  changes.
- `PERFORMANCE_LIMITS_SET` and `PERFORMANCE_LIMITS_GET` answer -3
  (DENIED).
- The DPM's cap of the GPU is applied only when a level is set by
  message; the GPU's PLLs otherwise stay where they were.

Of the tests:

- Nothing has run on hardware (section 1). What the model has of devices
  is inferred from CIX's code; the DDR controllers' frequency-switch
  handshake in particular is inferred from what CIX's code waits for, and
  nothing documents it.
- All suites together run 74.0% of the open image's code bytes; 182 of
  its 1339 functions never run (292 before `--full`). Most of those never
  run in CIX's image either under the same tests: code no agent can reach
  as configured, or scenarios not written yet. `sky1emu cov` lists them.
- The options no shipping board sets (watchdog, heartbeat and EC GPIOs,
  the SoC idle switch's thresholds, the skin temperature controllers) are
  tested only on the O6N's configuration.
- The thermal interrupt lines' reset masks on a real part are unknown; the
  tests unmask them by hand.

## 9. Checking it yourself

On the host, with a Rust toolchain; CIX's image is fetched into
`blobs-cache/` by `scripts/open/cix_debug.py` when it is not there:

```sh
make -C firmware/csupm O=/tmp/csupm all check        # in the build container
cargo build --release --manifest-path emu/Cargo.toml
emu/test-pm.sh /tmp/csupm/csupm.elf --cov             # everything, seven configurations
E=emu/target/release/sky1emu
$E pm-test /tmp/csupm/csupm.elf --full --board o6n -v --only 'by its fast channel'
$E pm-script /tmp/csupm/csupm.elf --board o6n 'scmi 0 0x13 7 3 6931; ms 5; scmi 0 0x13 8 3'
```

`test-pm.sh` exits 0 when nothing differs outside the expected ones and
prints the end of each run's log; the logs are in `out/emu/test/`. The
expected differences and their reasons are `EXPECTED` in
[emu/src/pm/full.rs](../../emu/src/pm/full.rs).
