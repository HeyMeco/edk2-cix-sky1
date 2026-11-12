// SPDX-License-Identifier: BSD-2-Clause-Patent
//! The Orion O6 around its EC: power tree, SoC, the devices on the EC's
//! buses (two RTS5453H PD controllers, CSU_PM's I2C slave), the NTCs and
//! the fan. Port of scripts/open/ec_emu.py's `O6Board`, `Rts5453`,
//! `CsuPm` and ec_o6_pins.py's net names, plus the faults the gap
//! scenarios inject (all off by default: then it is the Python model).
//!
//! Rails follow their enables after RAIL_MS; the SoC leaves reset after
//! RSM_RST, and a PWRBTN_EC pulse moves it S5 -> S0 (SLP_S5_L and
//! SLP_S3_L high, which the inverters Q3114/Q3112 turn into low
//! SOC_SLP5/SOC_SLP3 at the EC).

use super::chip::{pin_id, Dev, Ec, Io, Pin};

/// Orion O6 EC (ITE IT5570E, U90130) pins and nets, from Radxa's Orion O6
/// V1.2 schematic, sheet 35 "EC Module"
/// (https://dl.radxa.com/orion/o6/hw/radxa_orion_o6_v1.20_schematic.pdf),
/// checked against the GPIO control values in Radxa's EC V1.07 image.
pub const PINS: &[(&str, &str)] = &[
    ("A0", "-"), ("A1", "BLINK_L"), ("A2", "FANOUT_EC"), ("A3", "ATX_DET_EC"), ("A4", "I2C_PD_SCL"),
    ("A5", "I2C_PD_SDA"), ("A6", "PD0_I2C_INT_L"), ("A7", "PD1_I2C_INT_L"), ("B0", "GND"), ("B1", "GPB1"),
    ("B2", "-"), ("B3", "PWRBTN_L_HW"), ("B4", "EC_PWRON"), ("B5", "SOC_SLP5"), ("B6", "EC_SOC_RST"), ("B7", "-"),
    ("C0", "LID_SW_L"), ("C1", "EC_I2C_ACPI_SCL"), ("C2", "EC_I2C_ACPI_SDA"), ("C3", "TP92"), ("C4", "-"),
    ("C5", "TP93"), ("C6", "DEPOP_MUTE_L"), ("C7", "UART_RX_DEBUG"), ("D0", "EC_SLP_S3_L"), ("D1", "SOC_RESET_OUT"),
    ("D2", "ERST#"), ("D3", "EC_ACPI_INT"), ("D4", "SOC_PWR_OK_IN_L"), ("D5", "-"), ("D6", "FANTACH_EC"),
    ("D7", "EC_ALW_EN"), ("E0", "TP100"), ("E1", "VTR_PWR_CTL"), ("E2", "RSM_RST"), ("E3", "-"), ("E4", "TP99"),
    ("E5", "TP91"), ("E6", "UART_TX_DEBUG"), ("E7", "TP101"), ("F0", "-"), ("F1", "-"), ("F2", "EC_I2C_HID_SCL"),
    ("F3", "EC_I2C_HID_SDA"), ("F4", "EC_HID_INT"), ("F5", "EC_CSU_INT"), ("F6", "I2C_CSU_SCL"),
    ("F7", "I2C_CSU_SDA"), ("G0", "+1.8V_S5_EN (NC)"), ("G1", "EC_SPI_D2 (NC)"), ("G2", "STRAP0"),
    ("G3", "EC_SPI_CS_L"), ("G4", "EC_SPI_D0"), ("G5", "EC_SPI_D1"), ("G6", "EC_SPI_D3 (NC)"), ("G7", "EC_SPI_CLK"),
    ("H0", "SOC_SLP3"), ("H1", "PROCHOT"), ("H2", "THERMTRIP"), ("H3", "EC_PMIC_EN_GATE_L"), ("H4", "SYS_S0_PG"),
    ("H5", "+1.8V_S5_EN"), ("H6", "SYS_S3_PG"), ("H7", "DC_S5_ALW_EN_CTL"), ("I0", "SOC_NTC"), ("I1", "BRDEG_NTC"),
    ("I2", "HP_ADC (NC)"), ("I3", "DRAM_NTC"), ("I4", "USBC_NTC"), ("I5", "PMIC_NTC"), ("I6", "TP90"),
    ("I7", "THERMTRIP_EC"), ("J0", "TPM_STR_L"), ("J1", "TP122"), ("J2", "TPM_PWR_EN"), ("J3", "SYS_ALW_PG"),
    ("J4", "PWRBTN_EC"), ("J5", "TP89"), ("J6", "EC_SLP_S5_L"), ("J7", "GPJ7"), ("M2", "RSV2"), ("M3", "RSV1"),
    ("M6", "RSV4"),
];

/// The net on an EC pin ("?" when the schematic has none).
pub fn net(p: Pin) -> &'static str {
    let name = super::chip::pin_name(p);
    PINS.iter().find(|e| e.0 == name).map(|e| e.1).unwrap_or("?")
}

/// ADC data registers (low, high) of the virtual channels the O6 EC uses
pub const ADC_DATA: [(u16, u16); 6] =
    [(0x1918, 0x1919), (0x1907, 0x1908), (0x190A, 0x190B), (0x190D, 0x190E), (0x193A, 0x1939), (0x193D, 0x193C)];

/// 10-bit ADC code of a 100K NTC to ground under a 24K pull-up from
/// 3.3 V (R96248/R96249, ...) at temp_c
pub fn ntc_code(temp_c: f64) -> u16 {
    let (beta, r25, rup) = (4250.0f64, 100.0f64, 24.0f64);
    let r = r25 * (beta * (1.0 / (temp_c + 273.15) - 1.0 / 298.15)).exp();
    (1023.0 * r / (r + rup)).round_ties_even().clamp(0.0, 1023.0) as u16
}

/// Realtek RTS5453H PD controller, SMBus side (as in Chromium EC's
/// rts54xx driver): a command is a block write (cmd, len, data...);
/// receive-byte reads the ping status (bits 1:0: 1 = complete, bits 7:2:
/// data length); block read 0x80 returns the command's data.
#[derive(Clone)]
pub struct Rts5453 {
    pub reply: Vec<u8>,
    pub cmds: Vec<Vec<u8>>,
    pub fw_version: [u8; 2],
    /// faults: NACK every transfer
    pub nack: bool,
}

impl Rts5453 {
    fn answer(&self, wr: &[u8]) -> Vec<u8> {
        match wr[0] {
            0x01 => vec![], // vendor command enable
            0x3A => {
                // GET_IC_STATUS: length in byte 4
                let n = if wr.len() > 4 { wr[4] as usize } else { 0x14 };
                let mut st = vec![0u8; n];
                // Python's st[3:5] = version (slice assignment)
                let lo = 3.min(st.len());
                let hi = 5.min(st.len());
                st.splice(lo..hi, self.fw_version);
                st
            }
            _ => vec![],
        }
    }

    pub fn xfer(&mut self, wr: &[u8], rdn: usize) -> Option<Vec<u8>> {
        if self.nack {
            return None;
        }
        if !wr.is_empty() {
            if wr[0] == 0x80 && rdn > 0 {
                // read data
                let d = &self.reply;
                let mut o = vec![d.len() as u8];
                o.extend_from_slice(d);
                o.resize(o.len() + (rdn as isize - 1 - d.len() as isize).max(0) as usize, 0);
                return Some(o);
            }
            self.cmds.push(wr.to_vec());
            self.reply = self.answer(wr);
            return Some(vec![]);
        }
        if rdn > 0 {
            // ping status
            let mut o = vec![0x01 | ((self.reply.len() << 2) & 0xFC) as u8];
            o.resize(rdn, 0);
            return Some(o);
        }
        Some(vec![])
    }
}

/// CSU_PM's I2C slave (controller 3, address 4): a write's first two
/// bytes select a CUS_REG register (big-endian index), two more are
/// written to it; a read returns its two bytes (firmware/csupm
/// src/board/i2c_slave.c)
#[derive(Clone, Default)]
pub struct CsuPm {
    pub regs: std::collections::BTreeMap<u16, [u8; 2]>,
    pub reg: u16,
    /// CUS_TEMPT_REG1: degrees, hundredths
    pub soc_temp: (u8, u8),
    /// faults: the PM does not answer (NACK)
    pub nack: bool,
    /// what the EC wrote, in order: (register, value)
    pub writes: Vec<(u16, [u8; 2])>,
    /// answer as both PM firmwares really do (sky1emu pm-test --full,
    /// "I2C slave"): see `xfer`
    pub real: bool,
}

/// CIX's CUS_REG: 127 two-byte registers
pub const CUS_REG_MAX: u16 = 127;

impl CsuPm {
    /// As both PM firmwares answer (firmware/csupm src/i2c.c
    /// i2c_slave_isr, src/board/i2c_slave.c; sky1emu pm-test --full
    /// "I2C slave" finds CIX's and the open one the same): the first two
    /// bytes written select a register, the next two are written to it
    /// (a third byte alone writes byte 0); a register past 126 is neither
    /// written nor read (the controller's FIFO is left empty: 0xFF on
    /// the wire); a read gets the register's two bytes, then 0xFF;
    /// reading register 33's byte 1 (the fan mode Linux asked for) clears
    /// register 4's event bit. Register 32 is the SoC's temperature.
    fn xfer_real(&mut self, wr: &[u8], rdn: usize) -> Vec<u8> {
        if self.regs.is_empty() {
            self.regs.insert(33, [0, 3]);
        }
        if wr.len() >= 2 {
            self.reg = ((wr[0] as u16) << 8) | wr[1] as u16;
            if self.reg < CUS_REG_MAX && wr.len() >= 3 {
                let mut v = *self.regs.get(&self.reg).unwrap_or(&[0, 0]);
                v[0] = wr[2];
                if wr.len() >= 4 {
                    v[1] = wr[3];
                }
                self.regs.insert(self.reg, v);
                self.writes.push((self.reg, v));
            }
        }
        let mut o = Vec::new();
        if rdn > 0 {
            if self.reg < CUS_REG_MAX {
                let v = if self.reg == 32 { [self.soc_temp.0, self.soc_temp.1] } else { *self.regs.get(&self.reg).unwrap_or(&[0, 0]) };
                o.extend_from_slice(&v[..rdn.min(2)]);
                if self.reg == 33 && rdn >= 2 {
                    if let Some(r4) = self.regs.get_mut(&4) {
                        r4[1] &= 0xFE;
                    }
                }
            }
            o.resize(rdn, 0xFF);
        }
        o
    }

    pub fn xfer(&mut self, wr: &[u8], rdn: usize) -> Option<Vec<u8>> {
        if self.nack {
            return None;
        }
        if self.real {
            return Some(self.xfer_real(wr, rdn));
        }
        if wr.len() >= 2 {
            self.reg = ((wr[0] as u16) << 8) | wr[1] as u16;
            if wr.len() >= 4 {
                self.regs.insert(self.reg, [wr[2], wr[3]]);
                self.writes.push((self.reg, [wr[2], wr[3]]));
            }
        }
        if rdn > 0 {
            let v = if self.reg == 32 { [self.soc_temp.0, self.soc_temp.1] } else { *self.regs.get(&self.reg).unwrap_or(&[0, 0]) };
            let mut o = v.to_vec();
            o.resize(o.len() + rdn, 0);
            o.truncate(rdn);
            return Some(o);
        }
        Some(vec![])
    }
}

/// The board's state and the faults to inject.
#[derive(Clone)]
pub struct Board {
    pub soc: bool,
    pub csu: CsuPm,
    /// NTC temperatures (C) in the order of their ADC channel: SoC, board
    /// edge, headphone (none), DRAM, USB-C, PMIC; None: not fitted (reads 0)
    pub temps: [Option<f64>; 6],
    /// an ADC code that overrides the NTC (open: 1023, shorted: 0)
    pub ntc_raw: [Option<u16>; 6],
    pub fan_max_rpm: u32,
    /// the fan turns no matter the duty (tach reads 0 when stalled)
    pub fan_stalled: bool,
    pub pd: [Rts5453; 2],
    /// enables whose power good never comes (EC output pin)
    pub rail_dead: Vec<Pin>,
    pub rail_ms: f64,
}

pub const RAIL_MS: f64 = 3.0;

impl Board {
    pub fn attach(io: &mut Io) {
        let b = Board {
            soc: true,
            csu: CsuPm { soc_temp: (45, 0), ..Default::default() },
            temps: [Some(40.0), Some(35.0), None, Some(38.0), Some(33.0), Some(36.0)],
            ntc_raw: [None; 6],
            fan_max_rpm: 5000,
            fan_stalled: false,
            pd: [
                Rts5453 { reply: vec![], cmds: vec![], fw_version: [0x01, 0x09], nack: false },
                Rts5453 { reply: vec![], cmds: vec![], fw_version: [0x01, 0x0a], nack: false },
            ],
            rail_dead: Vec::new(),
            rail_ms: RAIL_MS,
        };
        io.board = Some(b);
        io.smb[2].devices.push((0x04, Dev::Csu));
        io.smb[5].devices.push((0x67, Dev::Pd(0)));
        io.smb[5].devices.push((0x68, Dev::Pd(1)));
        for (n, l) in [
            ("B3", 1), ("B0", 0), ("B1", 1), ("C0", 1), ("D1", 0), ("D4", 1), ("F5", 1), ("H0", 1), ("B5", 1), ("H2", 1),
            ("H4", 0), ("H6", 0), ("J3", 0), ("H7", 1), ("A3", 1), ("A6", 1), ("A7", 1), ("E1", 0), ("G2", 0),
        ] {
            io.pins[pin_id(n) as usize] = Some(l);
        }
    }
}

impl Io {
    pub fn board(&mut self) -> &mut Board {
        self.board.as_mut().expect("no board")
    }

    pub(super) fn dev_xfer(&mut self, dev: Dev, wr: &[u8], rdn: usize) -> Option<Vec<u8>> {
        // (extended model) CSU_PM is in the SoC: it answers only while the
        // SoC's always-on part is powered (ALW, 1.8V_S5, RSM_RST released)
        let soc_aon = self.pin(pin_id("D7")) != 0 && self.pin(pin_id("H5")) != 0 && self.pin(pin_id("E2")) == 0;
        let ext = self.ext;
        let b = self.board.as_mut()?;
        match dev {
            Dev::Csu if ext && !soc_aon => None,
            Dev::Csu => b.csu.xfer(wr, rdn),
            Dev::Pd(i) => b.pd[i].xfer(wr, rdn),
        }
    }

    /// The EC changed an output: what the board does about it.
    pub(super) fn board_output(&mut self, p: Pin, level: u8) {
        let b = self.board.as_ref().unwrap();
        let rail = b.rail_ms;
        let dead = b.rail_dead.contains(&p);
        let follow = |pg: &str| -> Option<Pin> { if dead { None } else { Some(pin_id(pg)) } };
        let name = super::chip::pin_name(p);
        let pg = match name.as_str() {
            "D7" => follow("J3"), // EC_ALW_EN -> SYS_ALW_PG
            "J6" => follow("H6"), // EC_SLP_S5_L -> S3 rails
            "D0" => follow("H4"), // EC_SLP_S3_L -> S0 rails
            "J4" if b.soc => {
                // PWRBTN_EC: the SoC's power button; the SoC (its always-on
                // domain) needs the ALW rails and RSM_RST released
                let powered = self.pin(pin_id("D7")) != 0 && self.pin(pin_id("H5")) != 0 && self.pin(pin_id("E2")) == 0;
                if level == 1 && powered && self.pin(pin_id("B5")) == 1 {
                    self.after(20.0, Box::new(|ec: &mut Ec| soc_on(ec)));
                } else if level == 1 && powered && self.pin(pin_id("B5")) == 0 && self.pin(pin_id("H0")) == 1 {
                    // in S3 (SLP_S5_L high, SLP_S3_L low): the button wakes the SoC
                    self.after(20.0, Box::new(|ec: &mut Ec| ec.io.drive(pin_id("H0"), 0)));
                }
                None
            }
            _ => None,
        };
        if let Some(pg) = pg {
            self.after(rail, Box::new(move |ec: &mut Ec| ec.io.drive(pg, level)));
        }
    }

    /// the fan: speed proportional to the PWM2 duty (DCR2 of 256)
    pub fn fan_rpm(&self) -> u32 {
        let b = self.board.as_ref().unwrap();
        if b.fan_stalled {
            return 0;
        }
        b.fan_max_rpm * self.x[0x1804] as u32 / 255
    }

    pub(super) fn board_read(&mut self, a: u16) -> Option<u8> {
        if self.board.is_none() {
            // (ec-ntc-table) no board: ADC channel 0 reads a fixed code
            let (lo, hi) = ADC_DATA[0];
            return match self.adc0 {
                Some(c) if a == lo => Some(c as u8),
                Some(c) if a == hi => Some((c >> 8) as u8),
                _ => None,
            };
        }
        if a == 0x181E || a == 0x181F {
            // fan 1 tachometer (F1TLRR, F1TMRR)
            let r = self.fan_rpm();
            let t = if r != 0 { (2156250 / r).min(0xFFFF) } else { 0 };
            return Some(if a == 0x181E { t as u8 } else { (t >> 8) as u8 });
        }
        let b = self.board.as_ref().unwrap();
        for (ch, &(lo, hi)) in ADC_DATA.iter().enumerate() {
            if a == lo || a == hi {
                let code = match b.ntc_raw[ch] {
                    Some(c) => c,
                    None => b.temps[ch].map_or(0, ntc_code),
                };
                return Some(if a == lo { code as u8 } else { (code >> 8) as u8 });
            }
        }
        None
    }
}

pub fn soc_on(ec: &mut Ec) {
    ec.io.drive(pin_id("B5"), 0); // SLP_S5_L high
    ec.after(5.0, Box::new(|ec: &mut Ec| ec.io.drive(pin_id("H0"), 0))); // SLP_S3_L high
    ec.after(30.0, Box::new(|ec: &mut Ec| ec.io.drive(pin_id("D1"), 1))); // reset released
}

/// the board drives `pin` to `level` at `ms`
pub fn drive_at(ec: &mut Ec, ms: f64, pin: &str, level: u8) {
    let p = pin_id(pin);
    ec.at(ms, Box::new(move |ec: &mut Ec| ec.io.drive(p, level)));
}

/// the OS shuts down: SLP_S3_L then SLP_S5_L go low
pub fn soc_off(ec: &mut Ec, at_ms: f64) {
    drive_at(ec, at_ms, "D1", 0);
    drive_at(ec, at_ms + 5.0, "H0", 1);
    drive_at(ec, at_ms + 10.0, "B5", 1);
}

/// the OS suspends to RAM: SLP_S3_L goes low (SLP_S5_L stays high)
pub fn soc_suspend(ec: &mut Ec, at_ms: f64) {
    drive_at(ec, at_ms, "H0", 1);
}

pub fn press(ec: &mut Ec, at_ms: f64, hold_ms: f64) {
    drive_at(ec, at_ms, "B3", 0);
    drive_at(ec, at_ms + hold_ms, "B3", 1);
}
