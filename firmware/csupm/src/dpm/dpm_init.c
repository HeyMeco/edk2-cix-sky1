/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The DPM's task (CIX's Source/common/dpm/dpm_init.c, dpm_process.c and
 * Source/projects/sky1/dpm/unitest/cfg_sim.c's dpm_sim_init).
 *
 * CIX's DPM is a FreeRTOS task ("DPM", g_xTasklist[2]) that waits for a
 * notification; the performance streams' tick (src/perf, every 1 ms)
 * notifies it through dpm_ticker, and while pm_ready's dpm_enable holds it
 * runs one iteration.  Here the tick's event (scp/module/dpm) runs
 * dpm_proc after the streams' work, in CIX's order; ticks that came while
 * the iteration was pending collapse into one, as notification bits do.
 */
#include "dpm_rt.h"

static struct {
	bool enable;
	bool notified;
} dpm_iteration_enable;

/* CIX's dpm_ticker: notify the task, when there is one (always, here) */
void dpm_ticker(void *pData)
{
	(void)pData;
	dpm_iteration_enable.notified = true;
}

void dpm_enable(void)
{
	dpm_iteration_enable.enable = true;
}

/* No more iterations; the limits lifted */
void dpm_disable(void)
{
	dpm_iteration_enable.enable = false;
	dpm_restore_opp();
}

/* CIX's dpm_proc, one wake-up */
void dpm_proc(void)
{
	if (!dpm_iteration_enable.notified)
		return;
	dpm_iteration_enable.notified = false;
	if (dpm_iteration_enable.enable)
		dpm_iteration();
}

/* CIX's dpm_iteration (dpm_process.c) */
void dpm_iteration(void)
{
	dpm_refresh_dashboard();
	dpm_stream_actual_power();
	dpm_calc_expect_opp();
	dpm_spt_iteration();
	dpm_spt_arbitration();
	dpm_update_opp_limit();
	dpm_export_opp();
}

/*
 * CIX's dpm_sim_init (the task's initialisation): the power maps, the clock
 * controllers, the arbiters' configuration, an unknown board layout taken
 * as the EVB, the rails' limits; the arbiters start only if the
 * configuration is consistent.
 */
bool dpm_sim_init(void)
{
	app_pwr_init();
	cfg_x_clk_init();
	dpm_set_spt_cfg(&g_spt_opp_lv_cfg);
	if (dpm_edp_cfg_get_type() > PMIC_ALCOR)
		dpm_edp_cfg_set_type(PMIC_EVB);
	dpm_update_edp_limit();
	if (!dpm_params_consistency_checker())
		return false;
	for (uint32_t i = 0; i < 4; i++)
		x_arbit_init(&spt_ctrls[i]);
	return true;
}

/* CIX's dpm_init, the DPM task's initProc (ss_init_enter) */
bool dpm_init(void)
{
	return dpm_sim_init();
}
