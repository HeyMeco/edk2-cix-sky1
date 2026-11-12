// SPDX-License-Identifier: BSD-2-Clause-Patent
//! BL2 from its entry to the jump to BL31, in several configurations:
//! what the SE reports (DRAM, the DDR channels, the boot core), what the
//! flash is (its JEDEC ID picks the read command from bootloader1's
//! table), what the flash holds (a header that is not there, images and
//! certificates with a byte changed, the vendor's own bootloader3).

use super::images;
use super::machine::Config;
use super::test::{audit_add, run_bl2, wanted, Coverage, Images, Opts, Phase};
use std::rc::Rc;

type Change = Box<dyn Fn(&mut Config)>;

/// A byte of the flash changed.
fn flip(c: &mut Config, at: usize) {
    let f = Rc::make_mut(&mut c.flash);
    if at < f.len() {
        f[at] ^= 0x40;
    }
}

/// Where an entry of a FIP is in the flash: the FIP by its firmware header type (2: bootloader2, 7:
/// bootloader3), the entry by its place in the FIP.
fn fip_at(c: &Config, kind: u32, entry: usize) -> Option<(usize, usize)> {
    let (off, len) = images::flash_entry(&c.flash, kind)?;
    let fip = c.flash.get(off..off + len)?;
    let e = images::fip_entries(fip);
    e.get(entry).map(|e| (off + e.1, e.2))
}

fn scenarios() -> Vec<(String, Change)> {
    let mut v: Vec<(String, Change)> = vec![("BL2: boot".into(), Box::new(|_c: &mut Config| {}))];
    for mb in [4096u32, 8192, 32768, 65536] {
        v.push((format!("BL2: {} GiB of DRAM", mb / 1024), Box::new(move |c: &mut Config| c.dram_mb = mb)));
    }
    for mask in [0xcu8, 0x3, 0x1] {
        v.push((format!("BL2: DDR channel mask {mask:#x}"), Box::new(move |c: &mut Config| c.channel_mask = mask)));
    }
    // SW_USED_REG5 bit 16: the TZC-400s get the debug set of regions, which depends on the DRAM size
    for mb in [16384u32, 65536] {
        v.push((
            format!("BL2: TZC debug regions, {} GiB", mb / 1024),
            Box::new(move |c: &mut Config| {
                c.dram_mb = mb;
                c.presets.push((0x1600_0514, 1 << 16));
            }),
        ));
    }
    for core in [0usize, 4, 11] {
        v.push((format!("BL2: boot core {core}"), Box::new(move |c: &mut Config| c.boot_core = core)));
    }
    // the flash: in bootloader1's table with quad reads, in it with single reads, another maker's
    for id in [[0xefu8, 0x60, 0x17], [0xef, 0x60, 0x18], [0xc8, 0x65, 0x17], [0x00, 0x00, 0x00]] {
        v.push((format!("BL2: flash with JEDEC ID {:02x}{:02x}{:02x}", id[0], id[1], id[2]), Box::new(move |c: &mut Config| c.jedec = id)));
    }
    // the boot ROM booted from the second copy (no header at 2 MiB in this image), and no header at all
    v.push(("BL2: boot ROM reports the second copy".into(), Box::new(|c: &mut Config| c.presets.push((0x0504_0060, 1)))));
    v.push(("BL2: no firmware header in the flash".into(), Box::new(|c: &mut Config| flip(c, 0x10_0000))));
    v.push((
        "BL2: bootloader1's flash table says no entries".into(),
        Box::new(|c: &mut Config| {
            let f = Rc::make_mut(&mut c.flash);
            f[0x18_8800] = 0;
        }),
    ));
    v.push(("BL2: the flash controller never gets idle".into(), Box::new(|c: &mut Config| c.xspi_stuck = true)));
    // one byte changed in each thing BL2 loads: the open BL2 refuses it, CIX's (the fuses say no secure boot) boots it
    let fips: [(u32, &str); 2] = [(2, "bootloader2"), (7, "bootloader3")];
    for (kind, fip) in fips {
        for entry in 0..8usize {
            v.push((
                format!("BL2: open BL2 refuses a changed byte in {fip}'s entry {entry}"),
                Box::new(move |c: &mut Config| {
                    if let Some((at, len)) = fip_at(c, kind, entry) {
                        flip(c, at + len / 2);
                    }
                }),
            ));
        }
    }
    v.push((
        "BL2: open BL2 refuses a changed byte in bootloader2's table of contents".into(),
        Box::new(|c: &mut Config| {
            if let Some((off, _)) = images::flash_entry(&c.flash, 2) {
                flip(c, off + 16 + 5);
            }
        }),
    ));
    v
}

pub fn run(imgs: &Images, o: &Opts, cov: &mut Coverage, pairs: &mut Vec<(Phase, Phase)>, audit: &mut (u64, Vec<String>)) {
    for (name, change) in scenarios() {
        if !wanted(o, &name) {
            continue;
        }
        let mut cfg = o.cfg.clone();
        change(&mut cfg);
        // (a scenario for an entry the FIP has not got is the plain boot again: left out)
        if name.contains("entry") && Rc::ptr_eq(&cfg.flash, &o.cfg.flash) {
            continue;
        }
        let (po, wo) = run_bl2(&imgs.open_bl2, &cfg, o, cov, &name);
        let (pc, wc) = run_bl2(&imgs.cix_bl2, &cfg, o, cov, &name);
        for w in [&wo, &wc].into_iter().flatten() {
            audit_add(audit, &w.m);
        }
        pairs.push((po, pc));
    }
    // the vendor's layout: its own bootloader2 and a bootloader3 certified with its tool
    if let Some(flash) = o.vendor_flash.as_ref() {
        let name = "BL2: the vendor's flash image";
        if wanted(o, name) {
            let mut cfg = o.cfg.clone();
            cfg.flash = flash.clone();
            let (po, _) = run_bl2(&imgs.open_bl2, &cfg, o, cov, name);
            let (pc, _) = run_bl2(&imgs.cix_bl2, &cfg, o, cov, name);
            pairs.push((po, pc));
        }
    }
}
