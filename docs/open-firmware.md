# Open firmware before BL33

Goal: build, debug and modify everything that runs before UEFI (BL33) from
source. This page tracks what is open, how to build it, what is still
missing and in which order to close the gaps. What each binary is and where it
came from is in [closed firmware](closed-firmware.md).
How each open binary differs from the vendor's: [differences.md](differences.md).

## Boot chain and status

```
mask ROM ─▶ SE firmware ─▶ CSU_PM ─▶ TF-A BL2 ─▶ BL31 + OP-TEE (StMM) ─▶ BL33 UEFI
            └──────────── bootloader1 ───────────┘ └──── bootloader2 ────┘   bootloader3
```

| Stage | Source | Status here |
|---|---|---|
| SE firmware (Cortex-M33: SoC bring-up, eFuse, image checks, AP services) | [firmware/se](../firmware/se), translated from CIX's debug build, DDR driver included | **built from source** (`--open`; `--vendor-se` keeps CIX's); same register writes, reads and memory contents as CIX's in [`sky1emu se-test`](../emu/README.md#se-and-sfh-cortex-m33) but for what [firmware/se/differences.md](../firmware/se/differences.md) lists ([below](#se-firmware)) |
| CSU_PM (Xtensa LX7, Arm SCP-firmware based) | [firmware/csupm](../firmware/csupm): SCP-firmware v2.11.0 and CIX's code translated from its debug build | **built from source** (`--open`; `--vendor-pm` keeps CIX's); does what CIX's does in the emulator ([`sky1emu pm-test`](../emu/README.md)) at boot, over SCMI, for PM messages, through suspend/resume, shutdown and reset and at run time (DPM, fans, thermal), with each board's PM configuration; not yet boot-tested on hardware ([below](#csu_pm)) |
| TF-A BL2 | `arm-trusted-firmware` + patches 0002, 0008 | **built from source** (`--open`, chain A), re-signed into bootloader1; does what CIX's does from its entry to the jump to BL31 in the emulator ([`sky1emu a64-test`](../emu/README.md#bl2-and-bl31-aarch64)) but for what [differences-bl2-bl31.md](differences-bl2-bl31.md) lists |
| TF-A BL31 | `arm-trusted-firmware` + patches 0004-0008 | **built from source** (`--open`); same cold boot and same answer to every SMC as CIX's in the emulator, but for what [differences-bl2-bl31.md](differences-bl2-bl31.md) lists |
| OP-TEE BL32 | `optee_os` + patch queue | **built from source** (`--open`) |
| StandaloneMM inside OP-TEE | EDK2 + [Sky1 StandaloneMm](../edk2-project/Platform/CIX/Sky1/StandaloneMm) | **built from source** (`--open`; `--vendor-stmm` embeds CIX's instead) |
| FIP certificates, `fiptool` | TF-A `cert_create` (patched), `fiptool` | **open** for bootloader2 and bootloader3 |
| bootloader1 container (`CIXBTFF!`) | — | **open**: parse, verify, replace, re-sign (`cixfw.py bl1`) |
| SPI image packer (`cix_package_tool`) | — | **open**: `cixfw.py pack`, used by every build |
| `memory_config`, `pm_config` | CIX generators | built from source where the board package has them |
| Sensor fusion hub firmware (`sfh_fw.bin`, Cortex-M) | [firmware/sfh](../firmware/sfh) | **built from source** (`--open`); same hardware writes as CIX's in every phase of [`sky1emu sfh-test`](../emu/README.md#se-and-sfh-cortex-m33) ([differences](../firmware/sfh/differences.md)) |
| EC firmware (`ec_fw.bin`, Orion O6 only; ITE IT5570E, 8051) | [firmware/ec](../firmware/ec) | **written from scratch** after Radxa's EC V1.07; same pin changes, I2C transfers, host-command answers, e-flash writes and LED as the vendor's in all 61 scenarios of `sky1emu ec-test` ([emu](../emu/README.md#ec-orion-o6)) but for the differences listed in [differences.md](../firmware/ec/differences.md), and the update to it and back works in the model; `--open-ec`; not hardware-tested ([below](#ec-firmware-orion-o6)) |
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
pre-BL33 firmware                               1379 KB   ████████████████████ 100% open
├── bootloader1
│   ├── SE firmware (Cortex-M33)                 141 KB   ████████████████████ 100% open (CIX's: 334 KB)
│   │   ├── start-up, eFuse, flash, crypto, key manager,
│   │   │   SoC bring-up, CI-700, secure boot, services        open (firmware/se)
│   │   ├── DDR driver (DRAM training)                  open (firmware/se/ddr)
│   │   └── PVT table (V/F selection data)              CIX's data as generated source
│   ├── CSU_PM (Xtensa LX7)                      206 KB   ████████████████████ 100% open (CIX's: 327 KB)
│   │   └── by CIX's code (198 KB; 79% translated, the rest replaced or CLI-only)
│   │       ├── SCP-firmware framework, modules   51 KB   ████████████████░░░░  80% translated
│   │       ├── CIX Source/projects/sky1          30 KB   ███████████████████░  96% translated
│   │       ├── CIX Source/common                 71 KB   ██████████████░░░░░░  72% translated
│   │       ├── FreeRTOS, Cadence runtime         29 KB   upstream FreeRTOS, own port and libc
│   │       └── debug CLI                         16 KB   not ported
│   └── TF-A BL2                                  92 KB   ████████████████████ 100% open
├── bootloader2
│   ├── TF-A BL31                                 94 KB   ████████████████████ 100% open
│   └── OP-TEE with StandaloneMM                 842 KB   ████████████████████ 100% open
└── sensor fusion hub firmware (Cortex-M)          4 KB   ████████████████████ 100% open

open    1379 KB  SE (141), CSU_PM (206), BL2 (92), BL31 (94), OP-TEE + StMM (842), SFH (4)
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
equivalence; the emulator's tests (`sky1emu pm-test`) check behaviour. What is left is mostly code
the open firmware replaces rather than translates (CIX's timer, event and
job framework and IPC arbiter, by SCP-firmware's events, alarms and
transport; FreeRTOS's Xtensa port and CIX's I2C state machine for tasks,
by our port of upstream FreeRTOS and an I2C driver that waits for the same
interrupts with CIX's register sequence) or that only CIX's debug CLI reaches (CI-700 event
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

1. BL31: `make PLAT=sky1 SPD=opteed SMP=1 ENABLE_FEAT_HCX=1 bl31` in
   `arm-trusted-firmware` (CIX `cix_p1_k6.6_2025q3_tfa_open_dev`, TF-A
   v2.7 + `plat/cix`, with the patch queue). `SMP=1` and
   `ENABLE_FEAT_HCX=1` are what CIX's own build passes, read off its BL31
   in the emulator: without the first BL31 is CIX's single-core variant,
   which does not get past its GIC setup on the core the SE boots.
2. StandaloneMM: `build -p Platform/CIX/Sky1/StandaloneMm/Sky1StandaloneMm.dsc`
   (see [below](#standalonemm)). With `--vendor-stmm` the build instead
   decompresses CIX's image from the OP-TEE in the reference bootloader2
   (image `…_open_cixstmm.bin`).
3. OP-TEE: `make PLATFORM=cix-sky1 CFG_ARM64_core=y ta-targets=ta_arm64
   CFG_STMM_PATH=<StMM FV>` in `optee_os` (CIX OP-TEE 3.17 + `plat-cix`);
   the build checks that OP-TEE carries exactly that StMM image.
4. bootloader2: certificates with TF-A `cert_create`, every key the
   published CIX key (`cix_privatekey.pem`), the NV counter of the vendor
   FIP (31 on the O6); patch 0001 lets the trusted key certificate carry
   only the trusted-world key, as CIX's does. `fiptool create`, then an
   independent check (`cixfw.verify_fip`): root, key and content
   certificates, NV counters and the BL31/BL32 hashes.
5. BL2: `make PLAT=sky1 SMP=1 ENABLE_FEAT_HCX=1 TRUSTED_BOARD_BOOT=1
   ROT_KEY=cix_privatekey.pem bl2`
   with mbed TLS 2.28 (submodule `mbedtls`). The root of trust is the
   SHA-256 of that key's public half, built into BL2.
6. SE firmware: `make -C firmware/se` builds `se_fw.bin` (arm-none-eabi
   GCC, Mbed TLS), DDR driver included; the build no longer needs CIX's
   debug build. `--vendor-se` skips this (image `…_open_cixse.bin`). `--cix-ddr` builds it with CIX's
   DDR driver relinked unchanged from their debug build
   (`make DDR=cix CIX_ELF=…`, fetched by `scripts/open/cix_debug.py`) in
   place of `firmware/se/ddr`; the image is `…_open_cixddr.bin`.
7. CSU_PM: `make -C firmware/csupm all check` (Espressif's Xtensa GCC with
   the pm_core_v1 description, every instruction checked against the
   core's) builds `csupm.xbin`, the XBIN image the SE loads.
   `--vendor-pm` skips this (image `…_open_cixpm.bin`).
8. bootloader1: the reference bootloader1 (the board's, or CIX's with
   `--boot-firmware cix`) with its AP image replaced by BL2, its SE image
   by the open SE firmware and its CSU_PM image by the open one, and the
   signatures redone with the published BL1 keys (`oem_privatekey.pem`,
   `cix_privatekey.pem`). BL2 and the SE firmware are zero-padded to the
   size of CIX's so the container keeps its layout; CSU_PM is not (the SE
   hashes only the XBIN's sections and finds the signature from the
   image's length): it ends in CIX's slot and its signature moves down,
   nothing else moves. `sky1emu se-test --secure` checks that the open SE
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
| `arm-trusted-firmware` | `114fb205` (CIX, 2026-03-10) | 0001, 0003 `cert_create`: certificates that carry only one world's key; 0002 BL2 platform support; 0004 TrustZone configuration in BL31; 0005 audio domain and resume marker after system suspend; 0006 NI-700 QoS as in the CIX BL31; 0007 normal-world SiP argument checks; 0008 xSPI: a read across a 2 MiB boundary no longer reads on (CIX's fix of July 2026) |
| `mbedtls` | `8b3f26a5` (2.28.0) | none |
| `edk2` | CIX `9212cf39` | 0001, 0002 Secure Boot defaults and TPM page ([parity](parity.md)); 0003 the StandaloneMM core builds CIX's NVRAM information HOB |
| `optee_os` | `cc66640f` (CIX, 2025-11-11) | 0001-0005 from [orion-unofficial/edk2-cix](https://github.com/orion-unofficial/edk2-cix) `source/unofficial/cix/1.2/op-tee/3.17.0` (BSD-2-Clause, authorship kept): compiler fixes, fail-closed hardware entropy and key return, FF-A 1.2 StandaloneMM loader behind `CFG_STMM_FFA_VERSION=12` (default 10 keeps the vendor StMM interface), stop StMM on a failed NOR transaction; 0006 NOR secure storage only where the TrustZone configuration puts it; 0007 the secure storage key derived as CIX's OP-TEE derives it |

They are applied by `scripts/prepare.py` like the EDK2 queues
([architecture](architecture.md#patch-queue)).

## How the open firmware differs from CIX's

What `--open` builds for the O6 and O6N differs from CIX's firmware only
where listed here; everything else matched CIX's write for write in the
emulator tests (SE: boot, every run-time request, DRAM initialisation
with four boards' memory configurations, suspend/resume, secure boot,
crash records, failures, [all of it](../firmware/se/differences.md); CSU_PM: boot, SCMI, PM messages, power
transitions, run time, all five boards' PM configurations and one with
the options no board sets; SFH: every
hardware write and the time of every wake-up). The open SE and CSU_PM have not run on hardware, and
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
| SE | secure boot stops at a CSU_PM image that fails its digest or signature | logs the error and starts the PM all the same: secure boot does not protect CSU_PM ([differences](../firmware/se/differences.md#faults-of-cixs-image-that-the-open-one-does-not-have)) |
| SE | the eFuse requests (`0x8200000e`, `0x82000002`) check address, offset and length | unchecked: the OS's mailbox can overflow the SE's stack and reset the system, and read past the fuse array |

Left out, debug or unused:

* SE: the UART debug monitor; the DDR driver's training report and
  register dumps, and its BIST pass/fail arrays (only printed; the BIST
  runs are kept); the clock-tree and power-domain parts of the hang
  record (raw register dumps under names of their own instead).
* CSU_PM ([below](#csu_pm-differences)): the debug CLI and what only it reaches;
  CIX's code overlays; CIX's timer, event and job framework (SCP-firmware
  events and alarms instead, with CIX's periods, under upstream FreeRTOS
  tasks); CIX's I2C state machine for tasks (the same interrupts and
  register sequence, with time-outs and bus recovery).
* BL2: bootloader3 backup recovery (no board's layout has it). BL31: the
  uncalled flash write code. Both: CIX's extra POST codes.

Behaviour that differs slightly:

* CSU_PM: the run-time services start 1 ms after the ready handshake
  (CIX's from its event worker once it first idles); the heartbeat timer
  is created once (CIX's adds one on every resume); its tick and timers
  keep their period while the SoC idles at 200 MHz (CIX's run at half
  speed); under the DPM's cap, `PERFORMANCE_LEVEL_GET` answers the level
  that runs and no notification is sent for a level that does not change
  ([below](#csu_pm-differences)).
* StandaloneMM: a failed flash program or erase re-reads the block; partial
  writes land at their offset.
* SE: a memory-configuration block lookup that stays inside its table; a
  write enable per erased flash sector; the DDR driver's density table
  padded so it is not read past its end; messages to the AP sent without
  CIX's 100 ms wait; refused requests answered as errors (the full list:
  [firmware/se/differences.md](../firmware/se/differences.md)).
* bootloader1: CSU_PM's image is not padded to CIX's size and its
  signature moves down (the SE hashes the image's sections only); the
  container is otherwise CIX's, re-signed with the same published chain-A
  keys.

Not functional:

* versions: the PM reports its build version and commit (CIX's:
  `00.12.001`), the SE `beta2.0-<version>`;
* images built with GCC and Mbed TLS rather than CIX's toolchains and
  crypto library, and smaller (SE 141 KB, CIX's 334 KB; CSU_PM 206 KB,
  CIX's 327 KB);
* CSU_PM's RAM log holds its messages (CIX's line buffer is never flushed,
  so its log holds stray bytes);
* CIX's oddities that do not harm are kept as CIX has them and commented
  in the source, not fixed (in CSU_PM and the DDR driver, e.g. the memory
  configuration write-back writing as many bytes as the checksum's value).

The open BL2 and BL31 were written from the CIX binaries in the 2026Q3
bootloader1/bootloader2 (TF-A `v2.7(release):Beta_2.1.5_release`,
built 2026-07-15): functions matched against a build of the public tree
(`funcmatch.py`), the rest decompiled. They are checked by running both
pairs in an emulator ([`sky1emu a64-test`](../emu/README.md#bl2-and-bl31-aarch64)):
BL2 from its entry to the jump to BL31 in 34 configurations, BL31's cold
boot and every SMC. That found what reading had not (BL2 refusing every
certificate with a counter, BL31 built as the single-core variant, a
flash read running on past the image, four places of the suspend
sequence), all fixed; the complete list of what differs now, with the
evidence for each, is [differences-bl2-bl31.md](differences-bl2-bl31.md).
In short, where they differ on purpose:

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
* No POST codes at `0x05030F00`; they are a debugging aid (UEFI writes
  its own).
* The firmware header is used only with its magic; bootloader3 as the
  vendor's tool certifies it (under another root key) is refused.

### BL31

The public BL31 is the CIX tree; the patches add what the CIX BL31 has and
it lacks, and the build passes what CIX's passes (`SMP=1`,
`ENABLE_FEAT_HCX=1`). In the emulator its cold boot writes what CIX's
writes (every NI-700 and CI-700 QoS entry, the GIC, the power domains,
RAS) and it answers every SMC as CIX's does, but for:

* **TrustZone configuration** (patch 0004), as the CIX BL31 reads it: SPE for
  the normal world follows `spe_feature_en`, and the data bytes go to
  `0x80400000` for OP-TEE. Unlike CIX, a secure storage region that overlaps
  the flash header or an image it lists is dropped (see below).
* **System suspend** (patch 0005): the audio domain sequence and the `0xBB`
  resume marker in `SW_USED_REG1` the CIX BL31 has on the first core back,
  the suspending core in `SW_USED_REG2`; the SE's suspend command carries
  the state alone (CIX's sends three bytes of its stack with it).
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
[`sky1emu pm-test`](../emu/README.md); the details are in
[firmware/csupm/README.md](../firmware/csupm/README.md); every difference,
with what each firmware does and where it is shown, is in
[firmware/csupm/differences.md](../firmware/csupm/differences.md).
In short:

* No debug CLI, and nothing only it reaches: CI-700 event collections, the
  DFD trace, GPIO interrupt helpers, forced operating points; no log
  dumps.
* Tasks: upstream FreeRTOS (V11.1.0, submodule) with our Xtensa port,
  co-operative: SCP-firmware's loop, a PMIC worker, a board-sensor worker
  and idle. CIX's timer, event and job framework becomes SCP-firmware
  alarms and events (`dpm` and `pm_svc` modules) with CIX's periods and
  order. CIX's 32 KB of code overlays are not needed.
* I2C: the bytes on the bus are CIX's. A transfer gives up after 10 ms
  (CIX's waits 600 ms), a time-out or lost arbitration resets the
  controller (CIX's has no recovery), and a bus that fails three times in
  a row backs off from 100 ms to 2 s. CIX's PMIC voltage write retries for
  ever on a dead bus; ours stops after three rounds, and a failed voltage
  write raises no frequency (`sky1soc_perf`, DVFS, `dpm_edp_set_voltage`;
  [details](../firmware/csupm/README.md#tasks-and-i2c)).
* The run-time services (fans, PVT polling, board sensors, watchdog) start
  1 ms after the ready handshake; CIX's start from its event worker once
  it first idles.
* The heartbeat timer is created once; CIX's creates another on every
  resume, so after one two timers toggle the LED.
* The tick and the timers keep their period when the core's clock
  changes; CIX's tick keeps its cycle count, so while the SoC idle switch
  has the core at 200 MHz its tick, DPM ticker, heartbeat and timers run
  at half speed.
* The CPU clusters and the DSU have one owner, `sky1soc_perf`. Over SCMI:
  a DSU level by message sets the PLL's post-divider (CIX's DVFS
  programs the PLL); `PERFORMANCE_LEVEL_GET` answers the level that runs
  after a fast channel or under the DPM's cap (CIX's the level of its last
  message); under the cap no notification is sent for a level that does
  not change (CIX's sends one at the next message); a level set while the
  cap comes down follows each step of it.
* SCMI: a message of a protocol the PM has not got is answered
  `NOT_SUPPORTED` (CIX's image dies), `SYSTEM_POWER_STATE_SET` with a
  state that does not exist `INVALID_PARAMETERS` (CIX's asserts); the
  clock protocol has no `CLOCK_NAME_GET` or rate notifications (CIX's has;
  no clock offers them).
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
[`sky1emu sfh-test`](../emu/README.md#se-and-sfh-cortex-m33) runs both
images on a model of the mailboxes, the timers (which count) and the
control block, in step by simulated time, through 162 phases: boot, the
three messages with every delay, every command number and length on both
channels, several messages at once, every interrupt and fault vector. The
writes, their order, the registers read and the times of the wake-up
requests are the same but for what
[firmware/sfh/differences.md](../firmware/sfh/differences.md) lists: CIX's
image answers tens of milliseconds late once it has resumed (it logs
through a UART pointer that is 0), and its vector table ends three
interrupts early.

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

[`sky1emu se-test`](../emu/README.md#se-and-sfh-cortex-m33) runs CIX's
debug build and the open one on the same flash image, on a model of the
SoC as the SE sees it (boot ROM hand-over, both flash controllers, PLLs,
power controllers, mailboxes, watchdog, timers, the PM's and the sensor
hub's handshakes, the DDR controllers), in step by simulated time, and
compares every register write in order, the registers read, the answers
and what ends up in DRAM, the PM's SRAM, the hub's SRAM and the flash:

* cold boot from reset to the idle loop: 2039 writes to 844 registers in
  the same order, for 8 life cycles with and without device keys, 11 boot
  reasons, 14 flash parts and 24 damaged pieces of the flash image;
* every run-time request (`0x82000001` to `0x82000020`) on both mailboxes
  with valid, boundary and invalid arguments (`--services`);
* suspend to RAM and resume five times over, with the sensor hub started
  and stopped and every wake-up source (`--suspend`); the hub image the SE
  loaded is then started in the hub's model;
* secure boot fused for the image's keys (`--secure`, both firmwares, 20
  cases): the image as signed boots; an altered BL2, header, key or key
  hash stops both. An altered CSU_PM stops only the open one: **CIX's
  starts a PM image that failed its digest or signature check**;
* crash records and watchdogs (`--crash`), PLLs, PM and hub that do not
  answer (`--failures`), and the code no board's image takes: the flash
  behind the FCH's xSPI controller, trim and repair data in the fuses,
  debug certificates, two DDR channels (`--paths`);
* DRAM initialisation with the driver live (`--ddr`, on a model of the
  controllers, PHYs and PI blocks inferred from what CIX's driver does):
  51 634 writes to 9759 registers in the same order on the O6, and the
  same for the O6N's, the Orange Pi 6's and the 6 Plus's memory
  configurations (the MS-R1's is of a format both firmwares refuse);
  19 feature settings, 7 kinds of parts, 9 failures and every interrupt
  (`--ddr-suite`).  Training itself happens in the PI hardware and is
  only modelled.

The tests run 81 % of the open firmware's code; the functions they do not
reach cannot be reached in it. Every difference, with the place in CIX's
image it comes from, is in
[firmware/se/differences.md](../firmware/se/differences.md): four things
refused for security (below), what is left out, and a dozen faults of
CIX's image (the secure-boot hole above, a stack overflow from the
non-secure mailbox, uninitialised message headers, a 100 ms wait in an
interrupt handler).

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
* a CSU_PM image that fails its check under secure boot stops the boot;
* the eFuse requests check their address, bit offset and length;
* no debug monitor; a memory-configuration block lookup that is correct
  where CIX's reads past its table; a write enable per erased sector.

Not done: the clock and power records of crash reports are raw register
dumps (named `CLK_RAW` and `PWR_RAW`, so that nothing takes them for
CIX's).  The open SE has not been boot-tested on hardware.

### CSU_PM

CSU_PM (`csupm_sky1`, 00.12.001, in the same repository, with full DWARF)
is Xtensa LX7 (core `pm_core_v1`, Cadence XtensaTools 14.10 clang):
FreeRTOS, Arm SCP-firmware (both public) and about 100 KB of CIX code (clock
tree and PLLs, V/F tables and DVFS, DPM, PMICs, DDR low power, system power
states, CI-700/NI-700 counters, messages, a debug CLI).
[firmware/csupm](../firmware/csupm) rebuilds it: the core has no user TIE,
so Espressif's GCC builds for it with a core description made from the
instruction set CIX's own build uses (`xtensa_core.py`), and
`xtensa_isa.py check` keeps every build inside that set. `xtensa_flix.py`
decodes the core's FLIX bundles. The emulator, [emu](../emu/README.md)
(Rust, `sky1emu`), interprets the core
([emu/src/xtensa](../emu/src/xtensa)) and models the SoC around it
([emu/src/pm/soc.rs](../emu/src/pm/soc.rs): I2C and PMICs, PLLs,
mailboxes, interrupts, the SE's side, S5 and DDR controllers as CIX's
image expects them).

The open firmware is SCP-firmware v2.11.0 and FreeRTOS-Kernel V11.1.0
(submodules, FreeRTOS with our co-operative Xtensa port) with CIX's changes
to SCP-firmware's modules, CIX's own modules and CIX's sources translated function by
function from the debug build. In the emulator, against CIX's image
([`sky1emu pm-test`](../emu/README.md); both images asked the same things
in lock-step on simulated time):

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
| what the short suites leave out (`--full`, 27 phases: every SCMI message on every channel, every domain switched and every level set, notifications, malformed messages, every PM message id, PCC commands, the I2C slave the EC reads and its errors, the fans by time, a temperature sweep from 30 to 130 C, the thermal and GPIO interrupt lines, the SoC idle switch and the DDR's frequency, the watchdog and its interrupt, a second suspend) | all but the expected differences below |
| the five boards' PM configurations (O6, O6N, MS-R1, Orange Pi 6 with MP2845 PMICs and shared bucks, Orange Pi 6 Plus) | every phase above |
| the options of the BIOS's PM configuration no board sets, on the O6N's (watchdog, heartbeat and EC GPIOs, the SoC idle switch's thresholds with DDR controllers that can switch frequency, the four skin temperature controllers) | every phase above |
| I2C buses made to hang or fail (`sky1emu pm-faults`, open image alone) | every check |
| V/F tables on simulated parts (`sky1emu pm-vf --matrix`: straps, fuses, ATE phases, part numbers, BIOS configurations, Phecda; 78 cases) | every case |

Differences that are known and meant, or are CIX's image failing, are
listed with their reason in the emulator's table and not counted (about
705 to 730 per standard configuration, in six to ten kinds; 827 in ten
kinds with the options no board sets): CIX's image dies on an
SCMI protocol it has not got and asserts on a system power state that does
not exist (ours answers with an error); its clock protocol has
`CLOCK_NAME_GET` and rate notifications, ours not; the DSU's level by
message, `PERFORMANCE_LEVEL_GET` after a fast channel or under the DPM's
cap, a level set while the cap comes down and CIX's notification of a
level that already runs at the next message (one owner, `sky1soc_perf`,
in ours); CIX's PVT warning handler computes in floating
point and, taken while the idle task runs, panics in FreeRTOS's
coprocessor handler (CIX leaves that line masked, so not as shipped; ours
takes it); CIX's fan message 0x802 empties the user's table when it
refuses one, which asserts at the fans' next pass; and timing of the fans'
loop and the DPM's PID and grant values at a phase's ends. Anything else
is a failure; there are none in the seven configurations. The list is in
[emu/README.md](../emu/README.md#expected-differences); what each means
for the OS, in
[firmware/csupm/differences.md](../firmware/csupm/differences.md).

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

The O6's EC (ITE IT5570E, 8051; pins from Radxa's V1.2 schematic, sheet
35, in [emu/src/ec/board.rs](../emu/src/ec/board.rs)) sequences the
board's power (always-on rails, `RSM_RST`, the SoC's power button and
sleep signals, S0 rails, the PMIC enable gate), runs the fan from the
SoC's PVT temperature (read from CSU_PM's I2C slave) and five board NTCs,
sets the two RTS5453H PD controllers up, and answers the host on I2C6 at
0x76 (CIX's ChromeOS-EC style host commands: UEFI's `Ite5570EcLib`, and
Linux's `google,cros-ec-i2c` with CIX's `cix-ec-*` drivers in device-tree
mode). Radxa's image is a Keil C51 build of CIX's EC code base, mostly
laptop code the O6 never runs.

[firmware/ec](../firmware/ec) is a new firmware (SDCC) that does what the
vendor's does on the O6: its sequencer tables, suspend to RAM and the
resume, its thresholds and duty table, its NTC conversion (the vendor's
own routine run over every ADC code: `sky1emu ec-ntc-table`), its
host-command answers byte for byte, including the persistent buffers some
commands read leftovers from, its e-flash records (AC-loss policy at
0x1F8FF, power-off reason at 0x1FCFC), the watchdog it arms after an NTC
trip, the power LED (breathing in suspend). The emulator is part of
`sky1emu` ([emu/README.md](../emu/README.md#ec-orion-o6), feature `ec`):
its own 8051 interpreter, the IT5570 (timers, interrupt and wake-up
controllers, GPIO, ADC, PWM, SMBus masters and slaves, the flash follow
mode, the e-flash, the external watchdog) and the board (rails and power
goods, the SoC's sleep signals, CSU_PM as its firmware answers, the PD
controllers, NTCs, the fan). `sky1emu ec-test` runs both images through
61 scenarios and compares what the board sees, part by part (the power
pins and the e-flash, the thermal pass, the PD bus, the host interface,
the LED), in order and with a timing tolerance: power-on, S0, shutdown,
suspend to RAM and the wakes, AC lost in S0, S3 and standby, rails that
never come or are lost, the SoC's THERMTRIP, a stalled fan, open and
shorted NTCs, CSU_PM and the PD controllers not answering or their buses
hung, an e-flash that does not take a write, an NTC trip and the
watchdog, every fan mode, every host command id of the vendor's table and
its neighbours in standby, S0 and S3, malformed and cut-short requests,
commands back to back, runs of minutes. It also installs each image over
the other the way UEFI does (the new image in the SPI flash, host command
3E19, the mirror reset) and checks that the new one boots, reaches S0 and
answers. The result: no difference besides the ones listed, each with its
reason, in [firmware/ec/differences.md](../firmware/ec/differences.md);
the open image's functions all run (99 % of its code bytes; `ec-cov`).
The suite takes about 35 s. The Python emulator it replaced
(`i8051_emu.py`, `ec_emu.py`, `test_ec.py`) is gone; `ec-test --port`
still runs that test's 16 scenarios its way.

Found by it: on top of what the open firmware did not do yet (suspend to
RAM: the S0 rails never came back after a resume; the fan's duty not set
again after a mode change or a resume; a 1 ms tick running ~2 % slow),
faults of Radxa's image the open one does not reproduce: a request with
a bad checksum is answered "invalid checksum" but run; two GPIO writes
(0x92) within 100 ms lose the first; a PD controller that does not answer
stops its main loop for 16.5 s at each set-up (a button press then is
lost). The list, with the evidence: differences.md.

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
2. Boot-test the open SE (UART5 log; `--vendor-se` to compare, `--cix-ddr`
   to tell the DDR port from the rest of the SE), then
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
UART came up: rebuild with `--vendor-se` to tell which). An open SE that
stops in memory training, or a board whose DRAM misbehaves, gets
`--cix-ddr`: the open SE with CIX's DDR driver; if that boots, the fault is
in `firmware/se/ddr`. SE output without AP output points at BL2 or
the bootloader2 signatures, or at CSU_PM: the SE starts the AP only after
the PM's handshake (`0x5c5a`, `0x55ca`, `0x5a5c`), so an SE log that stops
waiting for the PM, or a silent UART4, points at the open CSU_PM
(`--vendor-pm` to compare).
