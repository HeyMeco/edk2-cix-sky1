/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The DDR driver's interface to the SE firmware (CIX
 * driver/ddr/src/drv_ddr.c): DRAM initialisation, the result for later
 * stages, the memory size and channel count.  Translated from CIX's SE
 * firmware debug build, function by function.
 */
#include "mem_types.h"

/*
 * drv_ddr_init: in boot environment 1 the DRAM is already set up.  Else
 * the controllers, PHYs and PIs are set up and trained until training
 * succeeds (a detection pass asks for another one; CIX retries a failed
 * one for good), then the optional memory BIST, the run-time controller
 * state, interrupts and the TZC-400s.
 */
void drv_ddr_init(void)
{
	uint8_t st;

	postcode(0x9100);
	CdcbDriverInit();
	if (MemGetBootEnv() != 1) {
		MemStructInit();
		do {
			MemDramFunctionalInit();
			MemBpCheckpoint(1);
			MemDdrcReset();
			postcode(0x9102);
			DdrcPllInit();
			MemBpCheckpoint(2);
			MemDdrcInit();
			MemPhyInit();
			MemPiInit();
			MemFeatConfig();
			st = MemDdrssInit();
			if (st != 0 && st != 3)
				log_print(1, "DDR training failed (%d), retrying\n", st);
		} while (st != 0);
		RunMemBistTest();
		MemDdrcAfterPhyTrain();
		postcode(0x9101);
		RegisterDdrcIrq();
		MemBpCheckpoint(0x10);
		Tzc400Init();
		MemBpCheckpoint(0x20);
	}
	postcode(0x910f);
}

/* drv_ddr_postci: after the interconnect is up (DRAM reachable): the
 * result record; the memory configuration buffer is freed */
void drv_ddr_postci(void)
{
	if (MemGetBootEnv() != 1)
		MemInitialResultOutput();
	CdcbDeallocateBuffer();
}

/* drv_ddr_get_ch_num: 4 with all channels, else 2 */
uint32_t drv_ddr_get_ch_num(void)
{
	return GetMemInitPtr()->ChannelMask == 0xf ? 4 : 2;
}

/* drv_ddr_get_mem_size: MiB */
uint32_t drv_ddr_get_mem_size(void)
{
	return GetMemInitPtr()->MemorySize;
}

/* drv_ddr_set_avail_mem_size: what is left after the interconnect's
 * address bit drop, for the result record */
void drv_ddr_set_avail_mem_size(uint32_t size)
{
	GetMemOutputBuffer()->AvailableSize = size;
}

/* drv_ddr_ecc_enabled */
uint8_t drv_ddr_ecc_enabled(void)
{
	return WrLinkEccEn(2) || RdLinkEccEn(2) || IEccEn();
}
