# One codebase for every Sky1 board

Goal: the five boards run the same firmware, from SE to UEFI, and differ
only where their hardware does (pin multiplexing and GPIOs, PCIe lanes and
reset pins, USB-C/PD wiring, EC, LAN chip, power button, DRAM and PM tables,
flash layout, ACPI board configuration, SMBIOS strings, logo).

## What cannot be shared

The Orange Pi 6, Orange Pi 6 Plus and retail MS-R1 (signing chain B) only
start boot firmware signed with a key whose private half is not public: the
bootloader2 root `0d999213` is fused (it is in no flash file: not in BL2, SE,
PM, `se_config` or the configuration blobs), and these boards reject the
chain-A BL1. On them, code before BL33 can only be what CIX or the vendor has
signed. Chain-A boards (Orion O6, O6N) can run boot firmware built here
([open firmware](open-firmware.md)).

## Boot firmware (SE, CSU_PM, BL2, BL31, OP-TEE, StMM)

CIX 2026Q3 ships its boot firmware signed for both chains (`PackageTool/
Firmwares` for A, `Firmwares2` for B). The SE, CSU_PM, BL2, BL31, OP-TEE and
StMM builds are identical in the two (one OP-TEE build timestamp differs), so
every board can run the same boot firmware:

```sh
./build.sh --all --boot-firmware cix        # or boot_firmware: cix in boards/*.yaml
```

| Board | Chain | Vendor boot firmware today | Memory config | `--boot-firmware cix` |
|---|---|---|---|---|
| Orion O6 | A | Radxa, BL2 Jun 2026 | v2.0 | builds; not boot-tested |
| Orion O6N | A | Radxa (same as O6) | v2.0 | builds; not boot-tested |
| MS-R1 | B | Minisforum retail, BL2 Oct 2025 | v1.6 → `mem_config_v2` | builds; not boot-tested |
| Orange Pi 6 | B | Orange Pi, BL2 Dec 2025 | v2.0 | builds; not boot-tested |
| Orange Pi 6 Plus | B | Orange Pi, BL2 Sep 2025 | v2.0 | builds; not boot-tested |

The build checks that the selected BL1 and bootloader2 are signed for the
board's chain. MS-R1's DRAM settings were ported from the v1.6 to the v2.0
format; `scripts/open/memcfg.py` decodes both and every build checks that the
port changes no setting. (The v1.6 table shifted its Vref values by a reserved
byte; the board, and the retail image, train with DqVref 280 and CaVref 0, and
the port keeps those.)

The vendor boot firmware stays the default until each board has booted the
CIX one. Things to watch on the first boot: DRAM training at the board's rate
(UART5, SE log), the PM configuration (v3.0 and v3.2 files under a CSU_PM whose
own default is v3.1), fans on the boards where CSU_PM runs them.

## UEFI

All five boards build from one shared DSC/FDF,
[Sky1Board.dsc.inc](../edk2-project/Platform/CIX/Sky1/Sky1Board.dsc.inc) and
[Sky1Board.fdf](../edk2-project/Platform/CIX/Sky1/Sky1Board.fdf) (CIX 2026Q3
Merak plus the patch queue). The shared body sets every feature; a board DSC
only describes hardware ([architecture](architecture.md#shared-board-body)).

### Features (the same on every board)

| Feature | Notes |
|---|---|
| Setup | the CIX setup interface (`SetupManagerDxe`), with the PCIe 64-bit window and ASPM options; items for hardware a board lacks are hidden (EC: version, fan mode, light sensor, device power; GMAC page) |
| ACPI or DT boot | chosen in setup (*System Table*); DT boot loads `BOARD_DTB_NAME` from disk |
| Network | stack, PXE and HTTP boot built in, **off by default** (setup: *Platform → Network Stack*); Realtek PCIe (RTL8125/8126/8136/8161/8168) and USB UNDI drivers load only when it is on |
| Secure Boot | defaults are Microsoft's PK/KEK/db |
| Configuration updates | memory and SE configuration from setup (CIX drivers); PM configuration off |
| Watchdog | `GlobalWatchdogDxe`, boot watchdog 90 s (before this, only MS-R1 built it) |
| Tables | FPDT (boot performance), SPCR on the UART2 console |
| Other | x86 option-ROM emulator (graphics cards), AMD GOP, I2C HID, RAM disk, boot logo (per board) |
| Capsule update | **none**: update by flashing `cix_flash_all.bin` (or the OTA image) |

### SPI TPM

The discrete TPM support is shared but only MS-R1 turns it on. A board with a
TPM on one of the SoC SPI controllers adds to its DSC:

```
  DEFINE SPI_TPM_ENABLE             = TRUE          # before the !include

[PcdsFixedAtBuild.common]                           # after it
  gCixPlatformTokenSpaceGuid.PcdDTPMSpiBus|0x0      # 0: SPI1 (0x04090000), 1: SPI2 (0x040A0000)
  gCixPlatformTokenSpaceGuid.PcdDTPMSpiChipSelect|0x2   # 1: SS0, 2: SS1
```

and muxes the SPI pins in its pin table. That builds `Tcg2Dxe` with
`Tpm2DeviceLibCdnsSpi` (measured boot, Secure Boot page) and `SpiTpmAcpiDxe`
(the CRB TPM in ACPI for the OS). Without a TPM answering, both stay idle.

### What still differs

Hardware, as intended: pin and GPIO tables, PCIe lanes/reset pins/x4 slot,
USB and USB-C ports, PD controllers (O6N: CS32G051 with its own PD driver),
EC (O6, Orange Pi 6 Plus) and power button, RTC chip (HYM8563 on the Orions,
RA8900CE elsewhere), UART/I2S/DP enables and priorities, flash layout
(variable store, BL33 size: 0x220000 on the Orions, 0x1F0000 elsewhere), ACPI
tables and `LinuxAcpiConfig.h`, SMBIOS strings, logo, DTB name.

Not yet merged:

* MS-R1 keeps Minisforum's `PlatformBootManagerLib`, `PlatformEnvHookLib`,
  `PlatformBootHookLib`, `ConfigParamsHookLib`, reset library and ACPI
  platform driver; the Orions keep Radxa's ACPI platform driver. These are
  three-way merges of CIX code with board changes; folding the generic parts
  back into the shared modules is the next step.
* Orion O6/O6N power on when AC returns (`PcdStateAfterG3` 1, Radxa's
  default); the other boards keep CIX's default.
* MS-R1 no longer names its memory part in setup (Minisforum's setup copy read
  it from GPIO 82–85); O6N's UFS power option has no setup entry (Auto, from
  `PcdUFSPowerMode`).

### Changes for users of the previous images

* Orion O6/O6N: setup is the CIX interface instead of Radxa's; DT boot needs
  `sky1-orion-o6.dtb` / `sky1-orion-o6n.dtb` on the ESP (the embedded DTB and
  Radxa's ACPI/DT switch are gone); firmware debug output goes to UART2
  (Radxa's builds used UART3; `DEFINE DEBUG_ON_UART3 = TRUE` in the board
  DSC restores that).
* MS-R1, Orange Pi 6, Orange Pi 6 Plus: no capsule updates.
* Orange Pi, Orion: "Reset Secure Boot keys" installs Microsoft's keys.
* MS-R1: network stack available (off by default).

All of this is built and checked statically; none of it is boot-tested yet.

### Order of work

1. Boot-test the shared firmware on each board (vendor boot firmware first,
   then `--boot-firmware cix`).
2. Fold the remaining MS-R1 and Radxa board-module copies of CIX code back
   into the shared modules.
3. Make `--boot-firmware cix` the default per board once it boots.
