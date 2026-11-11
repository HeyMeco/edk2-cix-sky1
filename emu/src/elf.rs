// SPDX-License-Identifier: BSD-2-Clause-Patent
//! A reader for the 32-bit little-endian ELF files the firmware builds
//! produce: loadable segments, sections and the symbol table.

use std::collections::HashMap;
use std::path::Path;

pub const STT_NOTYPE: u8 = 0;
pub const STT_OBJECT: u8 = 1;
pub const STT_FUNC: u8 = 2;
pub const STT_FILE: u8 = 4;

#[derive(Clone, Debug)]
pub struct Segment {
    pub vaddr: u32,
    pub paddr: u32,
    pub data: Vec<u8>,
    pub memsz: u32,
    pub flags: u32,
}

#[derive(Clone, Debug)]
pub struct Section {
    pub name: String,
    pub kind: u32,
    pub flags: u32,
    pub addr: u32,
    pub offset: u32,
    pub size: u32,
}

#[derive(Clone, Debug)]
pub struct Symbol {
    pub name: String,
    pub value: u32,
    pub size: u32,
    pub kind: u8,
    pub shndx: u16,
    /// the STT_FILE symbol this local symbol follows, if any
    pub file: Option<u32>,
}

#[derive(Clone, Debug, Default)]
pub struct Elf {
    pub path: String,
    pub entry: u32,
    pub machine: u16,
    pub segments: Vec<Segment>,
    pub sections: Vec<Section>,
    pub symbols: Vec<Symbol>,
    pub files: Vec<String>,
    raw: Vec<u8>,
}

fn u16le(b: &[u8], o: usize) -> u16 {
    u16::from_le_bytes([b[o], b[o + 1]])
}

fn u32le(b: &[u8], o: usize) -> u32 {
    u32::from_le_bytes([b[o], b[o + 1], b[o + 2], b[o + 3]])
}

fn cstr(b: &[u8], o: usize) -> String {
    let end = b[o..].iter().position(|&c| c == 0).map_or(b.len(), |n| o + n);
    String::from_utf8_lossy(&b[o..end]).into_owned()
}

impl Elf {
    pub fn load(path: impl AsRef<Path>) -> Result<Elf, String> {
        let path = path.as_ref();
        let raw = std::fs::read(path).map_err(|e| format!("{}: {e}", path.display()))?;
        Elf::parse(raw, &path.display().to_string())
    }

    pub fn parse(raw: Vec<u8>, name: &str) -> Result<Elf, String> {
        if raw.len() < 52 || &raw[..4] != b"\x7fELF" || raw[4] != 1 || raw[5] != 1 {
            return Err(format!("{name}: not a 32-bit little-endian ELF file"));
        }
        let mut elf = Elf { path: name.to_string(), entry: u32le(&raw, 24), machine: u16le(&raw, 18), ..Default::default() };
        let (phoff, shoff) = (u32le(&raw, 28) as usize, u32le(&raw, 32) as usize);
        let (phentsize, phnum) = (u16le(&raw, 42) as usize, u16le(&raw, 44) as usize);
        let (shentsize, shnum, shstrndx) = (u16le(&raw, 46) as usize, u16le(&raw, 48) as usize, u16le(&raw, 50) as usize);
        for i in 0..phnum {
            let p = phoff + i * phentsize;
            if u32le(&raw, p) != 1 {
                continue; // PT_LOAD
            }
            let (off, filesz) = (u32le(&raw, p + 4) as usize, u32le(&raw, p + 16) as usize);
            elf.segments.push(Segment {
                vaddr: u32le(&raw, p + 8),
                paddr: u32le(&raw, p + 12),
                data: raw[off..off + filesz].to_vec(),
                memsz: u32le(&raw, p + 20),
                flags: u32le(&raw, p + 24),
            });
        }
        let mut names = Vec::new();
        for i in 0..shnum {
            let s = shoff + i * shentsize;
            names.push(u32le(&raw, s) as usize);
            elf.sections.push(Section {
                name: String::new(),
                kind: u32le(&raw, s + 4),
                flags: u32le(&raw, s + 8),
                addr: u32le(&raw, s + 12),
                offset: u32le(&raw, s + 16),
                size: u32le(&raw, s + 20),
            });
        }
        if shstrndx < shnum {
            let stroff = elf.sections[shstrndx].offset as usize;
            for (i, n) in names.iter().enumerate() {
                elf.sections[i].name = cstr(&raw, stroff + n);
            }
        }
        for i in 0..shnum {
            if elf.sections[i].kind != 2 {
                continue; // SHT_SYMTAB
            }
            let s = shoff + i * shentsize;
            let link = u32le(&raw, s + 24) as usize;
            let stroff = elf.sections[link].offset as usize;
            let (off, size) = (elf.sections[i].offset as usize, elf.sections[i].size as usize);
            let mut file = None;
            for k in 0..size / 16 {
                let e = off + 16 * k;
                let info = raw[e + 12];
                let name = cstr(&raw, stroff + u32le(&raw, e) as usize);
                let kind = info & 15;
                if kind == STT_FILE {
                    file = Some(elf.files.len() as u32);
                    elf.files.push(name.clone());
                }
                elf.symbols.push(Symbol {
                    name,
                    value: u32le(&raw, e + 4),
                    size: u32le(&raw, e + 8),
                    kind,
                    shndx: u16le(&raw, e + 14),
                    file: if info >> 4 == 0 { file } else { None },
                });
            }
        }
        elf.raw = raw;
        Ok(elf)
    }

    pub fn section(&self, name: &str) -> Option<&Section> {
        self.sections.iter().find(|s| s.name == name)
    }

    pub fn section_data(&self, s: &Section) -> &[u8] {
        if s.kind == 8 {
            return &[]; // SHT_NOBITS
        }
        &self.raw[s.offset as usize..(s.offset + s.size) as usize]
    }

    /// Functions as (address, size, name), sorted; Thumb bit cleared.
    pub fn functions(&self) -> Vec<(u32, u32, String)> {
        let thumb = self.machine == 40;
        let mut v: Vec<_> = self
            .symbols
            .iter()
            .filter(|s| s.kind == STT_FUNC && !s.name.is_empty())
            .map(|s| (if thumb { s.value & !1 } else { s.value }, s.size, s.name.clone()))
            .collect();
        v.sort();
        v.dedup();
        v
    }

    /// Name -> (value, size) of functions and objects; the first in the table wins.
    pub fn by_name(&self) -> HashMap<String, (u32, u32, u8)> {
        let mut m = HashMap::new();
        for s in &self.symbols {
            if (s.kind == STT_FUNC || s.kind == STT_OBJECT) && !s.name.is_empty() {
                m.entry(s.name.clone()).or_insert((s.value, s.size, s.kind));
            }
        }
        m
    }
}
