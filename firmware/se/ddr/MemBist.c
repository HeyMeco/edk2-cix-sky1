/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Memory BIST after training (CIX driver/ddr/src/Feat/BIST/MemBist.c): the
 * controller's BIST (MbistEn 1) or the PI's (MbistEn 2), then the data eye
 * scan if enabled.  Translated from CIX's SE firmware debug build; the
 * results go only to the log, which the port leaves out.
 */
#include "mem_types.h"

/* RunMemBistTest: the controller's BIST over 30 address bits from the start
 * of each channel's second region, or the PI's over 2^17 (galpat) or 2^20
 * locations */
void RunMemBistTest(void)
{
	MEM_FEATURE_S *MemFeatPtr = GetMemFeatPtr();
	MEMORY_INIT_S *MemInitPtr = GetMemInitPtr();
	MEM_BIST_MODE mode;
	uint64_t Addr;
	uint8_t BistFail;

	if (MemFeatPtr->MbistEn == 1) {
		mode = MemFeatPtr->MbistMode;
		if (MemInitPtr->ChannelCount == 4)
			Addr = 0x20000000;
		else if (MemInitPtr->ChannelCount == 2)
			Addr = 0x40000000;
		else
			Addr = 0x80000000;
		BistFail = MemDdrcBistTest(GetMemInitPtr()->ChannelMask, 0, Addr, 30, mode);
		(void)BistFail;
	} else if (MemFeatPtr->MbistEn == 2) {
		mode = MemFeatPtr->MbistMode;
		BistFail = MemDdrpiBistDataCheck(GetMemInitPtr()->ChannelMask, 0, 0, 0,
						 mode == MEM_BIST_DDRPI_GALPAT ? 17 : 20, mode);
		(void)BistFail;
	}
	if (MemFeatPtr->EyeScanEn)
		MemBistDataEye();
}
