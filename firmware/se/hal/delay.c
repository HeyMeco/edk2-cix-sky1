/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Busy waits on SysTick (CIX hal/common/hal_delay.c) and the SE timer */
#include "se.h"
#include "arch.h"
#include "drivers.h"
#include "platform.h"

/* One SysTick countdown of val ticks; gives up after val * ratio polls */
static void _hal_delay(uint32_t val)
{
	uint32_t polls = val * sys_clk_get_systick_ratio();
	uint32_t ctrl;

	if (val - 1 < 0x1000000) {
		REG32(SYST_CVR) = 0;
		REG32(SYST_RVR) = val - 1;
	}
	ctrl = (REG32(SYST_CSR) & ~1u) | 1;
	REG32(SYST_CSR) = ctrl;		/* enable */
	for (;;) {
		if (REG32(SYST_CSR) & BIT(16)) {
			REG32(SYST_CSR) = ctrl;
			return;
		}
		if (polls-- == 0)
			break;
	}
	REG32(SYST_CSR) = ctrl;
	hal_printf_level(LOG_WARN, "systick doesn't set cuntdown-zearo-flag \n");
}

void hal_udelay(uint32_t us)
{
	while (us--)
		_hal_delay(sys_clk_get_systick_clk() / 1000000);
}

void hal_mdelay(uint32_t ms)
{
	while (ms--)
		_hal_delay(sys_clk_get_systick_clk() / 1000);
}

/* hal_timer: the 64-bit SE timer, counting at the reference clock */
typedef struct { void *priv; } hal_timer_t;

uint32_t hal_timer_init(hal_timer_t *t)
{
	drv_timer_init();
	return 0;
}

uint32_t hal_timer_get_value(hal_timer_t *t, uint64_t *value)
{
	*value = drv_timer_get_current_count();
	return 0;
}

uint32_t hal_timer_value_to_us(hal_timer_t *t, uint64_t *value, uint32_t *us)
{
	*us = (uint32_t)(*value * 1000000 / ((uint64_t)plat_get_ref_freq() * 1000000));
	return 0;
}
