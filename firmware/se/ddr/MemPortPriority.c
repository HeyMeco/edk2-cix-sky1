/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * AXI port priorities and bandwidth limits (CIX driver/ddr/src/Feat/QOS/
 * MemPortPriority.c).  Translated from CIX's SE firmware debug build.
 */
#include "mem_types.h"
#include "mem_regs.h"

/* MemPortPriorityCfg: port priority mode 0 fixes both ports' priority at
 * 100% bandwidth, mode 1 uses the configured bandwidths; then the read and
 * write priorities and the bandwidth overflow bits */
void MemPortPriorityCfg(void)
{
	MEM_FEATURE_S *MemFeatPtr = GetMemFeatPtr();
	DENALI_CTL_415 ddrc_415;
	DENALI_CTL_416 ddrc_416;
	DENALI_CTL_624 ddrc_624;
	DENALI_CTL_723 ddrc_723;
	DENALI_CTL_724 ddrc_724;
	uint8_t Channel;

	for (Channel = 0; Channel < 4; Channel++) {
		if (!MemChannelPopulated(Channel))
			continue;
		ddrc_415.value = MemDdrcRegRead(DENALI_CTL(415), Channel);
		ddrc_416.value = MemDdrcRegRead(DENALI_CTL(416), Channel);
		ddrc_624.value = MemDdrcRegRead(DENALI_CTL(624), Channel);
		ddrc_723.value = MemDdrcRegRead(DENALI_CTL(723), Channel);
		ddrc_724.value = MemDdrcRegRead(DENALI_CTL(724), Channel);
		if (MemFeatPtr->PortPriority == 0) {
			ddrc_415.fields.AXI0_FIXED_PORT_PRIORITY_ENABLE = 1;
			ddrc_624.fields.AXI1_FIXED_PORT_PRIORITY_ENABLE = 1;
			ddrc_723.fields.AXI0_BDW = 100;
			ddrc_723.fields.AXI1_BDW = 100;
		} else if (MemFeatPtr->PortPriority == 1) {
			ddrc_415.fields.AXI0_FIXED_PORT_PRIORITY_ENABLE = 0;
			ddrc_624.fields.AXI1_FIXED_PORT_PRIORITY_ENABLE = 0;
			ddrc_723.fields.AXI0_BDW = MemFeatPtr->BdwP0;
			ddrc_723.fields.AXI1_BDW = MemFeatPtr->BdwP1;
		}
		ddrc_416.fields.AXI0_R_PRIORITY = MemFeatPtr->RPriorityP0;
		ddrc_416.fields.AXI0_W_PRIORITY = MemFeatPtr->WPriorityP0;
		ddrc_624.fields.AXI1_R_PRIORITY = MemFeatPtr->RPriorityP1;
		ddrc_624.fields.AXI1_W_PRIORITY = MemFeatPtr->WPriorityP1;
		ddrc_723.fields.AXI0_BDW_OVFLOW = MemFeatPtr->BdwOvflowP0;
		ddrc_724.fields.AXI1_BDW_OVFLOW = MemFeatPtr->BdwOvflowP1;
		MemDdrcRegWrite(DENALI_CTL(415), Channel, ddrc_415.value);
		MemDdrcRegWrite(DENALI_CTL(416), Channel, ddrc_416.value);
		MemDdrcRegWrite(DENALI_CTL(624), Channel, ddrc_624.value);
		MemDdrcRegWrite(DENALI_CTL(723), Channel, ddrc_723.value);
		MemDdrcRegWrite(DENALI_CTL(724), Channel, ddrc_724.value);
	}
}
