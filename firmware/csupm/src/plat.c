/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Platform identification (CIX's app_plat_id_init, at power-on).
 */
#include "plat.h"
#include "soc.h"

#define STRAP0			0x05040100u
#define STRAP1			0x05040104u
#define STRAP_NONE		0xe8e8e8e8u	/* what a simulator reads */
#define PM_STRAP		0xa0000800u
#define HARVEST			0x56000508u	/* S5_SW_HARVEST (local, not through the SoC window) */

uint32_t plat_id_0, plat_id_4, plat_id_iss;
bool boot_strap_valid;
uint64_t boot_strap;
bool core_info_valid;
uint8_t core_info[4];

void plat_init(void)
{
	plat_id_0 = soc_read32(STRAP0);
	plat_id_4 = soc_read32(STRAP1);
	if (plat_id_0 == STRAP_NONE) {
		plat_id_iss = 1;
		plat_id_0 = 0;
		plat_id_4 = 0xffffffff;
	} else if (((plat_id_0 >> 17) & 3) == 0) {
		/* no package strapped: CIX's default package */
		plat_id_0 = (plat_id_0 & ~0x60000u) + 0x40000;
		plat_id_4 = (plat_id_4 & 0x80000000u) + 0x15fa3dff;
	}
	/* the SE's PM strap counts when it is set and its bytes sum to 0xff */
	boot_strap = REG32(PM_STRAP) | (uint64_t)REG32(PM_STRAP + 4) << 32;
	if (boot_strap) {
		uint8_t sum = 0;

		for (unsigned i = 0; i < 8; i++)
			sum = (uint8_t)(sum + (boot_strap >> (8 * i)));
		boot_strap_valid = sum == 0xff;
	}
	/*
	 * The SE's harvest word: [11:0] a bit per core, set when fused off
	 * (kept inverted, a bit per core present), [15:12] the boot core.
	 */
	uint32_t harvest = REG32(HARVEST) ^ 0xfffu;
	unsigned boot_core = (harvest >> 12) & 0xf;

	for (unsigned i = 0; i < 4; i++)
		core_info[i] = (uint8_t)(harvest >> (8 * i));
	core_info_valid = harvest && boot_core < 12 && (harvest & BIT(boot_core));
}
