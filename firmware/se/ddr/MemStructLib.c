/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The DDR driver's state (CIX driver/ddr/src/Lib/MemStructLib.c):
 * MEMORY_INIT_S, allocated on first use, filled from the memory
 * configuration (CDCB blocks), the BIOS setup options and the eFuse, and
 * the accessors the rest of the driver uses.  Translated from CIX's SE
 * firmware debug build, function by function.
 */
#include "mem_types.h"

/* drv_ddr_assert file code of MemStructLib.c; the line numbers passed with
 * it are CIX's */
#define FILE_CODE	0x2006

MEMORY_INIT_S *gMemInitPtr;

/* MemFeatBiosSetupOv: BIOS setup options over the configuration's features */
void MemFeatBiosSetupOv(void)
{
	MemSetupGetField(0x12, 1, &gMemInitPtr->Features.WckAlwaysOn);
	MemSetupGetField(0x13, 1, &gMemInitPtr->Features.DataMask);
	MemSetupGetField(0x14, 1, &gMemInitPtr->Features.RfmEn);
	MemSetupGetField(0x15, 1, &gMemInitPtr->Features.AutoPrechargeEn);
	MemSetupGetField(0x16, 1, &gMemInitPtr->Features.PbrEn);
	if (MemSetupGetField(0x17, 1, &gMemInitPtr->Features.PortPriority)) {
		MemSetupGetField(0x18, 1, &gMemInitPtr->Features.BdwP0);
		MemSetupGetField(0x19, 1, &gMemInitPtr->Features.BdwP1);
		MemSetupGetField(0x21, 1, &gMemInitPtr->Features.RPriorityP0);
		MemSetupGetField(0x22, 1, &gMemInitPtr->Features.WPriorityP0);
		MemSetupGetField(0x23, 1, &gMemInitPtr->Features.BdwOvflowP0);
		MemSetupGetField(0x24, 1, &gMemInitPtr->Features.RPriorityP1);
		MemSetupGetField(0x25, 1, &gMemInitPtr->Features.WPriorityP1);
		MemSetupGetField(0x26, 1, &gMemInitPtr->Features.BdwOvflowP1);
	}
	if (MemSetupGetField(0x1a, 1, &gMemInitPtr->Features.MbistEn))
		MemSetupGetField(0x1b, 1, &gMemInitPtr->Features.MbistMode);
	MemSetupGetField(0x1c, 1, &gMemInitPtr->Features.EyeScanEn);
	MemSetupGetField(0x1d, 1, &gMemInitPtr->Features.BpEn);
	MemSetupGetField(0x2b, 1, &gMemInitPtr->Features.IEcc);
}

/* GetMemInitPtr: the driver's state, allocated and cleared on first use */
MEMORY_INIT_S *GetMemInitPtr(void)
{
	if (gMemInitPtr == NULL) {
		gMemInitPtr = malloc(sizeof(MEMORY_INIT_S));
		if (gMemInitPtr == NULL)
			drv_ddr_assert(FILE_CODE, 90);
		memset(gMemInitPtr, 0, sizeof(MEMORY_INIT_S));
	}
	return gMemInitPtr;
}

/* MemConfigurationInit: density, width and ranks unknown (0xff) until the
 * SPD is analysed; features from the configuration's feature block (0x1002)
 * and the setup options; then the SPD, the technology's functions, the
 * AXI-to-DFI ratio for LPDDR5 and the retraining filter */
void MemConfigurationInit(void)
{
	uint32_t size;
	MEM_CONFIG_BLOCK_FEATURE *MemCfgBlkFeat;

	gMemInitPtr->Density = 0xff;
	gMemInitPtr->DeviceWidth = 0xff;
	gMemInitPtr->RanksPerChannel = 0xff;
	if (CdcbLocateBlockBuf(0x1002, &size, (uint8_t **)&MemCfgBlkFeat) == 0) {
		memcpy(&gMemInitPtr->Features, &MemCfgBlkFeat->Feat, sizeof(MEM_CONFIG_FEATURE));
		CdcbDeallocateBlockBuf((uint8_t **)&MemCfgBlkFeat);
	}
	MemFeatBiosSetupOv();
	MemBpCheckpoint(0);
	MemSpdAnalyze();
	MemTechFuncInit(GetMemInitPtr()->DdrType);
	if (gMemInitPtr->DdrType == 1)
		gMemInitPtr->Axi2Dfi = 0x43;
	gMemInitPtr->RetrainFilter = gMemInitPtr->Features.PeriodicTrain;
}

/* GetMemFspCfgPtr */
MEM_FSP_CONFIG_S *GetMemFspCfgPtr(uint8_t fsp)
{
	if (fsp > 2)
		drv_ddr_assert(FILE_CODE, 102);
	return &gMemInitPtr->GlobalFspConfig[fsp];
}

/* GetMemFeatPtr */
MEM_FEATURE_S *GetMemFeatPtr(void)
{
	return &gMemInitPtr->Features;
}

/* GetMemChannelPtr */
MEM_CHANNEL_S *GetMemChannelPtr(uint8_t Channel)
{
	if (Channel > 3)
		drv_ddr_assert(FILE_CODE, 119);
	return &gMemInitPtr->Channel[Channel];
}

/* GetMemTimingsPtr */
LPDDR_TIMING_S *GetMemTimingsPtr(uint8_t Channel, uint8_t fsp)
{
	if (Channel > 3)
		drv_ddr_assert(FILE_CODE, 129);
	if (fsp > 2)
		drv_ddr_assert(FILE_CODE, 130);
	return &gMemInitPtr->Channel[Channel].Timings[fsp];
}

/* GetMemSpdTimingsPtr */
MEM_SPD_TIMING_S *GetMemSpdTimingsPtr(void)
{
	return &gMemInitPtr->SpdTimings;
}

/* GetBusCfgPtr */
MEM_BUSCFG_ENTRY_S *GetBusCfgPtr(uint8_t fsp)
{
	if (fsp > 2)
		drv_ddr_assert(FILE_CODE, 147);
	return &gMemInitPtr->GlobalFspConfig[fsp].BusCfg;
}

/* GetPhyPadCfgPtr */
MEM_PHY_PAD_CFG_ENTRY_S *GetPhyPadCfgPtr(uint8_t fsp)
{
	if (fsp > 2)
		drv_ddr_assert(FILE_CODE, 156);
	return &gMemInitPtr->GlobalFspConfig[fsp].PhyPadCfg;
}

/* MemPlatformLookupPhyPadConfig (inlined in CIX's build): for each FSP, the
 * first entry of the configuration's PHY pad block (0x1006) that covers its
 * frequency, the rank count and the device width; one FFE value serves
 * every transmitter, the ODT values are 4 bits */
static inline void MemPlatformLookupPhyPadConfig(void)
{
	MEM_PHY_PAD_CFG_ENTRY_S *PhyPadCfg;
	MEMORY_INIT_S *MemInitPtr = GetMemInitPtr();
	MEM_CONFIG_PHYPADCFG *MemConfigPhyPadCfg;
	MEM_CONFIG_PHYPADCFG_ENTRY *e;
	uint16_t MemFreq;
	uint8_t RankNum = MemInitPtr->RanksPerChannel;
	uint32_t index, EntryCount, size;
	uint8_t fsp;

	if (CdcbLocateBlockBuf(0x1006, &size, (uint8_t **)&MemConfigPhyPadCfg))
		drv_ddr_assert(FILE_CODE, 180);
	EntryCount = (MemConfigPhyPadCfg->Header.BlockSize - sizeof(MEM_CONFIG_BLOCK_HEADER)) /
		     sizeof(MEM_CONFIG_PHYPADCFG_ENTRY);
	for (fsp = 0; fsp < 3; fsp++) {
		MemFreq = GetMemFspCfgPtr(fsp)->MemFreq;
		PhyPadCfg = GetPhyPadCfgPtr(fsp);
		for (index = 0; index < EntryCount; index++) {
			e = &MemConfigPhyPadCfg->PhyPadCfg[index];
			if (e->MaxMemFreq == 0)
				drv_ddr_assert(FILE_CODE, 188);
			if (MemFreq <= e->MaxMemFreq && (e->RankPerCh & (1 << (RankNum - 1))) &&
			    (MemInitPtr->DeviceWidth & e->DevWidth)) {
				PhyPadCfg->FdbkDrv = e->FdbkDrv;
				PhyPadCfg->FdbkOdt = e->FdbkOdt & 0xf;
				PhyPadCfg->RstDrv = e->RstDrv;
				PhyPadCfg->RstTxFFE = e->FFE;
				PhyPadCfg->DqDrv = e->DqDrv;
				PhyPadCfg->DqOdt = e->DqOdt & 0xf;
				PhyPadCfg->DqTxFFE = e->FFE;
				PhyPadCfg->DqRxDFE = e->DFE;
				PhyPadCfg->DqsDrv = e->DqsDrv;
				PhyPadCfg->DqsOdt = e->DqsOdt & 0xf;
				PhyPadCfg->DqsTxFFE = e->FFE;
				PhyPadCfg->CkDrv = e->CkDrv;
				PhyPadCfg->CkTxFFE = e->FFE;
				PhyPadCfg->CkeDrv = e->CkeDrv;
				PhyPadCfg->CkeTxFFE = e->FFE;
				PhyPadCfg->CaDrv = e->CaDrv;
				PhyPadCfg->CaTxFFE = e->FFE;
				PhyPadCfg->CsDrv = e->CsDrv;
				PhyPadCfg->CsTxFFE = e->FFE;
				PhyPadCfg->CTLE = e->CTLE;
				break;
			}
		}
	}
}

/*
 * MemDramFunctionalInit: channels (configuration block 0x1001, less what
 * the eFuse harvests and the setup option masks), capacity, the three FSPs'
 * frequencies and ratios, bank architecture, then the PHY pad and bus
 * configuration lookups.  CIX's code reads the configuration block even if
 * it was not found; kept.
 */
void MemDramFunctionalInit(void)
{
	MEM_CONFIG_BLOCK_CONFIG *MemCfgBlkCfg;
	uint8_t ChMskOv;
	uint16_t MaxFreq;
	uint32_t size;
	uint8_t Channel, mask, fsp;

	CdcbLocateBlockBuf(0x1001, &size, (uint8_t **)&MemCfgBlkCfg);
	gMemInitPtr->ChannelMask = MemCfgBlkCfg->ChMask;
	gMemInitPtr->ChannelMask &= MemFuseGetChannelMsk();
	if (MemSetupGetField(0x1e, 1, &ChMskOv))
		gMemInitPtr->ChannelMask &= ChMskOv;
	if (gMemInitPtr->RanksPerChannel > MemFuseGetMaxRankNum())
		drv_ddr_assert(FILE_CODE, 262);
	gMemInitPtr->ChannelCount = 0;
	for (mask = gMemInitPtr->ChannelMask; mask; mask &= mask - 1)
		gMemInitPtr->ChannelCount++;
	/* in MiB: dies per rank x density (Gb) / 8 x 1024 */
	gMemInitPtr->MemorySize = gMemInitPtr->ChannelCount * gMemInitPtr->RanksPerChannel *
				  gMemInitPtr->Density * (32 / gMemInitPtr->DeviceWidth) * 128;
	if (IEccEn())
		gMemInitPtr->MemorySize = gMemInitPtr->MemorySize / 8 * 7;

	MaxFreq = MemGetBootEnv() ? MemCfgBlkCfg->MaxFreq : 3200;
	MemSetupGetField(0x10, 2, &MaxFreq);
	if (MemFuseGetMaxFrequency() < MaxFreq)
		MaxFreq = MemFuseGetMaxFrequency();
	if (gMemInitPtr->DramPkgDetect) {
		MaxFreq = 533;
		gMemInitPtr->DfsRatio = 1;
	} else {
		gMemInitPtr->DfsRatio = MaxFreq < 1200 ? 1 : 4;
	}

	for (fsp = 0; fsp < 3; fsp++) {
		if (gMemInitPtr->DdrType == 1)
			gMemInitPtr->GlobalFspConfig[fsp].FreqRatio = fsp == 0 ? 2 : 4;
		else if (gMemInitPtr->DdrType == 0)
			gMemInitPtr->GlobalFspConfig[fsp].FreqRatio = 2;
		if (fsp == 0)
			gMemInitPtr->GlobalFspConfig[0].MemFreq = gMemInitPtr->GlobalFspConfig[0].FreqRatio * 50;
		else if (fsp == 1)
			gMemInitPtr->GlobalFspConfig[1].MemFreq = MaxFreq / gMemInitPtr->DfsRatio;
		else
			gMemInitPtr->GlobalFspConfig[fsp].MemFreq = MaxFreq;
		gMemInitPtr->TrainLvlType = 0x105d;
		if (gMemInitPtr->GlobalFspConfig[2].MemFreq >= 1200)
			gMemInitPtr->TrainLvlType = 0x105f;
		if (BankArch8BMode())
			gMemInitPtr->GlobalFspConfig[fsp].BankArchMode = 0;
		else
			gMemInitPtr->GlobalFspConfig[fsp].BankArchMode =
				gMemInitPtr->GlobalFspConfig[fsp].MemFreq > 1600 ? 2 : 1;
	}
	for (Channel = 0; Channel < 4; Channel++)
		gMemInitPtr->Channel[Channel].Enabled = (gMemInitPtr->ChannelMask >> Channel) & 1;
	CdcbDeallocateBlockBuf((uint8_t **)&MemCfgBlkCfg);

	MemPlatformLookupPhyPadConfig();
	MemGetTechFuncPtr()->MemPlatformLookupBusConfig();
}

/* MemStructInit */
void MemStructInit(void)
{
	if (gMemInitPtr == NULL) {
		gMemInitPtr = malloc(sizeof(MEMORY_INIT_S));
		if (gMemInitPtr == NULL)
			drv_ddr_assert(FILE_CODE, 366);
		memset(gMemInitPtr, 0, sizeof(MEMORY_INIT_S));
	}
	MemConfigurationInit();
}
