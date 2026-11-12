// SPDX-License-Identifier: BSD-2-Clause-Patent
//! What the port of test_ec.py left out: suspend to RAM, faults of the
//! board and its devices, every host command in every power state with
//! malformed requests, long runs, the CSU_PM interface as the PM really
//! answers. Each scenario runs both images and is compared like the
//! port's (test.rs); what differs on purpose is in EXPECTED.

use super::board::{drive_at, press, soc_off, soc_suspend};
use super::chip::{pin_id, Ec};
use super::test::{cmd, ec_request, host_at, host_slots, raw, Cmd, Expect, Scenario};

/// the SoC resumes from S3 by itself (an RTC alarm, the network): SLP_S3_L high again
pub fn soc_wake(ec: &mut Ec, at_ms: f64) {
    drive_at(ec, at_ms, "H0", 0);
}

fn s0(ec: &mut Ec) {
    press(ec, 2000.0, 300.0);
}

fn sc_s3_button(ec: &mut Ec) -> f64 {
    s0(ec);
    soc_suspend(ec, 8000.0);
    press(ec, 14000.0, 300.0);
    host_at(ec, 20000.0, vec![cmd(0x55, b"", 8)]);
    22000.0
}

fn sc_s3_soc_wake(ec: &mut Ec) -> f64 {
    s0(ec);
    soc_suspend(ec, 8000.0);
    soc_wake(ec, 14000.0);
    22000.0
}

fn sc_s3_shutdown(ec: &mut Ec) -> f64 {
    s0(ec);
    soc_suspend(ec, 8000.0);
    soc_off(ec, 14000.0);
    22000.0
}

fn sc_s3_long(ec: &mut Ec) -> f64 {
    s0(ec);
    soc_suspend(ec, 8000.0);
    heat(ec, 30000.0, 70);
    soc_wake(ec, 68000.0);
    76000.0
}

fn at(ec: &mut Ec, ms: f64, f: fn(&mut Ec)) {
    ec.at(ms, Box::new(move |ec: &mut Ec| f(ec)));
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

fn policy(ec: &mut Ec, ms: f64, p: u8) {
    host_at(ec, ms, vec![cmd(0x3E0B, &[p], 0)]);
}

fn sc_s3_ac_lost(ec: &mut Ec) -> f64 {
    s0(ec);
    policy(ec, 5000.0, 1);
    soc_suspend(ec, 8000.0);
    at(ec, 10000.0, Ec::ac_lost);
    at(ec, 12000.0, Ec::ac_restored);
    20000.0
}

fn sc_ac_lost_s0_on(ec: &mut Ec) -> f64 {
    s0(ec);
    policy(ec, 5000.0, 1);
    at(ec, 8000.0, Ec::ac_lost);
    at(ec, 10000.0, Ec::ac_restored);
    18000.0
}

fn sc_ac_lost_s0_off(ec: &mut Ec) -> f64 {
    s0(ec);
    policy(ec, 5000.0, 0);
    at(ec, 8000.0, Ec::ac_lost);
    at(ec, 10000.0, Ec::ac_restored);
    16000.0
}

fn sc_ac_lost_standby(ec: &mut Ec) -> f64 {
    at(ec, 3000.0, Ec::ac_lost);
    at(ec, 3500.0, Ec::ac_restored);
    press(ec, 6000.0, 300.0);
    14000.0
}

fn sc_alw_pg_never(ec: &mut Ec) -> f64 {
    ec.io.board().rail_dead.push(pin_id("D7"));
    press(ec, 3000.0, 300.0);
    10000.0
}

fn sc_s0_pg_never(ec: &mut Ec) -> f64 {
    ec.io.board().rail_dead.push(pin_id("D0"));
    s0(ec);
    host_at(ec, 9000.0, vec![cmd(0x3E09, b"", 4)]);
    14000.0
}

fn sc_s0_pg_lost(ec: &mut Ec) -> f64 {
    s0(ec);
    at(ec, 7999.0, |ec| ec.io.board().rail_dead.push(pin_id("D0")));
    drive_at(ec, 8000.0, "H4", 0);
    host_at(ec, 14000.0, vec![cmd(0x3E09, b"", 4)]);
    16000.0
}

fn sc_s3_pg_lost(ec: &mut Ec) -> f64 {
    s0(ec);
    drive_at(ec, 3000.0, "H6", 1);
    drive_at(ec, 8000.0, "H6", 0);
    host_at(ec, 14000.0, vec![cmd(0x3E09, b"", 4)]);
    16000.0
}

fn sc_soc_thermtrip(ec: &mut Ec) -> f64 {
    s0(ec);
    drive_at(ec, 8000.0, "H2", 0);
    host_at(ec, 14000.0, vec![cmd(0x3E09, b"", 4)]);
    16000.0
}

fn sc_fan_stall(ec: &mut Ec) -> f64 {
    ec.io.board().fan_stalled = true;
    s0(ec);
    host_at(ec, 5000.0, vec![cmd(0x21, &[0, 0, 0x0f, 0xa0], 0)]);
    host_at(ec, 30000.0, vec![cmd(0x20, b"", 4), cmd(0x26, &[0], 4)]);
    heat(ec, 32000.0, 75);
    host_at(ec, 50000.0, vec![cmd(0x20, b"", 4), cmd(0x26, &[0], 4)]);
    52000.0
}

fn ntc_raw(ec: &mut Ec, ms: f64, ch: usize, code: Option<u16>) {
    ec.at(ms, Box::new(move |ec: &mut Ec| ec.io.board().ntc_raw[ch] = code));
}

fn sc_ntc_open(ec: &mut Ec) -> f64 {
    s0(ec);
    ntc_raw(ec, 5000.0, 3, Some(1023));
    ntc_raw(ec, 5000.0, 0, Some(1023));
    host_at(ec, 28000.0, vec![cmd(0x3E0C, b"", 4), cmd(0x3E09, b"", 4)]);
    30000.0
}

fn sc_ntc_short(ec: &mut Ec) -> f64 {
    s0(ec);
    ntc_raw(ec, 5000.0, 4, Some(0));
    host_at(ec, 18000.0, vec![cmd(0x3E09, b"", 4)]);
    20000.0
}

fn sc_csu_nack(ec: &mut Ec) -> f64 {
    ec.io.board().csu.nack = true;
    s0(ec);
    heat(ec, 9000.0, 80);
    host_at(ec, 40000.0, vec![cmd(0x3E0C, b"", 4)]);
    42000.0
}

fn sc_csu_back(ec: &mut Ec) -> f64 {
    s0(ec);
    heat(ec, 9000.0, 70);
    at(ec, 15000.0, |ec| ec.io.board().csu.nack = true);
    heat(ec, 16000.0, 40);
    at(ec, 35000.0, |ec| ec.io.board().csu.nack = false);
    host_at(ec, 56000.0, vec![cmd(0x3E0C, b"", 4)]);
    58000.0
}

fn sc_host_lid(ec: &mut Ec) -> f64 {
    s0(ec);
    host_at(ec, 9000.0, vec![cmd(0x3E20, b"", 4)]);
    drive_at(ec, 9500.0, "C0", 0);
    host_at(ec, 10000.0, vec![cmd(0x3E20, b"", 4)]);
    drive_at(ec, 10500.0, "C0", 1);
    host_at(ec, 11000.0, vec![cmd(0x3E20, b"", 4)]);
    12000.0
}

fn sc_fan_duty_sweep(ec: &mut Ec) -> f64 {
    s0(ec);
    // 0x25 (a duty in percent, as a float times 2.55): every value 0-110
    let v: Vec<Cmd> = (0..=110u8).map(|d| cmd(0x25, &[0, d], 0)).collect();
    host_slots(ec, 15000.0, 40.0, v);
    20000.0
}

fn sc_fan_rpm_max(ec: &mut Ec) -> f64 {
    s0(ec);
    // targets above what the fan can do (5000 rpm), one past 16 bits
    host_at(ec, 5000.0, vec![cmd(0x21, &[0, 0, 0x17, 0x70], 0)]);
    host_at(ec, 30000.0, vec![cmd(0x20, b"", 4), cmd(0x26, &[0, 0], 4), cmd(0x21, &[0, 1, 0, 0], 0)]);
    host_at(ec, 45000.0, vec![cmd(0x20, b"", 4), cmd(0x26, &[0, 0], 4)]);
    host_at(ec, 89000.0, vec![cmd(0x20, b"", 4), cmd(0x26, &[0, 0], 4)]);
    90000.0
}

fn sc_fan_rpm_settle(ec: &mut Ec) -> f64 {
    s0(ec);
    // a target the correction reaches (within 50 rpm), after the first pass
    host_at(ec, 16000.0, vec![cmd(0x21, &[0, 0, 0x0b, 0xb8], 0)]);
    host_at(ec, 79000.0, vec![cmd(0x20, b"", 4), cmd(0x26, &[0, 0], 4)]);
    80000.0
}

fn sc_s3_mute(ec: &mut Ec) -> f64 {
    s0(ec);
    host_at(ec, 5000.0, vec![cmd(0x52, &[2], 0)]);
    soc_suspend(ec, 8000.0);
    soc_wake(ec, 14000.0);
    20000.0
}

fn sc_sd_button_late(ec: &mut Ec) -> f64 {
    s0(ec);
    soc_off(ec, 8000.0);
    // after the slow hook saw SD, before G3
    press(ec, 11300.0, 100.0);
    18000.0
}

fn sc_csu_hang(ec: &mut Ec) -> f64 {
    s0(ec);
    heat(ec, 9000.0, 70);
    at(ec, 9500.0, |ec| ec.io.smb[2].hang = true);
    at(ec, 30000.0, |ec| ec.io.smb[2].hang = false);
    heat(ec, 31000.0, 40);
    host_at(ec, 50000.0, vec![cmd(0x3E0C, b"", 4), cmd(0x3E09, b"", 4)]);
    52000.0
}

fn sc_pd_hang(ec: &mut Ec) -> f64 {
    at(ec, 700.0, |ec| ec.io.smb[5].hang = true);
    at(ec, 6000.0, |ec| ec.io.smb[5].hang = false);
    s0(ec);
    host_at(ec, 9000.0, vec![cmd(0x3E07, b"", 8)]);
    10000.0
}

fn sc_pd_nack(ec: &mut Ec) -> f64 {
    ec.io.board().pd[0].nack = true;
    s0(ec);
    host_at(ec, 9000.0, vec![cmd(0x3E07, b"", 8)]);
    press(ec, 40000.0, 300.0);
    host_at(ec, 58000.0, vec![cmd(0x3E07, b"", 8), cmd(0x3E09, b"", 4)]);
    60000.0
}

fn sc_pd_int(ec: &mut Ec) -> f64 {
    s0(ec);
    drive_at(ec, 8000.0, "A6", 0);
    drive_at(ec, 8500.0, "A6", 1);
    drive_at(ec, 9000.0, "A7", 0);
    host_at(ec, 11000.0, vec![cmd(0x55, b"", 8), cmd(0x3E07, b"", 8)]);
    12000.0
}

fn sc_eflash_fail(ec: &mut Ec) -> f64 {
    s0(ec);
    at(ec, 5000.0, |ec| ec.io.eflash.fail_writes = true);
    host_at(ec, 6000.0, vec![cmd(0x3E0B, &[1], 0), cmd(0x3E0B, &[1], 0)]);
    8000.0
}

fn sc_overheat_long(ec: &mut Ec) -> f64 {
    s0(ec);
    ec.at(5000.0, Box::new(|ec: &mut Ec| ec.io.board().temps[3] = Some(90.0)));
    host_at(ec, 30000.0, vec![cmd(0x3E09, b"", 4)]);
    soc_off(ec, 16000.0);
    host_at(ec, 60000.0, vec![cmd(0x3E09, b"", 4), cmd(0x3FFF, b"", 32)]);
    // (not up to a thermal pass: ~62 s in one image, ~63 s in the other)
    60500.0
}

fn sc_fan_mode_back(ec: &mut Ec) -> f64 {
    s0(ec);
    // a pass in auto (45 C: 20 %), then mute, then auto with the same temperature
    host_at(ec, 16000.0, vec![cmd(0x52, &[2], 0)]);
    host_at(ec, 26000.0, vec![cmd(0x52, &[1], 0)]);
    host_at(ec, 36000.0, vec![cmd(0x52, &[4], 0)]);
    host_at(ec, 46000.0, vec![cmd(0x52, &[3], 0)]);
    host_at(ec, 56000.0, vec![cmd(0x52, &[0], 0), cmd(0x25, &[0, 60], 0)]);
    heat(ec, 57000.0, 75);
    host_at(ec, 76000.0, vec![cmd(0x52, &[5], 0)]);
    88000.0
}

fn sc_sd_button(ec: &mut Ec) -> f64 {
    s0(ec);
    soc_off(ec, 8000.0);
    press(ec, 9000.0, 300.0);
    16000.0
}

fn sc_g3_long_press(ec: &mut Ec) -> f64 {
    s0(ec);
    soc_off(ec, 8000.0);
    press(ec, 14000.0, 6000.0);
    26000.0
}

fn sc_standby_long_press(ec: &mut Ec) -> f64 {
    press(ec, 2000.0, 8000.0);
    16000.0
}

/// The vendor's host command table (bank 1 0xD0A4) and the ids next to
/// them that it does not have, with the parameters each reads.
pub fn every_cmd() -> Vec<Cmd> {
    let table: &[(u16, &[u8])] = &[
        (0x000B, b""), (0x0020, b""), (0x0021, &[0, 0, 0x0b, 0xb8]), (0x0022, b""), (0x0023, &[0x20]),
        (0x0025, &[0, 0x32]), (0x0026, &[0, 0]), (0x002C, b""), (0x0031, &[1, 2, 3, 4, 5]), (0x0032, b""),
        (0x0052, &[1]), (0x0055, b""), (0x0092, &[0x4a, 1]), (0x0093, &[0x4a]), (0x0600, b""), (0x0601, b""),
        (0x3E01, b""), (0x3E02, b""), (0x3E03, b""), (0x3E06, b""), (0x3E07, b""), (0x3E09, b""), (0x3E0A, b""),
        (0x3E0B, &[0]), (0x3E0C, b""), (0x3E0D, &[1, 2, 3, 4, 5, 6, 7, 8, 9, 10]), (0x3E0E, &[0]), (0x3E0E, &[1]),
        (0x3E0E, &[2]), (0x3E0F, &[0x10, 1]), (0x3E10, &[0; 16]), (0x3E13, b""), (0x3E14, b""), (0x3E20, b""),
        (0x3E21, &[1]), (0x3E22, &[1]), (0x3FFF, b""),
    ];
    let around: &[u16] = &[
        0x0000, 0x000A, 0x000C, 0x001F, 0x0024, 0x0027, 0x002B, 0x002D, 0x0030, 0x0033, 0x0051, 0x0053, 0x0054,
        0x0056, 0x0091, 0x0094, 0x05FF, 0x0602, 0x3E00, 0x3E04, 0x3E05, 0x3E08, 0x3E11, 0x3E12, 0x3E15, 0x3E18,
        0x3E1A, 0x3E1F, 0x3E23, 0x3FFE, 0x4000, 0xFFFF,
    ];
    // read back as UEFI does: 4 bytes of data, more where the answer is longer
    let n = |c: u16| match c {
        0x3FFF => 32,
        0x000B => 16,
        0x0055 | 0x0032 | 0x3E07 => 8,
        _ => 4,
    };
    let mut v: Vec<Cmd> = table.iter().map(|&(c, d)| cmd(c, d, n(c))).collect();
    v.extend(around.iter().map(|&c| cmd(c, b"", 4)));
    v
}

fn sc_host_overread(ec: &mut Ec) -> f64 {
    s0(ec);
    // 48 bytes read after answers of 0, 1 and 2 bytes, after commands that
    // leave parameters behind (0x31, 0x92)
    let v = vec![
        cmd(0x0031, &[1, 2, 3, 4, 5], 0),
        cmd(0x0021, &[0, 0, 0x0b, 0xb8], 48),
        cmd(0x0092, &[0x4a, 1], 48),
        cmd(0x3E09, b"", 48),
        cmd(0x3E0C, b"", 48),
    ];
    host_slots(ec, 15000.0, 100.0, v);
    16000.0
}

fn sc_host_every_s0(ec: &mut Ec) -> f64 {
    s0(ec);
    host_slots(ec, 15000.0, 50.0, every_cmd());
    host_slots(ec, 19000.0, 50.0, vec![cmd(0x3E0F, &[0x2A, 0], 0), cmd(0x3E0F, &[0x2A, 1], 0), cmd(0x3E0F, &[0x2A, 0], 0)]);
    20000.0
}

fn sc_host_every_standby(ec: &mut Ec) -> f64 {
    host_slots(ec, 1500.0, 50.0, every_cmd());
    5500.0
}

fn sc_host_every_s3(ec: &mut Ec) -> f64 {
    s0(ec);
    soc_suspend(ec, 8000.0);
    host_slots(ec, 10000.0, 50.0, every_cmd());
    14000.0
}

/// requests that are wrong
pub fn bad_requests() -> Vec<Cmd> {
    let good = ec_request(0x3E09, b"", 0);
    let mut bad_sum = good.clone();
    bad_sum[2] ^= 0x55;
    let ver2 = {
        let mut r = good.clone();
        r[1] = 2;
        r[2] = r[2].wrapping_add(1);
        r
    };
    let cver = ec_request(0x3E09, b"", 1);
    let mut not_da = good.clone();
    not_da[0] = 0xDB;
    // a length of 4 declared, 2 parameter bytes sent
    let mut short = ec_request(0x0021, &[0, 0, 0x0f, 0xa0], 0);
    short.truncate(short.len() - 2);
    // a length of 0 declared, 3 bytes more sent
    let mut long = ec_request(0x3E09, b"", 0);
    long.extend_from_slice(&[1, 2, 3]);
    // 70 parameter bytes (more than the 64-byte buffer)
    let huge = ec_request(0x0031, &[0x5A; 70], 0);
    let only_da = vec![0xDA];
    // 80 bytes read: past the 64-byte buffer
    let mut long_read = cmd(0x3E09, b"", 0);
    long_read.n = 80;
    // a GPIO write (TPM_PWR_EN off) with a bad checksum: is it done?
    let mut bad_gpio = ec_request(0x0092, &[0x4a, 0], 0);
    bad_gpio[2] ^= 0x01;
    let mut cut = cmd(0x3FFF, b"", 32);
    cut.cut = Some(5);
    let mut cut_read = cmd(0x3FFF, b"", 32);
    cut_read.cut = Some(14);
    vec![
        raw(0x3E09, &bad_sum, 8),
        cmd(0x3E09, b"", 4),
        raw(0x3E09, &ver2, 8),
        raw(0x3E09, &cver, 8),
        raw(0x3E09, &not_da, 8),
        cmd(0x3E09, b"", 4),
        raw(0x0021, &short, 8),
        cmd(0x0020, b"", 4),
        raw(0x3E09, &long, 8),
        raw(0x0031, &huge, 8),
        cmd(0x0032, b"", 8),
        raw(0x00DA, &only_da, 8),
        cmd(0x3E09, b"", 4),
        cut,
        cmd(0x3E09, b"", 4),
        cut_read,
        cmd(0x3E09, b"", 4),
        raw(0x0000, &[], 8),
        cmd(0x3E09, b"", 4),
        long_read,
        raw(0x0092, &bad_gpio, 0),
        cmd(0x0093, &[0x4a], 2),
        cmd(0x0093, &[0x4a], 2),
    ]
}

fn sc_host_bad(ec: &mut Ec) -> f64 {
    s0(ec);
    host_slots(ec, 15000.0, 60.0, bad_requests());
    17000.0
}

fn back_to_back(ec: &mut Ec, gap: f64) -> f64 {
    s0(ec);
    // no gap (or a short one) after an answer: the next request starts at the STOP
    ec.host.gap_ms = Some(gap);
    let mut v = Vec::new();
    for _ in 0..4 {
        v.extend([cmd(0x3E09, b"", 4), cmd(0x0093, &[0x4a], 2), cmd(0x0092, &[0x4a, 0], 0), cmd(0x0093, &[0x4a], 2), cmd(0x0092, &[0x4a, 1], 0), cmd(0x3E0C, b"", 4), cmd(0x55, b"", 8)]);
    }
    host_at(ec, 15000.0, v);
    17000.0
}

fn sc_host_back_to_back(ec: &mut Ec) -> f64 {
    // 1 ms after the STOP. Closer (0.1 ms; the model adds its own byte
    // time) the vendor's image can answer with the answer before; see
    // differences.md.
    back_to_back(ec, 1.0)
}

fn sc_host_gap2(ec: &mut Ec) -> f64 {
    back_to_back(ec, 2.0)
}

fn sc_host_gap5(ec: &mut Ec) -> f64 {
    back_to_back(ec, 5.0)
}

fn sc_host_gap10(ec: &mut Ec) -> f64 {
    back_to_back(ec, 10.0)
}

fn sc_long_s0(ec: &mut Ec) -> f64 {
    s0(ec);
    let temps = [50u8, 66, 75, 60, 48, 85, 40];
    // between the thermal passes (every ~10.2 s from ~14 s), so that
    // both images read each temperature at the same pass
    for (i, t) in temps.iter().enumerate() {
        heat(ec, 19200.0 + i as f64 * 20400.0, *t);
        host_at(ec, 27000.0 + i as f64 * 20400.0, vec![cmd(0x3E0C, b"", 4), cmd(0x0026, &[0, 0], 4), cmd(0x0020, b"", 4)]);
    }
    soc_off(ec, 190000.0);
    // (the watchdog resets the EC ~32 s after the last hot pass, ~200 s)
    press(ec, 212000.0, 300.0);
    host_at(ec, 228000.0, vec![cmd(0x3FFF, b"", 32), cmd(0x3E09, b"", 4)]);
    232000.0
}

fn sc_long_standby(ec: &mut Ec) -> f64 {
    host_at(ec, 100000.0, vec![cmd(0x3FFF, b"", 32)]);
    press(ec, 140000.0, 300.0);
    host_at(ec, 150000.0, vec![cmd(0x3E09, b"", 4)]);
    155000.0
}

pub const GAPS: &[Scenario] = &[
    Scenario { name: "s3_button", doc: "S0, suspend to RAM, the button wakes it", f: sc_s3_button },
    Scenario { name: "s3_soc_wake", doc: "S0, suspend to RAM, the SoC wakes by itself", f: sc_s3_soc_wake },
    Scenario { name: "s3_shutdown", doc: "S0, suspend to RAM, then the SoC powers off from S3", f: sc_s3_shutdown },
    Scenario { name: "s3_long", doc: "a minute in S3 (thermal passes, the LED), warmer, then a wake", f: sc_s3_long },
    Scenario { name: "s3_ac_lost", doc: "AC lost in S3 and back, policy 1", f: sc_s3_ac_lost },
    Scenario { name: "ac_lost_s0_on", doc: "AC lost in S0 and back, policy 1: on by itself", f: sc_ac_lost_s0_on },
    Scenario { name: "ac_lost_s0_off", doc: "AC lost in S0 and back, policy 0: stays off", f: sc_ac_lost_s0_off },
    Scenario { name: "ac_lost_standby", doc: "AC lost in standby and back, then the button", f: sc_ac_lost_standby },
    Scenario { name: "alw_pg_never", doc: "SYS_ALW_PG never comes", f: sc_alw_pg_never },
    Scenario { name: "s0_pg_never", doc: "SYS_S0_PG never comes after the button", f: sc_s0_pg_never },
    Scenario { name: "s0_pg_lost", doc: "SYS_S0_PG lost in S0", f: sc_s0_pg_lost },
    Scenario { name: "s3_pg_lost", doc: "SYS_S3_PG (DRAM) lost in S0", f: sc_s3_pg_lost },
    Scenario { name: "soc_thermtrip", doc: "the SoC's THERMTRIP line in S0", f: sc_soc_thermtrip },
    Scenario { name: "fan_stall", doc: "a fan that never turns: target speed, then auto", f: sc_fan_stall },
    Scenario { name: "ntc_open", doc: "the SoC and DRAM NTCs open (ADC 1023)", f: sc_ntc_open },
    Scenario { name: "ntc_short", doc: "the USB-C NTC shorted (ADC 0)", f: sc_ntc_short },
    Scenario { name: "csu_nack", doc: "CSU_PM never answers on SMBus C", f: sc_csu_nack },
    Scenario { name: "csu_back", doc: "CSU_PM stops answering for 20 s, then answers again", f: sc_csu_back },
    Scenario { name: "host_lid", doc: "the lid switch (3E20) closed and open", f: sc_host_lid },
    Scenario { name: "fan_duty_sweep", doc: "0x25 with every duty from 0 to 110 %", f: sc_fan_duty_sweep },
    Scenario { name: "fan_rpm_max", doc: "target speeds the fan cannot reach, one past 16 bits", f: sc_fan_rpm_max },
    Scenario { name: "fan_rpm_settle", doc: "a target speed the correction reaches", f: sc_fan_rpm_settle },
    Scenario { name: "s3_mute", doc: "fan mode mute, suspend, resume (no pass at the resume)", f: sc_s3_mute },
    Scenario { name: "sd_button_late", doc: "the button 3.3 s after a shutdown (in SD, before G3)", f: sc_sd_button_late },
    Scenario { name: "csu_hang", doc: "SMBus C (CSU_PM) hangs for 20 s: busy, never done", f: sc_csu_hang },
    Scenario { name: "pd_hang", doc: "SMBus F (the PD controllers) hangs through the standby set-up", f: sc_pd_hang },
    Scenario { name: "pd_nack", doc: "PD controller 1 NACKs everything", f: sc_pd_nack },
    Scenario { name: "pd_int", doc: "the PD controllers' interrupt lines in S0", f: sc_pd_int },
    Scenario { name: "eflash_fail", doc: "e-flash erase and program that change nothing (3E0B)", f: sc_eflash_fail },
    Scenario { name: "overheat_long", doc: "an NTC trip, the SoC off, a minute after (the watchdog)", f: sc_overheat_long },
    Scenario { name: "fan_mode_back", doc: "auto, mute, auto, perf, auto(3), 0 with a duty, an unknown mode", f: sc_fan_mode_back },
    Scenario { name: "sd_button", doc: "the button 1 s after a shutdown (before G3)", f: sc_sd_button },
    Scenario { name: "g3_long_press", doc: "a 6 s hold in G3", f: sc_g3_long_press },
    Scenario { name: "standby_long_press", doc: "an 8 s hold in standby", f: sc_standby_long_press },
    Scenario { name: "host_every_s0", doc: "every command id of the vendor's table and its neighbours, in S0", f: sc_host_every_s0 },
    Scenario { name: "host_every_standby", doc: "the same in standby", f: sc_host_every_standby },
    Scenario { name: "host_every_s3", doc: "the same in S3", f: sc_host_every_s3 },
    Scenario { name: "host_overread", doc: "48 bytes read after short answers (what lies past the answer)", f: sc_host_overread },
    Scenario { name: "host_bad", doc: "bad checksum, version, prefix, lengths, a request cut short", f: sc_host_bad },
    Scenario { name: "host_back_to_back", doc: "commands 1 ms after the answer", f: sc_host_back_to_back },
    Scenario { name: "host_gap2", doc: "the same, 2 ms after each answer", f: sc_host_gap2 },
    Scenario { name: "host_gap5", doc: "the same, 5 ms after each answer", f: sc_host_gap5 },
    Scenario { name: "host_gap10", doc: "the same, 10 ms after each answer", f: sc_host_gap10 },
    Scenario { name: "long_s0", doc: "190 s in S0 (the ms counter wraps), temperatures, then off and on", f: sc_long_s0 },
    Scenario { name: "long_standby", doc: "140 s in standby, then on", f: sc_long_standby },
];

/// The differences kept on purpose, and the vendor's faults the open
/// firmware does not reproduce; each matched by scenario, side and text.
/// firmware/ec/differences.md has the evidence for each.
pub const EXPECTED: &[Expect] = &[
    // --- the AC-loss byte: the vendor erases and programs it at every 3E0B
    Expect {
        scenarios: &["host", "ac_lost_s0_off", "host_every_s0", "host_every_s3"],
        who: "vendor",
        key: "eflash ",
        why: "3E0B with the value the e-flash already holds: the vendor erases and programs it again (bank 2 0xBB25, no compare); the open firmware writes only a change",
    },
    // --- PD firmware update over the host interface
    Expect {
        scenarios: &["host_every_s0", "host_every_s3"],
        who: "vendor",
        key: "pin D3 EC_ACPI_INT",
        why: "3E0D/3E10 (PD firmware update): the vendor's update flow raises and clears EC_ACPI_INT (bank 1 0xD489, 0xD4F3); not in the open firmware",
    },
    // --- bytes read past an answer
    Expect {
        scenarios: &["host_overread"],
        who: "vendor",
        key: "host ",
        why: "bytes read past an answer and its 4 target-speed bytes: the vendor's are its RAM after the buffer (0x31's parameters, the fan mode, the last 0x92); the open firmware's are 0",
    },
    Expect {
        scenarios: &["host_overread"],
        who: "open",
        key: "host ",
        why: "bytes read past an answer and its 4 target-speed bytes: the vendor's are its RAM after the buffer (0x31's parameters, the fan mode, the last 0x92); the open firmware's are 0",
    },
    // --- malformed requests
    Expect {
        scenarios: &["host_bad"],
        who: "vendor",
        key: "host 3E09 da03e33e",
        why: "a request with a bad checksum: the vendor answers 7 but runs the command (its data in the answer; a 0x92 so sent is done); the open firmware does not run it",
    },
    Expect {
        scenarios: &["host_bad"],
        who: "open",
        key: "host 3E09 da03e33e",
        why: "a request with a bad checksum: the vendor answers 7 but runs the command (its data in the answer; a 0x92 so sent is done); the open firmware does not run it",
    },
    Expect {
        scenarios: &["host_bad"],
        who: "vendor",
        key: "pin J2 TPM_PWR_EN=0",
        why: "a request with a bad checksum: the vendor answers 7 but runs the command (its data in the answer; a 0x92 so sent is done); the open firmware does not run it",
    },
    Expect {
        scenarios: &["host_bad"],
        who: "vendor",
        key: "host 0093 4a -> 00 0b 03 fc",
        why: "a request with a bad checksum: the vendor answers 7 but runs the command (its data in the answer; a 0x92 so sent is done); the open firmware does not run it",
    },
    Expect {
        scenarios: &["host_bad"],
        who: "open",
        key: "host 0093 4a -> 00 0b 03 fb",
        why: "a request with a bad checksum: the vendor answers 7 but runs the command (its data in the answer; a 0x92 so sent is done); the open firmware does not run it",
    },
    Expect {
        scenarios: &["host_bad"],
        who: "vendor",
        key: "host 3E09 da02b73e",
        why: "a request of protocol version 2: the vendor ignores the version; the open firmware answers 6 (invalid version)",
    },
    Expect {
        scenarios: &["host_bad"],
        who: "open",
        key: "host 3E09 da02b73e",
        why: "a request of protocol version 2: the vendor ignores the version; the open firmware answers 6 (invalid version)",
    },
    // --- the vendor's main loop waiting
    Expect {
        scenarios: &["pd_nack"],
        who: "both",
        key: "",
        why: "PD controller 1 never answers: the vendor's loop waits 200 pings x 27.5 ms for each of its commands (bank 2 0x93CC), 16.5 s at each set-up, running nothing else (the button pressed at 2 s is lost, the board comes on at the second press); the open firmware retries as long but goes on with the rest",
    },
    Expect {
        scenarios: &["pd_hang"],
        who: "both",
        key: "",
        why: "SMBus F hangs through the standby PD set-up: the vendor's loop waits on each transfer and its retries and runs nothing else (the button pressed at 2 s is lost, the board stays off); the open firmware times each transfer out (26 ms), goes on, and sets the controllers up when the bus is back",
    },
    Expect {
        scenarios: &["csu_hang"],
        who: "vendor",
        key: "pin F",
        why: "a transfer on a hung SMBus: the vendor also clocks the bus's pins as GPIOs to free it (bus recovery); the open firmware only kills the transfer and resets the channel",
    },
    Expect {
        scenarios: &["alw_pg_never"],
        who: "open",
        key: "pin J4 PWRBTN_EC",
        why: "SYS_ALW_PG never comes: the vendor's loop waits for it in its AC sequence and does nothing else (the button is not mirrored); the open firmware waits in its sequencer and still mirrors the button (to an unpowered SoC)",
    },
    Expect {
        scenarios: &["s0_pg_never"],
        who: "vendor",
        key: "pin E5 TP91=1",
        why: "SYS_S0_PG never comes: the vendor's S0 sequence sets TP91 (a test point) before it waits; the open firmware sets it in its step after SYS_S0_PG",
    },
    // --- the thermal pass's phase
    Expect {
        scenarios: &["overheat_long"],
        who: "both",
        key: "fan DCR2",
        why: "after the trip the fan is stopped at an even slow hook in SD; the vendor's SD here lasts two hooks, both odd, so its fan stays at 20 % until the watchdog resets the EC (the rails are off by then)",
    },
    Expect {
        scenarios: &["g3_long_press"],
        who: "both",
        key: "i2c C 04",
        why: "the pass count goes on through G3: where the first pass after the resume's comes depends on the hooks before (the vendor's 2.6 s after it here)",
    },
    // --- the power-off reason after an NTC trip
    Expect {
        scenarios: &["overheat_long", "long_s0"],
        who: "vendor",
        key: "eflash ",
        why: "an NTC still at 85 C or more at the next passes: the vendor writes the power-off reason record (an erase, 4 bytes) again at each; the open firmware only at the trip",
    },
    // --- S3: the fan between the vendor's passes
    Expect {
        scenarios: &["s3_button", "s3_soc_wake", "s3_shutdown", "s3_long", "host_every_s3"],
        who: "both",
        key: "fan DCR2",
        why: "in S3 a thermal pass sets the state's duty and every other slow hook stops the fan again; which of the two the fan shows, and when, follows each image's slow hook (the vendor's ~1.6 s and irregular in S3)",
    },
    Expect {
        scenarios: &["s3_long", "host_every_s3"],
        who: "both",
        key: "i2c C 04",
        why: "in S3 the thermal passes follow each image's slow hook (the vendor's ~1.6 s and irregular in S3: 5 hooks a pass)",
    },
    Expect {
        scenarios: &["host_every_s3"],
        who: "both",
        key: "led ",
        why: "the LED breathes in S3 (1 % a 40 ms); where it is in its cycle at a given moment depends on the vendor's main loop, which the host commands slow down",
    },
    // --- GPIO writes in quick succession
    Expect {
        scenarios: &["host_back_to_back", "host_gap2", "host_gap5", "host_gap10"],
        who: "both",
        key: "pin J2 TPM_PWR_EN",
        why: "two 0x92 within one 100 ms hook: the vendor does only the last (a write before it is lost); the open firmware does the pending one when the next comes",
    },
    Expect {
        scenarios: &["host_back_to_back", "host_gap2", "host_gap5", "host_gap10"],
        who: "both",
        key: "host 0093",
        why: "two 0x92 within one 100 ms hook: the vendor does only the last (a write before it is lost); the open firmware does the pending one when the next comes",
    },
];
