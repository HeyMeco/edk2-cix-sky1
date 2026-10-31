# Closed firmware and open counterparts

What each binary in the flash image is, whether public source exists for it,
and whether that source can replace the binary. Based on the CIX
`edk2-non-osi` 2026Q3 binaries and the vendor images in `vendor/orangepi/`
(analysis of 2026-10-26). Comparisons with public trees are by strings
(format strings, `__FILE__` paths, assertion text): identical strings do not
prove identical code, and nothing here has been built or boot-tested.

## Summary

| Region | Contents | Public source | Can be rebuilt |
|---|---|---|---|
| `bootloader1.img` #1 | SE firmware (Cortex-M) | none | no |
| `bootloader1.img` #2 | CSU_PM firmware (Xtensa, Arm SCP-firmware based) | SCP-firmware framework only | only with large reverse engineering |
| `bootloader1.img` #3 | TF-A BL2 | cixtech TF-A, 5 platform files missing | after those are written; O6/O6N BL1 can be re-signed ([open firmware](open-firmware.md#bl2)) |
| `bootloader2.img` | TF-A FIP: BL31 + OP-TEE BL32 (with StandaloneMM) | cixtech TF-A and OP-TEE, with gaps | **built** for O6/O6N with `--open` (StMM still binary) |
| `csu_pm_config.bin` | PM configuration | `pm_config` generator | yes, byte-identical for both Orange Pi boards |
| `memory_config.bin` | DRAM configuration | `mem_config` generator | Radxa/Minisforum: built; Orange Pi: not yet decoded |
| `trustzone_config.bin` | TZC configuration | `trustzone_config` generator | yes |
| `se_config.bin` | `CIXSECONFIG` header, otherwise zero | format known from `SeConfigUpdateDxe` | yes |
| `sfh_fw.bin` | 17 KB Cortex-M image (vectors at `0x2007fff0`, mailbox) | none | no counterpart; small enough to clean-room |
| `ec_fw.bin` | ITE 8051 EC firmware (`ITE EC-V00.1`, `EC V1.13`) | none | no |
| `Silicon/CIX/Sky1/Drivers/*.efi`, `Platform/CIX/Sky1/Library/*.lib` | CIX DXE drivers and libraries (CIX Jenkins build paths in the debug directory) | none, except `NorFlashStmmRuntimeDxe`, `I2cDxe`, `SpiDxe` in `edk2-platforms` | small drivers could be clean-roomed; PCIe, TypeC PHY, DPU, CDNSP are large |
| `fiptool`, `cert_create` | host tools | TF-A `tools/` | built from `arm-trusted-firmware` for `--open` |
| `cix_package_tool` | host tool | binary only | **replaced** by `scripts/open/cixfw.py pack` (byte-identical) |
| `cert_uefi_create_rsa` | host tool | binary only | TF-A `cert_create` can replace it |

Public CIX trees (no license file; GitHub reports `NOASSERTION`; upstream parts
are BSD-3-Clause for TF-A and BSD-2-Clause for OP-TEE):

* `cixtech/cix_opensource__arm-trusted-firmware`, branch
  `cix_p1_k6.6_2025q3_tfa_open_dev` (TF-A v2.7 + `plat/cix`, last commit
  2026-03-10)
* `cixtech/cix_opensource__tee__op-tee`, same branch (OP-TEE 3.17 +
  `plat-cix`)

## bootloader2: BL31 and BL32

Despite the name, `bootloader2.img` is not BL2 but a TF-A FIP (TOC
`0xAA640001`):

| Entry | Contents |
|---|---|
| trusted key, SoC firmware key/content, TOS firmware key/content certificates | X.509, signed with the CIX OEM key chain |
| `soc-fw` (BL31) | TF-A `v2.7(release):Beta_2.1.5_release`, built Jul 15 2026 |
| `tos-fw` (BL32) | OP-TEE `Beta_2.1.5_release-3.17`, built Aug 3 2026, **with StandaloneMM embedded** (`stmm_sp.c`) |

Both payloads are plaintext. Missing from the public trees:

* BL31 (about 29 of 128 log strings):
  * the TrustZone configuration loader (`TrustZone: CRC check failed`,
    `Failed to get config address from flash header, use default`). This is
    the code that reads `trustzone_config.bin` at `0xF0000`; a BL31 built from
    the public tree ignores that region.
  * SPI-NOR erase/write in BL31 (`spi_nor_erase_sector`,
    `write failure when override flash`).
  * RAS event forwarding for IDM and TZC400.
* BL32: `cix_ssk_gen_string`, the eFuse read through TZ shared memory, and a
  few lock paths. The CIX platform, HDCP2 and the crypto-engine driver
  (`lib/libmbedtls/core/te_driver`, `lib/libhosal`) are public.

The embedded StandaloneMM is CIX's build, not ours: the secure-world variable
driver comes from this blob, not from the UEFI tree built here. A rebuilt BL32
would take our own StMM FV through `CFG_STMM_PATH`.

Signing: BL2 checks the bootloader2 certificate chain against a root key of
its own, separate from the BL33 chain. On the Orion O6/O6N (and the CIX
default images) that root is CIX's published `cix_privatekey.pem`
(`33d47f74`), which also signs every certificate in the vendor FIP (NV
counter 31). On the Orange Pi boards and the retail MS-R1 the root is
`0d999213`, whose private key is not public. `--open` builds and signs
bootloader2 for the first group; see [open firmware](open-firmware.md).

### Builds per board

Each board's `bootloader2` comes from the same source as its BL1, and the
BL31 in it is the same build as the BL2 in that BL1 (same `Built :` line).

| `bootloader2` | BL31 | OP-TEE (BL32) |
|---|---|---|
| Orion O6 / O6N (`7ff0c68c…`, Radxa `4e99786e`) | `Beta_2.1.5_release`, Jun 13 2026 | `Beta_2.1.5`, Mar 24 2026 |
| MS-R1 retail (`2b830829…`) | `Beta_2.0.3_release`, Oct 24 2025 | `Beta_2.0.3_release-3.17-6dc7ab48a9ef`, Jul 14 2025 |
| Orange Pi 6 (`a5041da9…`) | `Beta_2.1.5_release`, Dec 10 2025 | `Beta_2.1.5_release-3.17-bd3cc0e2705e`, Jan 4 2026 |
| Orange Pi 6 Plus (`99f9ab63…`) | `Beta_2.1.3_release`, Sep 4 2025 | `Beta_2.1.3_release-3.17-7c1538ba6c38`, Jul 9 2025 |
| CIX 2026Q3 (`cbe992c9…`; `Firmwares2/` `e535526f…`) | `Beta_2.1.5_release`, Jul 15 2026 | `Beta_2.1.5_release-3.17-67e342df9765`, Aug 3 2026 |

MS-R1 is the only board on the `Beta_2.0.x` line. Its retail `bootloader2`
and Minisforum's public one (`6738acee…`) carry the same version strings and
BL31 build; the OP-TEE builds are 8 seconds apart (10:21:32 vs 10:21:24 on
Jul 14 2025). Only the strings were compared.

### Firmware TPM (fTPM): not available

The 2026Q3 UEFI tree carries CIX's fTPM trusted application
(`Platform/CIX/Sky1/Bin/fTPM.bin`: the Microsoft reference fTPM, UUID
`bc50d971-d4c9-42c4-82cb-343fb7f37896`, signed with a 3072-bit RSA key, NV
state in OP-TEE secure storage), the `FTPM_SUPPORT` switch and a TPM2 table
for Windows' TrEE interface (`Ftpm.aslc`, start method 9, device
`CIXHA023`). It cannot be used:

* `FTpmDeviceLib`, `OpteeClientApiLib` and `FTPMDxe`, which load the TA
  into OP-TEE and talk to it, are not in the public tree.
* The OP-TEE in `bootloader2` has no built-in fTPM. It loads TAs from the
  normal world and keeps secure storage on the normal-world file system
  (REE FS; RPMB is off in the public `plat-cix` config), so the fTPM only
  works while a normal-world client answers its RPCs. Under Windows that
  is CIX's OP-TEE TrEE driver, which is not public and, like any
  third-party kernel driver, would need Microsoft's signature to load with
  Secure Boot on.
* Making it work without an OS driver would take a rebuilt BL32 (public
  OP-TEE plus the CIX-only pieces listed above and our own StMM), the
  fTPM as an early TA, a doorbell an ACPI `_DSM` can ring, and secure
  storage the secure world writes by itself.

Boards that need a TPM use a discrete one instead; on Linux, the vendor
OP-TEE with `tee-supplicant` and `ftpm_tee` would be the remaining option.

## bootloader1

A signed `CIXBTFF!` container:

| Offset | Contents |
|---|---|
| `0x0` | magic, version `0003 0101` |
| `0x1c` | RSA-3072 public key 1 (root), exponent at `0x19c` |
| `0x324` | RSA-3072 public key 2 (OEM), exponent at `0x4a4` |
| `0x800` | supported SPI flash table (JEDEC IDs, Winbond/GigaDevice) |
| `0x9e8` | image count, then 24-byte entries from `0x9ec`: type, flag, 0, offset, size, load address |
| `0xb7c` | SHA-256 of each image's data, every `0x48` bytes |
| `0xe00` | signature length (`0x180`), RSA-3072 PSS signature by key 1 over `[0, 0xe00)` |
| end of each image slot | `0x200` before the next 4 KiB boundary: length and PSS signature by key 2 over the slot |

| # | Offset (CIX default) | Load | Contents |
|---|---|---|---|
| 1 | `0x1000`, 332 KB | `0x20000000` | **SE firmware**, Cortex-M (`CiX SE firmware Sky1`, `se_fw.elf`): eFuse ROTPK selection, image verification (RSA/SM2, mbedtls), secure debug, and **DDR initialisation and training** (`dpmv8_*`, `Ch %d Rank %d`), clocks and resets |
| 2 | `0x53000`, 319 KB | `0x06a90000` | **CSU_PM firmware**, Xtensa (`XBIN` header, `MINI_PORT_00.10.002`, `cix_trunk`), built on Arm SCP-firmware (`[FWK]`, `[PD]`, `[PPU_V1]`, `[SCMI]`, `mod_clock`): DVFS/OPP, PMIC/EDP limits, thermal, fans, DDR DFS, a debug CLI |
| 3 | `0xa3000`, 194 KB | | **TF-A BL2** (`BL2_AT_EL3`), the same build as the BL31 in `bootloader2` |

### Signing

| Image | Key 1 | Key 2 |
|---|---|---|
| CIX default (`Firmwares/`), Radxa `4e99786e`, Minisforum `c22fb43e` | `c6d0e93f…` | `5672dfd9…` |
| Orange Pi 6, Orange Pi 6 Plus, Minisforum MS-R1 retail image, CIX 2026Q3 `Firmwares2/` | `fb3f60e4…` | `fb3f60e4…` (same key) |

(prefixes of SHA-256 over the 384-byte modulus)

CIX 2026Q3 `Firmwares2/bootloader1.img` carries the same three images as
`Firmwares/` (identical SHA-256), signed with the `fb3f60e4` chain instead.
It may therefore start on the Orange Pi and retail MS-R1 boards; not tested.

The private keys of the CIX default chain are public (`cixtech/edk2-non-osi`
`94368c7c`, `Platform/CIX/Sky1/PackageTool/Keys`): `oem_privatekey.pem` is
key 1 (`c6d0e93f`), `cix_privatekey.pem` key 2 (`5672dfd9`). A modified BL1
re-signed with them has been reported booting on an Orion O6
([Neol00/edk2-cix-unlocked#12](https://github.com/Neol00/edk2-cix-unlocked/issues/12));
`scripts/open/cixfw.py bl1` verifies and re-signs the container. For the
`fb3f60e4` chain no key is public.

The SE firmware verifies a two-step chain (`Verify Cix ROTPK successful`,
`Verify OEM ROTPK successful`). Orange Pi ships a different key, self-certified
in both slots, and those boards boot it. Together with the strings
`[Image]System will use OEM RSA rotpk`, `oem pubkey hash is empty`,
`Secure boot disable` (SE) and `ROTPK is not deployed on platform. Skipping
ROTPK verification.` (BL2), this suggests that a chip with an empty OEM
ROTPK hash in eFuse trusts the key embedded in the image, so a container
signed with our own key would boot on unfused boards. The first check is in
the mask ROM, so this cannot be confirmed from the binaries. The retail MS-R1
does not start a BL1 of the CIX default chain, so at least those chips have
a key fused.

### Builds per board

None of these BL1s carries a single release number. The table lists what the
images contain: the SE weekly tag (where present), the CSU_PM build
(`MINI_PORT_00.10.002G<yymmddhhmmss><branch><commit>`) and the TF-A BL2 tag.

| BL1 | SE firmware | CSU_PM | TF-A BL2 | BL2 offset | SPI flash entries |
|---|---|---|---|---|---|
| Orion O6 / O6N (`fcddd093…`) | no tag, stamp 2026-06-03 | `cix_trunk` `79f0fdb030f9`, 2026-05-11 | `v2.7-ed9772217e49` release, Jun 13 2026 | `0xa4000` | 10 |
| MS-R1 retail (`7b1e83ad…`) | no tag, stamp 2025-12-08 | `cix_p1_mg_202510_dev` `86cd83d159f8`, 2026-02-09 | `v2.7-8d091f2c5132` debug, Oct 24 2025 | `0xa4000` | 9 |
| Orange Pi 6 (`ed15fa1e…`) | `2.0.2026W01.01` | `cix_trunk` `64709ae53a19`, 2025-12-23 | `v2.7-4adcf71a1937` debug, Dec 10 2025 | `0xa4000` | 10 |
| Orange Pi 6 Plus (`e8162901…`) | `2.0.2025W36.01` | `cix_trunk` `8b80e7d12d8d`, 2026-01-29 | `v2.7-6f1cdf4f3a61` debug, Sep 4 2025 | `0xa4000` | 9 |
| CIX 2026Q3 (`6a3b4bbd…`, `Firmwares2/` `96536692…`) | `2.0.2026W31.01` | `cix_trunk` `791c46272210`, 2026-07-21 | `v2.7-33527c31abbe` release, Jul 15 2026 | `0xa3000` | 10 |

* The MS-R1 retail BL1 has the same three images and the same flash table as
  Minisforum's public one (`c22fb43e`, `e6de7d0f…`); only the keys and
  the signature differ.
* The 9-entry flash tables lack JEDEC `c8 60 18` (GigaDevice, 16 MB).
* Every image's SHA-256 matches the hash in its container's table.

### Per component

* **BL2**: the public TF-A has the sky1 BL2 build. Missing: the crypto-engine
  driver used for trusted boot (`ce_*`, `aca_op_*`, `sec_rsa_verify`; an
  adapted copy is in the public OP-TEE `te_driver`, or TF-A's mbedtls can do
  the verification in software), bootloader3 recovery from the backup copy
  (`plat_recovery_from_backup`, about 25 strings), and the TrustZone loader
  and SPI-NOR write shared with BL31. Best done together with BL31.
* **CSU_PM**: the framework is open (Arm SCP-firmware, BSD-3-Clause) and the
  module structure is visible, but every CIX module (PLL, PMIC/EDP, DDR DFS,
  thermal, fans, `SKY1SOC_PERF`, IPC arbiter, CLI) would have to be recovered
  from Xtensa code, and building needs the exact Xtensa core configuration,
  which is proprietary. This is the firmware that sets frequency caps.
* **SE firmware**: no counterpart. It contains the DRAM training and the
  eFuse and secure-boot logic; not a realistic target.

## EC firmware

| Board | `ec_fw.bin` | Version | Flashed | Used |
|---|---|---|---|---|
| Orion O6 | `84cbdc21…` (`radxa/edk2-platforms`) | `EC V1.07` | yes, `0x10000` | yes, runs the fan |
| Orion O6N | `03a5a116…` (Radxa 1.3.1 release) | none: 8 KiB of `0xFF`, then 120 KiB of `0x00`, no code | yes, `0x10000` | no EC |
| MS-R1 | `c228c902…` (CIX default, staged by the generic flow) | `EC V1.13` | no: `ec_file` is `dummy.bin` | no EC |
| Orange Pi 6 | `12c3f1c3…` (vendor image) | `EC V1.15` | yes, `0x10000` | no: UEFI never talks to it |
| Orange Pi 6 Plus | `07a81289…` (vendor image) | `EC V1.13` | yes, `0x10000` | yes: power rails through EC GPIOs, fan mode |

All the real images are ITE (`ITE EC-V00.1`) builds of the same CIX EC code
base. Where no EC is used, CSU_PM runs the fans from `pm_config` (O6N, MS-R1,
Orange Pi 6); where the EC runs them, `pm_config` has no fan tables (O6,
Orange Pi 6 Plus).

The Orange Pi 6 vendor UEFI removed every EC call (power rails, AC-loss
policy, RTC wake, EC defaults; see `OPI6/Library/PlatformEnvHookLib`), but its
SPI image still carries a real EC image, which we flash unchanged. Whether the
board has the EC chip at all is not visible from the firmware; the region may
be left over from the Merak layout. `OPI6.dsc` still links Merak's
`Ite5570EcRuntimeLib`, unused.

## Memory configuration

`memory_config.bin` starts with a 32-byte quick configuration, followed by a
`CDCB` header at `0x20` (version, 8-bit block count at `0x2e`) and a table of
(GUID, offset) block entries. Each `CONF` block describes one DRAM variant:
maximum data rate, type, die density, width and ranks, selected by a board ID
bit mask. A variant with no specific block uses the default (`mask all`)
`CONF`. The board ID comes from GPIO straps (`BDGM`) or from a PCB ID map
(`BDMP`). The rate is `MaxFreq` × 2; the setup option (`BSET MemFreq`) is
Auto on every board, so the maximum is what training targets. All boards are
LPDDR5 on four channels.

| Board | Format | Board ID | Data rate (MT/s) |
|---|---|---|---|
| Orion O6 | v2.0, 24 blocks | PCB ID map | 5500 default; 4800 for 16 GB HS and 32 GB Lo; 6000 for 32 GB Hynix |
| Orion O6N | v2.0, 21 blocks | GPIO 49, 50, 57, 60 | 5500 default; 6000 for Hynix 16/32/48 GB; 5000 for HS 16/24/48 GB and 32 GB x8; 4800 for 64 GB Rayson |
| MS-R1 | v1.6, 16 blocks | GPIO 82, 83 | 5500 for all four variants |
| Orange Pi 6 | v2.0, 14 blocks | GPIO 86–89 | 6000 for board IDs 0, 1, 2; 5500 for ID 15 and the default |
| Orange Pi 6 Plus | v2.0, 14 blocks | PCB ID map | 6400 for every variant |
| CIX 2026Q3 default | v2.0, 27 blocks | (unused) | 3200–6000 |

Variants per board (board ID: name from the source, or what the block
describes for the vendor binaries):

| Board | Variants |
|---|---|
| Orion O6 | 0: 8 GB (8 Gb x16 1R); 1: 12 GB (12 Gb x16 1R); 2: 16 GB (16 Gb x16 1R); 4: 32 GB (16 Gb x16 2R); 5: 48 GB HS (12 Gb x8 2R); 6: 64 GB (16 Gb x8 2R); 7: 32 GB Lo (16 Gb x16 2R, 4800); 8: 64 GB Rayson (16 Gb x8 2R); 9: 32 GB x8 HS (8 Gb x8 2R); 10: 24 GB HS (12 Gb x16 2R); 11: 16 GB HS (8 Gb x16 2R, 4800); 12: 32 GB Hynix (16 Gb x16 2R, 6000) |
| Orion O6N | 1: 16 GB Hynix (8 Gb x16 2R, 6000); 3: 48 GB Hynix (12 Gb x8 2R, 6000); 6: 32 GB x8 HS (8 Gb x8 2R, 5000); 7: 24 GB HS (12 Gb x16 2R, 5000); 9: 48 GB HS (12 Gb x8 2R, 5000); 12: 64 GB Rayson (16 Gb x8 2R, 4800); 13: 16 GB HS (8 Gb x16 2R, 5000); 14: 32 GB Hynix (16 Gb x16 2R, 6000) |
| MS-R1 | 0: `4g` (16 Gb x16 1R); 1: `8g` (16 Gb x16 2R); 2: `16g` (16 Gb x8 2R); 3: `32g` (16 Gb x8 2R) |
| Orange Pi 6 | 0, 1, 2: 6000, geometry auto-detected; 15: 5500, auto |
| Orange Pi 6 Plus | Micron DRAM (known from the board; the firmware does not name a vendor). 0: 16 Gb x16 1R; 1: 16 Gb x16 2R; 2: 16 Gb x16 1R; 3: 16 Gb x16 2R; 4: auto |

The Orange Pi 6 Plus PCB ID map gives IDs 0–3 a mask of 0, which as decoded
would match any PCB ID; how the SE firmware treats a zero mask is not known.
The MS-R1 binary is the older v1.6 format: its `CONF` blocks have the v2.0
layout, but its bus and pad entries do not and were not decoded.

## Other per-board files

| File | O6 / O6N | MS-R1 | Orange Pi 6 | Orange Pi 6 Plus |
|---|---|---|---|---|
| `sfh_fw.bin` | CIX stock `80a2cb58…` | same | same | same |
| `se_config.bin` | CIX stock `7bb9621a…` (3856 B) | Minisforum `30b25c64…` (144 B) | CIX stock | CIX stock |
| `trustzone_config.bin` | Radxa `818bd96d…`, flashed at `0xF0000` | CIX stock `7b35f99a…`, staged but not in the flash layout | same as MS-R1 | same as MS-R1 |
| `certs/trusted_key_no.crt` | Radxa pregenerated `2a25ee2b…` | same | regenerated with the OEM key (`trusted_key_cert: regenerate`), 1602 B | Radxa pregenerated |
| `certs/nt_fw_*.crt` | generated per build | same | same | same |

## PM configuration

Both Orange Pi `csu_pm_config.bin` files regenerate byte-identically (CRC and
timestamp included) with the CIX `pm_config` generator:

| Board | Format | Generator | Changes |
|---|---|---|---|
| Orange Pi 6 Plus | v3.0, length 3520 | Radxa `O6/pm_config` (v3.0, length 3516) | `reserved[115]` instead of `[111]` (the older SDK's struct is 4 bytes longer); `PM_LOG_CONFIG 1` with log flag `0x2DBA0C1D` and 115200 baud. NOC idle 2 and WDT 5 s are the O6 defaults. |
| Orange Pi 6 | v3.2, length 3536 | `MGP1WSB/pm_config` (v3.1) | `config_data_t op_mode;` after `gpio_config`, `PM_CONFIG_VERSION_MINOR 2`; PMIC/EDP, all three fan entries, NOC idle and WDT from the binary |

The layout only grows at the end: v3.0 is 3516 bytes, v3.1 adds
`opp_100M_enable` and `gpio_config` (3532), v3.2 adds `op_mode` (3536), v3.4
adds `psi7_mode` (3540, CIX Alcor).

Orange Pi 6 contents: custom EDP scheme, `opp_max` all `OPP_NO_LIMIT`; power
caps LIT 2.5 W, GM0/GM1 6.5 W, GB0/GB1 8 W, DSU 5.5 W, GPU 12 W, SoC 9 W;
every rail on I2C0 address `0x45`; LIT and DSU fixed at 815 mV, the others
MP2845 at 790 mV with 15–30 mV guard bands. Fan 1 and 2 are marked invalid
but carry table data, so a source version has to write every field, not just
the meaningful ones; OPP table, PVT, log, Vmin, SPT, 100 MHz OPP, GPIO and
`op_mode` are unset.

### Contents per board

From the built (Radxa, MS-R1) and vendor (Orange Pi) binaries. None of them
sets an OPP table, Vmin, SPT, the 100 MHz OPP, GPIO functions or `op_mode`.

| Setting | O6 | O6N | MS-R1 | Orange Pi 6 | Orange Pi 6 Plus |
|---|---|---|---|---|---|
| PMIC/EDP | – | – | – | caps and rails (above) | – |
| Board sensors | – | – | 2 on I2C3 `0x4d`, regs 0 and 1 | – | – |
| Fans | – (EC) | CPU fan 720–3720 rpm and M.2 fan, 25 kHz | fan 0: 8-step table 1500–3200 rpm, perf up to 5500, quiet from 90 °C; fans 1/2 on sensors 13/14; 5 kHz | fan 0: 2900–5200 rpm, 25 kHz; fans 1/2 as MS-R1 but invalid | – (EC) |
| NOC idle | 2 | – | 2 | 2 | 2 |
| PM watchdog | 5 s | – | 5 s | 5 s | 5 s |
| PM log | – | – | – | – | `0x2DBA0C1D`, 115200 |

An empty fan section does not mean the board has no fan control: on the O6
and the Orange Pi 6 Plus the EC runs the fan (see [EC firmware](#ec-firmware)),
so the fan section is empty. Boards without a working EC (O6N, MS-R1,
Orange Pi 6) let CSU_PM drive their fans. The O6N source also sets
`PM_NOC_IDLE_CONFIG 0` and `PM_WDT_CONFIG 0`.

To reproduce: build the generator's parse mode (`csupm_bin_config <bin>`)
against the matching header to decode, write `user_config.h` from the decoded
structure, and generate with `TZ=UTC` and `SOURCE_DATE_EPOCH` set to the vendor
timestamp plus the CIX epoch (2021-10-15 09:00, `CixEpoch()` uses `mktime`,
so the time zone matters).
