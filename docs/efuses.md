# eFuses

What the Sky1's one-time-programmable fuses hold, who reads them, what the
firmware does with them, and who can burn them, as far as CIX's firmware
and the open firmware built here show. Field addresses come from CIX's SE
and CSU_PM debug builds (full DWARF) and are the ones the open firmware
uses ([firmware/se](../firmware/se), [firmware/csupm](../firmware/csupm)).
CIX publishes no fuse map; what is not used by any firmware here is not
known, and the values on a given board are only known by reading them.

## The hardware

* **The array**: 2 KiB of fuses (16 384 bits), owned by the SE. The fuse
  controller at the SE's `SE_EFUSE_CTRL_BASE` programs one bit at a time
  (row = bit address [12:5], column [4:0], bank [13]) and re-senses rows
  into the mirror.
* **The mirror ("fuse SRAM")**: the boot ROM loads the whole array into a
  RAM at SE `0xb0040000` (`SE_EFUSE_BASE`); every read in the firmware is a
  read of the mirror, and a field is addressed by byte address, bit offset
  and size in bits. The mirror can be overwritten (CIX's
  `sec_efuse_program_sram`), which changes what later code sees without
  touching the fuses; neither firmware here does that.
* **The PM's copy**: the SE copies part of the array into CSU_PM's SRAM
  (SoC `0x06ace4d8`, PM `0x200be4d8`: fuse byte *A* lands at
  `0x200be4d8 + A`) for the PM's sensor trims and V/F data ([below](#csu_pm)).
* **Life cycle**: a 3-bit state the SE reads at `SE_SYSCTRL_LIFECYCLE`
  (`0xb00300e0`), set from the crypto engine's OTP:

| Value | State | Effect in the SE firmware |
|---|---|---|
| 0, 1 | unknown, unrecognised | |
| 3 | FRA (field return) | test keys and test root hashes |
| 4 | BLANK (nothing fused) | test keys; the harvest fields and one flash field read as 0; serial number reported as a fixed placeholder; full logging; no check of the flash's read parameters |
| 5 | NONSECURE | test keys and test root hashes |
| 6 | SECUNLOCK | test keys and test root hashes |
| 7 | SECURE | CIX's production keys and root hashes |

## Map

Byte address, bit offset and width as the firmware reads them. "SE" is the
SE firmware, "PM" CSU_PM, "DDR" the SE's DDR driver.

| Byte | Bits | Field | Read by |
|---|---|---|---|
| `0x001`-`0x003` | 1 each | block harvest: NPU0 (`0x001`.7), ISP, DPU0-4, PCIe x8, audio (`0x002`.0-7), NPU1 (`0x003`.0) | SE (harvest, SRAM repair) |
| `0x046` | [15:4], 12 | CPU cores fused off (one bit per core) | SE, PM (through the S5 harvest register) |
| `0x048` | 64 | public serial number | SE: key blocks, mailbox |
| `0x054` | 128 | CIX model key | SE: HKDF for OP-TEE's TA key, crypto engine OTP (**secret**) |
| `0x064` | 128 | CIX device key | SE: HKDF for OP-TEE's hardware unique key, crypto engine OTP (**secret**) |
| `0x074` | 256 | CIX root public key hash, RSA | SE: verifies bootloader1's image key |
| `0x094` | 256 | CIX root public key hash, SM2 | SE |
| `0x0b4`, `0x0c4` | 128 each | test model and device keys (life cycles other than SECURE) | SE (**secret**) |
| `0x0d4`, `0x0f4` | 256 each | test root public key hashes, RSA and SM2 | SE |
| `0x154`-`0x15e` | | boot configuration ([below](#boot-configuration)) | SE |
| `0x168` | 128 | OEM model key | SE: mailbox, TEE only in the open SE (**secret**) |
| `0x188` | 256 | OEM root public key hash, RSA: bootloader1's header key | SE |
| `0x1a8` | 256 | OEM root public key hash, SM2 | SE |
| `0x1c8` | 64 | chip ID | SE: key blocks, secure debug binding, mailbox |
| `0x1d5`-`0x1d7` | 5, 9, 9 | wafer number, die x, die y | SE: PVT table lookup |
| `0x228`-`0x4e3` | | ATE region, copied to the PM ([below](#csu_pm)) | SE, PM |
| `0x308` | [16:7], 10 | PVT (process) average | SE: V/F table selection |
| `0x4a0` | 30 | glitch detector trims | SE, into the PLLs |
| `0x4a3` | [7:6] | ATE distribution flags: bit 0 PVT trims to the PM, bit 1 glitch trims | SE |
| `0x4ac` | 64 | DDR configuration ([below](#dram)) | DDR |
| `0x4cc` | [7:4] | DDR channels fused off | SE, DDR |
| `0x4cf` | [9:6], 4 | VPU cores fused off | SE |
| `0x4d1` | [11:2], 10 | GPU shader cores fused off | SE |
| `0x4d4` | [11:8], 4 | CPU frequency grade | SE, into the S5 registers |
| `0x4dc` | 64 | part number (OPN); its first 3 bytes also go to the clock controller | SE, PM, mailbox |
| `0x4e4` | 256 | OEM public key hash for the OEM boot chain (what "burning the OEM key" writes) | SE: key block for BL2, mailbox status |
| `0x564`-`0x58b` | | SRAM repair directory: three 10-bit entries a word (group [4:0], rows - 1 [9:5]) | SE |
| `0x588`- | 32 a row | SRAM repair data, per group | SE |

CIX's code also names fields it never reads here (AVB public key hash,
HDCP 2.2 and 2.3 LC128, JTAG mode, fuse override mode); their addresses are
not in either firmware.

### Boot configuration

| Byte | Bits | Field |
|---|---|---|
| `0x154` | 3 | secure boot enable (`SE_BT_SEC`) |
| `0x154` | [5:4] | signature algorithm: 1 RSA, 2 SM2, else by the image |
| `0x154` | [7:6] | hash algorithm |
| `0x155` | 0 | boot from the FCH flash controller |
| `0x155` | [3:1] | flash bus clock |
| `0x155` | 4 | flash SPI mode |
| `0x155` | [6:5] | flash RX delay |
| `0x155` | 7 | 4-byte addressing |
| `0x156` | 0 | watchdog |
| `0x156` | [8:1] | run address |
| `0x157` | [2:1] | error policy |
| `0x157` | [4:3] | SMN clock divider |
| `0x15c` | [3:2] | log mode |
| `0x15c` | [5:4] | POST code mode |
| `0x15c` | [8:7] | fault policy |
| `0x15e` | 1 | flash read advance |
| `0x15e` | 3 | OEM root key valid |
| `0x15e` | [6:4] | key mode |

## Secure boot

With the secure boot bit (`0x154`.3) set, the SE checks bootloader1
([firmware/se/framework/xloader.c](../firmware/se/framework/xloader.c)):

1. the OEM key in bootloader1's header hashes to `0x188` (RSA) or `0x1a8`
   (SM2), and signs the header;
2. CIX's image key in the header hashes to `0x074`/`0x094` (or the test
   hashes `0x0d4`/`0x0f4` below life cycle SECURE), and signs each image
   (CSU_PM, BL2); the header holds each image's digest.

What the boot ROM checks before it starts the SE firmware cannot be seen
in any binary here. With the bit clear the SE loads CSU_PM and BL2
unverified and logs `Secure boot disable`; CIX's BL2 then skips
authentication of everything after it, the open BL2 does not
([open firmware](open-firmware.md#bl2)).

Which boards have what fused is only known from behaviour: the O6 and
O6N boot bootloader1 containers re-signed with CIX's published chain-A
keys, so either nothing is fused or the fused hashes are those keys'; the
retail MS-R1 rejects a chain-A bootloader1, so a key is fused there
([closed firmware](closed-firmware.md), [what cannot be
shared](parity.md#what-cannot-be-shared)).

The **OEM public key hash at `0x4e4`** is a different field: CIX's
*Security Development Guide* describes it as the OEM's production key for
bootloader3 and everything after it. The SE hands it, with its valid bit
(`0x15e`.3), to BL2 in BL2's key block (`KEY_OEM_ROTPK_HASH_OFFSET`); the
public TF-A tree declares `cix_get_oem_rotpk_info` but only CIX's BL2
binary implements it. The open BL2 does not use it: it verifies bootloader2
and bootloader3 against the root built into it.

### Secure debug

A 4 KiB `CIXSECDEBUG` block in flash (`0x107000`, or `0x207000` by strap)
bound to the chip ID (`0x1c8`) opens debug features in the SE's debug
control register: secure and non-secure invasive and non-invasive debug,
JTAG. With secure boot fused the block must be signed with the OEM key;
without, an unsigned block that names the chip is enough. The open SE does
the same as CIX's.

## Keys

The SE derives every key the secure world gets from the eFuse keys
([firmware/se/framework/km.c](../firmware/se/framework/km.c)):

| Key | From | To |
|---|---|---|
| OP-TEE hardware unique key (16 bytes) | HKDF-SHA256 of the device key (`0x064`, or `0x0c4` below SECURE) | OP-TEE's key block, SoC `0x824fe000` |
| OP-TEE TA encryption key (32 bytes) | HKDF-SHA256 of the model key (`0x054`/`0x0b4`) | the same block |
| crypto engine OTP model and root keys | `0x054`, `0x064` as they are | the crypto engine (at start-up) |

OP-TEE's secure storage key is derived from the hardware unique key; the
open OP-TEE derives it as CIX's does (`CFG_CIX_SSK_COMPAT`, [open
firmware](open-firmware.md#op-tee)), so secure storage written by either
firmware stays readable by the other on the same chip. A chip's device key
never leaves the SE in the open firmware; in CIX's firmware the normal
world can read it ([below](#reading-fuses-from-the-ap)).

Every stage also gets the life cycle, the secure boot bit, the serial
number and chip ID: BL2 at SoC `0x801ff000`, UEFI at `0x83e00000`
(`PcdSeHandoffInfoBaseAddr`), OP-TEE in its key block.

## Harvesting

The SE tells the rest of the chip which parts are fused off
([firmware/se/framework/harvest.c](../firmware/se/framework/harvest.c)):

* the S5 power controller's software register `0x56000508`: CPU cores off
  [11:0], the AP boot core [15:12], blocks off [25:16] (NPU0/1, DPU0-4, PCIe
  x8, audio, ISP); CSU_PM and TF-A read it from there. CSU_PM keeps the
  core bits inverted (cores present) and takes its boot core from it;
* `0x5600050c`: the CPU frequency grade;
* the GPU (`0x55000304`) and VPU (`0x54230308`) harvest registers;
* SRAM repair skips fused-off blocks, and the DDR driver skips fused-off
  channels.

## DRAM

The DDR driver reads 64 bits at `0x4ac` once
([firmware/se/ddr/MemFuseLib.c](../firmware/se/ddr/MemFuseLib.c)):
channel configuration (2 bits), LPDDR type (2), rank count (2), bank
architecture (3), maximum data rate (16). With `0x4cc` [7:4] (channels
fused off) and the board's memory configuration it decides which channels
train and at what speed. CIX's "supported" checks return the complement of
a bit and are never 0; the port keeps that.

## V/F tables and sensors

The **SE** picks the V/F table CSU_PM will use
([firmware/se/driver/pvt.c](../firmware/se/driver/pvt.c)): the version the
board's memory configuration names, if it names one; otherwise by the PVT
(process) average, the fused value (`0x308`) times 1.1 or, on early dies
with none fused, CIX's measured value for the die's wafer and position
(`0x1d5`-`0x1d7`, [driver/pvt_data.c](../firmware/se/driver/pvt_data.c)):
435 and up table 4.1, 415 and up 4.0, 400 and up 4.2, below that 1.0; one
die (wafer 7, x 13, y 6) is listed for 4.0. The version and the average go
to CSU_PM in the PM strap.

### CSU_PM

If ATE marked the PVT data for distribution (`0x4a3`, bit 6), the SE
copies fuse bytes `0x228`-`0x4e3` into the PM's SRAM (PM `0x200be700`
onwards) and clears `0x4a4`-`0x4db` in that copy. CSU_PM's image carries
marker words at `0x200be700` (`0xa8763271`); while they are there the
fuses were not distributed and the PM reads nothing from them (this is
the case in the emulator). From the copy
([firmware/csupm/src/dpm/cfg_fuse.c](../firmware/csupm/src/dpm/cfg_fuse.c),
[src/pvt.c](../firmware/csupm/src/pvt.c)):

* **PVT sensor trims**: two per sensor (5 and 6 bits, every 31 bits from
  bit 70 of the copy);
* **part number** (`0x4dc`): its segment field (high word, [24:20]) picks
  the OPP limit table and whether V/F patches apply;
* **V/F curves**: for each CPU cluster three points of minimum voltage
  (frequency in 100 MHz steps, 5 bits; voltage in mV - 400, 10 bits), a
  DSU point, patches; the ATE phase (ES, DVT, PVT, MP, MP2, MP3) is
  inferred from which are present. When the BIOS's PM configuration enables
  it, the OPPs marked "from the fuses" get their voltages from these curves
  (`cfg_vf_calc_vmin_voltage`, capped at 980 mV);
* **leakage currents** (ibase) per domain (the GPU's at 25 °C and 85 °C),
  for the DPM's power model.

The open CSU_PM computes all of this as CIX's does
([test_vf.py](../scripts/open/test_vf.py) gives both images the same
simulated fuses: every ATE phase, part numbers, patches).

## Reading fuses from the AP

SE mailbox requests (`firmware/se/service/runtime.c`):

| Request | What | Open SE | CIX's SE |
|---|---|---|---|
| `0x82000002` | by ID: byte `0x001` or the OPN bytes `0x4dc`-`0x4e3`; `0x240` serial number and chip ID; `0xb40` OEM model key | OEM model key to the TEE mailbox only | to any mailbox |
| `0x8200000e` | by address: up to `0x70` bytes anywhere in the array | secret fields (model and device keys, test keys, OEM model key) to the TEE mailbox only | any field to any mailbox, the normal world's included |
| `0x8200001b` | OEM public key hash (`0x4e4`) status: 3 empty, 2 fused | the same | the same |
| `0x8200001a` | burn the OEM public key hash | refused | burns it |

So with CIX's SE firmware the operating system can read the device key the
OP-TEE hardware unique key is derived from.

## Burning

Burning is permanent. Setting the OEM public key hash binds the chip's
boot chain to that key.

What can burn fuses:

| Path | CIX's firmware | Built here (`--open`) |
|---|---|---|
| SE request `0x8200001a` (OEM public key hash) | burns it | refused |
| BL31 SiP calls `0xC2000030`/`0xC2000031`: pass a caller's address and length to SE requests `0x8200001A`/`0x8200001B` | in CIX's BL31 on every board, callable from the normal world | left out |
| UEFI `EfuseOemKeyDxe`: burns `OemPublicKey.bin` at boot when the hash is empty | `EFUSE_OEM_KEY_ENABLE`, off by default | off, pinned in `Sky1Board.dsc.inc` |
| UEFI fastboot `oem_pk_fuse` (CIX's `MiniFastbootLib`, through the binary `EfusePublicKeyLib`) | in fastboot builds | fastboot off on every board (`boards/*.yaml`) |
| fuse controller programming (`fw_efuse_program`, `drv_fuse_write_onerow`) | used for the request above | in the open SE's source, never called |

With CIX's boot firmware, then, the operating system can have the OEM key
hash burned through BL31 and the SE, on any board. Built with `--open`
nothing in the firmware burns a fuse. (`EfusePublicKeyLib` is a CIX binary
library; that it reaches the SE through the BL31 calls above is inferred
from its strings and the call chain, not read from its code.)

## In the emulators

The SE emulator ([se_emu.py](../scripts/open/se_emu.py)) models the mirror
as a byte map and the life cycle as a register; `test_se.py` runs life
cycles 4, 5 and 7, with test device and model keys (`--keys`) and with the
secure boot bit and the image's own root hashes fused (`--secure`). The PM
emulator ([pm_emu.py](../scripts/open/pm_emu.py)) leaves the PM's copy at
its marker; `test_vf.py --fuses SEED --phase N --opn ...` fills it with
random curves for an ATE phase, a part number and leakage data. None of
this says what a real board holds.
