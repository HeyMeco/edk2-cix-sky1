// SPDX-License-Identifier: BSD-2-Clause-Patent
//! The images: an ELF64 reader for the open BL2 and BL31 (segments and the
//! symbol table), and CIX's, which are raw binaries with no names.

use crate::elf;

#[derive(Clone, Debug, Default)]
pub struct Image {
    /// the file's name without its directory: what coverage is recorded under
    pub name: String,
    pub base: u64,
    pub entry: u64,
    /// what is loaded: (address, bytes)
    pub chunks: Vec<(u64, Vec<u8>)>,
    /// the end of what the image occupies (with .bss)
    pub end: u64,
    /// the end of its code, where known (the ELF's .text; CIX's: found by the caller)
    pub text_end: u64,
    /// functions (address, size, name), sorted; empty for a raw image
    pub functions: Vec<(u64, u64, String)>,
    /// objects and functions by name
    pub symbols: std::collections::HashMap<String, (u64, u64)>,
    /// the same symbols for cov.rs (ELF32 structures; these images sit below 4 GiB)
    pub elf32: Option<elf::Elf>,
}

fn u16le(b: &[u8], o: usize) -> u16 {
    u16::from_le_bytes([b[o], b[o + 1]])
}

fn u32le(b: &[u8], o: usize) -> u32 {
    u32::from_le_bytes([b[o], b[o + 1], b[o + 2], b[o + 3]])
}

fn u64le(b: &[u8], o: usize) -> u64 {
    u64::from_le_bytes(b[o..o + 8].try_into().unwrap())
}

fn cstr(b: &[u8], o: usize) -> String {
    let end = b[o..].iter().position(|&c| c == 0).map_or(b.len(), |n| o + n);
    String::from_utf8_lossy(&b[o..end]).into_owned()
}

fn file_name(path: &str) -> String {
    std::path::Path::new(path).file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default()
}

impl Image {
    /// An ELF64 file, or (not ELF) a raw image loaded at `raw_base`.
    pub fn load(path: &str, raw_base: u64) -> Result<Image, String> {
        let raw = std::fs::read(path).map_err(|e| format!("{path}: {e}"))?;
        if raw.len() >= 64 && &raw[..4] == b"\x7fELF" {
            Image::parse(raw, path)
        } else {
            let end = raw_base + raw.len() as u64;
            Ok(Image { name: file_name(path), base: raw_base, entry: raw_base, end, text_end: end, chunks: vec![(raw_base, raw)], ..Default::default() })
        }
    }

    fn parse(raw: Vec<u8>, path: &str) -> Result<Image, String> {
        if raw[4] != 2 || raw[5] != 1 || u16le(&raw, 18) != 183 {
            return Err(format!("{path}: not a little-endian AArch64 ELF64 file"));
        }
        let mut img = Image { name: file_name(path), entry: u64le(&raw, 24), base: u64::MAX, ..Default::default() };
        let (phoff, shoff) = (u64le(&raw, 32) as usize, u64le(&raw, 40) as usize);
        let (phentsize, phnum) = (u16le(&raw, 54) as usize, u16le(&raw, 56) as usize);
        let (shentsize, shnum, shstrndx) = (u16le(&raw, 58) as usize, u16le(&raw, 60) as usize, u16le(&raw, 62) as usize);
        for i in 0..phnum {
            let p = phoff + i * phentsize;
            if u32le(&raw, p) != 1 {
                continue;
            }
            let (off, vaddr, filesz, memsz) =
                (u64le(&raw, p + 8) as usize, u64le(&raw, p + 16), u64le(&raw, p + 32) as usize, u64le(&raw, p + 40));
            img.base = img.base.min(vaddr);
            img.end = img.end.max(vaddr + memsz);
            img.chunks.push((vaddr, raw[off..off + filesz].to_vec()));
        }
        let mut e32 = elf::Elf::default();
        e32.path = path.to_string();
        e32.machine = 183;
        e32.entry = img.entry as u32;
        let sh = |i: usize| shoff + i * shentsize;
        let shstr = if shstrndx < shnum { u64le(&raw, sh(shstrndx) + 24) as usize } else { 0 };
        for i in 0..shnum {
            let s = sh(i);
            let name = cstr(&raw, shstr + u32le(&raw, s) as usize);
            let (kind, addr, offset, size) = (u32le(&raw, s + 4), u64le(&raw, s + 16), u64le(&raw, s + 24) as usize, u64le(&raw, s + 32) as usize);
            if name == ".text" {
                img.text_end = addr + size as u64;
            }
            if kind != 2 {
                continue;
            }
            let link = u32le(&raw, s + 40) as usize;
            let stroff = u64le(&raw, sh(link) + 24) as usize;
            let mut file = None;
            for k in 0..size / 24 {
                let e = offset + 24 * k;
                let info = raw[e + 4];
                let name = cstr(&raw, stroff + u32le(&raw, e) as usize);
                let (value, size) = (u64le(&raw, e + 8), u64le(&raw, e + 16));
                let kind = info & 15;
                if kind == elf::STT_FILE {
                    file = Some(e32.files.len() as u32);
                    e32.files.push(name.clone());
                }
                if (kind == elf::STT_FUNC || kind == elf::STT_OBJECT || kind == elf::STT_NOTYPE) && !name.is_empty() && !name.starts_with('$') {
                    img.symbols.entry(name.clone()).or_insert((value, size));
                }
                if kind == elf::STT_FUNC && !name.is_empty() {
                    img.functions.push((value, size, name.clone()));
                }
                e32.symbols.push(elf::Symbol {
                    name,
                    value: value as u32,
                    size: size as u32,
                    kind,
                    shndx: u16le(&raw, e + 6),
                    file: if info >> 4 == 0 { file } else { None },
                });
            }
        }
        img.functions.sort();
        img.functions.dedup();
        img.elf32 = Some(e32);
        Ok(img)
    }

    /// Names for a raw image: lines of "address name size" (funcmatch.py's, or made from a decompilation).
    pub fn add_names(&mut self, path: &str) -> Result<(), String> {
        let text = std::fs::read_to_string(path).map_err(|e| format!("{path}: {e}"))?;
        let mut e32 = elf::Elf::default();
        e32.path = self.name.clone();
        e32.machine = 183;
        for line in text.lines() {
            let f: Vec<&str> = line.split_whitespace().collect();
            if f.len() < 3 {
                continue;
            }
            let (Some(addr), Some(size)) = (crate::util::parse_int(f[0]), crate::util::parse_int(f[2])) else { continue };
            self.functions.push((addr, size, f[1].to_string()));
            self.symbols.entry(f[1].to_string()).or_insert((addr, size));
            e32.symbols.push(elf::Symbol { name: f[1].to_string(), value: addr as u32, size: size as u32, kind: elf::STT_FUNC, shndx: 1, file: None });
        }
        self.functions.sort();
        self.functions.dedup();
        if let Some(last) = self.functions.last() {
            self.text_end = self.text_end.min((last.0 + last.1 + 0xfff) & !0xfff);
        }
        self.elf32 = Some(e32);
        Ok(())
    }

    pub fn sym(&self, name: &str) -> Option<u64> {
        self.symbols.get(name).map(|s| s.0)
    }

    /// "function+0x12" for an address in a named function, else the address.
    pub fn at(&self, addr: u64) -> String {
        let i = self.functions.partition_point(|f| f.0 <= addr);
        if i > 0 {
            let (a, size, name) = &self.functions[i - 1];
            if addr < a + (*size).max(4) || (i < self.functions.len() && *size == 0 && addr < self.functions[i].0) {
                return if addr == *a { format!("{name} ({addr:#x})") } else { format!("{name}+{:#x} ({addr:#x})", addr - a) };
            }
        }
        format!("{addr:#x}")
    }
}
