# Open firmware before BL33

Goal: build, debug and modify everything that runs before UEFI (BL33) from
source. This page tracks what is open, how to build it, what is still
missing and in which order to close the gaps. What each binary is and where it
came from is in [closed firmware](closed-firmware.md).

## Boot chain and status

```
mask ROM ─▶ SE firmware ─▶ CSU_PM ─▶ TF-A BL2 ─▶ BL31 + OP-TEE (StMM) ─▶ BL33 UEFI
            └──────────── bootloader1 ───────────┘ └──── bootloader2 ────┘   bootloader3
```

| Stage | Source | Status here |
|---|---|---|
| SE firmware (Cortex-M: DRAM training, eFuse, image checks) | none | binary; not a realistic target |
| CSU_PM (Xtensa, Arm SCP-firmware based) | framework only | binary |
| TF-A BL2 | `arm-trusted-firmware` + patch 0002 | **built from source** (`--open`, chain A), re-signed into bootloader1 |
| TF-A BL31 | `arm-trusted-firmware` + patches 0004-0007 | **built from source** (`--open`) |
| OP-TEE BL32 | `optee_os` + patch queue | **built from source** (`--open`) |
| StandaloneMM inside OP-TEE | EDK2 + [Sky1 StandaloneMm](../edk2-project/Platform/CIX/Sky1/StandaloneMm) | **built from source** (`--open`; `--vendor-stmm` embeds CIX's instead) |
| FIP certificates, `fiptool` | TF-A `cert_create` (patched), `fiptool` | **open** for bootloader2 and bootloader3 |
| bootloader1 container (`CIXBTFF!`) | — | **open**: parse, verify, replace, re-sign (`cixfw.py bl1`) |
| SPI image packer (`cix_package_tool`) | — | **open**: `cixfw.py pack`, used by every build |
| `memory_config`, `pm_config` | CIX generators | built from source where the board package has them |
| `trustzone_config` | CIX generator | CIX stock binary on every board; identical to the generator's output apart from its timestamp ([below](#trustzone-configuration)) |

With `--open` on an Orion O6/O6N, the only CIX-built code left before UEFI
is the Cortex-M and Xtensa firmware (SE and CSU_PM in bootloader1,
`sfh_fw.bin`). Everything on the application cores up to and beside BL33
(TF-A, OP-TEE, StandaloneMM) is built here.

## Which boards

Only boards whose BL2 trusts keys CIX has published can run code built here
before BL33 (`signing_chain` in `boards/<board>.yaml`; every board can still
run the same CIX-built boot firmware, see [parity](parity.md)):

| Chain | Boards | BL1 key 1 / key 2 | bootloader2 root | Private keys |
|---|---|---|---|---|
| A | Orion O6, Orion O6N (CIX `Firmwares/`, Minisforum's public tree) | `c6d0e93f` / `5672dfd9` | `33d47f74` | public |
| B | Orange Pi 6, Orange Pi 6 Plus, MS-R1 retail (CIX `Firmwares2/`) | `fb3f60e4` / `fb3f60e4` | `0d999213` | not public |

Key identities: BL1 keys as SHA-256 over the 384-byte modulus, the FIP root
as SHA-256 over the DER public key (prefixes). CIX published the private keys
of chain A in `cixtech/edk2-non-osi` at `94368c7c`
(`Platform/CIX/Sky1/PackageTool/Keys`): `oem_privatekey.pem` is BL1 key 1,
`cix_privatekey.pem` is BL1 key 2 and the bootloader2 root of trust. They are
pinned by hash in [manifests/cix-keys.json](../manifests/cix-keys.json) and
fetched at build time.

On the chain-B boards only BL33 can be replaced: the retail MS-R1 already
showed that these chips reject a chain-A BL1 (they stay silent). The BL33
FIP on every board is rooted at `0d999213` (the pregenerated
`trusted_key_no.crt`, NV counter 223), separately from bootloader2.

## Building

```sh
./build.sh orion-o6 --open          # dist/orion-o6_2026Q3_open.bin
./build.sh orion-o6n --open --boot-firmware cix
./build.sh orion-o6 --open --target DEBUG
```

`--open` builds, with [scripts/open/trusted.py](../scripts/open/trusted.py):

1. BL31: `make PLAT=sky1 SPD=opteed bl31` in `arm-trusted-firmware` (CIX
   `cix_p1_k6.6_2025q3_tfa_open_dev`, TF-A v2.7 + `plat/cix`, with the
   patch queue).
2. StandaloneMM: `build -p Platform/CIX/Sky1/StandaloneMm/Sky1StandaloneMm.dsc`
   (see [below](#standalonemm)). With `--vendor-stmm` the build instead
   decompresses CIX's image from the OP-TEE in the reference bootloader2.
3. OP-TEE: `make PLATFORM=cix-sky1 CFG_ARM64_core=y ta-targets=ta_arm64
   CFG_STMM_PATH=<StMM FV>` in `optee_os` (CIX OP-TEE 3.17 + `plat-cix`);
   the build checks that OP-TEE carries exactly that StMM image.
4. bootloader2: certificates with TF-A `cert_create`, every key the
   published CIX key (`cix_privatekey.pem`), the NV counter of the vendor
   FIP (31 on the O6); patch 0001 lets the trusted key certificate carry
   only the trusted-world key, as CIX's does. `fiptool create`, then an
   independent check (`cixfw.verify_fip`): root, key and content
   certificates, NV counters and the BL31/BL32 hashes.
5. BL2: `make PLAT=sky1 TRUSTED_BOARD_BOOT=1 ROT_KEY=cix_privatekey.pem bl2`
   with mbed TLS 2.28 (submodule `mbedtls`). The root of trust is the
   SHA-256 of that key's public half, built into BL2.
6. bootloader1: the reference bootloader1 (the board's, or CIX's with
   `--boot-firmware cix`) with its AP image replaced by BL2, zero-padded to
   the size of the CIX BL2 so the container keeps its layout, and both
   signatures redone with the published BL1 keys (`oem_privatekey.pem`,
   `cix_privatekey.pem`). SE and CSU_PM stay byte for byte.
7. bootloader3: the BL33 certificates come from `cert_create` under the
   same root (trusted key certificate with only the non-trusted-world key,
   NV counter 223) instead of the closed `cert_uefi_create_rsa`; the open
   BL2 checks them as `NON_TRUSTED_KEY_CERT_ID`.

`--target DEBUG` builds TF-A with `DEBUG=1` and OP-TEE with log level 3.
TF-A and OP-TEE take their build time from `SOURCE_DATE_EPOCH` (default: the
commit time), so a commit rebuilds bit-identically. `manifest.json` gets an
`open` section with the BL2, BL31, BL32, StMM, FIP and bootloader1 hashes.

The source-built images sit where the CIX ones do (BL2 `0x80000000`, BL31
`0x80200000`, OP-TEE `0x80500000`, BL33 `0x84400000`) and start with the
same entry code. **No `--open` image has been boot-tested yet.** Keep an SPI
programmer and a known-good image at hand.

Check a finished image stage by stage
([check_open.py](../scripts/open/check_open.py), in the build container):

```sh
python3 scripts/open/check_open.py dist/orion-o6_2026Q3_open.bin \
    edk2-project/Platform/Radxa/Orion/O6/Firmwares/bootloader1.img \
    output/orion-o6/RELEASE/open/tfa-bl2/sky1/release/bl2.bin --root cix_privatekey.pem
```

It checks the bootloader1 keys, signatures and digests, that SE and CSU_PM
are the reference's and the AP image is the built BL2, that BL2's root of
trust signs both FIPs and their chains verify, and that the TrustZone
configuration passes BL31's checks with any secure storage clear of the
boot firmware.

### Tools

[scripts/open/cixfw.py](../scripts/open/cixfw.py) (no CIX binaries needed):

```sh
cixfw.py pack -c spi_flash_config_all.json -o cix_flash_all.bin
cixfw.py pack -c spi_flash_config_ota.json -O cix_flash_ota.bin
cixfw.py fip bootloader2.img --verify --root cix_privatekey.pem [--extract DIR]
cixfw.py bl1 bootloader1.img --verify [--extract DIR]
cixfw.py bl1 bootloader1.img --replace ap=bl2.bin --resign out.img \
         --key1 oem_privatekey.pem --key2 cix_privatekey.pem
cixfw.py stmm bootloader2.img -o stmm.fd
check_open.py IMAGE REF_BOOTLOADER1 BL2.bin [--root ROOT.pem]
```

* `pack` output is byte-identical to `cix_package_tool` for the full and OTA
  images of all five boards, and for test layouts with several OTA images,
  unsorted regions, sec-debug and XIP files (compared against the AArch64
  CIX tool in the build container). `build.py` uses it for every board.
* `bl1` verifies every vendor container (CIX, CIX `Firmwares2`, Radxa,
  Minisforum retail, Orange Pi 6 and 6 Plus). Re-signing the Radxa O6 BL1
  with the published keys changes only the four signatures. The container
  layout is documented in `cixfw.py`; the signed regions follow
  [Neol00/edk2-cix-unlocked#12](https://github.com/Neol00/edk2-cix-unlocked/issues/12),
  which reports a modified, re-signed BL1 booting on an Orion O6.
* [scripts/open/test_cixfw.py](../scripts/open/test_cixfw.py) runs in CI;
  [scripts/open/test_stmm_nor.py](../scripts/open/test_stmm_nor.py) tests the
  StandaloneMM NOR driver on the host.
* [scripts/open/re](../scripts/open/re) holds the reverse-engineering tools
  used for BL2 and BL31: `fnmatch.py` names the functions of a stripped CIX
  image from a build of the public tree, `decompile.sh` runs Ghidra on it
  with those names, `cot.py` and `bl2tables.py` decode the chain of trust,
  image descriptors, I/O policies and memory map of a BL2.

## Patch queues

| Submodule | Pin | Patches |
|---|---|---|
| `arm-trusted-firmware` | `114fb205` (CIX, 2026-03-10) | 0001, 0003 `cert_create`: certificates that carry only one world's key; 0002 BL2 platform support; 0004 TrustZone configuration in BL31; 0005 audio domain and resume marker after system suspend; 0006 NI-700 QoS as in the CIX BL31; 0007 normal-world SiP argument checks |
| `mbedtls` | `8b3f26a5` (2.28.0) | none |
| `edk2` | CIX `9212cf39` | 0001, 0002 Secure Boot defaults and TPM page ([parity](parity.md)); 0003 the StandaloneMM core builds CIX's NVRAM information HOB |
| `optee_os` | `cc66640f` (CIX, 2025-11-11) | 0001-0005 from [orion-unofficial/edk2-cix](https://github.com/orion-unofficial/edk2-cix) `source/unofficial/cix/1.2/op-tee/3.17.0` (BSD-2-Clause, authorship kept): compiler fixes, fail-closed hardware entropy and key return, FF-A 1.2 StandaloneMM loader behind `CFG_STMM_FFA_VERSION=12` (default 10 keeps the vendor StMM interface), stop StMM on a failed NOR transaction; 0006 NOR secure storage only where the TrustZone configuration puts it; 0007 the secure storage key derived as CIX's OP-TEE derives it |

They are applied by `scripts/prepare.py` like the EDK2 queues
([architecture](architecture.md#patch-queue)).

## How the open firmware differs from CIX's

The open BL2 and BL31 were written and checked against the CIX binaries in
the 2026Q3 bootloader1/bootloader2 (TF-A `v2.7(release):Beta_2.1.5_release`,
built 2026-07-15): functions matched against a build of the public tree
(`fnmatch.py`), the rest decompiled. Where they differ on purpose:

### BL2

The five platform files CIX left out (patch 0002). Its chain of trust, image
descriptors, I/O policies and memory map decode identically to the CIX
BL2's (`cot.py`, `bl2tables.py`), apart from two policy entries for image
IDs the chain never loads. Differences:

* **Every image is authenticated.** The CIX BL2 skips authentication
  altogether when the SE reports secure boot off (the key-management block's
  enable flag, which follows the eFuses; unfused O6 boards), and even with it
  on never verifies image 36, bootloader3's trusted key certificate. The
  open BL2 verifies the whole chain, both FIPs, against the root built into
  it, whatever the fuses say. On chain-A boards that root is public, so this
  catches corruption, not an attacker with flash access.
* The root of trust is the hash of `ROT_KEY` built into BL2, not the one the
  SE passes; there is no rollback protection (NV counters read 0 and are not
  written, as in the CIX BL2).
* mbed TLS instead of the CIX crypto engine library (not public).
* No bootloader3 recovery: the CIX BL2 can copy a backup bootloader3 (image
  types 0x68/0x69) over a failing one when the TrustZone configuration
  enables it. No board layout here has those regions and every
  configuration disables it.
* No POST codes at `0x05030F00` beyond the public tree's; they are a
  debugging aid (UEFI writes its own).

### BL31

The public BL31 is the CIX tree; the patches add what the CIX BL31 has and
it lacks. Every NI-700 and CI-700 QoS entry, power domain, RAS and TZC table
was compared between the two binaries; after patch 0006 they agree.

* **TrustZone configuration** (patch 0004), as the CIX BL31 reads it: SPE for
  the normal world follows `spe_feature_en`, and the data bytes go to
  `0x80400000` for OP-TEE. Unlike CIX, a secure storage region that overlaps
  the flash header or an image it lists is dropped (see below).
* **System suspend** (patch 0005): the audio domain sequence and the `0xBB`
  resume marker in `SW_USED_REG1` the CIX BL31 has on the first core back.
* **QoS** (patch 0006): NPU at 0x7, SE and PM ports at 0xD.
* **SiP argument checks** (patch 0007). `CIX_SIP_NOR_STORAGE`
  (`0xC200000B`) runs a SPI NOR transaction on a buffer the caller gives as
  a physical address, and the CIX BL31 accepts it from the normal world
  unchecked: since BL31 maps its own and OP-TEE's memory, a normal-world
  caller can have flash data written over the secure firmware, or secure
  memory written to flash. The open BL31 keeps normal-world buffers inside
  non-secure DRAM (OP-TEE's calls for StandaloneMM come from the secure
  world and are unchanged). `CIX_SIP_POWER_DOMAIN_SET` (`0xC200000A`)
  indexed a table with an unchecked ID and now ignores out-of-range ones.
  Every board's vendor BL31 has both handlers as they were.

Left out on purpose:

* SiP calls `0xC2000030` and `0xC2000031`, which pass a caller-supplied
  address and length straight into SE mailbox commands `0x8200001A` and
  `0x8200001B`. Nothing in UEFI calls them and their purpose is unknown; an
  unchecked normal-world pointer in EL3 is not worth copying blind.
* SPI-NOR erase and write in BL31 (`spi_nor_write`, "override flash"): the
  CIX BL31 has the code but nothing calls it.
* The CIX BL31's renumbered POST codes (`0xC0xx`).

### OP-TEE

Patch 0006: the NOR file system (`TEE_STORAGE_PRIVATE_NOR`) took its offset
from `CFG_NOR_FS_OFFSET` (`0x800000`) plus 1 or 2 MiB depending on the
flash's JEDEC density. On an 8 MiB flash that is past the end of the chip,
which ignores the upper address bits: the first write would land on the
firmware header and bootloader1. It now uses only the region the TrustZone
configuration enables, inside the chip; otherwise secure storage reports
`TEE_ERROR_STORAGE_NOT_AVAILABLE`. REE-FS storage (through
`tee-supplicant`), the default, is unaffected.

Patch 0007: the secure storage key. CIX's OP-TEE derives it as
HMAC-SHA256(HUK, die ID ‖ `"cix_ssk_gen_string"`), the die ID being the
HUK subkey for that purpose; the public tree uses the generic compatible
construction with a fixed die ID and `"ONLY_FOR_tee_fs_ssk"`. With the
public derivation neither firmware could read the secure storage objects
the other wrote (TA data under `/data/tee`, fTPM state). The open OP-TEE now
derives CIX's key (`CFG_CIX_SSK_COMPAT`), so switching between vendor and
open firmware keeps them.

### TrustZone configuration

`trustzone_config.bin` (image type 9 at `0xF0000`; only the O6/O6N layouts
place it) holds `spe_feature_en`, the NOR secure storage type, address and
size, and `bl3_backup_recovery_en`. The CIX BL31 checks it and copies it to
`0x80400000`; the CIX OP-TEE takes the secure storage region from it on
64 Mbit flashes.

The file Radxa ships for the O6 and O6N enables secure storage at 6 MiB,
2 MiB long. bootloader3 starts at `0x406000` and, with the features every
board has here, is 2.1 MiB long, so it ends at about `0x6272EC`: the
secure OS's NOR file system would overwrite UEFI. Every board now gets the
CIX stock configuration (SPE on, secure storage and bootloader3 recovery
off), the defaults BL31 also uses where the layout has none. BL31 (patch
0004) and `check_open.py` refuse an overlapping region in any case.

## Gaps

### StandaloneMM

The StandaloneMM image inside OP-TEE holds the UEFI variable store. CIX's
is a `PlatformMmStandalone` build of EDK2's core, CPU, variable and fault
tolerant write drivers plus three drivers it ships only as binaries.
[Platform/CIX/Sky1/StandaloneMm](../edk2-project/Platform/CIX/Sky1/StandaloneMm)
builds the same image from source: the EDK2 modules from the CIX `edk2`
tree, and the three drivers written from their interfaces (the UEFI-side
clients and OP-TEE's `stmm_sp.c` are public) and checked against the CIX
binaries. The firmware volume has the same files, file GUIDs and order as
CIX's.

| Module | Source | What it does |
|---|---|---|
| `StandaloneMmCore` | EDK2 + patch 0003 | as CIX's: also turns the NVRAM location OP-TEE appends to the boot information (variable store, FTW working and spare: three flash offsets, three sizes) into a GUID HOB (`4c8167fc-…`) |
| `NorFlashStandaloneMm` | new | the variable store's FVB protocol on the boot flash, at the HOB's location; keeps the region in RAM (what the variable driver reads); single SPI NOR transactions for UEFI's `NorFlashStmmRuntimeLib` (`gCixMmNorFlashCommunicateGuid`) |
| `SerialOutStandaloneMm` | new | UEFI runtime log lines (`gCixMmSerialOutCommunicateGuid`) to the OP-TEE console |
| `FwVersionStandaloneMm` | new | the StMM version FwVersionDxe shows (`gCixMmStmmInfoCommunicateGuid`): `edk2-cix-<version>-<commit>` |
| `FaultTolerantWriteStandaloneMm`, `VariableStandaloneMm` | EDK2 + CIX `VariablePcdLib` | unchanged |
| `StandaloneMmCpu` | EDK2 | unchanged |

StandaloneMM reaches the flash and the console through FF-A direct
requests to OP-TEE (endpoints 101 and 100); OP-TEE turns flash requests
into `CIX_SIP_NOR_STORAGE` SMCs. The NOR driver uses the commands CIX's
does and every flash in the bootloader1 table supports (single-line,
3-byte addresses, READ, PAGE PROGRAM, 4 KiB SECTOR ERASE), writes in place
where new data only clears bits and erases otherwise, and formats the store
when its headers are invalid, as CIX's does (which keeps the inherited
recovery weakness the orion-unofficial project found: an interruption after
the variable block's header was erased loses the store). Differences:

* The UEFI transaction passthrough checks that the data fits in the
  message. CIX's driver takes the transaction's data length as given and
  OP-TEE and TF-A use it on StandaloneMM's buffer, so whoever can send MM
  messages (UEFI, and through OP-TEE the OS kernel) can have flash data
  written over secure memory past it.
* A failed program or erase re-reads the block, so the RAM copy never
  claims data the flash does not hold.
* Partial FVB writes at an offset are written at that offset (CIX's FVB
  code writes a whole block at offset 0 when a cut write is block-sized).

[scripts/open/test_stmm_nor.py](../scripts/open/test_stmm_nor.py) compiles
the NOR and FVB code for the host and runs it against a simulated flash:
formatting, 20 000 random writes, erases and reads checked against a model
(flash and RAM copy), bounds, and failing transactions in the middle of
writes.

### Not planned

SE firmware and CSU_PM stay binary: no source exists, the SE does DRAM
training, and CSU_PM needs the proprietary Xtensa core configuration to
build. `sfh_fw.bin` (17 KB Cortex-M) is small enough to clean-room later.

## Roadmap

1. Boot-test `--open` on an O6 (UART log of BL2, BL31 and OP-TEE; variables
   persist across a cold boot; Secure Boot keys enrol; suspend and resume).
   `--vendor-stmm` isolates the StandaloneMM image if variables misbehave.
   Then make it the default for the chain-A boards.
2. `sfh_fw.bin` clean-room.

## Debugging

UARTs on the Orion O6/O6N, 115200 baud (from
[orion-unofficial](https://github.com/orion-unofficial/edk2-cix/blob/build/docs/src/debug.md)):

| UART | Owner |
|---|---|
| UART1 | EC |
| UART2 | AP: BL2, BL31, OP-TEE, UEFI, OS console |
| UART3 | 40-pin header (GPIO105/106), usable as a debug UART once muxed |
| UART4 | CSU_PM |
| UART5 | SE |

Output starts on the EC, then the SE, then the AP. A board that stays silent
on UART5 rejected bootloader1; SE output without AP output points at BL2 or
the bootloader2 signatures.
