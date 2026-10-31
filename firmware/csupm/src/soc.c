/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * SoC register access through the PM's window (CIX's SocRegRead32 and
 * friends).  Interrupts stay as they are: nothing here is shared with an
 * interrupt handler yet.
 */
#include "soc.h"

uint32_t soc_read32(uint32_t addr)
{
	uint32_t v;

	REG32(SOC_READING) = addr;
	v = REG32(addr | SOC_WINDOW);
	REG32(SOC_READING) = 0;
	return v;
}

void soc_write32(uint32_t addr, uint32_t v)
{
	REG32(SOC_WRITING) = addr;
	REG32(addr | SOC_WINDOW) = v;
	REG32(SOC_WRITING) = 0;
}

uint64_t soc_read64(uint32_t addr)
{
	uint32_t hi = soc_read32(addr + 4);

	return (uint64_t)hi << 32 | soc_read32(addr);
}

void soc_write64(uint32_t addr, uint64_t v)
{
	soc_write32(addr, (uint32_t)v);
	soc_write32(addr + 4, (uint32_t)(v >> 32));
}
