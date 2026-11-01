/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Refresh mode (CIX driver/ddr/src/Feat/REF/MemRefreshMode.c): all-bank or
 * per-bank refresh and the refresh thresholds of each populated channel.
 * Translated from CIX's SE firmware debug build.
 */
#include "mem_types.h"
#include "mem_regs.h"

/* MemRefreshModeCfg */
void MemRefreshModeCfg(void)
{
	MEMORY_INIT_S *MemInitPtr = GetMemInitPtr();
	DENALI_CTL_75 ddrc_75;
	DENALI_CTL_76 ddrc_76;
	DENALI_CTL_298 ddrc_298;
	uint8_t Channel;

	for (Channel = 0; Channel < 4; Channel++) {
		if (!MemChannelPopulated(Channel))
			continue;
		if (!MemPbrEn()) {
			ddrc_75.value = MemDdrcRegRead(DENALI_CTL(75), Channel);
			ddrc_75.fields.PBR_MODE_EN = 0;
			MemDdrcRegWrite(DENALI_CTL(75), Channel, ddrc_75.value);
			ddrc_298.value = MemDdrcRegRead(DENALI_CTL(298), Channel);
			ddrc_298.fields.AREF_NORM_THRESHOLD = 0x11;
			ddrc_298.fields.AREF_HIGH_THRESHOLD = 0x14;
			MemDdrcRegWrite(DENALI_CTL(298), Channel, ddrc_298.value);
		} else {
			ddrc_75.value = MemDdrcRegRead(DENALI_CTL(75), Channel);
			ddrc_75.fields.PBR_MODE_EN = MemInitPtr->DdrType == MEM_LPDDR5 ? 4 : 1;
			MemDdrcRegWrite(DENALI_CTL(75), Channel, ddrc_75.value);
			ddrc_76.value = MemDdrcRegRead(DENALI_CTL(76), Channel);
			ddrc_76.fields.PBR_NUMERIC_ORDER = 0;
			ddrc_76.fields.PBR_CONT_REQ_EN = 1;
			ddrc_76.fields.AREF_PBR_CONT_EN_THRESHOLD = 0x11;
			MemDdrcRegWrite(DENALI_CTL(76), Channel, ddrc_76.value);
			ddrc_298.value = MemDdrcRegRead(DENALI_CTL(298), Channel);
			ddrc_298.fields.AREF_NORM_THRESHOLD = 0x13;
			ddrc_298.fields.AREF_HIGH_THRESHOLD = 0x14;
			MemDdrcRegWrite(DENALI_CTL(298), Channel, ddrc_298.value);
		}
	}
}
