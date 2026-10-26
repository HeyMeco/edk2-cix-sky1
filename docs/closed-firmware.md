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
| `bootloader1.img` #3 | TF-A BL2 | cixtech TF-A, with gaps | yes, if BL1 can be re-signed |
| `bootloader2.img` | TF-A FIP: BL31 + OP-TEE BL32 (with StandaloneMM) | cixtech TF-A and OP-TEE, with gaps | yes |
| `csu_pm_config.bin` | PM configuration | `pm_config` generator | yes, byte-identical for both Orange Pi boards |
| `memory_config.bin` | DRAM configuration | `mem_config` generator | Radxa/Minisforum: built; Orange Pi: not yet decoded |
| `trustzone_config.bin` | TZC configuration | `trustzone_config` generator | yes |
| `se_config.bin` | `CIXSECONFIG` header, otherwise zero | format known from `SeConfigUpdateDxe` | yes |
| `sfh_fw.bin` | 17 KB Cortex-M image (vectors at `0x2007fff0`, mailbox) | none | no counterpart; small enough to clean-room |
| `ec_fw.bin` | ITE 8051 EC firmware (`ITE EC-V00.1`, `EC V1.13`) | none | no |
| `Silicon/CIX/Sky1/Drivers/*.efi`, `Platform/CIX/Sky1/Library/*.lib` | CIX DXE drivers and libraries (CIX Jenkins build paths in the debug directory) | none, except `NorFlashStmmRuntimeDxe`, `I2cDxe`, `SpiDxe` in `edk2-platforms` | small drivers could be clean-roomed; PCIe, TypeC PHY, DPU, CDNSP are large |
| `fiptool`, `cert_create` | host tools | TF-A `tools/` | yes |
| `cix_package_tool`, `cert_uefi_create_rsa` | host tools | binaries only (`cixtech/cix_package-tool`) | packer simple enough to reimplement |

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

Signing is not a blocker: BL2 checks the FIP certificate chain against the
same trusted-key certificate and default CIX OEM key that already sign BL33
(see [architecture](architecture.md#build-flow-scriptsbuildpy)), and TF-A's
`cert_create` produces the certificates.

## bootloader1

A signed `CIXBTFF!` container:

| Offset | Contents |
|---|---|
| `0x0` | magic, version `0003 0101` |
| `0x1c` | RSA-3072 public key 1 (root), exponent at `0x19c` |
| `0x324` | RSA-3072 public key 2 (OEM), exponent at `0x4a4` |
| `0x800` | supported SPI flash table (JEDEC IDs, Winbond/GigaDevice) |
| `0x9e4` | image table: 3 entries of type, offset, size, load address |
| `0xb6c` | SHA-256 per image |
| `0xe00` | RSA-3072 signature |

| # | Offset (CIX default) | Load | Contents |
|---|---|---|---|
| 1 | `0x1000`, 332 KB | `0x20000000` | **SE firmware**, Cortex-M (`CiX SE firmware Sky1`, `se_fw.elf`): eFuse ROTPK selection, image verification (RSA/SM2, mbedtls), secure debug, and **DDR initialisation and training** (`dpmv8_*`, `Ch %d Rank %d`), clocks and resets |
| 2 | `0x53000`, 319 KB | `0x06a90000` | **CSU_PM firmware**, Xtensa (`XBIN` header, `MINI_PORT_00.10.002`, `cix_trunk`), built on Arm SCP-firmware (`[FWK]`, `[PD]`, `[PPU_V1]`, `[SCMI]`, `mod_clock`): DVFS/OPP, PMIC/EDP limits, thermal, fans, DDR DFS, a debug CLI |
| 3 | `0xa3000`, 194 KB | | **TF-A BL2** (`BL2_AT_EL3`), the same build as the BL31 in `bootloader2` |

### Signing

| Image | Key 1 | Key 2 |
|---|---|---|
| CIX default, Radxa `4e99786e`, Minisforum `c22fb43e` | `c6d0e93f…` | `5672dfd9…` |
| Orange Pi 6, Orange Pi 6 Plus | `fb3f60e4…` | `fb3f60e4…` (same key) |

(prefixes of SHA-256 over the 384-byte modulus)

The SE firmware verifies a two-step chain (`Verify Cix ROTPK successful`,
`Verify OEM ROTPK successful`). Orange Pi ships a different key, self-certified
in both slots, and those boards boot it. Together with the strings
`[Image]System will use OEM RSA rotpk`, `oem pubkey hash is empty`,
`Secure boot disable` (SE) and `ROTPK is not deployed on platform. Skipping
ROTPK verification.` (BL2), this suggests that a chip with an empty OEM
ROTPK hash in eFuse trusts the key embedded in the image, so a container
signed with our own key would boot on unfused boards. The first check is in
the mask ROM, so this cannot be confirmed from the binaries. The test is to
re-sign an unmodified Orange Pi BL1 with a new key and boot it on a board
that can be recovered with an SPI programmer. The CIX package tool does not
build this container; a packer would have to be written.

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

To reproduce: build the generator's parse mode (`csupm_bin_config <bin>`)
against the matching header to decode, write `user_config.h` from the decoded
structure, and generate with `TZ=UTC` and `SOURCE_DATE_EPOCH` set to the vendor
timestamp plus the CIX epoch (2021-10-15 09:00, `CixEpoch()` uses `mktime`,
so the time zone matters).
