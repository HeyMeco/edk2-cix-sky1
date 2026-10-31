/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* SE watchdog (CIX driver/wdt/drv_wdt.c), counts at plat_get_star_wdt_freq() MHz */
#include "se.h"
#include "drivers.h"
#include "platform.h"

#define WDT_LOAD	(SE_WDT_BASE + 0x0)
#define WDT_VALUE	(SE_WDT_BASE + 0x4)
#define WDT_CTRL	(SE_WDT_BASE + 0x8)	/* bit 0: count, bit 1: reset on expiry */
#define WDT_INTCLR	(SE_WDT_BASE + 0xc)
#define WDT_MAX_MS	128000

#define S5_SE_WDT_RESET	0x56000208u		/* let the SE watchdog reset the system */

static uint32_t g_wdt_rload;

uint32_t drv_wdt_init(uint32_t ms, uint32_t reset_enable)
{
	REG32(SE_SYSCTRL_CTRL) |= BIT(2);
	REG32(SE_SYSCTRL_WDT_CTRL) |= 6;
	if (ms > WDT_MAX_MS)
		ms = WDT_MAX_MS;
	g_wdt_rload = plat_get_star_wdt_freq() * ms * 1000;
	REG32(WDT_CTRL) &= ~BIT(1);
	REG32(WDT_CTRL) &= ~BIT(0);
	if (reset_enable && !plat_is_se_only())
		REG32(S5_SE_WDT_RESET) = 1;
	return 0;
}

uint32_t drv_wdt_stop(void)
{
	REG32(WDT_CTRL) &= ~BIT(1);
	REG32(WDT_CTRL) &= ~BIT(0);
	REG32(WDT_LOAD) = 0xffffffff;
	return 0;
}

uint32_t drv_wdt_deinit(void)
{
	return drv_wdt_stop();
}

uint32_t drv_wdt_start(void)
{
	REG32(WDT_LOAD) = g_wdt_rload ? g_wdt_rload : 1;
	REG32(WDT_CTRL) |= BIT(1);
	REG32(WDT_CTRL) |= BIT(0);
	REG32(WDT_INTCLR) = 0;
	return 0;
}

uint32_t drv_wdt_feed(void)
{
	REG32(WDT_LOAD) = g_wdt_rload ? g_wdt_rload : 1;
	return 0;
}

uint32_t drv_wdt_cur_count(void)
{
	return REG32(WDT_VALUE);
}

void drv_wdt_reset_sys(void)
{
	REG32(WDT_CTRL) |= BIT(0);
	REG32(WDT_CTRL) |= BIT(1);
	REG32(WDT_LOAD) = 0x100;
	for (;;)
		;
}
