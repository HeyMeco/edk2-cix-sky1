/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Suspend, resume and shutdown (CIX's Source/projects/sky1/subsys_hsm.c,
 * the parts after power-on; ss_por_enter, ss_init_enter and ss_run_enter
 * are main.c's).
 *
 * Suspend (TF-A: FFA_SUSPEND to the SE, PM message 0x42 with the boot
 * core, SCMI SYSTEM_POWER_STATE_SET suspend): trigger_STR sets the target;
 * from the idle loop the HSM leaves RUN (the mailboxes' state saved), goes
 * through PTS into S3: the boot core and the DSU idle, the SE's "suspended"
 * (2 in mailbox 8's fast channel), the clock tree saved, interrupts masked
 * but the S5 controller's, DRAM into self-refresh with the DDR controllers'
 * state saved (src/mclp), the AP's counter slowed, the core clock down to
 * 100 MHz, the sleep type to the S5 controller.  The PM then sleeps.
 *
 * Wake-up: the S5 controller's interrupt, the SE's "resumed" (1 in the
 * fast channel), the core clock back, the debug UART and watchdog, the
 * sensor fusion hub's wake-up off; then 0xaabb to the SE and back to RUN
 * (or, when CIX's audio workaround says so, 0xaacc: the SE starts the
 * sensor fusion hub and the PM suspends again).  Leaving S3: mailboxes
 * reset, AXI protection, TLB windows, I2C, the rails' voltages, the clock
 * tree, the counter, DRAM out of self-refresh with the controllers
 * restored; then main.c's ss_run as at boot, and (CIX's on_sys_pwr_event)
 * the UEFI's S3 boot script, the boot core and DSU powered up.
 *
 * Shutdown: through PTS into S5: the S5 interrupt, the core clock down,
 * sleep type 2.  Warm reset: the S5 controller's warm reset.
 *
 * CIX's RTOS suspends its tasks around the sequence; here the I2C workers
 * finish their jobs first (rtos_quiesce) and the sequence runs in the
 * SCP-firmware task, which nothing else can run beside (the scheduler is
 * co-operative), its I2C polled.  The tick only runs while a task waits
 * with a time-out, so there is none to stop.  Not here: the log messages.  The performance
 * streams (suspended here, restored by ss_run) and the SoC idle switch
 * are src/perf's.  The fans (src/fan), the board
 * services' timers (pm_svc_pts/run, as CIX's md_timer), the MP2845s'
 * PSI setting (src/pmic.c) and the watchdog (src/dpm/pm_config.c) are
 * their own files'.
 */
#include "hsm/hsm.h"
#include "board/board.h"
#include "dpm/dvfs_cfg.h"
#include "fan/fan.h"
#include "i2c.h"
#include "irq.h"
#include "lcru.h"
#include "log/log.h"
#include "mbox.h"
#include "mclp/mclp.h"
#include "msg/msg.h"
#include "perf/perf.h"
#include "plat.h"
#include "pmic.h"
#include "rtos.h"
#include "rtt/rtt.h"
#include "s3script/s3script.h"
#include "soc.h"
#include "uart.h"

#include <mod_pm_svc.h>

volatile uint32_t stdby_target = STDBY_NONE;
bool trigger_slpTyp = true;
uint32_t boot_core_id;

/* CIX's g_mp_sel fields */
static bool soc_idle_switch_was_enabled;
static uint8_t audio_clk_gate_and_reset_n;	/* the audio block's power, clock, reset */
static bool audio_war_full_str_retry;
static uint32_t audio_pgfsm_ctrl;
static uint32_t irq_enable_status;

/* CIX's resumeEvt (md_event: bits 7, any, no auto clear) and its callback */
static uint32_t resume_evt;
static bool resume_evt_signalled;

/* CIX's g_pm_str_retry_postpone_timerId: suspend again in 1 s */
#define STR_POSTPONE_US		1000000u
static bool str_postponed;
static uint64_t str_postpone_at;

/* Registers of the audio block and the sensor fusion hub */
#define AUDIO_RCSU_RST		0x56000800u	/* bit 0: the audio subsystem's RCSU out of reset */
#define AUDIO_PWR		0x56000404u	/* [1:0] */
#define AUDIO_CLK		0xa006211cu	/* bit 16 */
#define AUDIO_PGFSM		0x47000020u
#define AUDIO_PGFSM_ON		BIT(23)
#define AUDIO_ID		0x07110004u	/* SoC address */
#define AUDIO_ID_VALUE		0x20220000u
#define SFH_S5_EN		0x56000008u
#define SFH_S5_RST		0x5600030cu
#define SFH_CLK			0x48000034u
#define SFH_WAKE		0x48080080u
#define SFH_TIMER		0x480c00acu	/* writes of 3 words, then +0xc bit 2 when done */

/* The DSU's and the cores' power policy units */
#define PPU_DSU			0x4f030000u
#define PPU_CORE(c)		(0x4f080000u + ((uint32_t)(c) << 20))
#define PPU_PWPR		0x0
#define PPU_PWSR		0x8
#define PPU_ON			8u
#define CORE_RCSU		0x4fc50000u	/* CIX's g_rcsu_id2base[34]: the big cores' SRAM repair */

/* The SE's mailbox (CIX's call_se_to_suspend sends it the AP's FFA_SUSPEND) */
#define SE_MBOX			0x45080000u

/* --- requests (CIX's trigger_*) ----------------------------------------------- */

void trigger_STR(void)
{
	soc_idle_switch_was_enabled = perf_soc_idle_switch_get_enable();
	if (soc_idle_switch_was_enabled)
		(void)perf_soc_idle_switch_set_enable(false);
	stdby_target = HSM_ST_S3;
}

void trigger_SD(void)
{
	(void)perf_soc_idle_switch_set_enable(false);
	stdby_target = HSM_ST_S5;
}

void trigger_RST(void)
{
	stdby_target = HSM_ST_POR;
}

void setSlpType(bool enable)
{
	trigger_slpTyp = enable;
}

/* CIX's pm_str_postpone, from its timer */
static void pm_str_postpone(void)
{
	trigger_STR();
}

/* --- interrupts around S3 (CIX's irq_save_and_mask, irq_restore) ------------- */

static uint32_t rsr_intenable(void)
{
	uint32_t v;

	__asm__ volatile("rsr.intenable %0" : "=a"(v));
	return v;
}

static void wsr_intenable(uint32_t v)
{
	__asm__ volatile("wsr.intenable %0; rsync" : : "a"(v) : "memory");
}

/* every line but the S5 controller's (CIX then also disables its tick, 7) */
static void irq_save_and_mask(void)
{
	irq_enable_status = rsr_intenable();
	wsr_intenable(irq_enable_status & BIT(S5_PM_IRQ_LINE));
}

static void irq_restore(void)
{
	if (irq_enable_status) {
		wsr_intenable(irq_enable_status);
		irq_enable_status = 0;
	}
}

/* --- the audio block's divider workaround ------------------------------------ */

static void audio_pwr_clk_rst_stats(void)
{
	audio_pgfsm_ctrl = 0;
	audio_clk_gate_and_reset_n = (uint8_t)(REG32(AUDIO_PWR) & 3);
	audio_clk_gate_and_reset_n += (uint8_t)(((REG32(AUDIO_CLK) >> 16) & 1) << 2);
	audio_clk_gate_and_reset_n += (uint8_t)((REG32(AUDIO_RCSU_RST) & 1) << 3);
	audio_pgfsm_ctrl = REG32(AUDIO_PGFSM);
}

static void audio_pwr_clk_rst_restore(void)
{
	uint32_t v;

	REG32(AUDIO_PGFSM) = audio_pgfsm_ctrl;
	us_delay(50);
	REG32(AUDIO_PWR) = (REG32(AUDIO_PWR) >> 2 << 2) + (audio_clk_gate_and_reset_n & 3);
	us_delay(5);
	v = REG32(AUDIO_CLK);
	REG32(AUDIO_CLK) = audio_clk_gate_and_reset_n & 4 ? v | BIT(16) : v & ~BIT(16);
	v = REG32(AUDIO_RCSU_RST);
	REG32(AUDIO_RCSU_RST) = audio_clk_gate_and_reset_n & 8 ? v | 1 : v & ~1u;
}

static void audio_ss_toggle_rcsu_rst(void)
{
	REG32(AUDIO_RCSU_RST) &= ~1u;
	us_delay(20);
	REG32(AUDIO_RCSU_RST) |= 1;
}

static void audio_ss_off(void)
{
	REG32(AUDIO_PGFSM) &= ~1u;
	us_delay(20);
	REG32(AUDIO_PWR) &= ~3u;
	us_delay(5);
	REG32(AUDIO_CLK) &= ~BIT(16);
}

static void audio_ss_on(void)
{
	REG32(AUDIO_CLK) |= BIT(16);
	us_delay(5);
	REG32(AUDIO_PWR) |= 3;
	us_delay(5);
	REG32(AUDIO_PGFSM) |= 0xfff;
	us_delay(50);
	/* up to 10 tries for the power FSM to reach on (3); CIX logs the others */
	for (int i = -10; i; i++) {
		if (((REG32(AUDIO_PGFSM) >> 12) & 7) == 3) {
			us_delay(5);
			break;
		}
	}
	REG32(0x47000034) = BIT(29);
	REG32(0x47001000) = 0x1190;
	REG32(0x47001000) = 0x80001190;
}

static bool audio_probe(void)
{
	return soc_read32(AUDIO_ID) == AUDIO_ID_VALUE;
}

/*
 * CIX's audio_div_workaround: with the audio block's power FSM on, power
 * it off and on again and check it answers; false when it does not (then
 * CIX resets at boot, and suspends again after a wake-up).
 */
bool audio_div_workaround(void)
{
	audio_ss_toggle_rcsu_rst();
	if (!(REG32(AUDIO_PGFSM) & AUDIO_PGFSM_ON))
		return true;
	audio_pwr_clk_rst_stats();
	audio_ss_off();
	audio_ss_on();
	if (!audio_probe())
		return false;
	audio_ss_off();
	audio_pwr_clk_rst_restore();
	return true;
}

/* --- the sensor fusion hub ---------------------------------------------------- */

void sensor_hub_wakeup_set(bool enable)
{
	uint32_t v;

	REG32(SFH_S5_EN) |= 1;
	if (!REG32(SFH_S5_RST))
		REG32(SFH_S5_RST) = 1;
	us_delay(10);
	REG32(SFH_CLK) = 0x50000000;
	v = REG32(SFH_WAKE);
	REG32(SFH_WAKE) = enable ? v | 3 : v & ~3u;
}

static void sensor_hub_timeout_set(void)
{
	const uint32_t cmd[3] = { 12, 0xfff7, 5000 };

	REG32(SFH_S5_EN) |= 1;
	if (!REG32(SFH_S5_RST)) {
		REG32(SFH_S5_RST) = 1;
		us_delay(10);
	}
	REG32(SFH_CLK) = 0x50000000;
	REG32(SFH_TIMER + 0xc) = 3;
	for (unsigned i = 0; i < 3; i++)
		REG32(SFH_TIMER) = cmd[i];
	while (!(REG32(SFH_TIMER + 8) & 4))
		;
}

/*
 * CIX's call_se_to_suspend: send the SE the AP's FFA_SUSPEND (0x82000007)
 * in its mailbox, as TF-A would: wait for the channel (100 ms if it is
 * not free), the message, the doorbell, then wait for the SE to take it.
 */
static void call_se_to_suspend(void)
{
	if (!(REG32(SE_MBOX + 0x84) & 1))
		us_delay(100000);
	REG32(SE_MBOX + 0x84) &= ~1u;
	REG32(SE_MBOX + 0x00) = 12;
	REG32(SE_MBOX + 0x04) = 0x82000007;
	REG32(SE_MBOX + 0x08) = 2;
	REG32(SE_MBOX + 0x80) = 1;
	while (REG32(SE_MBOX + 0x84) & 1)
		us_delay(1);
	REG32(SE_MBOX + 0x84) |= 1;
}

/*
 * CIX's audio_div_war_retry: ask the SE to suspend; at once (wait for its
 * "suspended", 200 ms, sleep type 1) or, from on_sys_pwr_event, later
 * (pm_str_postpone in 1 s).
 */
static void audio_div_war_retry(bool now)
{
	call_se_to_suspend();
	if (!now) {
		str_postponed = true;
		str_postpone_at = arch_getTimeNowUs() + STR_POSTPONE_US;
		return;
	}
	(void)soc_pull_register(PM_FAST_CHANNEL, ~0u, 2, 0);
	us_delay(200000);
	REG32(PM_FAST_CHANNEL) = 0;
	REG32(S5_SLP_TYP) = 1;
}

/* CIX's trigger_soc_re_str: the audio workaround failed after a wake-up */
static void trigger_soc_re_str(void)
{
	audio_war_full_str_retry = true;
	sensor_hub_wakeup_set(true);
	us_delay(1000);
	audio_div_war_retry(false);
}

/* --- the boot core and the DSU ------------------------------------------------ */

/* CIX's do_mem_repair: an RCSU's SRAM repair (SoC address base) */
void do_mem_repair(uint32_t base, uint32_t enable)
{
	(void)soc_pull_register((base + 24) | SOC_WINDOW, 0xffff0000u, 0, 100000);
	REG32((base + 20) | SOC_WINDOW) = enable;
	(void)soc_pull_register((base + 16) | SOC_WINDOW, 6, 6, 800000);
	REG32((base + 20) | SOC_WINDOW) = 0;
}

/* the boot core's clock gate (CIX asserts the node is a gate) */
static void boot_core_gate(uint32_t core, bool on)
{
	CLK_TREE_STRUCT *st = clk_nodes_get(dpm_elem_get_clk_id(core));

	CLK_ASSERT((st->mass_ctrl0 & (15u << 19)) == (3u << 20), 0xf19f0907);
	gate_ctrl_set_state(st->gate.reg, st->gate.mask, on);
}

static void wait_for_bsp_and_dsu_idle(void)
{
	uint32_t timeout = audio_war_full_str_retry ? 20000000 : 800000000;

	(void)soc_pull_register(PPU_CORE(boot_core_id) + PPU_PWSR, 15, 0, timeout);
	(void)soc_pull_register(PPU_DSU + PPU_PWSR, 15, 0, timeout);
	if (memr_info & 2)
		boot_core_gate(boot_core_id, false);
}

static void power_up_dsu_and_bsp(void)
{
	REG32(PPU_DSU + PPU_PWPR) = PPU_ON;
	(void)soc_pull_register(PPU_DSU + PPU_PWSR, 15, PPU_ON, 0);
	if (memr_info & 2) {
		/* the big cores (4 and up) repair their SRAM first */
		if (boot_core_id >= 4)
			do_mem_repair(CORE_RCSU, BIT(boot_core_id - 4));
		boot_core_gate(boot_core_id, true);
	}
	REG32(PPU_CORE(boot_core_id) + PPU_PWPR) = PPU_ON;
	(void)soc_pull_register(PPU_CORE(boot_core_id) + PPU_PWSR, 15, PPU_ON, 800000);
}

/* --- states (CIX's ss_*_enter and ss_*_exit) --------------------------------- */

/* CIX's ss_init_exit: going to power-on is a warm reset */
void ss_init_exit(void)
{
	soc_warm_reset(0);
}

/* CIX's ss_run_exit: perf_stream_suspend, md_timer_pts, app_msgSave */
void ss_run_exit(void)
{
	perf_stream_suspend();
	pm_svc_pts();
	app_msgSave();
}

/* CIX's ss_pts_enter: the fans' counters freed */
void ss_pts_enter(void)
{
	fan_ctrl_suspend();
}

void ss_pts_exit(void)
{
	pm_ctrls5_intr_mask_off();
}

static bool package_without_ddr(void)
{
	return (plat_id_0 & (3u << 17)) == (3u << 17);
}

void ss_s3_enter(void)
{
	wait_for_bsp_and_dsu_idle();
	/* the SE has flushed the system cache and says it is suspended */
	(void)soc_pull_register(PM_FAST_CHANNEL, ~0u, 2, 0);
	clk_nodes_save();
	resume_evt = 0;
	resume_evt |= 1;
	irq_save_and_mask();
	fwlog_enable_dram(false, false);
	REG32(PM_FAST_CHANNEL) = 0;
	if (!package_without_ddr())
		ddr_s3_enter();
	handle_counter_before_sleep();
	pm_ctrls5_intr_enable();
	lcru_core_clock(7);			/* 100 MHz */
	if (trigger_slpTyp)
		REG32(S5_SLP_TYP) = 1;
}

void ss_s3_exit(void)
{
	uint32_t lost;

	dev_mbox_toggle_all_rst(~BIT(8));	/* mailbox 8 keeps the SE's words */
	/* CIX's check_boot_reason logs the S5 power reason */
	lcru_axprot();
	tlb_ctrl_restore();
	/* CIX: app_i2c_setup(400 kHz, overwrite_i2c_master | 7, 8 without the masters) */
	i2c_setup(400000, 7 | overwrite_i2c_master, 8 & ~overwrite_i2c_master);
	lost = dpm_edp_restore_voltage();
	clk_nodes_restore();
	recover_counter_after_sleep();
	if (!package_without_ddr())
		ddr_s3_exit();
	/*
	 * Not CIX's: no clock left above a rail that could not be set (the AP
	 * cores run once this returns; set before ddr_s3_exit, the clocks
	 * upset the DDR's resume)
	 */
	if (lost)
		dvfs_rails_lost(lost);
	pmic_resume();				/* set_mp2845b_psi7 on Phecda */
	irq_restore();
	fwlog_enable_dram(false, true);
	REG32(PM_FAST_CHANNEL) = PM_FC_RLOG_ON;
	/* CIX restarts its RTOS tick (CCOMPARE1, line 7) here; ours only runs
	 * while a task waits with a time-out (rtos/port/port.c) */
}

void ss_s5_enter(void)
{
	pm_ctrls5_intr_enable();
	lcru_core_clock(7);
	resume_evt = 0;
	resume_evt |= 2;
	if (trigger_slpTyp)
		REG32(S5_SLP_TYP) = 2;
}

void ss_s5_exit(void)
{
}

/* the end of CIX's ss_run_enter: resumeEvt bit 2 (and pm_ready's, at boot) */
void ss_run_entered(void)
{
	resume_evt |= 4;
	resume_evt_signalled = true;
}

/*
 * CIX's on_sys_pwr_event, from resumeEvt: after S3 (bits 0 and 2) the
 * audio workaround (suspend again if it fails), the UEFI's S3 boot
 * script, the boot core and DSU, the SoC idle switch as it was, the fans;
 * after S5 (bits 1 and 2) nothing.
 */
static void on_sys_pwr_event(uint32_t bits)
{
	bits &= 7;
	if (bits == 6) {
		resume_evt &= ~5u;
		return;
	}
	if (bits != 5)
		return;
	resume_evt &= ~5u;
	audio_war_full_str_retry = false;
	if (!audio_div_workaround()) {
		trigger_soc_re_str();
		return;
	}
	if (s3script_has_table() && s3script_check_table())
		(void)s3script_execute_table();
	power_up_dsu_and_bsp();
	if (soc_idle_switch_was_enabled)
		(void)perf_soc_idle_switch_set_enable(true);
	fan_ctrl_start();
}

void hsm_init(void)
{
	/* CIX's cpu_harvest_setup: the boot core from the SE's harvest word */
	boot_core_id = (uint32_t)core_info[1] >> 4;
}

static void waiti(void)
{
	uint32_t ps;

	__asm__ volatile("rsil %0, 5" : "=a"(ps) : : "memory");
	__asm__ volatile("waiti 0" : : : "memory");
	__asm__ volatile("wsr.ps %0; rsync" : : "a"(ps) : "memory");
}

/*
 * CIX's vApplicationIdleHook, called from the SCP-firmware loop once its
 * events are done (CIX: once every task idles).  With a target set, the
 * HSM goes there (power-on: a warm reset); then, asleep, each wake-up of
 * the S5 controller is served until the HSM is back in RUN.  Returns
 * whether the loop may sleep.
 */
bool pm_idle(void)
{
	uint32_t target = stdby_target;

	if (str_postponed && arch_getTimeNowUs() >= str_postpone_at) {
		str_postponed = false;
		pm_str_postpone();
		target = stdby_target;
	}
	if (target > HSM_ST_S5 || !(BIT(target) & (BIT(HSM_ST_POR) | BIT(HSM_ST_S3) | BIT(HSM_ST_S5)))) {
		if (resume_evt_signalled) {
			resume_evt_signalled = false;
			on_sys_pwr_event(resume_evt);
		}
		return !str_postponed;
	}
	/* the I2C workers' jobs done first; the sequence's own I2C is polled
	 * (CIX suspends its tasks around it) */
	rtos_quiesce();
	if (target == HSM_ST_POR)
		soc_warm_reset(0);
	ss_hsm_set((SS_HSM_STATE)target);
	while (ss_hsm_run() != stdby_target)
		;
	stdby_target = STDBY_NONE;
	for (;;) {
		if (ss_hsm_run() == HSM_ST_RUN)
			break;
		waiti();
		if (arch_irq_pending & BIT(S5_PM_IRQ_LINE)) {
			/* CIX's pm_ctrls5_non_aggre_isr */
			arch_irq_pending &= ~BIT(S5_PM_IRQ_LINE);
			REG32(AGG_ENABLE(S5_PM_IRQ_LINE)) = 0;
		}
		if (!pm_ctrls5_intr_is_fired())
			continue;
		/* the SE has resumed */
		(void)soc_pull_register(PM_FAST_CHANNEL, ~0u, 1, 0);
		lcru_core_clock(1);		/* 400 MHz */
		uart_init();			/* dbguart_init */
		CLK_ASSERT(pll_ctrl_get_state((void *)0xa0062100), 0xf19f0295);	/* detect_clk */
		wdt_restore();
		sensor_hub_wakeup_set(false);
		if (audio_div_workaround()) {
			REG32(PM_MAGIC) = PM_MAGIC_SFH_STOP;
			ss_hsm_set(HSM_ST_RUN);
			continue;
		}
		REG32(PM_MAGIC) = PM_MAGIC_SFH_START;
		if (!soc_pull_register(PM_MAGIC, 0xffff, PM_MAGIC_SFH_STARTED, core_hz / 1000000 * 5000000))
			soc_warm_reset(1);
		sensor_hub_timeout_set();
		audio_div_war_retry(true);
	}
	rtos_unquiesce();
	if (resume_evt_signalled) {
		resume_evt_signalled = false;
		on_sys_pwr_event(resume_evt);
	}
	return !str_postponed;
}
