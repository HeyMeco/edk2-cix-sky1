# How the open Orion O6 EC firmware differs from Radxa's

Every way the open image (`firmware/ec`, `ec_fw.bin`) differs from Radxa's
(`edk2-project/Platform/Radxa/Orion/O6/Firmwares/ec_fw.bin`, "V1.07") that
the board, the SoC, UEFI, Linux or a user could see, or that matters for
trust. Each item names the `sky1emu ec-test` scenario that shows it (the
`EXPECTED` table in [emu/src/ec/gaps.rs](../../emu/src/ec/gaps.rs) holds
the same reasons) and, for the vendor's side, the code in its image
(bank:address, Keil C51 banks switched by P1[2:0]).

## Status

How it was checked: both images run in the same emulator of the IT5570
and the O6 board ([emu/README.md](../../emu/README.md#ec-orion-o6)), 61
scenarios, each compared by what the board sees (pins, I2C transfers a
device answered, host answers on the wire, e-flash commands, the
watchdog's registers, the fan's duty, the LED) in order and within a
time tolerance, plus the update of each image over the other through
3E19. Result: no difference other than the ones below. Every function of
the open image runs in these scenarios (99 % of its code bytes; the rest:
[What no test reaches](#what-no-test-reaches)).

Result: 0 differences, 254 expected in 16 kinds (commit "EC: the emulator
in Rust, suspend to RAM, and what 45 more scenarios found"); the method is
in [emu/README.md, EC (Orion O6)](../../emu/README.md#ec-orion-o6), "How
it compares".

**Nothing here has run on an O6**: the open EC firmware has only been
emulated. What the model cannot show (the real chip's timing, the
registers modelled from ITE's documentation and Zephyr's drivers, the PD
controllers, CSU_PM, the SoC, the NTCs, the fan, the e-flash mirror) is
under [Known limits and open points](#known-limits-and-open-points).

## What is the same

From [docs/open-firmware.md](../../docs/open-firmware.md#ec-firmware-orion-o6):
the open firmware does what the vendor's does on the O6: its sequencer
tables, suspend to RAM and the resume, its thresholds and duty table, its
NTC conversion (the vendor's own routine run over every ADC code:
`sky1emu ec-ntc-table`), its host-command answers byte for byte, including
the persistent buffers some commands read leftovers from, its e-flash
records (AC-loss policy at 0x1F8FF, power-off reason at 0x1FCFC), the
watchdog it arms after an NTC trip, the power LED (breathing in suspend).
In all 61 scenarios (power-on, S0, shutdown, suspend to RAM and the
wakes, AC lost in S0, S3 and standby, rails that never come or are lost,
the SoC's THERMTRIP, a stalled fan, open and shorted NTCs, CSU_PM and the
PD controllers not answering or their buses hung, an e-flash that does
not take a write, an NTC trip and the watchdog, every fan mode, every
host command id of the vendor's table and its neighbours in standby, S0
and S3, malformed and cut-short requests, commands back to back, runs of
minutes) the pin changes, I2C transfers, host-command answers, e-flash
writes, the fan's duty and the LED are the vendor's but for the
differences below, and each image installs over the other through 3E19
and boots, reaches S0 and answers. Odd behaviour of the vendor's that is
kept: [Vendor quirks kept](#vendor-quirks-kept).

## What is not the vendor's code

From [README.md](README.md) and
[docs/open-firmware.md](../../docs/open-firmware.md#ec-firmware-orion-o6):
Radxa's image is a Keil C51 build of CIX's EC code base, mostly laptop
code the O6 never runs. [firmware/ec](.) is a new firmware, written from
scratch in C for SDCC (4.2 to 4.6): `vectors.s` (vectors, ITE signature
at 0x40), `main.c` (tick, hooks), `power.c` (sequencer tables, button,
the SoC's sleep signals), `host.c` (SMBus B slave, host commands),
`fan.c` (NTCs, CSU_PM temperature, fan states, the trip), `pd.c`
(RTS5453H set-up), `led.c`, `smbus.c`, `eflash.c` and `scratch*.c`
(e-flash writes from scratch ROM, mirror reset), `gpio.c` (the O6 pin
table). `ntc_table.h` is generated
by `sky1emu ec-ntc-table` from the vendor's own conversion routine (bank
1 0xDD46).

## Differences kept on purpose

- **A request with a bad checksum is not run.** The vendor answers 7
  (invalid checksum) but runs the command first: its data come back in
  the answer, and a 0x92 GPIO write so sent is done (`host_bad`: TPM_PWR_EN
  goes low after a 0x92 with a broken checksum). The open firmware answers
  7 and does nothing (host.c `run_command`).
- **The protocol version is checked.** A request whose version byte is not
  3 gets result 6 (invalid version) from the open firmware; the vendor
  ignores the byte (`host_bad`). UEFI and Linux send 3.
- **GPIO writes are not lost.** The vendor does a 0x92 write at its next
  100 ms hook and keeps only one pending: of two writes within 100 ms only
  the second is done (`host_gap5`, `host_gap10`: its TPM_PWR_EN never
  changes). The open firmware does a pending write at once when the next
  0x92 comes, then the new one at the hook (host.c, 0x0092). A 0x93 read
  still answers the level sampled 15 ms after the previous 0x93, as the
  vendor's.
- **The AC-loss byte is written only when it changes.** The vendor erases
  the e-flash sector and programs 0x1F8FF at every 0x3E0B (bank 2 0xBB25,
  no comparison; requests that come while one is being written are merged).
  UEFI sends 0x3E0B from its start-up hooks (CIX's PlatformEnvHookLib,
  `SetStateAfterG3`): with the vendor's image one e-flash erase per boot.
  The open firmware compares with what the e-flash holds and writes only a
  change; a write that did not take is written again at the next request,
  as the vendor's would (`host`, `ac_lost_s0_off`, `host_every_s0`,
  `host_every_s3`; `eflash_fail` is the same in both).
- **The power-off reason is written once per NTC trip.** While an NTC stays
  at 85 C or more the vendor writes the 4-byte record at 0x1FCFC again at
  every thermal pass (an erase each, every ~10 s in S0, ~5 s in standby;
  bank 1 0xDBEF). The open firmware writes it at the trip; like the
  vendor's it re-arms the watchdog at every hot pass, so the EC is reset
  ~32 s after the passes stop or the NTC cools (`overheat_long`,
  `long_s0`).
- **A PD controller that does not answer does not stop the EC.** The
  vendor waits up to 200 pings x 27.5 ms for each command it sends a PD
  controller (bank 2 0x93CC), 16.5 s per set-up with one controller dead,
  and runs nothing else meanwhile: a button press then is lost (`pd_nack`:
  the board comes on only at the second press, 38 s later), and a hung PD
  bus keeps the board off (`pd_hang`). The open firmware pings as many
  times, without holding up the rest (pd.c `retry`); the power button's
  host event waits for the set-up's end, as the vendor's.
- **The open firmware's loop does not stop while the board waits.** When
  SYS_ALW_PG never comes the vendor's AC sequence waits for it and runs
  nothing else; the open firmware waits in its sequencer and still mirrors
  the power button (to an unpowered SoC: `alw_pg_never`). When SYS_S0_PG
  never comes the vendor has set TP91 (a test point) before it waits; the
  open firmware sets it in its step after SYS_S0_PG (`s0_pg_never`).
- **Bytes read past an answer.** Both give the 4 target-speed bytes after
  an answer of no data, as UEFI's GetEcInfo expects. Further bytes are the
  vendor's RAM after its buffer (0x31's parameters, the fan mode, the last
  0x92); the open firmware's are 0 (`host_overread`).

### Timing

Both images run their slow work in hooks whose period the vendor's main
loop sets: ~2.04 s in S0, ~1.6 s in S3, ~1.0 s in standby, none while a
power sequence, the S0 set-up, the PD set-up or a held button in standby
keeps its loop busy. The open firmware uses those periods; within them
the phases differ. Hence, within the tolerances the comparison allows:
the thermal passes (every fifth hook; the first ~14 s after a power-on)
come up to ~1.5 s apart; the audio de-pop line goes low or high at the
first slow hook after a suspend or resume (0.2-0.7 s); the LED comes on
up to 2.5 s earlier after a power-on (the vendor's waits for its PD
set-up); after a reset the vendor's first standby pass comes ~1.5 s
sooner. In S3 and around a trip which of a pass and a fan stop came last
can differ (`s3_*`, `overheat_long`: expected). The open firmware's 1 ms
tick is exact (768 counts of the 9.2 MHz / 12 clock; it ran ~1.8 % slow
before, see below).

## Left out

- **Laptop parts the O6 does not have**: the vendor probes three PMICs at
  0x60 (SMBus A, B, C), an I/O expander at 0x26, a light sensor (SCM3218,
  0x29), an "ID_SKU" part and runs an address scan ("rRenByte") on SMBus
  F at every power-on; all NACK on the O6. Not in the open firmware (the
  NACKed transfers are not compared). Host commands for them answer as the
  vendor's do (0x3E01 board ID 0, 0x3E03 PMIC versions 0, 0x0600/0x0601
  battery and 0x3E02/0x3E14 charger: empty).
- **I2C HID** (SMBus A slave, EC_HID_INT) and the keyboard: set up as the
  vendor sets the channel, not served.
- **PD firmware update over the host interface** (0x3E0D update
  information, 0x3E10 data, 0x3E0F with 0x2A,2): the vendor runs an update
  flow that raises and clears EC_ACPI_INT (bank 1 0xD489, 0xD4F3; flags
  0x09.5/6/7, 0x0A.0). The open firmware stores 0x3E0D's parameters,
  answers 0x3E0E from them, and does nothing else (`host_every_s0`,
  `host_every_s3`: the vendor's D3 events).
- **Bus recovery by the pins**: after a transfer times out on a hung SMBus
  the vendor also switches the bus's pins to GPIOs and clocks them to free
  a device holding SDA (`csu_hang`: F6/F7 toggled). The open firmware kills
  the transfer and resets the channel (smbus.c `reset`), nothing more.
- **Keyboard backlight on resume**: on a resume from S3 the vendor sets
  PWM0 to 100 % when its flag 0x18.2 is set (bank 1 0xCB76). PWM0 is not
  connected on the O6.
- **The debug console**: the vendor prints a lot on UART1 (115200 8N1);
  the open firmware prints a few lines (its version, the AC-loss policy,
  standby, S0, suspend, resume, shutdown, G3, an NTC overheat, EC mirror).
- **Version string**: 0x3FFF answers `V1.07-open` (padding as the
  vendor's); the vendor's answers its own string.

## Faults of the vendor's image that the open one does not have

The items above marked as such, with what shows each:

| Fault | Where | Scenario |
|---|---|---|
| a command with a bad checksum is run | the vendor's host handler answers 7 after dispatching | `host_bad` |
| of two 0x92 within 100 ms the first is lost | one pending GPIO write (flags 0x09/0x0A, bank 1 0xD893) | `host_gap5`, `host_gap10` |
| a PD controller not answering stops the main loop 16.5 s per set-up; the button is lost | bank 2 0x93CC: 200 pings (0x963D = 0xC8), every 27.5 ms, per command | `pd_nack`, `pd_hang` |
| an e-flash erase at every 0x3E0B, i.e. every boot | bank 2 0xBB25 | `host`, `host_every_s0` |
| the power-off record rewritten at every hot pass | bank 1 0xDBEF..0xDC28 | `overheat_long`, `long_s0` |
| a request 0.1 ms after the previous answer's STOP gets the previous answer | not established on hardware: in the model the first byte then comes 0.2 ms after the STOP (at 100 kHz a START and the address byte take ~0.1 ms); 0.2 ms and more work (`host_back_to_back` uses 1 ms) | (by hand: `back_to_back(ec, 0.0)` in gaps.rs) |
| RAM after the answer buffer read out | bytes past an answer | `host_overread` |

## Vendor quirks kept

Kept because the board or UEFI may depend on them, and documented here
because a reader of the source would not expect them:

- Standby is the vendor's "SD" state: thermal passes run there too (every
  ~5 s), each sets the state's duty and the next hook stops the fan again
  a millisecond later (`long_standby`). In S3 the same, with the vendor's
  ~1.6 s hook: the fan can run at the state's duty for up to ~3 s
  (`s3_long`).
- Host work left for later (0x92, 0x93, 0x21, 0x3E0B, 0x3E19) is not done
  in standby or G3 until the board is on (the vendor's standby loop does
  not run it); no host can send commands then on an O6.
- After an NTC trip the external watchdog resets the EC ~32 s after the
  last hot pass (`overheat_long`: the EC restarts in standby, trips again
  if still hot).
- 0x93 answers the level read at the previous 0x93.
- A shutdown from S3 leaves GPJ7 high, as the vendor does (`s3_shutdown`).

## Faults found in the open firmware by these tests

Fixed in the open firmware by these tests (2026-11). For the record; none of these is a difference any more:

- No suspend to RAM: SLP_S3 low with SLP_S5 high dropped EC_SLP_S3_L and
  nothing brought it back, so the S0 rails stayed off after a resume.
  Now the vendor's STR state (power.c `soc_signals`), with the PMIC gate,
  the resume, shutdown from S3, the de-pop line, the breathing LED.
- The fan's duty was set only when the thermal state changed: after mute
  or full speed and back to automatic, or after a resume, the fan kept the
  old duty. Now the applied state is forgotten as the vendor does (fan.c).
- CSU_PM never answering: the fan stayed off (the states used 0 C); now
  the last temperature read, 25 C before any (20 %), as the vendor.
- A read-back value (0x93) changed by a 0x92 write; target speeds of 65536
  rpm and more; the button released in SD (the SoC back on before G3);
  power good lost in S0 (the reason, 4 or 8, recorded as the vendor does);
  the watchdog after a trip; PD controller retries without end; the 1 ms
  tick reloading TL0 (losing the interrupt's latency: ~1.8 % slow); two
  unused functions removed.

## Known limits and open points

Nothing here has run on an O6. What the model cannot show: the real
chip's timing and its registers where the model is a reading of ITE's
documentation and Zephyr's it8xxx2 drivers (the wake-up controller's
numbering, the external watchdog: EWDCNTLLR starting it, its 1.024 kHz
clock by ETWCFG bit 4), the SMBus slave's address phase (the model sends
the first byte of a request 0.1 ms after the master starts), the PD
controllers (a minimal RTS5453H: vendor-command enable, GET_IC_STATUS,
ping status), CSU_PM (its register file as both PM firmwares serve it in
`sky1emu pm-test --full`, answering only while the SoC's always-on part is
powered), the SoC (its SLP_S3/SLP_S5 and reset lines, waking from S3 on
the power button), the NTCs (100K, B 4250, 24K pull-up), the fan (speed
proportional to the duty, 5000 rpm at full). The e-flash mirror (3E19,
the only way a new EC image gets in) is modelled after what the vendor's
image does; a first test on a board needs a way back (an SPI programmer).

From [docs/open-firmware.md](../../docs/open-firmware.md#ec-firmware-orion-o6):
the EC's own e-flash writes (the AC-loss policy, the power-off reason) run
from scratch ROM; the model copies what SCAR0 maps and stops on any code
fetch from the e-flash while it erases or programs, and both firmwares
pass. The real chip (pin timing, the PD controllers' and CSU_PM's real
answers, the fan, the mirror itself) is not shown. Radxa's image can be
mirrored back the same way (the open firmware's 3E19 does that too). The
times compared are within tolerances (see [Timing](#timing) and
[emu/README.md](../../emu/README.md#ec-orion-o6)). No unfixed fault of
the open firmware is recorded.

## What no test reaches

`ec-cov`: every function of the open image runs. Parts of functions that
no scenario reaches: the code after the mirror reset's watchdog key
(`power_force_reset`, `mirror_reset`: the EC is reset there); the SMBus B
slave's time-out status (SLSTA bit 2: the model's slave does not time
out); `fan_set_pct`'s guard against more than 100 % (no caller passes
more); a branch of the I2C read loop for reads of two bytes and more with
nothing written; the C library's generic-pointer read for pointer kinds
the firmware does not use; a few bytes of the C start-up.

Every function of the open image runs: 99 % of its code bytes
(`ec-test --cov FILE`, then `ec-cov FILE firmware/ec/build --partial
--lines`).

## Checking it yourself

On the host, with a Rust toolchain. Radxa's image is
`edk2-project/Platform/Radxa/Orion/O6/Firmwares/ec_fw.bin`
(`--vendor IMG` takes another); the update check takes the EC image from
the O6's SPI flash image (`output/orion-o6/RELEASE/cix_flash_all.bin`,
`--flash`).

```sh
cargo build --release --manifest-path emu/Cargo.toml   # emu/target/release/sky1emu
emu/build-open.sh ec                                   # out/emu/ec/ec_fw.bin (in the build container)
emu/target/release/sky1emu ec-test --open out/emu/ec/ec_fw.bin -v   # all 61 scenarios and the update, ~35 s
emu/test-all.sh [--build] [--cov] [--audit]            # every suite of every open binary (~14 min with coverage)
```

As the document stood (a build in `firmware/ec/build`, `make` in
[firmware/ec](.)):

```sh
cargo build --release --manifest-path emu/Cargo.toml
emu/target/release/sky1emu ec-test --open firmware/ec/build/ec_fw.bin -v
```

A run ends with "N differences, M expected in K kinds"; each expected
kind is an entry of `EXPECTED` in
[emu/src/ec/gaps.rs](../../emu/src/ec/gaps.rs).
