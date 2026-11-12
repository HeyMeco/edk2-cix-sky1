/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The core's private clock and reset block, the local watchdog; the boot
 * part of the GPIO driver, whose reset is here (the counters' driver is
 * src/perf/dev_counters.c).
 */
#include "lcru.h"

uint32_t core_hz;

uint64_t arch_getTimeNowUs(void);	/* src/clk/clk_port.c */

__attribute__((noinline)) void udelay_cycles(uint32_t cycles)
{
	uint32_t start = ccount();

	while (ccount() - start < cycles)
		;
}

void lcru_core_clock(uint32_t div)
{
	/* the cycles so far are time at the clock they were counted at */
	(void)arch_getTimeNowUs();
	REG32(LCRU_CORE_DIV) = (REG32(LCRU_CORE_DIV) & ~7u) | (div & 7);
	core_hz = CORE_PLL_HZ / ((div & 7) + 1);
}

/* AXI protection of the PM's bus masters, as CIX's secure_AxPROT1 sets it */
void lcru_axprot(void)
{
	REG32(LCRU_AXPROT) = 0xff;
	REG32(LCRU_AXPROT + 4) = 1;
	REG32(LCRU_AXPROT + 8) = 5;
}

void lcru_reset(uint32_t bits, bool release)
{
	uint32_t v = REG32(LCRU_RESET);

	REG32(LCRU_RESET) = release ? v | bits : v & ~bits;
}

/* Hold the mailboxes in reset, then release those in mask (bit i: mailbox i) */
void lcru_mbox_reset(uint32_t mask)
{
	uint32_t v = REG32(LCRU_RESET) & ~RESET_MBOX_ALL;

	REG32(LCRU_RESET) = v;
	udelay_cycles(20);
	REG32(LCRU_RESET) = v | ((mask << RESET_MBOX_SHIFT) & RESET_MBOX_ALL);
}

/* The local watchdog: out of reset, stopped (CIX: wdt_set(0)) */
void wdt_off(void)
{
	lcru_reset(RESET_WDT, false);
	lcru_reset(RESET_WDT, true);
	REG32(LOCAL_WDT_CTRL) = 0;
}

void scratch_clear(void)
{
	for (uint32_t i = 0; i < 64; i++)
		REG32(PM_SCRATCH + 4 * i) = 0;
}

/* --- GPIO (CIX's dev_gpio_init) ------------------------------------------ */

/* The GPIO block through a reset (held only if it was out), its first
 * register cleared */
void dev_gpio_init(void)
{
	uint32_t v = REG32(LCRU_RESET);

	if (v & RESET_GPIO) {
		REG32(LCRU_RESET) = v & ~RESET_GPIO;
		udelay_cycles(20);
	}
	REG32(LCRU_RESET) |= RESET_GPIO;
	REG32(LOCAL_GPIO) = 0;
}
