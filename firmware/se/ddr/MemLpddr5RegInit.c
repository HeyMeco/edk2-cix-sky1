/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * LPDDR5 register initialisation (CIX driver/ddr/src/Tech/lp5/
 * MemLpddr5RegInit.c): CIX's register tables (MemLpddr5RegData.c, generated
 * from CIX's firmware) written to each populated channel's controller, PI
 * and PHY before the computed settings.  Translated from CIX's SE firmware
 * debug build, function by function.
 */
#include "mem_types.h"

#define ARRAY_SIZE(a)	(sizeof(a) / sizeof((a)[0]))

extern const REG_VAL_PAIR DdrcInitList[79];
extern const uint32_t DdrpiInitList[1227], DdrpiDarrayMapList[60];
extern const uint32_t DdrpInitValList[105], DdrpAcValList[69], DdrpDsliceValList[139];

/* MemLpddr5DdrcRegInit */
void MemLpddr5DdrcRegInit(void)
{
	uint32_t i;
	uint8_t Channel;

	for (Channel = 0; Channel < 4; Channel++) {
		if (!MemChannelPopulated(Channel))
			continue;
		for (i = 0; i < ARRAY_SIZE(DdrcInitList); i++)
			MemDdrcRegWrite(DdrcInitList[i].offset, Channel, DdrcInitList[i].value);
	}
}

/* MemLpddr5DdrpiRegInit: PI registers 0-1226, then the data array map */
void MemLpddr5DdrpiRegInit(void)
{
	uint32_t i;
	uint8_t Channel;

	for (Channel = 0; Channel < 4; Channel++) {
		if (!MemChannelPopulated(Channel))
			continue;
		for (i = 0; i < ARRAY_SIZE(DdrpiInitList); i++)
			MemDdrcRegWrite(0x2000 + 4 * i, Channel, DdrpiInitList[i]);
		for (i = 0; i < ARRAY_SIZE(DdrpiDarrayMapList); i++)
			MemDdrcRegWrite(0x391c + 4 * i, Channel, DdrpiDarrayMapList[i]);
	}
}

/* MemLpddr5DdrpRegInit: PHY global registers, the address/command slice,
 * and data slice 0 with PHY_SLICE_MULTICAST_EN (PHY_1384) set so that it
 * goes to every data slice */
void MemLpddr5DdrpRegInit(void)
{
	DENALI_PHY_1384 ddrp_1384;
	uint32_t i;
	uint8_t Channel;

	for (Channel = 0; Channel < 4; Channel++) {
		if (!MemChannelPopulated(Channel))
			continue;
		for (i = 0; i < ARRAY_SIZE(DdrpInitValList); i++)
			MemDdrcRegWrite(0x5400 + 4 * i, Channel, DdrpInitValList[i]);
		for (i = 0; i < ARRAY_SIZE(DdrpAcValList); i++)
			MemDdrcRegWrite(0x5000 + 4 * i, Channel, DdrpAcValList[i]);
		ddrp_1384.value = MemDdrcRegRead(0x55a0, Channel);
		ddrp_1384.fields.PHY_SLICE_MULTICAST_EN = 1;
		MemDdrcRegWrite(0x55a0, Channel, ddrp_1384.value);
		for (i = 0; i < ARRAY_SIZE(DdrpDsliceValList); i++)
			MemDdrcRegWrite(0x4000 + 4 * i, Channel, DdrpDsliceValList[i]);
		ddrp_1384.value = MemDdrcRegRead(0x55a0, Channel);
		ddrp_1384.fields.PHY_SLICE_MULTICAST_EN = 0;
		MemDdrcRegWrite(0x55a0, Channel, ddrp_1384.value);
	}
}
