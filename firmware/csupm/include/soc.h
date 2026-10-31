/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Access to SoC registers outside the PM (CIX's soc_reg_access.c).
 *
 * SoC address a (below 0x20000000) is at PM address a | 0x40000000.  While
 * an access is on the bus its SoC address is in a scratch word (reads
 * SOC_READING, writes SOC_WRITING), so whoever finds the PM stuck on an
 * unclocked block can see which one.
 */
#ifndef SOC_H
#define SOC_H

#include "lcru.h"

#define SOC_WINDOW		0x40000000u
#define SOC_READING		(PM_SCRATCH + 0x30)
#define SOC_WRITING		(PM_SCRATCH + 0x34)

uint32_t soc_read32(uint32_t addr);
void soc_write32(uint32_t addr, uint32_t v);
uint64_t soc_read64(uint32_t addr);		/* high word first */
void soc_write64(uint32_t addr, uint64_t v);	/* low word first */

#endif
