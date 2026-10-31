/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Postcode, boot ROM lock, system counter and the SYSHUB window
 * (CIX driver/{postcode,rom,syscnt,syshub}).
 */
#include "se.h"
#include "drivers.h"

#define PC_STATUS	(SE_POSTCODE_BASE + 0x18)	/* bit 5: busy */
#define PC_DATA		(SE_POSTCODE_BASE + 0xf00)

uint32_t drv_pc_trace(uint16_t code)
{
	int i;

	for (i = 0; i <= 10000; i++) {
		if (!(REG32(PC_STATUS) & BIT(5))) {
			REG32(PC_DATA) = (uint32_t)code << 8;
			return 0;
		}
	}
	return 0x9101;
}

uint32_t drv_rom_lock(void)
{
	hal_printf_level(LOG_ERR, "[SE_FW]ROM will lock!\n");
	REG32(SE_SYSCTRL_ROM_CTRL) |= BIT(13);
	hal_printf_level(LOG_ERR, "[SE_FW]ROM locked!\n");
	return 0;
}

#define SYSCNT_CLK	0x56000018u
#define SYSCNT_CTRL	0x56002000u
#define SYSCNT_CNTCV_LO	(SYSCNT_CTRL + 0x08)
#define SYSCNT_CNTCV_HI	(SYSCNT_CTRL + 0x0c)
#define SYSCNT_CNTFID0	(SYSCNT_CTRL + 0x20)

void drv_syscounter_clk_enable(void)
{
	REG32(SYSCNT_CLK) |= 1;
	REG32(SYSCNT_CTRL) |= 3;			/* enable, halt on debug */
	REG32(SYSCNT_CNTFID0) = 1000000000;		/* 1 GHz */
}

uint64_t drv_syscounter_read(void)
{
	uint32_t hi, lo;

	do {
		hi = REG32(SYSCNT_CNTCV_HI);
		lo = REG32(SYSCNT_CNTCV_LO);
	} while (hi != REG32(SYSCNT_CNTCV_HI));
	return (uint64_t)hi << 32 | lo;
}

/* Slot registers: SoC address bits [36:27] of each 128 MiB slot */
static const uint32_t sysh_slot_reg[8] = {
	SE_SYSCTRL_BASE + 0x100, SE_SYSCTRL_BASE + 0x104, SE_SYSCTRL_BASE + 0x108,
	SE_SYSCTRL_BASE + 0x10c, SE_SYSCTRL_BASE + 0x110, SE_SYSCTRL_BASE + 0x114,
	SE_SYSCTRL_BASE + 0x118, SE_SYSCTRL_BASE + 0x11c,
};
static unsigned int g_index;

uint32_t sysh_io_map(uint64_t addr, uint32_t range)
{
	REG32(sysh_slot_reg[g_index]) = (uint32_t)addr >> 27 | (uint32_t)(addr >> 32) << 5;
	return 0;
}

/* Map 1 GiB of SoC space from map_addr (128 MiB aligned) at the window */
uint32_t sysh_io_init(uint64_t map_addr)
{
	uint32_t err;

	if (map_addr & (SYSH_IO_SLOT_SIZE - 1))
		return 0xffffffff;
	for (g_index = 0; g_index < 8; g_index++) {
		err = sysh_io_map(map_addr + (uint64_t)g_index * SYSH_IO_SLOT_SIZE, SYSH_IO_SLOT_SIZE);
		if (err) {
			SE_ERR(err);
			return err;
		}
	}
	return 0;
}

uint64_t sysh_io_get_map_addr(void)
{
	uint32_t v = REG32(SE_SYSCTRL_SYSH_MAP);

	return (uint64_t)(v >> 5) << 32 | v << 27;
}
