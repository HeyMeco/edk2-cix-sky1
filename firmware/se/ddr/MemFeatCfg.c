/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Controller features (CIX driver/ddr/src/Feat/MemFeatCfg.c): DQS
 * oscillator, WCK, data mask, bank selection, auto precharge, preambles,
 * DBI, link ECC and inline ECC of each populated channel, then address
 * mapping, refresh mode and port priorities.  Translated from CIX's SE
 * firmware debug build, function by function.
 */
#include "mem_types.h"
#include "mem_regs.h"

/* MemInlineEccCfg */
void MemInlineEccCfg(void)
{
	DENALI_CTL_260 ddrc_260;
	DENALI_CTL_263 ddrc_263;
	DENALI_CTL_264 ddrc_264;
	DENALI_CTL_293 ddrc_293;
	DENALI_CTL_332 ddrc_332;
	DENALI_CTL_333 ddrc_333;
	DENALI_CTL_335 ddrc_335;
	DENALI_CTL_343 ddrc_343;
	uint8_t Channel;

	for (Channel = 0; Channel < 4; Channel++) {
		if (!MemChannelPopulated(Channel))
			continue;
		if (IEccEn()) {
			ddrc_263.value = MemDdrcRegRead(DENALI_CTL(263), Channel);
			ddrc_263.fields.ECC_WRITEBACK_EN = 1;
			ddrc_263.fields.INLINE_ECC_SAME_PAGE = 1;
			ddrc_263.fields.INLINE_ECC_BANK_OFFSET = 2;
			MemDdrcRegWrite(DENALI_CTL(263), Channel, ddrc_263.value);
			ddrc_264.value = MemDdrcRegRead(DENALI_CTL(264), Channel);
			ddrc_264.fields.INLINE_ECC_BG_OFFSET = 0;
			ddrc_264.fields.ECC_READ_CACHING_EN = 1;
			ddrc_264.fields.ECC_WRITE_COMBINING_EN = 1;
			MemDdrcRegWrite(DENALI_CTL(264), Channel, ddrc_264.value);
			ddrc_293.value = MemDdrcRegRead(DENALI_CTL(293), Channel);
			ddrc_293.fields.ECC_SCRUB_MODE = 1;
			MemDdrcRegWrite(DENALI_CTL(293), Channel, ddrc_293.value);
			ddrc_332.value = MemDdrcRegRead(DENALI_CTL(332), Channel);
			ddrc_332.fields.ADDR_COLLISION_MPM_DIS = 1;
			MemDdrcRegWrite(DENALI_CTL(332), Channel, ddrc_332.value);
			ddrc_333.value = MemDdrcRegRead(DENALI_CTL(333), Channel);
			ddrc_333.fields.RW_SAME_PAGE_EN = 0;
			MemDdrcRegWrite(DENALI_CTL(333), Channel, ddrc_333.value);
			ddrc_335.value = MemDdrcRegRead(DENALI_CTL(335), Channel);
			ddrc_335.fields.DISABLE_RD_INTERLEAVE = 0;
			ddrc_335.fields.SWAP_EN = 0;
			MemDdrcRegWrite(DENALI_CTL(335), Channel, ddrc_335.value);
			ddrc_343.value = MemDdrcRegRead(DENALI_CTL(343), Channel);
			ddrc_343.fields.IN_ORDER_ACCEPT = 1;
			MemDdrcRegWrite(DENALI_CTL(343), Channel, ddrc_343.value);
		}
		/* sideband ECC stays off, inline ECC or not */
		ddrc_260.value = MemDdrcRegRead(DENALI_CTL(260), Channel);
		ddrc_260.fields.ECC_ENABLE = 0;
		MemDdrcRegWrite(DENALI_CTL(260), Channel, ddrc_260.value);
	}
}

/* MemFeatConfig */
void MemFeatConfig(void)
{
	DENALI_CTL_12 ddrc_12;
	DENALI_CTL_54 ddrc_54;
	DENALI_CTL_76 ddrc_76;
	DENALI_CTL_113 ddrc_113;
	DENALI_CTL_154 ddrc_154;
	DENALI_CTL_155 ddrc_155;
	DENALI_CTL_335 ddrc_335;
	DENALI_CTL_343 ddrc_343;
	DENALI_CTL_344 ddrc_344;
	DENALI_CTL_345 ddrc_345;
	DENALI_CTL_346 ddrc_346;
	DENALI_CTL_545 ddrc_545;
	DENALI_PI_221 ddrpi_221;
	uint8_t Channel;

	for (Channel = 0; Channel < 4; Channel++) {
		if (!MemChannelPopulated(Channel))
			continue;
		if (!DqsOscEn()) {
			ddrc_12.value = MemDdrcRegRead(DENALI_CTL(12), Channel);
			ddrc_12.fields.DQS_OSC_ENABLE = 0;
			MemDdrcRegWrite(DENALI_CTL(12), Channel, ddrc_12.value);
			ddrpi_221.value = MemDdrcRegRead(DENALI_PI(221), Channel);
			ddrpi_221.fields.PI_DQS_OSC_PERIOD_EN = 0;
			MemDdrcRegWrite(DENALI_PI(221), Channel, ddrpi_221.value);
		} else {
			ddrc_12.value = MemDdrcRegRead(DENALI_CTL(12), Channel);
			ddrc_12.fields.DQS_OSC_ENABLE = 1;
			MemDdrcRegWrite(DENALI_CTL(12), Channel, ddrc_12.value);
			MemDdrcRegWrite(DENALI_CTL(14), Channel, 0x10);
			MemDdrcRegWrite(DENALI_CTL(15), Channel, 0x100);
			MemDdrcRegWrite(DENALI_CTL(16), Channel, 0x10000);
			ddrpi_221.value = MemDdrcRegRead(DENALI_PI(221), Channel);
			ddrpi_221.fields.PI_DQS_OSC_PERIOD_EN = 1;
			MemDdrcRegWrite(DENALI_PI(221), Channel, ddrpi_221.value);
		}
		ddrc_113.value = 0;
		ddrc_113.fields.WCK_GROUP = 2;
		ddrc_113.fields.WCK_MODE = WckAlwaysOn() != 0;
		ddrc_113.fields.WCK_ON_ADJUST = 1;
		MemDdrcRegWrite(DENALI_CTL(113), Channel, ddrc_113.value);
		ddrc_335.value = MemDdrcRegRead(DENALI_CTL(335), Channel);
		ddrc_335.fields.SWAP_EN = 0;
		MemDdrcRegWrite(DENALI_CTL(335), Channel, ddrc_335.value);
		ddrc_343.value = MemDdrcRegRead(DENALI_CTL(343), Channel);
		ddrc_343.fields.IN_ORDER_ACCEPT = 0;
		MemDdrcRegWrite(DENALI_CTL(343), Channel, ddrc_343.value);
		ddrc_545.value = MemDdrcRegRead(DENALI_CTL(545), Channel);
		ddrc_545.fields.DISABLE_MEMORY_MASKED_WRITE = !DataMaskEn() && !IEccEn();
		MemDdrcRegWrite(DENALI_CTL(545), Channel, ddrc_545.value);
		ddrc_76.value = MemDdrcRegRead(DENALI_CTL(76), Channel);
		ddrc_76.fields.SELECT_BANK_IN_Q = SelBankInQEn() != 0;
		MemDdrcRegWrite(DENALI_CTL(76), Channel, ddrc_76.value);
		ddrc_54.value = MemDdrcRegRead(DENALI_CTL(54), Channel);
		ddrc_54.fields.AP = AutoPrechargeEn() != 0;
		MemDdrcRegWrite(DENALI_CTL(54), Channel, ddrc_54.value);
		ddrc_344.value = MemDdrcRegRead(DENALI_CTL(344), Channel);
		ddrc_344.fields.RD_PREAMBLE_LEN_F0 = RdPreambleLen();
		ddrc_344.fields.RD_PREAMBLE_LEN_F1 = RdPreambleLen();
		MemDdrcRegWrite(DENALI_CTL(344), Channel, ddrc_344.value);
		ddrc_345.value = MemDdrcRegRead(DENALI_CTL(345), Channel);
		ddrc_345.fields.RD_PREAMBLE_LEN_F2 = RdPreambleLen();
		ddrc_345.fields.WR_PREAMBLE_LEN_F1 = WrPreambleLen();
		ddrc_345.fields.WR_PREAMBLE_LEN_F2 = WrPreambleLen();
		MemDdrcRegWrite(DENALI_CTL(345), Channel, ddrc_345.value);
		ddrc_346.value = MemDdrcRegRead(DENALI_CTL(346), Channel);
		ddrc_346.fields.RD_DBI_EN = ReadDbiEn();
		ddrc_346.fields.WR_DBI_EN = WriteDbiEn();
		MemDdrcRegWrite(DENALI_CTL(346), Channel, ddrc_346.value);
		ddrc_154.value = MemDdrcRegRead(DENALI_CTL(154), Channel);
		ddrc_154.fields.RD_LINK_ECC_EN_F1 = RdLinkEccEn(1);
		ddrc_154.fields.WR_LINK_ECC_EN_F1 = WrLinkEccEn(1);
		MemDdrcRegWrite(DENALI_CTL(154), Channel, ddrc_154.value);
		ddrc_155.value = MemDdrcRegRead(DENALI_CTL(155), Channel);
		ddrc_155.fields.RD_LINK_ECC_EN_F2 = RdLinkEccEn(2);
		ddrc_155.fields.WR_LINK_ECC_EN_F2 = WrLinkEccEn(2);
		MemDdrcRegWrite(DENALI_CTL(155), Channel, ddrc_155.value);
		/* link ECC settings of FSP 1 and 2 (CIX has no field types for these) */
		MemDdrcRegWrite(DENALI_CTL(160), Channel, WrLinkEccEn(1) ? 0x400 : 0);
		MemDdrcRegWrite(DENALI_CTL(161), Channel, WrLinkEccEn(1) ? 0x800 : 0);
		MemDdrcRegWrite(DENALI_CTL(162), Channel, WrLinkEccEn(1) ? 0xc00 : 0);
		MemDdrcRegWrite(DENALI_CTL(163), Channel, WrLinkEccEn(2) ? 0x400 : 0);
		MemDdrcRegWrite(DENALI_CTL(164), Channel, WrLinkEccEn(2) ? 0x800 : 0);
		MemDdrcRegWrite(DENALI_CTL(165), Channel, WrLinkEccEn(2) ? 0xc00 : 0);
	}
	MemAddressMapping();
	MemRefreshModeCfg();
	MemPortPriorityCfg();
	MemInlineEccCfg();
}
