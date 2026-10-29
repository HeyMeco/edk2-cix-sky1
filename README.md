<img src="assets/icon.svg" width="64" height="64" alt="">

# edk2-cix-sky1

UEFI firmware (EDK2) for CIX P1 / Sky1 (CD8180) boards, built from one tree
on top of the CIX 2026Q3 BSP and booting operating systems through **ACPI**.

| Board | ID | Board package | Board support from |
|---|---|---|---|
| Radxa Orion O6 | `orion-o6` | `edk2-project/Platform/Radxa/Orion/O6` | Radxa `cix_p1_dev_2026q2` |
| Radxa Orion O6N | `orion-o6n` | `edk2-project/Platform/Radxa/Orion/O6N` | Radxa `cix_p1_dev_2026q2` |
| Minisforum MS-R1 | `ms-r1` | `edk2-project/Platform/CIX/Sky1/MGP1WSB` | Minisforum `cix_p1_mg_mp_dev`, rebased onto 2026Q3 |
| Orange Pi 6 Plus | `opi6plus` | `edk2-project/Platform/CIX/Sky1/OPI6Plus` | reconstructed from the Orange Pi firmware |
| Orange Pi 6 | `opi6` | `edk2-project/Platform/CIX/Sky1/OPI6` | reconstructed from the Orange Pi firmware |

> **Status:** every board builds and packages, and the images are checked
> statically (flash layout, vendor regions, ACPI). **Only the Orange Pi 6 Plus has been
> boot-tested on hardware so far.** Keep a way to recover (SPI programmer or the
> vendor's recovery method) before flashing. See [docs/validation.md](docs/validation.md).

## What is in here

* **CIX 2026Q3** `edk2`, `edk2-platforms` and `edk2-non-osi` as pinned
  submodules ([manifests/pins.json](manifests/pins.json)).
* **Board packages** in `edk2-project/`, which takes precedence over
  `edk2-platforms` in `PACKAGES_PATH`.
* **A patch queue** for CIX code in [patches/2026Q3/edk2-platforms](patches/2026Q3/edk2-platforms):
  DSDT board-configuration guards used by the Orange Pi boards, plus fixes
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
  * **PCIe 64-bit window** setup option (Auto / 16 / 32 / 64 / 128 GB) for the
    x8 slot, so GPUs with 16 GB or 24 GB of VRAM can use Resizable BAR; Auto
    sizes it from the installed card
* **Signed UEFI capsules** (`.cap`) for the boards whose firmware supports
  capsule updates (MS-R1, Orange Pi 6, Orange Pi 6 Plus).
* **Scripts** that fetch and verify vendor binaries, build, sign, package and
  check the images ([scripts/](scripts)).

Vendor binaries come from three places, all verified against the SHA-256
hashes in [manifests/blobs](manifests/blobs):

* Radxa BL1/BL2, Radxa and Minisforum TrustZone/se_config and EC firmware are
  downloaded at build time from the vendors' public git trees and releases.
* The Orange Pi BL1/BL2, EC firmware, memory/PM configuration and board SSDT
  are not published anywhere except inside Orange Pi's firmware images; the
  pieces needed for the build were extracted once and are stored in
  [vendor/orangepi](vendor/orangepi) (see [provenance](docs/provenance/orangepi.md)).
* The MS-R1 BL1/BL2 are taken the same way from Minisforum's retail firmware
  and stored in [vendor/minisforum](vendor/minisforum): the ones in
  Minisforum's public `edk2-non-osi` tree are signed with a different root key
  and do not boot retail boards.

## Building locally

Requirements: git, Docker (Docker Desktop, Colima, or Docker on Linux), on an
arm64 or x86_64 host. The build runs inside a Debian 12 container with the Arm
GNU toolchain 10.2-2020.11 and ACPICA R2024_12_12 ([docker/Dockerfile](docker/Dockerfile)).

```sh
git clone --recurse-submodules https://github.com/HeyMeco/edk2-cix-sky1.git
cd edk2-cix-sky1

./build.sh --list                    # boards
./build.sh orion-o6 ms-r1            # one or more boards
./build.sh --all                     # every board
```

The result is one file per board:

```
dist/<board>_<version>.bin     e.g. dist/orion-o6_2026Q3.bin
```

the complete SPI flash image, padded with 0xFF to exactly 8 MiB. The version
defaults to the CIX BSP release (`bios_version` in
[manifests/pins.json](manifests/pins.json), currently `2026Q3`) and is also the
firmware version string shown in setup.

`--extras` additionally writes `dist/extras/<board>_<version>_extras.zip` with
the unpadded image, the BL33-only OTA image, the signed UEFI capsules (MS-R1,
Orange Pi), the raw FD and `manifest.json`. Intermediate files and the build
log stay in `output/<board>/RELEASE/`.

Options: `--target DEBUG`, `--version 2026Q3-2`, `--extras`, `--jobs N`. On Apple Silicon
the container runs natively (arm64) and takes about two minutes per board.

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

## Releases (GitHub Actions)

* **CI** ([.github/workflows/ci.yml](.github/workflows/ci.yml)) builds the
  boards on every push and pull request.
* **Release** ([.github/workflows/release.yml](.github/workflows/release.yml))
  is started by hand from the *Actions* tab ("Run workflow"). It builds every
  board (or a chosen subset), attaches `<board>_<version>.bin` for each board
  plus `SHA256SUMS` to a new GitHub release, and tags it with the version you
  enter (default `2026Q3`; use e.g. `2026Q3-2` for a rebuild).

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
BL33-only `cix_flash_ota.bin` and, on MS-R1 and the Orange Pi boards, signed
UEFI capsules (`*.cap`, e.g. for `CapsuleApp.efi`).

All images are signed with the default CIX OEM key, like the vendor firmware.

## Repository layout

```
boards/*.yaml                board registry (DSC, package, flash layout, blobs)
docker/Dockerfile            build container
edk2, edk2-platforms,        CIX 2026Q3 submodules
edk2-non-osi
edk2-project/Platform/       board packages
manifests/                   pins, vendor image and blob hashes, import provenance
vendor/orangepi/             Orange Pi blobs extracted from the vendor images
patches/<release>/            patch queues per CIX release, applied by scripts/prepare.py
scripts/build.py             build + sign + package + check
scripts/prepare.py           submodule pins, sparse checkout, patches
scripts/fetch-blobs.py       vendor binaries
scripts/imports.py           provenance of the Radxa / Minisforum imports
scripts/extract/             vendor image tooling (FV parser, pin tables, HII)
scripts/acpi/                ACPI device-level comparison
docs/                        architecture, provenance, closed firmware, validation
```

More detail: [docs/architecture.md](docs/architecture.md),
[docs/provenance/orangepi.md](docs/provenance/orangepi.md),
[docs/closed-firmware.md](docs/closed-firmware.md),
[docs/validation.md](docs/validation.md).

## Licensing

First-party code and the board packages are BSD-2-Clause-Patent (see
[LICENSE](LICENSE) and the SPDX headers). The submodules keep their own
licenses. Vendor binaries remain under their vendors' terms: Radxa,
Minisforum and CIX binaries are fetched from the vendors' public repositories
at build time; the Orange Pi binaries in `vendor/orangepi/` and the MS-R1
BL1/BL2 in `vendor/minisforum/` were extracted from the vendors' firmware
images, for which no explicit redistribution license was found. Release archives contain these binaries as
part of the flash images, as the vendor firmware does.
