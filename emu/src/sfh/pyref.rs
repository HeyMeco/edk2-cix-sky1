// SPDX-License-Identifier: BSD-2-Clause-Patent
//! The differential test of the open SFH firmware against CIX's sfh_fw.bin
//! as the Python test this replaces (scripts/open/test_sfh.py, on Unicorn)
//! made it: `sfh-test --pyref`.  Kept to show that the port reproduces the
//! Python reference; the timed tests in test.rs go further.
//!
//! Both images run against a model of the SFH peripherals: the mailbox
//! FIFOs, the timer block (counters read as expired) and the control
//! block.  Each scenario (boot, then the AP suspend, resume and PM
//! wake-after messages, then the timer interrupts) is driven through the
//! image's own interrupt dispatcher (the handler in vector 16) with IPSR
//! set, on a scratch stack, and the writes each image makes to the hardware
//! are compared: the values written to each register, in order per
//! register.

use crate::arm::{Arm, Bus, Io, Mem, Stop};
use std::collections::{BTreeMap, VecDeque};

pub const SRAM: u32 = 0x20000000;
pub const SRAM_SIZE: u32 = 0x80000;
/// fake return address for injected interrupts
const RETURN: u32 = 0x2007f000;
const MBOX: [u32; 4] = [0x50080000, 0x50090000, 0x500a0000, 0x500b0000];

#[derive(Clone, Default)]
struct Model {
    /// channel -> words still in its RX FIFO
    rx: [VecDeque<u32>; 4],
    int_status: [u32; 4],
    timer_status: u32,
}

impl Bus for Model {
    fn read(&mut self, _io: &mut Io, mem: &mut Mem, addr: u32, _size: u32) -> Option<u32> {
        // (the reference put a whole word at the address read, whatever the access's size)
        let mut poke = |v: u32| mem.write(addr, 4, v);
        if !(0x50000000..0x50100000).contains(&addr) {
            return None;
        }
        for (ch, base) in MBOX.iter().enumerate() {
            if addr == base + 0xc8 {
                poke(self.int_status[ch]);
            } else if addr == base + 0xb0 {
                poke(self.rx[ch].pop_front().unwrap_or(0));
            } else if addr == base + 0xb4 {
                poke(if self.rx[ch].is_empty() { 4 } else { 0 });
            }
        }
        if (0x50010000..0x50018000).contains(&addr) {
            match addr & 0xfff {
                0x1c => poke(0), // timer counters: already expired
                0x30 => poke(self.timer_status),
                _ => {}
            }
        }
        None
    }

    fn write(&mut self, _io: &mut Io, _mem: &mut Mem, _addr: u32, _size: u32, _value: u32) {}
}

struct Sfh {
    cpu: Arm<Model>,
    dispatch: u32,
}

impl Sfh {
    fn new(image: &[u8]) -> Sfh {
        let mut cpu = Arm::new(Model::default());
        cpu.add_ram(SRAM, SRAM_SIZE);
        // Address 0 aliases the start of SRAM.  CIX's image logs through a UART pointer that stays 0
        // once logging is on (after resume): it reads the vector table as the flag register, sees "TX
        // full" and gives up after 10000 polls per character, writing nothing.
        cpu.mem.add_ram(0, 0x1000);
        cpu.write_bytes(0, &image[..image.len().min(0x1000)]);
        cpu.write_bytes(SRAM, image);
        cpu.scs_reads = false;
        cpu.systick_jump = false;
        cpu.deliver = false;
        let dispatch = crate::util::u32le(image, 16 * 4);
        Sfh { cpu, dispatch }
    }

    fn run(&mut self, pc: u32) -> Result<(), String> {
        self.cpu.r[15] = pc & !1;
        for _ in 0..20_000_000u32 {
            let pc = self.cpu.r[15];
            // the fake return address, or an instruction branching to itself: the idle loop
            if pc == RETURN || self.cpu.mem.peek(pc, 2) == 0xe7fe {
                return Ok(());
            }
            match self.cpu.step() {
                Ok(()) | Err(Stop::Wfi) => {}
                Err(e) => return Err(format!("emulation error at {:#x}: {e}", self.cpu.r[15])),
            }
        }
        Ok(())
    }

    fn boot(&mut self) -> Result<(), String> {
        self.cpu.r[13] = self.cpu.read32(SRAM);
        self.run(self.cpu.read32(SRAM + 4))
    }

    fn irq(&mut self, irq: u32) -> Result<(), String> {
        self.cpu.ipsr = 16 + irq;
        self.cpu.r[13] = 0x2007e000;
        self.cpu.r[14] = RETURN | 1;
        let r = self.run(self.dispatch);
        self.cpu.ipsr = 0;
        r
    }
}

enum Scenario {
    Boot,
    Message(usize, Vec<u32>),
    Timer(u32),
}

fn scenarios() -> Vec<(String, Scenario)> {
    let mut v = vec![
        ("boot".to_string(), Scenario::Boot),
        ("AP: suspend, wake after 2 s".into(), Scenario::Message(1, vec![0x0c, 0xfff2, 2])),
        ("AP: resume".into(), Scenario::Message(1, vec![0x0c, 0xfff4, 0])),
        ("PM: wake after 5 ms".into(), Scenario::Message(3, vec![0x0c, 0xfff7, 5])),
        ("AP: unknown command".into(), Scenario::Message(1, vec![0x0c, 0xfff9, 0])),
        ("AP: short message".into(), Scenario::Message(1, vec![0x08, 0xfff2])),
    ];
    for n in 16..22 {
        v.push((format!("timer interrupt {n}"), Scenario::Timer(n)));
    }
    v
}

fn step(s: &mut Sfh, sc: &Scenario) -> Result<Vec<(u32, u32)>, String> {
    let mark = s.cpu.writes.len();
    match sc {
        Scenario::Boot => s.boot()?,
        Scenario::Message(ch, words) => {
            s.cpu.bus.rx[*ch] = words.iter().copied().collect();
            s.cpu.bus.int_status[*ch] = 0x14;
            s.irq(23 + *ch as u32)?;
            s.cpu.bus.int_status[*ch] = 0;
        }
        Scenario::Timer(n) => {
            s.cpu.bus.timer_status = 1;
            s.irq(*n)?;
            s.cpu.bus.timer_status = 0;
        }
    }
    Ok(s.cpu.writes[mark..].iter().map(|w| (w.addr, w.value)).collect())
}

fn per_register(writes: &[(u32, u32)]) -> BTreeMap<u32, Vec<u32>> {
    let mut regs: BTreeMap<u32, Vec<u32>> = BTreeMap::new();
    for (addr, value) in writes {
        regs.entry(*addr).or_default().push(*value);
    }
    regs
}

pub fn main(open_img: &[u8], cix_img: &[u8]) -> Result<i32, String> {
    let (mut a, mut b) = (Sfh::new(cix_img), Sfh::new(open_img));
    let mut failed = 0;
    for (name, sc) in scenarios() {
        let (wa, wb) = (step(&mut a, &sc)?, step(&mut b, &sc)?);
        let (ra, rb) = (per_register(&wa), per_register(&wb));
        if ra == rb {
            let order = if wa == wb { "same order" } else { "same writes per register" };
            println!("ok   {name}: {} writes, {order}", wa.len());
            continue;
        }
        failed += 1;
        println!("FAIL {name}");
        let hexes = |v: Option<&Vec<u32>>| {
            let v: Vec<String> = v.map(|v| v.iter().take(8).map(|x| format!("'{x:#x}'")).collect()).unwrap_or_default();
            format!("[{}]", v.join(", "))
        };
        let all: std::collections::BTreeSet<u32> = ra.keys().chain(rb.keys()).copied().collect();
        for reg in all {
            if ra.get(&reg) != rb.get(&reg) {
                println!("     {reg:#010x}: CIX {} open {}", hexes(ra.get(&reg)), hexes(rb.get(&reg)));
            }
        }
    }
    if failed == 0 {
        println!("all scenarios match");
    } else {
        println!("{failed} scenario(s) differ");
    }
    Ok((failed != 0) as i32)
}
