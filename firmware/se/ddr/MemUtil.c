/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Register and mode-register access, reset, the memory space test (CIX
 * driver/ddr/src/Lib/MemUtil.c).  Translated from CIX's SE firmware debug
 * build, function by function.
 *
 * Each channel's Cadence controller, PI and PHY are one register file at
 * SMN 0x0c010000 + channel * 0x20000: controller registers (DENALI_CTL_n)
 * at 4 * n, PI registers from 0x2000, PHY registers from 0x4000.
 */
#include "mem_types.h"

#define DDRC_SMN_BASE	0x0c010000
#define DDRC_STRIDE_SHIFT	17

/* MemGetBootEnv: the boot environment of the memory configuration's quick
 * configuration (1: DRAM already set up, nothing to train; 0: the PI and
 * PHY registers are not there), read once */
uint8_t MemGetBootEnv(void)
{
	static uint8_t BootEnv = 0xdd;		/* not read yet */

	if (BootEnv == 0xdd)
		BootEnv = CdcbGetQuickConfig()->BootEnv;
	return BootEnv;
}

/* MemDdrcRegRead: in boot environment 0 the PI and PHY registers read 0 */
uint32_t MemDdrcRegRead(uint32_t offset, uint8_t ch)
{
	uint8_t env = MemGetBootEnv();

	if (offset < 0x2000 || env != 0)
		return SmnRegRead32(DDRC_SMN_BASE + offset + ((uint32_t)ch << DDRC_STRIDE_SHIFT));
	return 0;
}

/* MemDdrcRegWrite */
void MemDdrcRegWrite(uint32_t offset, uint8_t ch, uint32_t value)
{
	uint8_t env = MemGetBootEnv();

	if (offset < 0x2000 || env != 0)
		SmnRegWrite32(DDRC_SMN_BASE + offset + ((uint32_t)ch << DDRC_STRIDE_SHIFT), value);
}

/* MemModeRegRead: mode register MrNum of Device (16-bit lanes, 8-bit in
 * byte mode) on Rank, through the controller's READ_MODEREG (CTL_206) */
uint8_t MemModeRegRead(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t MrNum)
{
	DENALI_CTL_206 ddrc_206;
	uint32_t ReadVal;

	ddrc_206.value = MemDdrcRegRead(0x338, Channel);
	ddrc_206.fields.READ_MODEREG = MrNum | Rank << 8 | 0x10000;
	MemDdrcRegWrite(0x338, Channel, ddrc_206.value);
	while (!(MemDdrcRegRead(0x59c, Channel) & 0x40000))	/* MRR_DONE */
		;
	MemDdrcRegWrite(0x5c0, Channel, 0x40000);
	ReadVal = MemDdrcRegRead(0x33c, Channel);		/* PERIPHERAL_MRR_DATA */
	return ReadVal >> (Device * (ByteModeEn() ? 8 : 16));
}

/* MemModeRegWrite: Value to mode register MrNum of Rank (0xff: all ranks)
 * through WRITE_MODEREG (CTL_205), MRSINGLE_DATA (CTL_217) holding it.
 * Without MRW_DONE after 1001 polls it stops for good (CIX). */
void MemModeRegWrite(uint8_t Channel, uint8_t Rank, uint8_t MrNum, uint8_t Value)
{
	DENALI_CTL_205 ddrc_205 = { .value = 0 };
	DENALI_CTL_217 ddrc_217;
	uint32_t count;

	ddrc_217.value = MemDdrcRegRead(0x364, Channel);
	ddrc_217.fields.MRSINGLE_DATA_0 = Value;
	ddrc_217.fields.MRSINGLE_DATA_1 = Value;
	MemDdrcRegWrite(0x364, Channel, ddrc_217.value);
	if (Rank == 0xff)
		ddrc_205.fields.WRITE_MODEREG = MrNum | 0x3800000;
	else
		ddrc_205.fields.WRITE_MODEREG = MrNum | Rank << 8 | 0x2800000;
	MemDdrcRegWrite(0x334, Channel, ddrc_205.value);
	for (count = 1001; count; count--) {
		if (MemDdrcRegRead(0x59c, Channel) & 0x80000) {	/* MRW_DONE */
			MemDdrcRegWrite(0x5c0, Channel, 0x80000);
			return;
		}
	}
	log_print(1, "MR%d writing timeout.\n", MrNum);
	for (;;)
		;
}

/* MemDdrcReset: pulse the DDR subsystem's reset (SoC reset release
 * register bit 0) */
void MemDdrcReset(void)
{
	SmnRegWrite32(0x16000700, 0);
	SmnRegWrite32(0x16000400, SmnRegRead32(0x16000400) | 1);
	udelay(1);
	SmnRegWrite32(0x16000400, SmnRegRead32(0x16000400) & ~1u);
	udelay(1);
	SmnRegWrite32(0x16000400, SmnRegRead32(0x16000400) | 1);
}

/* MemChannelPopulated */
bool MemChannelPopulated(uint8_t Channel)
{
	return GetMemChannelPtr(Channel)->Enabled == 1;
}

/* MemorySpaceInit: with inline ECC, the whole of DRAM goes through the
 * controller's BIST once (with its ECC check off) so that the ECC is
 * initialised; with read link ECC, the channels whose DRAM reads MR5
 * (manufacturer) as 0xff get the same. */
void MemorySpaceInit(void)
{
	MEMORY_INIT_S *MemInitPtr = GetMemInitPtr();
	DENALI_CTL_260 ddrc_260;
	uint32_t StartAddr, AddrSpace, Row;
	uint8_t Channel, chmsk;

	if (MemInitPtr->ChannelCount == 4)
		StartAddr = 0x20000000;
	else if (MemInitPtr->ChannelCount == 2)
		StartAddr = 0x40000000;
	else
		StartAddr = 0x80000000;
	Row = MemInitPtr->RowBits;
	if (MemInitPtr->RanksPerChannel == 2)
		Row++;
	AddrSpace = Row + MemInitPtr->ColBits + MemInitPtr->BaBits + MemInitPtr->BgBits + 2;
	if (IEccEn()) {
		for (Channel = 0; Channel < 4; Channel++) {
			if (!MemChannelPopulated(Channel))
				continue;
			ddrc_260.value = MemDdrcRegRead(0x410, Channel);
			ddrc_260.value = (ddrc_260.value & ~0x3000000u) | 0x1000000;
			MemDdrcRegWrite(0x410, Channel, ddrc_260.value);
		}
		MemDdrcBistTest(MemInitPtr->ChannelMask, 0, StartAddr, AddrSpace, MEM_BIST_DDRPI_PROGRAMMABLE_MARCH);
		for (Channel = 0; Channel < 4; Channel++) {
			if (!MemChannelPopulated(Channel))
				continue;
			ddrc_260.value = MemDdrcRegRead(0x410, Channel);
			MemDdrcRegWrite(0x410, Channel, ddrc_260.value | 0x3000000);
		}
	} else if (RdLinkEccEn(2)) {
		chmsk = 0;
		for (Channel = 0; Channel < 4; Channel++) {
			if (MemChannelPopulated(Channel) && MemModeRegRead(Channel, 0, 0, 5) == 0xff)
				chmsk |= 1 << Channel;
		}
		if (chmsk)
			MemDdrcBistTest(chmsk, 0, StartAddr, AddrSpace, MEM_BIST_DDRPI_PROGRAMMABLE_MARCH);
	}
}

/* drv_ddr_assert: report and stop */
void drv_ddr_assert(uint32_t filecode, uint32_t line)
{
	log_print(0, "FATAL_ERROR found @ FILECODE 0x%04x Line %d\n", filecode, line);
	for (;;)
		;
}
