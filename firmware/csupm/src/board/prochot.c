/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * PROCHOT (CIX's intr_mgmt.c: init_prochot_timer, prochot_outside_isr,
 * prochot_timeout_cb, prochot_throttle_Fmax_Khz).
 *
 * The board's PROCHOT# line is aggregator line 30.  When it is asserted
 * the PM blocks the AP's dispatch (0xa0000010 bits 4-11: the clusters
 * stop issuing), caps the CPUs at 500 MHz (prochot_throttle_Fmax_Khz,
 * which CIX's DPM applies) and masks the line; after 1 ms the dispatch is
 * free again and the line is sampled every 5 ms: while it stays asserted
 * the cap stays at 500 MHz, once released it is raised a step per period
 * (1, 2, 3 GHz), then lifted (4 GHz) and the line unmasked.
 */
#include <mod_pm_svc.h>

#include "board/board.h"
#include "irq.h"

#define PROCHOT_LINE		30
#define AP_DISPATCH		0xa0000010u	/* CIX's ap_set/clr_dispatch_block */
#define AP_DISPATCH_BLOCK	0xff0u
#define PROCHOT_US		1000u
#define PROCHOT_STEP_US		5000u
#define FMAX_THROTTLED_KHZ	500000u
#define FMAX_STEP_KHZ		1000000u
#define FMAX_LIFTED_KHZ		4000000u
#define FSM_IDLE		6

/* CIX's g_prochot_timer: the timer, the state, the cap (none at boot) */
static struct {
	uint8_t timer_id;
	uint8_t fsm;
	uint32_t fmax_khz;
} g_prochot_timer = { PM_SVC_TIMER_NONE, FSM_IDLE, 0xffffffffu };

static bool prochot_asserted(void)
{
	return REG32(AGG_STATUS(PROCHOT_LINE)) & 1;
}

/* CIX's ap_set_dispatch_block / ap_clr_dispatch_block */
static void ap_dispatch_block(bool block)
{
	if (block)
		REG32(AP_DISPATCH) |= AP_DISPATCH_BLOCK;
	else
		REG32(AP_DISPATCH) &= ~AP_DISPATCH_BLOCK;
}

uint32_t prochot_throttle_Fmax_Khz(void)
{
	return g_prochot_timer.fmax_khz;
}

static void prochot_restart(uint32_t us)
{
	pm_svc_timer_disable(g_prochot_timer.timer_id);
	pm_svc_timer_update_interval(g_prochot_timer.timer_id, us);
	pm_svc_timer_enable(g_prochot_timer.timer_id);
}

/* CIX's prochot_timeout_cb: the state machine above */
static void prochot_timeout_cb(void *arg)
{
	uint8_t fsm = g_prochot_timer.fsm;

	(void)arg;
	switch (fsm) {
	case 0:				/* 1 ms after the edge */
		ap_dispatch_block(false);
		prochot_restart(PROCHOT_STEP_US);
		g_prochot_timer.fsm = 1;
		break;
	case 1:
		g_prochot_timer.fmax_khz = FMAX_THROTTLED_KHZ;
		/* fall through */
	case 2:
		g_prochot_timer.fsm = prochot_asserted() ? 1 : fsm + 1;
		break;
	case 3:
	case 4:
	case 5:
		if (prochot_asserted()) {
			g_prochot_timer.fsm = 1;
			break;
		}
		g_prochot_timer.fsm = fsm + 1;
		g_prochot_timer.fmax_khz = (fsm + 1u) * FMAX_STEP_KHZ - 3 * FMAX_STEP_KHZ;
		break;
	case FSM_IDLE:
		pm_svc_timer_disable(g_prochot_timer.timer_id);
		ap_dispatch_block(false);
		REG32(AGG_ENABLE(PROCHOT_LINE)) |= 1;
		g_prochot_timer.fmax_khz = FMAX_LIFTED_KHZ;
		break;
	default:
		break;
	}
}

void init_prochot_timer(void)
{
	if (g_prochot_timer.timer_id == PM_SVC_TIMER_NONE)
		g_prochot_timer.timer_id = pm_svc_timer_alloc(PROCHOT_US, prochot_timeout_cb, NULL,
							      false, true);
}

/* CIX's prochot_outside_isr (aggregator line 30) */
void prochot_outside_isr(void)
{
	if (!prochot_asserted())
		return;
	REG32(AGG_ENABLE(PROCHOT_LINE)) &= ~1u;
	ap_dispatch_block(true);
	g_prochot_timer.fsm = 0;
	g_prochot_timer.fmax_khz = FMAX_THROTTLED_KHZ;
	prochot_restart(PROCHOT_US);
}

/* CIX's set/get_specific_operating_point: an operating point forced for the
 * GPU (CIX's debug CLI sets it; nothing does here) */
static uint32_t specific_opp_freq_khz, specific_opp_voltage_mv;

void set_specific_operating_point(uint32_t freq_khz, uint32_t voltage_mv)
{
	specific_opp_freq_khz = freq_khz;
	specific_opp_voltage_mv = voltage_mv;
}

bool get_specific_operating_point(uint32_t *freq_khz, uint32_t *voltage_mv)
{
	if (!specific_opp_freq_khz || !specific_opp_voltage_mv)
		return false;
	*freq_khz = specific_opp_freq_khz;
	*voltage_mv = specific_opp_voltage_mv;
	return true;
}
