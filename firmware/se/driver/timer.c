/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* SE timers (CIX driver/timer/drv_timer.c): two 32-bit halves of one 64-bit count */
#include "se.h"
#include "drivers.h"

#define TIMER(n, reg)	(SE_TIMER_BASE + (n) * 0x1000 + (reg))
#define TIMER_LOAD	0x00
#define TIMER_COUNT	0x1c
#define TIMER_LOAD2	0x20
#define TIMER_CTRL	0x24

void drv_timer_init(void)
{
	REG32(TIMER(0, TIMER_LOAD)) = 0;
	REG32(TIMER(1, TIMER_LOAD)) = 0;
	REG32(TIMER(0, TIMER_LOAD2)) = 0xffffffff;
	REG32(TIMER(1, TIMER_LOAD2)) = 0xffffffff;
	REG32(TIMER(0, TIMER_CTRL)) = 0x9032;	/* free running, chained */
	REG32(TIMER(1, TIMER_CTRL)) = 0x9032;
	REG32(TIMER(0, TIMER_CTRL)) |= BIT(17);	/* enable */
	REG32(TIMER(1, TIMER_CTRL)) |= BIT(17);
}

uint64_t drv_timer_get_current_count(void)
{
	uint32_t lo = REG32(TIMER(0, TIMER_COUNT));

	return (uint64_t)REG32(TIMER(1, TIMER_COUNT)) << 32 | lo;
}
