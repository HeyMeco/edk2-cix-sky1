# Orange Pi 6 / 6 Plus provenance

Orange Pi does not publish the UEFI source for these boards. This document
records how the board packages were derived from the vendor firmware and how
to repeat the checks.

## Vendor images

| Board | File | SHA-256 | Build string |
|---|---|---|---|
| Orange Pi 6 Plus | `OPI6PLUS_BIOS_1.4_General.bin` (6,288,078 B) | `acf957ac…c16c649a` | `BiosV1.4-W20260130-97268c4c`, Windows build of the CIX Merak tree |
| Orange Pi 6 | `bios_v1.0.bin` (6,288,062 B) inside `opi6_bios_image_20261019.img` | `bdc47cf9…42775de2a` | `v1.0_for_opi6_linux-W43M261019175125N-355f4c7494f9`, CIX 2025Q4 SDK Merak build |

Both are CIX Merak builds: the module set is Merak's (OPI6 adds the fastboot
modules, the Plus lacks the modules CIX added after its SDK).

### Flash layout (corrects earlier notes)

The images use the same **8 MiB** layout as the Minisforum MS-R1, trimmed after
the BL33 FIP by `cix_package_tool` (hence the "6 MiB" file size). They contain a
memory configuration and a PM configuration:

| Region | Address | Plus size | OPI6 size | Source in this tree |
|---|---|---|---|---|
| EC firmware | `0x010000` | `0x20000` | `0x20000` | vendor image |
| sfh_fw | `0x101000` | `0x43D0` | `0x43D0` | CIX stock (identical) |
| BL1 | `0x188000` | `0xD7000` | `0xD8000` | vendor image |
| BL2 (BL31 + OP-TEE) | `0x288000` | `0xEE9E4` | `0xF69F4` | vendor image |
| reserved | `0x380000` | `0x2000` | `0x2000` | generated |
| memory config | `0x3F8000` | `0x3C0` | `0x3B0` | vendor image |
| PM config | `0x3FC000` | `0x1000` | `0x1000` | vendor image |
| se_config | `0x3FD000` | `0xF10` | `0xF10` | CIX stock (identical) |
| BL33 FIP | `0x3FE000` | `0x2012CE` | `0x2012BE` | **built here** |

None of the vendor BL1/BL2/memory/PM blobs matches anything in the public CIX,
Radxa or Minisforum trees. The extracted regions are stored in
`vendor/orangepi/<board>/` so that the build needs no vendor image; each entry
in `manifests/blobs/opi6*.json` records its hash and, under `origin`, where it
was cut from. `scripts/fetch-blobs.py --from-vendor` regenerates them.
`scripts/compare-image.py` confirms that a build differs from the vendor image
only in BL33.

## Board configuration recovered

### Pin multiplexing and GPIO tables

`PinMuxTableLib` and `GpioTableLib` are packed static arrays linked into
`PlatformDxe`. `scripts/extract/iotables.py` scans for runs of records with
legal field values; on a CIX Merak build it finds exactly Merak's tables, and
on the vendor images it finds one table of each kind (Plus: 119 pin + 35 GPIO
entries, OPI6: 126 + 42). Every pin offset decodes to an `IO_S0_*`/`IO_S5_*`
name from `IoConfig.h`. `scripts/extract/gen_iolibs.py` writes the board
libraries from them.

### PCDs

The setup forms take their defaults from fixed PCDs, so the HII form defaults
(`scripts/extract/ifr.py`, `dump.py setup`) expose board PCDs. Compared with a
CIX 2025Q4 Merak build (same setup layout as the OPI6):

| PCD | Merak | Orange Pi |
|---|---|---|
| `PcdPcieRootPort0LaneNum` | x8 | x4 |
| `PcdPcieRootPort2LaneNum` | x2 | x1 |
| `PcdI2c3BusFreq` | 100 kHz | 400 kHz |
| `PcdUsbCDrdControl0DataRole` | device | host |

Same for both boards. The USB-PD driver data (`PdDxe`) gives two PD
controllers (I2C7 `0x30`/`0x31`, alert pin 8) and USB-C PHYs 2 and 3 in
DisplayPort-only mode (`PcdTypecPortDefaultModes = {1, 1, 4, 4}`). The DSDT
disables the SoC GMACs, so `PcdGmac0Enable` is `FALSE`.

### Platform hooks

`PlatformDxe` strings show that the Plus keeps Merak's EC-driven hooks
(device power rails through EC GPIOs), while the OPI6 removed every EC call;
`OPI6/Library/PlatformEnvHookLib` drops those hooks.

## ACPI

* **Static tables** (FACP, GTDT, IORT, …) are identical to CIX Merak's.
* **SSDT**: the vendor SSDT is used as is (`vendor/orangepi/<board>/Ssdt.aml`,
  SHA-256 Plus `624228d9…`, OPI6 `ecc47f6e…`), staged into the board package
  by `fetch-blobs.py` and included through a binary INF.
* **DSDT**: built from CIX 2026Q3 source. `ACPI/LinuxAcpiConfig.h` describes
  the board (USB-C PD/PHY modes, PCIe PERST and VCC regulators, HDA model)
  and sets the guards from `patches/edk2-platforms/0001-*`:

| Device | Change | Board |
|---|---|---|
| COM0, COM1 | no pin group | OPI6 |
| COM3 | enabled, DMA and `_DSD`; pin group on the Plus | both |
| I2C2/4/5 or I2C3 | hidden from the OS | OPI6 / Plus |
| I2C3, I2C4, I2C5 | pin groups, I2C3 recovery GPIOs | both |
| PWM1 | at `0x04112000`; PWM3 and PWM5 added | both |
| SPI0, SPI1 | SPI0 two chip selects; disabled on OPI6, SPI1 disabled on both | both |
| DP04 | PS185 DP-to-HDMI bridge (pin group, GPIO, `_DSD`) | OPI6 |
| SUB0–SUB3 | no VBUS GPIOs | both |
| HUB0, HUB3 | `pinctrl_usb6`/`pinctrl_usb9` | Plus |
| DCT0–3, CGFX, GCRU | removed / disabled | Plus |
| PRC0, PRC1, PRC4 | VCC regulators PVC0/PVC1/PVC4, no X1_0 power-on quirk | both |
| HDA | no power-down GPIO, model `CIX SKY1 ORAPI 6P HDA` | both |

### Parity check

`scripts/acpi/compare.py` computes, per device, the vendor's edits on top of
its CIX base and this tree's edits on top of CIX 2026Q3, so version drift
between the two bases cancels out. For the OPI6 the remaining differences are
intentional (`boards/opi6.yaml`):

* I2C2–5, MAC0, MAC1: enabled at runtime from setup defaults in Q3 instead of
  a hard-coded `_STA`; same result.
* DP04: same properties plus Q3's `cfg_adapter_port`.
* PRC3: the vendor DSDT references a `PVC3` regulator its SSDT does not define.
* PRC1, SHM0, SHM1: bus-range and SCMI fixes from the patch queue.

The Plus image comes from an older CIX release, so it is compared against the
OPI6: the Plus-specific edits (UART3 pin group, I2C3 off, SPI0 on, USB hub pin
groups, DCT/CGFX/GCRU, no HDMI bridge) match, and what remains is drift of the
older base (ramoops size, I2S `dp_pair_id`, `_UID`s).

`acpiexec` loading this DSDT with the vendor SSDT reports exactly the same
diagnostics as the vendor DSDT with the vendor SSDT (unresolved `UXC*`
references in both, plus EC timeouts that only occur in the emulator).

### Reproducing

```sh
mkdir -p out/vendor/opi6 && cp bios_v1.0.bin out/vendor/opi6/image.bin
scripts/extract/dump.py layout  out/vendor/opi6/image.bin
scripts/extract/dump.py acpi    out/vendor/opi6/image.bin out/vendor/opi6/acpi
scripts/extract/iotables.py     out/vendor/opi6/image.bin --start 0x3FE000 \
    --ioconfig edk2-platforms/Silicon/CIX/Sky1/Include/IoConfig.h
scripts/extract/dump.py setup   <merak-build.fd> out/vendor/opi6/image.bin
iasl -d out/vendor/opi6/acpi/DSDT.aml
scripts/acpi/compare.py VENDOR_BASE.dsl VENDOR.dsl OUR_BASE.dsl OUR.dsl
scripts/compare-image.py out/vendor/opi6/image.bin output/opi6/RELEASE/cix_flash_all.bin
```

`VENDOR_BASE` is a CIX Merak DSDT built from the same CIX release as the vendor
image (2025Q4: `edk2` `9075ff07`, `edk2-platforms` `ae728f72`,
`edk2-non-osi` `907396d`); `OUR_BASE` is the Merak DSDT of the pinned tree.

## Open points

* Not boot-tested. In particular the PCIe lane/PERST assignment, the EC hooks
  on the Plus and the PD configuration are inferred, not measured.
* Vendor BL2 accepting a BL33 signed with the default CIX OEM key is expected
  (the vendor BL33 uses that key) but unverified on hardware.
* LNCL (Linlon display cluster, new in CIX 2026Q3) is present in this DSDT and
  absent from the vendor's; vendor kernels ignore it.
