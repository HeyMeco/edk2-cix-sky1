// SPDX-License-Identifier: BSD-2-Clause-Patent
//! An SE image (CIX's debug build or the open one) on the model of soc.rs,
//! driven the way the Python emulator this replaces (se_emu.py, on Unicorn)
//! drove it, so that its results can be reproduced to the write:
//!
//! * the run is cut into translation blocks as QEMU cuts them; stops,
//!   hooks and stubs act at a block's start, and so does an interrupt: a
//!   line that is up and enabled, with PRIMASK clear, has its handler
//!   called on a scratch stack with IPSR set and the registers put back
//!   afterwards (no exception frame; no nesting);
//! * SysTick's COUNTFLAG always reads set (a delay takes no time), WFI
//!   returns at once, registers of the system control space read as
//!   memory;
//! * a block run `poll_limit` times with no bus write in between ends the
//!   run: the firmware is polling a register the model does not drive.
//!
//! Time does not exist in this mode.  The scenarios that need it (and real
//! exception entry) are in timed.rs.

use super::soc::{SkySoc, SRAM, SRAM_SIZE};
use crate::arm::{Arm, Stop, Stub};
use std::rc::Rc;

/// fake return address for injected interrupts and calls
pub const IRQ_RETURN: u32 = 0x2007c000;
/// their scratch stack
pub const SCRATCH_SP: u32 = 0x2006f000;

pub struct Se {
    pub cpu: Arm<SkySoc>,
    /// why the last run ended: a stop's name, or the write that ended it
    pub stopped: Option<String>,
    /// (function, block, last register read) when the run ended polling
    pub poll: Option<(String, u32, Option<u32>)>,
    pub poll_limit: u32,
    /// per block: (bus writes when last seen, runs since)
    blocks: Vec<(u32, u32)>,
    pub wfi: u64,
    in_irq: bool,
    take_irq: Option<u32>,
    pub irqs_taken: u32,
    /// function entries in order, when tracing
    pub trace: bool,
    pub calls: Vec<String>,
}

impl Se {
    /// Load `elf` into SRAM with the state the boot ROM leaves; `stops` and `stubs` by symbol name
    /// (names the image has not got are left out).
    pub fn new(elf: &str, soc: SkySoc, stubs: &[(&str, Stub<SkySoc>)], stops: &[&str]) -> Result<Se, String> {
        let mut cpu = Arm::new(soc);
        cpu.add_ram(SRAM, SRAM_SIZE);
        cpu.load_elf(elf)?;
        cpu.scs_reads = false;
        cpu.systick_jump = false;
        cpu.wfi_stops = true;
        cpu.deliver = false;
        let soc = cpu.bus.clone();
        soc.reset_state(&mut cpu.mem);
        for (name, f) in stubs {
            if let Some(a) = cpu.sym(name) {
                cpu.set_stub(a, f.clone());
            }
        }
        for name in stops.iter().chain(["service_idle_entry"].iter()) {
            if let Some(a) = cpu.sym(name) {
                cpu.set_stop(a, name);
            }
        }
        Ok(Se {
            cpu,
            stopped: None,
            poll: None,
            poll_limit: 200_000,
            blocks: vec![(0, 0); (SRAM_SIZE / 2) as usize],
            wfi: 0,
            in_irq: false,
            take_irq: None,
            irqs_taken: 0,
            trace: false,
            calls: Vec::new(),
        })
    }

    pub fn pc_func(&self) -> String {
        self.cpu.func(self.cpu.r[15])
    }

    /// The lowest external interrupt that is up and enabled.
    fn ready_irq(&self) -> Option<u32> {
        (0..8).find_map(|w| {
            let m = self.cpu.io.lines[w];
            (0..64).find(|b| m >> b & 1 != 0 && self.cpu.irq_enabled(64 * w as u32 + b)).map(|b| 64 * w as u32 + b)
        })
    }

    /// Run from `pc` until something ends the run (Unicorn's emu_start), for at most `count` instructions.
    fn start(&mut self, pc: u32, count: u64) -> Result<(), String> {
        self.cpu.r[15] = pc & !1;
        self.cpu.block_start = true;
        let end = self.cpu.count.saturating_add(count);
        loop {
            if self.cpu.block_start {
                let addr = self.cpu.r[15];
                if self.cpu.count >= end {
                    return Ok(());
                }
                if !self.in_irq && !self.cpu.primask && self.cpu.io.lines.iter().any(|w| *w != 0) {
                    if let Some(n) = self.ready_irq() {
                        self.take_irq = Some(n);
                        return Ok(());
                    }
                }
                let (stop, stub, hook) = self.cpu.marks(addr);
                if stop {
                    self.stopped = Some(self.cpu.stops[&addr].clone());
                    return Ok(());
                }
                if hook {
                    let h = self.cpu.hooks[&addr].clone();
                    h(&mut self.cpu);
                }
                if stub {
                    let f = self.cpu.stubs[&addr].clone();
                    let r0 = f(&mut self.cpu);
                    self.cpu.ret(r0).map_err(|e| e.to_string())?;
                    continue;
                }
                if self.trace {
                    let name = self.cpu.func(addr);
                    if self.cpu.sym(&name) == Some(addr) {
                        self.calls.push(name);
                    }
                }
                if let Some(b) = self.blocks.get_mut((addr.wrapping_sub(SRAM) / 2) as usize) {
                    let gen = self.cpu.writes.len() as u32;
                    *b = if b.0 == gen && b.1 != 0 { (gen, b.1 + 1) } else { (gen, 1) };
                    if b.1 > self.poll_limit {
                        self.poll = Some((self.cpu.func(addr), addr, self.cpu.last_read));
                        return Ok(());
                    }
                }
            }
            self.cpu.pass_stop = true;
            match self.cpu.step() {
                Ok(()) => {}
                Err(Stop::Wfi) => self.wfi += 1,
                Err(Stop::Bus(why)) => {
                    self.stopped = Some(why);
                    return Ok(());
                }
                Err(e) => {
                    let pc = self.cpu.r[15];
                    return Err(format!("emulation error at {pc:#x} ({}): {e}", self.cpu.func(pc)));
                }
            }
        }
    }

    /// Take external interrupt `n`: run the vector-table handler with IPSR set, on a scratch stack,
    /// until it returns.  True if it did.
    pub fn irq(&mut self, n: u32, count: u64) -> Result<bool, String> {
        let handler = self.cpu.read32(SRAM + (16 + n) * 4);
        let saved = self.cpu.context();
        self.cpu.ipsr = 16 + n;
        self.cpu.r[13] = SCRATCH_SP;
        self.cpu.r[14] = IRQ_RETURN | 1;
        self.cpu.set_stop(IRQ_RETURN, "irq return");
        self.stopped = None;
        self.in_irq = true;
        let r = self.start(handler, count);
        self.in_irq = false;
        self.cpu.del_stop(IRQ_RETURN);
        self.cpu.set_context(&saved);
        r?;
        Ok(self.stopped.as_deref() == Some("irq return"))
    }

    /// Call a firmware function with up to four word arguments on a scratch stack and return its r0
    /// (the registers are put back afterwards).
    pub fn call(&mut self, name: &str, args: &[u32], count: u64) -> Result<u32, String> {
        let saved = self.cpu.context();
        let stops: Vec<(u32, String)> = self.cpu.stops.iter().map(|(a, n)| (*a, n.clone())).collect();
        for (a, _) in &stops {
            self.cpu.del_stop(*a);
        }
        for (i, v) in args.iter().enumerate() {
            self.cpu.r[i] = *v;
        }
        self.cpu.r[13] = SCRATCH_SP;
        self.cpu.r[14] = IRQ_RETURN | 1;
        self.cpu.set_stop(IRQ_RETURN, "return");
        self.stopped = None;
        let r = self.start(self.cpu.addr(name), count);
        let r0 = self.cpu.r[0];
        self.cpu.del_stop(IRQ_RETURN);
        for (a, n) in &stops {
            self.cpu.set_stop(*a, n);
        }
        self.cpu.set_context(&saved);
        r?;
        if self.stopped.as_deref() != Some("return") {
            return Err(format!("{name}() did not return"));
        }
        Ok(r0)
    }

    /// Carry on from where the last run stopped.
    pub fn resume(&mut self, count: u64) -> Result<Option<String>, String> {
        self.stopped = None;
        self.poll = None;
        self.blocks.iter_mut().for_each(|b| *b = (0, 0));
        self.run(Some(self.cpu.r[15]), count)
    }

    /// Run from reset (pc None) or from `pc`, taking interrupts as their lines come up; what stopped it.
    pub fn run(&mut self, pc: Option<u32>, count: u64) -> Result<Option<String>, String> {
        let mut pc = match pc {
            Some(pc) => pc,
            None => {
                self.cpu.reset(SRAM);
                self.cpu.r[15]
            }
        };
        loop {
            self.start(pc, count)?;
            let Some(n) = self.take_irq.take() else { return Ok(self.stopped.clone()) };
            // an interrupt line came up: take it, then carry on where the code was
            pc = self.cpu.r[15];
            self.irqs_taken += 1;
            if !self.irq(n, 20_000_000)? {
                return Err(format!("interrupt {n}: the handler did not return ({})", self.why()));
            }
            self.stopped = None;
        }
    }

    /// How the last run ended, for reports.
    pub fn why(&self) -> String {
        match (&self.stopped, &self.poll) {
            (Some(s), _) => s.clone(),
            (None, Some(p)) => format!("polling in {}", p.0),
            _ => self.pc_func(),
        }
    }
}

/// A stub that returns a constant.
pub fn constant(v: u32) -> Stub<SkySoc> {
    Rc::new(move |_| Some(v))
}
