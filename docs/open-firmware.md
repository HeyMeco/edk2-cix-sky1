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
| SE firmware (Cortex-M33: SoC bring-up, eFuse, image checks, AP services) | [firmware/se](../firmware/se), translated from CIX's debug build, DDR driver included | **built from source** (`--open`; `--vendor-se` keeps CIX's); same register writes and memory contents as CIX's in [test_se.py](../scripts/open/test_se.py) ([below](#se-firmware)) |
| CSU_PM (Xtensa LX7, Arm SCP-firmware based) | [firmware/csupm](../firmware/csupm): SCP-firmware v2.11.0 and CIX's code translated from its debug build | **built from source** (`--open`; `--vendor-pm` keeps CIX's); does what CIX's does in [test_pm.py](../scripts/open/test_pm.py) at boot, over SCMI, for PM messages, through suspend/resume, shutdown and reset and at run time (DPM, fans, thermal), with each board's PM configuration; not yet boot-tested on hardware ([below](#csu_pm)) |
| TF-A BL2 | `arm-trusted-firmware` + patch 0002 | **built from source** (`--open`, chain A), re-signed into bootloader1 |
| TF-A BL31 | `arm-trusted-firmware` + patches 0004-0007 | **built from source** (`--open`) |
| OP-TEE BL32 | `optee_os` + patch queue | **built from source** (`--open`) |
| StandaloneMM inside OP-TEE | EDK2 + [Sky1 StandaloneMm](../edk2-project/Platform/CIX/Sky1/StandaloneMm) | **built from source** (`--open`; `--vendor-stmm` embeds CIX's instead) |
| FIP certificates, `fiptool` | TF-A `cert_create` (patched), `fiptool` | **open** for bootloader2 and bootloader3 |
| bootloader1 container (`CIXBTFF!`) | — | **open**: parse, verify, replace, re-sign (`cixfw.py bl1`) |
| SPI image packer (`cix_package_tool`) | — | **open**: `cixfw.py pack`, used by every build |
| `memory_config`, `pm_config` | CIX generators | built from source where the board package has them |
| Sensor fusion hub firmware (`sfh_fw.bin`, Cortex-M) | [firmware/sfh](../firmware/sfh) | **built from source** (`--open`); same hardware writes as CIX's in every scenario of [test_sfh.py](../scripts/open/test_sfh.py) |
| EC firmware (`ec_fw.bin`, Orion O6 only; ITE IT5570E, 8051) | [firmware/ec](../firmware/ec) | **written from scratch** after Radxa's EC V1.07; same pin changes, I2C transfers, host-command answers and e-flash writes as the vendor's in [test_ec.py](../scripts/open/test_ec.py) in 14 of 16 scenarios, and the update to it and back works in the model; `--open-ec`; not hardware-tested ([below](#ec-firmware-orion-o6)) |
| `trustzone_config` | CIX generator | CIX stock binary on every board; identical to the generator's output apart from its timestamp ([below](#trustzone-configuration)) |

With `--open` on an Orion O6/O6N, everything that runs before UEFI apart
from the boot ROM is built here from source: the SE firmware (DRAM
training included), CSU_PM, TF-A, OP-TEE with StandaloneMM and the sensor
fusion hub firmware.  None of it has been boot-tested on hardware yet.

### Tracker

What runs before BL33 on an Orion O6 built with `--open`, by size of the
image each stage ships (O6, 2026Q3; the boot ROM is in the chip and cannot
be replaced, so it is not counted):

```
pre-BL33 firmware                               1369 KB   ████████████████████ 100% open
├── bootloader1
│   ├── SE firmware (Cortex-M33)                 141 KB   ████████████████████ 100% open (CIX's: 334 KB)
│   │   ├── start-up, eFuse, flash, crypto, key manager,
│   │   │   SoC bring-up, CI-700, secure boot, services        open (firmware/se)
│   │   ├── DDR driver (DRAM training)                  open (firmware/se/ddr)
│   │   └── PVT table (V/F selection data)              CIX's data as generated source
│   ├── CSU_PM (Xtensa LX7)                      196 KB   ████████████████████ 100% open (CIX's: 327 KB)
│   │   └── by CIX's code (198 KB; 79% translated, the rest replaced or CLI-only)
│   │       ├── SCP-firmware framework, modules   51 KB   ████████████████░░░░  80% translated
│   │       ├── CIX Source/projects/sky1          30 KB   ███████████████████░  96% translated
│   │       ├── CIX Source/common                 71 KB   ██████████████░░░░░░  72% translated
│   │       ├── FreeRTOS, Cadence runtime         29 KB   replaced: no RTOS, own libc
│   │       └── debug CLI                         16 KB   not ported
│   └── TF-A BL2                                  92 KB   ████████████████████ 100% open
├── bootloader2
│   ├── TF-A BL31                                 94 KB   ████████████████████ 100% open
│   └── OP-TEE with StandaloneMM                 842 KB   ████████████████████ 100% open
└── sensor fusion hub firmware (Cortex-M)          4 KB   ████████████████████ 100% open

open    1369 KB  SE (141), CSU_PM (196), BL2 (92), BL31 (94), OP-TEE + StMM (842), SFH (4)
closed     0 KB  (the boot ROM, in the chip, is not counted)
```

By stage: all 7 stages open (SE, CSU_PM, BL2, BL31, OP-TEE,
StandaloneMM, SFH). Data CIX generated is carried as source where the
firmware needs it (the SE's PVT table, CSU_PM's clock and V/F tables,
generated from CIX's debug builds by scripts that check them against
CIX's bytes). The open SE and CSU_PM have not run on hardware yet
(`--vendor-se`, `--vendor-pm` keep CIX's). How much of CIX's CSU_PM code the port translates is measured
against CIX's debug build: the share of CIX's code, by function size, the
open firmware has a counterpart for (same name, or named in its sources as
what they translate), regenerated with
`python3 scripts/open/csupm_coverage.py csupm.elf --tracker` (`--units`
lists what is left per CIX source file). It counts translation, not
equivalence; `test_pm.py` checks behaviour. What is left is mostly code
the open firmware replaces rather than translates (CIX's timer, event and
job framework and IPC arbiter, by SCP-firmware's events, alarms and
transport; its interrupt-driven I2C path, by a polled driver moving the
same bytes) or that only CIX's debug CLI reaches (CI-700 event
collections, the SCMI decoder). The tools
around them (bootloader1 container, FIP certificates, SPI packer) are
open. Data CIX generates (`memory_config`, `pm_config`,
`trustzone_config`) is built from source where the board package has it
and is not counted above.

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
./build.sh orion-o6 --open-ec       # dist/orion-o6_2026Q3_openec.bin (with --open: _open_openec)
```

`--open-ec` (Orion O6 only, independent of `--open`) puts the EC firmware
built from [firmware/ec](../firmware/ec) (SDCC, in the container) at
`ec_addr` (0x10000) in place of Radxa's `ec_fw.bin`
([below](#ec-firmware-orion-o6)).

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
6. SE firmware: `make -C firmware/se` builds `se_fw.bin` (arm-none-eabi
   GCC, Mbed TLS), DDR driver included; the build no longer needs CIX's
   debug build. `--vendor-se` skips this.
7. CSU_PM: `make -C firmware/csupm all check` (Espressif's Xtensa GCC with
   the pm_core_v1 description, every instruction checked against the
   core's) builds `csupm.xbin`, the XBIN image the SE loads.
   `--vendor-pm` skips this.
8. bootloader1: the reference bootloader1 (the board's, or CIX's with
   `--boot-firmware cix`) with its AP image replaced by BL2, its SE image
   by the open SE firmware and its CSU_PM image by the open one, and the
   signatures redone with the published BL1 keys (`oem_privatekey.pem`,
   `cix_privatekey.pem`). BL2 and the SE firmware are zero-padded to the
   size of CIX's so the container keeps its layout; CSU_PM is not (the SE
   hashes only the XBIN's sections and finds the signature from the
   image's length): it ends in CIX's slot and its signature moves down,
   nothing else moves. `test_se.py --secure` checks that the open SE
   verifies it and starts the AP, and both SEs load it identically.
9. bootloader3: the BL33 certificates come from `cert_create` under the
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
  used for BL2 and BL31: `funcmatch.py` names the functions of a stripped CIX
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

What `--open` builds for the O6 and O6N differs from CIX's firmware only
where listed here; everything else matched CIX's write for write in the
emulator tests (SE: boot, DRAM initialisation on the O6 and O6N,
suspend/resume, secure boot; CSU_PM: boot, SCMI, PM messages, power
transitions, run time, all five boards' PM configurations; SFH: every
hardware write). The open SE and CSU_PM have not run on hardware, and
what the emulators model of the hardware is inferred from CIX's code.

### At a glance

Security fixes, where CIX's behaviour is a hole:

| Where | Open firmware | CIX's |
|---|---|---|
| BL2 ([below](#bl2)) | authenticates every image, whatever the fuses say, against the root built into it | skips authentication on unfused boards; never checks bootloader3's trusted key certificate |
| BL31 ([below](#bl31)) | SPI NOR SiP call: normal-world buffers must be in non-secure DRAM; out-of-range power-domain IDs ignored | the OS can have flash data written over secure memory, or secure memory to flash |
| BL31 | SiP calls `0xC2000030/31` left out | pass OS pointers and lengths straight into the SE commands that burn the OEM key hash into the eFuse ([eFuses](efuses.md#burning)) |
| BL31, TrustZone configuration ([below](#trustzone-configuration)) | a secure storage region overlapping the firmware is dropped; every board gets CIX's stock configuration | Radxa's O6/O6N file puts secure storage over UEFI |
| OP-TEE ([below](#op-tee)) | NOR secure storage only inside the region the TrustZone configuration enables | an offset past the end of an 8 MiB flash, which wraps onto the firmware header |
| StandaloneMM ([below](#standalonemm)) | the UEFI transaction passthrough checks lengths | UEFI, and the OS through OP-TEE, can have flash data written past StMM's buffer into secure memory |
| SE ([below](#se-firmware)) | secret eFuse fields (device and model keys) only to the TEE mailbox | to the OS's mailbox too |
| SE | the hash service reads DRAM only | hashes any address, SE SRAM and eFuse included |
| SE | refuses to burn the OEM root key hash; programs no eFuses | burns it, turning secure boot on for good |

Left out, debug or unused:

* SE: the UART debug monitor; the DDR driver's training report and
  register dumps, and its BIST pass/fail arrays (only printed; the BIST
  runs are kept).
* CSU_PM ([below](#csu_pm-differences)): the debug CLI and what only it reaches;
  CIX's code overlays; FreeRTOS and CIX's timer, event and job framework
  (SCP-firmware events and alarms instead, with CIX's periods); CIX's
  interrupt-driven I2C path (a polled driver moving the same bytes).
* BL2: bootloader3 backup recovery (no board's layout has it). BL31: the
  uncalled flash write code. Both: CIX's extra POST codes.

Behaviour that differs slightly:

* CSU_PM: the run-time services start 1 ms after the ready handshake
  (CIX's from its event worker once it first idles); the heartbeat timer
  is created once (CIX's adds one on every resume).
* StandaloneMM: a failed flash program or erase re-reads the block; partial
  writes land at their offset.
* SE: a memory-configuration block lookup that stays inside its table; a
  write enable per erased flash sector; the DDR driver's density table
  padded so it is not read past its end.
* bootloader1: CSU_PM's image is not padded to CIX's size and its
  signature moves down (the SE hashes the image's sections only); the
  container is otherwise CIX's, re-signed with the same published chain-A
  keys.

Not functional:

* versions: the PM reports its build version and commit (CIX's:
  `00.12.001`), the SE `beta2.0-<version>`;
* images built with GCC and Mbed TLS rather than CIX's toolchains and
  crypto library, and smaller (SE 141 KB, CIX's 334 KB; CSU_PM 196 KB,
  CIX's 327 KB);
* CSU_PM's RAM log holds its messages (CIX's line buffer is never flushed,
  so its log holds stray bytes);
* CIX's oddities that do not harm are kept as CIX has them and commented
  in the source, not fixed (in CSU_PM and the DDR driver, e.g. the memory
  configuration write-back writing as many bytes as the checksum's value).

The open BL2 and BL31 were written and checked against the CIX binaries in
the 2026Q3 bootloader1/bootloader2 (TF-A `v2.7(release):Beta_2.1.5_release`,
built 2026-07-15): functions matched against a build of the public tree
(`funcmatch.py`), the rest decompiled. Where they differ on purpose:

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
  `0x8200001B`: burning the OEM public key hash into the eFuse, and reading
  whether it is burnt ([eFuses](efuses.md#burning)). From the normal world,
  that is a permanent change to the chip's boot chain behind an unchecked
  pointer.
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

### CSU_PM differences

Written from CIX's debug build (`csupm_sky1` 00.12.001) and checked with
[test_pm.py](../scripts/open/test_pm.py); the details are in
[firmware/csupm/README.md](../firmware/csupm/README.md). Differences:

* No debug CLI, and nothing only it reaches: CI-700 event collections, the
  DFD trace, GPIO interrupt helpers, forced operating points; no log
  dumps.
* No RTOS: CIX's FreeRTOS tasks and its timer, event and job framework
  become SCP-firmware alarms and events (`dpm` and `pm_svc` modules) with
  CIX's periods and order. CIX's 32 KB of code overlays are not needed.
* I2C: CIX's interrupt-driven transfer path is a polled driver; the bytes
  on the bus are the same.
* The run-time services (fans, PVT polling, board sensors, watchdog) start
  1 ms after the ready handshake; CIX's start from its event worker once
  it first idles.
* The heartbeat timer is created once; CIX's creates another on every
  resume, so after one two timers toggle the LED.
* The version page reports the build's version and commit; the RAM log
  holds the messages logged (CIX's holds bytes from past its line buffer,
  which is never flushed).
* Kept as CIX has them: the PMIC error notification is built but not sent
  (CIX's PM-to-AP send is empty; the AP reads faults with PM message
  0x400); CIX's power-domain table, which makes ISP0 its own parent and
  so denies powering ISP0 and GPUTOP on; interrupt lines 28-30 (PVT
  alarms, PROCHOT) left masked as CIX leaves them.

## Components

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

### Sensor fusion hub

`sfh_fw.bin` (flash image type 8 at `0x101000`) runs on a small Cortex-M
next to the SE. The SE copies it into the SFH SRAM and starts it when the
system enters suspend to RAM; it serves timed wake-ups over the CIX mailbox
(`ap2sfh`, `pm2sfh`, replies on `sfh2ap`): `0xFFF2` (suspend, wake after N
seconds), `0xFFF4` (resume), `0xFFF7` (wake after N ms), waking the system
by writing 3 to `0x50070080`, and signals `0xAABB` at `0x50070100` once
up. CIX's image is FreeRTOS-based (the scheduler never starts) with
newlib's stdio; [firmware/sfh](../firmware/sfh) does the same in 4 KB of C.
[scripts/open/test_sfh.py](../scripts/open/test_sfh.py) runs both images
in Unicorn against a model of the mailbox, timers and control block and
compares every hardware write: boot, the three messages, malformed ones and
the timer interrupts produce the same writes in the same order.

The SE loads `sfh_fw.bin` from flash without any signature or hash check,
on every board, so this image (and anything else put there) runs whatever
the board's signing chain.

### SE firmware

[firmware/se](../firmware/se) rebuilds the SE firmware from CIX's debug
build (`se_fw.elf` 2.0.2026W01.01, full DWARF, in CIX's
`cix_proprietary__cix_firmware` repository; pinned in
[manifests/cix-debug-firmware.json](../manifests/cix-debug-firmware.json)
and fetched by the build, not redistributed): start-up, eFuse, memory
configuration, both flash controllers, the crypto engine's bring-up, TRNG
and OTP, key manager, PLLs and clock/power sequencing, CI-700, MMHUB,
RCSU, SRAM repair, loading and verifying CSU_PM and BL2, the mailbox
services, suspend/resume and crash records.  SHA-2, HKDF and RSA come from
the Mbed TLS submodule, SM2/SM3 are written here.  The DDR driver (DRAM
training, 44 KB, 195 functions) is translated too
([firmware/se/ddr](../firmware/se/ddr)), and CIX's PVT table is generated
source; how they were checked is in the
[README](../firmware/se/README.md#ddr-driver).  The image is 141 KB
(CIX's release image: 334 KB).

[se_emu.py](../scripts/open/se_emu.py) runs an SE image in Unicorn on a
model of the SoC as the SE sees it (boot ROM hand-over, QSPI flash, PLLs,
power controllers, crypto engine status, PM handshakes);
[test_se.py](../scripts/open/test_se.py) boots CIX's debug build and the
open one on the same flash image and compares every register write and
what ends up in DRAM and the PM's SRAM (CSU_PM, BL2, key blocks, boot
records):

* cold boot from reset to the AP's release: identical over 831 registers,
  for life cycles 4, 5 and 7, with and without device keys;
* run-time requests from the AP (`--services`): the same answers;
* a suspend request through WFI, the SoC's re-initialisation and the
  resume message to the PM (`--suspend`): identical over 804 registers;
* secure boot fused for the image's keys (`--secure`, open firmware only):
  the open O6 image is verified and boots; with one byte of BL2 or CSU_PM
  changed it does not.

* DRAM initialisation with the driver live (`--ddr`, on a model of the
  controllers, PHYs and PI blocks inferred from what CIX's driver does):
  identical over 9739 registers on the O6 and 9745 on the O6N, with each
  driver feature switched too.  Training itself happens in the PI
  hardware and is only modelled.

Differences from CIX's firmware, on purpose:

* (the eFuse fields, who reads them and who can burn them:
  [eFuses](efuses.md))
* secret eFuse fields (CIX device and model keys, test keys, OEM model key)
  go only to requests on the TEE mailbox; CIX's answers any field to the
  non-secure mailbox (`0x8200000e`), so the OS can read the keys OP-TEE's
  hardware unique key is derived from;
* the hash service (`0x8200000c`) only reads DRAM; CIX's hashes any
  address the caller names, SE SRAM and eFuse included;
* the request to burn the OEM root key hash (`0x8200001a`) is refused;
  CIX's burns it and turns secure boot on for good. Nothing in the open
  SE programs eFuses;
* no debug monitor; a memory-configuration block lookup that is correct
  where CIX's reads past its table; a write enable per erased sector.

Not done: the clock and power records of crash reports are raw register
dumps, and the SFH load during suspend has not been emulated.  The open
SE has not been boot-tested on hardware.

### CSU_PM

CSU_PM (`csupm_sky1`, 00.12.001, in the same repository, with full DWARF)
is Xtensa LX7 (core `pm_core_v1`, Cadence XtensaTools 14.10 clang):
FreeRTOS, Arm SCP-firmware (public) and about 100 KB of CIX code (clock
tree and PLLs, V/F tables and DVFS, DPM, PMICs, DDR low power, system power
states, CI-700/NI-700 counters, messages, a debug CLI).
[firmware/csupm](../firmware/csupm) rebuilds it: the core has no user TIE,
so Espressif's GCC builds for it with a core description made from the
instruction set CIX's own build uses (`xtensa_core.py`), and
`xtensa_isa.py check` keeps every build inside that set. `xtensa_flix.py`
decodes the core's FLIX bundles, `xt_emu.py` interprets the core and
`pm_emu.py` models the SoC around it (I2C and PMICs, PLLs, mailboxes,
interrupts, the SE's side, S5 and DDR controllers as CIX's image expects
them).

The open firmware is SCP-firmware v2.11.0 (submodule) with CIX's changes
to its modules, CIX's own modules and CIX's sources translated function by
function from the debug build. In the emulator, against CIX's image
([test_pm.py](../scripts/open/test_pm.py)):

| What | Same as CIX's |
|---|---|
| boot register writes | all 1329, in order |
| boot I2C transfers (PMIC rails, DVFS levels) | all, in order |
| SCMI answers (base, power domains, system power, sensors, 273 clocks, performance on 12 domains, levels set directly and through fast channels) | all 215, with the same PLL writes and I2C transfers |
| shared memory (RTT, PCC tables, DPM data board, version, RAM log) | all but the version (ours) and the RAM log (CIX's holds bytes from past its unflushed line buffer) |
| CIX's PM messages (UEFI, Linux, TF-A) | all 20 answers |
| suspend, resume, shutdown, warm reset | all |
| run time: DPM (performance streams, power model, arbiters, TDP/EDP limits), PROCHOT, die at 100 C | all, including the DPM's state |
| board services: fans and Linux's fan modes, PVT thresholds, PMIC faults, DDR DFS, board sensors | all |
| the five boards' PM configurations (O6, O6N, MS-R1, Orange Pi 6 with MP2845 PMICs and shared bucks, Orange Pi 6 Plus) | every phase above |
| V/F tables on simulated parts ([test_vf.py](../scripts/open/test_vf.py): straps, fuses, ATE phases, part numbers, BIOS configurations, Phecda) | every case tried |

The emulator models devices from what CIX's image does with them, so what
it cannot show is real hardware behaviour: fans (pulses per turn,
polarity, stall), the EC's reads over the I2C slave, MP2845/AU4683
read-back formats, the watchdog's count, and whether the interrupt lines
CIX never unmasks (PVT alarms, PROCHOT) are live. Left out on purpose:
the debug CLI and what only it reaches. `build.sh --open` puts it in
bootloader1 (`--vendor-pm` keeps CIX's); it has not run on hardware yet.
The plan is in the
[README](../firmware/csupm/README.md#plan).

### EC firmware (Orion O6)

The O6's EC (ITE IT5570E, 8051; [pins](../scripts/open/ec_o6_pins.py)
from Radxa's V1.2 schematic, sheet 35) sequences the board's power
(always-on rails, `RSM_RST`, the SoC's power button and sleep signals,
S0 rails, the PMIC enable gate), runs the fan from the SoC's PVT
temperature (read from CSU_PM's I2C slave) and five board NTCs, sets the
two RTS5453H PD controllers up, and answers the host on I2C6 at 0x76
(CIX's ChromeOS-EC style host commands: UEFI's `Ite5570EcLib`, and
Linux's `google,cros-ec-i2c` with CIX's `cix-ec-*` drivers in device-tree
mode). Radxa's image is a Keil C51 build of CIX's EC code base, mostly
laptop code the O6 never runs.

[firmware/ec](../firmware/ec) is a new firmware (SDCC) that does what the
vendor's does on the O6: its sequencer tables, its thresholds and duty
table, its NTC conversion (the vendor's own routine run over every ADC
code), its host-command answers byte for byte, including the persistent
buffers some commands read leftovers from, and its e-flash records
(AC-loss policy at 0x1F8FF, power-off reason at 0x1FCFC). `i8051_emu.py`
interprets the 8051 and `ec_emu.py` models the IT5570 (timers, interrupt
and wake-up controllers, GPIO, ADC, PWM, SMBus masters and slaves, the
flash follow mode, the e-flash) and the board (rails and power goods, the
SoC's sleep signals, CSU_PM, the PD controllers, NTCs).
[test_ec.py](../scripts/open/test_ec.py) runs both images through 16
scenarios (AC on, button, S0, OS shutdown, button in G3, short and long
press, both AC-loss policies, heating and cooling, fan modes, a target fan
speed, an NTC at 90 C, every host command, the AC-loss write, UEFI's EC
reset) and compares what the board sees in order, with a timing
tolerance. It also installs each image over the other the way UEFI does
(the new image in the SPI flash, host command 3E19, the mirror reset) and
checks that the new one boots, reaches S0 and answers. Run it with
`scripts/open/run_ec_tests.sh`: it uses PyPy (`pypy3` on `PATH` or
`$PYPY`) when there is one, about 28 times faster than CPython (the
whole suite in under a minute, against about 25 minutes), and falls back
to `python3`. `test_ec.py`, `ec_emu.py` and `i8051_emu.py` run unchanged
under PyPy 7.3 (Python 3.11). On macOS take the official build
(`https://downloads.python.org/pypy/pypy3.11-v7.3.20-macos_arm64.tar.bz2`,
unpacked anywhere, `PYPY=<dir>/bin/pypy3`): Homebrew's `pypy3.10` bottle
does not install. `ec_ntc_table.py` needs numpy: run it with `python3`.

Not the same: the version string (`V1.07-open`); the vendor's probes of
parts the O6 lacks (PMICs, IO expander, light sensor, an SMBus scan),
its debug console and its keyboard, HID, battery and charger code are
left out; GPIO reads (0x93), which return the previous read, can see a
write a hook earlier or later than the vendor's; the vendor's slow hooks
run at slightly different moments (within a period: after a power-up from
G3 its first thermal pass comes a hook earlier).

The EC runs from its internal e-flash; a new image in the SPI flash
(offset 0x10000) is copied in only when UEFI's platform reset (host
command 0x3E19) asks the EC to mirror and reset, which the open firmware
does the same way. Programming its own e-flash (the AC-loss policy, the
power-off reason) runs from scratch ROM: the model copies what SCAR0 maps
and stops on any code fetch from the e-flash while it erases or programs,
and both firmwares pass. What the model cannot show is the real chip:
pin timing, the PD controllers' and CSU_PM's real answers, the fan, the
mirror itself. A first test on a board should keep a way back: an SPI
programmer for the flash, and Radxa's image to mirror back (the open
firmware's 3E19 does that too).

## Roadmap

1. Boot-test `--open` on an O6 (UART log of BL2, BL31 and OP-TEE; variables
   persist across a cold boot; Secure Boot keys enrol; suspend and resume).
   `--vendor-stmm` isolates the StandaloneMM image if variables misbehave.
   Then make it the default for the chain-A boards.
2. Boot-test the open SE (UART5 log; `--vendor-se` to compare), then
   suspend/resume and the SFH.
3. CSU_PM from source. The open firmware matches CIX's in the emulator at
   boot, over SCMI, for CIX's PM messages, through suspend/resume,
   shutdown and reset, and at run time (DPM, fans, thermal), with each
   board's PM configuration, and `--open` builds it into bootloader1
   (`--vendor-pm` keeps CIX's). Next: a hardware boot test (UART4 says
   `CSU_PM: open firmware`; then SCMI from Linux: cpufreq, clocks,
   sensors, fans on the O6N; suspend/resume)
   ([plan](../firmware/csupm/README.md#plan)).
4. The O6's EC (`--open-ec`): flash, then UEFI's platform reset (3E19)
   mirrors it in; check UART1 (`Orion O6 EC V1.07-open`), power on and
   off, the fan, the AC-loss setting, Linux's cros-ec. Radxa's image goes
   back the same way ([EC firmware](#ec-firmware-orion-o6)).

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
on UART5 rejected bootloader1 (or, with the open SE, failed before its
UART came up: rebuild with `--vendor-se` to tell which); SE output without AP output points at BL2 or
the bootloader2 signatures, or at CSU_PM: the SE starts the AP only after
the PM's handshake (`0x5c5a`, `0x55ca`, `0x5a5c`), so an SE log that stops
waiting for the PM, or a silent UART4, points at the open CSU_PM
(`--vendor-pm` to compare).
