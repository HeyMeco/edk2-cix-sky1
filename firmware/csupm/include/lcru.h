/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The core's private clock and reset block at 0xb0010000 (CIX's dev_lx7clk,
 * dev_wdt and the *_sw_reset helpers; DEV_CSU_PM_REG in its DWARF), and
 * the parts of the drivers of the blocks whose resets it holds that run
 * at boot: counters (dev_counters.c), GPIO (dev_gpio.c).
 */
#ifndef LCRU_H
#define LCRU_H

#include "pm.h"

#define LCRU			0xb0010000u
/* HUB_TLB0-7: the 128 MB of DRAM hub window i (0x60000000 + 128 MB * i)
 * shows, DRAM address bits [39:27] (src/rtt/tlb_ctrl.c) */
#define LCRU_HUB_TLB(i)		(LCRU + 0x008 + 4 * (i))
#define LCRU_AXPROT		(LCRU + 0x080)	/* three words */
/* the first, HUB_TLB_PRIVATE: [7:0] window i valid, [15:8] window i secure */
#define LCRU_HUB_TLB_PRIVATE	LCRU_AXPROT
#define LCRU_CORE_DIV		(LCRU + 0x100)	/* [2:0]: core clock = 800 MHz / (n + 1) */
#define LCRU_RESET		(LCRU + 0x200)	/* 1 = out of reset */
#define RESET_I2C(i)		BIT(i)		/* I2C 0-3 */
#define RESET_GPIO		BIT(4)
#define RESET_UART		BIT(5)
#define RESET_WDT		BIT(6)
#define RESET_TIMER(i)		BIT(8 + (i))	/* counters 0 and 1 */
#define RESET_MBOX_SHIFT	10		/* mailboxes 0-9 */
#define RESET_MBOX_ALL		(0x3ffu << RESET_MBOX_SHIFT)

#define CORE_PLL_HZ		800000000u
#define LOCAL_WDT_CTRL		0xa0000200u	/* 0: the local watchdog stops */
#define LOCAL_GPIO		0xa0020000u
#define PM_SCRATCH		0x200bef00u	/* 64 words, other agents read them */

extern uint32_t core_hz;

void lcru_core_clock(uint32_t div);
void lcru_axprot(void);
void lcru_reset(uint32_t bits, bool release);
void lcru_mbox_reset(uint32_t mask);
void wdt_off(void);
void scratch_clear(void);

/* CIX's dev_counters.c: the counter blocks, the AP's free-running counter */
#include "perf/dev_counters.h"
/* CIX's dev_gpio_init */
void dev_gpio_init(void);

#endif
