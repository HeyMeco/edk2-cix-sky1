/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The core's private clock and reset block, the local watchdog; the boot
 * part of the counter and GPIO drivers, whose resets are here.
 */
#include "lcru.h"

uint32_t core_hz;

__attribute__((noinline)) void udelay_cycles(uint32_t cycles)
{
	uint32_t start = ccount();

	while (ccount() - start < cycles)
		;
}

void lcru_core_clock(uint32_t div)
{
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

/* --- counters (CIX's dev_counters.c, the part that runs at boot) ----------- */

#define CNTS_INSTANCES		3
#define CNTS_NODES		8
#define CNTS_MODE_NONE		7

/* CIX's DEV_CNTS_SETUP: a counter block and its eight counters */
typedef struct {
	uint16_t mode;
	uint16_t data;
	union {
		void (*timerCb)(void);
		void (*tachCb)(uint32_t);
	};
	uint32_t ctrl;
} DEV_CNTS_NODE_CFG_DATA;

typedef struct {
	uint32_t baseAddr;
	uint32_t refClkFreq;
	DEV_CNTS_NODE_CFG_DATA cfgData[CNTS_NODES];
} DEV_CNTS_SETUP;

static DEV_CNTS_SETUP cnts_inst[CNTS_INSTANCES];

/* CIX's dev_cnts_init: no counter in use, the PM's two blocks in reset */
void dev_cnts_init(void)
{
	memset(cnts_inst, 0, sizeof(cnts_inst));
	for (uint32_t i = 0; i < CNTS_INSTANCES; i++) {
		for (uint32_t j = 0; j < CNTS_NODES; j++)
			cnts_inst[i].cfgData[j].mode = CNTS_MODE_NONE;
	}
	REG32(LCRU_RESET) &= ~(RESET_TIMER(0) | RESET_TIMER(1));
}

/* CIX's dev_cnts_set_instance: block inst at base, its reference clock;
 * blocks 0 and 1 (the PM's) come out of reset, block 2 (the SoC's) has no
 * bit here, CIX's rewrites the register all the same */
void dev_cnts_set_instance(uint32_t base, uint32_t inst, uint32_t ref_clk_hz)
{
	uint32_t v = REG32(LCRU_RESET);

	if (inst >= CNTS_INSTANCES)
		__builtin_trap();		/* CIX: vAssertCalledWithTRC(0xf1380188) */
	REG32(LCRU_RESET) = v | ((RESET_TIMER(0) << inst) & (RESET_TIMER(0) | RESET_TIMER(1)));
	cnts_inst[inst].baseAddr = base;
	cnts_inst[inst].refClkFreq = ref_clk_hz;
	for (uint32_t j = 0; j < CNTS_NODES; j++)
		cnts_inst[inst].cfgData[j].mode = CNTS_MODE_NONE;
}

/* The AP's free-running counter (the timestamps of the RAM log).  CIX's
 * reads the high word, the low word, and reads again (at most ten times)
 * while the high word changed, or while the two words are equal (odd, but
 * CIX's: on a counter whose high word is 0, as in a model, every read is
 * repeated). */
#define AP_FREE_COUNTER_LO	0x56002008u
#define AP_FREE_COUNTER_HI	0x5600200cu

uint64_t dev_cnts_get_ap_free_counter(void)
{
	uint32_t hi, lo, hi2;
	int n = 11;

	for (;;) {
		hi = REG32(AP_FREE_COUNTER_HI);
		lo = REG32(AP_FREE_COUNTER_LO);
		if (--n == 0)
			break;
		hi2 = REG32(AP_FREE_COUNTER_HI);
		if (hi != lo && hi == hi2)
			break;
	}
	return (uint64_t)hi << 32 | lo;
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
