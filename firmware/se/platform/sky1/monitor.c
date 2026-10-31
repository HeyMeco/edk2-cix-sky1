/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Clock and power state for crash reports (CIX platform/sky1/plat_monitor_clk.c,
 * plat_monitor_power.c).
 *
 * TODO: CIX records every clock's frequency and every power domain's state
 * here; the open firmware records only the raw PLL and power registers.
 */
#include "se.h"
#include "soc.h"

void clk_save_einfo(void)
{
	uint32_t *p = hal_crash_get_eaddr("CLK", 0x40), i;

	if (p == NULL)
		return;
	for (i = 0; i < 16; i++)
		p[i] = REG32(0x46570000 + i * 0x100 + 0x0c);
}

void pwr_save_einfo(void)
{
	uint32_t *p = hal_crash_get_eaddr("PWR", 0x40), i;

	if (p == NULL)
		return;
	for (i = 0; i < 16; i++)
		p[i] = REG32(0x56000400 + i * 4);
}
