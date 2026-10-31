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

uint32_t plat_id_0, plat_id_4, plat_id_iss;
bool boot_strap_valid;
uint64_t boot_strap;
/*
 * CIX takes the boot core from the SE's harvest information; until that is
 * here, the value CIX's image holds on an O6 (big core 2, DPM element 10).
 */
bool core_info_valid = true;
uint8_t core_info[4] = { 0xff, 0xaf };

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
	boot_strap = REG32(PM_STRAP) | (uint64_t)REG32(PM_STRAP + 4) << 32;
	boot_strap_valid = true;
}
