/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* SE clocks and the SysTick reference (CIX arch/star_se/system_clk.c) */
#include "se.h"
#include "arch.h"
#include "platform.h"

/* SysTick runs from the 24 MHz reference unless CLKSOURCE selects the core */
uint32_t sys_clk_get_systick_clk(void)
{
	if (!(REG32(SYST_CSR) & BIT(2)))
		return 24000000;
	return plat_get_core_freq() * 1000000;
}

void sys_clk_init(void)
{
	REG32(SE_CLKCTRL_BASE) = 0x3ff3;	/* SE peripheral clocks */
	REG32(SE_SYSCTRL_CTRL) = 0x1ff;
}

/* Core cycles per SysTick tick, rounded up */
uint32_t sys_clk_get_systick_ratio(void)
{
	uint32_t tick = sys_clk_get_systick_clk();
	uint32_t core = plat_get_core_freq() * 1000000;
	uint32_t n = core + tick - 1;

	return n < tick ? 1 : n / tick;
}
