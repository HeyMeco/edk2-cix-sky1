/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * DRAM organisation (CIX driver/ddr/src/Tech/MemLpddrSpd.c): there is no
 * SPD on the boards, so the memory configuration's global block (0x1001)
 * gives type, density, width and ranks, and the JEDEC tables the rest.
 * Values the configuration leaves at 0xff are detected later
 * (DramPkgDetect) and written back into the configuration once known.
 * Translated from CIX's SE firmware debug build, function by function.
 */
#include "mem_types.h"
#include "mem_regs.h"

/* drv_ddr_assert file code of MemLpddrSpd.c; the line numbers passed with
 * it are CIX's */
#define FILE_CODE	0x4002

/* LPDDR5 refresh parameters by die density (Gb): row bits, tRFCab, tRFCpb,
 * tPBR2PBR (ns) */
DRAM_REF_PARAM Lp5RefParamTbl[] = {
	{ .Density = 2, .RowBits = 13, .TrfcAb = 130, .TrfcPb = 60, .Tpbr2pbr = 60 },
	{ .Density = 4, .RowBits = 14, .TrfcAb = 180, .TrfcPb = 90, .Tpbr2pbr = 90 },
	{ .Density = 8, .RowBits = 15, .TrfcAb = 210, .TrfcPb = 120, .Tpbr2pbr = 90 },
	{ .Density = 16, .RowBits = 16, .TrfcAb = 280, .TrfcPb = 140, .Tpbr2pbr = 90 },
	{ .Density = 32, .RowBits = 17, .TrfcAb = 380, .TrfcPb = 190, .Tpbr2pbr = 90 },
};

/* MemSpdCrcCheck: CRC-16 (polynomial 0x1021) of SPD bytes 0-125 against
 * bytes 126-127 */
bool MemSpdCrcCheck(uint8_t *SpdBuf)
{
	uint16_t Crc = 0;
	int16_t i = 0, j, Count = 126;

	while (--Count >= 0) {
		Crc ^= (uint16_t)SpdBuf[i++] << 8;
		for (j = 0; j < 8; j++) {
			if (Crc & 0x8000)
				Crc = Crc << 1 ^ 0x1021;
			else
				Crc = Crc << 1;
		}
	}
	return *(uint16_t *)&SpdBuf[126] == Crc;
}

/* GetMediumTimeBase: 125 ps, the only medium time base defined */
int8_t GetMediumTimeBase(LPDDR_SPD_STRUCT *SpdBuffer)
{
	int8_t MtbPs = 0;

	if (SpdBuffer->Base.Timebases.Field.Mtb == 0)
		MtbPs = 125;
	return MtbPs;
}

/* GetFineTimeBase: 1 ps, the only fine time base defined */
int8_t GetFineTimeBase(LPDDR_SPD_STRUCT *SpdBuffer)
{
	int8_t FtbPs = 0;

	if (SpdBuffer->Base.Timebases.Field.Ftb == 0)
		FtbPs = 1;
	return FtbPs;
}

/* MemSpdDecodeDensity: SPD byte 4 density code to Gb per die */
uint8_t MemSpdDecodeDensity(uint8_t SpdDensity)
{
	uint8_t SpdDensityTable[] = { 0, 0, 1, 2, 4, 8, 16, 32, 12, 24, 3, 6, 18 };
	uint8_t Density = 0;

	if (SpdDensity < sizeof(SpdDensityTable))
		Density = SpdDensityTable[SpdDensity];
	return Density;
}

/* MemSpdAnalyze */
uint32_t MemSpdAnalyze(void)
{
	MEMORY_INIT_S *MemInitPtr = GetMemInitPtr();
	MEM_SPD_TIMING_S *SpdTimings = GetMemSpdTimingsPtr();
	MEM_CONFIG_BLOCK_CONFIG *MemGlobalConfig;
	DRAM_REF_PARAM *DramRefParamTbl;
	uint8_t index, found = 0, TblSize, updated = 0;
	uint32_t size;

	MemInitPtr->DramPkgDetect = 0;
	if (CdcbLocateBlockBuf(0x1001, &size, (uint8_t **)&MemGlobalConfig)) {
		drv_ddr_assert(FILE_CODE, 411);
		return 0xffffffff;
	}
	MemInitPtr->DdrType = MemGlobalConfig->DdrType;
	if (MemInitPtr->Density == 0xff && (MemInitPtr->Density = MemGlobalConfig->DeviceDensity) == 0xff) {
		MemInitPtr->DramPkgDetect |= 1;
		MemInitPtr->Density = 2;
	} else {
		MemInitPtr->DramPkgDetect &= ~1;
	}
	if (MemInitPtr->DeviceWidth == 0xff && (MemInitPtr->DeviceWidth = MemGlobalConfig->DeviceWidth) == 0xff) {
		MemInitPtr->DramPkgDetect |= 4;
		MemInitPtr->DeviceWidth = 8;
	} else {
		MemInitPtr->DramPkgDetect &= ~4;
	}
	if (MemInitPtr->RanksPerChannel == 0xff &&
	    (MemInitPtr->RanksPerChannel = MemGlobalConfig->RankNum) == 0xff) {
		MemInitPtr->DramPkgDetect |= 2;
		MemInitPtr->RanksPerChannel = 2;
	} else {
		MemInitPtr->DramPkgDetect &= ~2;
	}
	if (MemInitPtr->DramPkgDetect) {
		/* to be detected: the log stays quiet meanwhile */
		SaveMemLogFilter();
		SetMemLogFilter(0);
	} else {
		RestoreMemLogFilter();
		if (MemGlobalConfig->DeviceDensity == 0xff) {
			MemGlobalConfig->DeviceDensity = MemInitPtr->Density;
			updated = 1;
		}
		if (MemGlobalConfig->DeviceWidth == 0xff) {
			MemGlobalConfig->DeviceWidth = MemInitPtr->DeviceWidth;
			updated = 1;
		}
		if (MemGlobalConfig->RankNum == 0xff) {
			MemGlobalConfig->RankNum = MemInitPtr->RanksPerChannel;
			updated = 1;
		}
		if (updated)
			CdcbUpdateBlockBuf((uint8_t *)MemGlobalConfig);
	}

	MemInitPtr->BusWidth = 32;
	SpdTimings->Trcdrd = 18000;
	SpdTimings->Trcdwr = 18000;
	SpdTimings->TrpAb = 21000;
	SpdTimings->TrpPb = 18000;
	SpdTimings->Tzqcal = 1500000;
	if (MemInitPtr->DdrType != MEM_LPDDR5) {
		drv_ddr_assert(FILE_CODE, 379);
		return 0xffffffff;
	}
	MemInitPtr->BaBits = 2;
	MemInitPtr->BgBits = 2;
	MemInitPtr->ColBits = 10;
	DramRefParamTbl = Lp5RefParamTbl;
	TblSize = sizeof(Lp5RefParamTbl) / sizeof(Lp5RefParamTbl[0]);
	for (index = 0; index < TblSize; index++) {
		if (MemInitPtr->Density <= DramRefParamTbl[index].Density) {
			found = 1;
			break;
		}
	}
	if (!found) {
		drv_ddr_assert(FILE_CODE, 395);
		return 0xffffffff;
	}
	MemInitPtr->RowBits = DramRefParamTbl[index].RowBits;
	SpdTimings->TrfcAb = DramRefParamTbl[index].TrfcAb * 1000;
	SpdTimings->TrfcPb = DramRefParamTbl[index].TrfcPb * 1000;
	SpdTimings->Tpbr2pbr = DramRefParamTbl[index].Tpbr2pbr * 1000;
	if (MemInitPtr->DeviceWidth == 8) {
		MemInitPtr->RowBits++;
		MemInitPtr->ByteMode = 1;
	} else {
		MemInitPtr->ByteMode = 0;
		if (MemInitPtr->DeviceWidth != 16) {
			drv_ddr_assert(FILE_CODE, 407);
			return 0xffffffff;
		}
	}
	if ((MemInitPtr->DdrType == MEM_LPDDR4X && !MemFuseLp4xSupported()) ||
	    (MemInitPtr->DdrType == MEM_LPDDR5 && !MemFuseLp5Supported()))
		drv_ddr_assert(FILE_CODE, 418);
	return 0;
}
