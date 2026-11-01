/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The DRAM technology's functions (CIX driver/ddr/src/Tech/MemTechInit.c):
 * LPDDR5 only.  Translated from CIX's SE firmware debug build.
 */
#include "mem_types.h"

#define FILE_CODE	0x4001		/* drv_ddr_assert file code */

MEM_TECH_FUNC_S gTechFuncPtr;

/* MemTechFuncInit */
void MemTechFuncInit(uint8_t DdrType)
{
	if (DdrType != 1) {		/* LPDDR5 */
		log_print(0x80000000, "ERROR: Unsupported DDR type!\n");
		drv_ddr_assert(FILE_CODE, 31);		/* CIX's line number */
		return;
	}
	gTechFuncPtr.MemTimingInit = MemLpddr5TimingInit;
	gTechFuncPtr.MemDdrcRegInit = MemLpddr5DdrcRegInit;
	gTechFuncPtr.MemDdrpiRegInit = MemLpddr5DdrpiRegInit;
	gTechFuncPtr.MemDdrpRegInit = MemLpddr5DdrpRegInit;
	gTechFuncPtr.MemPlatformLookupBusConfig = MemPlatformLookupBusConfigLp5;
	gTechFuncPtr.MemGetSeqWait = MemLpddr5GetSeqWait;
	gTechFuncPtr.MemModeRegisterCallback = &MemLpddr5ModeRegisterCallback;
	gTechFuncPtr.MemGetPiTiming = MemLpddr5PiTiming;
	gTechFuncPtr.MemGetPhyTiming = MemLpddr5PhyTiming;
	gTechFuncPtr.MemGetTurnaroundTiming = MemLpddr5TurnaroundTiming;
}

/* MemGetTechFuncPtr */
MEM_TECH_FUNC_S *MemGetTechFuncPtr(void)
{
	return &gTechFuncPtr;
}
