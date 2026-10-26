# Architecture

## Source layers

| Layer | Location | Notes |
|---|---|---|
| EDK2 core | `edk2/` | CIX fork, 2026Q2 RC4 (`9212cf39`) |
| SoC and CIX boards | `edk2-platforms/` | CIX 2026Q3 RC2.3 (`e5cb70f6`) + `patches/edk2-platforms/` |
| Binary modules, packaging tools, keys | `edk2-non-osi/` | CIX 2026Q3 RC2.3 (`e725a1d3`) |
| Board packages | `edk2-project/` | this repository |

`PACKAGES_PATH` is

```
edk2-project : edk2 : edk2-platforms : edk2-non-osi : edk2-platforms/Silicon/Intel
```

BaseTools resolve a workspace-relative path by taking the first package path
that contains the file, so `edk2-project` wins over the CIX tree. CIX 2026Q3
also ships an older copy of `Platform/Radxa`; `scripts/prepare.py` hides it
with a sparse checkout of `edk2-platforms` (`!Platform/Radxa`) so that every
`Platform/Radxa/...` path resolves to the imported Radxa package. The
`Silicon/Intel` entry provides `IntelSiliconPkg`, which the Radxa package uses.

## Board packages

### Radxa Orion O6 / O6N

`edk2-project/Platform/Radxa` is a snapshot of `Platform/Radxa` from
`radxa/edk2-platforms` at `14cf7bdc` (`cix_p1_dev_2026q2`), which builds
against the CIX 2026Q3 shared code unchanged. Local changes (all listed in
`manifests/imports.json`):

* DSC include path for `LinuxAcpiConfig.h` (Radxa used `$(WORKSPACE)/..`)
* flash layouts copied from `radxa/edk2-non-osi` `4e99786e` into the packages
* `RadxaSetupVariableInitDxe` stack overrun fix
* `pm_config` honours `SOURCE_DATE_EPOCH`

BL1, BL2 and the TrustZone configuration come from `radxa/edk2-non-osi`
`4e99786e`; the O6 EC firmware from `radxa/edk2-platforms` `14cf7bdc`. The O6N
EC firmware is not in any public tree; it is taken from Radxa's 1.3.1 release
package (`edk2-cix_1.3.1_all.deb`), byte-identical to what Radxa ships.

### Minisforum MS-R1 (`MGP1WSB`)

Minisforum's board lives on a tree that branched from tianocore in 2022 and
carries its own copies of several CIX drivers. The package was rebased onto
CIX 2026Q3 rather than copied:

* `MGP1WSB.dsc`/`.fdf` regenerated as CIX 2026Q3 `Merak.dsc`/`.fdf` plus the
  Minisforum delta (three-way merge of Minisforum-Merak → MGP1WSB onto Q3
  Merak), keeping MS-R1 choices: no EC, no GMAC, no EC power button,
  `SPI_VARIABLE_BASE` `0x380000`, HDA enabled, I2S 5/6/8 only.
* `PlatformSetupVariableInitDxe` (identical to Minisforum's shared copy) is
  replaced by the CIX 2026Q3 driver.
* `SetupManagerDxe`, `PlatformBootManagerLib`, `PlatformEnvHookLib`: three-way
  merged; the PCIe menu follows Q3's per-port layout; hardware info shows the
  memory frequency and PMIC version from Q3 and the MS-R1 memory-chip table.
* ACPI and SMBIOS: Minisforum's own files, plus shared PD interrupts.

BL1, BL2 and `se_config.bin` come from
`minisforum-cix-p1-repo/cix_opensource__release__edk2-non-osi` `c22fb43e`.

### Orange Pi 6 Plus / Orange Pi 6

Orange Pi has not published UEFI source. Both vendor images are CIX Merak
builds (CIX 2025Q4 SDK for the OPI6, an older SDK for the Plus) with board
changes, so the packages are CIX 2026Q3 Merak plus the board configuration
recovered from the vendor images ([provenance](provenance/orangepi.md)):

| Item | Source |
|---|---|
| DSC / FDF | Q3 `Merak.dsc`/`.fdf`, board PCDs from the vendor setup forms and PD driver |
| Pin multiplexing, GPIO init | `Library/PinMuxTableLib`, `GpioTableLib`, generated from the tables in the vendor `PlatformDxe` (`scripts/extract/gen_iolibs.py`) |
| Platform hooks | Plus: Merak's; OPI6: Merak's without the EC-driven hooks, as in the vendor image |
| DSDT | CIX 2026Q3 source; board differences through `ACPI/LinuxAcpiConfig.h` and the guards added by `patches/edk2-platforms/0001-*` |
| SSDT | the vendor table, extracted from the vendor image (`vendor/orangepi/<board>/Ssdt.aml`) |
| BL1, BL2, EC, memory and PM configuration | the vendor image regions (`vendor/orangepi/<board>/`), verified by hash |
| SMBIOS | Merak's, with Orange Pi names |

### Boot firmware and `pm_config` per board

| Board | BL1 / BL2 | BL1 SHA-256 | `pm_config` | Format |
|---|---|---|---|---|
| Orion O6 | `radxa/edk2-non-osi` `4e99786e` | `fcddd093…` | built from `O6/pm_config` | v3.0 |
| Orion O6N | `radxa/edk2-non-osi` `4e99786e` | `fcddd093…` (same as O6) | built from `O6N/pm_config` | v3.0 |
| MS-R1 | Minisforum `edk2-non-osi` `c22fb43e` | `e6de7d0f…` | built from `MGP1WSB/pm_config` | v3.1 |
| Orange Pi 6 | vendor image (`vendor/orangepi/opi6/`) | `ed15fa1e…` | vendor binary | v3.2 |
| Orange Pi 6 Plus | vendor image (`vendor/orangepi/opi6plus/`) | `e8162901…` | vendor binary | v3.0 |

The format is `PM_CONFIG_VERSION_MAJOR/MINOR` in the board's
`pm_export_config.h`, or the first four bytes of `csu_pm_config.bin`
(`0300 MMMM`). For comparison, the CIX `edk2-non-osi` default binary is v3.1
and the CIX source in `edk2-platforms/.../Alcor/pm_config` is v3.4; no board
uses either.

The source-built boards carry no OPP table (`PM_OPP_TABLE_CONFIG 0`), and
the Orange Pi binaries have not been decoded that far, so CPU, GPU and NPU
frequency levels are whatever the SCP firmware reports over SCMI; `AcpiSocDxe` copies the highest and lowest level of each
domain into `_CPC` at boot. The per-domain `opp_max` caps and per-rail EDP
limits are only present in the Orange Pi 6 binary (`opp_max` all
`OPP_NO_LIMIT`); the source-built boards set `PM_PMIC_CONFIG 0`, and the
Orange Pi 6 Plus section is left erased. All of these are the v3.x
firmware generation, not the pm_config v2.1 + older BL1 pairing that
Neol00/edk2-cix-unlocked relies on for its OPP menu.

## PCIe 64-bit window (patch 0010)

The CIX layout gives each root bridge a fixed 16 GB above-4G window
(`0x18_0000_0000` for the x8 slot). A Resizable BAR has to be aligned to its
size, so a 16 GB BAR fills the whole window and the resize fails; 24 GB cards
(32 GB BAR) do not fit at all. `Platform/CIX/Sky1/Library/PcieWindowLib`
(from Neol00/edk2-cix-unlocked) grows only the x8 window, upward from its
fixed base, to 32/64/128 GB. The setup option *PCIe 64-bit Window* picks the
layout; *Auto* (default) reads the Resizable BAR capability of the card behind
each root port and takes the smallest layout that fits with 512 MB to spare
(never 128 GB, which is experimental). The result feeds the host bridge
(MemAbove4G/PMemAbove4G), the ACPI RAM variables `0x4D`–`0x56` read by the
`PCI0-4._CRS` and `PRC0-4._INI` methods, and the device-tree `ranges`.

## Patch queue

`patches/edk2-platforms/*.patch` are `git format-patch` files applied in order
to the pinned `edk2-platforms` by `scripts/prepare.py` (it records a hash of
the queue in the submodule's git directory and re-applies when the queue
changes). To work on them:

```sh
cd edk2-platforms
git checkout -b work                  # on top of the pinned commit
git am --keep-cr ../patches/edk2-platforms/*.patch
# edit, commit
git format-patch --no-signature --zero-commit -N -o ../patches/edk2-platforms <pinned-commit>
git checkout --detach <pinned-commit>
```

## Build flow (`scripts/build.py`)

1. `prepare.py`: submodule commits, sparse checkout, patch queue.
2. `fetch-blobs.py`: stage and verify every blob in `manifests/blobs/<board>.json`
   into the board package (`Firmwares/`, Orange Pi SSDT).
3. EDK2 `build` (`-a AARCH64 -t GCC5`, `ACPI_BOOT_ENABLE=1`, SPI variables,
   StandaloneMM) with the board DSC.
4. Staging as in CIX `build_and_package.sh`: CIX base firmware, board
   `mem_config`/`pm_config`/`trustzone_config` built from source when the
   package has them, board `Firmwares/` overrides, PackageTool binaries for the
   host architecture, keys, board flash layouts.
5. Signing: `cert_uefi_create_rsa` and `fiptool create` → `bootloader3.img`;
   `cix_package_tool` → `cix_flash_all.bin` and `cix_flash_ota.bin`. The
   **x86_64** CIX tools are used on every host (through `qemu-user` inside the
   container on arm64): the AArch64 builds in `edk2-non-osi` sign with an
   RSA-PSS salt length of 0x15E instead of 0x20, which vendor BL2 images
   reject. The trusted-key certificate follows each vendor (`trusted_key_cert`
   in `boards/*.yaml`):

   | Board | `trusted_key_no.crt` |
   |---|---|
   | O6, O6N, OPI6 Plus | pregenerated, CIX root key `1e0b4d…` (`radxa/edk2-non-osi` `certs/`), as in Radxa's releases and the Plus vendor image |
   | MS-R1 | pregenerated, root key `898ddc…` (Minisforum `edk2-non-osi` `certs/`), as Minisforum's packaging script uses |
   | OPI6 | regenerated from the OEM key pair (`cix_regen_trusted_key_cert`), as in the vendor image |

   All of them certify the default CIX OEM key, which signs the UEFI
   certificates; the build checks that every certificate uses a 32-byte salt.
6. Layout check: every firmware-header entry is in the layout, fits its region,
   holds the staged file, and no regions overlap.
7. Capsules (boards with `capsule: true`): `GenerateCapsule` wraps both
   images for the CIX system-firmware FMP (`351c9808-…`), version 5, signed
   with the CIX test certificates from `Platform/CIX/Sky1/Bin`, as CIX
   `GenBiosCap.sh` does. The Radxa firmware has no capsule driver.
8. `manifest.json` (submodule SHAs, patches, tool versions, hashes, layout).
9. `dist/<board>_<version>.bin`: `cix_flash_all.bin` padded with 0xFF to the
   8 MiB flash size; the default version is `bios_version` from
   `manifests/pins.json`. `--extras` adds a zip with the OTA image, capsules,
   FD and manifest.

`BUILD_DATE` and the PM configuration timestamp come from `SOURCE_DATE_EPOCH`
(default: the commit time), so rebuilding a commit on the same host
architecture gives a bit-identical UEFI FD and configuration blobs. arm64 and
x86_64 builds differ (the toolchain and BaseTools are host binaries), and the
signed BL33 container always differs because the CIX certificate tool embeds
the signing time. Releases are built on arm64 runners by default.

## Flash layouts

| Board | BL2 | Mem cfg | PM cfg | se_config | BL33 | Notes |
|---|---|---|---|---|---|---|
| O6 / O6N | `0x288000`+`0x100000` | `0x400000` | `0x404000` | `0x405000` | `0x406000`+`0x229000` | TrustZone cfg at `0xF0000` |
| MS-R1 | `0x288000`+`0xF8000` | `0x3F8000` | `0x3FC000` | `0x3FD000` | `0x3FE000`+`0x402000` | EC area dummy |
| OPI6 / OPI6 Plus | `0x288000`+`0xF8000` | `0x3F8000` | `0x3FC000` | `0x3FD000` | `0x3FE000`+`0x402000` | EC firmware at `0x10000` |

All boards: 8 MiB flash, firmware header `0x100000`, `sfh_fw` `0x101000`, BL1
`0x188000`. `cix_package_tool` stops writing after the last region, which is
why images (vendor and ours) are shorter than 8 MiB.

What each binary region contains and which ones have open-source
counterparts: [closed firmware](closed-firmware.md).
