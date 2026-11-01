/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* The DPM's tick: alarm 12 of the timer (dvfs has 0-11, the board's services
 * 16); its work on software line 33 */
#include <mod_dpm.h>

#include <fwk_id.h>
#include <fwk_module.h>
#include <fwk_module_idx.h>

static const struct mod_dpm_config dpm_config = {
	.alarm_id = FWK_ID_SUB_ELEMENT_INIT(FWK_MODULE_IDX_TIMER, 0, 12),
	.tick_ms = 1,
	.work_irq = 33,
	.cnts_irq = { 22, 23 },
};

const struct fwk_module_config config_dpm = {
	.data = &dpm_config,
};
