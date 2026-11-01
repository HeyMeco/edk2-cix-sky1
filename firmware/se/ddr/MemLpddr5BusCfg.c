/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * LPDDR5 bus configuration (CIX driver/ddr/src/Tech/lp5/MemLpddr5BusCfg.c):
 * DRAM ODT, drive and Vref per FSP from the memory configuration's bus
 * block (0x1005).  Translated from CIX's SE firmware debug build.
 */
#include "mem_types.h"
#include "mem_regs.h"

/* drv_ddr_assert file code of MemLpddr5BusCfg.c; the line numbers passed
 * with it are CIX's */
#define FILE_CODE	0x4201

/*
 * MemPlatformLookupBusConfigLp5: for each FSP, the first entry covering its
 * frequency, the rank count and the device width.  A Vref the entry leaves
 * at 0 is computed from the termination and drive strengths (codes are
 * 240 ohm / n): the divider's ratio in percent, times 5 (0.1% steps of
 * MR12/MR14).  Then the latency sets: write set 0, read set 1 for byte
 * mode, plus one with read DBI.
 */
void MemPlatformLookupBusConfigLp5(void)
{
	MEMORY_INIT_S *MemInitPtr = GetMemInitPtr();
	MEM_CONFIG_BUSCFG_LP5 *MemConfigBusCfgLp5;
	MEM_CONFIG_BUSCFG_LP5_ENTRY *e;
	MEM_BUSCFG_ENTRY_S *BusCfg;
	uint32_t index, EntryCount, size;
	uint8_t fsp, dram_odt, pad_drv, denominator, RdDbi;

	if (CdcbLocateBlockBuf(0x1005, &size, (uint8_t **)&MemConfigBusCfgLp5))
		drv_ddr_assert(FILE_CODE, 31);
	EntryCount = (MemConfigBusCfgLp5->Header.BlockSize - sizeof(MEM_CONFIG_BLOCK_HEADER)) /
		     sizeof(MEM_CONFIG_BUSCFG_LP5_ENTRY);
	for (fsp = 0; fsp < 3; fsp++) {
		BusCfg = GetBusCfgPtr(fsp);
		for (index = 0; index < EntryCount; index++) {
			e = &MemConfigBusCfgLp5->BusCfg[index];
			if (e->MaxMemFreq == 0) {
				drv_ddr_assert(FILE_CODE, 40);
				break;
			}
			if (GetMemFspCfgPtr(fsp)->MemFreq > e->MaxMemFreq ||
			    !((e->RankPerCh >> (MemInitPtr->RanksPerChannel - 1)) & 1) ||
			    !(MemInitPtr->DeviceWidth & e->DevWidth))
				continue;
			BusCfg->CA_ODT = e->CA_ODT;
			BusCfg->CK_ODT = e->CK_ODT;
			BusCfg->CS_ODT = e->CS_ODT;
			BusCfg->DQ_ODT = e->DQ_ODT;
			BusCfg->NTDQ_ODT = e->NTDQ_ODT;
			BusCfg->WCK_ODT = e->WCK_ODT;
			BusCfg->SOC_ODT = e->SOC_ODT;
			BusCfg->ODT_PDDS = e->ODT_PDDS;
			BusCfg->DqVref = e->DqVref;
			BusCfg->CaVref = e->CaVref;
			/* CA termination of the dies on the bus (4 in byte mode,
			 * else 2), with two ranks both ranks' */
			denominator = (ByteModeEn() ? 4 : 2) * (e->CA_ODT & 0xf);
			if (GetMemInitPtr()->RanksPerChannel > 1)
				denominator += (ByteModeEn() ? 4 : 2) * (BusCfg->CA_ODT >> 4);
			if (BusCfg->CaVref == 0) {
				dram_odt = 240 / denominator;
				pad_drv = 240 / (GetPhyPadCfgPtr(fsp)->CaDrv & 0xf);
				BusCfg->CaVref = dram_odt * 100 / (dram_odt + pad_drv) * 5;
			}
			if (BusCfg->DqVref == 0) {
				dram_odt = 240 / ((BusCfg->DQ_ODT & 0xf) + (BusCfg->NTDQ_ODT & 0xf));
				pad_drv = 240 / (GetPhyPadCfgPtr(fsp)->DqDrv & 0xf);
				BusCfg->DqVref = dram_odt * 100 / (dram_odt + pad_drv) * 5;
			}
			break;
		}
		BusCfg->WLSet = 0;
		RdDbi = ReadDbiEn();
		BusCfg->RLSet = ByteModeEn() != 0;
		if (RdDbi)
			BusCfg->RLSet++;
	}
	CdcbDeallocateBlockBuf((uint8_t **)&MemConfigBusCfgLp5);
}
