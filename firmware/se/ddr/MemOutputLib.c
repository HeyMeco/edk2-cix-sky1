/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The DDR initialisation result for later stages (CIX
 * driver/ddr/src/Lib/MemOutputLib.c): a "CDOB" record at the start of the
 * SYSHUB window (DRAM).  Translated from CIX's SE firmware debug build.
 */
#include "mem_types.h"

#define MEM_OUTPUT_BUFFER	0x63c00000
#define MEM_OUTPUT_SIGNATURE	0x424f4443	/* "CDOB" */

/* GetMemOutputBuffer */
MEM_INIT_OUTPUT_BUFFER *GetMemOutputBuffer(void)
{
	return (MEM_INIT_OUTPUT_BUFFER *)MEM_OUTPUT_BUFFER;
}

/* MemInitialResultOutput: version 1.0; ECC type 5 with any ECC (inline or
 * link), else 3; the highest frequency set point's data rate.  The
 * available size is filled in later (drv_ddr_set_avail_mem_size). */
void MemInitialResultOutput(void)
{
	MEMORY_INIT_S *MemInitPtr = GetMemInitPtr();
	MEM_INIT_OUTPUT_BUFFER *Out = GetMemOutputBuffer();
	uint8_t Ecc;

	Out->Signature = MEM_OUTPUT_SIGNATURE;
	Out->MajorVer = 1;
	Out->MinorVer = 0;
	Out->DdrType = MemInitPtr->DdrType;
	Out->ChannelMask = MemInitPtr->ChannelMask;
	Out->RanksPerChannel = MemInitPtr->RanksPerChannel;
	Out->TotalSize = MemInitPtr->MemorySize;
	Ecc = IEccEn();
	Ecc |= WrLinkEccEn(2);
	Ecc |= RdLinkEccEn(2);
	Out->EccType = Ecc ? 5 : 3;
	Out->MaxFreq = MemInitPtr->GlobalFspConfig[2].MemFreq;
}
