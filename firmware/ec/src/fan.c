/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Temperatures and the fan, as Radxa's EC V1.07 runs them on the O6.
 *
 * Every fifth ~2 s hook (the vendor's 0x9237), in S0, suspend (STR) and
 * shutdown (SD): the five board NTCs (SoC, board edge, DRAM, USB-C, PMIC;
 * ADC channels 0, 1, 3, 4, 5) are read, and the hottest at 85 C or more
 * raises THERMTRIP_EC; the SoC's temperature is read from CSU_PM over I2C
 * (controller 3, address 4, register 32: degrees, hundredths; when it
 * does not answer, the last one read, 25 C before any, as the vendor); in the automatic modes it selects a
 * state from the vendor's thresholds with hysteresis, and the state's
 * duty is set when it is not the one set last (0x93E4).  A change of fan
 * mode, and a resume to S0 from STR, SD or G3, forget the state set, so
 * its duty is set again (the vendor's 0xDC42, 0xCB71).  Every other hook
 * the fan is stopped in STR and SD.  Standby (S5) is the vendor's SD
 * state: passes run there too, and the fan is stopped.  The NTC conversion is the vendor's
 * own (ntc_table.h).
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
static uint8_t tick;		/* counts slow hooks (0x9237): a pass at every 5th */
static uint8_t passes;		/* the vendor's 0x9238 */
static uint8_t applied;		/* the state whose duty was set (0x93E4) */
static uint8_t last_mode = 1;	/* the mode of the last pass (0x93A7) */
static uint8_t state_temp = 25;	/* the temperature the states use: the last read, 25 C before any */
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
uint16_t rpm_target;		/* the target's low 16 bits */
uint8_t rpm_target_hi;		/* the target is 65536 or more */
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
	/* as the vendor: which way from the whole target, how far from its
	 * low 16 bits */
	if (rpm == rpm_target && !rpm_target_hi)
		return;
	err = (int16_t)(rpm_target - rpm);
	aerr = err < 0 ? -err : err;
	if (aerr < 50)
		return;
	step = rpm < 2500 ? 0 : (uint8_t)(2 * (aerr / 100));
	duty = DCR2;
	if (rpm_target_hi || rpm_target > rpm) {
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

/* the vendor's 2 s hook, the other half (0xC4D7): every second time, the
 * fan off in STR and SD, else the target-speed correction */
static void fan_hook2(void)
{
	uint8_t c = hook_cnt;

	hook_cnt = c >= 10 ? 0 : c + 1;
	if (c & 1)
		return;
	if (power_state == PS_STR || power_state == PS_SD || power_state == PS_S5)
		DCR2 = 0;
	else if (!SOC_SLP3() && rpm_mode)
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

/* the SoC's temperature from CSU_PM; 0 if it did not answer */
static uint8_t soc_temp_read(void)
{
	static __xdata uint8_t w[2] = { 0x00, 0x20 };
	static __xdata uint8_t r[2];
	int8_t e;

	e = i2c_xfer(SMB_C, 0x04, w, 2, 0, 0);
	delay_ms(25);
	if (i2c_xfer(SMB_C, 0x04, 0, 0, r, 2) || e)
		return 0;
	soc_temp = r[0];
	soc_frac = r[1];
	return 1;
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

/* the vendor's thermal pass (bank 1 0xDB7C) */
static void thermal_pass(void)
{
	static __code const uint8_t ch[5] = { 0, 1, 3, 4, 5 };
	int8_t hot = -128, t;
	uint8_t i, temp;

	for (i = 0; i < 5; i++) {
		t = ntc_deg[adc_read(ch[i]) & 0x3FF];
		if (t > hot)
			hot = t;
	}
	if (hot >= 85) {
		/* as the vendor: THERMTRIP_EC, the power-off reason (2) kept
		 * in the e-flash, and the external watchdog set to reset the
		 * EC ~32 s later (1.024 kHz, 0x7FF8), again at every pass
		 * while it is hot (so the reset comes 32 s after the passes
		 * stop or it cools).  The vendor writes the reason at every
		 * such pass too (an e-flash erase each time); here only at
		 * the first. */
		if (!PIN_GET(GPDRI, 7)) {
			THERMTRIP_EC(1);
			puts_("NTC overheat\r\n");
			eflash_write_reason(2);	/* (3E09 reports it after the next start) */
		}
		{
			XREG(0x1F02) = 0x00;		/* ETPSR */
			XREG(0x1F04) = 0xFF;		/* ET1CNTLLR */
			ETWCFG = 0x30;			/* key enable, WDT clock 1.024 kHz */
			XREG(0x1F05) = 0x10;		/* ETWCTRL */
			XREG(0x1F09) = 0x7F;		/* EWDCNTLHR */
			XREG(0x1F06) = 0xF8;		/* EWDCNTLLR: starts it */
			trip_flag = 1;
		}
	} else {
		THERMTRIP_EC(0);
	}
	if (soc_temp_read())
		state_temp = soc_temp;
	temp = state_temp;
	if (fan_mode != last_mode) {
		last_mode = fan_mode;
		applied = 0;
	}
	switch (fan_mode) {
	case 1:
	case 3:
		tstate_update(temp);
		if (tstate != applied) {
			applied = tstate;
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

/* standby -> S0: the vendor's pass count starts again */
void fan_s0(void)
{
	tick = 0;
}

/* back in S0 from STR, SD or G3: in the automatic modes, the duty set again now */
void fan_resume(void)
{
	if (fan_mode == 1 || fan_mode == 3) {
		applied = 0;
		thermal_pass();
	}
}

void fan_poll_2s(void)
{
	/* in S0 the vendor's fan hook (0xC4D7) runs just before its pass
	 * counter's (0xC505); in its other loops just after */
	uint8_t s0 = power_state == PS_S0;

	if (s0)
		fan_hook2();
	if (++tick < 5)
		goto hook2;
	tick = 0;
	passes++;
	/* (standby is the vendor's SD as well) */
	if (power_state == PS_S0 || power_state == PS_STR || power_state == PS_SD || power_state == PS_S5)
		thermal_pass();
	/* the board temperatures to CSU_PM at the 4th pass of every 12 */
	if (passes == 4)
		pm_board_temps();
	else if (passes >= 12)
		passes = 0;
hook2:
	if (!s0)
		fan_hook2();
}
