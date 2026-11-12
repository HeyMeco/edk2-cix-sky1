// SPDX-License-Identifier: BSD-2-Clause-Patent
//! A Sky1 with an image on it: the cores, the model, and what the harness
//! does in place of the lower exception levels.

use super::cpu::{self, Cpu, Stop};
use super::elf64::Image;
use super::soc::{self, Soc};
use std::rc::Rc;

pub const BL2_BASE: u64 = 0x8000_0000;
pub const BL31_BASE: u64 = 0x8020_0000;
pub const BL32_BASE: u64 = 0x8050_0000;
/// The core the SE starts by default (firmware/se plat_get_ap_bootcore_index).
pub const BOOT_CORE: usize = 10;

/// What a board looks like to the boot stages.
#[derive(Clone)]
pub struct Config {
    pub flash: Rc<Vec<u8>>,
    pub dram_mb: u32,
    /// the eFuses' secure boot enable, as the SE reports it
    pub secure: bool,
    pub rotpk: [u8; 32],
    pub boot_core: usize,
    /// the DDR channels the SE trained (0xf: all four)
    pub channel_mask: u8,
    /// the SPI NOR's JEDEC ID
    pub jedec: [u8; 3],
    /// device registers as something before BL2 left them: (address, value)
    pub presets: Vec<(u64, u32)>,
    /// the PM and the SE answer their mailboxes
    pub pm_alive: bool,
    pub se_alive: bool,
    /// the xSPI controller never gets idle
    pub xspi_stuck: bool,
}

#[derive(Clone)]
pub struct Machine {
    pub cpus: Vec<Cpu>,
    pub soc: Soc,
    pub image: Rc<Image>,
    pub audit: bool,
}

/// Cores 0-3 are Cortex-A520, 4-11 Cortex-A720; MPIDR: Aff1 is the core, MT set.
pub fn new_cpu(core: usize, image: &Image, audit: bool) -> Cpu {
    let mut c = Cpu::new(core);
    c.sys.insert(cpu::MPIDR_EL1, 0x8100_0000 | ((core as u64) << 8));
    c.sys.insert(cpu::MIDR_EL1, if core < 4 { 0x410f_d801 } else { 0x410f_d811 });
    c.cov_base = image.base;
    c.cov = vec![0; ((image.text_end - image.base) as usize / 4 + 63) / 64];
    if audit {
        c.audit = Some(cpu::Audit::default());
    }
    c
}

impl Machine {
    /// The machine as the SE leaves it for an image at its entry on the boot core.
    pub fn new(image: Rc<Image>, cfg: &Config, audit: bool) -> Machine {
        let mut soc = Soc::new(cfg.flash.clone(), cfg.dram_mb);
        soc::se_handover(&mut soc, cfg.dram_mb, cfg.secure, &cfg.rotpk, cfg.channel_mask);
        soc.jedec = cfg.jedec;
        soc.pm_alive = cfg.pm_alive;
        soc.se_alive = cfg.se_alive;
        soc.xspi_stuck = cfg.xspi_stuck;
        // the SE's harvest word: [11:0] cores the eFuses leave out, [15:12] the boot core
        soc.set_reg(0x1600_0508, (cfg.boot_core as u32) << 12);
        for (addr, v) in &cfg.presets {
            soc.set_reg(*addr, *v);
        }
        for (addr, data) in &image.chunks {
            soc.ram.load(*addr, data);
        }
        let mut cpus: Vec<Cpu> = (0..soc::CORES).map(|i| new_cpu(i, &image, audit)).collect();
        cpus[cfg.boot_core].pc = image.entry;
        Machine { cpus, soc, image, audit }
    }

    pub fn run(&mut self, core: usize, limit: u64, stops: &[u64]) -> Stop {
        if self.soc.watch == 0 {
            return self.cpus[core].run(&mut self.soc, limit, stops);
        }
        // one instruction at a time, to say who touches the watched register
        let end = self.cpus[core].insns.saturating_add(limit);
        while self.cpus[core].insns < end {
            let pc = self.cpus[core].pc;
            if stops.contains(&pc) {
                return Stop::Break(pc);
            }
            if let Err(stop) = self.cpus[core].step(&mut self.soc) {
                return stop;
            }
            if let Some((write, v)) = self.soc.watch_hit.take() {
                eprintln!("  {}: core {core} {} {:#x} {} {v:#x} at {} (lr {})", self.image.name, if write { "writes" } else { "reads" }, self.soc.watch, if write { "<-" } else { "->" }, self.image.at(pc), self.image.at(self.cpus[core].x[30]));
            }
        }
        Stop::Limit
    }

    /// Where a stop happened, with the image's names.
    pub fn describe(&self, core: usize, stop: &Stop) -> String {
        let c = &self.cpus[core];
        let at = |a: u64| self.image.at(a);
        match stop {
            Stop::Fault { pc, addr, what } => format!("fault at {}: {what} {addr:#x} (lr {})", at(*pc), at(c.x[30])),
            Stop::Undefined { pc, insn, why } => format!("undefined at {}: {insn:08x} ({why})", at(*pc)),
            Stop::Hang(pc) => format!("hang at {} (lr {})", at(*pc), at(c.x[30])),
            Stop::Wfi(pc) => format!("wfi at {} (lr {})", at(*pc - 4), at(c.x[30])),
            Stop::Break(pc) => format!("reached {}", at(*pc)),
            Stop::Eret => format!("eret to EL{} at {:#x}", c.el, c.pc),
            Stop::Limit => format!("instruction limit at {} (lr {})", at(c.pc), at(c.x[30])),
        }
    }

    pub fn cov_bits(&self) -> Vec<u64> {
        let mut bits = vec![0u64; self.cpus[0].cov.len()];
        for c in &self.cpus {
            for (i, w) in c.cov.iter().enumerate() {
                bits[i] |= w;
            }
        }
        bits
    }

    /// The audit's result: returns checked, and the callee-saved registers that changed.
    pub fn audit_report(&self) -> (u64, Vec<String>) {
        let mut n = 0;
        let mut faults = Vec::new();
        for c in &self.cpus {
            if let Some(a) = &c.audit {
                n += a.checked;
                for (callee, text) in &a.faults {
                    faults.push(format!("{}: {text} ({})", self.image.name, self.image.at(*callee)));
                }
            }
        }
        (n, faults)
    }
}

/// Bits (one per instruction word from `base`) as address ranges.
pub fn ranges(base: u64, bits: &[u64]) -> Vec<(u32, u32)> {
    let mut out: Vec<(u32, u32)> = Vec::new();
    for (i, w) in bits.iter().enumerate() {
        if *w == 0 {
            continue;
        }
        for b in 0..64 {
            if w >> b & 1 != 0 {
                let a = (base + 4 * (64 * i as u64 + b)) as u32;
                match out.last_mut() {
                    Some(last) if last.1 == a => last.1 = a + 4,
                    _ => out.push((a, a + 4)),
                }
            }
        }
    }
    out
}
