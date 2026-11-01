/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Temperatures and the fan, as Radxa's EC V1.07 runs them on the O6.
 *
 * Every 10 s while the SoC is on: the five board NTCs (SoC, board edge,
 * DRAM, USB-C, PMIC; ADC channels 0, 1, 3, 4, 5) are read, and the
 * hottest at 85 C or more raises THERMTRIP_EC; the SoC's temperature is
 * read from CSU_PM over I2C (controller 3, address 4, register 32:
 * degrees, hundredths); in the automatic modes it selects a state from
 * the vendor's thresholds with hysteresis and the state the fan duty.
 * The NTC conversion is the vendor's own (ntc_table.h).
 *
 * Fan modes (host command 0x52, UEFI's EcFanMode): 1 and 3 automatic,
 * 2 off ("mute"), 4 full ("performance"), 0 left alone.
 */
#include "ec.h"
#include "ntc_table.h"

uint8_t fan_mode = 1;
uint8_t fan_duty;
uint8_t soc_temp;
static uint8_t soc_frac;
static uint8_t tstate, last_temp;
static int8_t tick = -1;	/* counts 2 s hooks in S0 and shutdown; the vendor's first pass is the 6th */
static uint8_t off_done;
static uint8_t passes;
uint8_t trip_flag;

/* the vendor's tables (its initialised data at 0x93C1, 0x93C8, 0x93CF) */
static __code const uint8_t up[7] = { 0, 40, 50, 60, 65, 72, 200 };
static __code const uint8_t down[7] = { 0, 25, 46, 56, 62, 67, 200 };
static __code const uint8_t duty_tbl[7] = { 0, 20, 20, 30, 50, 75, 100 };

static __code const uint16_t adc_data[6] = { 0x1918, 0x1907, 0x190A, 0x190D, 0x193A, 0x193D };
static __code const uint16_t adc_data_hi[6] = { 0x1919, 0x1908, 0x190B, 0x190E, 0x1939, 0x193C };

void fan_init(void)
{
	/* ADC: channels 0-5 on the NTC inputs, as the vendor */
	ADCSTS = 0x08;
	ADCSTS = 0x80;
	ADCCFG = 0x00;
	KDCTL = 0x80;
	ADCCFG = 0x20;
	ADCCTL = 0x15;
	XREG(0x1A01) = 0x00;
	ADCSTS = 0x88;
	ADCSTS = 0x80;
	VCH0CTL = 0x80;
	ADCCFG = 0x21;
	VCH1CTL = 0x81;
	VCH2CTL = 0x82;
	VCH3CTL = 0x83;
	VCH4CTL = 0x90;
	VCH5CTL = 0x90;
	ADCGCR = 0x3F;

	/* PWM: fan on PWM2 (cycle 256), the power LED on PWM1 */
	ZTIER = 0x00;
	CTR0 = 0xC8;
	C4CPRS = 0x01;
	C4MCPRS = 0x00;
	C6CPRS = 0x01;
	C6MCPRS = 0x00;
	C7CPRS = 0x03;
	C7MCPRS = 0x00;
	PCSSGL = 0x88;
	PCSSGH = 0x7D;
	CR256PCSSG = 0xC0;
	CTR1 = 0xA2;
	CTR2 = 0xFF;
	CTR3 = 0xFF;
	ZTIER = 0x02;
	DCR0 = 0x00;
	DCR1 = 0xFF;
	DCR2 = 0x00;
	DCR3 = 0x00;
}

static uint16_t adc_read(uint8_t ch)
{
	return ((uint16_t)XREG(adc_data_hi[ch]) << 8) | XREG(adc_data[ch]);
}

/* the vendor's duty: (cycle + 1) * percent / 100, the full cycle at 100 */
void fan_set_pct(uint8_t pct)
{
	if (pct > 100)
		return;
	fan_duty = pct;
	DCR2 = pct == 100 ? CTR2 : (uint8_t)(((uint16_t)(CTR2 + 1) * pct) / 100);
}

/*
 * Host command 0x25: the vendor multiplies by the float 2.55
 * (0x40233333 = 10695475 / 2^22) in single precision and truncates.  The
 * product is rounded to 24 significant bits (to nearest, ties to even)
 * before the truncation, which this reproduces in integers.
 */
void fan_set_duty(uint16_t v)
{
	uint32_t p, half, mask;
	uint8_t bits = 0, sh;

	if (v > 400) {				/* past 32 bits: never a real duty */
		DCR2 = (uint8_t)((uint32_t)v * 255 / 100);
		return;
	}
	p = (uint32_t)v * 10695475UL;
	for (mask = p; mask; mask >>= 1)
		bits++;
	if (bits > 24) {
		sh = bits - 24;
		half = 1UL << (sh - 1);
		mask = (1UL << sh) - 1;
		if ((p & mask) > half || ((p & mask) == half && (p >> sh) & 1))
			p += (1UL << sh);
		p &= ~mask;
	}
	DCR2 = (uint8_t)(p >> 22);
}

/* target-speed mode (host command 0x21; 0x25 leaves it) */
uint8_t rpm_mode;
uint16_t rpm_target;
__xdata uint8_t rpm_meas[4];		/* the last measured speed, for 0x20 */
static uint8_t hook_cnt;

/* the vendor's speed: 2156250 / the tachometer count (0: stopped) */
static uint16_t tach_rpm(void)
{
	uint16_t t = ((uint16_t)F1TMRR << 8) | F1TLRR;

	if (!t)
		return 0;
	if (t == 0xFFFF)
		return 0xFFFF;
	return (uint16_t)(2156250UL / t);
}

uint16_t fan_rpm(void)
{
	return ((uint16_t)rpm_meas[2] << 8) | rpm_meas[3];
}

/*
 * The vendor's correction towards the target speed, every second 2 s
 * hook: nothing within 50 rpm; else the duty moves by one, plus
 * 2 * (error / 100) above 2500 rpm, towards the target.
 */
static void rpm_step(void)
{
	uint16_t rpm = tach_rpm(), aerr;
	int16_t err;
	uint8_t step, duty;

	rpm_meas[0] = 0;
	rpm_meas[1] = 0;
	rpm_meas[2] = rpm >> 8;
	rpm_meas[3] = rpm;
	if (rpm == rpm_target)
		return;
	err = (int16_t)(rpm_target - rpm);
	aerr = err < 0 ? -err : err;
	if (aerr < 50)
		return;
	step = rpm < 2500 ? 0 : (uint8_t)(2 * (aerr / 100));
	duty = DCR2;
	if (rpm_target > rpm) {
		if (duty != 0xFF)
			duty++;
		duty = (uint16_t)duty + step > 0xFF ? 0xFF : duty + step;
	} else {
		if (duty)
			duty--;
		if ((int16_t)duty - step >= 1)
			duty -= step;
	}
	DCR2 = duty;
}

/* the vendor's 2 s hook, the other half: every second time, the fan off
 * while the SoC is off, else the target-speed correction */
static void fan_hook2(void)
{
	uint8_t c = hook_cnt;

	hook_cnt = c >= 10 ? 0 : c + 1;
	if (c & 1)
		return;
	if (!SOC_SLP3() && rpm_mode)
		rpm_step();
}

static uint8_t tstate_update(uint8_t t)
{
	uint8_t i, old = tstate;

	if (t == last_temp)
		return 0;
	if (t > last_temp) {
		for (i = 0; i < 7; i++)
			if (t < up[i])
				break;
		if (i < 7 && i >= old)
			tstate = i;
	} else {
		for (i = 0; i < 7; i++)
			if (t < down[i])
				break;
		if (i < 7 && i < old)
			tstate = i;
	}
	last_temp = t;
	return tstate != old;
}

static void soc_temp_read(void)
{
	static __xdata uint8_t w[2] = { 0x00, 0x20 };
	static __xdata uint8_t r[2];

	if (i2c_xfer(SMB_C, 0x04, w, 2, 0, 0))
		return;
	delay_ms(25);
	if (i2c_xfer(SMB_C, 0x04, 0, 0, r, 2))
		return;
	soc_temp = r[0];
	soc_frac = r[1];
}

/*
 * Every fourth pass the vendor gives CSU_PM the board temperatures:
 * registers 16-21 (CUS_TEMPR) = ADC channels 0-5, degrees and tenths
 * (channel 2, the headphone input, as 0).
 */
static void pm_board_temps(void)
{
	static __xdata uint8_t w[4];
	uint8_t c;
	uint16_t a;

	for (c = 0; c < 6; c++) {
		w[0] = 0;
		w[1] = 16 + c;
		if (c == 2) {
			w[2] = 0;
			w[3] = 0;
		} else {
			a = adc_read(c) & 0x3FF;
			w[2] = (uint8_t)ntc_deg[a];
			w[3] = ntc_tenth[a];
		}
		i2c_xfer(SMB_C, 0x04, w, 4, 0, 0);
		delay_ms(40);
	}
}

void fan_poll_2s(void)
{
	fan_hook2();
	static __code const uint8_t ch[5] = { 0, 1, 3, 4, 5 };
	int8_t hot = -128, t;
	uint8_t i;

	if (SOC_SLP3()) {			/* the SoC is off */
		if (!off_done) {		/* the vendor stops the fan once */
			off_done = 1;
			fan_set_pct(0);
		}
	} else {
		off_done = 0;
	}
	if (power_state != PS_S0 && power_state != PS_SD)
		return;
	if (++tick < 5)
		return;
	tick = 0;
	for (i = 0; i < 5; i++) {
		t = ntc_deg[adc_read(ch[i]) & 0x3FF];
		if (t > hot)
			hot = t;
	}
	if (hot >= 85) {
		if (!PIN_GET(GPDRI, 7)) {
			/* as the vendor: THERMTRIP_EC, the power-off reason (2)
			 * kept in the e-flash, the watchdog set up, and the
			 * AC-loss policy off */
			THERMTRIP_EC(1);
			puts_("NTC overheat\r\n");
			eflash_write_reason(2);
			poweroff_reason = 2;
			ETWCFG = 0x30;
			trip_flag = 1;
		}
	} else {
		THERMTRIP_EC(0);
	}
	soc_temp_read();
	if (++passes == 4) {
		passes = 0;
		pm_board_temps();
	}
	switch (fan_mode) {
	case 1:
	case 3:
		/* the duty is written only when the state changes */
		if (tstate_update(soc_temp)) {
			fan_set_pct(duty_tbl[tstate]);
			rpm_mode = 0;		/* as the vendor: the state's duty wins */
		}
		break;
	case 2:
		fan_set_pct(0);
		break;
	case 4:
		fan_set_pct(100);
		break;
	default:
		break;
	}
}
