# How the open firmware differs from the vendors'

For every binary `--open` (and `--open-ec`) builds from source there is a
document of every way it differs from the vendor's image it replaces,
each with the same eleven sections, from "Status" to "Checking it
yourself". Five were made by running the open image and the vendor's side
by side in the emulators of [emu/](../emu/README.md) (`sky1emu`) and
comparing what each does, write for write, with every difference that
stays listed with its reason in the test's table of expected ones; OP-TEE
and StandaloneMM were compared by reading only.

| Binary | Compared with | How | Result | Run on hardware | Document |
|---|---|---|---|---|---|
| SE firmware | CIX's debug build 2.0.2026W01.01 (the shipping 2.0.2026W31.01 has no symbols and is not compared) | `sky1emu se-test`, `emu/test-se.sh`: 18 jobs (every suite, every board's flash image, DDR driver live, secure boot in 20 cases) | 0 differences; expected ones in `EXPECTED` of `emu/src/se/test.rs` (no count stated) | no | [firmware/se/differences.md](../firmware/se/differences.md) |
| CSU_PM | CIX's `csupm_sky1` 00.12.001 (debug build) | `sky1emu pm-test`, `emu/test-pm.sh`: every suite (27 `--full` phases) in seven configurations, `pm-faults`, `pm-vf --matrix` (78 parts) | 0 differences; about 705 to 730 expected in six to ten kinds per standard configuration, 827 in ten kinds in `options` | no | [firmware/csupm/differences.md](../firmware/csupm/differences.md) |
| Sensor fusion hub (SFH) | CIX's `sfh_fw.bin` (`edk2-non-osi`, 17 360 bytes) | `sky1emu sfh-test`: 162 phases | 0 differences; five expected entries for two reasons | no | [firmware/sfh/differences.md](../firmware/sfh/differences.md) |
| Orion O6 EC | Radxa's EC V1.07 | `sky1emu ec-test`: 61 scenarios and the update of each image over the other | 0 differences, 254 expected in 16 kinds | no | [firmware/ec/differences.md](../firmware/ec/differences.md) |
| TF-A BL2 and BL31 | CIX's build of 2026-07-15 (`v2.7-33527c31abbe`); Radxa's O6 bootloaders of June 2026 (`ed9772217e49`) | `sky1emu a64-test`, `emu/test-a64.sh`: 121 phases (BL2 in 34 configurations, BL31's cold boot, every SMC) | CIX's: 0 differences, 1656 expected in 16 kinds; Radxa's: 0 differences, 1725 expected in 19 kinds | no | [docs/differences-bl2-bl31.md](differences-bl2-bl31.md) |
| OP-TEE and StandaloneMM (and the containers, certificates and configuration data) | the OP-TEE in CIX's 2026Q3 bootloader2 (`Beta_2.1.5_release-3.17-67e342df9765`), with CIX's StMM | reading: sources, patches and CIX's binaries; StMM's NOR code on the host (`test_stmm_nor.py`) | not compared by running | no | [docs/differences-optee-stmm.md](differences-optee-stmm.md) |

All emulated suites at once: `emu/test-all.sh [--build] [--cov] [--audit]`
(about 14 minutes with coverage); `--build` rebuilds the open firmware
first with `emu/build-open.sh`.

UEFI (BL33) is not one of these: it is built from source for every board
in any case, from CIX's 2026Q3 tree with the patch queue. What it changes
against CIX's code is described in [README.md](../README.md#what-is-in-here)
(the fixes of the `edk2-platforms` and `edk2` patch queues),
[docs/parity.md](parity.md#uefi) (the features every board shares, what
still differs between boards, and the
[changes for users of the vendors' images](parity.md#changes-for-users-of-the-previous-images))
and the patches' own messages in `patches/2026Q3/edk2-platforms` and
`patches/2026Q3/edk2`. No emulator compares it with a vendor's UEFI, and
it has no document in this set.

## Faults of the vendors' images that matter

For security, or for whether a board boots or stays up; none in the open
images. Each is shown in an emulator unless marked "read".

* SE: secure boot does not protect CSU_PM: a CSU_PM whose digest or
  signature check fails is started all the same
  ([SE](../firmware/se/differences.md#faults-of-the-vendors-image-that-the-open-one-does-not-have)).
* SE: requests `0x8200000e` and `0x82000002` from the non-secure mailbox
  overflow the SE's stack (from 0x200 bytes a fault and a system reset)
  and give out fuse bytes past the fuse array
  ([SE](../firmware/se/differences.md#faults-of-the-vendors-image-that-the-open-one-does-not-have)).
* SE: the secret eFuse fields (device and model keys) go to either
  mailbox; the hash service hashes any address, the SE's SRAM and the
  eFuse mirror among them; request `0x8200001a` burns the OEM root key
  hash, which turns secure boot on for good
  ([SE](../firmware/se/differences.md#differences-kept-on-purpose)).
* BL31: SiP call `0xc2000030` copies the caller's length into a 120-byte
  buffer on EL3's stack: the normal world can run EL3 at an address of
  its choice ([BL2, BL31](differences-bl2-bl31.md#faults-of-the-vendors-image-that-the-open-one-does-not-have)).
* BL31: `0xc200000b` runs flash transactions on any address (secure
  memory read to flash, flash over secure memory); `0xc200000a` with an ID
  out of range faults ([BL2, BL31](differences-bl2-bl31.md#faults-of-the-vendors-image-that-the-open-one-does-not-have)).
* BL2: authenticates nothing with secure boot not fused (every board
  sold), and never checks bootloader3's trusted key certificate
  ([BL2, BL31](differences-bl2-bl31.md#differences-kept-on-purpose)).
* BL2 of June 2026 (Radxa's O6): a flash read across a 2 MiB boundary
  reads on, 3.8 MiB past UEFI and the chip's end, into DRAM
  ([BL2, BL31](differences-bl2-bl31.md#faults-of-the-vendors-image-that-the-open-one-does-not-have)).
* CSU_PM: any SCMI message of a protocol it has not got (0x16 and up)
  kills it; `SYSTEM_POWER_STATE_SET` with a state that does not exist
  asserts ([CSU_PM](../firmware/csupm/differences.md#faults-of-the-vendors-image-that-the-open-one-does-not-have)).
* CSU_PM: fan message 0x802 with a user table of nine points empties the
  table, and the fans' loop then asserts; its PVT warning handler panics
  in the idle task when the line is unmasked (masked as shipped)
  ([CSU_PM](../firmware/csupm/differences.md#faults-of-the-vendors-image-that-the-open-one-does-not-have)).
* CSU_PM: a cluster's clock raised without its voltage (two owners of the
  clusters; a voltage write reported failed but done: 810 mV under a
  raised PLL), and a resume on a dead PMIC bus that never completes
  ([CSU_PM](../firmware/csupm/differences.md#faults-of-the-vendors-image-that-the-open-one-does-not-have)).
* EC: a PD controller that does not answer stops the main loop 16.5 s per
  set-up, a button press is lost, and a hung PD bus keeps the board off;
  a host command with a bad checksum is run (a GPIO write so sent is
  done) ([EC](../firmware/ec/differences.md#faults-of-the-vendors-image-that-the-open-one-does-not-have)).
* StandaloneMM (read): an unchecked transaction length lets UEFI, and the
  OS through OP-TEE, write flash data past StMM's buffer into secure
  memory ([OP-TEE, StMM](differences-optee-stmm.md#faults-of-the-vendors-image-that-the-open-one-does-not-have)).
* OP-TEE and Radxa's TrustZone configuration (read): NOR secure storage
  past the end of an 8 MiB flash, onto the firmware header and
  bootloader1; Radxa's O6/O6N file puts secure storage over bootloader3
  ([OP-TEE, StMM](differences-optee-stmm.md#faults-of-the-vendors-image-that-the-open-one-does-not-have)).

The SFH's faults (late wake-ups after a resume, a vector table that ends
at interrupt 60) are of neither kind.

## Faults the comparisons found in the open firmware that would have stopped a board

All fixed.

* BL2 refused every certificate whose counter is above 0 (the O6's carry
  31): no `--open` image could have booted
  ([BL2, BL31](differences-bl2-bl31.md#faults-found-in-the-open-firmware-by-these-tests)).
* BL31 was built as CIX's single-core variant and stopped at "No GICR base
  frame found for Primary CPU" ([BL2, BL31](differences-bl2-bl31.md#faults-found-in-the-open-firmware-by-these-tests)).
* BL31 left `SCR_EL3.HXEn` clear: a write to `HCRX_EL2` from EL2 trapped
  to EL3, which has no handler for it ([BL2, BL31](differences-bl2-bl31.md#faults-found-in-the-open-firmware-by-these-tests)).
* BL2 read on past an image across a 2 MiB boundary, as the vendor's June
  BL2 ([BL2, BL31](differences-bl2-bl31.md#faults-found-in-the-open-firmware-by-these-tests)).
* EC: no suspend to RAM: after a resume the S0 rails stayed off
  ([EC](../firmware/ec/differences.md#faults-found-in-the-open-firmware-by-these-tests)).
* EC: with CSU_PM never answering the fan stayed off
  ([EC](../firmware/ec/differences.md#faults-found-in-the-open-firmware-by-these-tests)).
* CSU_PM: after the SoC idle switch took the core to 200 MHz, the
  services' timers (the fans' loop, PROCHOT's, the heartbeat) waited as
  long as the PM had been up ([CSU_PM](../firmware/csupm/differences.md#faults-found-in-the-open-firmware-by-these-tests)).
* CSU_PM: an SCMI power-off was taken for a power-on; notifications rang
  a doorbell bit the AP's driver does not take for a message; a message
  of a bad length was copied all the same, so any agent could have the PM
  overwrite its own memory ([CSU_PM](../firmware/csupm/differences.md#faults-found-in-the-open-firmware-by-these-tests)).
* SE: an erased memory configuration stopped the boot
  ([SE](../firmware/se/differences.md#faults-found-in-the-open-firmware-by-these-tests)).

## What is common to all

* **None of it has run on hardware.** Every result above is from an
  emulator or from reading.
* The emulators' models of the hardware are inferred from what the
  vendors' code does with it (and, for the EC, from ITE's documentation
  and Zephyr's drivers), not from the SoC's documentation; whatever a
  model gets wrong in the same way for both images, no comparison shows.
  Each document's "Known limits and open points" says what its model
  assumes.
