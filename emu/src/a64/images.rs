// SPDX-License-Identifier: BSD-2-Clause-Patent
//! Where the images come from: CIX's BL2 out of a bootloader1 container,
//! its BL31 out of a bootloader2 FIP (the layouts scripts/open/cixfw.py
//! documents), and pieces of a flash image.

use super::elf64::Image;
use crate::util;

pub const UUID_BL31: [u8; 16] = [0x47, 0xd4, 0x08, 0x6d, 0x4c, 0xfe, 0x98, 0x46, 0x9b, 0x95, 0x29, 0x50, 0xcb, 0xbd, 0x5a, 0x00];

/// The AP image (BL2) of a "CIXBTFF!" container: image entries of 24 bytes
/// from 0x9ec (type, flag, 0, offset, size, load address), type 3.
pub fn bl1_ap(bl1: &[u8]) -> Result<Vec<u8>, String> {
    if bl1.len() < 0x1000 || &bl1[..8] != b"CIXBTFF!" {
        return Err("not a CIXBTFF! container (bootloader1)".into());
    }
    let count = util::u32le(bl1, 0x9e8) as usize;
    for i in 0..count.min(16) {
        let e = 0x9ec + 24 * i;
        if util::u32le(bl1, e) == 3 {
            let (off, size) = (util::u32le(bl1, e + 12) as usize, util::u32le(bl1, e + 16) as usize);
            return bl1.get(off..off + size).map(|d| d.to_vec()).ok_or("bootloader1: AP image out of range".into());
        }
    }
    Err("bootloader1: no AP image".into())
}

/// The entries of a FIP: (UUID, offset, size).
pub fn fip_entries(fip: &[u8]) -> Vec<([u8; 16], usize, usize)> {
    let mut out = Vec::new();
    if fip.len() < 16 || util::u32le(fip, 0) != 0xaa64_0001 {
        return out;
    }
    let mut at = 16;
    while at + 40 <= fip.len() {
        let mut uuid = [0u8; 16];
        uuid.copy_from_slice(&fip[at..at + 16]);
        if uuid == [0; 16] {
            break;
        }
        let off = u64::from_le_bytes(fip[at + 16..at + 24].try_into().unwrap()) as usize;
        let size = u64::from_le_bytes(fip[at + 24..at + 32].try_into().unwrap()) as usize;
        out.push((uuid, off, size));
        at += 40;
    }
    out
}

pub fn fip_image(fip: &[u8], uuid: &[u8; 16]) -> Result<Vec<u8>, String> {
    fip_entries(fip)
        .iter()
        .find(|e| &e.0 == uuid)
        .and_then(|e| fip.get(e.1..e.1 + e.2))
        .map(|d| d.to_vec())
        .ok_or("the FIP has no such image".into())
}

/// A raw image as the interpreter's Image, with names when there is a file of them.
pub fn raw(name: &str, base: u64, data: Vec<u8>, names: Option<&str>) -> Result<Image, String> {
    let end = base + data.len() as u64;
    let mut img = Image { name: name.to_string(), base, entry: base, end, text_end: end, chunks: vec![(base, data)], ..Default::default() };
    if let Some(n) = names {
        if std::path::Path::new(n).exists() {
            img.add_names(n)?;
        }
    }
    Ok(img)
}

/// The firmware header cix_package_tool writes at 1 MiB of a flash image:
/// magic 0x55aa55aa, version, count, then (type, address, length, 0).
pub fn flash_entry(flash: &[u8], kind: u32) -> Option<(usize, usize)> {
    let h = 0x10_0000;
    if flash.len() < h + 16 || util::u32le(flash, h) != 0x55aa_55aa {
        return None;
    }
    let count = util::u32le(flash, h + 8) as usize;
    for i in 0..count.min(256) {
        let e = h + 16 + 16 * i;
        if util::u32le(flash, e) == kind {
            return Some((util::u32le(flash, e + 4) as usize, util::u32le(flash, e + 8) as usize));
        }
    }
    None
}

/// SHA-256 of BL1 key 2 (the image key, which is also the FIPs' root) as
/// the SE hands it to BL2 and CIX's BL2 compares it: of the modulus and the
/// exponent's three bytes.
pub fn rotpk_of_flash(flash: &[u8]) -> Option<[u8; 32]> {
    let at = flash.windows(8).position(|w| w == b"CIXBTFF!")?;
    let mut key = flash.get(at + 0x324..at + 0x324 + 384)?.to_vec();
    key.extend_from_slice(&[1, 0, 1]);
    Some(util::sha256(&key))
}

/// The commit a vendor's TF-A image names itself by ("v2.7-33527c31abbe" -> "33527c31abbe").
pub fn commit(image: &[u8]) -> String {
    let Some(at) = image.windows(5).position(|w| w == b"v2.7-") else { return "unknown".into() };
    image[at + 5..].iter().take_while(|b| b.is_ascii_hexdigit()).map(|&b| b as char).collect()
}
