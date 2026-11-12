// SPDX-License-Identifier: BSD-2-Clause-Patent
//! What differs between the open BL2 and BL31 and CIX's and is not
//! counted: each entry with the reason, matched by phase, item and both
//! values (substrings; "" matches anything).

use super::test::Expected;

const OWN_ADDRESS: &str = "an address inside the stage's own image (its translation tables, vectors, stack), which is not laid out as CIX's; the next stage sets its own";
const POST_CODES: &str = "CIX's stages write progress codes to 0x05030f00; the open ones do not (docs/differences-bl2-bl31.md)";
const RECOVERY: &str = "CIX's BL2 (July 2026) sets the flash up a second time and reads the TrustZone configuration at 0xf0000, for its bootloader3 recovery; the open BL2 has no recovery (docs/differences-bl2-bl31.md)";
const POST_DRAM: &str = "the public tree's cix_postcode_debug() writes its codes to DRAM at 0x85000000 (POSTCODE_REGISTER_BASE): the open BL31 writes the three of early setup there (the last is 0xc203), CIX's build only the first";
const READ_ON: &str = "the vendor's BL2 of June 2026 reads on past an image that crosses a 2 MiB boundary of the flash (xspi.c leaves the last piece's address and length in the operation, spi_nor_read() goes on from them): fixed in CIX's July build and in the open one";
const PD_RANGE: &str = "CIX_SIP_POWER_DOMAIN_SET (0xc200000a) with a domain that does not exist: CIX's BL31 indexes its table of use counts with it unchecked and faults on the address; the open one ignores it (patch 0007)";
const OEM_KEY: &str = "0xc2000030 and 0xc2000031 (burn and read the OEM key hash through the SE) are left out of the open BL31: SMC_UNK, nothing sent to the SE. CIX's copies as many bytes as the caller says from the caller's address into a 120-byte buffer on EL3's stack (mbox_send_cmd_sync, memcpy at 0x80207290): with 0x1000 bytes the handler returns to an address out of the caller's data";
const NOR_BUFFER: &str = "CIX_SIP_NOR_STORAGE (0xc200000b) from the normal world with a buffer that is not non-secure DRAM: CIX's BL31 runs the transaction on it (flash data over OP-TEE or the shared secure pages, secure memory programmed into the flash and read back, a write fault in BL31's own code, a translation fault on an address BL31 has not mapped); the open one answers SMC_UNK and touches nothing (patch 0007)";
const SUSPEND_WORD: &str = "the SE's suspend command (0x82000007) carries a word: the open BL31 sends the system state (2), CIX's the address of a one-byte variable, so the three bytes above the state are whatever its stack holds there";
const X3_LEAK: &str = "0xbe000009 (OP-TEE's request to the SE) from the normal world returns x3 from a variable nothing sets (ser_num_data.x3, opteed_main.c): each build returns what its stack holds; kept as the public tree has it";
const AUTH: &str = "the open BL2 authenticates every image against the root key built into it and stops at the first that fails; CIX's authenticates nothing when the SE reports secure boot off (the fuses of the boards sold), and boots the changed image";
const VENDOR_BL3: &str = "bootloader3 as the vendor's tool certifies it: its trusted key certificate (image 36, made in June 2025) is under another root key (SPKI 0d999213...) than bootloader1's and bootloader2's and has no counter; CIX's BL2 never verifies that certificate, the open one does and refuses the image (--open builds certify bootloader3 under the one root)";
const HEADER_MAGIC: &str = "the firmware header at 1 MiB without its magic (0x55aa55aa): the open BL2 does not take it and falls back to the default offsets (bootloader3 is not at 0x3f0000 in this layout); CIX's has no check of the magic (its lookup at 0x80004920 walks as many entries as the count word says) and finds the entries";
const SPE_VALUE: &str = "the TrustZone configuration's spe_feature_en: 1 is on, 0 and 0xff off (trustzone_export_config.h); the open BL31 keeps SPE from the normal world unless it is 1, CIX's gives it for any value but 0 (its tool writes 0 or 1)";
const TZ_OVERLAP: &str = "a secure storage region of no size, or over the flash's header or an image it lists (Radxa's file for the O6: 2 MiB at 6 MiB, where bootloader3 is): the open BL31 hands OP-TEE a configuration without secure storage, CIX's hands the region on (patch 0004)";
const TZ_STRICT: &str = "a TrustZone configuration file whose length or configuration length is not one the tool writes: the open BL31 refuses it and uses the defaults; CIX's sums as many bytes as the length word says (beyond its 40-byte copy too) and takes the 11 data bytes whatever the configuration length is";
const STUCK: &str = "a flash controller that never gets idle: both BL2s give up and panic; CIX's has tried the flash once more by then (its TrustZone configuration read), the open one says what failed";
const QOS_JULY: &str = "the NI-700 QoS values CIX changed between June and July 2026: the NPU's two ports 0x7 for 0xd, and entries for the SE's and the PM's ports (0xd); the open BL31 has July's (patch 0006)";
const TZ_OLD: &str = "the BL31 of June 2026 knows the trustzone_config tool's older file (36 bytes: no bootloader3 recovery flag) and sums a 40-byte file's last word from outside its copy of it: it refuses CIX's stock file (\"CRC check failed\") and uses its defaults, which are that file's values";

/// The table for a vendor build: what differs from CIX's build of 2026-07-15 (33527c31abbe), and with
/// an older vendor build what that build lacks of it.
pub fn table(reference: &str) -> Vec<&'static Expected> {
    let mut t: Vec<&Expected> = EXPECTED.iter().collect();
    if reference == "ed9772217e49" {
        t.extend(O6_JUNE.iter());
    }
    t
}

/// Radxa's bootloaders for the Orion O6 (built 2026-06-13, ed9772217e49).
pub const O6_JUNE: &[Expected] = &[
    Expected { phase: "BL2", what: "SPI", cix: "03 @0x6260d8/3", open: "nothing more", why: READ_ON },
    Expected { phase: "BL2", what: "(xSPI)", cix: "more: ", open: "nothing more", why: READ_ON },
    Expected { phase: "BL31", what: "register 0x0d05a0", cix: "more: 0xd", open: "more: 0x7", why: QOS_JULY },
    Expected { phase: "BL31", what: "register 0x0d05b0", cix: "more: 0xd", open: "more: 0x7", why: QOS_JULY },
    Expected { phase: "BL31", what: "register 0x0d0970", cix: "nothing more (of 0)", open: "more: 0x", why: QOS_JULY },
    Expected { phase: "BL31", what: "register 0x0d0980", cix: "nothing more (of 0)", open: "more: 0x", why: QOS_JULY },
    Expected { phase: "BL31", what: "console", cix: "TrustZone: CRC check failed | ERROR:   TrustZone: Flash config invalid, use default", open: "", why: TZ_OLD },
    Expected { phase: "BL31", what: "DRAM page 0x83dac000", cix: "TrustZone: CRC check failed", open: "", why: TZ_OLD },
    Expected { phase: "TrustZone configuration", what: "DRAM page 0x80400000", cix: "", open: "", why: TZ_OLD },
];

pub const EXPECTED: &[Expected] = &[
    Expected { phase: "BL2", what: "system register TTBR0_EL3", cix: "0x800", open: "0x800", why: OWN_ADDRESS },
    Expected { phase: "BL2", what: "system register VBAR_EL3", cix: "0x800", open: "0x800", why: OWN_ADDRESS },
    Expected { phase: "BL2", what: "stack pointers EL0-EL2", cix: "0x800", open: "0x800", why: OWN_ADDRESS },
    Expected { phase: "BL2", what: "register 0x05030f00", cix: "more: 0xb00000 0xb00100", open: "nothing more (of 0)", why: POST_CODES },
    Expected { phase: "BL2", what: "(FCH control)", cix: "more: ", open: "nothing more", why: RECOVERY },
    Expected { phase: "BL2", what: "(IOMUX_S5)", cix: "more: 0xdc (of 2)", open: "nothing more", why: RECOVERY },
    Expected { phase: "BL2", what: "(xSPI)", cix: "more: ", open: "nothing more", why: RECOVERY },
    Expected { phase: "BL2", what: "SPI", cix: "9f @0x0/0 -> 0x3; 03 @0x188800/3 -> 0x1; 03 @0x188804/3 -> ", open: "nothing more", why: RECOVERY },
    Expected { phase: "BL2", what: "console", cix: "TrustZone: Failed to get config address from flash header, use default", open: "", why: RECOVERY },
    Expected { phase: "flash controller never gets idle", what: "console", cix: "busy timeout | PANIC at PC", open: "BL2: cannot set up the boot flash | PANIC at PC", why: STUCK },
    Expected { phase: "open BL2 refuses", what: "end", cix: "jumps to BL31", open: "stops: wfi", why: AUTH },
    Expected { phase: "open BL2 refuses", what: "console", cix: "BL2: Booting BL31", open: "ERROR:   ####", why: AUTH },
    Expected { phase: "open BL2 refuses", what: "", cix: "", open: "", why: AUTH },
    Expected { phase: "BL2: the vendor's flash image", what: "end", cix: "jumps to BL31", open: "stops: wfi", why: VENDOR_BL3 },
    Expected { phase: "BL2: the vendor's flash image", what: "console", cix: "BL2: Booting BL31", open: "Verify trusted_key_no.crt fail", why: VENDOR_BL3 },
    Expected { phase: "BL2: the vendor's flash image", what: "", cix: "", open: "", why: VENDOR_BL3 },
    Expected { phase: "BL2: no firmware header", what: "end", cix: "jumps to BL31", open: "stops: wfi", why: HEADER_MAGIC },
    Expected { phase: "BL2: no firmware header", what: "console", cix: "BL2: Booting BL31", open: "use deault value", why: HEADER_MAGIC },
    Expected { phase: "BL2: no firmware header", what: "", cix: "", open: "", why: HEADER_MAGIC },
    Expected { phase: "BL31", what: "system register TTBR0_EL3", cix: "0x802", open: "0x802", why: OWN_ADDRESS },
    Expected { phase: "BL31", what: "system register VBAR_EL3", cix: "0x802", open: "0x802", why: OWN_ADDRESS },
    Expected { phase: "BL31", what: "system register TPIDR_EL3", cix: "0x802", open: "0x802", why: OWN_ADDRESS },
    Expected { phase: "BL31", what: "(DSU RCSU)", cix: "more: 0x802", open: "more: 0x802", why: OWN_ADDRESS },
    Expected { phase: "BL31", what: "SMC 0xc200000a 0x", cix: "died: translation fault", open: "x0-x3 [0x0 ", why: PD_RANGE },
    Expected { phase: "BL31", what: "SMC 0xc200003", cix: "", open: "x0-x3 [0xffffffffffffffff ", why: OEM_KEY },
    Expected { phase: "BL31", what: "SMC 0xc200003", cix: "more: ", open: "nothing more (of 0)", why: OEM_KEY },
    Expected { phase: "BL31", what: "SMC 0xc200003", cix: "NOTICE:  Send CMD: 0x820000", open: "-", why: OEM_KEY },
    Expected { phase: "SPI NOR", what: "outside the caller's memory", cix: "", open: "x0-x3 [0xffffffffffffffff ", why: NOR_BUFFER },
    Expected { phase: "SPI NOR", what: "outside the caller's memory", cix: "more: ", open: "nothing more (of 0)", why: NOR_BUFFER },
    Expected { phase: "SPI NOR", what: "read back what the programs from secure memory left", cix: "x0-x3 [0x0 ", open: "buffer now ffffffffffffffffffffffffffffffff", why: NOR_BUFFER },
    Expected { phase: "BL31", what: "going down: register 0x05080008 (SE mailbox)", cix: "more: 0x2", open: "more: 0x2 (of 1)", why: SUSPEND_WORD },
    Expected { phase: "BL31", what: "going down: to the SE", cix: "more: 0x2", open: "more: 0x2 (of 2)", why: SUSPEND_WORD },
    Expected { phase: "BL31", what: "going down: register 0x1600051c (PMCTRL_S5)", cix: "more: 0x802", open: "more: 0x802", why: OWN_ADDRESS },
    Expected { phase: "BL31", what: "SMC 0xbe000009 0x11", cix: "x0-x3 [0x4 0x0 0x0 ", open: "x0-x3 [0x4 0x0 0x0 ", why: X3_LEAK },
    Expected { phase: "with SPE", what: "system register MDCR_EL3", cix: "0x140381b000", open: "0x403818000", why: SPE_VALUE },
    Expected { phase: "with SPE", what: "CPU_ON core", cix: "MDCR_EL3=0x140381b000", open: "MDCR_EL3=0x403818000", why: SPE_VALUE },
    Expected { phase: "secure storage o", what: "DRAM page 0x80400000", cix: "+0x0=0x101 +0x4=0x", open: "+0x0=0x1", why: TZ_OVERLAP },
    Expected { phase: "length of 20", what: "console", cix: "", open: "TrustZone: CRC check failed", why: TZ_STRICT },
    Expected { phase: "length of 20", what: "DRAM page 0x83dac000", cix: "", open: "TrustZone: CRC check failed", why: TZ_STRICT },
    Expected { phase: "configuration length of 1", what: "console", cix: "", open: "TrustZone: configuration length 1", why: TZ_STRICT },
    Expected { phase: "configuration length of 1", what: "DRAM page 0x83dac000", cix: "", open: "TrustZone: configuration length 1", why: TZ_STRICT },
    Expected { phase: "configuration length of 1", what: "DRAM page 0x80400000", cix: "+0x0=0x101 +0x4=0x70 +0x8=0x10008", open: "+0x0=0x1", why: TZ_STRICT },
    Expected { phase: "BL31", what: "register 0x05030f00", cix: "more: 0xc0", open: "nothing more (of 0)", why: POST_CODES },
    Expected { phase: "BL31: cold boot", what: "DRAM page 0x85000000", cix: "+0x0=0xc201", open: "+0x0=0xc203", why: POST_DRAM },
];
