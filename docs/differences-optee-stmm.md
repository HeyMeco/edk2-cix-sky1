# How the open OP-TEE and StandaloneMM differ from CIX's

The open OP-TEE (BL32) is CIX's public `optee_os` tree with this
repository's patch queue (`patches/2026Q3/optee_os`); the open
StandaloneMM (StMM, the UEFI variable store inside OP-TEE) is built from
EDK2 and [Platform/CIX/Sky1/StandaloneMm](../edk2-project/Platform/CIX/Sky1/StandaloneMm).
`--open` builds both into bootloader2, with the open BL31
([differences-bl2-bl31.md](differences-bl2-bl31.md)). This page lists
what the repository's documents say differs from CIX's OP-TEE and StMM,
and, under each heading, what differs in the containers, certificates and
configuration data `--open` also builds (bootloader1, bootloader2,
bootloader3, the SPI image, `trustzone_config`, `memory_config`,
`pm_config`), which have no document of their own. UEFI (BL33) is not
covered here: see [differences.md](differences.md).

## Status

What it is: OP-TEE 3.17 from CIX's public tree
(`cixtech/cix_opensource__tee__op-tee`, branch
`cix_p1_k6.6_2025q3_tfa_open_dev`, pinned at `cc66640f`, 2025-11-11) with
patches 0001 to 0007 ([docs/open-firmware.md](open-firmware.md#patch-queues),
[docs/architecture.md](architecture.md)), built with
`make PLATFORM=cix-sky1 CFG_ARM64_core=y ta-targets=ta_arm64
CFG_STMM_PATH=<StMM FV>`; and StandaloneMM, a `PlatformMmStandalone` build
of EDK2's core, CPU, variable and fault tolerant write drivers with three
drivers written here ([open-firmware.md, StandaloneMM](open-firmware.md#standalonemm)).

Compared with: the OP-TEE in CIX's 2026Q3 bootloader2
(`Beta_2.1.5_release-3.17-67e342df9765`, built 2026-08-03, with CIX's
StandaloneMM embedded through `stmm_sp.c`); the Orion O6/O6N's own
bootloader2 carries an older CIX build (`Beta_2.1.5`, 2026-03-24)
([closed-firmware.md](closed-firmware.md)).

How: **from the sources' and the patches' documentation and from reading
CIX's binaries, not by running the two side by side.** No emulator runs
OP-TEE or StandaloneMM; in `sky1emu a64-test` OP-TEE is played by the
harness, which answers BL31's entries with fixed values
([differences-bl2-bl31.md](differences-bl2-bl31.md#known-limits-and-open-points)).
The StMM's NOR and FVB code alone is tested on the host against a
simulated flash (`scripts/open/test_stmm_nor.py`).

**Neither has run on hardware.** "No `--open` image has been
boot-tested yet" ([open-firmware.md](open-firmware.md#building));
patch 0004 says of its FF-A 1.2 mode: "No modern hybrid image or hardware
boot is claimed." What only a board can show: whether OP-TEE and StMM boot
behind the open BL31, whether UEFI variables persist across a cold boot,
and whether secure storage written by one firmware is read by the other.

## What is the same

* StandaloneMM: the firmware volume has the same files, file GUIDs and
  order as CIX's. `FaultTolerantWriteStandaloneMm`,
  `VariableStandaloneMm` (EDK2 with CIX's `VariablePcdLib`) and
  `StandaloneMmCpu` are unchanged EDK2; `StandaloneMmCore` with patch 0003
  does what CIX's does: it turns the NVRAM location OP-TEE appends to the
  boot information (variable store, FTW working and spare: three flash
  offsets, three sizes) into a GUID HOB (`4c8167fc-5a9d-468e-a942-861fb50ab539`,
  36 bytes). The three new drivers were written from their interfaces
  (the UEFI-side clients and OP-TEE's `stmm_sp.c` are public) and checked
  against the CIX binaries ([open-firmware.md](open-firmware.md#standalonemm)).
* The NOR driver uses the commands CIX's does and every flash in the
  bootloader1 table supports (single-line, 3-byte addresses, READ, PAGE
  PROGRAM, 4 KiB SECTOR ERASE), writes in place where new data only clears
  bits and erases otherwise, and formats the store when its headers are
  invalid, as CIX's does. StMM reaches the flash and the console through
  FF-A direct requests to OP-TEE (endpoints 101 and 100), which turns
  flash requests into `CIX_SIP_NOR_STORAGE` SMCs, as CIX's does.
* OP-TEE: the CIX platform (`plat-cix`), HDCP2 and the crypto-engine
  driver (`lib/libmbedtls/core/te_driver`, `lib/libhosal`) are CIX's public
  code. The secure storage key is derived as CIX's OP-TEE derives it (patch
  0007, below), so the two firmwares read each other's secure storage
  objects. `CFG_STMM_FFA_VERSION` defaults to 10, which keeps the vendor
  StMM interface (patch 0004).
* The source-built images sit where the CIX ones do (OP-TEE `0x80500000`)
  and start with the same entry code ([open-firmware.md](open-firmware.md#building)).
* Containers and data: the SPI image packer (`cixfw.py pack`) is
  byte-identical to `cix_package_tool` for the full and OTA images of all
  five boards; the bootloader1 container is CIX's, re-signed with the same
  published chain-A keys, and re-signing the Radxa O6 BL1 with them changes
  only the four signatures; `trustzone_config` is CIX's stock binary on
  every board, identical to its generator's output apart from its
  timestamp ([open-firmware.md](open-firmware.md#tools)).

## What is not the vendor's code

* OP-TEE: CIX's binary has code missing from the public tree: the secure
  storage key derivation (`cix_ssk_gen_string`, matched by patch 0007),
  the eFuse read through TZ shared memory, and a few lock paths
  ([closed-firmware.md](closed-firmware.md)). Patches 0001 to 0005 come
  from [orion-unofficial/edk2-cix](https://github.com/orion-unofficial/edk2-cix)
  (`source/unofficial/cix/1.2/op-tee/3.17.0`, BSD-2-Clause, authorship
  kept): 0001 "the eight substantive source adaptations" for the compiler
  used by those builds, 0002 the compiler and output-buffer fixes of
  OpenDevicePartnership/odp-platform-radxa-orion-o6 PR 85, 0003 the
  key-return and hardware-entropy error paths (ODP issues #86 and #87),
  0004 a backport of upstream OP-TEE's StandaloneMM boot-information and
  HOB interface (behind `CFG_STMM_FFA_VERSION=12`), 0005 stopping StMM on
  a failed NOR transaction. 0006 and 0007 are this repository's.
* StandaloneMM: CIX ships three drivers only as binaries;
  `NorFlashStandaloneMm`, `SerialOutStandaloneMm` and
  `FwVersionStandaloneMm` are new code here. The core carries patch 0003
  (`edk2`).
* Containers and data: bootloader2's certificates come from TF-A
  `cert_create` (patches 0001, 0003 let a trusted key certificate carry
  only one world's key) and `fiptool`; bootloader3's BL33 certificates
  from `cert_create` instead of the closed `cert_uefi_create_rsa`; the
  bootloader1 container is rebuilt and re-signed by `cixfw.py bl1`; the
  SPI image by `cixfw.py pack`. `memory_config` and `pm_config` are built
  from CIX's generators where the board package has them
  ([open-firmware.md](open-firmware.md#building)).

## Differences kept on purpose

### OP-TEE

* **NOR secure storage only where the TrustZone configuration says**
  (patch 0006). The public tree's NOR file system
  (`TEE_STORAGE_PRIVATE_NOR`) wrote at `CFG_NOR_FS_OFFSET` (`0x800000`)
  plus 1 or 2 MiB by the flash's JEDEC density. CIX's OP-TEE takes the
  region from the TrustZone configuration BL31 leaves at `0x80400000`; the
  open one does too, with checks CIX's has not got: `plat-cix` reports the
  region only when `secure_storage_type` is 1; `spi_nor_fs_setup()` takes
  it only inside the chip (JEDEC density) and below 16 MiB, on 4 KiB
  boundaries, else the file system reports
  `TEE_ERROR_STORAGE_NOT_AVAILABLE`; the block count is capped to the
  region. REE-FS storage (through `tee-supplicant`), the default, is
  unaffected. CIX's default configuration has secure storage disabled.
* **The secure storage key as CIX derives it** (patch 0007,
  `CFG_CIX_SSK_COMPAT`, default y on `plat-cix`): SSK =
  HMAC-SHA256(HUK, die ID ‖ `"cix_ssk_gen_string\0"`), the die ID being
  `huk_subkey_derive(HUK_SUBKEY_DIE_ID)`. The public tree's generic
  construction (a fixed "BEEF" die ID, `"ONLY_FOR_tee_fs_ssk"`) would have
  left neither firmware able to read the objects (TA data under
  `/data/tee`, fTPM state) the other wrote. Kept on purpose so that
  switching between the vendor's and the open firmware keeps them.
* **Hardware entropy and key return** (patch 0003): the CIX rand adapter
  has no error channel, so the open OP-TEE panics in the core, or aborts
  in a user TA, if hardware entropy fails or returns anything other than
  the requested length; the selected key retrieval's result is returned.
* **A failed NOR transaction stops StMM** (patch 0005,
  `core/arch/arm/kernel/stmm_sp.c`): the vendor NOR driver ignores a failed
  direct response; OP-TEE no longer resumes it after a failed transaction
  (its RAM shadow may no longer match the flash) and returns
  `TEE_ERROR_STORAGE_NOT_AVAILABLE`.
* **FF-A 1.2 StandaloneMM** (patch 0004): an experimental build option,
  `CFG_STMM_FFA_VERSION=12`, needs a matching modern MM core and CPU
  driver and rejects the two known legacy vendor images; it corrects the
  upstream free-memory-top calculation and rejects invalid or overflowing
  HOB bounds. The default (10) is the vendor interface.

### StandaloneMM

* **The UEFI transaction passthrough checks that the data fits in the
  message.** CIX's driver takes the transaction's data length as given,
  and OP-TEE and TF-A use it on StMM's buffer.
* **A failed program or erase re-reads the block**, so the RAM copy never
  claims data the flash does not hold.
* **Partial FVB writes at an offset are written at that offset** (CIX's
  FVB code writes a whole block at offset 0 when a cut write is
  block-sized).
* **The version** FwVersionDxe shows (`gCixMmStmmInfoCommunicateGuid`) is
  `edk2-cix-<version>-<commit>`.

### Containers and data

* bootloader1: BL2 and the SE firmware are zero-padded to the size of
  CIX's so the container keeps its layout; CSU_PM is not (the SE hashes
  only the XBIN's sections and finds the signature from the image's
  length): it ends in CIX's slot and its signature moves down, nothing
  else moves ([open-firmware.md](open-firmware.md#building), step 8).
* bootloader2: every key is the published CIX key, with the NV counter of
  the vendor FIP (31 on the O6); bootloader3: NV counter 223, trusted key
  certificate with only the non-trusted-world key, under the same root as
  bootloader1's and bootloader2's (the vendor's bootloader3 is certified
  under another root; see [differences-bl2-bl31.md](differences-bl2-bl31.md#differences-kept-on-purpose)).
* `trustzone_config`: every board gets CIX's stock configuration (SPE on,
  secure storage and bootloader3 recovery off), not Radxa's O6/O6N file,
  which puts secure storage at 6 MiB, 2 MiB long, over bootloader3
  ([open-firmware.md](open-firmware.md#trustzone-configuration)).

## Left out

* Nothing of OP-TEE is recorded as left out. CIX's binary has the eFuse
  read through TZ shared memory and a few lock paths that the public tree
  lacks; the documents do not say whether they were added
  ([closed-firmware.md](closed-firmware.md)).
* The fTPM: CIX's OP-TEE has no built-in fTPM, and the UEFI pieces that
  would load the TA (`FTpmDeviceLib`, `OpteeClientApiLib`, `FTPMDxe`) are
  not public; neither firmware here offers it
  ([closed-firmware.md](closed-firmware.md#firmware-tpm-ftpm-not-available)).
* StandaloneMM: nothing recorded.

## Faults of the vendor's image that the open one does not have

From [closed-firmware.md](closed-firmware.md#security-issues-in-cixs-boot-firmware)
and [open-firmware.md](open-firmware.md#at-a-glance); read from CIX's code,
none tried on a board:

* **StandaloneMM: unchecked transaction length.** Whoever can send MM
  messages (UEFI, and through OP-TEE the OS kernel) can have flash data
  written past StMM's buffer into secure memory. The open StMM checks the
  length.
* **OP-TEE: secure storage past the end of the flash.** The NOR file
  system's offset on an 8 MiB flash is past the chip's end, which ignores
  the upper address bits: the first write would land on the firmware
  header and bootloader1. [open-firmware.md](open-firmware.md#at-a-glance)
  and [closed-firmware.md](closed-firmware.md#security-issues-in-cixs-boot-firmware)
  list it as CIX's; patch 0006's message places it in the public tree's
  `CFG_NOR_FS_OFFSET` code and says CIX's OP-TEE binary takes the region
  from the TrustZone configuration instead. Fixed by patch 0006 either
  way.
* **Secure storage over UEFI** (Radxa's `trustzone_config.bin` for the
  O6/O6N): OP-TEE's NOR file system would overwrite bootloader3. Every
  board gets CIX's stock file, and BL31 (patch 0004) and `check_open.py`
  refuse an overlapping region.
* **The vendor's StMM NOR driver ignores a failed direct response**
  (patch 0005's comment) and goes on with a RAM shadow that may not match
  the flash; the open OP-TEE stops StMM instead.

Patches 0002 and 0003 fix defects of the public tree reported to
OpenDevicePartnership (PR 85, issues #86 and #87); whether CIX's binary
has them is not recorded.

## Vendor quirks kept

* The NOR driver formats the variable store when its headers are invalid,
  as CIX's does, which keeps the inherited recovery weakness the
  orion-unofficial project found: an interruption after the variable
  block's header was erased loses the store
  ([open-firmware.md](open-firmware.md#standalonemm)).
* The secure storage key is CIX's derivation, not the public tree's
  (above).

## Faults found in the open firmware by these tests

No test compares OP-TEE or StandaloneMM with CIX's, so none found any.
`scripts/open/test_stmm_nor.py` (formatting, 20 000 random writes, erases
and reads checked against a model of the flash and the RAM copy, bounds,
failing transactions in the middle of writes) records no fault in the
documents.

## Known limits and open points

* Not run: neither image has run on hardware or in an emulator. The
  roadmap's first step is a boot test of `--open` on an O6 (UART log of
  BL2, BL31 and OP-TEE; variables persist across a cold boot; Secure Boot
  keys enrol; suspend and resume); `--vendor-stmm` isolates the StMM image
  if variables misbehave ([open-firmware.md](open-firmware.md#roadmap)).
* Patch 0004's FF-A 1.2 mode: both configurations compile and mocked C
  tests cover bounds, serialization and memory-permission error paths; no
  modern hybrid image exists.
* Patch 0002: "The separately reported entropy and missing-return defects
  remain separate work" (fixed by 0003).
* The open StMM's equivalence to CIX's is from reading its interfaces and
  binaries; the NOR test runs the code on the host, not in OP-TEE.

## What no test reaches

All of OP-TEE: no test runs it. Of StandaloneMM, only the NOR and FVB code
runs (in `test_stmm_nor.py`); the core, the variable and FTW drivers and
the serial and version drivers are not exercised by any test in the
repository. No coverage has been measured.

## Checking it yourself

There is no comparison to run. What can be run:

```sh
./build.sh orion-o6 --open                  # builds OP-TEE with the open StMM into bootloader2
./build.sh orion-o6 --open --vendor-stmm    # the same with CIX's StMM (image ..._open_cixstmm.bin)
python3 scripts/open/test_stmm_nor.py       # the StMM NOR and FVB code on the host (needs scripts/prepare.py's submodules)
python3 scripts/open/cixfw.py fip bootloader2.img --verify --root cix_privatekey.pem
python3 scripts/open/check_open.py dist/orion-o6_2026Q3_open.bin \
    edk2-project/Platform/Radxa/Orion/O6/Firmwares/bootloader1.img \
    output/orion-o6/RELEASE/open/tfa-bl2/sky1/release/bl2.bin --root cix_privatekey.pem   # in the build container
```

The build checks that OP-TEE carries exactly the StMM image built.
`emu/test-all.sh` runs the emulators' suites, which do not include these
two binaries.
