/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The core's private clock and reset block at 0xb0010000 (CIX's dev_lx7clk,
 * dev_wdt and the *_sw_reset helpers).
 */
#ifndef LCRU_H
#define LCRU_H

#include "pm.h"

#define LCRU			0xb0010000u
#define LCRU_AXPROT		(LCRU + 0x080)	/* three words */
#define LCRU_CORE_DIV		(LCRU + 0x100)	/* [2:0]: core clock = 800 MHz / (n + 1) */
#define LCRU_RESET		(LCRU + 0x200)	/* 1 = out of reset */
#define RESET_I2C(i)		BIT(i)		/* I2C 0-3 */
#define RESET_UART		BIT(5)
#define RESET_WDT		BIT(6)
#define RESET_MBOX_SHIFT	10		/* mailboxes 0-9 */
#define RESET_MBOX_ALL		(0x3ffu << RESET_MBOX_SHIFT)

#define CORE_PLL_HZ		800000000u
#define LOCAL_WDT_CTRL		0xa0000200u	/* 0: the local watchdog stops */
#define PM_SCRATCH		0x200bef00u	/* 64 words, other agents read them */

extern uint32_t core_hz;

void lcru_core_clock(uint32_t div);
void lcru_axprot(void);
void lcru_reset(uint32_t bits, bool release);
void lcru_mbox_reset(uint32_t mask);
void wdt_off(void);
void scratch_clear(void);

#endif
