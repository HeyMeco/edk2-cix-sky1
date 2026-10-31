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
| TF-A BL2 | `arm-trusted-firmware`, 5 platform files missing | binary; [gap analysed](#bl2) |
| TF-A BL31 | `arm-trusted-firmware` | **built from source** (`--open`) |
| OP-TEE BL32 | `optee_os` + patch queue | **built from source** (`--open`) |
| StandaloneMM inside OP-TEE | upstream EDK2 + 3 CIX drivers | binary, taken from the board's vendor `bootloader2` |
| FIP certificates, `fiptool` | TF-A `cert_create` (patched), `fiptool` | **open** for bootloader2 |
| bootloader1 container (`CIXBTFF!`) | — | **open**: parse, verify, replace, re-sign (`cixfw.py bl1`) |
| SPI image packer (`cix_package_tool`) | — | **open**: `cixfw.py pack`, used by every build |
| `memory_config`, `pm_config`, `trustzone_config` | CIX generators | built from source (unchanged) |

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
showed that these chips reject a `cix`-chain BL1 (they stay silent). The BL33
FIP on every board is rooted at `0d999213` (the pregenerated
`trusted_key_no.crt`, NV counter 223), separately from bootloader2.

## Building

```sh
./build.sh orion-o6 --open          # dist/orion-o6_2026Q3_open.bin
./build.sh orion-o6 --open --target DEBUG
```

`--open` replaces `bootloader2.img` with one built here
([scripts/open/trusted.py](../scripts/open/trusted.py)):

1. BL31: `make PLAT=sky1 SPD=opteed` in `arm-trusted-firmware` (CIX
   `cix_p1_k6.6_2025q3_tfa_open_dev`, TF-A v2.7 + `plat/cix`).
2. OP-TEE: `make PLATFORM=cix-sky1 CFG_ARM64_core=y ta-targets=ta_arm64
   CFG_STMM_PATH=<StMM FV>` in `optee_os` (CIX OP-TEE 3.17 + `plat-cix`).
   The StMM firmware volume is decompressed from the OP-TEE in the board's
   vendor bootloader2 and embedded unchanged; the build checks that the new
   OP-TEE carries exactly that image.
3. Certificates with TF-A `cert_create`, every key the published CIX key, the
   NV counter of the vendor FIP (31 on the O6). The vendor chain uses that
   one key throughout as well; patch
   [0001](../patches/2026Q3/arm-trusted-firmware) lets the trusted key
   certificate carry only the trusted-world key, as CIX's does. The
   certificates then match the vendor ones in structure (serial numbers,
   validity and the OpenSSL 3 encoding of the RSA-PSS parameters aside; BL2
   already accepts that encoding in the BL33 certificates of every build).
4. `fiptool create`, then an independent check (`cixfw.verify_fip`): root,
   key and content certificates, NV counters and the BL31/BL32 hashes.

`--target DEBUG` builds TF-A with `DEBUG=1` and OP-TEE with log level 3.
TF-A and OP-TEE take their build time from `SOURCE_DATE_EPOCH` (default: the
commit time), so a commit rebuilds bit-identically. `manifest.json` gets an
`open` section with the BL31, BL32, StMM and FIP hashes.

The source-built BL31 and BL32 sit where the closed BL2 loads them (BL31
`0x80200000`, OP-TEE `0x80500000`) and start with the same entry code as the
vendor builds. **No `--open` image has been boot-tested yet.** Keep an SPI
programmer and a known-good image at hand.

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
* [scripts/open/test_cixfw.py](../scripts/open/test_cixfw.py) runs in CI.

## Patch queues

| Submodule | Pin | Patches |
|---|---|---|
| `arm-trusted-firmware` | `114fb205` (CIX, 2026-03-10) | `cert_create`: optional non-trusted-world key |
| `optee_os` | `cc66640f` (CIX, 2025-11-11) | five commits from [orion-unofficial/edk2-cix](https://github.com/orion-unofficial/edk2-cix) `source/unofficial/cix/1.2/op-tee/3.17.0` (BSD-2-Clause, authorship kept): compiler fixes, fail-closed hardware entropy and key return, FF-A 1.2 StandaloneMM loader behind `CFG_STMM_FFA_VERSION=12` (default 10 keeps the vendor StMM interface), stop StMM on a failed NOR transaction |

They are applied by `scripts/prepare.py` like the EDK2 queues
([architecture](architecture.md#patch-queue)).

## Gaps

### BL2

The public TF-A builds BL31 but not BL2: `platform.mk` and `cix_common.mk`
list five files that CIX did not publish (`sky1_bl2_el3_setup.c`,
`sky1_trusted_boot.c`, `cix_io_storage.c`, `cix_image_load.c`,
`cix_bl2_mem_params_desc.c`). With them stubbed out, BL2 (without trusted
board boot) only lacks the standard TF-A platform interface:

* `bl2_el3_early_platform_setup`, `bl2_el3_plat_arch_setup`,
  `bl2_platform_setup`
* `plat_get_bl_image_load_info`, `plat_get_next_bl_params`,
  `plat_flush_next_bl_params` (BL31 at `0x80200000`, OP-TEE at
  `0x80500000`, BL33 at `0x84400000`; see `platform_def.h`)
* `plat_get_image_source`: FIP in SPI NOR through the public xSPI driver
  (`drivers/cadence/xspi`), bootloader2 at the address in the firmware
  header (`0x288000`), bootloader3 at its own entry

Trusted board boot adds `plat_get_rotpk_info` and the NV counter hooks, and
a crypto backend (TF-A's mbedTLS, or the CE driver whose adapted copy is in
OP-TEE `lib/libmbedtls/core/te_driver`). Beyond that the vendor BL2 also
recovers bootloader3 from a backup copy, loads `trustzone_config.bin`, feeds
the watchdog and checks the board ID; the strings are listed in
[closed firmware](closed-firmware.md#per-component). What the SE firmware
hands to BL2 in the shared page (`BL2_SE_FW_SHARE_BASE`, DRAM size, boot
device) has to be read from the vendor BL2.

A source-built BL2 then goes into bootloader1 with
`cixfw.py bl1 --replace ap=bl2.bin --resign …` (chain A only). The AP slot
must keep its offset for now (the replacement has to fit the existing slot;
the O6 slot holds `0x2fe00` bytes, the vendor BL2 is `0x2f729`).

### BL31

The public BL31 covers the vendor one except about 35 strings:

* the TrustZone configuration loader (`trustzone_config.bin` at `0xF0000`;
  only the O6/O6N layouts have one, a BL31 without the loader uses the
  defaults, as the vendor one does when the region is invalid)
* SPI-NOR erase/write inside BL31 (flash override), xSPI clock message
* audio power-on and SYSHUB names in error reporting

The StMM flash path is public: OP-TEE forwards StMM NOR requests as
`CIX_SIP_NOR_STORAGE` (`0xC200000B`) SMCs, which `cix_sip_svc.c` executes
on the xSPI controller.

### StandaloneMM

The StMM in vendor bootloader2 is CIX's `PlatformMmStandalone` build:
`StandaloneMmCore`, `StandaloneMmCpu`, `VariableStandaloneMm`,
`FaultTolerantWriteStandaloneMm` (upstream EDK2) and three CIX drivers
without public source: `NorFlashStandaloneMm` (NOR through the SMC above),
`SerialOutStandaloneMm`, `FwVersionStandaloneMm`. The orion-unofficial
project documents the interfaces needed to replace them
([standalone-mm.md](https://github.com/orion-unofficial/edk2-cix/blob/build/docs/src/standalone-mm.md)):

* CIX OP-TEE hands StMM the legacy `stmm_boot_info`, CPU information and a
  CIX NVRAM GUID HOB (`4c8167fc-5a9d-468e-a942-861fb50ab539`, 36 bytes:
  variable, FTW working and FTW spare base as u64, then their sizes as u32);
  EDK2 after 202411 expects FF-A boot information instead, which the
  imported OP-TEE patch 0004 can provide (`CFG_STMM_FFA_VERSION=12`).
* They built a hybrid image (EDK2 202608 core, variable and FTW drivers plus
  the three CIX executables) that matches the vendor behaviour in offline
  tests, including an inherited NOR recovery weakness: an interruption after
  erasing the variable FV header makes the vendor NOR driver reformat the
  store before FTW can recover it.

An open `NorFlashStandaloneMm` (NOR commands through `0xC200000B`, the NVRAM
HOB for the regions) is the piece that removes the last binary from
bootloader2.

### Not planned

SE firmware and CSU_PM stay binary: no source exists, the SE does DRAM
training, and CSU_PM needs the proprietary Xtensa core configuration to
build. `sfh_fw.bin` (17 KB Cortex-M) is small enough to clean-room later.

## Roadmap

1. Boot-test `--open` on an O6 (UART log of BL31/OP-TEE, variables persist
   across a cold boot, Secure Boot keys enrol). Then make it the default for
   the `cix` boards.
2. BL2 from source: the five platform files, first without trusted board
   boot, checked against the vendor BL2's behaviour on UART; then re-signed
   into bootloader1 for the O6.
3. TrustZone configuration loader in BL31.
4. Open `NorFlashStandaloneMm`, `SerialOutStandaloneMm` and
   `FwVersionStandaloneMm`, and an StMM build from source.
5. Replace `cert_uefi_create_rsa` for the BL33 certificates with TF-A
   `cert_create` (non-trusted chain: key ext 1101, hashes 1201/1202, NV
   counter ext 2).

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
