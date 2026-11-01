/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The PI's memory BIST (CIX driver/ddr/src/Feat/BIST/MemDdrpiBist.c).
 * Translated from CIX's SE firmware debug build.
 */
#include "mem_types.h"
#include "mem_regs.h"

/* drv_ddr_assert file code of MemDdrpiBist.c; the line numbers passed with
 * it are CIX's */
#define FILE_CODE	0x6002

/*
 * MemDdrpiBistDataCheck: the PI's data-check BIST in mode (pattern 9) on
 * the channels of ChMask, over 2^AddrSpace locations from StartAddrHi:Lo,
 * with the controller accepting in order meanwhile.  Each channel gets up to
 * 50000 x 10 us, the count shared by all channels as in CIX's code; the
 * controller setting of the last channel started is what every channel gets
 * back, as in CIX's code.  Returns the mask of failed channels.
 */
uint8_t MemDdrpiBistDataCheck(uint8_t ChMask, uint32_t BistDataMask, uint32_t StartAddrHi, uint32_t StartAddrLo,
			      uint32_t AddrSpace, MEM_BIST_MODE mode)
{
	DENALI_CTL_343 ddrc_343;
	DENALI_PI_324 ddrpi_324;
	DENALI_PI_325 ddrpi_325;
	DENALI_PI_326 ddrpi_326;
	DENALI_PI_327 ddrpi_327;
	DENALI_PI_351 ddrpi_351;
	DENALI_PI_360 ddrpi_360;
	uint32_t loop = 0, ddrc_343_val = 0;
	uint8_t Channel, result = 0;

	if (mode > MEM_BIST_DDRPI_PROGRAMMABLE_MARCH)
		drv_ddr_assert(FILE_CODE, 35);
	for (Channel = 0; Channel < 4; Channel++) {
		if (!MemChannelPopulated(Channel) || !((ChMask >> Channel) & 1))
			continue;
		/* clear the BIST done interrupt (PI interrupt acknowledge, bit 16) */
		MemDdrcRegWrite(DENALI_PI(298), Channel, 0x10000);
		ddrc_343_val = MemDdrcRegRead(DENALI_CTL(343), Channel);
		ddrc_343.value = ddrc_343_val;
		ddrc_343.fields.IN_ORDER_ACCEPT = 1;
		ddrc_343.fields.WR_ORDER_REQ = 0;
		MemDdrcRegWrite(DENALI_CTL(343), Channel, ddrc_343.value);
		/* data masks (registers 329-330, no field types) */
		MemDdrcRegWrite(DENALI_PI(329), Channel, BistDataMask);
		MemDdrcRegWrite(DENALI_PI(330), Channel, BistDataMask);
		ddrpi_325.value = MemDdrcRegRead(DENALI_PI(325), Channel);
		ddrpi_325.fields.PI_BIST_DATA_CHECK = 1;
		ddrpi_325.fields.PI_BIST_ADDR_CHECK = 0;
		MemDdrcRegWrite(DENALI_PI(325), Channel, ddrpi_325.value);
		ddrpi_326.value = MemDdrcRegRead(DENALI_PI(326), Channel);
		ddrpi_326.fields.PI_BIST_START_ADDRESS_0 = StartAddrLo;
		MemDdrcRegWrite(DENALI_PI(326), Channel, ddrpi_326.value);
		ddrpi_327.value = MemDdrcRegRead(DENALI_PI(327), Channel);
		ddrpi_327.fields.PI_BIST_START_ADDRESS_1 = StartAddrHi;
		MemDdrcRegWrite(DENALI_PI(327), Channel, ddrpi_327.value);
		ddrpi_351.value = MemDdrcRegRead(DENALI_PI(351), Channel);
		ddrpi_351.fields.PI_BIST_MODE = mode;
		ddrpi_351.fields.PI_BIST_PAT_MODE = 2;
		MemDdrcRegWrite(DENALI_PI(351), Channel, ddrpi_351.value);
		ddrpi_360.value = MemDdrcRegRead(DENALI_PI(360), Channel);
		ddrpi_360.fields.PI_BIST_PAT_NUM = 9;
		MemDdrcRegWrite(DENALI_PI(360), Channel, ddrpi_360.value);
		ddrpi_324.value = MemDdrcRegRead(DENALI_PI(324), Channel);
		ddrpi_324.fields.PI_ADDR_SPACE = AddrSpace;
		MemDdrcRegWrite(DENALI_PI(324), Channel, ddrpi_324.value);
		ddrpi_324.fields.PI_BIST_GO = 1;
		MemDdrcRegWrite(DENALI_PI(324), Channel, ddrpi_324.value);
	}
	for (Channel = 0; Channel < 4; Channel++) {
		if (!MemChannelPopulated(Channel) || !((ChMask >> Channel) & 1))
			continue;
		/* BIST done: PI interrupt status bit 16 (register 296) */
		for (;;) {
			if (MemDdrcRegRead(DENALI_PI(296), Channel) & 0x10000)
				break;
			udelay(10);
			if (++loop >= 50001) {
				result |= 1 << Channel;
				break;
			}
		}
		MemDdrcRegWrite(DENALI_PI(298), Channel, 0x10000);
		ddrpi_324.value = MemDdrcRegRead(DENALI_PI(324), Channel);
		if (!(ddrpi_324.fields.PI_BIST_RESULT & 1))
			result |= 1 << Channel;
		ddrpi_324.value = MemDdrcRegRead(DENALI_PI(324), Channel);
		ddrpi_324.fields.PI_BIST_GO = 0;
		MemDdrcRegWrite(DENALI_PI(324), Channel, ddrpi_324.value);
		MemDdrcRegWrite(DENALI_CTL(343), Channel, ddrc_343_val);
	}
	return result;
}
