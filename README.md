<img src="assets/icon.svg" width="64" height="64" alt="">

# edk2-cix-sky1

UEFI firmware (EDK2) for CIX P1 / Sky1 (CD8180) boards, built from one tree
on top of the CIX 2026Q3 BSP and booting operating systems through **ACPI**.
On the Radxa Orion O6 and O6N it can also build all of the firmware that runs
before UEFI from source (`--open`, [below](#open-boot-firmware)).

| Board | ID | Board package | Board support from | Signing chain |
|---|---|---|---|---|
| Radxa Orion O6 | `orion-o6` | `edk2-project/Platform/Radxa/Orion/O6` | Radxa `cix_p1_dev_2026q2` | A (public keys) |
| Radxa Orion O6N | `orion-o6n` | `edk2-project/Platform/Radxa/Orion/O6N` | Radxa `cix_p1_dev_2026q2` | A (public keys) |
| Minisforum MS-R1 | `ms-r1` | `edk2-project/Platform/CIX/Sky1/MGP1WSB` | Minisforum `cix_p1_mg_mp_dev`, rebased onto 2026Q3 | B |
| Orange Pi 6 Plus | `opi6plus` | `edk2-project/Platform/CIX/Sky1/OPI6Plus` | reconstructed from the Orange Pi firmware | B |
| Orange Pi 6 | `opi6` | `edk2-project/Platform/CIX/Sky1/OPI6` | reconstructed from the Orange Pi firmware | B |

The signing chain decides what can be rebuilt: on chain A, CIX has published
the private keys, so everything before UEFI can be rebuilt and re-signed. On
chain B the keys are fused and not public, so only BL33 (UEFI) can be
replaced ([docs/open-firmware.md](docs/open-firmware.md#which-boards)).

> **Status:** every board builds and packages, and the images are checked
> statically (flash layout, vendor regions, ACPI). **Boot-tested on hardware so
> far: Orange Pi 6 Plus, Orange Pi 6, Radxa Orion O6N and Minisforum MS-R1**
> (MS-R1: Secure Boot and TPM under Windows 11). None of the boot firmware
> built from source (`--open`, `--open-ec`, `--boot-firmware cix`) has run on
> hardware yet; it has been checked only in emulators. Keep a way to recover
> (SPI programmer or the vendor's recovery method) before flashing. See
> [docs/validation.md](docs/validation.md).

## What is in here

* **CIX 2026Q3** `edk2`, `edk2-platforms` and `edk2-non-osi` as pinned
  submodules ([manifests/pins.json](manifests/pins.json)).
* **One firmware for every board**: all five boards build from one shared
  DSC/FDF ([Sky1Board.dsc.inc](edk2-project/Platform/CIX/Sky1/Sky1Board.dsc.inc),
  CIX 2026Q3 plus the patch queue) with the same features. A board package in
  `edk2-project/` only describes its hardware ([docs/parity.md](docs/parity.md)).
  Every board has the CIX setup interface, the network stack (PXE/HTTP boot,
  Realtek RTL8125/8126/8168 drivers; off until enabled in setup), Microsoft's
  Secure Boot keys as defaults, ACPI or device-tree boot (DTB from disk),
  FPDT and SPCR tables, the x86 option-ROM emulator for graphics cards and the
  boot watchdog. Firmware is updated by flashing the image; there are no
  capsules.
* **A patch queue** for CIX code in [patches/2026Q3/edk2-platforms](patches/2026Q3/edk2-platforms)
  (and two for EDK2 core in [patches/2026Q3/edk2](patches/2026Q3/edk2), for
  the MS-R1 Secure Boot defaults and the TPM on the Secure Boot page). It has
  the DSDT board-configuration guards the Orange Pi boards use, and fixes
  that apply to every board, most of them from
  [Neol00/edk2-cix-unlocked](https://github.com/Neol00/edk2-cix-unlocked):
  * setup-variable stack overrun after a firmware downgrade
  * setup variable left by the vendor firmware (different layout) reset to
    the defaults instead of being read at the wrong offsets
  * unassigned USB-PD alert pins (0xFF) registered as GPIO interrupts
  * boot watchdog raised from 20 s to 90 s (no false resets on slow boots)
  * SCMI shared memory `reg-io-width` (SCMI/DVFS/GPU on Linux ≥ 6.13)
  * PCIe x4 `bus-range` matching `_CRS`
  * SMMU HTTU (hardware access/dirty) advertised in IORT
  * root bridges without I/O space no longer advertise a fake I/O window
  * Linux efi-pstore crash dumps swept before they fill the variable store
  * **PCIe 64-bit window** setup option (Auto / 16 / 32 / 64 / 128 GB) for
    Port0 (the x8 slot on the O6 and MS-R1), so GPUs with 16 GB or 24 GB of
    VRAM can use Resizable BAR; Auto sizes it from the installed card
  * **manual ASPM selection** per root port, as in Minisforum's firmware
    (ASPM Max Support: Disable / L0s / L1 / L0sL1)
  * Port0 shown as x4 (no X8 width option) on the O6N and the Orange Pi
    boards, where the x8 root port drives an x4 M.2 slot
* **Secure Boot and TPM** (tested with Windows 11 on MS-R1; Linux untested):
  Microsoft's PK/KEK/db as the Secure Boot defaults on every board
  ([SecureBootKeys](edk2-project/Platform/CIX/Sky1/SecureBootKeys)), and TPM
  2.0 measured boot with a discrete TPM on MS-R1's dTPM header (ASUS TPM-SPI,
  Nuvoton NPCT750). The TPM appears on the Secure Boot Configuration page and
  is published to the OS as a CRB TPM driven from AML, so Windows' inbox
  driver uses it. To add an SPI TPM to another board, set `SPI_TPM_ENABLE`
  and the bus/chip-select PCDs ([parity](docs/parity.md#spi-tpm)).
* **One boot firmware for every board**: `--boot-firmware cix` gives every
  board the same CIX 2026Q3 SE, CSU_PM, BL2, BL31 and OP-TEE, signed for the
  board's chain ([docs/parity.md](docs/parity.md#boot-firmware-se-csu_pm-bl2-bl31-op-tee-stmm)).
* **Open boot firmware** for the chain-A boards ([below](#open-boot-firmware)):
  the SE firmware (DRAM training included), CSU_PM, TF-A BL2/BL31, OP-TEE
  with StandaloneMM and the sensor fusion hub firmware are built from source,
  plus the Orion O6's embedded controller firmware.
* **Scripts** that fetch and verify vendor binaries, build, sign, package and
  check the images, and an open replacement for CIX's image tools: SPI image
  packer, FIP verifier, bootloader1 re-signing ([scripts/](scripts),
  [scripts/open](scripts/open)).
* **Documentation** of the closed firmware and the SoC: what each binary is
  ([closed firmware](docs/closed-firmware.md), including the security issues
  found in CIX's boot firmware) and the eFuses ([docs/efuses.md](docs/efuses.md)).

## Building

Requirements: git, Docker (Docker Desktop, Colima, or Docker on Linux), on an
arm64 or x86_64 host. The build runs inside a Debian 12 container with the Arm
GNU toolchains (AArch64 and arm-none-eabi), Espressif's Xtensa GCC for
CSU_PM, SDCC for the EC and ACPICA R2024_12_12
([docker/Dockerfile](docker/Dockerfile)). `build.sh` builds the container
whenever the Dockerfile changes.

```sh
git clone --recurse-submodules https://github.com/HeyMeco/edk2-cix-sky1.git
cd edk2-cix-sky1

./build.sh --list                    # boards
./build.sh orion-o6 ms-r1            # one or more boards
./build.sh --all                     # every board
```

Each board produces one file:

```
dist/<board>_<version>.bin     e.g. dist/orion-o6_2026Q3.bin
```

This is the complete SPI flash image, padded with 0xFF to exactly 8 MiB. The
version defaults to the CIX BSP release (`bios_version` in
[manifests/pins.json](manifests/pins.json), currently `2026Q3`) and is also the
firmware version string shown in setup. Intermediate files, the build log and
`manifest.json` (what each region came from, with hashes) stay in
`output/<board>/<target>/`. On Apple Silicon the container runs natively
(arm64) and a board takes about two minutes.

### Options

| Option | What it does |
|---|---|
| `--list` | list the boards and exit |
| `--all` | build every board in `boards/` |
| `--target RELEASE\|DEBUG` | EDK2 build target (default `RELEASE`); with `--open` also TF-A/OP-TEE debug builds. A DEBUG image is named `…_debug.bin` |
| `--version V` | version string for file names and setup (default: `bios_version`, e.g. `--version 2026Q3-2`) |
| `--jobs N` | parallel build jobs (default: all CPUs) |
| `--extras` | also write `dist/extras/<board>_<version>_extras.zip`: the unpadded image, the BL33-only OTA image (`cix_flash_ota.bin`), the raw FD and `manifest.json` |
| `--image KEY=PATH\|URL` | vendor firmware image for `fetch-blobs.py` (see [Orange Pi vendor images](#orange-pi-vendor-images)) |
| `--skip-prepare` | do not run `scripts/prepare.py` (submodule pins, sparse checkout, patch queue) |
| `--boot-firmware vendor\|cix` | BL1/BL2/bootloader2 from the board vendor (default, `boot_firmware` in `boards/<board>.yaml`) or CIX 2026Q3 for the board's chain; image `…_cixboot.bin` |
| `--open` | chain A (O6, O6N) only: all firmware before UEFI from source and re-signed with CIX's published keys; image `…_open.bin` ([below](#open-boot-firmware)) |
| `--vendor-se` | with `--open`: keep CIX's SE firmware in bootloader1; image `…_open_cixse.bin` |
| `--cix-ddr` | with `--open`: the open SE, but with CIX's DDR driver (memory training) linked unchanged from their debug build in place of `firmware/se/ddr`; image `…_open_cixddr.bin` |
| `--vendor-pm` | with `--open`: keep CIX's CSU_PM firmware in bootloader1; image `…_open_cixpm.bin` |
| `--vendor-stmm` | with `--open`: embed CIX's StandaloneMM instead of the one built from source; image `…_open_cixstmm.bin` |
| `--open-ec` | Orion O6 only, with or without `--open`: the EC firmware from `firmware/ec` in place of Radxa's; image `…_openec.bin` |

The suffixes combine, e.g. `orion-o6_2026Q3_open_cixddr_openec_debug.bin` or
`orion-o6n_2026Q3_open_cixpm_cixstmm.bin`, so the images of a bisection sit
side by side in `dist/`.
`EDK2CIX_NO_DOCKER=1 ./build.sh …` runs the build directly, inside an
environment that is already set up like the container.

### Open boot firmware

On the Orion O6 and O6N, `--open` builds everything that runs before UEFI
from source, apart from the boot ROM in the chip:

| Stage | Source | Checked against CIX's |
|---|---|---|
| SE firmware (Cortex-M33: SoC bring-up, eFuses, DRAM training, secure boot, run-time services) | [firmware/se](firmware/se), translated from CIX's debug build | same register writes and memory contents in [test_se.py](scripts/open/test_se.py): cold boot, DDR, services, suspend/resume, secure boot |
| CSU_PM (Xtensa LX7: clocks, PMICs, DVFS, SCMI, fans, suspend) | [firmware/csupm](firmware/csupm): Arm SCP-firmware v2.11.0, FreeRTOS and CIX's code translated | [`sky1emu pm-test`](emu/README.md): boot, SCMI, PM messages, suspend/resume, run-time DPM, fans, every board's PM configuration |
| TF-A BL2, BL31 | `arm-trusted-firmware` + [patches](patches/2026Q3) | — |
| OP-TEE with StandaloneMM (UEFI variables) | `optee_os` + EDK2 [StandaloneMm](edk2-project/Platform/CIX/Sky1/StandaloneMm) | — |
| Sensor fusion hub firmware | [firmware/sfh](firmware/sfh) | [test_sfh.py](scripts/open/test_sfh.py) |
| EC firmware (Orion O6, `--open-ec`) | [firmware/ec](firmware/ec), written from scratch (ITE IT5570E) | `sky1emu ec-test` ([emu](emu/README.md#ec-orion-o6)), against Radxa's V1.07 |

```sh
./build.sh orion-o6 --open                  # dist/orion-o6_2026Q3_open.bin
./build.sh orion-o6n --open --target DEBUG  # TF-A, OP-TEE debug logs
./build.sh orion-o6 --open --open-ec        # and the EC firmware from source
```

None of this has booted on hardware yet. If an open image does not boot,
rebuild with CIX's part put back to find the stage at fault:

| Symptom (UARTs at 115200: UART5 SE, UART4 CSU_PM, UART2 AP) | Rebuild with |
|---|---|
| UART5 silent | `--vendor-se` |
| SE stops in memory training, or DRAM misbehaves | `--cix-ddr` (boots: the fault is in `firmware/se/ddr`) |
| SE waits for the PM, UART4 silent | `--vendor-pm` |
| UEFI variables misbehave | `--vendor-stmm` |

The full picture: what is open and how it differs from CIX's
([docs/open-firmware.md](docs/open-firmware.md), with a
[debugging](docs/open-firmware.md#debugging) section), and per component
the READMEs in [firmware/se](firmware/se/README.md),
[firmware/csupm](firmware/csupm/README.md) and
[firmware/ec](firmware/ec/README.md).

### Emulator tests

The open firmware is checked against CIX's (or Radxa's) binaries in
emulators of the SoC, run on the host rather than in the container. The
CSU_PM's and the Orion O6 EC's emulators and their tests are the Rust
crate [emu](emu/README.md) (`sky1emu`, no dependencies beyond a Rust
toolchain; the EC's with `--features ec`); the SE's and SFH's are still
Python in `scripts/open`, and need `unicorn` 2.1.4,
`capstone` and `pyelftools` (PyPy runs them much faster than CPython). CIX's
debug builds they compare against are pinned in
[manifests/cix-debug-firmware.json](manifests/cix-debug-firmware.json) and
fetched by `scripts/open/cix_debug.py`.

```sh
pypy3 scripts/open/test_se.py output/orion-o6/RELEASE/open/se/se_fw.elf dist/orion-o6_2026Q3_open.bin --ddr
pypy3 scripts/open/test_se.py <se_fw.elf> <image.bin> --services --suspend
pypy3 scripts/open/test_se.py <se_fw.elf> <image.bin> --secure
emu/test-pm.sh <csupm.elf>                     # CSU_PM: every suite, every board
pypy3 scripts/open/test_sfh.py <sfh_fw.bin>    # sensor fusion hub
emu/target-ec/release/sky1emu ec-test --open firmware/ec/build/ec_fw.bin   # Orion O6 EC (cargo build --release --manifest-path emu/Cargo.toml --features ec --target-dir emu/target-ec)
```

### Orange Pi vendor images

The files in `vendor/orangepi/` were cut from these Orange Pi images. To
regenerate them (e.g. for a newer vendor release, after updating the hashes in
`manifests/`):

```sh
python3 scripts/fetch-blobs.py opi6plus opi6 --from-vendor \
    --image opi6plus=OPI6PLUS_BIOS_1.4_General.bin \
    --image opi6=opi6_bios_image_20261019.img     # run inside the container (needs mtools)
```

| Key | File | SHA-256 |
|---|---|---|
| `opi6plus` | `OPI6PLUS_BIOS_1.4_General.bin` | `acf957ac8af7c391e0ee6296dd748504dde3d6b36254210ca60c93c2c16c649a` |
| `opi6` | `opi6_bios_image_20261019.img` (contains `bios_v1.0.bin`) | `bfbffe87c842bcf81c11c2384b0334eb175994e91dfbea84ec1c7b8f183280a7` |

## Vendor binaries

Unless built from source (`--open`, `--open-ec`), the boot firmware and other
non-UEFI regions are the vendors'. They come from three places, all verified
against the SHA-256 hashes in [manifests/blobs](manifests/blobs):

* Radxa BL1/BL2, Radxa and Minisforum TrustZone/se_config and EC firmware are
  downloaded at build time from the vendors' public git trees and releases.
* The Orange Pi BL1/BL2, EC firmware, memory/PM configuration and board SSDT
  are not published anywhere except inside Orange Pi's firmware images. The
  pieces the build needs were extracted once and are stored in
  [vendor/orangepi](vendor/orangepi) (see [provenance](docs/provenance/orangepi.md)).
* The MS-R1 BL1/BL2 are taken the same way from Minisforum's retail firmware
  and stored in [vendor/minisforum](vendor/minisforum). The ones in
  Minisforum's public `edk2-non-osi` tree are signed with a different root key
  and do not boot retail boards.

`scripts/compare-image.py REFERENCE.bin BUILT.bin` compares an image with the
vendor image it replaces, region by region.

## Releases (GitHub Actions)

* **CI** ([.github/workflows/ci.yml](.github/workflows/ci.yml)) builds every
  board, plus the O6 and O6N with `--open`, on every push and pull request.
* **Release** ([.github/workflows/release.yml](.github/workflows/release.yml))
  is started by hand from the *Actions* tab ("Run workflow"). It builds every
  board (or a chosen subset), attaches `<board>_<version>.bin` for each board
  plus `SHA256SUMS` to a new GitHub release, and tags it with the version you
  enter (default `2026Q3`; use e.g. `2026Q3-2` for a rebuild). Releases carry
  the vendor boot firmware; open images are built locally.

No secrets or extra inputs are needed.

Builds run on GitHub's `ubuntu-24.04-arm` runners (free for public
repositories). For a private repository choose `ubuntu-24.04` in the release
form; the container then uses the x86_64 toolchain and CIX tools.

## Flashing

`<board>_<version>.bin` is the whole 8 MiB flash. Writing it resets the UEFI
settings (the firmware re-initialises its variable store on first boot).

* From a UEFI shell: `FlashUpdate.efi -f orion-o6_2026Q3.bin` (CIX tool from
  `edk2-non-osi/Platform/CIX/Sky1/FlashTool`, also shipped by the vendors).
* With an SPI programmer: the flash is 1.8 V; write the file at offset 0
  (e.g. `flashrom -p ch341a_spi -w orion-o6_2026Q3.bin`).

For updates without touching the rest of the flash, `--extras` provides the
BL33-only `cix_flash_ota.bin`.

Images are signed like the vendor firmware: BL33 with the default CIX OEM
key on every board, and with `--open` the pre-UEFI stages with CIX's
published chain-A keys ([manifests/cix-keys.json](manifests/cix-keys.json)).
No firmware built here programs eFuses.

## Repository layout

```
boards/*.yaml                board registry (DSC, package, flash layout, blobs, signing chain)
docker/Dockerfile            build container
edk2, edk2-platforms,        CIX 2026Q3 submodules
edk2-non-osi
arm-trusted-firmware,        TF-A, OP-TEE and mbed TLS submodules (--open)
optee_os, mbedtls
edk2-project/Platform/       board packages, shared Sky1 board body, StandaloneMm
firmware/se/                 open SE firmware (Cortex-M33), DDR driver in ddr/
firmware/csupm/              open CSU_PM firmware (Xtensa; SCP-firmware, FreeRTOS submodules)
firmware/sfh/                open sensor fusion hub firmware
firmware/ec/                 open Orion O6 EC firmware (IT5570E, SDCC)
manifests/                   pins, vendor image and blob hashes, CIX keys and debug builds, imports
vendor/orangepi/             Orange Pi blobs extracted from the vendor images
vendor/minisforum/           MS-R1 retail BL1/BL2
patches/<release>/           patch queues per CIX release, applied by scripts/prepare.py
scripts/build.py             build + sign + package + check
scripts/prepare.py           submodule pins, sparse checkout, patches
scripts/fetch-blobs.py       vendor binaries
scripts/compare-image.py     region-by-region image comparison
scripts/imports.py           provenance of the Radxa / Minisforum imports
scripts/open/                open builds (trusted.py), image tools (cixfw.py), emulators and tests
scripts/extract/             vendor image tooling (FV parser, pin tables, HII)
scripts/acpi/                ACPI device-level comparison
docs/                        architecture, parity, open and closed firmware, eFuses, validation
```

More detail: [architecture](docs/architecture.md),
[parity](docs/parity.md), [open firmware](docs/open-firmware.md),
[closed firmware](docs/closed-firmware.md), [eFuses](docs/efuses.md),
[validation](docs/validation.md),
[Orange Pi provenance](docs/provenance/orangepi.md).

## Licensing

First-party code, the open firmware and the board packages are
BSD-2-Clause-Patent (see [LICENSE](LICENSE) and the SPDX headers). The
submodules keep their own licenses. The open SE and CSU_PM firmware are
translated from CIX's publicly posted debug builds; each source file names
the CIX file it translates. Vendor binaries remain under their
vendors' terms: Radxa, Minisforum and CIX binaries are fetched from the
vendors' public repositories at build time. The Orange Pi binaries in
`vendor/orangepi/` and the MS-R1 BL1/BL2 in `vendor/minisforum/` were
extracted from the vendors' firmware images, for which no explicit
redistribution license was found. Release archives contain these binaries as
part of the flash images, as the vendor firmware does.
