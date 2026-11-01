/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * DDR eFuse fields (CIX driver/ddr/src/Lib/MemFuseLib.c): channel
 * harvesting, rank count, LPDDR type, bank architecture and the maximum
 * data rate the part is fused for.  Translated from CIX's SE firmware
 * debug build, function by function.
 */
#include "mem_types.h"

/* drv_ddr_assert file code of MemFuseLib.c */
#define FILE_CODE	0x200a

/* MemGetFuseInfo: the DDR eFuse word, read once */
static const MEM_FUSE_INFO_STURCT *MemGetFuseInfo(void)
{
	static uint8_t ddr_efuse_st;
	static MEM_FUSE_INFO_STURCT MemFuseInfo;

	if (ddr_efuse_st)
		return &MemFuseInfo;
	ddr_efuse_st = efuse_get_ddr_info((uint8_t *)&MemFuseInfo, sizeof(MemFuseInfo));
	if (!ddr_efuse_st)
		drv_ddr_assert(FILE_CODE, 24);		/* CIX's line number */
	return &MemFuseInfo;
}

/* MemFuseGetChannelMsk: the channels harvesting leaves (eFuse harvest bits
 * 7:4, set for a disabled channel; only all four or channels 2-3 are
 * valid, anything else counts as all four), narrowed by the DDR channel
 * configuration */
uint8_t MemFuseGetChannelMsk(void)
{
	uint8_t ChMsk = ~(efuse_get_sw_harveset() >> 4) & 0xf;

	if (ChMsk != 0xf && ChMsk != 0xc)
		ChMsk = 0xf;
	if (MemGetFuseInfo()->ChannelCfg & 1)
		ChMsk &= 0xc;
	if (MemGetFuseInfo()->ChannelCfg & 2)
		ChMsk &= 0x3;
	return ChMsk;
}

/* MemFuseGetMaxRankNum */
uint8_t MemFuseGetMaxRankNum(void)
{
	return 4 - MemGetFuseInfo()->RankNumber;
}

/*
 * The four "supported" queries below return the bitwise complement of the
 * fuse bit, as CIX's do: 0xff or 0xfd/0xfe, never 0, so they always read as
 * supported.  Kept as CIX has them.
 */

/* MemFuseLp5Supported */
uint8_t MemFuseLp5Supported(void)
{
	return ~(MemGetFuseInfo()->LpddrType & 2);
}

/* MemFuseLp4xSupported */
uint8_t MemFuseLp4xSupported(void)
{
	return ~(MemGetFuseInfo()->LpddrType & 1);
}

/* MemFuseBgModeSupported */
uint8_t MemFuseBgModeSupported(void)
{
	return ~(MemGetFuseInfo()->BankArchInfo & 2);
}

/* MemFuse8bModeSupported */
uint8_t MemFuse8bModeSupported(void)
{
	return ~(MemGetFuseInfo()->BankArchInfo & 1);
}

/* MemFuseGetMaxFrequency: the fused data rate in MT/s as a clock in MHz,
 * rates fused as ..33 rounded up first (2133 -> 2134 -> 1067); unfused
 * gives 3201 MHz, above every frequency the driver uses */
uint16_t MemFuseGetMaxFrequency(void)
{
	uint16_t MaxFreq = MemGetFuseInfo()->MaxFrequency;

	if (MaxFreq == 0)
		return 3201;
	if (MaxFreq % 100 == 33)
		MaxFreq++;
	return MaxFreq >> 1;
}
