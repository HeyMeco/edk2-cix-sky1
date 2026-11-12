/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Power sequencing and the power button, as Radxa's EC V1.07 does it on
 * the Orion O6.  Its sequencer runs tables of (step, delay) entries; a
 * step that is not ready yet is retried after its delay.  The tables:
 *
 *   AC (power applied, EC reset):  1.8V_S5 off, 40 ms, ALW on, 1.8V_S5 on,
 *       wait SYS_ALW_PG, release RSM_RST: standby (S5).
 *   S5 -> S0 (button released, or the AC-loss policy):  a PWRBTN_EC pulse
 *       if the policy started it, wait for the SoC's SLP_S5/SLP_S3,
 *       GPJ7 and EC_SLP_S3_L on, wait SYS_S0_PG.
 *   G3 -> S0 (button in G3):  ALW, 1.8V_S5, wait SYS_ALW_PG and LID_SW_L,
 *       release RSM_RST, a PWRBTN_EC pulse, wait for the SoC, GPJ7 and
 *       EC_SLP_S3_L, TP91 and the PMIC enable gate, wait SYS_S0_PG.
 *
 * The SoC's sleep signals are acted on as the vendor's handler (bank 2
 * 0xC423) does, a few ms after SLP_S3 goes low, with SLP_S5 as it is
 * then: EC_SLP_S3_L low; with SLP_S5 low too, GPJ7 low ("s5 n_off") and
 * shutdown (SD), else suspend to RAM (STR); the PMIC gate low in both.
 * SLP_S3 high again in STR (SLP_S5 high) is the resume: GPJ7,
 * EC_SLP_S3_L and the PMIC gate high, S0.  SLP_S5 low in STR is a
 * shutdown from S3 (GPJ7 left as the vendor leaves it).  In the ~2 s
 * hook, as the vendor's state-change hook (bank 1 0xCCEF): S0 -> STR or
 * SD mutes the audio (de-pop line low, 0xCB43); STR, SD or G3 -> S0
 * unmutes it and runs a thermal pass at once (0xCB71); the second hook
 * in SD turns the always-on rails off (G3).  The button is mirrored to
 * the SoC's (PWRBTN_EC) whenever it changes; a release in S0 is a host
 * event (EC_ACPI_INT); in SD the mirrored button starts the SoC again,
 * whose SLP signals then bring the board back as from STR.  SYS_S0_PG or SYS_S3_PG lost while the SoC is on
 * is recorded as the power-off reason (4, 8) in the e-flash, as the
 * vendor does (it does not turn anything off).
 */
#include "ec.h"

uint8_t power_state = PS_G3;
uint8_t auto_power_on;
uint8_t poweroff_reason;

typedef uint8_t (*step_fn)(void);

struct step {
	step_fn fn;
	uint8_t delay;
};

static __code const struct step *seq;
static uint8_t seq_len, seq_i;
static uint16_t seq_at;
static uint8_t seq_auto;		/* S5 -> S0 started by the policy */

static uint8_t btn, btn_cnt;
static uint8_t slp3, slp5;
static uint8_t sd_stage;
static uint8_t g3_press;
static uint16_t g3_at;
static uint8_t s0_post, s0_pd;
static uint16_t s0_at;
static uint8_t evt_pending;
static uint8_t prev_state = 0xFF;	/* the vendor's 0x923E: the state its hook saw last
					 * (0xFF: none yet, its G3PP after AC) */
static uint8_t slp3_wait;
static uint16_t slp3_at;
static uint8_t pg_s0, pg_s3;

/* ---- steps ---- */
static uint8_t st_s5_off(void)	{ S5_1V8_EN(0); return 1; }
static uint8_t st_alw_on(void)	{ EC_ALW_EN(1); return 1; }
static uint8_t st_s5_on(void)	{ S5_1V8_EN(1); return 1; }
static uint8_t st_alw_pg(void)	{ return SYS_ALW_PG(); }
static uint8_t st_lid(void)	{ return LID_SW_L(); }
static uint8_t st_nop(void)	{ return 1; }

static uint8_t st_rsm(void)
{
	SMFI_1063 |= 0x01;
	TP89(1);
	RSM_RST(0);
	return 1;
}

static void pwrbtn_pulse(uint8_t ms)
{
	PWRBTN_EC(1);
	delay_ms(ms);
	PWRBTN_EC(0);
}

static uint8_t st_auto_pulse(void)
{
	if (seq_auto)
		pwrbtn_pulse(2);
	return 1;
}

static uint8_t st_pulse(void)
{
	pwrbtn_pulse(4);
	return 1;
}

static uint8_t st_soc_on(void)
{
	return !SOC_SLP3() && !SOC_SLP5();
}

static uint8_t st_s3_on(void)
{
	GPJ7(1);
	EC_SLP_S3_L(1);
	return 1;
}

static uint8_t st_gate(void)
{
	TP91(1);
	PMIC_EN_GATE_L(1);
	return 1;
}

static uint8_t st_s0_pg(void)	{ return SYS_S0_PG(); }

static __code const struct step seq_ac[] = {
	{ st_s5_off, 40 }, { st_alw_on, 0 }, { st_s5_on, 0 },
	{ st_alw_pg, 20 }, { st_rsm, 0 },
};

static __code const struct step seq_s0[] = {
	{ st_auto_pulse, 20 }, { st_nop, 0 }, { st_soc_on, 20 },
	{ st_s3_on, 10 }, { st_nop, 0 }, { st_nop, 0 }, { st_nop, 20 },
	{ st_s0_pg, 0 }, { st_nop, 0 },
};

static __code const struct step seq_g3[] = {
	{ st_alw_on, 0 }, { st_s5_on, 0 }, { st_alw_pg, 20 },
	{ st_lid, 0 }, { st_rsm, 20 }, { st_pulse, 0 }, { st_soc_on, 20 },
	{ st_s3_on, 10 }, { st_nop, 0 }, { st_gate, 0 }, { st_s0_pg, 0 },
};

#define SEQ_AC 1
#define SEQ_S0 2
#define SEQ_G3 3
static uint8_t seq_id;

static void seq_start(uint8_t id)
{
	seq_id = id;
	if (id == SEQ_AC) {
		seq = seq_ac;
		seq_len = sizeof(seq_ac) / sizeof(seq_ac[0]);
	} else if (id == SEQ_S0) {
		seq = seq_s0;
		seq_len = sizeof(seq_s0) / sizeof(seq_s0[0]);
	} else {
		seq = seq_g3;
		seq_len = sizeof(seq_g3) / sizeof(seq_g3[0]);
	}
	seq_i = 0;
	seq_at = ms_now;
}

static void seq_done(void)
{
	uint8_t id = seq_id;

	seq_id = 0;
	if (id == SEQ_AC) {
		power_state = PS_S5;
		puts_("standby\r\n");
		/* the vendor powers on with the policy 1, and with the
		 * e-flash byte erased (0xFF: a freshly mirrored image); it
		 * sets the PD controllers up only when it stays in standby */
		if (auto_power_on == 1 || auto_power_on == 0xFF) {
			seq_auto = 1;
			seq_start(SEQ_S0);
		} else {
			pd_init_all();
		}
		return;
	}
	power_state = PS_S0;
	seq_auto = 0;
	if (id == SEQ_S0)
		fan_s0();
	/* the vendor sets the PD controllers up again only after S5 -> S0 */
	s0_pd = id == SEQ_S0;
	s0_post = 1;
	s0_at = ms_now + 550;
	puts_("S0\r\n");
}

static void seq_run(void)
{
	if (!seq_id || !after(seq_at))
		return;
	if (seq[seq_i].fn()) {
		seq_at = ms_now + seq[seq_i].delay;
		if (++seq_i == seq_len)
			seq_done();
	} else {
		seq_at = ms_now + (seq[seq_i].delay ? seq[seq_i].delay : 10);
	}
}

/* ---- the button ---- */
static void button(void)
{
	uint8_t p = PWRBTN_PRESSED();

	if (p == btn) {
		btn_cnt = 0;
		return;
	}
	if (++btn_cnt < 4)			/* 4 ms debounce */
		return;
	btn_cnt = 0;
	btn = p;
	PWRBTN_EC(p);
	if (p)
		return;
	/* released */
	if (power_state == PS_S5 && !seq_id) {
		seq_start(SEQ_S0);
	} else if (power_state == PS_G3 && !seq_id) {
		g3_press = 1;
		g3_at = ms_now + 1300;		/* the vendor's slow hook */
	}
	if (power_state == PS_S0 || seq_id == SEQ_S0)
		evt_pending = 1;
}

/* ---- the SoC's sleep signals ---- */
static void soc_signals(void)
{
	uint8_t s3 = SOC_SLP3(), s5 = SOC_SLP5();

	if (s3 != slp3) {
		slp3 = s3;
		if (s3 && power_state == PS_S0) {
			/* the vendor acts on it ~8 ms later, with SLP_S5 as it is then */
			slp3_wait = 1;
			slp3_at = ms_now + 8;
		} else if (!s3 && (power_state == PS_STR || power_state == PS_SD) && !s5) {
			/* the SoC resumes from suspend to RAM, or comes back on
			 * before the rails went off (the button in SD) */
			GPJ7(1);
			EC_SLP_S3_L(1);
			PMIC_EN_GATE_L(1);
			power_state = PS_S0;
			puts_("resume\r\n");
		}
	}
	if (slp3_wait && after(slp3_at)) {
		slp3_wait = 0;
		if (SOC_SLP3() && power_state == PS_S0) {
			EC_SLP_S3_L(0);			/* "s3 n_off" */
			if (SOC_SLP5()) {
				GPJ7(0);		/* "s5 n_off" */
				power_state = PS_SD;
				sd_stage = 0;
				puts_("shutdown\r\n");
			} else {
				power_state = PS_STR;
				puts_("suspend\r\n");
			}
			PMIC_EN_GATE_L(0);
		}
	}
	if (s5 != slp5) {
		slp5 = s5;
		if (s5 && power_state == PS_STR) {
			power_state = PS_SD;		/* off from S3 */
			sd_stage = 0;
			puts_("shutdown\r\n");
		}
	}
}

/* power good lost while the SoC is on: the reason kept in the e-flash */
static void power_goods(void)
{
	uint8_t s0 = SYS_S0_PG(), s3 = SYS_S3_PG();
	uint8_t on = !SOC_SLP3() && !SOC_SLP5();

	if (s0 != pg_s0) {
		pg_s0 = s0;
		if (!s0 && on)
			eflash_write_reason(4);
	}
	if (s3 != pg_s3) {
		pg_s3 = s3;
		if (!s3 && on)
			eflash_write_reason(8);
	}
}

/* the vendor's ~2 s hook: its state-change handlers, and the step from
 * shutdown to G3 at the second tick in SD */
void power_2s(void)
{
	/* (the vendor's standby loop does not run its state-change hook) */
	if (power_state != prev_state && power_state != PS_S5) {
		if (power_state == PS_S0 &&
		    (prev_state == PS_STR || prev_state == PS_SD || prev_state == PS_G3)) {
			fan_resume();			/* "OS resume" */
			DEPOP_MUTE_L(1);
		} else if ((power_state == PS_STR || power_state == PS_SD) && prev_state == PS_S0) {
			DEPOP_MUTE_L(0);		/* "OS suspend" */
		}
		prev_state = power_state;
	}
	if (power_state != PS_SD)
		return;
	if (sd_stage == 0) {
		sd_stage = 1;
	} else {
		S5_1V8_EN(0);
		EC_ALW_EN(0);
		power_state = PS_G3;
		puts_("G3\r\n");
	}
}

/* once in S0: the test points and the TPM's power, then the PD set-up */
static void s0_post_run(void)
{
	if (!s0_post || !after(s0_at))
		return;
	if (s0_post == 1) {
		TP91(1);
		PIN_HI(GPDRC, 3);		/* TP92 */
		PIN_HI(GPDRE, 4);		/* TP99 */
		PIN_HI(GPDRJ, 2);		/* TPM_PWR_EN */
		PIN_HI(GPDRM, 5);
		s0_post = 2;
		s0_at = ms_now + 520;
	} else {
		if (s0_pd)
			pd_init_all_status();
		s0_post = 0;
	}
}

void power_init(void)
{
	auto_power_on = eflash_read(0x1F8FF);
	/* the vendor's power-off reason record: 4 bytes at 0x1FCFC */
	poweroff_reason = eflash_read(0x1FCFF);
	btn = PWRBTN_PRESSED();
	slp3 = SOC_SLP3();
	slp5 = SOC_SLP5();
	pg_s0 = SYS_S0_PG();
	pg_s3 = SYS_S3_PG();
	puts_("AC on, policy ");
	puthex(auto_power_on);
	puts_("\r\n");
	seq_start(SEQ_AC);
	/* the vendor's start-up takes ~690 ms before its first step; keep
	 * the rails' timing after an EC reset the same */
	seq_at = ms_now + 680;
}

void power_1ms(void)
{
	button();
	soc_signals();
	power_goods();
	seq_run();
	s0_post_run();
	pd_poll();
	if (g3_press && after(g3_at) && power_state == PS_G3) {
		g3_press = 0;
		seq_start(SEQ_G3);
	}
	if (evt_pending && power_state == PS_S0 && !s0_post && !pd_busy()) {
		evt_pending = 0;
		host_event(EVT_POWER_BUTTON);
	}
}

/* a power sequence or the S0 set-up still running, or the button held
 * in standby (the vendor's standby loop waits for its release) */
uint8_t power_busy(void)
{
	return seq_id || s0_post || (power_state == PS_S5 && btn);
}

/*
 * Host command 3E19: UEFI's platform-specific reset.  The vendor saves the
 * AC-loss policy, turns the always-on rails off and resets the EC through
 * the watchdog with the mirror bits set, which copies the EC image from
 * the shared SPI flash into the e-flash (how a new EC firmware gets in).
 */
void power_force_reset(void)
{
	puts_("EC mirror\r\n");
	eflash_write_byte(0x1F8FF, auto_power_on);
	S5_1V8_EN(0);
	EC_ALW_EN(0);
	mirror_reset();
}
