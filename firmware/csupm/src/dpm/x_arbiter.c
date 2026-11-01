/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * CIX's arbiters (Source/common/dpm/x_arbiter.c) and clock controllers
 * (x_clk.c, cfg_x_clk.c).
 *
 * An arbiter integrates its PID controller's feedback into a phase record
 * (0 to just under 8): the phase, skipping those switched off downwards,
 * selects a row of throttling coefficients for the EDP domains not
 * switched off.  x_arbit_redist only says whether the arbiter is on (CIX's
 * redistribution is empty).  The clock controllers are initialised only:
 * CIX's image has no cfg_x_clk_iterate.
 */
#include "dpm_rt.h"

void x_arbit_init(X_ARB_CTRL *ctrl)
{
	if (!ctrl || !ctrl->pCfg || !ctrl->domain_limit || !ctrl->domain_expect || !ctrl->pid)
		__builtin_trap();	/* CIX: vAssertCalledWithTRC(0xf1350014) */
	for (uint32_t i = 0; i < 8; i++)
		ctrl->domain_throttle_coeff[i] = 1.0f;
}

void x_arbit_iteration(X_ARB_CTRL *ctrl)
{
	float rec = pid_iteration(ctrl->pid) + ctrl->phase_record;
	uint32_t phase;

	ctrl->phase_record = rec;
	if (8.0f <= rec) {
		rec = 7.999f;
		ctrl->phase_record = rec;
	} else if (rec < 0.0f) {	/* CIX: !ULE.S; a NaN stays */
		rec = 0.0f;
		ctrl->phase_record = rec;
	}
	phase = (uint32_t)rec;
	if (ctrl->phase_off_bitmap & BIT(phase & 31)) {
		do {
			if ((int32_t)phase < 1) {
				phase = 0;
				break;
			}
			phase--;
		} while (ctrl->phase_off_bitmap & BIT(phase & 31));
	}
	ctrl->phase_id = (uint8_t)phase;
	for (uint32_t i = 0; i < 8; i++) {
		if (!(ctrl->domain_off_bitmap & BIT(i)))
			ctrl->domain_throttle_coeff[i] = ctrl->pCfg->phase_cfg[phase][i];
	}
}

bool x_arbit_redist(X_ARB_CTRL *ctrl)
{
	return ctrl->is_enabled;
}

bool x_clk_init(X_CLK_CTRL_T *domain)
{
	if (!domain || !domain->pCfg || !domain->pPid)
		return false;
	domain->freq_tracking = 0;
	domain->freq_range = domain->pCfg->freq_max - domain->pCfg->freq_min;
	domain->freq_target = 0;
	domain->freq_delivered = 0;
	domain->utilization = 0;
	domain->stick_to_effec_level = true;
	return true;
}

extern X_CLK_CTRL_T x_clk_ctrl[10];

void cfg_x_clk_init(void)
{
	for (uint32_t i = 0; i < 10; i++)
		(void)x_clk_init(&x_clk_ctrl[i]);
}
