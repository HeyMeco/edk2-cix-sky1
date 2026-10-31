/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * CSU_PM firmware: common definitions.
 *
 * CSU_PM is a Cadence Xtensa LX7 core ("pm_core_v1") running from the PM
 * SRAM (SoC 0x06a10000, here 0x20000000).  Its local peripherals are at
 * 0xa0000000 (SoC 0x06510000); the rest of the SoC is reached at the SoC
 * addresses below 0x60000000.  GCC orders volatile accesses with MEMW.
 */
#ifndef PM_H
#define PM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define REG32(a)		(*(volatile uint32_t *)(uintptr_t)(a))
#define REG16(a)		(*(volatile uint16_t *)(uintptr_t)(a))
#define REG8(a)			(*(volatile uint8_t *)(uintptr_t)(a))
#define ARRAY_SIZE(a)		(sizeof(a) / sizeof((a)[0]))
#define BIT(n)			(1u << (n))

/* local peripherals (SoC address - 0x06510000 + 0xa0000000) */
#define PM_LOCAL		0xa0000000u
#define PM_MBOX(i)		(0xa0080000u + (i) * 0x10000u)	/* SoC 0x06590000 + ... */

/* The SE's boot handshake, in mailbox 8 (the SE's 0x46610090) */
#define PM_MAGIC		(PM_MBOX(8) + 0x90)
#define PM_MAGIC_UP		0x5c5au		/* PM -> SE: running */
#define PM_MAGIC_SE_DONE	0x55cau		/* SE -> PM: SoC init done */
#define PM_MAGIC_READY		0x5a5cu		/* PM -> SE: ready, start the AP */

static inline uint32_t ccount(void)
{
	uint32_t v;

	__asm__ volatile("rsr.ccount %0" : "=a"(v));
	return v;
}

void udelay_cycles(uint32_t cycles);	/* busy-wait on CCOUNT */


#endif
