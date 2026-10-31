/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Alarms (CIX's config_timer.c: one timer, 17 alarms) */
#include <mod_timer.h>
#include <mod_xt_timer.h>

#include <fwk_element.h>
#include <fwk_id.h>
#include <fwk_module.h>
#include <fwk_module_idx.h>

#include "irq.h"

static const struct fwk_element elements[] = {
	[0] = {
		.name = "CCOUNT0",
		.sub_element_count = 17,
		.data = &(const struct mod_timer_dev_config){
			.id = FWK_ID_ELEMENT_INIT(FWK_MODULE_IDX_XT_TIMER, 0),
			.timer_irq = IRQ_SOFT_TIMER,
		},
	},
	[1] = { 0 },
};

const struct fwk_module_config config_timer = {
	.elements = FWK_MODULE_STATIC_ELEMENTS_PTR(elements),
};
