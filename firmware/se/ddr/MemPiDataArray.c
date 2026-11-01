/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The PI's data arrays (CIX driver/ddr/src/phy/MemPiDataArray.c): the
 * mode register values the PI writes to the DRAM during initialisation
 * and frequency changes, packed four to a register: array 0 per channel,
 * array 3 per chip select (rank, device) and frequency set point.
 * Translated from CIX's SE firmware debug build, function by function.
 */
#include "mem_types.h"

#define PI_DARRAY0	0x35d8		/* 9 registers */
#define PI_DARRAY3	0x373c		/* 10 registers per set point, 3 per chip select */

#define MR(n)		((uint32_t)MemCalcMrVal(n, Channel, 0, 0, 0))
#define MR3(n)		((uint32_t)MemCalcMrVal(n, Channel, Rank, Device, fsp))

/* MemGetPiDataArray0Settings: DdrType 0 LPDDR4, 1 LPDDR5 */
void MemGetPiDataArray0Settings(uint8_t DdrType, uint8_t Channel, uint32_t *Array)
{
	uint32_t ArrayRet[9] = { 0 };

	if (DdrType == 0) {
		ArrayRet[1] = MR(10) << 8;
		ArrayRet[2] = MR(15) << 16;
		ArrayRet[2] |= MR(16) << 24;
		ArrayRet[3] = MR(17);
		ArrayRet[3] |= MR(18) << 8;
		ArrayRet[3] |= MR(19) << 16;
		ArrayRet[3] |= MR(20) << 24;
		ArrayRet[4] = MR(23) << 8;
		ArrayRet[5] = MR(32) << 24;
		ArrayRet[6] = MR(40) << 16;
	} else if (DdrType == 1) {
		ArrayRet[2] = MR(13);
		ArrayRet[4] = MR(23) << 8;
		ArrayRet[4] |= MR(37) << 16;
		ArrayRet[5] = MR(31) << 16;
		ArrayRet[5] |= MR(32) << 24;
		ArrayRet[6] = MR(33);
		ArrayRet[6] |= MR(34) << 8;
	} else {
		ArrayRet[4] = MR(23) << 8;
		ArrayRet[5] = MR(32) << 24;
	}
	memcpy(Array, ArrayRet, sizeof(ArrayRet));
}

/* MemGetPiDataArray3Settings */
void MemGetPiDataArray3Settings(uint8_t DdrType, uint8_t Channel, uint8_t Rank, uint8_t Device, uint8_t fsp,
				uint32_t *Array)
{
	uint32_t ArrayRet[10] = { 0 };

	ArrayRet[0] = MR3(1);
	ArrayRet[0] |= MR3(2) << 8;
	ArrayRet[0] |= MR3(3) << 16;
	if (DdrType == 1) {
		ArrayRet[0] |= MR3(10) << 24;
		ArrayRet[1] = MR3(11);
		ArrayRet[1] |= MR3(12) << 8;
		ArrayRet[1] |= MR3(14) << 24;
		ArrayRet[2] = MR3(15);
		ArrayRet[2] |= MR3(16) << 8;
		ArrayRet[2] |= MR3(17) << 16;
		ArrayRet[2] |= MR3(18) << 24;
		ArrayRet[3] = MR3(19);
		ArrayRet[3] |= MR3(20) << 8;
		ArrayRet[3] |= MR3(22) << 16;
		ArrayRet[3] |= MR3(25) << 24;
		ArrayRet[4] = (uint8_t)(MR3(12) | 0x80) << 8;
		ArrayRet[5] = MR3(30);
		ArrayRet[5] |= MR3(28) << 8;
	} else {
		ArrayRet[1] = MR3(11);
		ArrayRet[1] |= MR3(12) << 8;
		if (DdrType == 0) {
			ArrayRet[1] |= MR3(13) << 16;
			ArrayRet[1] |= MR3(14) << 24;
		} else {
			ArrayRet[1] |= MR3(14) << 24;
		}
		ArrayRet[3] = MR3(22) << 16;
		ArrayRet[4] = (uint8_t)(MR3(12) | 0x80) << 8;
		if (DdrType == 0)
			ArrayRet[6] = MR3(40);
	}
	ArrayRet[6] |= MR3(14) << 8;
	memcpy(Array, ArrayRet, sizeof(ArrayRet));
}

/* MemPiDataArrayCfg */
void MemPiDataArrayCfg(void)
{
	MEMORY_INIT_S *MemInitPtr = GetMemInitPtr();
	uint32_t DataArray3[10], DataArray0[9], index;
	uint8_t Channel, cs, Rank, Device, fsp;

	for (Channel = 0; Channel < 4; Channel++) {
		if (!MemChannelPopulated(Channel))
			continue;
		MemGetPiDataArray0Settings(MemInitPtr->DdrType, Channel, DataArray0);
		for (index = 0; index < 9; index++)
			MemDdrcRegWrite(PI_DARRAY0 + 4 * index, Channel, DataArray0[index]);
	}
	for (Channel = 0; Channel < 4; Channel++) {
		if (!MemChannelPopulated(Channel))
			continue;
		for (cs = 0; cs < 4; cs++) {
			Rank = cs & 1;
			Device = cs >> 1;
			for (fsp = 0; fsp < 3; fsp++) {
				MemGetPiDataArray3Settings(MemInitPtr->DdrType, Channel, Rank, Device, fsp, DataArray3);
				for (index = 0; index < 10; index++)
					MemDdrcRegWrite(PI_DARRAY3 + 0x78 * cs + 0x28 * fsp + 4 * index, Channel,
							DataArray3[index]);
			}
		}
	}
}
