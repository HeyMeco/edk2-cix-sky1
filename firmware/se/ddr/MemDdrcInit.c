/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Controller set-up (CIX driver/ddr/src/ddrc/MemDdrcInit.c): CIX's
 * register table, DRAM class, DFI ratios, chip selects, byte mode, the
 * automatic low-power modes, timings; and after training, the memory test
 * and the controller's run-time state.  Translated from CIX's SE firmware
 * debug build, function by function.
 */
#include "mem_types.h"

/*
 * MemDdrcEmuOv: emulation platforms (boot environment 0): fixed DFI read
 * latencies and read data enables.  CIX sets TDFI_RDDATA_EN_F2 twice.
 */
void MemDdrcEmuOv(void)
{
	DENALI_CTL_190 ddrc_190;
	DENALI_CTL_510 ddrc_510;
	DENALI_CTL_521 ddrc_521;
	DENALI_CTL_532 ddrc_532;
	DENALI_CTL_519 ddrc_519;
	DENALI_CTL_530 ddrc_530;
	DENALI_CTL_541 ddrc_541;
	uint8_t Channel;

	for (Channel = 0; Channel < 4; Channel++) {
		if (!MemChannelPopulated(Channel))
			continue;
		ddrc_510.value = MemDdrcRegRead(0x7f8, Channel);
		ddrc_510.fields.TDFI_PHY_RDLAT_F0 = 100;
		MemDdrcRegWrite(0x7f8, Channel, ddrc_510.value);
		ddrc_521.value = MemDdrcRegRead(0x824, Channel);
		ddrc_521.fields.TDFI_PHY_RDLAT_F1 = 100;
		MemDdrcRegWrite(0x824, Channel, ddrc_521.value);
		ddrc_532.value = MemDdrcRegRead(0x850, Channel);
		ddrc_532.fields.TDFI_PHY_RDLAT_F2 = 100;
		MemDdrcRegWrite(0x850, Channel, ddrc_532.value);
		ddrc_519.value = MemDdrcRegRead(0x81c, Channel);
		ddrc_519.fields.TDFI_RDDATA_EN_F0 = 24;
		MemDdrcRegWrite(0x81c, Channel, ddrc_519.value);
		ddrc_530.value = MemDdrcRegRead(0x848, Channel);
		ddrc_530.fields.TDFI_RDDATA_EN_F1 = 24;
		MemDdrcRegWrite(0x848, Channel, ddrc_530.value);
		ddrc_541.value = MemDdrcRegRead(0x874, Channel);
		ddrc_541.fields.TDFI_RDDATA_EN_F2 = 24;
		MemDdrcRegWrite(0x874, Channel, ddrc_541.value);
		ddrc_541.value = MemDdrcRegRead(0x874, Channel);
		ddrc_541.fields.TDFI_RDDATA_EN_F2 = 24;
		MemDdrcRegWrite(0x874, Channel, ddrc_541.value);
		ddrc_190.value = MemDdrcRegRead(0x2f8, Channel);
		ddrc_190.fields.TDFI_LP_RESP = 12;
		MemDdrcRegWrite(0x2f8, Channel, ddrc_190.value);
	}
}

/* MemDdrcInit */
void MemDdrcInit(void)
{
	MEMORY_INIT_S *MemInitPtr;
	DENALI_CTL_0 ddrc_0;
	DENALI_CTL_6 ddrc_6;
	DENALI_CTL_7 ddrc_7;
	DENALI_CTL_152 ddrc_152;
	DENALI_CTL_187 ddrc_187;
	DENALI_CTL_190 ddrc_190;
	DENALI_CTL_191 ddrc_191;
	DENALI_CTL_192 ddrc_192;
	DENALI_CTL_335 ddrc_335;
	DENALI_CTL_340 ddrc_340;
	uint8_t Channel;

	postcode(0x9130);
	MemInitPtr = GetMemInitPtr();
	MemGetTechFuncPtr()->MemDdrcRegInit();
	for (Channel = 0; Channel < 4; Channel++) {
		if (!MemChannelPopulated(Channel))
			continue;
		ddrc_0.value = 0;
		if (MemInitPtr->DdrType == 1)
			ddrc_0.fields.DRAM_CLASS = 0xd;		/* LPDDR5 */
		else if (MemInitPtr->DdrType == 0)
			ddrc_0.fields.DRAM_CLASS = 0xb;		/* LPDDR4 */
		MemDdrcRegWrite(0, Channel, ddrc_0.value);
		/* DFI frequency ratio 1:4 or 1:2 per set point */
		ddrc_6.value = MemDdrcRegRead(0x18, Channel);
		ddrc_7.value = MemDdrcRegRead(0x1c, Channel);
		ddrc_7.fields.DFI_FREQ_RATIO_F1 = GetMemFspCfgPtr(1)->FreqRatio == 4 ? 2 : 1;
		ddrc_7.fields.DFI_FREQ_RATIO_F2 = GetMemFspCfgPtr(2)->FreqRatio == 4 ? 2 : 1;
		MemDdrcRegWrite(0x18, Channel, ddrc_6.value);
		MemDdrcRegWrite(0x1c, Channel, ddrc_7.value);
		ddrc_335.value = MemDdrcRegRead(0x53c, Channel);
		ddrc_335.fields.CS_MAP = MemInitPtr->RanksPerChannel == 2 ? 3 : 1;
		MemDdrcRegWrite(0x53c, Channel, ddrc_335.value);
		ddrc_152.value = MemDdrcRegRead(0x260, Channel);
		ddrc_152.fields.MC_RESERVED18 = ByteModeEn() != 0;
		ddrc_152.fields.MC_RESERVED19 = 0;
		MemDdrcRegWrite(0x260, Channel, ddrc_152.value);
		ddrc_340.value = MemDdrcRegRead(0x550, Channel);
		ddrc_340.fields.MEMDATA_RATIO_0 = ByteModeEn() ? 2 : 1;
		ddrc_340.fields.MEMDATA_RATIO_1 = ByteModeEn() ? 2 : 1;
		MemDdrcRegWrite(0x550, Channel, ddrc_340.value);
		MemDdrcRegWrite(0x554, Channel, ByteModeEn() ? 0x08040201 : 0x401);
		MemDdrcRegWrite(0x558, Channel, ByteModeEn() ? 0x08040201 : 0x401);
		/* automatic low power: wake-up events, no automatic entry or exit
		 * yet (MemDdrcAfterPhyTrain), memory gating, idle times */
		ddrc_187.value = MemDdrcRegRead(0x2ec, Channel);
		ddrc_187.fields.LPI_WAKEUP_EN = 0x2f;
		ddrc_187.fields.LPI_CTRL_WAKEUP_EN = 1;
		MemDdrcRegWrite(0x2ec, Channel, ddrc_187.value);
		ddrc_190.value = MemDdrcRegRead(0x2f8, Channel);
		ddrc_190.fields.TDFI_LP_RESP = 0x20;
		ddrc_190.fields.LP_AUTO_ENTRY_EN = 0;
		ddrc_190.fields.LP_AUTO_EXIT_EN = 0;
		MemDdrcRegWrite(0x2f8, Channel, ddrc_190.value);
		ddrc_191.value = MemDdrcRegRead(0x2fc, Channel);
		ddrc_191.fields.LP_AUTO_MEM_GATE_EN = 7;
		ddrc_191.fields.LP_AUTO_PD_IDLE = 0x400;
		MemDdrcRegWrite(0x2fc, Channel, ddrc_191.value);
		ddrc_192.value = MemDdrcRegRead(0x300, Channel);
		ddrc_192.fields.LP_AUTO_SR_LONG_IDLE = 0x80;
		ddrc_192.fields.LP_AUTO_SR_LONG_MC_GATE_IDLE = 0x80;
		ddrc_192.fields.LP_AUTO_SR_SHORT_IDLE = 0x800;
		MemDdrcRegWrite(0x300, Channel, ddrc_192.value);
	}
	MemDdrcTimingCfg();
	if (MemGetBootEnv() == 0)
		MemDdrcEmuOv();
}

/*
 * MemDdrcAfterPhyTrain: memory test, per-chip-select training multicast on
 * in the data slices, automatic low-power entry and exit on, every pending
 * interrupt acknowledged, master interrupt mask for run time.
 */
void MemDdrcAfterPhyTrain(void)
{
	DENALI_CTL_190 ddrc_190;
	uint32_t offset;
	uint8_t Channel, DataSlice;

	MemorySpaceInit();
	for (Channel = 0; Channel < 4; Channel++) {
		if (!MemChannelPopulated(Channel))
			continue;
		for (DataSlice = 0; DataSlice < 4; DataSlice++) {	/* PHY_6 of each slice */
			offset = 0x4018 + DataSlice * 0x400;
			MemDdrcRegWrite(offset, Channel, MemDdrcRegRead(offset, Channel) | 0x10000);
		}
		ddrc_190.value = MemDdrcRegRead(0x2f8, Channel);
		ddrc_190.fields.LP_AUTO_ENTRY_EN = 0xf;
		ddrc_190.fields.LP_AUTO_EXIT_EN = 0xf;
		MemDdrcRegWrite(0x2f8, Channel, ddrc_190.value);
		MemDdrcRegWrite(0x5a0, Channel, MemDdrcRegRead(0x57c, Channel));	/* TIMEOUT */
		MemDdrcRegWrite(0x5a4, Channel, MemDdrcRegRead(0x580, Channel));	/* ECC */
		MemDdrcRegWrite(0x5a8, Channel, MemDdrcRegRead(0x584, Channel));	/* LOWPOWER */
		MemDdrcRegWrite(0x5b0, Channel, MemDdrcRegRead(0x58c, Channel));	/* TRAINING */
		MemDdrcRegWrite(0x5b4, Channel, MemDdrcRegRead(0x590, Channel));	/* USERIF */
		MemDdrcRegWrite(0x5b8, Channel, MemDdrcRegRead(0x594, Channel));	/* MISC */
		MemDdrcRegWrite(0x5bc, Channel, MemDdrcRegRead(0x598, Channel));	/* BIST, DFI */
		MemDdrcRegWrite(0x5c0, Channel, MemDdrcRegRead(0x59c, Channel));	/* FREQ, INIT, MODE */
		MemDdrcRegWrite(0x578, Channel, 0x3445);
	}
	MemRfmModeCfg();
}
