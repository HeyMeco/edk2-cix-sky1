// SPDX-License-Identifier: BSD-2-Clause-Patent
//! Compare the open Orion O6 EC firmware (firmware/ec) with Radxa's EC
//! V1.07 in the IT5570 + O6 board model: port of scripts/open/test_ec.py.
//!
//! Each scenario runs both images on the same board and compares what the
//! board sees, in order: pin changes (the EC's outputs), I2C transfers
//! that a device answered, host-command responses (bytes on the wire),
//! e-flash programming. Times must agree within a tolerance; the vendor's
//! probes of devices the O6 does not have (NACKed) and its debug output
//! are not compared. The order is compared as Python's difflib did
//! (seqmatch.rs), so the counts are the Python test's.
//!
//!   sky1emu ec-test [--open IMG] [--vendor IMG] [--flash SPI] [-v] [--dump DIR] [scenario...]

use super::board::{press, soc_off};
use super::chip::{Ec, MC_PER_MS};
use super::seqmatch::{Matcher, Tag};
use crate::util::{hex, hex_sp};

pub const VENDOR: &str = "edk2-project/Platform/Radxa/Orion/O6/Firmwares/ec_fw.bin";
pub const OPEN: &str = "firmware/ec/build/ec_fw.bin";
pub const FLASH: &str = "output/orion-o6/RELEASE/cix_flash_all.bin";

#[derive(Clone, Debug)]
pub struct Cmd {
    pub cmd: u16,
    pub data: Vec<u8>,
    /// data bytes to read back
    pub n: usize,
    /// (gap scenarios) a request built by hand instead of ec_request
    pub raw: Option<Vec<u8>>,
    /// (gap scenarios) the master gives up after this many bus steps
    /// (bytes written, the read's address, bytes read) and sends a STOP
    pub cut: Option<usize>,
}

pub fn cmd(c: u16, d: &[u8], n: usize) -> Cmd {
    Cmd { cmd: c, data: d.to_vec(), n, raw: None, cut: None }
}

/// a request of my own bytes (bad checksums, versions, lengths)
pub fn raw(label: u16, req: &[u8], n: usize) -> Cmd {
    Cmd { cmd: label, data: req.to_vec(), n, raw: Some(req.to_vec()), cut: None }
}

/// A command's answer: its result, its data.
#[derive(Clone, Debug)]
pub struct Res {
    pub cmd: u16,
    pub result: Option<u16>,
    pub rdata: Vec<u8>,
}

/// the SoC side of the host interface: commands in order, results kept
#[derive(Default)]
pub struct Host {
    pub lists: Vec<Vec<Cmd>>,
    pub res: Vec<Res>,
    /// gap between a response and the next request, ms
    pub gap_ms: Option<f64>,
    /// per list: the requests go out at fixed times (start + i * slot)
    /// instead of after each answer, so both images get them at the same
    /// moments however fast each answers
    pub slot: Vec<Option<f64>>,
}

/// CIX's EC host request (protocol 3 over I2C), as UEFI's GetEcInfo
/// builds it: 0xDA, then the header with the command and the length
/// byte-swapped (big-endian on the wire), checksum making the sum 0
pub fn ec_request(cmd: u16, data: &[u8], version: u8) -> Vec<u8> {
    let mut h = vec![3, 0, (cmd >> 8) as u8, cmd as u8, version, 0, (data.len() >> 8) as u8, data.len() as u8];
    h.extend_from_slice(data);
    let s: u32 = h.iter().map(|&b| b as u32).sum();
    h[1] = (0u32.wrapping_sub(s) & 0xFF) as u8;
    let mut o = vec![0xDA];
    o.extend(h);
    o
}

/// (result, data) from the bytes read back: Result, PacketLength, then
/// the 8-byte header (version, checksum, result, length BE, reserved)
/// and the data
pub fn ec_response(raw: &[u8]) -> (Option<u16>, Vec<u8>) {
    if raw.len() < 10 {
        return (None, raw.to_vec());
    }
    let h = &raw[2..10];
    let n = ((h[4] as usize) << 8) | h[5] as usize;
    (Some(((h[2] as u16) << 8) | h[3] as u16), raw[10..(10 + n).min(raw.len())].to_vec())
}

pub fn host_at(ec: &mut Ec, ms: f64, cmds: Vec<Cmd>) {
    let list = ec.host.lists.len();
    ec.host.lists.push(cmds);
    ec.host.slot.push(None);
    ec.at(ms, Box::new(move |ec: &mut Ec| host_go(ec, list, 0)));
}

/// the commands at ms, ms + slot, ms + 2 slot, ... whatever the answers
pub fn host_slots(ec: &mut Ec, ms: f64, slot: f64, cmds: Vec<Cmd>) {
    let list = ec.host.lists.len();
    let n = cmds.len();
    ec.host.lists.push(cmds);
    ec.host.slot.push(Some(slot));
    for i in 0..n {
        ec.at(ms + i as f64 * slot, Box::new(move |ec: &mut Ec| host_go(ec, list, i)));
    }
}

fn host_go(ec: &mut Ec, list: usize, i: usize) {
    let Some(c) = ec.host.lists[list].get(i).cloned() else { return };
    let req = c.raw.clone().unwrap_or_else(|| ec_request(c.cmd, &c.data, 0));
    let done = Box::new(move |ec: &mut Ec, raw: Vec<u8>| {
        let (r, data) = ec_response(&raw);
        ec.host.res.push(Res { cmd: c.cmd, result: r, rdata: data });
        ec.io.ev("resp", format!("{:04X} {} -> {}", c.cmd, hex(&c.data), hex_sp(&raw)));
        if ec.host.slot[list].is_none() {
            let gap = ec.host.gap_ms.unwrap_or(20.0);
            ec.after(gap, Box::new(move |ec: &mut Ec| host_go(ec, list, i + 1)));
        }
    });
    ec.io.slave[1].cut_after = c.cut;
    ec.io.slave_transfer(1, &req, 10 + c.n, Some(done));
}

// ---------------------------------------------------------------- scenarios
pub struct Scenario {
    pub name: &'static str,
    pub doc: &'static str,
    /// set the scenario up; the time to run to (ms)
    pub f: fn(&mut Ec) -> f64,
}

fn heat(ec: &mut Ec, ms: f64, t: u8) {
    ec.at(
        ms,
        Box::new(move |ec: &mut Ec| {
            let b = ec.io.board();
            b.csu.soc_temp = (t, 0);
            b.temps[0] = Some(t as f64);
        }),
    );
}

fn sc_boot(_ec: &mut Ec) -> f64 {
    3000.0
}

fn sc_poweron(ec: &mut Ec) -> f64 {
    press(ec, 2000.0, 300.0);
    16000.0
}

fn sc_shutdown(ec: &mut Ec) -> f64 {
    press(ec, 2000.0, 300.0);
    soc_off(ec, 16000.0);
    21000.0
}

fn sc_g3_button(ec: &mut Ec) -> f64 {
    press(ec, 2000.0, 300.0);
    soc_off(ec, 8000.0);
    press(ec, 14000.0, 300.0);
    20000.0
}

fn sc_short_press(ec: &mut Ec) -> f64 {
    press(ec, 2000.0, 300.0);
    press(ec, 9000.0, 300.0);
    host_at(ec, 10000.0, vec![cmd(0x55, b"", 8), cmd(0x55, b"", 8)]);
    11000.0
}

fn sc_long_press(ec: &mut Ec) -> f64 {
    press(ec, 2000.0, 300.0);
    press(ec, 9000.0, 6000.0);
    16000.0
}

fn sc_ac_policy_on(ec: &mut Ec) -> f64 {
    ec.io.eflash.data[0x1F8FF] = 1;
    4000.0
}

fn sc_ac_policy_erased(ec: &mut Ec) -> f64 {
    ec.io.eflash.data[0x1F8FF] = 0xFF;
    4000.0
}

fn sc_fan(ec: &mut Ec) -> f64 {
    press(ec, 2000.0, 300.0);
    for (i, t) in [45u8, 62, 74, 82, 70, 55, 30].iter().enumerate() {
        heat(ec, 9000.0 + i as f64 * 10000.0, *t);
    }
    80000.0
}

fn sc_fan_modes(ec: &mut Ec) -> f64 {
    press(ec, 2000.0, 300.0);
    host_at(ec, 5000.0, vec![cmd(0x52, &[2], 0)]);
    host_at(ec, 15000.0, vec![cmd(0x52, &[4], 0)]);
    host_at(ec, 25000.0, vec![cmd(0x52, &[1], 0)]);
    36000.0
}

fn sc_fan_rpm(ec: &mut Ec) -> f64 {
    press(ec, 2000.0, 300.0);
    host_at(ec, 5000.0, vec![cmd(0x21, &[0, 0, 0x0f, 0xa0], 0), cmd(0x20, b"", 4)]);
    host_at(ec, 30000.0, vec![cmd(0x20, b"", 4), cmd(0x21, &[0, 0, 0x07, 0xd0], 0)]);
    host_at(ec, 50000.0, vec![cmd(0x20, b"", 4), cmd(0x25, &[0, 0x28], 0), cmd(0x20, b"", 4)]);
    60000.0
}

fn sc_overheat(ec: &mut Ec) -> f64 {
    press(ec, 2000.0, 300.0);
    ec.at(5000.0, Box::new(|ec: &mut Ec| ec.io.board().temps[3] = Some(90.0)));
    16000.0
}

pub fn host_cmds() -> Vec<Cmd> {
    let t: &[(u16, &[u8], usize)] = &[
        (0x3FFF, b"", 32), (0x000B, b"", 16), (0x3E01, b"", 4), (0x0052, b"\x01", 0),
        (0x3E0B, b"\x00", 0), (0x0055, b"", 8), (0x0020, b"", 4),
        (0x0021, b"\x00\x00\x0b\xb8", 0), (0x0020, b"", 4), (0x0026, b"\x00", 4),
        (0x0025, b"\x00\x32", 0), (0x0026, b"\x00", 4), (0x0026, b"\x01", 4),
        (0x0022, b"", 4), (0x0023, b"\x10", 0), (0x0022, b"", 4), (0x002C, b"", 4),
        (0x0031, b"\x01\x02\x03\x04\x05", 0), (0x0032, b"", 8), (0x3E03, b"", 4),
        (0x3E06, b"", 4), (0x3E07, b"", 8), (0x3E09, b"", 4), (0x3E0A, b"", 4),
        (0x3E0C, b"", 4), (0x3E0E, b"\x00", 4), (0x3E0E, b"\x01", 4), (0x3E13, b"", 4),
        (0x3E20, b"", 4), (0x3E21, b"\x01", 0), (0x3E22, b"\x01", 0), (0x0600, b"", 4),
        (0x0601, b"", 4), (0x3E02, b"", 4), (0x3E14, b"", 4), (0x1234, b"", 4),
        (0x0093, b"\x4a", 2), (0x0092, b"\x4a\x00", 0), (0x0093, b"\x4a", 2),
        (0x0092, b"\x4a\x01", 0), (0x0093, b"\x4a", 2), (0x0092, b"\xcb\x01", 0),
        (0x0093, b"\x6b", 2), (0x0092, b"\x6b\x01", 0),
    ];
    t.iter().map(|&(c, d, n)| cmd(c, d, n)).collect()
}

fn sc_host(ec: &mut Ec) -> f64 {
    press(ec, 2000.0, 300.0);
    if ec.io.ext {
        // at fixed times, so both images get each request at the same
        // moment; the GPIO commands 250 ms apart: a write (0x92) is done
        // at the next 100 ms hook and a read (0x93) answers the level
        // sampled 15 ms after the previous read, so closer together the
        // answers depend on each image's hook phase
        let mut c = host_cmds();
        let gpio = c.split_off(36);
        host_slots(ec, 15000.0, 50.0, c);
        host_slots(ec, 16900.0, 250.0, gpio);
        return 19000.0;
    }
    host_at(ec, 15000.0, host_cmds());
    18000.0
}

fn sc_host_standby(ec: &mut Ec) -> f64 {
    host_at(ec, 1500.0, vec![cmd(0x3FFF, b"", 32), cmd(0x3E09, b"", 4), cmd(0x3E0C, b"", 4)]);
    2500.0
}

fn sc_autoflag(ec: &mut Ec) -> f64 {
    press(ec, 2000.0, 300.0);
    host_at(ec, 6000.0, vec![cmd(0x3E0B, &[1], 0), cmd(0x3E0B, &[1], 0), cmd(0x3E0B, &[0], 0)]);
    7000.0
}

fn sc_force_reset(ec: &mut Ec) -> f64 {
    press(ec, 2000.0, 300.0);
    host_at(ec, 6000.0, vec![cmd(0x3E0B, &[1], 0), cmd(0x3E19, &[0], 0)]);
    9000.0
}

pub const SCENARIOS: &[Scenario] = &[
    Scenario { name: "boot", doc: "AC applied: standby, PD set-up", f: sc_boot },
    Scenario { name: "poweron", doc: "standby, button, S0, a while in S0", f: sc_poweron },
    Scenario { name: "shutdown", doc: "S0, OS shutdown: S3, S5 off, G3", f: sc_shutdown },
    Scenario { name: "g3_button", doc: "after a shutdown to G3, the button powers on again", f: sc_g3_button },
    Scenario { name: "short_press", doc: "a short press in S0: mirrored to the SoC, a host event", f: sc_short_press },
    Scenario { name: "long_press", doc: "a 6 s hold in S0: the EC holds the SoC's button", f: sc_long_press },
    Scenario { name: "ac_policy_on", doc: "AC-loss policy 1 in the e-flash: powers on by itself", f: sc_ac_policy_on },
    Scenario {
        name: "ac_policy_erased",
        doc: "AC-loss byte erased (a fresh EC image): powers on by itself too",
        f: sc_ac_policy_erased,
    },
    Scenario { name: "fan", doc: "the SoC heats up and cools down: the fan's states", f: sc_fan },
    Scenario { name: "fan_modes", doc: "fan modes from the host (0x52): mute, performance, auto", f: sc_fan_modes },
    Scenario {
        name: "fan_rpm",
        doc: "a target fan speed (0x21): the duty corrected from the tachometer, the measured speed read back (0x20), then a fixed duty (0x25)",
        f: sc_fan_rpm,
    },
    Scenario { name: "overheat", doc: "a board NTC at 90 C: THERMTRIP_EC", f: sc_overheat },
    Scenario { name: "host", doc: "every host command the vendor answers, with parameters", f: sc_host },
    Scenario { name: "host_standby", doc: "host commands in standby (UEFI is not running, but the bus is)", f: sc_host_standby },
    Scenario { name: "autoflag", doc: "UEFI sets the AC-loss policy (3E0B): the e-flash is programmed", f: sc_autoflag },
    Scenario { name: "force_reset", doc: "UEFI's platform reset (3E19): flag saved, rails off, mirror reset", f: sc_force_reset },
];

// ---------------------------------------------------------------- running
pub fn new_ec(img: &[u8], flash: Option<&[u8]>, cov: bool, ext: bool) -> Ec {
    let mut ec = Ec::new(img, true, true, flash);
    ec.io.ext = ext;
    ec.io.board().csu.real = ext;
    if cov {
        ec.io.cov = Some(vec![0; 0x20000]);
    }
    ec.io.eflash.data[0x1F8FF] = 0; // AC-loss policy: stay off, unless a scenario sets it
    ec
}

pub fn run(img: &[u8], flash: Option<&[u8]>, sc: fn(&mut Ec) -> f64, cov: bool, ext: bool) -> Ec {
    let mut ec = new_ec(img, flash, cov, ext);
    let end = sc(&mut ec);
    ec.run(end);
    ec
}

/// the vendor's I2C bus recovery toggles the PD bus pins as GPIOs (they
/// are in alternate-function mode, so nothing reaches the bus)
const IGNORE_PINS: [&str; 2] = ["A4", "A5"];

/// what the board sees, in order: (ms, key)
pub fn board_events(ec: &Ec, start: f64) -> Vec<(f64, String)> {
    let mut out: Vec<(f64, String)> = Vec::new();
    for e in &ec.io.log.events {
        if e.ms < start {
            continue;
        }
        let d = &e.detail;
        match e.kind {
            "out" => {
                let pin = d.split_whitespace().next().unwrap_or("");
                if IGNORE_PINS.contains(&pin) {
                    continue;
                }
                out.push((e.ms, format!("pin {d}")));
            }
            // (w[ca]: the vendor's scan for a part the O6 lacks, which PD2 happens to acknowledge)
            "i2c" if !d.ends_with("NACK") && !d.ends_with("HANG") && !d.contains("w[ca]") => out.push((e.ms, format!("i2c {d}"))),
            "resp" => {
                // the version string differs by design
                let d = if d.starts_with("3FFF") { format!("{} <version>", &d[..4]) } else { d.clone() };
                out.push((e.ms, format!("host {d}")));
            }
            "eflash" | "spi" => out.push((e.ms, format!("{} {d}", e.kind))),
            _ => {}
        }
    }
    // (extended) the fan's duty as it changes: a write of the value it
    // already has is no event, whatever was written before `start`
    let mut dcr2: Option<u8> = None;
    for &(t, a, v) in &ec.io.reglog {
        let ms = t as f64 / MC_PER_MS as f64;
        if ms < start {
            if a == 0x1804 {
                dcr2 = Some(v);
            }
            continue;
        }
        // the mirror reset: SMFI 1063 = 0x18, the watchdog
        if a == 0x1F01 || a == 0x1F07 || (a == 0x1063 && v == 0x18) {
            out.push((ms, format!("reg {a:04X}={v:02X}")));
        }
        if a == 0x1804 {
            if ec.io.ext && dcr2 == Some(v) {
                continue;
            }
            dcr2 = Some(v);
            out.push((ms, format!("fan DCR2={v:02X}")));
        }
    }
    // the power LED (BLINK_L, PWM1, active low): on, off, or anything
    // between (dimmed, breathing: its ends, 0 and 255 between two dim
    // values within 100 ms, are part of it)
    if ec.io.ext {
        let w: Vec<(f64, u8)> = ec
            .io
            .reglog
            .iter()
            .filter(|r| r.1 == 0x1803)
            .map(|&(t, _, v)| (t as f64 / MC_PER_MS as f64, v))
            .collect();
        let dim = |v: u8| v != 0 && v != 0xFF;
        for (i, &(ms, v)) in w.iter().enumerate() {
            if ms < start {
                continue;
            }
            let mid = i > 0
                && i + 1 < w.len()
                && dim(w[i - 1].1)
                && dim(w[i + 1].1)
                && w[i + 1].0 - w[i - 1].0 < 100.0;
            let s = if dim(v) || mid {
                "dim"
            } else if v == 0 {
                "on"
            } else {
                "off"
            };
            out.push((ms, format!("led {s}")));
        }
    }
    // (extended) after a reset or AC back, the start-up's fan and LED
    // writes (the vendor's turn the fan and LED on for a moment) are not
    // compared either
    if ec.io.ext {
        let boots: Vec<f64> = ec.io.log.events.iter().filter(|e| e.kind == "reset" || (e.kind == "power" && e.detail == "AC restored")).map(|e| e.ms).collect();
        out.retain(|(ms, k)| !((k.starts_with("fan ") || k.starts_with("led ")) && boots.iter().any(|b| *ms >= *b && *ms < *b + 600.0)));
    }
    out.sort_by(|x, y| x.0.partial_cmp(&y.0).unwrap());
    // a register written with the same value again is not an event
    let mut res = Vec::new();
    let mut last_fan: Option<String> = None;
    let mut last_led: Option<String> = None;
    for (ms, key) in out {
        if key.starts_with("fan ") {
            if last_fan.as_deref() == Some(key.as_str()) {
                continue;
            }
            last_fan = Some(key.clone());
        }
        if key.starts_with("led ") {
            if last_led.as_deref() == Some(key.as_str()) {
                continue;
            }
            last_led = Some(key.clone());
        }
        res.push((ms, key));
    }
    res
}

/// Python's '%10.1f'
pub fn f1(x: f64) -> String {
    format!("{x:10.1}")
}

pub type Tol = fn(&str) -> f64;

/// Compare two event lists; prints the result line (and the differences).
/// True if the same.
/// A difference kept on purpose (or a fault of the vendor's image),
/// matched narrowly: the scenario, which side, the event's text, and why.
pub struct Expect {
    /// scenario names it applies to (exact; "*" for all)
    pub scenarios: &'static [&'static str],
    /// "vendor" (only the vendor does it), "open" (only the open one), "time",
    /// "both" (such events compared apart from the rest, any difference)
    pub who: &'static str,
    /// the event's text contains this
    pub key: &'static str,
    pub why: &'static str,
}

fn expected<'a>(table: &'a [Expect], name: &str, who: &str, key: &str) -> Option<&'a Expect> {
    table.iter().find(|e| {
        (e.scenarios.contains(&"*") || e.scenarios.contains(&name)) && (e.who == who.trim() || e.who == "both") && key.contains(e.key)
    })
}

/// Compare two event lists; prints the result line (and the differences).
/// The differences that are in `table` are counted, not printed (with -v
/// they are, marked). Returns (unexpected differences, expected ones by
/// their reason).
/// When the EC was reset or got AC back (ms): times after such a point
/// are compared from it.
pub fn boots(ec: &Ec) -> Vec<f64> {
    ec.io.log.events.iter().filter(|e| e.kind == "reset" || (e.kind == "power" && e.detail == "AC restored")).map(|e| e.ms).collect()
}

#[allow(clippy::too_many_arguments)]
pub fn compare(
    name: &str,
    ve: &[(f64, String)],
    oe: &[(f64, String)],
    tol: Tol,
    verbose: bool,
    table: &[Expect],
    ext: bool,
    vboots: &[f64],
    oboots: &[f64],
) -> (usize, Vec<&'static str>) {
    // (extended) independent parts are streams of their own, each in
    // order, not ordered against the others: the power sequence (pins,
    // e-flash, the reset), the thermal pass (CSU_PM, the fan), the PD
    // controllers, the host interface, the LED
    // events an expectation for both sides covers are a stream of their
    // own too, so that they do not upset how the rest is matched
    let both = |k: &str| {
        table.iter().any(|e| e.who == "both" && (e.scenarios.contains(&"*") || e.scenarios.contains(&name)) && k.contains(e.key))
    };
    let stream = |k: &str| -> usize {
        if ext && both(k) {
            5
        } else if k.starts_with("led ") {
            4
        } else if !ext {
            0
        } else if k.starts_with("i2c C") || k.starts_with("fan ") {
            1
        } else if k.starts_with("i2c F") {
            2
        } else if k.starts_with("host ") {
            3
        } else {
            0
        }
    };
    let mut diffs: Vec<(&str, f64, &str)> = Vec::new();
    let mut late: Vec<(f64, f64, &str)> = Vec::new();
    let mut same = 0;
    for st in 0..6 {
    let ve: Vec<&(f64, String)> = ve.iter().filter(|e| stream(&e.1) == st).collect();
    let oe: Vec<&(f64, String)> = oe.iter().filter(|e| stream(&e.1) == st).collect();
    let vk: Vec<&str> = ve.iter().map(|e| e.1.as_str()).collect();
    let ok_: Vec<&str> = oe.iter().map(|e| e.1.as_str()).collect();
    let sm = Matcher::new(&vk, &ok_);
    let ops = sm.opcodes();
    for &(tag, i1, i2, j1, j2) in &ops {
        if tag == Tag::Equal {
            same += i2 - i1;
            for (i, j) in (i1..i2).zip(j1..j2) {
                // after a reset in both images, times count from it
                let since = |b: &[f64], t: f64| b.iter().filter(|&&x| x <= t).last().copied();
                let dt = match (since(vboots, ve[i].0), since(oboots, oe[j].0)) {
                    // (its start-up: the first 2 s)
                    (Some(bv), Some(bo)) if ext && ve[i].0 - bv < 2000.0 && oe[j].0 - bo < 2000.0 => (oe[j].0 - bo) - (ve[i].0 - bv),
                    _ => oe[j].0 - ve[i].0,
                };
                if dt.abs() > tol(&ve[i].1) {
                    late.push((ve[i].0, oe[j].0, &ve[i].1));
                }
            }
        } else {
            for e in &ve[i1..i2] {
                diffs.push(("vendor", e.0, &e.1));
            }
            for e in &oe[j1..j2] {
                diffs.push(("open  ", e.0, &e.1));
            }
        }
    }
    }
    let total = ve.len();
    let mut exp: Vec<&'static str> = Vec::new();
    let mut unexp = 0;
    let mut lines = Vec::new();
    let mut tlines = Vec::new();
    for (who, ms, k) in &diffs {
        match expected(table, name, who, k) {
            Some(e) => {
                exp.push(e.why);
                if verbose {
                    lines.push(format!("    {} {} {}   [expected]", who, f1(*ms), k));
                }
            }
            None => {
                unexp += 1;
                lines.push(format!("    {} {} {}", who, f1(*ms), k));
            }
        }
    }
    for (v, o, k) in &late {
        match expected(table, name, "time", k) {
            Some(e) => {
                exp.push(e.why);
                if verbose {
                    tlines.push(format!("    time  vendor {} open {}  {}   [expected]", f1(*v), f1(*o), k));
                }
            }
            None => {
                unexp += 1;
                tlines.push(format!("    time  vendor {} open {}  {}", f1(*v), f1(*o), k));
            }
        }
    }
    let status = if unexp == 0 { "same" } else { "DIFF" };
    if !ext {
        // the Python test's line
        println!("{:<16} {:<4} {}/{} events in order, {} timing", name, status, same, total, late.len());
    } else {
        let e = if exp.is_empty() { String::new() } else { format!(", {} expected", exp.len()) };
        println!("{:<20} {:<4} {}/{} events in order, {} timing{}", name, status, same, total, late.len(), e);
    }
    if verbose || status != "same" {
        // as the Python test: at most 60 differences and 20 times
        for l in lines.iter().take(60) {
            println!("{l}");
        }
        for l in tlines.iter().take(20) {
            println!("{l}");
        }
    }
    (unexp, exp)
}

pub fn tolerance(key: &str) -> f64 {
    // what the vendor does in its slow periodic hooks may run up to one
    // period away; sequencer steps and pin mirroring must be close; the
    // thermal pass runs in the vendor's slow hook, and what it causes (a
    // trip's e-flash record) with it
    if ["i2c C", "fan ", "eflash", "reg 1F01"].iter().any(|p| key.starts_with(p)) {
        return 4000.0;
    }
    // done in the vendor's ~2 s hook: within a period of it
    if ["pin C6", "pin I7", "pin H5 +1.8V_S5_EN=0", "pin D7 EC_ALW_EN=0"].iter().any(|p| key.starts_with(p)) {
        return 2500.0;
    }
    if ["i2c F", "host"].iter().any(|p| key.starts_with(p)) {
        return 1500.0;
    }
    // the vendor's LED follows the power state in a hook that waits for
    // its PD set-up after a power-on (2.5 s)
    if key.starts_with("led ") {
        return 3000.0;
    }
    900.0
}

/// UEFI's EC update: the SPI flash holds a new EC image, 3E19 makes the
/// running firmware mirror it into the e-flash and reset; the new one
/// must boot, bring the board to S0 and answer. True if it does.
pub fn check_update(src: &[u8], new: &[u8], flash: Option<&[u8]>, label: &str) -> bool {
    let mut fl = flash.map(|f| f.to_vec()).unwrap_or_else(|| vec![0; 0x800000]);
    let mut img = new.to_vec();
    img.resize(0x20000, 0xFF);
    fl[0x10000..0x30000].copy_from_slice(&img);
    let mut ec = Ec::new(src, true, false, Some(&fl));
    ec.io.eflash.data[0x1F8FF] = 0;
    press(&mut ec, 2000.0, 300.0);
    host_at(&mut ec, 6000.0, vec![cmd(0x3E19, &[0], 0)]);
    host_at(&mut ec, 12500.0, vec![cmd(0x3FFF, b"", 32)]);
    ec.run(13000.0);
    let h = hex(&crate::util::sha256(&fl[0x10000..0x30000]));
    let mirrored = ec.io.log.events.iter().any(|e| e.kind == "reset" && e.detail.contains(&h[..12]));
    let s0 = ec.io.log.events.iter().any(|e| e.kind == "out" && e.detail.starts_with("D0 EC_SLP_S3_L=1") && e.ms > 6100.0);
    let ver: Vec<&Res> = ec.host.res.iter().filter(|r| r.cmd == 0x3FFF).collect();
    let ok = mirrored && s0 && !ver.is_empty() && ver[0].result == Some(0);
    let v = ver.first().map(|r| {
        let mut d = r.rdata.clone();
        while d.last() == Some(&0) {
            d.pop();
        }
        d.iter().map(|&c| c as char).collect::<String>()
    });
    let cap = |b: bool| if b { "True" } else { "False" };
    println!(
        "{:<16} {:<4} mirrored {}, S0 after the reset {}, version {}",
        label,
        if ok { "ok" } else { "FAIL" },
        cap(mirrored),
        cap(s0),
        v.unwrap_or_else(|| "None".into())
    );
    if let Some(h) = &ec.halted {
        println!("    halted: {h}");
    }
    ok
}

/// Everything about a run, for comparing the port with the Python model:
/// every event, every register write, where it ended.
pub fn dump(ec: &Ec) -> String {
    let mut s = String::new();
    for e in &ec.io.log.events {
        s.push_str(&format!("{:.3} {} {}\n", e.ms, e.kind, e.detail));
    }
    for &(t, a, v) in &ec.io.reglog {
        s.push_str(&format!("W {t} {a:04X} {v:02X}\n"));
    }
    s.push_str(&format!(
        "end t={} pc={:04X} steps={} sp={:02X} a={:02X}\n",
        ec.io.t,
        ec.cpu.pc,
        ec.insns,
        ec.cpu.sfr[0x81],
        ec.cpu.sfr[0xE0]
    ));
    s
}

pub fn load_flash(path: &str) -> Option<Vec<u8>> {
    let mut f = std::fs::read(crate::util::rooted(path)).ok()?;
    if f.len() < 0x800000 {
        f.resize(0x800000, 0xFF);
    }
    Some(f)
}

pub fn main(args: &[String]) -> i32 {
    let a = match crate::util::Args::parse(args, &["--open", "--vendor", "--flash", "--dump", "--cov"]) {
        Ok(a) => a,
        Err(e) => {
            eprintln!("{e}");
            return 2;
        }
    };
    if let Err(e) = a.check(&["-v", "--port", "--list"]) {
        eprintln!("{e}");
        return 2;
    }
    if a.flag("--list") {
        for sc in all_scenarios() {
            println!("{:<20} {}", sc.name, sc.doc);
        }
        return 0;
    }
    let read = |p: &str| std::fs::read(crate::util::rooted(p)).map_err(|e| format!("{p}: {e}"));
    let (vimg, oimg) = match (read(a.opt("--vendor").unwrap_or(VENDOR)), read(a.opt("--open").unwrap_or(OPEN))) {
        (Ok(v), Ok(o)) => (v, o),
        (Err(e), _) | (_, Err(e)) => {
            eprintln!("{e}");
            return 2;
        }
    };
    let flash = load_flash(a.opt("--flash").unwrap_or(FLASH));
    let verbose = a.flag("-v");
    let dump_dir = a.opt("--dump");
    let cov_file = a.opt("--cov");
    if let Some(d) = dump_dir {
        let _ = std::fs::create_dir_all(d);
    }
    let mut ok = true;
    let mut vcov: Vec<(u32, u32)> = Vec::new();
    let mut ocov: Vec<(u32, u32)> = Vec::new();
    // --port: the Python test exactly (its scenarios, its model, its output)
    let ext = !a.flag("--port");
    let list: Vec<&Scenario> = if ext { all_scenarios() } else { SCENARIOS.iter().collect() };
    let mut unexpected = 0;
    let mut kinds: std::collections::BTreeMap<&'static str, usize> = std::collections::BTreeMap::new();
    for sc in list {
        if !a.pos.is_empty() && !a.pos.iter().any(|p| p == sc.name) {
            continue;
        }
        let v = run(&vimg, flash.as_deref(), sc.f, cov_file.is_some(), ext);
        let o = run(&oimg, flash.as_deref(), sc.f, cov_file.is_some(), ext);
        if let Some(d) = dump_dir {
            let _ = std::fs::write(format!("{d}/{}.vendor.txt", sc.name), dump(&v));
            let _ = std::fs::write(format!("{d}/{}.open.txt", sc.name), dump(&o));
        }
        if cov_file.is_some() {
            vcov.extend(v.cov_ranges());
            ocov.extend(o.cov_ranges());
        }
        // the first 600 ms: the vendor's start-up (its init writes every
        // pin; ours too) is compared from the first sequencer step on
        let table: &[Expect] = if ext { super::gaps::EXPECTED } else { &[] };
        let (u, e) = compare(sc.name, &board_events(&v, 600.0), &board_events(&o, 600.0), tolerance, verbose, table, ext, &boots(&v), &boots(&o));
        ok &= u == 0;
        unexpected += u;
        for why in e {
            *kinds.entry(why).or_insert(0) += 1;
        }
        for (who, e) in [("vendor", &v), ("open", &o)] {
            if let Some(h) = &e.halted {
                println!("    {who} halted: {h}");
            }
        }
    }
    if a.pos.is_empty() || a.pos.iter().any(|p| p == "update") {
        ok &= check_update(&vimg, &oimg, flash.as_deref(), "update v->open");
        ok &= check_update(&oimg, &vimg, flash.as_deref(), "update open->v");
    }
    if let Some(f) = cov_file {
        let _ = crate::cov::save(f, "ec_fw.bin.vendor", &vcov);
        if let Err(e) = crate::cov::save(f, "ec_fw.bin", &ocov) {
            eprintln!("{e}");
        }
    }
    if ext {
        let n: usize = kinds.values().sum();
        println!("{unexpected} differences, {n} expected in {} kinds", kinds.len());
        if verbose {
            for (why, k) in &kinds {
                println!("  {k:4}  {why}");
            }
        }
    }
    println!("{}", if ok { "all same" } else { "differences" });
    if ok {
        0
    } else {
        1
    }
}

/// One image through one scenario: every event from --from MS on, with
/// the writes to the external RAM addresses given (--x ADDR, repeatable).
pub fn probe(args: &[String]) -> i32 {
    let a = match crate::util::Args::parse(args, &["--x", "--from", "--flash", "--stack", "--pc"]) {
        Ok(a) => a,
        Err(e) => {
            eprintln!("{e}");
            return 2;
        }
    };
    if a.pos.len() < 2 {
        eprintln!("usage: sky1emu ec-probe IMG SCENARIO [--x ADDR]... [--from MS] [--flash SPI]");
        return 2;
    }
    let img = match std::fs::read(crate::util::rooted(&a.pos[0])) {
        Ok(d) => d,
        Err(e) => {
            eprintln!("{}: {e}", a.pos[0]);
            return 2;
        }
    };
    let all = all_scenarios();
    let Some(sc) = all.iter().find(|s| s.name == a.pos[1]) else {
        eprintln!("no scenario {}", a.pos[1]);
        return 2;
    };
    let flash = load_flash(a.opt("--flash").unwrap_or(FLASH));
    let mut ec = new_ec(&img, flash.as_deref(), false, !a.flag("--port"));
    ec.stack_on = a.opt("--stack").map(|s| s.to_string());
    ec.break_pc = a.all("--pc").iter().filter_map(|s| crate::util::parse_int(s)).map(|v| v as u16).collect();
    ec.io.watch_x = a.all("--x").iter().filter_map(|s| crate::util::parse_int(s)).map(|v| v as u16).collect();
    let end = (sc.f)(&mut ec);
    ec.run(end);
    let from: f64 = a.opt("--from").and_then(|s| s.parse().ok()).unwrap_or(0.0);
    for e in &ec.io.log.events {
        if e.ms >= from {
            println!("{:10.3} {:<5} {}", e.ms, e.kind, e.detail);
        }
    }
    if let Some(h) = &ec.halted {
        println!("halted: {h}");
    }
    0
}

/// The port's scenarios and the gap scenarios (gaps.rs).
pub fn all_scenarios() -> Vec<&'static Scenario> {
    SCENARIOS.iter().chain(super::gaps::GAPS.iter()).collect()
}
