/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The controller's memory BIST (CIX driver/ddr/src/Feat/BIST/
 * MemDdrcBist.c).  Translated from CIX's SE firmware debug build.
 */
#include "mem_types.h"
#include "mem_regs.h"

/* drv_ddr_assert file code of MemDdrcBist.c; the line numbers passed with
 * it are CIX's */
#define FILE_CODE	0x6001

/*
 * MemDdrcBistTest: run the BIST in mode on the channels of ChMask, over
 * 2^AddrSpace bytes from StartAddr, data checked, all channels started
 * together; the retention modes (2, 3) wait for the retention state and
 * leave it.  Each channel gets up to 50000 x 10 us, the count shared by all
 * channels as in CIX's code.  Returns the mask of failed channels.
 */
uint8_t MemDdrcBistTest(uint8_t ChMask, uint32_t BistDataMask, uint64_t StartAddr, uint32_t AddrSpace,
			MEM_BIST_MODE mode)
{
	DENALI_CTL_243 ddrc_243;
	DENALI_CTL_244 ddrc_244;
	DENALI_CTL_245 ddrc_245;
	DENALI_CTL_246 ddrc_246;
	DENALI_CTL_247 ddrc_247;
	DENALI_CTL_248 ddrc_248;
	DENALI_CTL_249 ddrc_249;
	DENALI_CTL_250 ddrc_250;
	DENALI_CTL_259 ddrc_259;
	DENALI_CTL_260 ddrc_260;
	DENALI_CTL_336 ddrc_336;
	uint32_t DataPattern[8] = { 0 };
	uint32_t loop = 0;
	uint8_t Channel, i, FailMsk = 0;

	if (mode > MEM_BIST_MEM_INITIAL_PROGRAMMING)
		drv_ddr_assert(FILE_CODE, 53);
	for (Channel = 0; Channel < 4; Channel++) {
		if (!MemChannelPopulated(Channel) || !((ChMask >> Channel) & 1))
			continue;
		ddrc_244.value = MemDdrcRegRead(DENALI_CTL(244), Channel);
		ddrc_244.fields.BIST_START_ADDRESS_0 = (uint32_t)StartAddr;
		MemDdrcRegWrite(DENALI_CTL(244), Channel, ddrc_244.value);
		ddrc_245.value = MemDdrcRegRead(DENALI_CTL(245), Channel);
		ddrc_245.fields.BIST_START_ADDRESS_1 = StartAddr >> 32;
		MemDdrcRegWrite(DENALI_CTL(245), Channel, ddrc_245.value);
		ddrc_243.value = MemDdrcRegRead(DENALI_CTL(243), Channel);
		ddrc_243.fields.ADDR_SPACE = AddrSpace;
		ddrc_243.fields.BIST_DATA_CHECK = 1;
		MemDdrcRegWrite(DENALI_CTL(243), Channel, ddrc_243.value);
		ddrc_336.value = MemDdrcRegRead(DENALI_CTL(336), Channel);
		ddrc_336.fields.WRITE_DATA_CHAN_PARITY_EN = 0;
		MemDdrcRegWrite(DENALI_CTL(336), Channel, ddrc_336.value);
		ddrc_246.value = MemDdrcRegRead(DENALI_CTL(246), Channel);
		ddrc_246.fields.BIST_DATA_MASK_0 = BistDataMask;
		MemDdrcRegWrite(DENALI_CTL(246), Channel, ddrc_246.value);
		ddrc_247.value = MemDdrcRegRead(DENALI_CTL(247), Channel);
		ddrc_247.fields.BIST_DATA_MASK_1 = BistDataMask;
		MemDdrcRegWrite(DENALI_CTL(247), Channel, ddrc_247.value);
		ddrc_248.value = MemDdrcRegRead(DENALI_CTL(248), Channel);
		ddrc_248.fields.BIST_DATA_MASK_2 = BistDataMask;
		MemDdrcRegWrite(DENALI_CTL(248), Channel, ddrc_248.value);
		ddrc_249.value = MemDdrcRegRead(DENALI_CTL(249), Channel);
		ddrc_249.fields.BIST_DATA_MASK_3 = BistDataMask;
		MemDdrcRegWrite(DENALI_CTL(249), Channel, ddrc_249.value);
		ddrc_250.value = MemDdrcRegRead(DENALI_CTL(250), Channel);
		ddrc_250.fields.BIST_TEST_MODE = mode;
		MemDdrcRegWrite(DENALI_CTL(250), Channel, ddrc_250.value);
		/* the data pattern (registers 251-258, no field types) */
		if (mode != MEM_BIST_MOVI1_3N) {
			for (i = 0; i < 8; i++)
				MemDdrcRegWrite(DENALI_CTL(251) + i * 4, Channel, DataPattern[i]);
		}
		ddrc_259.value = MemDdrcRegRead(DENALI_CTL(259), Channel);
		ddrc_259.fields.BIST_ERR_STOP = 1;
		MemDdrcRegWrite(DENALI_CTL(259), Channel, ddrc_259.value);
	}
	for (Channel = 0; Channel < 4; Channel++) {
		if (!MemChannelPopulated(Channel) || !((ChMask >> Channel) & 1))
			continue;
		ddrc_243.value = MemDdrcRegRead(DENALI_CTL(243), Channel);
		ddrc_243.fields.BIST_GO = 1;
		MemDdrcRegWrite(DENALI_CTL(243), Channel, ddrc_243.value);
	}
	if (mode == MEM_BIST_SELF_REF_RETENTION || mode == MEM_BIST_IDLE_RETENTION) {
		for (Channel = 0; Channel < 4; Channel++) {
			if (!MemChannelPopulated(Channel) || !((ChMask >> Channel) & 1))
				continue;
			do
				ddrc_259.value = MemDdrcRegRead(DENALI_CTL(259), Channel);
			while (!ddrc_259.fields.BIST_RET_STATE);
		}
		for (Channel = 0; Channel < 4; Channel++) {
			if (!MemChannelPopulated(Channel) || !((ChMask >> Channel) & 1))
				continue;
			ddrc_260.value = MemDdrcRegRead(DENALI_CTL(260), Channel);
			ddrc_260.fields.BIST_RET_STATE_EXIT = 1;
			MemDdrcRegWrite(DENALI_CTL(260), Channel, ddrc_260.value);
		}
	}
	for (Channel = 0; Channel < 4; Channel++) {
		if (!MemChannelPopulated(Channel) || !((ChMask >> Channel) & 1))
			continue;
		/* BIST done: interrupt status bit 0 (register 358), acknowledged
		 * in register 367 */
		for (;;) {
			if (MemDdrcRegRead(DENALI_CTL(358), Channel) & 1)
				break;
			udelay(10);
			if (++loop >= 50001) {
				FailMsk |= 1 << Channel;
				break;
			}
		}
		MemDdrcRegWrite(DENALI_CTL(367), Channel, 1);
		ddrc_243.value = MemDdrcRegRead(DENALI_CTL(243), Channel);
		if (!(ddrc_243.fields.BIST_RESULT & 1))
			FailMsk |= 1 << Channel;
	}
	for (Channel = 0; Channel < 4; Channel++) {
		if (!MemChannelPopulated(Channel) || !((ChMask >> Channel) & 1))
			continue;
		ddrc_243.value = MemDdrcRegRead(DENALI_CTL(243), Channel);
		ddrc_243.fields.BIST_GO = 0;
		MemDdrcRegWrite(DENALI_CTL(243), Channel, ddrc_243.value);
	}
	return FailMsk;
}
