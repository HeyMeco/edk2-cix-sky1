/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Setup options of the memory configuration (CIX
 * driver/ddr/src/Lib/MemBiosSetupLib.c): block 0x1007, signature "BSET",
 * whose fields override the board's defaults unless left all ones.
 * Translated from CIX's SE firmware debug build.
 */
#include "mem_types.h"

#define BIOS_SETUP_GUID		0x1007
#define BIOS_SETUP_SIGNATURE	0x54455342	/* "BSET" */

static MEM_CONFIG_BIOS_SETUP mMemBiosSetupOptions;

/* MemSetupGetField: the width-byte field at offset of the setup options
 * into ptr; false when there are none or the field is unset (all ones) */
bool MemSetupGetField(uint32_t offset, uint8_t width, void *ptr)
{
	uint8_t *buf;
	uint32_t size, value = 0, unset;

	if (mMemBiosSetupOptions.Header.Signature != BIOS_SETUP_SIGNATURE) {
		if (CdcbLocateBlockBuf(BIOS_SETUP_GUID, &size, &buf))
			return false;
		memcpy(&mMemBiosSetupOptions, buf, sizeof(mMemBiosSetupOptions));
	}
	if (mMemBiosSetupOptions.Header.Signature != BIOS_SETUP_SIGNATURE)
		return false;
	memcpy(&value, (uint8_t *)&mMemBiosSetupOptions + offset, width);
	/* CIX computes (1 << width * 8) - 1, which the core's shift makes all
	 * ones for width 4 */
	unset = width >= 4 ? 0xffffffff : (1u << width * 8) - 1;
	if (value == unset)
		return false;
	switch (width) {
	case 1:
		*(uint8_t *)ptr = value;
		return true;
	case 2:
		*(uint16_t *)ptr = value;
		return true;
	case 4:
		*(uint32_t *)ptr = value;
		return true;
	default:
		return false;
	}
}
