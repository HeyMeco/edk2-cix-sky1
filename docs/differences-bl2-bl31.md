# How the open BL2 and BL31 differ from the vendor's

The open BL2 and BL31 are TF-A v2.7 from CIX's public tree
(`arm-trusted-firmware`, `114fb205`) with this repository's patch queue
(`patches/2026Q3/arm-trusted-firmware`), built by
[scripts/open/trusted.py](../scripts/open/trusted.py). This page lists
every way they differ from the vendor's stages that a user, the OS,
OP-TEE or UEFI could observe, or that matters for trust, and how each is
known.

How it is known: both pairs of images run in an emulator
([`sky1emu a64-test`](../emu/README.md#bl2-and-bl31-aarch64)) on the same
model of the Sky1, from BL2's entry to the jump to UEFI and through every
SMC, and what they do is compared item by item: every device register's
writes in order, the SPI transactions, messages to the PM and the SE,
what each next stage is entered with, DRAM, and per SMC what the caller
gets back. Today that is 121 phases with 0 unexplained differences; each
explained one is an entry of `EXPECTED` in
[emu/src/a64/expected.rs](../emu/src/a64/expected.rs), and each is below
with the phase that shows it. "CIX's" is CIX's own build of 2026-07-15
(TF-A `v2.7-33527c31abbe`, `edk2-non-osi/.../PackageTool/Firmwares`),
which the patches were written from; the Orion O6's own vendor
bootloaders are a month older and differ from it in three ways ([Radxa's
build](#radxas-build-for-the-orion-o6-june-2026)).

**Nothing here has run on hardware.** The model is inferred from the code
that drives it ([what the emulator does not show](#what-was-only-emulated)).

## At a glance

| Where | The open stage | The vendor's |
|---|---|---|
| BL2: authentication | every image and certificate, against the root key built into BL2 | none with secure boot not fused (every board sold); bootloader3's trusted key certificate never |
| BL2: bootloader3 as the vendor's tool certifies it | refused (another root key) | booted |
| BL2: bootloader3 recovery | none | reads the TrustZone configuration and can restore bootloader3 from a backup |
| BL2: firmware header without its magic | not used: default offsets | used |
| BL2, BL31: progress codes at `0x05030f00` | none | written |
| BL31: `0xc2000030`, `0xc2000031` (OEM key hash) | not there (`SMC_UNK`) | there, with a stack overflow from the caller's length |
| BL31: `0xc200000b` (SPI NOR) from the normal world | buffer must be non-secure DRAM | any address: secure memory read to flash, flash over secure memory |
| BL31: `0xc200000a` (power domain) with an ID out of range | ignored | faults |
| BL31: TrustZone configuration | secure storage over the boot firmware dropped; SPE only for `spe_feature_en` 1; file lengths the tool does not write refused | handed on as it is; SPE for any value but 0; anything whose sums fit |
| BL31: the SE's suspend command | the state as a word | the state and three bytes of BL31's stack |

Everything else compared is the same, including the items that were not
before the emulator found them ([faults found and fixed](#faults-of-the-open-stages-that-the-emulator-found)).

## BL2

Phases `BL2: ...` of `a64-test bl2` (34 configurations, in
[emu/src/a64/bl2.rs](../emu/src/a64/bl2.rs)). In all of them without a
note below, the two BL2s write the same values to the same registers in
the same order (the TZC-400s for every DRAM size and channel mask, the
flash controller, pads, clocks, watchdog), read the same flash
transactions but the ones listed, hand BL31 the same image list, entry
points, arguments, PSTATE and system registers, and leave the same bytes
in DRAM: BL31, OP-TEE and UEFI (by SHA-256), the version string for UEFI,
the boot record, the RAM log.

**Every image is authenticated.** CIX's BL2 returns "authenticated"
without looking when the SE's key block says secure boot is off (word
`0x801ff06c`; its function at `0x80001350`), which is what the fuses of
the boards sold say, and with it on still skips the signature of image
36, bootloader3's trusted key certificate (`0x80000fd8`). The open BL2
verifies the whole chain of both FIPs against the SHA-256 of `ROT_KEY`
built into it (`plat/cix/sky1/sky1_trusted_boot.c:30`,
`plat/cix/common/cix_cot_bl2.c`), whatever the fuses say. Phases
`BL2: open BL2 refuses a changed byte in bootloader2's entry 0..6` and
`... bootloader3's entry 0..3`: with one byte changed in any certificate,
BL31, OP-TEE or UEFI the open BL2 stops ("Verify image: N fail") and
CIX's boots the changed image. On the O6 and O6N that root key is
public, so this catches corruption, not an attacker who can write the
flash. Cost: about 216 million instructions from entry to the jump
against CIX's 4.5 million (the report prints both).

**The vendor's bootloader3 is refused.** As CIX's tool certifies UEFI,
bootloader3's trusted key certificate is a file made in June 2025 under
another root key (SubjectPublicKeyInfo SHA-256 `0d999213...`) than
bootloader1's and bootloader2's (`33d47f74...`), without a counter
extension. CIX's BL2 never checks that certificate, so it does not
matter there; the open BL2 does and stops with "Verify
trusted_key_no.crt fail" (phase `BL2: the vendor's flash image`).
`--open` builds certify bootloader3 with `cert_create` under the one
root. An open BL2 therefore boots only images built that way.

**The root of trust is the key built in**, not the hash the SE passes at
`0x801ff000` (CIX's `plat_get_rotpk_info` at `0x80006ba0` copies that:
the SHA-256 of BL1 key 2's modulus and exponent). There is no rollback
protection in either: the counters read 0 and are not written (CIX's
`plat_get_nv_ctr` at `0x80006cfc`, `plat_set_nv_ctr2` at `0x80005650`;
ours `sky1_trusted_boot.c:39`).

**Mbed TLS in software** in place of CIX's crypto engine library, which
drives a hardware engine at `0x05050000` and is not public. CIX's BL2
with secure boot fused on has not been run in the emulator for that
reason (the model has no such engine); what its authenticating path
decides is known from its code only.

**No bootloader3 recovery.** CIX's BL2 (July) sets the flash up a second
time and reads the TrustZone configuration at `0xf0000` for
`bl3_backup_recovery_en`; when that is set it can copy a backup
bootloader3 (image types 0x68 and 0x69) over one that fails. No board's
layout has those images and every configuration file here has the flag
off. Seen as: six more SPI transactions and a second write of the
flash's clock, reset and pad registers in CIX's (every `BL2` phase,
items `SPI`, `(xSPI)`, `(FCH control)`, `(IOMUX_S5)`), and its message
"TrustZone: Failed to get config address from flash header, use default"
when there is no header (phase `BL2: boot ROM reports the second copy`).

**The firmware header is taken only with its magic.** The header
`cix_package_tool` writes at 1 MiB starts with `0x55aa55aa`; the open
BL2 checks it and walks at most 256 entries
(`plat/cix/common/cix_spi_nor.c:142`), and without it uses the default
offsets (`0x288000`, `0x3f0000`). CIX's lookup (`0x80004920`) has no
check of the magic and walks as many entries as the count word says.
Phase `BL2: no firmware header in the flash` (the magic's first byte
changed, the entries intact): CIX's finds bootloader3 by its entry and
boots; the open one looks at `0x3f0000`, where this layout has no FIP,
and stops.

**No progress codes.** CIX's stages write codes to `0x05030f00` (43 in
BL2, more in BL31's cold boot and at run time); the open ones write none.
The public tree's `cix_postcode_debug()` writes to
`POSTCODE_REGISTER_BASE`, which it defines as `0x85000000`, a word of
DRAM: the open BL31 leaves `0xc203` there before UEFI starts, CIX's
`0xc201` (phase `BL31: cold boot`, item `DRAM page 0x85000000`).

**A flash controller that never gets idle** (phase of that name): both
give up and panic. CIX's has tried once more by then, the open one says
"BL2: cannot set up the boot flash".

Not different, and checked: the version string at `0x83e01080` and the
boot record at `0x83e04000` (same format; the version itself differs),
the RAM log at `0x83dbc000` (entries numbered 1, 2, 3 in both).

## BL31

Phases `BL31: ...` of `a64-test bl31` ([emu/src/a64/bl31.rs](../emu/src/a64/bl31.rs)).
The cold boot's 708 items agree but for the addresses inside BL31's own
image (`TTBR0_EL3`, `VBAR_EL3`, `TPIDR_EL3`, the cores' reset vector and
the resume address it leaves in `SW_USED_REG7`: its entry point, which is
not where CIX's is) and the progress codes. At run time every SMC of the
phases returns the same registers and does the same to the devices, the
PM and the SE, except:

**`0xc2000030` and `0xc2000031` are not there.** In CIX's BL31 they pass
the caller's x1 and x2 as address and length into the SE commands
`0x8200001a` (burn the OEM public key hash into the eFuse) and
`0x8200001b` (read whether it is burnt). The open BL31 answers `SMC_UNK`
and sends nothing (phase `BL31: SiP calls`, items `SMC 0xc200003...`).
See [the stack overflow](#faults-of-the-vendors-images-that-the-open-ones-do-not-have).

**The SPI NOR call checks the buffer.** `CIX_SIP_NOR_STORAGE`
(`0xc200000b`) runs a flash transaction on a buffer given as a physical
address. From the normal world the open BL31 takes it only inside
non-secure DRAM (`plat/cix/common/cix_sip_svc.c:151`, patch 0007);
OP-TEE's calls are not restricted. Phase `BL31: SPI NOR by SiP call`,
items `NOR outside the caller's memory, ...`: CIX's BL31 reads flash
data over OP-TEE's memory, the TrustZone configuration page and the last
secure page, programs BL31's and OP-TEE's memory into the flash (the
normal world reads it back), writes flash data into the GIC distributor's
registers, and stops with a fault when the buffer is BL31's read-only
code or an address it has not mapped; the open one answers `SMC_UNK` to
each and touches nothing. With a buffer in non-secure DRAM the two do
the same in every bus format, across a 2 MiB boundary, erasing and
programming.

**Power domain IDs are checked.** `CIX_SIP_POWER_DOMAIN_SET`
(`0xc200000a`) with an ID past the table
(`plat/cix/sky1/drivers/scmi/scmi_pd.c:393`): ignored; CIX's indexes its
table with it and faults for `0x80000000` and `0xffffffff` (phase
`BL31: SiP calls`).

**The TrustZone configuration** (`trustzone_config.bin`, image type 9;
`plat/cix/sky1/sky1_tz_config.c`, patch 0004). With CIX's stock file and
with files of both tool versions the two leave the same bytes for OP-TEE
at `0x80400000` and the same SPE setting. They differ in (phases
`BL31: cold boot, TrustZone configuration ...`):

* a secure storage region of no size, or over the flash's header or an
  image the header lists: the open BL31 hands OP-TEE a configuration
  without secure storage (`sky1_tz_config.c:184`), CIX's hands the region
  on. Radxa's file for the O6 and O6N is such a one: 2 MiB at 6 MiB,
  where bootloader3 is;
* `spe_feature_en` values other than 0 and 1 (0xff, 2): SPE off in the
  open BL31 (`sky1_tz_config.c:207`; `MDCR_EL3` `0x403818000`), on in
  CIX's (`0x140381b000`). The tool writes 0 or 1;
* a file whose length is below 36 or not a multiple of 4, or whose
  configuration length is below 12: refused by the open BL31 ("CRC check
  failed" or "configuration length N", then the defaults). CIX's sums as
  many bytes as the length word says, beyond its 40-byte copy of the file
  too, and takes the 11 data bytes whatever the configuration length
  says.

The messages are CIX's where it has one for the case ("TrustZone: CRC
check failed", "TrustZone: Flash config invalid, use default", ...), at
its level, down to the `%08X` its console's printf cuts the line at.

**The SE's suspend command** (`0x82000007`) carries a word. The open
BL31 sends the system state, 2 (`plat/cix/sky1/sky1_pm.c:453`); CIX's
passes the address of a one-byte variable, so the word is `0x..ef9002`
with three bytes of its stack (phase `BL31: SYSTEM_SUSPEND and resume`,
item `going down: to the SE`). The SE's handler does not read it
(`firmware/se/service/str.c`).

**`0xbe000009` from the normal world** (OP-TEE's request to the SE,
which BL31 also takes from the normal world) returns x3 from a variable
nothing sets (`services/spd/opteed/opteed_main.c:274`,
`ser_num_data.x3`): each build returns what its stack holds there, the
open one an address inside BL31, CIX's a register of the caller's. Kept
as the public tree has it; the value says nothing an image's listing does
not.

## Faults of the vendor's images that the open ones do not have

Each found by a question that killed the vendor's image in the emulator,
then read in its disassembly (CIX's July BL31 unless said).

* **`0xc2000030`: a stack overflow in EL3 with the caller's data.** The
  handler at `0x802073cc` calls `mbox_send_cmd_sync` (`0x80207220`) with
  the caller's x1 as source and x2 as length; that function copies
  `length` bytes to `sp+0x48` of its 0xc0-byte frame (`memcpy` called at
  `0x80207290`), a buffer of 120 bytes with the handler's saved x29 and
  x30 behind it. With 0x1000 bytes the handler's `ret` at `0x8020741c`
  goes to an address taken from the caller's buffer (in the emulator, of
  zeroes: "permission fault, execute at 0x0"). Any code that can make an
  SMC from the normal world can run EL3 at an address of its choice. The
  open BL31 has not got the call.
* **`0xc200000b` on any address** and **`0xc200000a` with any ID**:
  above.
* **Reading on past an image (BL2 of June 2026, and the public tree).**
  A flash transfer that crosses a 2 MiB boundary goes out in pieces, and
  `cdns_xspi_mem_op_execute()` left the last piece's address, length and
  buffer in the caller's operation; `spi_nor_read()` goes on from those.
  Loading UEFI (at `0x406000`, across `0x600000`) BL2 then read another
  3.8 MiB of the flash, beyond the chip's end, into the DRAM behind the
  image. CIX's July BL2 restores the three fields (`0x80002880`); the
  open tree now does the same (patch 0008,
  `drivers/cadence/xspi/xspi.c:465`).
* **The header lookup without bounds** (BL2, `0x80004920`): the entry
  count comes from the flash unchecked.

## Faults of the open stages that the emulator found

Fixed; before, no `--open` image could have booted.

1. **BL2 stopped at "Verify image: 9 fail" on every FIP whose
   certificates carry a counter above 0** (the O6's carry 31): TF-A's
   generic `plat_set_nv_ctr2()` refuses a certificate whose counter is
   above the platform's unless the root key signed it, and the platform's
   reads 0. CIX's BL2 has its own that returns 0; so has the open one now
   (`sky1_trusted_boot.c`, patch 0002).
2. **BL31 was built as CIX's single-core variant.** Without `SMP=1` the
   tree's `plat_cix_gicr_gicd_config()` turns every redistributor but one
   off (`GICD_RDOFFR = 0xfef`) and gives the one frame to core 4; on the
   core the SE boots (10) BL31 stops at "No GICR base frame found for
   Primary CPU". `trusted.py` now passes `SMP=1`.
3. **`SCR_EL3.HXEn` was clear.** CIX's build sets it (`ENABLE_FEAT_HCX=1`);
   without it a write to `HCRX_EL2` from EL2 on a core that has the
   register (the A720 and A520 do, by their manuals) traps to EL3, which
   has no handler for it. `trusted.py` now passes it.
4. **Reading on past an image**, as the vendor's June BL2 (above).
5. **System suspend** differed from CIX's in four places: bit 0 of
   `0x16000800` cleared then set (two writes); the suspending core left
   in bits 31:28 of `SW_USED_REG2` after the PM's suspend message; the
   audio domain and the `0xbb` marker before the first read of the
   counter (the counter value written back after the resume is then
   CIX's); the suspend word (above) (`sky1_pm.c`, patch 0005).
6. BL2's RAM log carried counter values where CIX's numbers its entries;
   its fall-back to the default FIP offsets was a warning (not printed)
   where CIX's is an error; BL31's TrustZone messages were its own.

## The same in both, and worth knowing

* An exception EL3 does not expect from a lower level (a trapped
  register access, say) ends in `plat_panic_handler`, which selects
  SP_EL0 (`plat/cix/common/cix_bl31_crash.S:8`) and runs CIX's panic
  handler in C: on SP_EL0 as the interrupted level left it, since the
  vector had not loaded EL3's own yet. In the emulator, with SP_EL0 0,
  both images fault there instead of reporting (phase `BL31: interrupts
  and exceptions taken to EL3`). Nothing in the phases makes such an
  exception from the normal world with these builds' traps.
* `SDEI_INTERRUPT_BIND` (`0xc400002d`) of interrupt 1020 or above ends in
  a panic in both ("Activation priority mismatch").
* OP-TEE's "CPU on done" without a CPU on, or an SMC number of its range
  that the dispatcher does not know, ends both.
* The flash's size is taken as twice the JEDEC capacity (`2 << id[2]`) in
  `xspi.c` in both.

## Radxa's build for the Orion O6 (June 2026)

`a64-test --cix-bl1 .../Radxa/Orion/O6/Firmwares/bootloader1.img
--cix-fip .../bootloader2.img` (TF-A `v2.7-ed9772217e49`, built
2026-06-13): 0 differences with `O6_JUNE` added to the table. Against
that build the open stages differ in what CIX changed in the month
after:

* NI-700 QoS: the NPU's two ports 0x7 (June: 0xd), and entries for the
  SE's and the PM's ports at 0xd (June: none) (patch 0006);
* the June BL31 knows the TrustZone tool's older file only (36 bytes): it
  refuses CIX's stock 40-byte file ("CRC check failed") and uses its
  defaults, which are that file's values; it has no bootloader3 recovery
  flag, and its BL2 no recovery;
* the June BL2 reads on past images (above).

## What was only emulated

All of it. In particular:

* the model's devices do what the drivers expect: a register with no
  modelled behaviour reads back what was written. The GIC's redistributor
  assignment (`GICD_RDOFFR`, `GICR_MPIDR`), the PPUs, the mailboxes and
  the SE's and PM's answers are inferred from CIX's code, not from a
  document;
* lower exception levels are the test harness: OP-TEE answers BL31's
  entries with fixed values, UEFI and the OS are the SMCs of the phases;
* the cores' identification registers are from the Cortex-A720 and A520
  manuals, not read from a chip;
* time passes when the firmware reads the counter, so nothing here
  measures speed; the open BL2's authentication will cost boot time on a
  board;
* CIX's BL2 with secure boot on is not run (its crypto engine).

The interpreter itself is checked against a real AArch64 CPU for every
instruction word of the four images that user mode can run, and by an
audit of callee-saved registers over every run
([emu/README.md](../emu/README.md#bl2-and-bl31-aarch64)).

## What no test reaches

`a64-test --cov FILE`, then `sky1emu a64-cov FILE bl2.elf bl31.elf`.

BL31: 81 % of its code bytes run; 69 of 653 functions never do:

* the device tree and `fconf` readers (`fdt_*`, `fconf_populate_dtb_registry`,
  `set_config_info`, with `memchr`, `memcmp`, `strlen`; 29 functions):
  linked in, but BL2 passes no device tree on this platform;
* the SCMI server's clock and reset handlers (`plat_scmi_clock_*`,
  `plat_scmi_rstd_*`, `write_rate_desc_array_in_buffer`): BL31 lists the
  power domain protocol only, and the phase `BL31: SCMI by SiP call`
  shows messages of the others refused before they get there;
* PSCI calls the platform does not offer, refused at the dispatcher
  (`psci_migrate`, `psci_node_hw_state`, `psci_mem_protect`,
  `psci_mem_chk_range`);
* the SPI NOR driver's paths for chips above 16 MiB (`spi_nor_clean_bar`,
  `spi_nor_read_sr`, `spi_nor_wait_ready`, `spi_nor_write_en`) and the
  controller's two empty hooks: the platform fixes the size at 16 MiB;
* error exits that need hardware faults the model does not make:
  external aborts routed to EL3 (`delegate_sync_ea`,
  `plat_handle_double_fault`, `plat_handle_el3_ea`,
  `plat_handle_uncontainable_ea`; the build has `HANDLE_EA_EL3_FIRST=0`),
  the GIC-600 power sequencer's wait (`gicr_wait_group_not_in_transit`);
* GIC helpers no caller reaches as configured (`gicd_set_igrpmodr`,
  `gicr_get_isactiver`, `gicr_set_icfgr`, `gicr_set_ispendr`,
  `gicv3_rdistif_base_addrs_probe`), the console's input
  (`console_cix_uart_getc`), the AMU's write of virtual offsets
  (`amu_group0/1_voffset_write_internal`: restored only when the
  hypervisor had them on at the save);
* not reached though they could be: `pcie_hub_its_ctrl` (the PCIe hub's
  domain is not switched by the SCMI server as the phases ask),
  `ehf_deactivate_priority` and `check_memory_valid` (the firmware's own
  SDEI event completed by the OS after a panic, and the last-stack dump).
  The vendor's image is not reached there either, so nothing is compared.

BL2: 51 % of its code bytes run; 152 of 450 functions never do: 103 are
Mbed TLS functions the chain of trust does not use (elliptic curves, key
writing, other hashes), 29 the same device tree readers, the rest the SPI
NOR paths for larger chips, `plat_report_exception` (exceptions are not
delivered in the emulator), the console's input and a few empty hooks.
