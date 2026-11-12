// SPDX-License-Identifier: BSD-2-Clause-Patent
//! The open BL2 and BL31 against CIX's (`a64-test`).
//!
//!   sky1emu a64-test [bl2] [bl31] [--flash FILE] [--only TEXT] [--audit] [--cov FILE] [-v] ...
//!
//! Both images of a stage run on the same model (soc.rs) from the same
//! start, and what each leaves is compared item by item:
//!
//! * BL2, from its entry as the SE starts it to the jump to BL31: every
//!   device register's writes in order, the SPI transactions, the console,
//!   what it hands BL31 (the image list with entry points and arguments,
//!   the registers, PSTATE and the system registers), the images in DRAM
//!   (their hashes) and every other page of DRAM it changed;
//! * BL31's cold boot, from that hand-over (the stage under test put in
//!   BL31's place) through OP-TEE's entry to the jump to UEFI: the same
//!   items, what OP-TEE and UEFI are entered with;
//! * BL31's run time: every SMC asked of both from copies of the booted
//!   machines, the lower exception levels played by the harness (UEFI/the
//!   OS issue the SMCs, OP-TEE answers the dispatcher's entries and makes
//!   its own calls; started cores come out of reset at the address BL31
//!   programmed).  Per question: the registers the caller gets back, device
//!   writes, messages to the PM and the SE, core power requests.
//!
//! A question that kills an image (a fault, a panic, a hang) puts both
//! machines back as they were before it.  What differs is looked up in
//! EXPECTED (expected.rs), each entry with its reason; anything else
//! counts as a difference and fails the test.

use super::cpu::{self, Stop};
use super::elf64::Image;
use super::images;
use super::machine::{self, Config, Machine, BL2_BASE, BL31_BASE, BL32_BASE};
use super::soc::{self, Io};
use crate::util::{self, Args};
use std::collections::BTreeMap;
use std::rc::Rc;

pub const OPEN_BL2: &str = "out/emu/a64/bl2/sky1/release/bl2/bl2.elf";
pub const OPEN_BL31: &str = "out/emu/a64/bl31/sky1/release/bl31/bl31.elf";
/// CIX's stages: its reference bootloaders (edk2-non-osi, the build of 2026-07-15 the open ones were
/// written from); `--cix-bl1`, `--cix-fip` take another vendor's (the Orion O6's are a month older)
pub const CIX_BL1: &str = "edk2-non-osi/Platform/CIX/Sky1/PackageTool/Firmwares/bootloader1.img";
pub const CIX_FIP: &str = "edk2-non-osi/Platform/CIX/Sky1/PackageTool/Firmwares/bootloader2.img";
/// names for the vendor's functions, where out/emu/a64-re.sh made them: <commit>-bl2.syms, <commit>-bl31.syms
pub const CIX_NAMES: &str = "out/emu/a64/re";
/// A flash image with every stage's certificates under one root, which both BL2s boot.
pub const OPEN_FLASH: &str = "dist/orion-o6_2026Q3_open.bin";
/// The same board's image with the vendor's boot firmware (when it is there: one more BL2 scenario).
pub const VENDOR_FLASH: &str = "dist/orion-o6_2026Q3.bin";

pub const BUDGET: u64 = 3_000_000_000;
/// One SMC's budget: a call that takes longer hangs.
pub const CALL_BUDGET: u64 = 400_000_000;
/// Where the harness's OP-TEE says its vector table is (nine entries of four bytes).
pub const TEE_VECTORS: u64 = BL32_BASE + 0x1000;
/// Where the harness's normal world has the cores it starts enter.
pub const NS_ENTRY: u64 = 0x9200_0000;

pub struct Expected {
    pub phase: &'static str,
    pub what: &'static str,
    pub cix: &'static str,
    pub open: &'static str,
    pub why: &'static str,
}

#[derive(Clone, Default)]
pub struct Phase {
    pub name: String,
    pub items: Vec<(String, String)>,
    /// instructions the phase took, where that is worth saying (BL2: not compared)
    pub insns: u64,
}

impl Phase {
    pub fn new(name: &str) -> Phase {
        Phase { name: name.to_string(), items: Vec::new(), insns: 0 }
    }

    pub fn put(&mut self, what: impl Into<String>, value: impl Into<String>) {
        self.items.push((what.into(), value.into()));
    }
}

/// A machine with one side's images, and how much of its logs has been reported.
#[derive(Clone)]
pub struct World {
    pub m: Machine,
    console: usize,
    pm: usize,
    se: usize,
    ops: usize,
    power: usize,
    sgis: usize,
    acks: usize,
    /// cores that are in EL3's WFI with their power down requested
    pub off: Vec<bool>,
    /// where the last exception to EL3 was taken from: PC, EL, PSTATE
    from: (u64, u32, u64),
    tee_from: (u64, u32, u64),
}

pub fn name_of(addr: u64) -> String {
    const NAMES: &[(u64, u64, &str)] = &[
        (0x0001_0000, 0x1000, "xSPI slave DMA"),
        (0x040d_0000, 0x1_0000, "UART"),
        (0x0416_0000, 0x1_0000, "FCH control"),
        (0x0418_0000, 0x1_0000, "xSPI"),
        (0x0504_0000, 0x1_0000, "PUB_APB"),
        (0x0508_0000, 0x1_0000, "SE mailbox"),
        (0x065b_0000, 0x1_0000, "PM mailbox"),
        (0x0e01_0000, 0x1_0000, "GICD"),
        (0x0e05_0000, 0x2_0000, "GIC ITS"),
        (0x0f03_0000, 0x1_0000, "cluster PPU"),
        (0x0fc5_0000, 0x1_0000, "DSU RCSU"),
        (0x1600_0000, 0x1000, "PMCTRL_S5"),
        (0x1600_2000, 0x1000, "system counter"),
        (0x1600_7000, 0x1000, "IOMUX_S5"),
    ];
    if let Some(n) = NAMES.iter().find(|n| addr >= n.0 && addr < n.0 + n.1) {
        return n.2.to_string();
    }
    if (soc::TZC..soc::TZC + 0x8_0000).contains(&addr) {
        return format!("TZC-400 {}", (addr - soc::TZC) / 0x2_0000);
    }
    if (soc::GICR..soc::GICR + 0x30_0000).contains(&addr) {
        return format!("GICR {}", (addr - soc::GICR) / 0x4_0000);
    }
    if (soc::PPU_CORE..soc::PPU_CORE + 0xc0_0000).contains(&addr) && (addr - soc::PPU_CORE) % 0x10_0000 < 0x1_0000 {
        return format!("core PPU {}", (addr - soc::PPU_CORE) / 0x10_0000);
    }
    String::new()
}

impl World {
    pub fn new(m: Machine) -> World {
        World { m, console: 0, pm: 0, se: 0, ops: 0, power: 0, sgis: 0, acks: 0, off: vec![false; soc::CORES], from: (0, 0, 0), tee_from: (0, 0, 0) }
    }

    /// What the machine did since the last time: one item per register
    /// written (its values in order), the SPI transactions, the messages,
    /// the console.
    pub fn drain(&mut self, into: &mut Phase, prefix: &str) {
        let s = &mut self.m.soc;
        let io: Vec<Io> = std::mem::take(&mut s.io);
        let mut writes: BTreeMap<u64, Vec<String>> = BTreeMap::new();
        for a in &io {
            if !a.write || a.addr == soc::UART || (soc::XSPI_SDMA..soc::XSPI_SDMA + 0x1000).contains(&a.addr) {
                continue;
            }
            let v = if a.size == 4 { format!("{:#x}", a.val) } else { format!("{:#x}/{}", a.val, a.size) };
            writes.entry(a.addr).or_default().push(v);
        }
        for (addr, v) in writes {
            let n = name_of(addr);
            let what = if n.is_empty() { format!("{prefix}register {addr:#010x}") } else { format!("{prefix}register {addr:#010x} ({n})") };
            into.put(what, v.join(" "));
        }
        if s.xspi.ops.len() > self.ops {
            let ops: Vec<String> = s.xspi.ops[self.ops..]
                .iter()
                .map(|o| if o.2 == 0 && o.3 == 0 { format!("{:02x}", o.0) } else { format!("{:02x} @{:#x}/{}{}{:#x}", o.0, o.1, o.2, if o.4 { " <- " } else { " -> " }, o.3) })
                .collect();
            into.put(format!("{prefix}SPI"), ops.join("; "));
            self.ops = s.xspi.ops.len();
        }
        if s.pm_msgs.len() > self.pm {
            let v: Vec<String> = s.pm_msgs[self.pm..]
                .iter()
                .map(|m| {
                    let args: Vec<String> = m[2..].iter().map(|x| format!("{x:#x}")).collect();
                    if m[0] == 0x0c {
                        format!("PM message {:#x}[{}]", m[1] & 0xffff, args.join(" "))
                    } else {
                        format!("SCMI {:#x}/{}[{}]", (m[1] >> 10) & 0xff, m[1] & 0xff, args.join(" "))
                    }
                })
                .collect();
            into.put(format!("{prefix}to the PM"), v.join("; "));
            self.pm = s.pm_msgs.len();
        }
        if s.se_msgs.len() > self.se {
            let v: Vec<String> = s.se_msgs[self.se..].iter().map(|m| m.iter().map(|x| format!("{x:#x}")).collect::<Vec<_>>().join(" ")).collect();
            into.put(format!("{prefix}to the SE"), v.join("; "));
            self.se = s.se_msgs.len();
        }
        if s.power.len() > self.power {
            let v: Vec<String> = s.power[self.power..].iter().map(|p| format!("core {} {:#x}", p.0, p.1)).collect();
            into.put(format!("{prefix}core power"), v.join("; "));
            self.power = s.power.len();
        }
        if s.sgis.len() > self.sgis {
            let v: Vec<String> = s.sgis[self.sgis..].iter().map(|p| format!("core {} {} {:#x}", p.0, cpu::sysreg_name(p.1), p.2)).collect();
            into.put(format!("{prefix}SGIs"), v.join("; "));
            self.sgis = s.sgis.len();
        }
        if s.acks.len() > self.acks {
            let v: Vec<String> = s.acks[self.acks..].iter().map(|p| format!("core {} {} {}", p.0, if p.2 { "ends" } else { "takes" }, p.1)).collect();
            into.put(format!("{prefix}interrupts"), v.join("; "));
            self.acks = s.acks.len();
        }
        let text = s.console(self.console);
        self.console = s.uart.len();
        let text = text.replace('\r', "");
        if !text.trim().is_empty() {
            into.put(format!("{prefix}console"), normalize(&text.trim_end().replace('\n', " | ")));
        }
    }

    pub fn ns(&self, core: usize) -> bool {
        self.m.cpus[core].sysreg(cpu::SCR_EL3) & 1 != 0
    }

    /// Run a core in EL3 until it leaves for the normal world, playing
    /// OP-TEE at every entry BL31 makes into it on the way.  The result:
    /// "returned", "wfi", or how the image died; `tee` gets what OP-TEE was
    /// entered with.
    pub fn resume(&mut self, core: usize, tee: &mut Vec<String>) -> String {
        loop {
            let stop = self.m.run(core, CALL_BUDGET, &[]);
            match stop {
                Stop::Eret => {
                    if self.ns(core) {
                        return "returned".into();
                    }
                    let c = &self.m.cpus[core];
                    let pc = c.pc;
                    let x: Vec<String> = c.x[..8].iter().map(|v| format!("{v:#x}")).collect();
                    let entry = if pc == BL32_BASE {
                        "cold entry".to_string()
                    } else if pc >= TEE_VECTORS && pc < TEE_VECTORS + 36 && pc % 4 == 0 {
                        ["yielding SMC", "fast SMC", "CPU on", "CPU off", "CPU resume", "CPU suspend", "FIQ", "system off", "system reset"][((pc - TEE_VECTORS) / 4) as usize].to_string()
                    } else {
                        return format!("eret to the secure world at {pc:#x}, which is no entry of OP-TEE");
                    };
                    tee.push(format!("{entry}: EL{} pstate {:#x} x0-x7 [{}]", c.el, c.pstate(), x.join(" ")));
                    let x = c.x;
                    // OP-TEE's answer, by its SMC to the dispatcher
                    let reply: [u64; 5] = match entry.as_str() {
                        "cold entry" => [0xbe00_0000, TEE_VECTORS, 0, 0, 0],
                        "CPU on" => [0xbe00_0001, 0, 0, 0, 0],
                        "CPU off" => [0xbe00_0002, 0, 0, 0, 0],
                        "CPU suspend" => [0xbe00_0003, 0, 0, 0, 0],
                        "CPU resume" => [0xbe00_0004, 0, 0, 0, 0],
                        "FIQ" => [0xbe00_0006, 0, 0, 0, 0],
                        "system off" => [0xbe00_0007, 0, 0, 0, 0],
                        "system reset" => [0xbe00_0008, 0, 0, 0, 0],
                        _ => [0xbe00_0005, 0x7ee0_0000 | (x[0] & 0xffff), x[1] ^ 0x1111, x[2].wrapping_add(1), x[3]],
                    };
                    let c = &mut self.m.cpus[core];
                    c.x[..5].copy_from_slice(&reply);
                    c.enter(0x400, 0x5e00_0000);
                }
                Stop::Wfi(_) => return "wfi".into(),
                other => return format!("died: {}", generic(&self.m, core, &other)),
            }
        }
    }

    /// Take an exception to EL3 on a core from where it is: every register a known value, then the
    /// arguments.
    pub fn enter(&mut self, core: usize, vector: u64, esr: u64, args: &[u64]) {
        let c = &mut self.m.cpus[core];
        for (i, v) in c.x.iter_mut().enumerate() {
            *v = 0xc0de_0000_0000_0000 | i as u64;
        }
        c.x[..args.len()].copy_from_slice(args);
        self.from = (c.pc, c.el, c.pstate());
        c.enter(vector, esr);
    }

    /// An SMC from the level the core is in (the normal world's EL2, or OP-TEE).
    pub fn smc(&mut self, core: usize, args: &[u64], tee: &mut Vec<String>) -> String {
        self.enter(core, 0x400, 0x5e00_0000, args);
        self.resume(core, tee)
    }

    /// An SMC OP-TEE makes while BL31 has entered it (the core is in the secure world): what it gets back.
    pub fn secure_smc(&mut self, core: usize, args: &[u64]) -> String {
        let (pc, el) = (self.m.cpus[core].pc, self.m.cpus[core].el);
        self.enter(core, 0x400, 0x5e00_0000, args);
        match self.m.run(core, CALL_BUDGET, &[]) {
            Stop::Eret => {
                let c = &self.m.cpus[core];
                let back = if self.ns(core) { " (in the normal world)" } else if c.pc != pc || c.el != el { " (elsewhere in the secure world)" } else { "" };
                format!("x0-x3 [{:#x} {:#x} {:#x} {:#x}]{back}", c.x[0], c.x[1], c.x[2], c.x[3])
            }
            Stop::Wfi(_) => "wfi".into(),
            other => format!("died: {}", generic(&self.m, core, &other)),
        }
    }

    /// Enter OP-TEE by a yielding SMC from the normal world and stop there: true when the core is in it.
    pub fn into_tee(&mut self, core: usize) -> bool {
        self.enter(core, 0x400, 0x5e00_0000, &[0x3200_0004]);
        let from = self.from;
        let ok = self.m.run(core, CALL_BUDGET, &[]) == Stop::Eret && !self.ns(core);
        self.tee_from = from;
        ok
    }

    /// Leave OP-TEE again: its "call done", back to the normal world's caller.
    pub fn out_of_tee(&mut self, core: usize) -> String {
        let c = &mut self.m.cpus[core];
        c.x[..5].copy_from_slice(&[0xbe00_0005, 0, 0, 0, 0]);
        c.enter(0x400, 0x5e00_0000);
        let mut tee = Vec::new();
        let end = self.resume(core, &mut tee);
        self.from = self.tee_from;
        end
    }

    /// A core comes out of reset at the address BL31 gave its reset vector register, and runs to
    /// the normal world.
    pub fn start(&mut self, core: usize, tee: &mut Vec<String>) -> String {
        let rvb = self.m.soc.reg(soc::CPU_RVB + 8 * core as u64) as u64;
        let mut c = machine::new_cpu(core, &self.m.image, self.m.audit);
        let old = &mut self.m.cpus[core];
        c.cov = std::mem::take(&mut old.cov);
        c.insns = old.insns;
        if old.audit.is_some() {
            c.audit = old.audit.take();
        }
        c.pc = rvb;
        self.m.cpus[core] = c;
        self.off[core] = false;
        let end = self.resume(core, tee);
        let c = &self.m.cpus[core];
        if end == "returned" {
            format!("core {core} enters the normal world: EL{} at {:#x}, pstate {:#x}, x0-x3 [{:#x} {:#x} {:#x} {:#x}]; {}", c.el, c.pc, c.pstate(), c.x[0], c.x[1], c.x[2], c.x[3], self.sysregs(core))
        } else {
            format!("core {core} out of reset at {}: {end}", self.m.image.at(rvb).split(' ').next().unwrap_or("?"))
        }
    }

    /// A core's system registers that are not at their reset values, in one line.
    pub fn sysregs(&self, core: usize) -> String {
        let c = &self.m.cpus[core];
        let reset: BTreeMap<u16, u64> = cpu::SYSREGS.iter().map(|e| (e.1, e.2)).collect();
        let own = [cpu::TTBR0_EL3, cpu::VBAR_EL3, cpu::key(3, 6, 13, 0, 2), cpu::MPIDR_EL1, cpu::MIDR_EL1, cpu::ELR_EL3, cpu::SPSR_EL3, cpu::ESR_EL3];
        let v: Vec<String> = c.sys.iter().filter(|(k, v)| !own.contains(k) && reset.get(k) != Some(v)).map(|(k, v)| format!("{}={v:#x}", cpu::sysreg_name(*k))).collect();
        format!("system registers: {}", v.join(" "))
    }

    /// An interrupt for EL3 (group 0) arrives while the core is in the normal world.
    pub fn interrupt(&mut self, core: usize, intid: u32, tee: &mut Vec<String>) -> String {
        self.m.soc.pending[core] = Some(intid);
        self.enter(core, 0x500, 0, &[]);
        let end = self.resume(core, tee);
        self.m.soc.pending[core] = None;
        end
    }

    /// The registers a caller sees after an SMC: x0-x3, any other that is not what it was, and
    /// where it finds itself if that is not where it called from.
    pub fn result(&self, core: usize, nargs: usize) -> String {
        let c = &self.m.cpus[core];
        let mut s = format!("x0-x3 [{:#x} {:#x} {:#x} {:#x}]", c.x[0], c.x[1], c.x[2], c.x[3]);
        let changed: Vec<String> = (4..31).filter(|&i| i >= nargs && c.x[i] != (0xc0de_0000_0000_0000 | i as u64)).map(|i| format!("x{i}={:#x}", c.x[i])).collect();
        if !changed.is_empty() {
            s.push_str(&format!(", changed: {}", changed.join(" ")));
        }
        if (c.pc, c.el, c.pstate()) != self.from {
            s.push_str(&format!(", at EL{} {:#x} pstate {:#x}", c.el, c.pc, c.pstate()));
        }
        s
    }

    pub fn result_at(&self, core: usize, nargs: usize) -> String {
        self.result(core, nargs)
    }
}

/// How an image died, without what differs between two builds of the same code (addresses).
pub fn generic(m: &Machine, core: usize, stop: &Stop) -> String {
    match stop {
        Stop::Fault { addr, what, pc } => format!("{what} {addr:#x} (in {})", function(m, *pc)),
        Stop::Undefined { why, pc, .. } => format!("undefined instruction: {why} (in {})", function(m, *pc)),
        Stop::Hang(pc) => format!("hang (in {}, called from {})", function(m, *pc), function(m, m.cpus[core].x[30])),
        Stop::Limit => format!("no end after {CALL_BUDGET} instructions (in {})", function(m, m.cpus[core].pc)),
        Stop::Break(pc) => format!("stop at {pc:#x}"),
        Stop::Wfi(_) => "wfi".into(),
        Stop::Eret => "eret".into(),
    }
}

fn function(m: &Machine, pc: u64) -> String {
    let s = m.image.at(pc);
    match s.split_once(|c| c == '+' || c == ' ') {
        Some((name, _)) if !name.starts_with("0x") => name.to_string(),
        _ => "?".into(),
    }
}

pub struct Images {
    pub open_bl2: Rc<Image>,
    pub open_bl31: Rc<Image>,
    pub cix_bl2: Rc<Image>,
    pub cix_bl31: Rc<Image>,
    /// the vendor build's commit ("33527c31abbe")
    pub reference: String,
}

pub struct Opts {
    pub verbose: bool,
    pub audit: bool,
    pub only: Vec<String>,
    pub cov: Option<String>,
    pub cfg: Config,
    /// a flash image in the vendor's layout (its bootloader2, a bootloader3 certified with its tool)
    pub vendor_flash: Option<Rc<Vec<u8>>>,
    /// both stages run, every phase: an expected difference that is not met is worth a note
    pub whole: bool,
    /// say which expected differences were not met (with CIX's own build: the table is written for it)
    pub notes: bool,
}

/// Executed code per image, kept from every copy of a machine.
#[derive(Default)]
pub struct Coverage {
    bits: BTreeMap<String, (u64, Vec<u64>)>,
}

impl Coverage {
    pub fn add(&mut self, m: &Machine) {
        let e = self.bits.entry(m.image.name.clone()).or_insert_with(|| (m.image.base, Vec::new()));
        let bits = m.cov_bits();
        if e.1.len() < bits.len() {
            e.1.resize(bits.len(), 0);
        }
        for (i, w) in bits.iter().enumerate() {
            e.1[i] |= w;
        }
    }

    pub fn save(&self, path: &str) -> Result<(), String> {
        for (name, (base, bits)) in &self.bits {
            crate::cov::save(path, name, &machine::ranges(*base, bits))?;
        }
        Ok(())
    }
}

pub fn load_images(a: &Args) -> Result<Images, String> {
    let open = |opt: &str, default: &str| -> Result<Rc<Image>, String> {
        let p = util::rooted(a.opt(opt).unwrap_or(default));
        Image::load(&p.display().to_string(), 0).map(Rc::new).map_err(|e| format!("{e} (build it: emu/build-open.sh bl2 bl31)"))
    };
    let read = |opt: &str, default: &str| -> Result<Vec<u8>, String> {
        let p = util::rooted(a.opt(opt).unwrap_or(default));
        std::fs::read(&p).map_err(|e| format!("{}: {e}", p.display()))
    };
    let bl1 = read("--cix-bl1", CIX_BL1)?;
    let fip = read("--cix-fip", CIX_FIP)?;
    let bl2 = images::bl1_ap(&bl1)?;
    let bl31 = images::fip_image(&fip, &images::UUID_BL31)?;
    let id = images::commit(&bl31);
    let names = |stage: &str| util::rooted(&format!("{CIX_NAMES}/{id}-{stage}.syms")).display().to_string();
    Ok(Images {
        open_bl2: open("--bl2", OPEN_BL2)?,
        open_bl31: open("--bl31", OPEN_BL31)?,
        cix_bl2: Rc::new(images::raw("cix-bl2", BL2_BASE, bl2, Some(&names("bl2")))?),
        cix_bl31: Rc::new(images::raw("cix-bl31", BL31_BASE, bl31, Some(&names("bl31")))?),
        reference: id,
    })
}

fn sysreg_items(m: &Machine, core: usize, into: &mut Phase, skip: &[u16]) {
    let c = &m.cpus[core];
    let reset: BTreeMap<u16, u64> = cpu::SYSREGS.iter().map(|e| (e.1, e.2)).collect();
    for (k, v) in &c.sys {
        if skip.contains(k) || matches!(*k, cpu::MPIDR_EL1 | cpu::MIDR_EL1) {
            continue;
        }
        if reset.get(k) != Some(v) {
            into.put(format!("system register {}", cpu::sysreg_name(*k)), format!("{v:#x}"));
        }
    }
    into.put("stack pointers EL0-EL2", format!("{:#x} {:#x} {:#x}", c.sp[0], c.sp[1], c.sp[2]));
}

/// The pages of DRAM that differ from `before` (or were written, with no `before`), outside `skip`.
fn memory_items(m: &Machine, before: Option<&Machine>, skip: &[(u64, u64)], into: &mut Phase) {
    for page in m.soc.ram.touched(soc::DRAM_BASE, 0x1_0000_0000) {
        if skip.iter().any(|s| page >= s.0 && page < s.1) {
            continue;
        }
        let now = m.soc.ram.bytes(page, 4096);
        if let Some(b) = before {
            if b.soc.ram.bytes(page, 4096) == now {
                continue;
            }
        }
        if now.iter().all(|&b| b == 0) && before.is_none() {
            continue;
        }
        into.put(format!("DRAM page {page:#x}"), normalize(&describe_page(&now)));
    }
}

/// Text without what names the build: the version after "(release):" and
/// "v2.7-", the time after "Built : ".  Two builds of the same source
/// differ there and it is not compared.
pub fn normalize(text: &str) -> String {
    let mut out = text.to_string();
    for (marker, with) in [("(release):", "<version>"), ("(debug):", "<version>"), ("Built : ", "<time>"), ("\"v2.7-", "<commit>"), ("\"time\":\"0x", "<tick>"), ("PANIC at PC : ", "<address>")] {
        let mut from = 0;
        while let Some(i) = out[from..].find(marker) {
            let start = from + i + marker.len();
            let end = out[start..].find(|c| c == '\\' || c == '|' || c == '"' || c == '\n').map_or(out.len(), |n| start + n);
            out.replace_range(start..end, with);
            from = start + with.len();
        }
    }
    out
}

/// A page as text where it is text, else its hash.
fn describe_page(p: &[u8]) -> String {
    let used: Vec<u8> = p.iter().copied().filter(|&b| b != 0).collect();
    if !used.is_empty() && used.len() > 12 && used.len() < 600 && used.iter().all(|&b| b == b'\n' || b == b'\r' || (0x20..0x7f).contains(&b)) {
        let mut text = String::new();
        let mut at = 0;
        while at < p.len() {
            if p[at] == 0 {
                at += 1;
                continue;
            }
            let end = p[at..].iter().position(|&b| b == 0).map_or(p.len(), |n| at + n);
            text.push_str(&format!("+{at:#x} \"{}\" ", String::from_utf8_lossy(&p[at..end]).replace('\n', "\\n").replace('\r', "")));
            at = end;
        }
        return text.trim_end().to_string();
    }
    if used.len() <= 48 {
        let mut s = String::new();
        for (i, c) in p.chunks(4).enumerate() {
            let w = util::u32le(c, 0);
            if w != 0 {
                s.push_str(&format!("+{:#x}={w:#x} ", 4 * i));
            }
        }
        return s.trim_end().to_string();
    }
    format!("sha256 {}", &util::hex(&util::sha256(p))[..16])
}

// --- BL2 -----------------------------------------------------------------------------

/// The image list BL2 hands on (bl_params_t at x0): per image its id,
/// where it is, its entry point, and the hash of what is there.
fn bl_params(m: &Machine, at: u64, into: &mut Phase) -> Vec<(u64, u64)> {
    let r = |a: u64, n: u32| m.soc.ram.read(a, n);
    let mut ranges = Vec::new();
    if at < soc::DRAM_BASE || at >= m.soc.ram.end {
        into.put("bl_params", format!("x0 = {at:#x} is not in DRAM"));
        return ranges;
    }
    into.put("bl_params header", format!("type {} version {} size {} attr {:#x}", r(at, 1), r(at + 1, 1), r(at + 2, 2), r(at + 4, 4)));
    let mut node = r(at + 8, 8);
    let mut n = 0;
    while node != 0 && n < 8 {
        let (id, info, ep) = (r(node, 4), r(node + 8, 8), r(node + 16, 8));
        let (base, size, max, iattr) = (r(info + 8, 8), r(info + 16, 4), r(info + 20, 4), r(info + 4, 4));
        let args: Vec<String> = (0..8).map(|i| format!("{:#x}", r(ep + 24 + 8 * i, 8))).collect();
        // (BL31's own first argument is the list itself: an address inside BL2)
        let args = if n == 0 { args[1..].join(" ") } else { args.join(" ") };
        into.put(
            format!("image {id} for the next stage"),
            format!("at {base:#x}, {size:#x} of {max:#x} bytes, attributes {iattr:#x}; entry {:#x} spsr {:#x} attributes {:#x}, arguments [{args}]", r(ep + 8, 8), r(ep + 16, 4), r(ep + 4, 4)),
        );
        if size > 0 && base >= soc::DRAM_BASE {
            into.put(format!("image {id} in DRAM"), format!("sha256 {}", util::hex(&util::sha256(&m.soc.ram.bytes(base, size as usize)))));
            ranges.push((base, (base + max + 0xfff) & !0xfff));
        }
        node = r(node + 24, 8);
        n += 1;
    }
    ranges
}

/// Run a BL2 from its entry: the phase, and the machine if it reached BL31.
pub fn run_bl2(image: &Rc<Image>, cfg: &Config, o: &Opts, cov: &mut Coverage, name: &str) -> (Phase, Option<World>) {
    let mut m = Machine::new(image.clone(), cfg, o.audit);
    let core = cfg.boot_core;
    let stop = m.run(core, BUDGET, &[BL31_BASE]);
    cov.add(&m);
    let mut p = Phase::new(name);
    p.insns = m.cpus[core].insns;
    let reached = stop == Stop::Break(BL31_BASE);
    p.put("end", if reached { "jumps to BL31".to_string() } else { format!("stops: {}", generic(&m, core, &stop)) });
    let mut w = World::new(m);
    w.drain(&mut p, "");
    if !reached {
        return (p, None);
    }
    let c = &w.m.cpus[core];
    p.put("x1-x7 at the jump", c.x[1..8].iter().map(|v| format!("{v:#x}")).collect::<Vec<_>>().join(" "));
    p.put("PSTATE at the jump", format!("{:#x}", c.pstate()));
    let x0 = c.x[0];
    sysreg_items(&w.m, core, &mut p, &[]);
    let mut skip = bl_params(&w.m, x0, &mut p);
    // BL2's own memory (code, data, stacks, tables), and what the SE left
    skip.push((BL2_BASE, soc::KM_BLOCK));
    memory_items(&w.m, None, &skip, &mut p);
    (p, Some(w))
}

// --- BL31 ----------------------------------------------------------------------------

/// Put a BL31 where BL2 loaded one and let the boot core go on into it.
pub fn enter_bl31(w: &mut World, image: &Rc<Image>) {
    for (addr, data) in &image.chunks {
        w.m.soc.ram.load(*addr, data);
    }
    w.m.image = image.clone();
    for c in w.m.cpus.iter_mut() {
        c.cov_base = image.base;
        c.cov = vec![0; ((image.text_end - image.base) as usize / 4 + 63) / 64];
        c.flush_tlb();
    }
}

/// BL31's cold boot: to the jump into the normal world, OP-TEE entered and answered on the way.
pub fn cold_boot(w: &mut World, core: usize, name: &str) -> (Phase, bool) {
    let before = w.m.clone();
    let mut p = Phase::new(name);
    let mut tee = Vec::new();
    let end = w.resume(core, &mut tee);
    let ok = end == "returned";
    p.put("end", if ok { "jumps to the normal world".to_string() } else { end });
    for (i, t) in tee.iter().enumerate() {
        p.put(format!("OP-TEE entered ({})", i + 1), t.clone());
    }
    w.drain(&mut p, "");
    if ok {
        let c = &w.m.cpus[core];
        p.put("normal world entered", format!("EL{} at {:#x}, pstate {:#x}, x0-x7 [{}]", c.el, c.pc, c.pstate(), c.x[..8].iter().map(|v| format!("{v:#x}")).collect::<Vec<_>>().join(" ")));
        sysreg_items(&w.m, core, &mut p, &[]);
        // BL31's own memory apart
        memory_items(&w.m, Some(&before), &[(BL31_BASE, BL31_BASE + 0x20_0000)], &mut p);
    }
    (p, ok)
}

/// Both machines, asked the same things.
pub struct Q<'a> {
    pub open: World,
    pub cix: World,
    pub phases: (Phase, Phase),
    pub cov: &'a mut Coverage,
    pub o: &'a Opts,
    pub core: usize,
}

impl<'a> Q<'a> {
    /// Ask both the same thing: `f` returns the answer.  If one dies, both
    /// are put back as they were.
    pub fn ask(&mut self, what: &str, f: &dyn Fn(&mut World, &mut Phase, &str) -> String) {
        // both clocks at the same time, on a millisecond: what a question does with time is then
        // the same in both unless they read the clock a different number of times
        let t = (self.open.m.soc.time.max(self.cix.m.soc.time) / 1_000_000 + 1) * 1_000_000;
        self.open.m.soc.time = t;
        self.cix.m.soc.time = t;
        let before = (self.open.clone(), self.cix.clone());
        let prefix = format!("{what}: ");
        let a = f(&mut self.open, &mut self.phases.0, &prefix);
        let b = f(&mut self.cix, &mut self.phases.1, &prefix);
        self.open.drain(&mut self.phases.0, &prefix);
        self.cix.drain(&mut self.phases.1, &prefix);
        self.phases.0.put(what, a.clone());
        self.phases.1.put(what, b.clone());
        self.cov.add(&self.open.m);
        self.cov.add(&self.cix.m);
        let lost = |s: &str| s.contains("died") || s == "wfi" || s.contains("no entry of OP-TEE");
        if lost(&a) || lost(&b) {
            self.open = before.0;
            self.cix = before.1;
        }
        if self.o.verbose {
            println!("    {what}: {a}{}", if a == b { String::new() } else { format!("   | CIX: {b}") });
        }
    }

    /// Change the model the same way in both.
    pub fn each(&mut self, f: &dyn Fn(&mut World)) {
        f(&mut self.open);
        f(&mut self.cix);
    }

    /// An SMC from the normal world on the phase's core.
    pub fn smc(&mut self, args: &[u64]) {
        self.smc_as("SMC", args)
    }

    pub fn smc_as(&mut self, what: &str, args: &[u64]) {
        let core = self.core;
        let label = format!("{what} {}", args.iter().map(|a| format!("{a:#x}")).collect::<Vec<_>>().join(" "));
        let args = args.to_vec();
        self.ask(&label, &move |w, p, prefix| {
            let mut tee = Vec::new();
            let end = w.smc(core, &args, &mut tee);
            for (i, t) in tee.iter().enumerate() {
                p.put(format!("{prefix}OP-TEE entered ({})", i + 1), t.clone());
            }
            if end == "returned" {
                w.result(core, args.len().max(4))
            } else {
                end
            }
        });
    }
}

pub fn differences(open: &Phase, cix: &Phase) -> Vec<(String, String, String)> {
    let a: BTreeMap<&str, &str> = open.items.iter().map(|i| (i.0.as_str(), i.1.as_str())).collect();
    let b: BTreeMap<&str, &str> = cix.items.iter().map(|i| (i.0.as_str(), i.1.as_str())).collect();
    let mut out = Vec::new();
    let mut seen = std::collections::BTreeSet::new();
    for (what, _) in open.items.iter().chain(cix.items.iter()) {
        if !seen.insert(what.as_str()) {
            continue;
        }
        let (x, y) = (a.get(what.as_str()).copied().unwrap_or("-"), b.get(what.as_str()).copied().unwrap_or("-"));
        if x != y {
            let (x, y) = if is_list(what) { only(x, y) } else { (x.to_string(), y.to_string()) };
            out.push((what.clone(), x, y));
        }
    }
    out
}

/// Items that are sequences (a register's writes, transactions, messages): of two that differ, what
/// each has that the other has not is what is reported and looked up.
fn is_list(what: &str) -> bool {
    what.contains("register 0x") || ["SPI", "to the PM", "to the SE", "core power", "SGIs", "interrupts"].iter().any(|k| what.ends_with(k))
}

fn only(x: &str, y: &str) -> (String, String) {
    let sep = if x.contains("; ") || y.contains("; ") { "; " } else { " " };
    let split = |s: &str| -> Vec<String> {
        if s == "-" {
            Vec::new()
        } else {
            s.split(sep).map(|t| t.to_string()).collect()
        }
    };
    let (a, b) = (split(x), split(y));
    let (mut oa, mut ob) = (Vec::new(), Vec::new());
    for e in util::diff(&a, &b) {
        match e {
            util::Edit::Del(i) => oa.push(a[i].clone()),
            util::Edit::Ins(j) => ob.push(b[j].clone()),
            util::Edit::Same(..) => {}
        }
    }
    let show = |v: Vec<String>, all: usize| if v.is_empty() { format!("nothing more (of {all})") } else { format!("more: {} (of {all})", v.join(sep)) };
    (show(oa, a.len()), show(ob, b.len()))
}

fn short(s: &str, verbose: bool) -> String {
    if verbose || s.len() <= 200 {
        s.to_string()
    } else {
        // the first place the eye needs: head and length
        format!("{}... ({} characters)", &s[..s.char_indices().nth(180).map_or(s.len(), |c| c.0)], s.len())
    }
}

/// Where two long values part, for the report.
fn parting(a: &str, b: &str) -> String {
    let n = a.bytes().zip(b.bytes()).take_while(|(x, y)| x == y).count();
    if n < 60 {
        return String::new();
    }
    let from = (0..=n.saturating_sub(40)).rev().find(|&i| a.is_char_boundary(i)).unwrap_or(0);
    let cut = |s: &str| {
        let end = (n + 80).min(s.len());
        let end = (end..=s.len()).find(|&i| s.is_char_boundary(i)).unwrap_or(s.len());
        s[from..end].to_string()
    };
    format!("\n        they part at character {n}:\n          open ...{}\n          CIX  ...{}", cut(a), cut(b))
}

/// Compare the phases; prints, and returns (differences, expected, kinds).
pub fn report(pairs: &[(Phase, Phase)], expected: &[&Expected], o: &Opts) -> (usize, usize, usize) {
    let (mut bad, mut exp) = (0, 0);
    let mut kinds = std::collections::BTreeSet::new();
    let mut used = vec![false; expected.len()];
    for (open, cix) in pairs {
        let d = differences(open, cix);
        let mut lines = Vec::new();
        let mut n_exp = 0;
        for (what, x, y) in &d {
            let hit = expected.iter().position(|e| open.name.contains(e.phase) && what.contains(e.what) && x.contains(e.open) && y.contains(e.cix));
            match hit {
                Some(i) => {
                    n_exp += 1;
                    used[i] = true;
                    kinds.insert(expected[i].why);
                    if o.verbose {
                        lines.push(format!("    (expected) {what}\n        open {}\n        CIX  {}\n        {}", short(x, false), short(y, false), expected[i].why));
                    }
                }
                None => {
                    bad += 1;
                    lines.push(format!("    DIFFERS {what}\n        open {}\n        CIX  {}{}", short(x, o.verbose), short(y, o.verbose), parting(x, y)));
                }
            }
        }
        exp += n_exp;
        let took = if open.insns > 0 { format!("; instructions: open {:.1} M, CIX {:.1} M", open.insns as f64 / 1e6, cix.insns as f64 / 1e6) } else { String::new() };
        println!("  {}: {} items, {} differ{}{took}", open.name, open.items.len().max(cix.items.len()), d.len() - n_exp, if n_exp > 0 { format!(" ({n_exp} expected)") } else { String::new() });
        for l in lines {
            println!("{l}");
        }
    }
    if o.only.is_empty() && o.whole && o.notes {
        for (i, e) in expected.iter().enumerate() {
            if !used[i] {
                println!("  note: expected difference not met: phase '{}', '{}' ({})", e.phase, e.what, e.why);
            }
        }
    }
    (bad, exp, kinds.len())
}

pub fn wanted(o: &Opts, name: &str) -> bool {
    o.only.is_empty() || o.only.iter().any(|t| name.contains(t.as_str()))
}

pub fn main(rest: &[String]) -> i32 {
    let mut valued = vec!["--bl2", "--bl31", "--cix-bl1", "--cix-fip", "--only", "--cov", "--vendor-flash"];
    valued.extend_from_slice(super::CONFIG_OPTS);
    let a = match Args::parse(rest, &valued) {
        Ok(a) => a,
        Err(e) => {
            eprintln!("{e}");
            return 2;
        }
    };
    if a.check(&["--secure", "--audit", "-v"]).is_err() || a.pos.iter().any(|p| !["bl2", "bl31"].contains(&p.as_str())) {
        eprintln!("usage: sky1emu a64-test [bl2] [bl31] [--flash FILE] [--dram MB] [--core N] [--bl2 ELF] [--bl31 ELF] [--cix-bl1 FILE] [--cix-fip FILE] [--only TEXT] [--audit] [--cov FILE] [-v]");
        return 2;
    }
    let (imgs, cfg) = match load_images(&a).and_then(|i| {
        let mut c = super::config_with(&a, OPEN_FLASH)?;
        if let Some(h) = images::rotpk_of_flash(&c.flash) {
            c.rotpk = h;
        }
        Ok((i, c))
    }) {
        Ok(x) => x,
        Err(e) => {
            eprintln!("a64-test: {e}");
            return 2;
        }
    };
    let vendor_flash = std::fs::read(util::rooted(a.opt("--vendor-flash").unwrap_or(VENDOR_FLASH))).ok().map(Rc::new);
    let o = Opts { verbose: a.flag("-v"), audit: a.flag("--audit"), only: a.all("--only"), cov: a.opt("--cov").map(|s| s.to_string()), cfg, vendor_flash, whole: a.pos.is_empty() || a.pos.len() == 2, notes: imgs.reference == "33527c31abbe" };
    let all = a.pos.is_empty();
    let mut cov = Coverage::default();
    let mut pairs: Vec<(Phase, Phase)> = Vec::new();
    let mut audit: (u64, Vec<String>) = (0, Vec::new());
    let t0 = std::time::Instant::now();
    if all || a.pos.iter().any(|p| p == "bl2") {
        super::bl2::run(&imgs, &o, &mut cov, &mut pairs, &mut audit);
    }
    if all || a.pos.iter().any(|p| p == "bl31") {
        super::bl31::run(&imgs, &o, &mut cov, &mut pairs, &mut audit);
    }
    println!("reference: the vendor's TF-A v2.7-{}", imgs.reference);
    let expected = super::expected::table(&imgs.reference);
    let (bad, exp, kinds) = report(&pairs, &expected, &o);
    if let Some(path) = &o.cov {
        if let Err(e) = cov.save(path) {
            eprintln!("{e}");
        }
    }
    if o.audit {
        println!("audit: {} returns checked, {} with a callee-saved register changed", audit.0, audit.1.len());
        for f in audit.1.iter().take(400) {
            println!("    {f}");
        }
    }
    println!(
        "a64-test: {} phases, {bad} differences, {exp} expected in {kinds} kinds ({:.0} s)",
        pairs.len(),
        t0.elapsed().as_secs_f64()
    );
    (bad > 0 || (o.audit && !audit.1.is_empty())) as i32
}

pub fn audit_add(total: &mut (u64, Vec<String>), m: &Machine) {
    let (n, f) = m.audit_report();
    total.0 += n;
    for x in f {
        if !total.1.contains(&x) {
            total.1.push(x);
        }
    }
}
