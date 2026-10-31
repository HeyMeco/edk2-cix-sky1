/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The PM's system power state machine (CIX's Source/common/sshsm/pwr_hsm.c
 * and Source/projects/sky1/subsys_hsm.c) and what it uses: the S5 power
 * controller's wake interrupt (intr_mgmt.c), polled registers
 * (soc_reg_access.c's soc_pull_register), the warm reset (sys_ctrl.c).
 *
 * States, in a tree: power-on (POR) above init (INIT), which has run
 * (RUN) and prepare-to-sleep (PTS) below it; suspend (S3) and shutdown
 * (S5) are below PTS.  A move runs the exit functions up to the common
 * parent, then the enter functions down to the target: RUN to S3 is
 * run exit, PTS enter, S3 enter; back, S3 exit, PTS exit, run enter
 * (main.c's ss_run, CIX's ss_run_enter, again).
 *
 * A request (SCMI system power from TF-A, CIX's PM message 3) only sets
 * the target (stdby_target, as CIX's trigger_STR/SD/RST); the sequence
 * runs from the idle loop (pm_idle, CIX's vApplicationIdleHook) once
 * nothing else is pending.
 */
#ifndef HSM_H
#define HSM_H

#include "pm.h"
#include "clk/clk.h"		/* SS_HSM_STATE, pm_ss_state (CIX's current_state) */

/* CIX's stdby_target: a state to go to, or none */
#define STDBY_NONE		6u
extern volatile uint32_t stdby_target;
/* CIX's trigger_slpTyp: write the sleep type to the S5 controller */
extern bool trigger_slpTyp;
/* The boot core TF-A names before suspend (PM message 0x42) */
extern uint32_t boot_core_id;

/* Mailbox 8: the SE's fast channel 0 (1 resumed, 2 suspended; the PM's
 * 0xaa55: RAM log on) and the handshake word */
#define PM_FAST_CHANNEL		(PM_MBOX(8) + 0x8c)
#define PM_MAGIC_SFH_START	0xaaccu		/* PM -> SE after wake: start the sensor fusion hub */
#define PM_MAGIC_SFH_STARTED	0xccaau
#define PM_MAGIC_SFH_STOP	0xaabbu		/* PM -> SE after wake: resume the AP */
#define PM_FC_RLOG_ON		0xaa55u

/* S5 power controller (CIX's register layout in mclp_retention.c) */
#define S5_SLP_TYP		0x56000100u	/* 1 suspend, 2 shutdown */
#define S5_PWR_REASON		0x56000218u
#define S5_CSU_PM_INTR		0x56000220u
#define S5_CSU_PM_INTR_EN	0x5600022cu
#define S5_WARM_RST		0x56000300u
#define S5_SW_REASON		0x56000500u	/* soc_warm_reset's reason */
#define S5_MEMR_INFO		0x56000504u	/* the SE's memory repair information */
#define S5_PM_IRQ_LINE		18		/* aggregator line 18, source 0 */

/* pwr_hsm.c */
void ss_hsm_init(void);
SS_HSM_STATE ss_hsm_run(void);
SS_HSM_STATE ss_hsm_get(void);
void ss_hsm_set(SS_HSM_STATE state);

/* subsys_hsm.c */
void hsm_init(void);
void trigger_STR(void);
void trigger_SD(void);
void trigger_RST(void);
void setSlpType(bool enable);
bool pm_idle(void);
void fan_ctrl_start(void);
void ss_init_exit(void);
void ss_run_exit(void);
void ss_pts_enter(void);
void ss_pts_exit(void);
void ss_s3_enter(void);
void ss_s3_exit(void);
void ss_s5_enter(void);
void ss_s5_exit(void);
void ss_run_entered(void);
bool audio_div_workaround(void);
void sensor_hub_wakeup_set(bool enable);
void do_mem_repair(uint32_t base, uint32_t enable);

/* soc.c-like helpers (CIX's soc_reg_access.c, sys_ctrl.c, intr_mgmt.c) */
bool soc_pull_register(uint32_t addr, uint32_t mask, uint32_t value, uint32_t timeout_cycles);
__attribute__((__noreturn__)) void soc_warm_reset(uint32_t reason);
void pm_ctrls5_intr_enable(void);
bool pm_ctrls5_intr_is_fired(void);
void pm_ctrls5_intr_mask_off(void);
void dev_mbox_toggle_all_rst(uint32_t mask);
void handle_counter_before_sleep(void);
void recover_counter_after_sleep(void);

/* main.c: CIX's ss_init_enter and ss_run_enter */
void ss_init(void);
void ss_run(void);

#endif
