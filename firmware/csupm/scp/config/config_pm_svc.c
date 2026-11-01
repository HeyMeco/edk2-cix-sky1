/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* The board services' scheduler: alarm 16 of the timer (config_timer.c) */
#include <mod_pm_svc.h>

#include <fwk_id.h>
#include <fwk_module.h>
#include <fwk_module_idx.h>

static const fwk_id_t alarm = FWK_ID_SUB_ELEMENT_INIT(FWK_MODULE_IDX_TIMER, 0, 16);

const struct fwk_module_config config_pm_svc = {
	.data = &alarm,
};
