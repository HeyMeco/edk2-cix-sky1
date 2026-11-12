// SPDX-License-Identifier: BSD-2-Clause-Patent
//! The differential test of the open SE firmware against CIX's debug
//! build, on the model with time (timed.rs): `se-test OPEN.elf FLASH.bin`.
//!
//! Suites (each a list of phases; `--only TEXT` keeps the phases whose
//! name has TEXT):
//!
//! * boot (always): reset to the idle loop;
//! * `--services`: every request of the run-time interface on both
//!   mailboxes, with valid, boundary and invalid arguments.
//!
//! EXPECTED lists the differences that are meant, each with its reason;
//! anything else is a failure.

use super::pyref::{ddr_stubs, keys_efuse, parse_features};
use super::soc::*;
use super::timed::*;
use crate::arm::Stub;
use crate::util::Args;
use std::collections::BTreeMap;

const CE_DRIVER: &str = "the open firmware has its own driver for the crypto engine (not CIX's CE-Lite library): it reads \
    the engine's configuration and status registers less often; what it writes is the same";
const SECRET_FUSES: &str = "secret eFuse fields (device and model keys) are given only to requests on the TEE mailbox";
const HASH_DRAM: &str = "the hash service only reads DRAM (CIX's hashes any address the SE can read, its own memory included)";
const NO_BURN: &str = "the request to burn the OEM root key hash into the eFuse is refused: nothing here programs eFuses";

const UNINIT: &str = "CIX's mailbox handler (cix_mbox_isr) reads a request into a buffer on its stack that it does not \
    clear (its sub sp, #260 at 0x20027436 is followed by a memset of the answer's half only, 0x200274e4): what a request \
    shorter than its handler expects leaves unset is whatever the stack held, the function ID itself when the size is \
    under 8. Ours clears the buffer: the missing words read as 0";
const OPN_STALE: &str = "CIX's fuse_id_handler sets neither the status nor the length of its answer when the OPN range is \
    refused (0x200337c0: it only logs): the AP gets status 0 with the length of the request before. Ours answers the \
    parameter error";
const OPN_WRAP: &str = "CIX's fuse_id_handler adds address and length without minding the carry, so a length near 4 GiB \
    passes its range check; fw_efuse_req_from_non_sec hands it to sec_efuse_read, which clears that many bytes from a \
    128-byte buffer on the stack (memset at 0x2001659c) before drv_fuse_read looks at the size: the SE's memory is \
    wiped from the stack up. Ours refuses the request";
const FUSE_UNCHECKED: &str = "CIX's fuse_addr_handler (0x8200000e) passes address, bit offset and size to sec_efuse_read \
    unchecked (0x20033824): fuse bytes past the fuse array are given out; more than 115 bytes do not fit the answer \
    (its size field wraps, the 128-byte buffers on the stack of sec_efuse_read and of the mailbox handler overflow): \
    the SE goes on with damaged registers, or takes a fault and resets the system (reason 0x447). Ours bounds all \
    three (at most 0x70 bytes, inside the array)";
const SEND_WAITS: &str = "CIX's firmware waits for the AP to take a message it sends (tx_block), up to 100 ms; the wake-up \
    source message is sent from the mailbox interrupt handler, with interrupts masked, where the acknowledgement \
    cannot be seen: the request takes the whole 100 ms and logs a time-out (the message is sent). Ours does not wait";

const STACK_LEAK: &str = "CIX's plat_send_wakeup_src sets only the seven size bits of its message's first word (bfi into a \
    byte of the stack, 0x200358c4): the other 25 bits go to the AP as the stack left them (an address of the SE's \
    stack). Ours sends them as 0";

const GROUP_ZERO: &str = "repair group 0: CIX's handlers take the entry before the first of their table of groups \
(s_memr_get_data, 0x200171ce: the index is the group less one, unchecked) and go by what lies there: with repair data in \
the fuses it fails on that entry's length with an error postcode; the open firmware refuses the group";
const HANG_RECORDS: &str = "the hang record's clock and power parts: CIX's walks its monitor's clock tree and power \
    domains (records CLK, 4304 bytes, and POWER, 1088 bytes); ours records the raw PLL and power registers (CLK_RAW \
    and PWR_RAW, 68 bytes each). The AMU record and the records of the three watchdogs are made as CIX's";

const WDT_FRAME: &str = "CIX's SE watchdog record (se_save_einfo) has the interrupted code's frame and the fault registers only \
    when the stack pointer saved at the interrupt is outside the stack (the test at 0x2001560c..0x20015638 is the wrong \
    way round); ours has them when it is inside";
const FAULT_DUMP: &str = "the fault report on the UART is our own (registers of the frame and the fault status registers; \
    CIX's has a call stack and reads DFSR): the postcode, the reset and its reason are the same";

const PM_NOT_REFUSED: &str = "CIX's image starts a CSU_PM that fails its checks under secure boot: where the PM image's \
    digest or signature is found wrong (fw_xloader_load_img, 0x2000556c and 0x20005588: lines 519 and 524 of its \
    source) the error is logged and its postcode set, but not put in the function's result (r4 stays 0, unlike BL2's \
    paths at 0x200056f6 and 0x20005718), so the PM's configuration is loaded and the PM released. Ours stops the boot \
    (error 0x930c, reset with reason 0x547)";

const QSPI_UNINIT: &str = "for a flash part the header's table does not list, on a part with nothing fused (life cycle \
    4), CIX's readers go on with read parameters they never set (hal_stor_load_fw, 0x200189be: 32 bytes of stack, \
    three of them initialised; get_qspi_config leaves the rest alone): here they happen to be refused and the \
    standard read is used. Ours start from cleared parameters: a standard read at the default clock";

const MEMCFG_PAST: &str = "an erased memory configuration (255 blocks, none the one looked for): CIX's lookup takes the \
    block's offset from the word after its table and reads the setup block from there (here: erased flash, every \
    setting 0xff); ours does not read past the table and takes every setting as erased. Both go on with the boot";

const SECTOR_BUFFER: &str = "after DRAM training CIX's firmware writes the memory configuration back using the boot ROM's \
    copy of the bootloader1 header in SRAM as its sector buffer (drv_memcfg_flash_write), so it has to read the header \
    from the flash again before it loads BL2; ours has a buffer of its own. The flash ends up the same";
const DDR_REPORT: &str = "CIX's DDR driver reads registers back for its training report; ours has no report";

const DENSITY_PAST: &str = "a density code no LPDDR5 part has (mode register 8 all ones): CIX's driver reads its \
    density table past its nine entries (whatever follows it); ours has the table padded to sixteen with zeros. The \
    timing registers set from the entry differ";

pub const EXPECTED: &[Expected] = &[
    // (with PI training status bits set CIX's MemDdrssInit reads PI_38 again, 0x20024ff0, for a line of its report)
    Expected { phase: "DDR failure: PI training status bits", what: "reads of 0x4c0", cix: "SmnRegRead32 3", open: "SmnRegRead32 2", why: DDR_REPORT },
    Expected { phase: "DDR part: all ones", what: "register 0x4c0", cix: "", open: "", why: DENSITY_PAST },
    Expected { phase: "DDR part: all ones", what: "memory 0x63c0", cix: "", open: "", why: DENSITY_PAST },
    Expected { phase: "DDR part: all ones", what: "flash 0x4001", cix: "", open: "", why: DENSITY_PAST },
    Expected { phase: "DDR", what: "register 0xe00440", cix: "", open: "", why: SECTOR_BUFFER },
    Expected { phase: "DDR", what: "reads of 0xe00440", cix: "", open: "", why: SECTOR_BUFFER },
    Expected { phase: "DDR", what: "reads of 0xa0030060", cix: "fw_xloader_get_header 1", open: "", why: SECTOR_BUFFER },
    Expected { phase: "DDR", what: "reads of 0xb00300e0", cix: "plat_get_info", open: "plat_get_info", why: SECTOR_BUFFER },
    Expected { phase: "DDR", what: "reads of 0xb0040155", cix: "memcpy", open: "memcpy", why: SECTOR_BUFFER },
    Expected { phase: "DDR", what: "read of 0x4c0", cix: "SmnRegRead32", open: "not read", why: DDR_REPORT },
    Expected { phase: "memory configuration erased", what: "register 0xe00440", cix: "", open: "", why: MEMCFG_PAST },
    Expected { phase: "memory configuration erased", what: "reads of 0xe00440", cix: "", open: "", why: MEMCFG_PAST },
    Expected { phase: "memory configuration erased", what: "reads of 0xb0030004", cix: "", open: "", why: MEMCFG_PAST },
    Expected { phase: "memory configuration erased", what: "reads of 0xb00300e0", cix: "plat_get_info", open: "plat_get_info", why: MEMCFG_PAST },
    Expected { phase: "memory configuration erased", what: "reads of 0xb0040155", cix: "memcpy", open: "memcpy", why: MEMCFG_PAST },
    Expected { phase: "input: flash JEDEC id", what: "reads of 0xb0030004", cix: "plat_get_core_freq", open: "plat_get_core_freq", why: QSPI_UNINIT },
    Expected { phase: "input: flash JEDEC id", what: "reads of 0xe0044004", cix: "drv_qspi_flash_read 45", open: "drv_qspi_flash_read 33", why: QSPI_UNINIT },
    Expected { phase: "secure boot, CSU_PM", what: "end*", cix: "running", open: "system reset, reason 0x547", why: PM_NOT_REFUSED },
    // (with a CIX key the fuses do not know, CIX's image starts the PM all the same and stops at BL2)
    Expected { phase: "secure boot, CIX key hash in the fuses wrong", what: "", cix: "", open: "", why: PM_NOT_REFUSED },
    Expected { phase: "crash: SE watchdog interrupt", what: "read of 0xe000ed28", cix: "not read", open: "rc_se_assert_policy", why: WDT_FRAME },
    Expected { phase: "crash: SE watchdog interrupt", what: "reads of 0xb0030004", cix: "plat_get_core_freq", open: "", why: SEND_WAITS },
    Expected { phase: "crash: SE watchdog interrupt", what: "reads of 0xe000e010", cix: "_hal_delay", open: "", why: SEND_WAITS },
    Expected { phase: "crash: ", what: "read of 0xe000ed3", cix: "", open: "", why: FAULT_DUMP },
    Expected { phase: "8200001a burn the OEM key hash", what: "*", cix: "", open: "c 8200001a ffffffff", why: NO_BURN },
    Expected { phase: "8200001a burn the OEM key hash, CRC right", what: "read of 0xb00", cix: "", open: "not read", why: NO_BURN },
    Expected { phase: "8200001a burn the OEM key hash, no parameters", what: "register 0xa0020f00", cix: "9b80", open: "", why: NO_BURN },
    Expected { phase: "8200001a burn the OEM key hash, no parameters", what: "read of 0xa0020018", cix: "drv_pc_trace", open: "not read", why: NO_BURN },
    Expected { phase: "8200001a burn the OEM key hash, no parameters", what: "event", cix: "error postcode 0x9b80", open: "(none)", why: NO_BURN },
    Expected { phase: "repair group 0, index", what: "register 0xa0020f00", cix: "94", open: "", why: GROUP_ZERO },
    Expected { phase: "repair group 0, index", what: "read of 0xa0020018", cix: "drv_pc_trace", open: "not read", why: GROUP_ZERO },
    Expected { phase: "repair group 0, index", what: "event", cix: "error postcode 0x9400", open: "(none)", why: GROUP_ZERO },
    Expected { phase: "crash: ", what: "read of 0x4", cix: "", open: "", why: HANG_RECORDS },
    Expected { phase: "crash: ", what: "read of 0x5", cix: "", open: "", why: HANG_RECORDS },
    Expected { phase: "crash: ", what: "reads of 0x4", cix: "", open: "", why: HANG_RECORDS },
    Expected { phase: "crash: ", what: "reads of 0x5", cix: "", open: "", why: HANG_RECORDS },
    Expected { phase: "crash: ", what: "order of first reads", cix: "", open: "", why: HANG_RECORDS },
    Expected { phase: "crash: ", what: "register 0x50390f58", cix: "83d0", open: "83d0", why: HANG_RECORDS },
    Expected { phase: "crash: ", what: "register 0x50790f58", cix: "83d0", open: "83d0", why: HANG_RECORDS },
    Expected { phase: "crash: ", what: "register 0x51390f58", cix: "83d0", open: "83d0", why: HANG_RECORDS },
    Expected { phase: "crash: ", what: "register 0x51790f58", cix: "83d0", open: "83d0", why: HANG_RECORDS },
    Expected { phase: "crash: ", what: "memory 0x63d0", cix: "", open: "", why: HANG_RECORDS },
    Expected { phase: "crash: SE watchdog interrupt", what: "register 0xe000e01", cix: "", open: "", why: SEND_WAITS },
    Expected { phase: "", what: "read of 0xa004", cix: "", open: "", why: CE_DRIVER },
    Expected { phase: "", what: "reads of 0xa004", cix: "", open: "", why: CE_DRIVER },
    // the deliberate differences of the run-time interface
    Expected { phase: "REE 82000002 OEM model key", what: "*", cix: "1c 82000002 0 ", open: "c 82000002 fffffffe", why: SECRET_FUSES },
    Expected { phase: "REE 8200000e fuse: ", what: "*", cix: " 8200000e 0 ", open: "c 8200000e fffffffe", why: SECRET_FUSES },
    Expected { phase: "8200000c hash below DRAM", what: "*", cix: "30 8200000c 0 20 ", open: "c 8200000c fffffffd", why: HASH_DRAM },
    Expected { phase: "8200000c hash of the fuse mirror", what: "*", cix: "30 8200000c 0 20 ", open: "c 8200000c fffffffd", why: HASH_DRAM },
    Expected { phase: "8200000c hash, end past the window", what: "*", cix: "30 8200000c 0 20 ", open: "c 8200000c fffffffd", why: HASH_DRAM },
    // faults of CIX's image
    Expected { phase: "8200000c hash, too few parameters", what: "*", cix: "30 8200000c 0 20 ", open: "30 8200000c 0 20 42c4b0e3 141cfc98", why: UNINIT },
    Expected { phase: "message of size 0", what: "*", cix: "9 ", open: "9 0 0", why: UNINIT },
    Expected { phase: "message of size 4", what: "*", cix: "9 ", open: "9 0 0", why: UNINIT },
    Expected { phase: "message of size 0x80", what: "*", cix: "14 ", open: "14 0 0 0 0", why: UNINIT },
    Expected { phase: "82000002 OPN byte 2", what: "*", cix: "d 82000002 0 0", open: "c 82000002 fffffffe", why: OPN_STALE },
    Expected { phase: "82000002 OPN, 9 bytes", what: "*", cix: "d 82000002 0 0", open: "c 82000002 fffffffe", why: OPN_STALE },
    Expected { phase: "82000002 OPN, from 0x4db", what: "*", cix: "d 82000002 0 0", open: "c 82000002 fffffffe", why: OPN_STALE },
    Expected { phase: "82000002 OPN, length wraps", what: "*", cix: "no answer", open: "c 82000002 fffffffe", why: OPN_WRAP },
    Expected { phase: "8200000e fuse, past the end", what: "*", cix: "d 8200000e 0 ", open: "c 8200000e fffffffe", why: FUSE_UNCHECKED },
    Expected { phase: "8200000e fuse, end past the end", what: "*", cix: "e 8200000e 0 ", open: "c 8200000e fffffffe", why: FUSE_UNCHECKED },
    Expected { phase: "8200000e fuse, 0x71 bytes", what: "*", cix: "7d 8200000e 0 ", open: "c 8200000e fffffffe", why: FUSE_UNCHECKED },
    Expected { phase: "8200000e fuse, 0x7c bytes", what: "*", cix: "8 8200000e", open: "c 8200000e fffffffe", why: FUSE_UNCHECKED },
    // (with --audit the interpreter's check of the registers a callee must keep sees the overflow itself:
    // the mailbox handler's saved r4 and r5 come back changed)
    Expected { phase: "8200000e fuse, 0x7c bytes", what: "*", cix: "audit: return to", open: "c 8200000e fffffffe", why: FUSE_UNCHECKED },
    Expected { phase: "8200000e fuse, 0x80 bytes", what: "*", cix: "audit: return to", open: "c 8200000e fffffffe", why: FUSE_UNCHECKED },
    Expected { phase: "8200000e fuse, 0x80 bytes", what: "*", cix: "c 8200000e 0", open: "c 8200000e fffffffe", why: FUSE_UNCHECKED },
    Expected { phase: "8200000e fuse, 0x200 bytes", what: "*", cix: "dead: system reset, reason 0x447", open: "c 8200000e fffffffe", why: FUSE_UNCHECKED },
    Expected { phase: "8200000e fuse, 0x800 bytes", what: "*", cix: "dead: system reset, reason 0x447", open: "c 8200000e fffffffe", why: FUSE_UNCHECKED },
    Expected { phase: "8200000e fuse, 0x801 bytes", what: "*", cix: "dead: system reset, reason 0x447", open: "c 8200000e fffffffe", why: FUSE_UNCHECKED },
    Expected { phase: "8200000e fuse, sizes wrap", what: "*", cix: "d 8200000e 0 0", open: "c 8200000e fffffffe", why: FUSE_UNCHECKED },
    Expected { phase: "8200000e fuse, bit offset 0x400", what: "*", cix: "", open: "c 8200000e fffffffe", why: FUSE_UNCHECKED },
    Expected { phase: "wake-up sources", what: "register 0xa00600ac", cix: "50000001", open: "28 50000001", why: STACK_LEAK },
    Expected { phase: "wake-up sources", what: "event", cix: "50000001", open: "[28, 50000001", why: STACK_LEAK },
    Expected { phase: "wake-up sources", what: "reads of 0x4661008c", cix: "service_str_entry", open: "service_str_entry", why: SEND_WAITS },
    Expected { phase: "wake-up sources", what: "reads of 0xe000e010", cix: "", open: "", why: SEND_WAITS },
    Expected { phase: "wake-up sources", what: "order of first reads", cix: "", open: "", why: SEND_WAITS },
    Expected { phase: "wake-up sources", what: "order of writes", cix: "e000e01", open: "", why: SEND_WAITS },
    Expected { phase: "wake-up sources", what: "read of 0xb0030004", cix: "plat_get_core_freq", open: "not read", why: SEND_WAITS },
    Expected { phase: "wake-up sources", what: "register 0xe000e01", cix: "", open: "", why: SEND_WAITS },
    Expected { phase: "wake-up sources", what: "read of 0xe000e010", cix: "sys_clk_get_systick_clk", open: "not read", why: SEND_WAITS },
    Expected { phase: "wake-up sources", what: "reads of 0xb0030004", cix: "plat_get_core_freq", open: "plat_get_core_freq", why: SEND_WAITS },
];

pub struct Images<'a> {
    pub open: &'a str,
    pub cix: &'a str,
}

fn stubs(cix: bool, cfg: &Config) -> Vec<(&'static str, Stub<SkySoc>)> {
    let mut s = if cfg.ddr { Vec::new() } else { ddr_stubs() };
    if cix {
        s.extend(super::crypto::stubs());
        // CIX's debug build has a UART monitor: 3 s for ESC before the boot ("boot time config"), and after
        // it the monitor's prompt, where the SE stays until it is asked to suspend.  The release build has
        // neither (nor has the open firmware): taken out, so that this image goes on into the idle loop.
        s.push(("service_monitor_entry", super::emu::constant(0)));
    }
    for (name, value) in &cfg.constants {
        s.retain(|(n, _)| n != name);
        s.push((name, super::emu::constant(*value)));
    }
    s
}

/// Both images from reset to the idle loop: the phase "boot", and "idle" (the loop's first 2 ms).
pub fn boot(im: &Images, cfg: &Config, name: &str) -> Result<Pair, String> {
    let a = Sim::new(im.cix, true, cfg, &stubs(true, cfg))?;
    let b = Sim::new(im.open, false, cfg, &stubs(false, cfg))?;
    let mut q = Pair::new(a, b);
    if std::env::var("SKY1_CALLS").is_ok() {
        q.each(|s| s.cpu.call_counts = Some(Default::default()));
    }
    // to the debug service (the AP is released by then), then on into the idle loop: CIX's debug service
    // waits 3 s for ESC on the UART (its monitor), the open one has none
    for (stop, limit, phase, memory) in [("service_debug_entry", 30 * SEC, "boot", true), ("service_idle_entry", 5 * SEC, "debug service", false)] {
        q.each(|s| {
            if let Some(addr) = s.cpu.sym(stop) {
                s.cpu.set_stop(addr, stop);
            }
            s.mark_if_new();
            let end = s.time() + limit;
            s.run_to_stop(stop, end);
            if let Some(addr) = s.cpu.sym(stop) {
                s.cpu.del_stop(addr);
            }
        });
        q.phase(&format!("{name}{phase}"), memory);
    }
    if let Ok(want) = std::env::var("SKY1_CALLS") {
        // a diagnostic: how often each function was called, and from where, where the two images differ
        let count = |s: &Sim| {
            let mut m: BTreeMap<String, BTreeMap<String, u64>> = BTreeMap::new();
            for ((to, from), n) in s.cpu.call_counts.as_ref().unwrap() {
                *m.entry(s.cpu.func(*to)).or_default().entry(s.cpu.func(*from)).or_default() += n;
            }
            m
        };
        let (ca, cb) = (count(&q.a), count(&q.b));
        if !want.is_empty() {
            for (who, c) in [("CIX", &ca), ("open", &cb)] {
                for (f, from) in c.iter().filter(|(f, _)| want.split(',').any(|w| f.contains(w))) {
                    println!("{who}: {f} {} {from:?}", from.values().sum::<u64>());
                }
            }
        }
        for (f, from) in &ca {
            if let Some(other) = cb.get(f) {
                let (na, nb): (u64, u64) = (from.values().sum(), other.values().sum());
                if na != nb && (want.is_empty() || f.contains(&want)) {
                    println!("calls of {f}: CIX {na} {from:?} | open {nb} {other:?}");
                }
            }
        }
    }
    q.sync();
    q.settle(2 * MS);
    q.phase(&format!("{name}idle"), false);
    Ok(q)
}

/// Suspend to RAM and back: the AP's request, the idle loop's way to WFI, the PM's wake-up interrupt,
/// the resume, and what the PM then asks for: the sensor hub started (`magic` 0xaacc), stopped (0xaabb)
/// or nothing (None: the SE gives up after 5 s).  `source` is the wake-up source the S5 block reports.
pub fn suspend(q: &mut Pair, name: &str, magic: Option<u32>, source: u32) {
    q.ask(&format!("{name}: suspend request"), 0, 0x82000007, &[0]);
    q.phase(&format!("{name}: suspend request"), false);
    q.until(500 * MS, |s| s.asleep());
    q.phase(&format!("{name}: to sleep"), true);
    q.each(|s| {
        let now = s.time();
        let soc = s.soc();
        soc.t.pm_resume_magic = magic;
        soc.t.s5.insert(0x56000210, source);
        // (GPIO banks' masks and states, and the USB wake-up type: read by the source they belong to)
        for i in 0..3u32 {
            soc.t.s5.insert(0x56004014 + i * 0x1000, 0xffff0000 | i);
            soc.t.s5.insert(0x56004020 + i * 0x1000, 0x00010100 << i);
        }
        soc.t.s5.insert(0x56000214, 0x5a);
        soc.at(now + 1 * MS, Action::Line(IRQ_SE_INTR, true));
    });
    // the SE's last word of a resume: its answer in the handshake register, or its message that it gave up
    q.until(6 * SEC, |s| s.cpu.bus.t.log.last().map_or(false, |l| l.1.starts_with("SE to PM, handshake")) || String::from_utf8_lossy(&s.cpu.bus.t.uart).ends_with("Wait for STR magic failed \r\n"));
    q.settle(1 * MS);
    if magic == Some(0xaacc) {
        // what the SE put in the sensor hub's SRAM: the flash's image (firmware table entry 8), and it starts
        q.note("the sensor hub's image as loaded", |s| {
            let flash = &s.cpu.bus.flash;
            let table = 0x100000;
            let entry = (0..crate::util::u32le(flash, table + 8) as usize).map(|i| table + 16 + 16 * i).find(|e| crate::util::u32le(flash, *e) == 8);
            let Some(e) = entry else { return "no image in the flash".into() };
            let (at, size) = (crate::util::u32le(flash, e + 4) as usize, crate::util::u32le(flash, e + 8));
            let loaded = s.cpu.mem.key_bytes(SOC_KEY | SFH_SOC, size);
            let same = loaded[..] == flash[at..at + size as usize];
            #[cfg(feature = "sfh")]
            let starts = crate::sfh::test::starts(&loaded);
            #[cfg(not(feature = "sfh"))]
            let starts = "(not started: built without the SFH's model)".to_string();
            format!("{size} bytes at the start of its SRAM, {}; started there: {starts}", if same { "as in the flash" } else { "NOT as in the flash" })
        });
    }
    q.phase(&format!("{name}: wake and resume"), true);
    if source & 1 == 0 {
        // the PM turns the RAM log back on
        q.each(|s| s.cpu.mem.write(0x4661008c, 4, 0xaa55));
        q.settle(3 * MS);
        q.phase(&format!("{name}: RAM log on"), false);
    }
    q.ask(&format!("{name}: wake-up sources"), 0, 0x82000016, &[]);
    q.phase(&format!("{name}: wake-up sources"), false);
    q.ask(&format!("{name}: firmware version"), 2, 0x8200000f, &[0]);
    q.phase(&format!("{name}: firmware version"), false);
}

/// Every request of the run-time interface, on the non-secure mailbox and on the TEE's, with valid,
/// boundary and invalid arguments: a phase each.
pub fn services(q: &mut Pair) {
    for (chan, who) in [(0usize, "REE"), (2, "TEE")] {
        let ask = |q: &mut Pair, name: &str, fid: u32, words: &[u32]| {
            let name = format!("{who} {fid:08x} {name}");
            q.ask(&name, chan, fid, words);
            q.phase(&name, false);
        };
        // (requests that damage CIX's image without ending it: both are put back afterwards)
        let scratch = |q: &mut Pair, name: &str, fid: u32, words: &[u32]| {
            let name = format!("{who} {fid:08x} {name}");
            q.ask_scratch(&name, chan, fid, words);
            q.phase(&name, false);
        };
        ask(q, "echo", 0x82000001, &[]);
        ask(q, "echo, parameters", 0x82000001, &[1, 2, 3]);
        ask(q, "echo, 30 parameter words", 0x82000001, &(100..129).collect::<Vec<u32>>());
        ask(q, "serial number and chip ID", 0x82000002, &[0, 0x240, 0]);
        ask(q, "OPN fuses", 0x82000002, &[1, 0x4dc, 8]);
        ask(q, "OPN byte 1", 0x82000002, &[1, 1, 1]);
        ask(q, "OPN byte 2 (past the end)", 0x82000002, &[1, 2, 1]);
        ask(q, "OPN, 9 bytes (past the end)", 0x82000002, &[1, 0x4dc, 9]);
        ask(q, "OPN, from 0x4db (before the start)", 0x82000002, &[1, 0x4db, 1]);
        ask(q, "OPN, last byte", 0x82000002, &[1, 0x4e3, 1]);
        ask(q, "OPN, no bytes", 0x82000002, &[1, 0x4dc, 0]);
        scratch(q, "OPN, length wraps", 0x82000002, &[1, 0x4dc, 0xfffffffc]);
        ask(q, "OEM model key", 0x82000002, &[0, 0xb40, 0]);
        ask(q, "fuse id unknown", 0x82000002, &[0, 0x123, 0]);
        ask(q, "fuse id, no parameters", 0x82000002, &[]);
        ask(q, "set power", 0x82000005, &[1]);
        ask(q, "set power, no parameters", 0x82000005, &[]);
        for g in [0u32, 1, 2, 3, 4, 8, 10, 12, 16, 19, 20, 21, 22, 0x103, 0xffffffff] {
            ask(q, &format!("memory repair group {g}"), 0x82000006, &[g, 0]);
        }
        ask(q, "hash SHA-256 of DRAM", 0x8200000c, &[0, 0x80000000, 0x1000, 0, 0]);
        ask(q, "hash SHA-384", 0x8200000c, &[0, 0x80000000, 0x1000, 1, 0]);
        ask(q, "hash SHA-512", 0x8200000c, &[0, 0x80000000, 0x1000, 2, 0]);
        ask(q, "hash SM3", 0x8200000c, &[0, 0x80000000, 0x1000, 3, 0]);
        ask(q, "hash, mode unknown", 0x8200000c, &[0, 0x80000000, 0x1000, 4, 0]);
        ask(q, "hash, no bytes", 0x8200000c, &[0, 0x80000000, 0, 0, 0]);
        ask(q, "hash, one byte", 0x8200000c, &[0, 0x80000040, 1, 0, 0]);
        ask(q, "hash, 100000 bytes", 0x8200000c, &[0, 0x80000000, 100000, 0, 0]);
        ask(q, "hash, engine index 1", 0x8200000c, &[1, 0x80000000, 0x100, 0, 0]);
        ask(q, "hash, last word not 0", 0x8200000c, &[0, 0x80000000, 0x100, 0, 1]);
        ask(q, "hash below DRAM (the SE's own SRAM)", 0x8200000c, &[0, 0x40000000, 0x100, 0, 0]);
        ask(q, "hash of the fuse mirror's address", 0x8200000c, &[0, 0xd0040000, 0x100, 0, 0]);
        ask(q, "hash, end past the window", 0x8200000c, &[0, 0xbffffff0, 0x20, 0, 0]);
        ask(q, "hash, too few parameters", 0x8200000c, &[0, 0x80000000]);
        ask(q, "fuse field", 0x8200000e, &[0x155, 0, 8]);
        ask(q, "fuse word 0", 0x8200000e, &[0, 0, 32]);
        ask(q, "fuse, last byte", 0x8200000e, &[0x7ff, 0, 8]);
        ask(q, "fuse, past the end", 0x8200000e, &[0x800, 0, 8]);
        ask(q, "fuse, end past the end", 0x8200000e, &[0x7ff, 0, 9]);
        ask(q, "fuse, 0x70 bytes", 0x8200000e, &[0x200, 0, 0x380]);
        ask(q, "fuse, 0x71 bytes (too many)", 0x8200000e, &[0x200, 0, 0x381]);
        scratch(q, "fuse, 0x7c bytes (too many)", 0x8200000e, &[0x200, 0, 0x3e0]);
        scratch(q, "fuse, 0x80 bytes (too many)", 0x8200000e, &[0x200, 0, 0x400]);
        scratch(q, "fuse, 0x200 bytes (too many)", 0x8200000e, &[0x200, 0, 0x1000]);
        scratch(q, "fuse, 0x800 bytes (too many)", 0x8200000e, &[0, 0, 0x4000]);
        scratch(q, "fuse, 0x801 bytes (too many)", 0x8200000e, &[0, 0, 0x4008]);
        ask(q, "fuse, bit offset 3, 7 bits", 0x8200000e, &[0x154, 3, 7]);
        ask(q, "fuse, no bits", 0x8200000e, &[0x154, 0, 0]);
        scratch(q, "fuse, sizes wrap", 0x8200000e, &[0x154, 0xfffffff9, 8]);
        scratch(q, "fuse, bit offset 0x400", 0x8200000e, &[0x154, 0x400, 8]);
        ask(q, "fuse: CIX model key", 0x8200000e, &[0x54, 0, 128]);
        ask(q, "fuse: CIX device key", 0x8200000e, &[0x64, 0, 128]);
        ask(q, "fuse: test model key", 0x8200000e, &[0xb4, 0, 128]);
        ask(q, "fuse: test device key", 0x8200000e, &[0xc4, 0, 128]);
        ask(q, "fuse: OEM model key", 0x8200000e, &[0x168, 0, 128]);
        ask(q, "fuse: byte before the keys", 0x8200000e, &[0x53, 0, 8]);
        ask(q, "fuse: range reaching into the keys", 0x8200000e, &[0x53, 0, 16]);
        ask(q, "fuse: byte after the keys", 0x8200000e, &[0x74, 0, 8]);
        ask(q, "fuse: last key byte", 0x8200000e, &[0x73, 0, 8]);
        ask(q, "firmware version", 0x8200000f, &[0]);
        ask(q, "firmware version, no parameters", 0x8200000f, &[]);
        for n in [0u32, 1, 16, 63, 64, 65, 0xffffffff] {
            ask(q, &format!("{n} random bytes"), 0x82000010, &[n]);
        }
        ask(q, "fence the FCH (id 1)", 0x82000012, &[1]);
        ask(q, "fence, id 2", 0x82000012, &[2]);
        ask(q, "fence, id 0x11", 0x82000012, &[0x11]);
        ask(q, "idle exit", 0x82000014, &[]);
        ask(q, "exception area, magic wrong", 0x82000015, &[0, 0x83d00000, 0x12345678]);
        ask(q, "exception area, too short", 0x82000015, &[0, 0x83d00000]);
        ask(q, "hang record, no area", 0x82000017, &[]);
        ask(q, "wake-up sources", 0x82000016, &[]);
        ask(q, "clock auto gating on", 0x82000018, &[]);
        ask(q, "clock auto gating off", 0x82000019, &[]);
        ask(q, "OEM key hash state", 0x8200001b, &[0]);
        ask(q, "MTE setting", 0x82000020, &[0]);
        ask(q, "MTE, operation unknown", 0x82000020, &[0x0200]);
        for fid in [0x82000000u32, 0x82000003, 0x82000004, 0x82000008, 0x82000009, 0x8200000a, 0x8200000b, 0x8200000d, 0x8200001c, 0x8200001f, 0x82000021, 0x82000099, 0, 0xffffffff, 0x02000001, 0xc2000001] {
            ask(q, "not a function", fid, &[0]);
        }
        // messages whose size is not what a request has
        for (name, msg) in [
            ("size 0", vec![0u32, 0x8200000f]),
            ("size 4", vec![4, 0x8200000f]),
            ("size 7", vec![7, 0x8200000f, 0]),
            ("size 9", vec![9, 0x8200000f, 0]),
            ("size 0x7f", vec![0x7f, 0x8200000f, 0]),
            ("size 0x80 (bit 7: not the size)", vec![0x80, 0x8200000f, 0]),
            ("size 12, type bits set", vec![0x38c, 0x8200000f, 0]),
        ] {
            let name = format!("{who} message of {name}");
            q.ask_raw(&name, chan, &msg);
            q.phase(&name, false);
        }
    }
}

pub fn main(a: &Args, open_elf: &str, cix_elf: &str, flash: &[u8]) -> Result<i32, String> {
    let im = Images { open: open_elf, cix: cix_elf };
    let mut cfg = Config::new(flash);
    cfg.life_cycle = a.int("--life-cycle").unwrap_or(4) as u32;
    cfg.ddr = a.flag("--ddr");
    cfg.audit = a.flag("--audit");
    cfg.features = parse_features(&a.all("--ddr-feature"))?;
    if a.flag("--keys") {
        cfg.efuse = keys_efuse(flash, false);
    }
    let verbose = a.flag("-v");
    if let Some(only) = a.opt("--only") {
        let _ = ONLY.set(only.to_string());
    }
    let mut used = BTreeMap::new();
    let mut bad = 0;
    let q = boot(&im, &cfg, if cfg.ddr { "DDR: " } else { "" })?;
    if verbose {
        for (pa, pb) in &q.phases {
            println!("--- {}: CIX {} ms, open {} ms", pa.name, pa.time / MS, pb.time / MS);
        }
    }
    let mut q = q;
    let base = (q.a.clone(), q.b.clone());
    if a.flag("--services") || a.flag("--full") {
        services(&mut q);
        q.reset(&base);
        services_scratch(&mut q);
        q.reset(&base);
    }
    if a.flag("--crash") || a.flag("--full") {
        crash(&mut q);
        q.reset(&base);
    }
    if a.flag("--suspend") || a.flag("--full") {
        suspend(&mut q, "suspend 1 (sensor hub stays off)", Some(0xaabb), 1);
        suspend(&mut q, "suspend 2 (sensor hub started)", Some(0xaacc), 2);
        suspend(&mut q, "suspend 3 (sensor hub stopped)", Some(0xaabb), 4);
        suspend(&mut q, "suspend 4 (sensor hub started again)", Some(0xaacc), 0);
        suspend(&mut q, "suspend 5 (the PM says nothing)", None, 6);
    }
    if a.flag("--secure") || a.flag("--full") {
        q.phases.extend(secure(&im, flash, verbose)?);
    }
    if a.flag("--ddr-suite") || a.flag("--full") {
        q.phases.extend(ddr(&im, &cfg, verbose, a.flag("--eye"))?);
    }
    if a.flag("--inputs") || a.flag("--full") {
        q.phases.extend(inputs(&im, flash, &cfg, verbose)?);
    }
    if a.flag("--paths") || a.flag("--full") {
        q.phases.extend(paths(&im, flash, &cfg, verbose)?);
    }
    if a.flag("--failures") || a.flag("--full") {
        q.phases.extend(failures(&im, &cfg, verbose)?);
    }
    if let Some(only) = a.opt("--only") {
        q.phases.retain(|p| p.0.name.contains(only));
    }
    for which in a.all("--uart") {
        // the log of the phases whose name has the text ("cix:TEXT": CIX's)
        let (cix, text) = match which.strip_prefix("cix:") {
            Some(t) => (true, t),
            None => (false, which.as_str()),
        };
        for p in q.phases.iter().filter(|p| p.0.name.contains(text)) {
            println!("--- {} ({})\n{}", p.0.name, if cix { "CIX" } else { "open" }, if cix { &p.0.uart } else { &p.1.uart });
        }
    }
    bad += report(&q.phases, EXPECTED, verbose, &mut used);
    if let Some(path) = a.opt("--cov") {
        let cov = q.coverage();
        // CIX's image has the open one's file name: kept apart
        crate::cov::save(path, &format!("{}.cix", q.a.cpu.name()), &cov[0])?;
        crate::cov::save(path, &q.b.cpu.name(), &cov[1])?;
    }
    let n: usize = used.values().sum();
    println!("{bad} differences, {n} expected in {} kinds", used.len());
    for (why, n) in &used {
        println!("  {n} x {why}");
    }
    Ok((bad != 0) as i32)
}

/// Requests that change what the SE does afterwards, each from a copy that is thrown away: the idle
/// request, the DDR interrupts' release, the OEM key hash burn, the MTE setting stored in flash.
pub fn services_scratch(q: &mut Pair) {
    let one = |q: &mut Pair, name: &str, chan: usize, fid: u32, words: &[u32], memory: bool| {
        let name = format!("{} {fid:08x} {name}", if chan == 0 { "REE" } else { "TEE" });
        q.ask_scratch(&name, chan, fid, words);
        q.settle(2 * MS);
        q.phase(&name, memory);
    };
    for chan in [0usize, 2] {
        one(q, "idle enter", chan, 0x82000013, &[], false);
        one(q, "DDR interrupts released", chan, 0x82000011, &[], false);
        one(q, "burn the OEM key hash", chan, 0x8200001a, &[0x11111111, 0x22222222, 0x33333333, 0x44444444, 0x55555555, 0x66666666, 0x77777777, 0x88888888], true);
        // (the hash, then its CRC-16: as CIX's handler wants it)
        one(q, "burn the OEM key hash, CRC right", chan, 0x8200001a, &[0x11111111, 0x22222222, 0x33333333, 0x44444444, 0x55555555, 0x66666666, 0x77777777, 0x88888888, 0xf05a], true);
        one(q, "burn the OEM key hash, no parameters", chan, 0x8200001a, &[], true);
        one(q, "MTE on, stored", chan, 0x82000020, &[0x0101], true);
        one(q, "MTE off, stored", chan, 0x82000020, &[0x0100], true);
    }
}

/// Crash records and watchdogs: the exception area the AP registers, the hang record on request, the SE's,
/// the PM's and the AP's watchdog interrupts, faults and the exceptions nothing should raise.
pub fn crash(q: &mut Pair) {
    // the AP registers its exception area (at 0x83d00000: the window's 0x63d00000); a hang record on request
    q.ask("exception area", 0, 0x82000015, &[0, 0x83d00000, 0xbab1face]);
    q.phase("crash: exception area registered", false);
    q.ask("hang record", 0, 0x82000017, &[]);
    q.phase("crash: hang record on request", true);
    if std::env::var("SKY1_RC").is_ok() {
        // a diagnostic: the records of the exception area in each image
        for s in [&q.a, &q.b] {
            let m = &s.cpu.mem;
            println!("{}: state {} count {}", s.cpu.name(), m.read32(0x63d00000), m.read32(0x63d00004));
            for i in 0..m.read32(0x63d00004).min(32) {
                let e = 0x63d00008 + 24 * i;
                let name = String::from_utf8_lossy(&m.bytes(e, 16)).trim_end_matches('\0').to_string();
                println!("  {name:16} offset {:#x} size {:#x}", m.read32(e + 16), m.read32(e + 20));
            }
        }
    }
    q.ask("hang record again", 2, 0x82000017, &[]);
    q.phase("crash: hang record a second time", true);
    q.save();
    // the SE's watchdog runs out once (as if the idle loop had not fed it for 10 s): its interrupt
    let wdt = |q: &mut Pair, name: &str, irq: u32| {
        q.each(|s| {
            let now = s.time();
            s.soc().at(now + 100 * US, Action::Line(irq, true));
        });
        q.until(400 * MS, |s| s.dead.is_some() || s.sent() > 0 && !s.cpu.in_handler());
        q.settle(1 * MS);
        q.phase(name, true);
        q.restore();
        q.each(|s| s.mark());
    };
    wdt(q, "crash: SE watchdog interrupt", IRQ_WDT);
    wdt(q, "crash: PM watchdog interrupt", IRQ_PM_WDT);
    wdt(q, "crash: AP watchdog interrupt", IRQ_AP_WDT);
    // the watchdog for real: the idle loop is kept from feeding it (the feed's register write is lost).  The
    // watchdog's clock is made 200 times faster for it (its 10 s become 50 ms), and it is started again
    q.each(|s| {
        let cpu = &mut s.cpu;
        cpu.bus.t.wdt_tick = REF_TICK / 200;
        let load = cpu.mem.read32(WDT);
        cpu.bus.wdt_restart(&cpu.io, load);
        cpu.bus.t.wdt_stuck = true;
    });
    q.until(300 * MS, |s| s.dead.is_some());
    q.phase("crash: SE watchdog not fed: its interrupt, then the reset", true);
    q.restore();
    q.each(|s| s.mark());
    // exceptions nothing should raise, and a bus fault in a service
    for (name, n) in [("NMI", 2u32), ("MemManage", 4), ("BusFault", 5), ("UsageFault", 6), ("SecureFault", 7), ("SVCall", 11), ("DebugMonitor", 12), ("PendSV", 14), ("SysTick", 15)] {
        q.each(|s| s.cpu.pend(n));
        q.until(50 * MS, |s| s.dead.is_some() || s.cpu.sleeping().is_some());
        q.phase(&format!("crash: {name} exception"), false);
        q.restore();
        q.each(|s| s.mark());
    }
    // the access-violation interrupts of the RCSUs (SE, PM, DP0..4, FCH, GPU, NPU), pended once each
    for irq in [31u32, 66, 77, 78, 79, 80, 81, 88, 90, 99] {
        q.each(|s| s.cpu.pend(16 + irq));
        q.until(50 * MS, |s| s.dead.is_some() || s.cpu.sleeping().is_some() && !s.cpu.in_handler());
        q.settle(1 * MS);
        q.phase(&format!("crash: RCSU violation interrupt {irq}"), false);
        q.restore();
        q.each(|s| s.mark());
    }
    q.each(|s| s.cpu.mem.faults.push((0x4fc50270, 0x4fc50274)));
    q.ask("bus fault", 0, 0x82000018, &[]);
    q.phase("crash: bus fault in a service (clock gating register)", false);
    q.restore();
    q.each(|s| s.mark());
}

/// Secure boot: life cycle 7 with the secure-boot fuse and the hashes of the image's own keys, and what
/// must then stop the boot: an altered image, an altered header, a key the fuses do not know.  Each case
/// is a boot of both images; the AP's boot core is powered (write of 8 to 0x4fa80000) or it is not.
pub fn secure(im: &Images, flash: &[u8], verbose: bool) -> Result<Vec<(Phase, Phase)>, String> {
    let hdr = &flash[BL1_HEADER..BL1_HEADER + 0x1000];
    let image = |id: u32| -> (usize, usize) {
        let n = crate::util::u32le(hdr, 0x9e8) as usize;
        (0..n)
            .map(|i| 0x9ec + 24 * i)
            .find(|e| crate::util::u32le(hdr, *e) == id)
            .map(|e| (BL1_HEADER + crate::util::u32le(hdr, e + 12) as usize, crate::util::u32le(hdr, e + 16) as usize))
            .unwrap_or((0, 0))
    };
    let (pm, pm_len) = image(2);
    let (bl2, bl2_len) = image(3);
    let pad = |len: usize| (len + 0x200 + 0xfff) & !0xfff;
    // (name, life cycle, bytes of the flash to flip, fuse bytes to flip, the header copy in SRAM to flip)
    type Case = (&'static str, u32, Vec<usize>, Vec<(u32, usize)>, Vec<usize>);
    let cases: Vec<Case> = vec![
        ("as signed", 7, vec![], vec![], vec![]),
        ("BL2 altered", 7, vec![bl2 + 0x100], vec![], vec![]),
        ("BL2's last byte altered", 7, vec![bl2 + bl2_len - 1], vec![], vec![]),
        ("BL2's padding altered", 7, vec![bl2 + bl2_len + 1], vec![], vec![]),
        ("BL2's signature altered", 7, vec![bl2 + pad(bl2_len) - 0x1f0], vec![], vec![]),
        ("CSU_PM altered", 7, vec![pm + 0x1000], vec![], vec![]),
        ("CSU_PM's header altered", 7, vec![pm + 0x14], vec![], vec![]),
        ("CSU_PM's signature altered", 7, vec![pm + pad(pm_len) - 0x1f0], vec![], vec![]),
        ("header altered (an image's length)", 7, vec![BL1_HEADER + 0x9ec + 24 * 2 + 16], vec![], vec![]),
        ("header's signature altered", 7, vec![BL1_HEADER + 0xe10], vec![], vec![]),
        ("header's digest of BL2 altered", 7, vec![BL1_HEADER + 0xb74 + 3 * 72 + 8], vec![], vec![]),
        ("header's copy in SRAM altered, flash good", 7, vec![], vec![], vec![0xe10]),
        ("OEM key hash in the fuses wrong", 7, vec![], vec![(0x188, 0)], vec![]),
        ("CIX key hash in the fuses wrong", 7, vec![], vec![(0x74, 31)], vec![]),
        ("OEM key in the header altered", 7, vec![BL1_HEADER + 0x1c + 5], vec![], vec![]),
        ("CIX key in the header altered", 7, vec![BL1_HEADER + 0x324 + 5], vec![], vec![]),
        ("life cycle 5, secure-boot fuse set", 5, vec![], vec![], vec![]),
        ("life cycle 6, secure-boot fuse set", 6, vec![], vec![], vec![]),
        ("life cycle 5, BL2 altered", 5, vec![bl2 + 0x100], vec![], vec![]),
        ("life cycle 3 (field return), BL2 altered", 3, vec![bl2 + 0x100], vec![], vec![]),
    ];
    let mut phases = Vec::new();
    for (name, lc, flips, fuses, sram) in cases {
        let mut f = flash.to_vec();
        for o in &flips {
            f[*o] ^= 1;
        }
        let mut cfg = Config::new(&f);
        cfg.life_cycle = lc;
        cfg.efuse = keys_efuse(flash, true);
        for (a, k) in &fuses {
            cfg.efuse.get_mut(a).unwrap()[*k] ^= 1;
        }
        if !sram.is_empty() {
            let mut h = flash[BL1_HEADER..BL1_HEADER + 0x1000].to_vec();
            for o in &sram {
                h[*o] ^= 1;
            }
            cfg.preload.push((BL1_HEADER_SRAM, h));
        }
        let mut q = boot(im, &cfg, &format!("secure boot, {name}: "))?;
        if verbose {
            for (who, p) in [("CIX", &q.phases[0].0), ("open", &q.phases[0].1)] {
                let started = p.writes.contains(&(0x4fa80000, 8));
                println!("--- secure boot, {name}: {who}: AP {}, {}", if started { "started" } else { "not started" }, p.end);
            }
        }
        phases.extend(std::mem::take(&mut q.phases).into_iter().take(1));
    }
    Ok(phases)
}

/// --only TEXT: the cases whose phases can have the text are the only ones booted.
static ONLY: std::sync::OnceLock<String> = std::sync::OnceLock::new();

fn wanted(name: &str) -> bool {
    ONLY.get().map(|only| name.contains(only.as_str()) || only.contains(name)).unwrap_or(true)
}

fn tweak(cfg: &mut Config, f: impl Fn(&mut SkySoc) + 'static) {
    cfg.tweak = Some(std::rc::Rc::new(f));
}

/// Boot each configuration in both images and keep its "boot" phase.
fn boots(im: &Images, cases: Vec<(String, Config)>, verbose: bool) -> Result<Vec<(Phase, Phase)>, String> {
    let mut phases = Vec::new();
    for (name, cfg) in cases {
        if !wanted(&name) {
            continue;
        }
        if std::env::var("SKY1_PROGRESS").is_ok() {
            eprintln!("{name}");
        }
        let mut q = boot(im, &cfg, &format!("{name}: "))?;
        if verbose {
            let p = &q.phases[0];
            println!("--- {name}: CIX {} after {} ms, open {} after {} ms", p.0.end, p.0.time / MS, p.1.end, p.1.time / MS);
        }
        phases.extend(std::mem::take(&mut q.phases).into_iter().take(1));
    }
    Ok(phases)
}

/// Other inputs of a boot: boot reasons, life cycles, flash parts of the header's table, what the boot ROM
/// left, damaged pieces of the flash image.
pub fn inputs(im: &Images, flash: &[u8], base: &Config, verbose: bool) -> Result<Vec<(Phase, Phase)>, String> {
    let mut cases: Vec<(String, Config)> = Vec::new();
    for reason in 0..11u32 {
        let mut c = base.clone();
        c.boot_reason = reason;
        cases.push((format!("input: boot reason {reason}"), c));
    }
    for lc in 0..8u32 {
        for keys in [false, true] {
            let mut c = base.clone();
            c.life_cycle = lc;
            if keys {
                c.efuse = keys_efuse(flash, false);
            }
            cases.push((format!("input: life cycle {lc}{}", if keys { ", device and model keys fused" } else { "" }), c));
        }
    }
    // the flash parts the header knows, and some it does not
    let hdr = &flash[BL1_HEADER..BL1_HEADER + 0x1000];
    let mut ids: Vec<[u8; 3]> = (0..crate::util::u32le(hdr, 0x800).min(30) as usize)
        .map(|i| {
            let id = crate::util::u32le(hdr, 0x804 + 16 * i);
            [(id >> 16) as u8, (id >> 8) as u8, id as u8]
        })
        .collect();
    ids.extend([[0, 0, 0], [0xff, 0xff, 0xff], [0xc2, 0x20, 0x18], [0xef, 0x40, 0x18]]);
    for id in ids {
        let mut c = base.clone();
        c.jedec = id;
        cases.push((format!("input: flash JEDEC id {:02x} {:02x} {:02x}", id[0], id[1], id[2]), c));
    }
    for (name, div, core) in [("the SE's clock at 200 MHz", 3u32, false), ("the SE's clock at 50 MHz", 15, false), ("SysTick on the core's clock", 0, true), ("SysTick on the core's clock at 100 MHz", 7, true)] {
        let mut c = base.clone();
        c.clkdiv = div;
        c.systick_core = core;
        cases.push((format!("input: {name}"), c));
    }
    // damaged pieces of the image: (name, offset in the flash, bytes written there); the boot ROM's copy of
    // the header follows the flash unless said
    let table = 0x100000usize;
    let entry = |ty: u32| -> usize {
        (0..crate::util::u32le(flash, table + 8) as usize)
            .map(|i| table + 16 + 16 * i)
            .find(|e| crate::util::u32le(flash, *e) == ty)
            .unwrap_or(table + 16)
    };
    let at = |e: usize| crate::util::u32le(flash, e + 4) as usize;
    let damage: Vec<(&str, usize, Vec<u8>)> = vec![
        ("bootloader1 header's magic", BL1_HEADER, b"XIXBTFF!".to_vec()),
        ("bootloader1 header: no images", BL1_HEADER + 0x9e8, vec![0, 0, 0, 0]),
        ("bootloader1 header: 17 images", BL1_HEADER + 0x9e8, vec![17, 0, 0, 0]),
        ("bootloader1 header: the PM's image id gone", BL1_HEADER + 0x9ec + 24, vec![9, 0, 0, 0]),
        ("bootloader1 header: BL2's image id gone", BL1_HEADER + 0x9ec + 48, vec![9, 0, 0, 0]),
        ("bootloader1 header: BL2's length 0", BL1_HEADER + 0x9ec + 48 + 16, vec![0, 0, 0, 0]),
        ("the PM image's magic", BL1_HEADER + crate::util::u32le(hdr, 0x9ec + 24 + 12) as usize, b"YBIN".to_vec()),
        ("the PM image: a section past its end", BL1_HEADER + crate::util::u32le(hdr, 0x9ec + 24 + 12) as usize + 0x10, vec![0xff, 0xff, 0xff, 0x7f]),
        ("the PM image: a section outside the PM's SRAM", BL1_HEADER + crate::util::u32le(hdr, 0x9ec + 24 + 12) as usize + 0x14, vec![0, 0, 0, 0x10]),
        ("firmware table's magic", table, vec![0, 0, 0, 0]),
        ("firmware table: no entries", table + 8, vec![0, 0, 0, 0]),
        ("firmware table: 1000 entries", table + 8, vec![0xe8, 3, 0, 0]),
        ("firmware table: no pm_config", entry(4), vec![0x44, 0, 0, 0]),
        ("firmware table: no memory configuration", entry(3), vec![0x33, 0, 0, 0]),
        ("firmware table: no SE configuration", entry(6), vec![0x66, 0, 0, 0]),
        ("firmware table: no SFH image", entry(8), vec![0x88, 0, 0, 0]),
        ("firmware table: pm_config of 16 MiB", entry(4) + 8, vec![0, 0, 0, 1]),
        ("memory configuration's first byte", at(entry(3)), vec![0xff]),
        ("memory configuration's checksum (a byte of its data)", at(entry(3)) + 0x40, vec![0xa5]),
        ("memory configuration erased", at(entry(3)), vec![0xff; 0x200]),
        ("SE configuration's magic", at(entry(6)), b"XIXSECONFIG".to_vec()),
        ("SE configuration erased", at(entry(6)), vec![0xff; 0x100]),
        ("pm_config erased", at(entry(4)), vec![0xff; 0x1000]),
        ("secure debug certificate present, other chip", 0x107000, b"CIXSECDEBUG".to_vec()),
    ];
    for (name, off, bytes) in damage {
        let mut f = flash.to_vec();
        f[off..off + bytes.len()].copy_from_slice(&bytes);
        let mut c = base.clone();
        c.flash = std::rc::Rc::new(f);
        cases.push((format!("input: damaged: {name}"), c));
    }
    // the header's copy in SRAM damaged, the flash good: read again from the flash
    let mut c = base.clone();
    c.preload.push((BL1_HEADER_SRAM, b"XIXBTFF!".to_vec()));
    cases.push(("input: the boot ROM's copy of the header damaged, the flash good".into(), c));
    let mut c = base.clone();
    tweak(&mut c, |soc| {
        soc.t.s5.insert(0xa0030060, 1);
    });
    cases.push(("input: strapped for the second copy of the flash".into(), c));
    boots(im, cases, verbose)
}

/// The code no board's image takes: the flash behind the FCH's controller, trims and repair data in the
/// fuses, a debug certificate, two DDR channels, the emulation platform, MTE on.
pub fn paths(im: &Images, flash: &[u8], base: &Config, verbose: bool) -> Result<Vec<(Phase, Phase)>, String> {
    let mut cases: Vec<(String, Config)> = Vec::new();
    let mut c = base.clone();
    c.efuse.insert(0x155, vec![1]);
    c.budget = 600_000_000;
    cases.push(("path: the flash behind the FCH's xSPI controller (fuse 0x155), which does not answer".into(), c));
    for (name, jedec) in [("", [0xefu8, 0x60, 0x18]), (", a part read on four lines", [0xef, 0x60, 0x17]), (", a part the table does not have", [0x01, 0x02, 0x03])] {
        let mut c = base.clone();
        c.efuse.insert(0x155, vec![1]);
        c.jedec = jedec;
        c.budget = 600_000_000;
        tweak(&mut c, |soc| soc.t.xspi = true);
        cases.push((format!("path: the flash behind the FCH's xSPI controller{name}"), c));
    }
    // PVT sensor and glitch detector trims, marked by ATE for distribution (bits 6 and 7 of fuse 0x4a3)
    for (name, ate) in [("PVT trim", 0x40u8), ("glitch detector trim", 0x80), ("PVT and glitch detector trims", 0xc0)] {
        let mut c = base.clone();
        c.efuse.insert(0x228, (0..0x278u32).map(|i| (i * 7 + 1) as u8).collect());
        c.efuse.insert(0x4a0, vec![0x21, 0x43, 0x65, ate | 0x07]);
        cases.push((format!("path: fuses: {name} marked for distribution"), c));
    }
    // two DDR channels: the multimedia hub's remap, the CI-700's other nodes
    let mut c = base.clone();
    c.constants.push(("drv_ddr_get_ch_num", 2));
    cases.push(("path: two DDR channels".into(), c));
    // the DDR driver on an emulation platform (the memory configuration's boot environment 0)
    let mut c = base.clone();
    c.ddr = true;
    c.constants.push(("MemGetBootEnv", 0));
    c.budget = 300_000_000;
    cases.push(("path: DDR driver, boot environment 0 (emulation)".into(), c));
    // debug certificates (flash 0x107000): header, then features at 0x200, signature length at 0xc00
    let cert = |features: &[(u32, u32)], sign: u32, chip: u8| -> Vec<u8> {
        let mut b = vec![0u8; 0x1000];
        b[..11].copy_from_slice(b"CIXSECDEBUG");
        b[12] = 1;
        b[20..28].fill(chip);
        b[52] = features.len() as u8;
        b[56] = sign as u8;
        for (i, (id, value)) in features.iter().enumerate() {
            b[0x200 + 8 * i..0x204 + 8 * i].copy_from_slice(&id.to_le_bytes());
            b[0x204 + 8 * i..0x208 + 8 * i].copy_from_slice(&value.to_le_bytes());
        }
        b[0xc00..0xc04].copy_from_slice(&0x180u32.to_le_bytes());
        for (i, x) in b[0xc04..0xc04 + 0x180].iter_mut().enumerate() {
            *x = (i * 13 + 5) as u8;
        }
        b
    };
    let all = [(1u32, 1u32), (2, 1), (3, 0), (4, 1), (5, 1)];
    for (name, lc, c) in [
        ("for this chip, every feature", 4u32, cert(&all, 0x5a, 0)),
        ("for this chip, no feature", 4, cert(&[], 0x5a, 0)),
        ("with feature 6", 4, cert(&[(1, 1), (6, 1)], 0x5a, 0)),
        ("with 1000 features", 4, {
            let mut b = cert(&all, 0x5a, 0);
            b[52..56].copy_from_slice(&1000u32.to_le_bytes());
            b
        }),
        ("for another chip", 4, cert(&all, 0x5a, 0x11)),
        ("under secure boot, RSA-3072, signature wrong", 7, cert(&all, 0x5a, 0)),
        ("under secure boot, RSA-2048, signature wrong", 7, cert(&all, 0x5c, 0)),
        ("under secure boot, signature type unknown", 7, cert(&all, 0x77, 0)),
        ("under secure boot, signature of 4 GiB", 7, {
            let mut b = cert(&all, 0x5a, 0);
            b[0xc00..0xc04].fill(0xff);
            b
        }),
    ] {
        let mut f = flash.to_vec();
        f[0x107000..0x108000].copy_from_slice(&c);
        let mut c = base.clone();
        c.flash = std::rc::Rc::new(f);
        c.life_cycle = lc;
        if lc == 7 {
            c.efuse = keys_efuse(flash, true);
        }
        cases.push((format!("path: debug certificate {name}"), c));
    }
    // the repair directory (fuse 0x564: three entries a word, group and rows - 1) with damage
    let dir = |entries: &[(u32, u32)]| -> Vec<u8> {
        let mut out = Vec::new();
        for w in entries.chunks(3) {
            let word = w.iter().enumerate().fold(0u32, |a, (i, (id, rows))| a | (id | (rows - 1) << 5) << (10 * i));
            out.extend(word.to_le_bytes());
        }
        out
    };
    for (name, entries) in [
        ("an unknown group (22)", vec![(1u32, 2u32), (22, 1)]),
        ("more rows than a group has (FCH: 2)", vec![(1, 2), (21, 2)]),
        ("more rows than the display's (15)", vec![(3, 15), (4, 1)]),
        ("32 rows for the CPUs, then every other group", (1..=21).map(|g| (g, if g == 1 { 32 } else { 1 })).collect()),
    ] {
        let mut c = base.clone();
        c.efuse.insert(0x564, dir(&entries));
        c.efuse.insert(0x58c, (0..0x100u32).map(|i| (i * 11 + 3) as u8).collect());
        cases.push((format!("path: repair fuses: {name}"), c));
    }
    let mut phases = boots(im, cases, verbose)?;
    if wanted("path: SM2 signature") {
        let mut q = boot(im, base, "path: SM2 signature: ")?;
        phases.extend(sm2(&mut q.b));
    }
    if !wanted("path: repair fuses for every group") && !wanted("path: MTE on") {
        return Ok(phases);
    }

    // repair data for every group, then the run-time requests with it
    let mut c = base.clone();
    c.efuse.insert(0x564, dir(&(1..=21).map(|g| (g, 1 + g % 2)).collect::<Vec<_>>()));
    c.efuse.insert(0x58c, (0..0x100u32).map(|i| (i * 11 + 3) as u8).collect());
    let mut q = boot(im, &c, "path: repair fuses for every group: ")?;
    for chan in [0usize, 2] {
        let side = if chan == 0 { "TEE" } else { "REE" };
        for g in 0..=22u32 {
            for index in [0u32, 1, 0x100, 0x8000] {
                q.ask(&format!("repair {g}/{index:#x}"), chan, 0x82000006, &[g, index]);
                q.phase(&format!("path: repair fuses for every group: {side} repair group {g}, index {index:#x}"), false);
            }
        }
    }
    phases.append(&mut q.phases);

    // MTE on (stored in the flash by the request), then what flushes the tag cache: a hang record, a suspend
    let mut q = boot(im, base, "path: MTE on: ")?;
    q.ask("MTE on, stored", 0, 0x82000020, &[0x0101]);
    q.phase("path: MTE on: the request", true);
    q.ask("exception area", 0, 0x82000015, &[0, 0x83d00000, 0xbab1face]);
    q.phase("path: MTE on, crash: exception area registered", false);
    q.ask("hang record", 0, 0x82000017, &[]);
    q.phase("path: MTE on, crash: hang record on request", true);
    suspend(&mut q, "path: MTE on, crash: record kept, suspend", Some(0xaabb), 0);
    phases.append(&mut q.phases);
    Ok(phases)
}

/// SM2 signatures, the open firmware's alone: no board's image is signed with SM2, and CIX's firmware has the
/// crypto engine check them (its public-key unit is not modelled).  sec_sm2_verify is called in the booted
/// image with the example of GB/T 32918.5 (annex A: message "message digest", identity "1234567812345678")
/// and with that example damaged; each result is set against what the standard says.
fn sm2(s: &mut Sim) -> Vec<(Phase, Phase)> {
    let hex = |t: &str| -> Vec<u8> {
        let t: String = t.chars().filter(|c| !c.is_whitespace()).collect();
        (0..t.len() / 2).map(|i| u8::from_str_radix(&t[2 * i..2 * i + 2], 16).unwrap()).collect()
    };
    let qx = hex("09F9DF31 1E5421A1 50DD7D16 1E4BC5C6 72179FAD 1833FC07 6BB08FF3 56F35020");
    let qy = hex("CCEA490C E26775A5 2DC6EA71 8CC1AA60 0AED05FB F35E084A 6632F607 2DA9AD13");
    let r = hex("F5A03B06 48D2C463 0EEAC513 E1BB81A1 5944DA38 27D5B741 43AC7EAC EEE720B3");
    let sv = hex("B1B6AA29 DF212FD8 763182BC 0D421CA1 BB9038FD 1F7F42D4 840B69C4 85BBC1AA");
    let n = hex("FFFFFFFE FFFFFFFF FFFFFFFF FFFFFFFF 7203DF6B 21C6052B 53BBF409 39D54123");
    let int = |v: &[u8]| -> Vec<u8> {
        let v: Vec<u8> = v.iter().copied().skip_while(|b| *b == 0).collect();
        let mut out = vec![2u8];
        let pad = v.first().map_or(true, |b| *b & 0x80 != 0);
        out.push((v.len() + pad as usize) as u8);
        if pad {
            out.push(0);
        }
        out.extend(v);
        out
    };
    let der = |r: &[u8], s: &[u8]| -> Vec<u8> {
        let body = [int(r), int(s)].concat();
        [vec![0x30, body.len() as u8], body].concat()
    };
    let flip = |v: &[u8], i: usize| -> Vec<u8> {
        let mut v = v.to_vec();
        v[i] ^= 1;
        v
    };
    let msg = b"message digest".to_vec();
    let id = b"1234567812345678".to_vec();
    let good = der(&r, &sv);
    // (name, key x, key y, identity, message, signature, accepted)
    type Case = (&'static str, Vec<u8>, Vec<u8>, Vec<u8>, Vec<u8>, Vec<u8>, bool);
    let cases: Vec<Case> = vec![
        ("the standard's example", qx.clone(), qy.clone(), id.clone(), msg.clone(), good.clone(), true),
        ("the message altered", qx.clone(), qy.clone(), id.clone(), b"message digesu".to_vec(), good.clone(), false),
        ("the identity altered", qx.clone(), qy.clone(), b"1234567812345679".to_vec(), msg.clone(), good.clone(), false),
        ("r altered", qx.clone(), qy.clone(), id.clone(), msg.clone(), der(&flip(&r, 31), &sv), false),
        ("s altered", qx.clone(), qy.clone(), id.clone(), msg.clone(), der(&r, &flip(&sv, 0)), false),
        ("r = 0", qx.clone(), qy.clone(), id.clone(), msg.clone(), der(&[0], &sv), false),
        ("s = 0", qx.clone(), qy.clone(), id.clone(), msg.clone(), der(&r, &[0]), false),
        ("r = n", qx.clone(), qy.clone(), id.clone(), msg.clone(), der(&n, &sv), false),
        ("s = n", qx.clone(), qy.clone(), id.clone(), msg.clone(), der(&r, &n), false),
        ("the key not on the curve", qx.clone(), flip(&qy, 31), id.clone(), msg.clone(), good.clone(), false),
        ("the key all zeros", vec![0; 32], vec![0; 32], id.clone(), msg.clone(), good.clone(), false),
        ("the signature cut short", qx.clone(), qy.clone(), id.clone(), msg.clone(), good[..good.len() - 1].to_vec(), false),
        ("the signature with a byte after it", qx.clone(), qy.clone(), id.clone(), msg.clone(), [good.clone(), vec![0]].concat(), false),
        ("the signature as bare r and s", qx.clone(), qy.clone(), id.clone(), msg.clone(), [r.clone(), sv.clone()].concat(), false),
        ("no signature", qx.clone(), qy.clone(), id.clone(), msg.clone(), vec![], false),
        ("an identity of 8192 bytes", qx.clone(), qy.clone(), vec![0x31; 0x2000], msg.clone(), good.clone(), false),
    ];
    let mut out = Vec::new();
    let Some(f) = s.cpu.sym("sec_sm2_verify") else { return out };
    // the data in the SE's SRAM above the image (its memory ends at 0x20070000), the call on the image's own
    // stack below where it is
    const DATA: u32 = 0x20072000;
    for (name, x, y, id, msg, sig, accepted) in cases {
        let cpu = &mut s.cpu;
        cpu.write_bytes(DATA, &x);
        cpu.write_bytes(DATA + 0x20, &y);
        // the context: no private key, the public key's x and y
        cpu.write32(DATA + 0x40, 0);
        cpu.write32(DATA + 0x44, DATA);
        cpu.write32(DATA + 0x48, DATA + 0x20);
        cpu.write_bytes(DATA + 0x100, &msg);
        cpu.write_bytes(DATA + 0x200, &sig);
        cpu.write_bytes(DATA + 0x1000, &id);
        let sp = (cpu.r[13] - 0x200) & !7;
        for (i, v) in [id.len() as u32, DATA + 0x200, sig.len() as u32].iter().enumerate() {
            cpu.write32(sp + 4 * i as u32, *v);
        }
        // (the return address is one nothing runs at, and not of the form of an exception return)
        let got = match cpu.call(f, &[DATA + 0x40, DATA + 0x100, msg.len() as u32, DATA + 0x1000], sp, 0x2007fff0, 2_000_000_000) {
            Ok(v) if v & 0xff == 1 => "accepted".to_string(),
            Ok(v) if v & 0xff == 0 => "refused".to_string(),
            Ok(v) => format!("returned {v:#x}"),
            Err(stop) => format!("did not return: {stop:?}"),
        };
        let phase = |end: &str| Phase { name: format!("path: SM2 signature, {name} (the open firmware against the standard)"), end: end.to_string(), ..Default::default() };
        out.push((phase(if accepted { "accepted" } else { "refused" }), phase(&got)));
    }
    out
}

/// Things that do not answer: PLLs that never lock, a PM that says nothing or wants the sensor hub, a
/// sensor hub that does not start, the audio subsystem's power.
pub fn failures(im: &Images, base: &Config, verbose: bool) -> Result<Vec<(Phase, Phase)>, String> {
    let mut cases: Vec<(String, Config)> = Vec::new();
    for (name, block) in [
        ("CPU PLL 0", 0x0000u32),
        ("CPU PLL 1", 0x0100),
        ("CPU PLL 4", 0x0400),
        ("DSU PLL", 0x0500),
        ("CPU 800M", 0x0600),
        ("MMHUB PLL", 0x2000),
        ("MMSYS PLL", 0x2100),
        ("CI-700 PLL", 0x5200),
        ("system PLL", 0x5300),
        ("PCIe AXI PLL", 0x6000),
        ("PCIe PHY PLL", 0xffff),
        ("DDRC PLL", 0x7000),
        ("pixel PLL 0", 0x1000),
        ("pixel PLL 4", 0x1400),
    ] {
        let mut c = base.clone();
        tweak(&mut c, move |soc| soc.t.dead_plls.push(block));
        cases.push((format!("failure: {name} never locks"), c));
    }
    let mut c = base.clone();
    tweak(&mut c, |soc| soc.t.pm_alive = false);
    cases.push(("failure: the PM never answers".into(), c));
    let mut c = base.clone();
    tweak(&mut c, |soc| soc.t.pm_delay = 4999 * MS);
    cases.push(("failure: the PM answers after 4999 ms".into(), c));
    let mut c = base.clone();
    tweak(&mut c, |soc| soc.t.pm_delay = 5002 * MS);
    cases.push(("failure: the PM answers after 5002 ms".into(), c));
    let mut c = base.clone();
    tweak(&mut c, |soc| soc.t.pm_boot_magic = 0x1234);
    cases.push(("failure: the PM answers with another word".into(), c));
    let mut c = base.clone();
    tweak(&mut c, |soc| soc.t.pm_boot_magic = 0xaacc);
    cases.push(("failure: the PM asks for the sensor hub at boot".into(), c));
    let mut c = base.clone();
    tweak(&mut c, |soc| {
        soc.t.pm_boot_magic = 0xaacc;
        soc.t.sfh_alive = false;
    });
    cases.push(("failure: the PM asks for the sensor hub at boot, the hub does not start".into(), c));
    let mut c = base.clone();
    tweak(&mut c, |soc| soc.t.audio_power = false);
    cases.push(("failure: the audio subsystem does not power up".into(), c));
    boots(im, cases, verbose)
}

/// The DDR driver on the model of the controllers: each feature the memory configuration could ask for,
/// what the subsystem can do wrong, and every interrupt status bit at run time.
pub fn ddr(im: &Images, base: &Config, verbose: bool, eye: bool) -> Result<Vec<(Phase, Phase)>, String> {
    let mut base = base.clone();
    base.ddr = true;
    let mut cases: Vec<(String, Config)> = Vec::new();
    let mut sets: Vec<&str> = vec![
        "DfsEn=1",
        "IEcc=1",
        "RdLEcc=1 WrLEcc=1",
        "MbistEn=1",
        "MbistEn=1 MbistMode=1",
        "SSC=5",
        "TrainMode=1",
        "RfmEn=1",
        "PbrEn=1 SelBankInQ=1",
        "AddressMode=1",
        "PortPriority=1",
        "WckAlwaysOn=1",
        "DataMask=1",
        "AutoPrechargeEn=1",
        "BdwP0=1 BdwP1=1",
        "BpEn=1",
        "RPriorityP0=1 WPriorityP0=1 BdwOvflowP0=1 RPriorityP1=1 WPriorityP1=1 BdwOvflowP1=1 PortPriority=1",
        "PeriodicTrain=1",
        "DfsEn=1 IEcc=1 SSC=5 RfmEn=1",
    ];
    if eye {
        sets.push("EyeScanEn=1");
    }
    for set in sets {
        let mut c = base.clone();
        c.features = parse_features(&set.split(' ').map(|s| s.to_string()).collect::<Vec<_>>())?;
        cases.push((format!("DDR feature {set}"), c));
    }
    // other parts: mode registers 5 (manufacturer) and 8 (density and width) as the parts would answer
    for (name, mr5, mr8) in [("Samsung 8 Gb", 0x01u32, 0x10u32), ("Hynix 16 Gb", 0x06, 0x18), ("Micron 12 Gb", 0xff, 0x14), ("CXMT 16 Gb", 0x13, 0x18), ("a part of 32 Gb", 0x01, 0x1c), ("byte mode", 0x01, 0x58), ("all ones", 0xff, 0xff)] {
        let mut c = base.clone();
        tweak(&mut c, move |soc| {
            let d = soc.ddr.as_mut().unwrap();
            d.mr_defaults.insert(5, mr5);
            d.mr_defaults.insert(8, mr8);
        });
        cases.push((format!("DDR part: {name} (MR5 {mr5:#x}, MR8 {mr8:#x})"), c));
    }
    let fault = |name: &str, f: fn(&mut DdrFault)| {
        let mut c = base.clone();
        c.budget = 400_000_000;
        tweak(&mut c, move |soc| f(&mut soc.ddr.as_mut().unwrap().fault));
        (format!("DDR failure: {name}"), c)
    };
    cases.push(fault("channel 0 never completes its initialisation", |f| f.init_never = 1));
    cases.push(fault("channel 3 never completes its initialisation", |f| f.init_never = 8));
    cases.push(fault("no channel completes its initialisation", |f| f.init_never = 15));
    cases.push(fault("a DFI error at initialisation (status 0x598 bit 4)", |f| f.init_status = 0x10));
    cases.push(fault("every error bit of 0x598 at initialisation", |f| f.init_status = 0xfffffffe));
    cases.push(fault("PI training status bits with init done", |f| f.pi_status = 0xfffe));
    cases.push(fault("PI status bit 1", |f| f.pi_status = 2));
    for (name, set) in [("the controller's BIST fails", "MbistEn=1"), ("the controller's BIST fails, other mode", "MbistEn=1 MbistMode=1")] {
        let mut c = base.clone();
        c.budget = 400_000_000;
        c.features = parse_features(&set.split(' ').map(|s| s.to_string()).collect::<Vec<_>>())?;
        tweak(&mut c, |soc| {
            let f = &mut soc.ddr.as_mut().unwrap().fault;
            f.bist_fail = true;
            f.pi_bist_fail = true;
        });
        cases.push((format!("DDR failure: {name}"), c));
    }
    for reason in [3u32, 4, 8] {
        let mut c = base.clone();
        c.boot_reason = reason;
        cases.push((format!("DDR, boot reason {reason} (warm)"), c));
    }
    let mut phases = boots(im, cases, verbose)?;
    // at run time: every status bit of every interrupt group, on each channel in turn
    let mut q = boot(im, &base, "DDR run time: ")?;
    q.phases.clear();
    for g in 0..9u32 {
        for ch in 0..4usize {
            q.save();
            for bit in 0..32u32 {
                // the status bit, and the channel's interrupt once (a bit the handler does not acknowledge
                // would keep a level up and the handler running again and again: how often is not compared)
                q.each(|s| {
                    let cpu = &mut s.cpu;
                    cpu.bus.ddr.as_mut().unwrap().set_status_quiet(ch, 0x57c + 4 * g, 1 << bit);
                    cpu.pend(16 + DDR_IRQ[ch]);
                });
                q.until(2 * MS, |s| s.dead.is_some() || !s.cpu.in_handler() && !s.cpu.wake_pending());
                q.each(|s| {
                    let cpu = &mut s.cpu;
                    let d = cpu.bus.ddr.as_mut().unwrap();
                    d.status[ch][g as usize] = 0;
                    d.set_status(&mut cpu.io, &cpu.mem, ch, 0x57c + 4 * g, 0);
                });
            }
            q.phase(&format!("DDR run time: interrupt group {g} (status 0x{:x}), every bit, channel {ch}", 0x57c + 4 * g), false);
            q.restore();
            q.each(|s| s.mark());
        }
    }
    // the AP has the SE let go of the DDR interrupts; then a status bit comes up
    q.ask("DDR interrupts released", 0, 0x82000011, &[]);
    q.each(|s| {
        let cpu = &mut s.cpu;
        cpu.bus.ddr.as_mut().unwrap().set_status(&mut cpu.io, &cpu.mem, 1, 0x580, 0x803);
    });
    q.settle(2 * MS);
    q.phase("DDR run time: interrupts released (0x82000011), then a status bit", false);
    suspend(&mut q, "DDR run time: suspend (sensor hub started)", Some(0xaacc), 2);
    phases.extend(std::mem::take(&mut q.phases));
    Ok(phases)
}
