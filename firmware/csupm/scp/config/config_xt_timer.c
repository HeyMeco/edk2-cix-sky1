/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* SCP-firmware's timer on CCOMPARE2 (interrupt 8, as CIX's hardware timer) */
#include <mod_xt_timer.h>

#include <fwk_element.h>
#include <fwk_module.h>

#include "irq.h"

static const struct fwk_element elements[] = {
	[0] = { .name = "CCOUNT0", .data = &(const struct mod_xt_timer_config){
		.ccompare = 2, .irq = IRQ_TIMER2, .alarm_irq = IRQ_SOFT_TIMER } },
	[1] = { 0 },
};

const struct fwk_module_config config_xt_timer = {
	.elements = FWK_MODULE_STATIC_ELEMENTS_PTR(elements),
};
