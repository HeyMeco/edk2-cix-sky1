/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Timer driver for the timer module on the core's cycle counter: CCOUNT
 * (32 bits, the core clock) extended to 64 bits, one CCOMPARE register
 * for the next alarm.
 */
#ifndef MOD_XT_TIMER_H
#define MOD_XT_TIMER_H

#include <fwk_id.h>

struct mod_xt_timer_config {
	unsigned int ccompare;		/* 0-2 */
	unsigned int irq;		/* its interrupt line */
	unsigned int alarm_irq;		/* the timer module's (software) line */
};

enum mod_xt_timer_api_idx {
	MOD_XT_TIMER_API_IDX_DRIVER,
	MOD_XT_TIMER_API_IDX_COUNT,
};

#endif
