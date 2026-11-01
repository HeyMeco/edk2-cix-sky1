/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Address mapping (CIX driver/ddr/src/ddrc/MemAddressMapping.c): bank
 * architecture, row/column/bank widths, chip-select ranges, the address
 * remap regions and the address order of each populated channel.
 * Translated from CIX's SE firmware debug build.
 */
#include "mem_types.h"
#include "mem_regs.h"

/*
 * MemAddressMapping.  The widths are the controller's maxima less the
 * part's: 18 row bits, 11 column bits, 4 bank (group) bits.  In 8-bank mode
 * (LPDDR5 with FSP 0's bank architecture 0) a bank bit becomes a column bit;
 * CIX's code does that once per populated channel, so the column and bank
 * differences of channels after the first are off (the CS ranges, from
 * their sum, are not).  Kept: 8-bank mode is not used on Sky1 boards.
 */
void MemAddressMapping(void)
{
	MEMORY_INIT_S *MemInitPtr = GetMemInitPtr();
	DENALI_CTL_325 ddrc_325;
	DENALI_CTL_326 ddrc_326;
	DENALI_CTL_327 ddrc_327;
	DENALI_CTL_328 ddrc_328;
	DENALI_CTL_329 ddrc_329;
	DENALI_CTL_330 ddrc_330;
	DENALI_CTL_331 ddrc_331;
	DENALI_CTL_347 ddrc_347;
	DENALI_CTL_152 ddrc_152;
	DENALI_CTL_153 ddrc_153;
	DENALI_CTL_155 ddrc_155;
	DENALI_CTL_555 ddrc_555;
	DENALI_CTL_556 ddrc_556;
	DENALI_CTL_557 ddrc_557;
	DENALI_CTL_558 ddrc_558;
	DENALI_CTL_559 ddrc_559;
	DENALI_CTL_560 ddrc_560;
	DENALI_CTL_561 ddrc_561;
	DENALI_CTL_562 ddrc_562;
	DENALI_CTL_563 ddrc_563;
	DENALI_CTL_564 ddrc_564;
	DENALI_CTL_565 ddrc_565;
	DENALI_CTL_566 ddrc_566;
	DENALI_CTL_567 ddrc_567;
	DENALI_CTL_568 ddrc_568;
	DENALI_CTL_569 ddrc_569;
	DENALI_CTL_570 ddrc_570;
	DENALI_PI_369 ddrpi_369;
	DENALI_PI_222 ddrpi_222;
	uint64_t MemRemapRegionLo0, MemRemapRegionHi0, MemRemapRegionLo1, MemRemapRegionHi1;
	uint64_t MemRemapRegionLo2, MemRemapRegionHi2;
	uint8_t Channel, CsBitCount, RowBitCount, ColBitCount, BaBitCount;
	uint16_t cs_msk, cs_size;
	int shift;

	/* remap regions by channel count */
	if (MemInitPtr->ChannelCount == 4) {
		MemRemapRegionLo0 = 0x20000000;
		MemRemapRegionHi0 = 0x1ffffffffull;
		MemRemapRegionLo1 = 0x2000000000ull;
		MemRemapRegionHi1 = 0x3fffffffffull;
	} else if (MemInitPtr->ChannelCount == 2) {
		MemRemapRegionLo0 = 0x40000000;
		MemRemapRegionHi0 = 0x3ffffffffull;
		MemRemapRegionLo1 = 0x4000000000ull;
		MemRemapRegionHi1 = 0x7fffffffffull;
	} else {
		MemRemapRegionLo0 = 0x80000000;
		MemRemapRegionHi0 = 0x7ffffffffull;
		MemRemapRegionLo1 = 0x8000000000ull;
		MemRemapRegionHi1 = 0xffffffffffull;
	}
	MemRemapRegionLo2 = 0xffffffffffull;
	MemRemapRegionHi2 = 0xffffffffffull;
	CsBitCount = MemInitPtr->RanksPerChannel == 2;
	RowBitCount = MemInitPtr->RowBits;
	ColBitCount = MemInitPtr->ColBits;
	BaBitCount = MemInitPtr->BaBits + MemInitPtr->BgBits;

	for (Channel = 0; Channel < 4; Channel++) {
		if (!MemChannelPopulated(Channel))
			continue;
		if (MemInitPtr->DdrType == MEM_LPDDR5 && GetMemFspCfgPtr(0)->BankArchMode == 0) {
			BaBitCount--;
			ColBitCount++;
		}
		shift = (18 - RowBitCount) + CsBitCount + (11 - ColBitCount) + (4 - BaBitCount);
		cs_msk = 0xffff >> (shift + 5);
		/* inline ECC takes the top eighth of each rank */
		cs_size = (IEccEn() ? 0xdfff : 0xffff) >> shift;

		ddrc_152.value = MemDdrcRegRead(DENALI_CTL(152), Channel);
		ddrc_152.fields.BANK_ARCH_MODE_F0 = GetMemFspCfgPtr(0)->BankArchMode;
		MemDdrcRegWrite(DENALI_CTL(152), Channel, ddrc_152.value);
		ddrc_153.value = MemDdrcRegRead(DENALI_CTL(153), Channel);
		ddrc_153.fields.BANK_ARCH_MODE_F1 = GetMemFspCfgPtr(1)->BankArchMode;
		MemDdrcRegWrite(DENALI_CTL(153), Channel, ddrc_153.value);
		ddrc_155.value = MemDdrcRegRead(DENALI_CTL(155), Channel);
		ddrc_155.fields.BANK_ARCH_MODE_F2 = GetMemFspCfgPtr(2)->BankArchMode;
		MemDdrcRegWrite(DENALI_CTL(155), Channel, ddrc_155.value);

		ddrc_325.value = MemDdrcRegRead(DENALI_CTL(325), Channel);
		ddrc_325.fields.BANK_DIFF_0 = 4 - BaBitCount;
		ddrc_325.fields.BANK_DIFF_1 = 4 - BaBitCount;
		ddrc_325.fields.ROW_DIFF_0 = 18 - RowBitCount;
		MemDdrcRegWrite(DENALI_CTL(325), Channel, ddrc_325.value);
		ddrpi_222.value = MemDdrcRegRead(DENALI_PI(222), Channel);
		ddrpi_222.fields.PI_BANK_DIFF = 4 - BaBitCount;
		ddrpi_222.fields.PI_ROW_DIFF = 18 - RowBitCount;
		MemDdrcRegWrite(DENALI_PI(222), Channel, ddrpi_222.value);
		ddrc_326.value = MemDdrcRegRead(DENALI_CTL(326), Channel);
		ddrc_326.fields.COL_DIFF_0 = 11 - ColBitCount;
		ddrc_326.fields.COL_DIFF_1 = 11 - ColBitCount;
		ddrc_326.fields.ROW_DIFF_1 = 18 - RowBitCount;
		MemDdrcRegWrite(DENALI_CTL(326), Channel, ddrc_326.value);

		/* chip select 0 from 0, chip select 1 right above */
		ddrc_327.value = MemDdrcRegRead(DENALI_CTL(327), Channel);
		ddrc_327.fields.CS_VAL_LOWER_0 = 0;
		ddrc_327.fields.CS_VAL_UPPER_0 = cs_size;
		MemDdrcRegWrite(DENALI_CTL(327), Channel, ddrc_327.value);
		ddrc_328.value = MemDdrcRegRead(DENALI_CTL(328), Channel);
		ddrc_328.fields.CS_MSK_0 = cs_msk;
		MemDdrcRegWrite(DENALI_CTL(328), Channel, ddrc_328.value);
		ddrc_329.value = MemDdrcRegRead(DENALI_CTL(329), Channel);
		ddrc_329.fields.CS_VAL_LOWER_1 = cs_size + 1;
		ddrc_329.fields.CS_VAL_UPPER_1 = cs_size + 1 + cs_size;
		MemDdrcRegWrite(DENALI_CTL(329), Channel, ddrc_329.value);
		ddrc_330.value = MemDdrcRegRead(DENALI_CTL(330), Channel);
		ddrc_330.fields.CS_MSK_1 = cs_msk;
		MemDdrcRegWrite(DENALI_CTL(330), Channel, ddrc_330.value);

		/* remap regions 0-3 (40-bit addresses), region 3 empty, remap on */
		ddrc_555.value = (uint32_t)MemRemapRegionLo0;
		MemDdrcRegWrite(DENALI_CTL(555), Channel, ddrc_555.value);
		ddrc_556.value = 0;
		ddrc_556.fields.LOWER_ADDR_REGION_0_1 = MemRemapRegionLo0 >> 32;
		MemDdrcRegWrite(DENALI_CTL(556), Channel, ddrc_556.value);
		ddrc_557.value = (uint32_t)MemRemapRegionHi0;
		MemDdrcRegWrite(DENALI_CTL(557), Channel, ddrc_557.value);
		ddrc_558.value = 0;
		ddrc_558.fields.UPPER_ADDR_REGION_0_1 = MemRemapRegionHi0 >> 32;
		MemDdrcRegWrite(DENALI_CTL(558), Channel, ddrc_558.value);
		ddrc_559.value = (uint32_t)MemRemapRegionLo1;
		MemDdrcRegWrite(DENALI_CTL(559), Channel, ddrc_559.value);
		ddrc_560.value = 0;
		ddrc_560.fields.LOWER_ADDR_REGION_1_1 = MemRemapRegionLo1 >> 32;
		MemDdrcRegWrite(DENALI_CTL(560), Channel, ddrc_560.value);
		ddrc_561.value = (uint32_t)MemRemapRegionHi1;
		MemDdrcRegWrite(DENALI_CTL(561), Channel, ddrc_561.value);
		ddrc_562.value = 0;
		ddrc_562.fields.UPPER_ADDR_REGION_1_1 = MemRemapRegionHi1 >> 32;
		MemDdrcRegWrite(DENALI_CTL(562), Channel, ddrc_562.value);
		ddrc_563.value = (uint32_t)MemRemapRegionLo2;
		MemDdrcRegWrite(DENALI_CTL(563), Channel, ddrc_563.value);
		ddrc_564.value = 0;
		ddrc_564.fields.LOWER_ADDR_REGION_2_1 = MemRemapRegionLo2 >> 32;
		MemDdrcRegWrite(DENALI_CTL(564), Channel, ddrc_564.value);
		ddrc_565.value = (uint32_t)MemRemapRegionHi2;
		MemDdrcRegWrite(DENALI_CTL(565), Channel, ddrc_565.value);
		ddrc_566.value = 0;
		ddrc_566.fields.UPPER_ADDR_REGION_2_1 = MemRemapRegionHi2 >> 32;
		MemDdrcRegWrite(DENALI_CTL(566), Channel, ddrc_566.value);
		ddrc_567.value = 0xffffffff;
		MemDdrcRegWrite(DENALI_CTL(567), Channel, ddrc_567.value);
		ddrc_568.value = 0;
		ddrc_568.fields.LOWER_ADDR_REGION_3_1 = 0xff;
		MemDdrcRegWrite(DENALI_CTL(568), Channel, ddrc_568.value);
		ddrc_569.value = 0xffffffff;
		MemDdrcRegWrite(DENALI_CTL(569), Channel, ddrc_569.value);
		ddrc_570.value = 0;
		ddrc_570.fields.UPPER_ADDR_REGION_3_1 = 0xff;
		ddrc_570.fields.ADDR_REMAP_ENABLE = 1;
		MemDdrcRegWrite(DENALI_CTL(570), Channel, ddrc_570.value);

		/* address order: mode 0 programmable order 9, banks from bit 8;
		 * mode 1 (inline ECC) order 1 with bank group rotation */
		if (GetAddressMode() == 0) {
			ddrc_331.value = MemDdrcRegRead(DENALI_CTL(331), Channel);
			ddrc_331.fields.PROGRAMMABLE_ADDRESS_ORDER = 9;
			ddrc_331.fields.BANK_START_BIT = 8;
			MemDdrcRegWrite(DENALI_CTL(331), Channel, ddrc_331.value);
			ddrc_347.value = MemDdrcRegRead(DENALI_CTL(347), Channel);
			ddrc_347.fields.BG_ROTATE_EN = 0;
			MemDdrcRegWrite(DENALI_CTL(347), Channel, ddrc_347.value);
		} else if (GetAddressMode() == 1) {
			ddrc_331.value = MemDdrcRegRead(DENALI_CTL(331), Channel);
			ddrc_331.fields.PROGRAMMABLE_ADDRESS_ORDER = 1;
			ddrc_331.fields.BANK_START_BIT = 0;
			MemDdrcRegWrite(DENALI_CTL(331), Channel, ddrc_331.value);
			ddrc_347.value = MemDdrcRegRead(DENALI_CTL(347), Channel);
			ddrc_347.fields.BG_ROTATE_EN = 1;
			MemDdrcRegWrite(DENALI_CTL(347), Channel, ddrc_347.value);
			ddrpi_369.value = MemDdrcRegRead(DENALI_PI(369), Channel);
			ddrpi_369.fields.PI_COL_DIFF = 11 - ColBitCount;
			ddrpi_369.fields.PI_BG_ROTATE_EN = 1;
			MemDdrcRegWrite(DENALI_PI(369), Channel, ddrpi_369.value);
		}
	}
}
