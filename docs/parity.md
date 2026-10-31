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

### Shared DSC/FDF

[Sky1Board.dsc.inc](../edk2-project/Platform/CIX/Sky1/Sky1Board.dsc.inc) and
[Sky1Board.fdf](../edk2-project/Platform/CIX/Sky1/Sky1Board.fdf) hold everything
the boards share (CIX 2026Q3 Merak plus our patches). A board DSC gives its
`[Defines]` header, the defines that describe it (`BOARD_DIR`,
`BOARD_DTB_NAME`, `PCIE_RP0_X4_SLOT`, `BOARD_*` module macros, feature
switches) and, after the `!include`, its own PCDs and components.

| Board | On the shared body |
|---|---|
| Orange Pi 6 | yes; FD byte-identical to before |
| Orange Pi 6 Plus | yes; FD byte-identical to before |
| MS-R1 | yes; FD byte-identical to before (its own setup manager, hooks, ACPI driver and logo still board modules) |
| Orion O6, O6N | no: built on Radxa's copies of CIX's `Sky1Define`/`Sky1Common` (an older CIX release, about 1,270 lines apart) |

### Feature differences today

| Feature | O6 | O6N | MS-R1 | OPi 6 | OPi 6 Plus | Target |
|---|---|---|---|---|---|---|
| Setup UI | Radxa `PlatformConfigDxe` | Radxa | Minisforum fork of CIX `SetupManagerDxe` | CIX `SetupManagerDxe` | CIX | CIX `SetupManagerDxe` (carries the PCIe window and ASPM patches) with the Radxa and Minisforum additions merged in |
| Capsule update (`.cap`, ESRT) | no | no | yes | yes | yes | all |
| Network stack, PXE/HTTP boot | yes | yes | no | yes | yes | all |
| Realtek LAN UEFI driver | RTL8126 (Radxa) | RTL8126 | CIX Realtek | CIX Realtek | CIX Realtek | all drivers on all boards (cards can be added over PCIe) |
| Secure Boot defaults | CIX test keys | CIX | Microsoft | CIX | CIX | Microsoft keys everywhere |
| SPI discrete TPM | no | no | yes | no | no | built in everywhere; the driver must tolerate an empty header (to verify) |
| AMD GOP, I2C HID | yes | yes | no | yes | yes | all |
| Boot logo | no | no | Minisforum | Orange Pi | Orange Pi | per board (branding) |
| Firmware/memory/PM config update from setup | yes (Radxa) | yes | SE config only | SE config only | SE config only | all |
| System information page | Radxa `SystemInfoDxe` | Radxa | — | — | — | all |
| DT or ACPI boot selection (`DtPlatformDxe`) | yes | yes | no | DTB update only | DTB update only | all, ACPI default |
| Boot performance tables (FPDT), SPCR | yes | yes | SPCR | — | — | all |
| Global watchdog | yes | yes | no | yes | yes | all |

Hardware differences that stay per board: EC (O6 and Orange Pi 6 Plus have
one), power button wiring, PD controllers and USB-C modes, PCIe lanes/reset
pins/x4 slot, UART and I2S enables, RTC chip (Radxa HYM8563, others RA8900CE),
flash layout and variable store address, DTB name, ACPI board configuration.

### Order of work

1. Orion O6/O6N onto the shared body, porting Radxa's features (system
   information, config update, DT/ACPI selection, FPDT, SPCR, RTL8126) into it
   as shared options instead of keeping Radxa's older CIX copies.
2. One setup UI: merge MS-R1's `SetupManagerDxe` fork and the Radxa pages into
   the CIX one; drop the board copies.
3. Flip the feature switches above to the target values in the shared body,
   one feature per commit, and remove them from the board DSCs.
4. Boot-test each board with `--boot-firmware cix`, then make it the default.

Each step is checked by building every board; steps 1–2 change the O6/O6N and
MS-R1 images, so they need a boot test on those boards.
