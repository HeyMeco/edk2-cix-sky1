/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Clock and power state for crash reports (CIX platform/sky1/plat_monitor_clk.c,
 * plat_monitor_power.c).
 *
 * TODO: CIX records every clock's frequency and every power domain's state
 * here (records "CLK", 4304 bytes, and "POWER", 1088 bytes); the open
 * firmware records only the raw PLL and power registers, under names of
 * their own so that nothing reads them as CIX's.
 */
#include "se.h"
#include "soc.h"

void clk_save_einfo(void)
{
	uint32_t *p = hal_crash_get_eaddr("CLK_RAW", 0x40), i;

	if (p == NULL)
		return;
	for (i = 0; i < 16; i++)
		p[i] = REG32(0x46570000 + i * 0x100 + 0x0c);
}

void pwr_save_einfo(void)
{
	uint32_t *p = hal_crash_get_eaddr("PWR_RAW", 0x40), i;

	if (p == NULL)
		return;
	for (i = 0; i < 16; i++)
		p[i] = REG32(0x56000400 + i * 4);
}
