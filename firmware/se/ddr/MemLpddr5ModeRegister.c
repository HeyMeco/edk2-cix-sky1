/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * LPDDR5 mode register values (CIX driver/ddr/src/Tech/lp5/
 * MemLpddr5ModeRegister.c), from the FSP configuration, the bus
 * configuration (ODT, Vref, PDDS) and the feature switches; the callback
 * table MemModeRegisters.c calls through.  Translated from CIX's SE
 * firmware debug build, function by function.
 *
 * The per-rank fields of MEM_BUSCFG_ENTRY_S hold one 4-bit value per rank
 * (rank r in bits 4r+3..4r).
 */
#include "mem_types.h"

/* MemLpddr5CalcMR0Val */
uint8_t MemLpddr5CalcMR0Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp)
{
	return 0;
}

/* MemLpddr5CalcMR4Val */
uint8_t MemLpddr5CalcMR4Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp)
{
	return 0;
}

/* MemLpddr5CalcMR8Val */
uint8_t MemLpddr5CalcMR8Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp)
{
	return 0;
}

/* MemLpddr5CalcMR16Val */
uint8_t MemLpddr5CalcMR16Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp)
{
	return 0;
}

/* MemLpddr5CalcMR20Val: WCK mode 0, RDQS 2 */
uint8_t MemLpddr5CalcMR20Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp)
{
	return 0x02;
}

/* MemLpddr5CalcMR23Val */
uint8_t MemLpddr5CalcMR23Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp)
{
	return 0;
}

/* MemLpddr5CalcMR25Val */
uint8_t MemLpddr5CalcMR25Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp)
{
	return 0;
}

/* MemLpddr5CalcMR28Val: ZQ mode 1 */
uint8_t MemLpddr5CalcMR28Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp)
{
	return 0x20;
}

/* MemLpddr5CalcMR30Val */
uint8_t MemLpddr5CalcMR30Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp)
{
	return 0;
}

/* MemLpddr5CalcMR31Val: DQ calibration pattern A */
uint8_t MemLpddr5CalcMR31Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp)
{
	return 0x55;
}

/* MemLpddr5CalcMR32Val: DQ calibration pattern B */
uint8_t MemLpddr5CalcMR32Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp)
{
	return 0x55;
}

/* MemLpddr5CalcMR33Val */
uint8_t MemLpddr5CalcMR33Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp)
{
	return 0x5a;
}

/* MemLpddr5CalcMR34Val */
uint8_t MemLpddr5CalcMR34Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp)
{
	return 0x3c;
}

/* MemLpddr5CalcMR35Val */
uint8_t MemLpddr5CalcMR35Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp)
{
	return 0;
}

/* MemLpddr5CalcMR37Val */
uint8_t MemLpddr5CalcMR37Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp)
{
	return 0;
}

/* MemLpddr5CalcMR1Val: write latency set by frequency band (upper limits
 * in MHz), 0 above the last */
uint8_t MemLpddr5CalcMR1Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp)
{
	uint32_t WlFreqEncoding[] = { 267, 533, 800, 1067, 1375, 1600, 1867, 2133, 2400, 2750, 3000, 3200 };
	MR1_DEF Mr1;
	uint32_t i;

	Mr1.AsByte = 0;
	for (i = 0; i < 12; i++) {
		if (GetMemFspCfgPtr(fsp)->MemFreq <= WlFreqEncoding[i]) {
			Mr1.mr.WL = i;
			break;
		}
	}
	return Mr1.AsByte;
}

/* MemLpddr5CalcMR2Val: read latency and nWR by the same frequency bands */
uint8_t MemLpddr5CalcMR2Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp)
{
	uint32_t RlWrFreqEncoding[] = { 267, 533, 800, 1067, 1375, 1600, 1867, 2133, 2400, 2750, 3000, 3200 };
	MR2_DEF Mr2;
	uint32_t i;

	Mr2.AsByte = 0;
	for (i = 0; i < 12; i++) {
		if (GetMemFspCfgPtr(fsp)->MemFreq <= RlWrFreqEncoding[i]) {
			Mr2.mr.RLandRbtp = i;
			Mr2.mr.WR = i;
			break;
		}
	}
	return Mr2.AsByte;
}

/* MemLpddr5CalcMR11Val: DQ and CA ODT, non-target ODT on when set */
uint8_t MemLpddr5CalcMR11Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp)
{
	MR11_DEF Mr11;

	Mr11.AsByte = 0;
	Mr11.mr.DQ_ODT = GetBusCfgPtr(fsp)->DQ_ODT >> (Rank * 4);
	Mr11.mr.NtOdtEnable = ((GetBusCfgPtr(fsp)->NTDQ_ODT >> (Rank * 4)) & 0xf) != 0;
	Mr11.mr.CA_ODT = GetBusCfgPtr(fsp)->CA_ODT >> (Rank * 4);
	return Mr11.AsByte;
}

/* MemLpddr5CalcMR12Val: CA Vref, in 0.5% steps above 10% (Vref in 0.1%) */
uint8_t MemLpddr5CalcMR12Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp)
{
	MR12_DEF Mr12;

	Mr12.AsByte = 0;
	if (GetBusCfgPtr(fsp)->CaVref > 100)
		Mr12.mr.VrefCA = (GetBusCfgPtr(fsp)->CaVref - 100) / 5;
	return Mr12.AsByte;
}

/* MemLpddr5CalcMR14Val: lower byte DQ Vref */
uint8_t MemLpddr5CalcMR14Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp)
{
	MR14_DEF Mr14;

	Mr14.AsByte = 0;
	if (GetBusCfgPtr(fsp)->DqVref > 100)
		Mr14.mr.VrefDQ = (GetBusCfgPtr(fsp)->DqVref - 100) / 5;
	return Mr14.AsByte;
}

/* MemLpddr5CalcMR15Val: upper byte DQ Vref (all 8 bits) */
uint8_t MemLpddr5CalcMR15Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp)
{
	MR15_DEF Mr15;

	Mr15.AsByte = 0;
	if (GetBusCfgPtr(fsp)->DqVref > 100)
		Mr15.mr.VrefDqUpper = (GetBusCfgPtr(fsp)->DqVref - 100) / 5;
	return Mr15.AsByte;
}

/* MemLpddr5CalcMR17Val: SoC ODT; CK, CS and CA ODT disabled where 0 */
uint8_t MemLpddr5CalcMR17Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp)
{
	MR17_DEF Mr17;

	Mr17.AsByte = 0;
	Mr17.mr.SocOdt = GetBusCfgPtr(fsp)->SOC_ODT >> (Rank * 4);
	Mr17.mr.OdtCa = ((GetBusCfgPtr(fsp)->CA_ODT >> (Rank * 4)) & 0xf) == 0;
	Mr17.mr.OdtCk = ((GetBusCfgPtr(fsp)->CK_ODT >> (Rank * 4)) & 0xf) == 0;
	Mr17.mr.OdtCs = ((GetBusCfgPtr(fsp)->CS_ODT >> (Rank * 4)) & 0xf) == 0;
	return Mr17.AsByte;
}

/* MemLpddr5CalcMR19Val: CS ODT code 3 - value */
uint8_t MemLpddr5CalcMR19Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp)
{
	MR19_DEF Mr19;

	Mr19.AsByte = 0;
	Mr19.mr.CsOdt = 3 - ((GetBusCfgPtr(fsp)->CS_ODT >> (Rank * 4)) & 0xf);
	return Mr19.AsByte;
}

/* MemLpddr5CalcMR41Val: non-target DQ ODT */
uint8_t MemLpddr5CalcMR41Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp)
{
	MR41_DEF Mr41;

	Mr41.AsByte = 0;
	Mr41.mr.NtDqOdt = GetBusCfgPtr(fsp)->NTDQ_ODT >> (Rank * 4);
	return Mr41.AsByte;
}

/* MemLpddr5CalcMR3Val: pull-down drive (6 when unset), bank organisation
 * (8 banks: 1, bank groups: 2), DBI and the write latency set */
uint8_t MemLpddr5CalcMR3Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp)
{
	MR3_DEF Mr3;

	Mr3.AsByte = 0;
	Mr3.mr.PDDS = (GetBusCfgPtr(fsp)->ODT_PDDS >> (Rank * 4)) & 7;
	if (Mr3.mr.PDDS == 0)
		Mr3.mr.PDDS = 6;
	if (GetMemFspCfgPtr(fsp)->BankArchMode == 0)
		Mr3.mr.BgOrg = 1;
	else if (GetMemFspCfgPtr(fsp)->BankArchMode == 1)
		Mr3.mr.BgOrg = 2;
	Mr3.mr.DBI_RD = ReadDbiEn();
	Mr3.mr.DBI_WR = WriteDbiEn();
	Mr3.mr.WLS = GetBusCfgPtr(fsp)->WLSet;
	return Mr3.AsByte;
}

/* MemLpddr5CalcMR10Val: WCK postamble, RDQS preamble and postamble */
uint8_t MemLpddr5CalcMR10Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp)
{
	MR10_DEF Mr10;

	Mr10.AsByte = 0;
	Mr10.mr.WckPostambleLength = WrPostambleEn();
	Mr10.mr.RdqsPreambleLength = RdPreambleLen();
	Mr10.mr.RdqsPostambleLength = RdPostambleEn();
	return Mr10.AsByte;
}

/* MemLpddr5CalcMR13Val: data mask disabled unless enabled; bit 6 (the
 * CBT field of CIX's MR13_DEF) always set, as CIX does */
uint8_t MemLpddr5CalcMR13Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp)
{
	MR13_DEF Mr13;

	Mr13.AsByte = 0;
	Mr13.mr.DMD = DataMaskEn() == 0;
	Mr13.mr.CBT = 1;
	return Mr13.AsByte;
}

/* MemLpddr5CalcMR18Val: WCK ODT, WCK frequency mode (from 1600 MHz), WCK
 * always on, WCK:CK ratio (2:1 unless the FSP runs at 4:1) */
uint8_t MemLpddr5CalcMR18Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp)
{
	MR18_DEF Mr18;

	Mr18.AsByte = 0;
	Mr18.mr.WckFM = GetMemFspCfgPtr(fsp)->MemFreq >= 1600;
	Mr18.mr.WckOdt = GetBusCfgPtr(fsp)->WCK_ODT >> (Rank * 4);
	Mr18.mr.WckRatio = GetMemFspCfgPtr(fsp)->FreqRatio != 4;
	Mr18.mr.WckON = WckAlwaysOn() != 0;
	return Mr18.AsByte;
}

/* MemLpddr5CalcMR22Val: read and write link ECC */
uint8_t MemLpddr5CalcMR22Val(uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp)
{
	MR22_DEF Mr22;

	Mr22.AsByte = 0;
	Mr22.mr.RECC = RdLinkEccEn(fsp) != 0;
	Mr22.mr.WECC = WrLinkEccEn(fsp) != 0;
	return Mr22.AsByte;
}

/* MemLpddr5GetRfmMrCfg: refresh management from the DRAM's MR27 */
MR_RFM_CFG MemLpddr5GetRfmMrCfg(uint8_t Channel)
{
	MR_RFM_CFG RfmCfg;
	MR27_DEF Mr27;

	Mr27.AsByte = MemModeRegRead(Channel, 0, 0, 27);
	RfmCfg.RfmEn = Mr27.mr.RFM;
	RfmCfg.RAAIMT = Mr27.mr.RAAIMT * 8;
	RfmCfg.RAAMULT = (Mr27.mr.RAAMULT + 1) * 2;
	return RfmCfg;
}

/*
 * MemLpddr5ModeRegisterOverride: outside the boot environment, program
 * FSP 1's write and read latency and WCK settings with FSP 1 as the write
 * target (MR16 = 1), switch the operating point to it with VRCG on
 * (MR16 = 5, then 0x45; MR14/MR15 Vref 0x3a/0x40), then MR3, and clear
 * bit 0 of DDRC register 0x540 on every populated channel.
 */
void MemLpddr5ModeRegisterOverride(void)
{
	uint8_t Channel;

	if (MemGetBootEnv())
		return;
	for (Channel = 0; Channel < 4; Channel++) {
		if (!MemChannelPopulated(Channel))
			continue;
		MemModeRegWrite(Channel, 0xff, 16, 0x01);
		MemModeRegWrite(Channel, 0xff, 1, MemLpddr5CalcMR1Val(Channel, 0, 0, 1));
		MemModeRegWrite(Channel, 0xff, 2, MemLpddr5CalcMR2Val(Channel, 0, 0, 1));
		MemModeRegWrite(Channel, 0xff, 18, MemLpddr5CalcMR18Val(Channel, 0, 0, 1));
		MemModeRegWrite(Channel, 0xff, 14, 0x3a);
		MemModeRegWrite(Channel, 0xff, 15, 0x40);
		MemModeRegWrite(Channel, 0xff, 16, 0x45);
	}
	udelay(1);
	for (Channel = 0; Channel < 4; Channel++) {
		if (MemChannelPopulated(Channel))
			MemModeRegWrite(Channel, 0xff, 16, 0x05);
	}
	udelay(1);
	for (Channel = 0; Channel < 4; Channel++) {
		if (MemChannelPopulated(Channel))
			MemModeRegWrite(Channel, 0xff, 3, MemLpddr5CalcMR3Val(Channel, 0, 0, 1));
	}
	for (Channel = 0; Channel < 4; Channel++) {
		if (MemChannelPopulated(Channel))
			MemDdrcRegWrite(0x540, Channel, MemDdrcRegRead(0x540, Channel) & ~1u);
	}
	udelay(1);
}

MEM_MODE_REGISTER_CALLBACK MemLpddr5ModeRegisterCallback = {
	.CalcMR0Val = MemLpddr5CalcMR0Val,
	.CalcMR1Val = MemLpddr5CalcMR1Val,
	.CalcMR2Val = MemLpddr5CalcMR2Val,
	.CalcMR3Val = MemLpddr5CalcMR3Val,
	.CalcMR4Val = MemLpddr5CalcMR4Val,
	.CalcMR8Val = MemLpddr5CalcMR8Val,
	.CalcMR10Val = MemLpddr5CalcMR10Val,
	.CalcMR11Val = MemLpddr5CalcMR11Val,
	.CalcMR12Val = MemLpddr5CalcMR12Val,
	.CalcMR13Val = MemLpddr5CalcMR13Val,
	.CalcMR14Val = MemLpddr5CalcMR14Val,
	.CalcMR15Val = MemLpddr5CalcMR15Val,
	.CalcMR16Val = MemLpddr5CalcMR16Val,
	.CalcMR17Val = MemLpddr5CalcMR17Val,
	.CalcMR18Val = MemLpddr5CalcMR18Val,
	.CalcMR19Val = MemLpddr5CalcMR19Val,
	.CalcMR20Val = MemLpddr5CalcMR20Val,
	.CalcMR22Val = MemLpddr5CalcMR22Val,
	.CalcMR23Val = MemLpddr5CalcMR23Val,
	.CalcMR25Val = MemLpddr5CalcMR25Val,
	.CalcMR28Val = MemLpddr5CalcMR28Val,
	.CalcMR30Val = MemLpddr5CalcMR30Val,
	.CalcMR31Val = MemLpddr5CalcMR31Val,
	.CalcMR32Val = MemLpddr5CalcMR32Val,
	.CalcMR33Val = MemLpddr5CalcMR33Val,
	.CalcMR34Val = MemLpddr5CalcMR34Val,
	.CalcMR35Val = MemLpddr5CalcMR35Val,
	.CalcMR37Val = MemLpddr5CalcMR37Val,
	.CalcMR40Val = NULL,
	.CalcMR41Val = MemLpddr5CalcMR41Val,
	.GetRfmMrCfg = MemLpddr5GetRfmMrCfg,
	.ModeRegisterOverride = MemLpddr5ModeRegisterOverride,
};
