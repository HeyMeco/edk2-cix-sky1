/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Sky1's RTT channels and control block (CIX's
 * Source/projects/sky1/rtt/cfg_rtt.c, _s_sky1_rtt_cfg from its image).
 * CIX's users of the channels: the debug console (up 0, down 0, its CLI),
 * performance streams (up 1) and the PM's log (up 2, src/log/fwlog.c);
 * only the log is here.
 */
#include "log/log.h"
#include "rtt.h"

#define RTT_DRAM		0x83400000u
#define RTT_DRAM_STREAM		0x83600000u
#define RTT_CB_OFFSET		0x7f3680u	/* the control block, DRAM 0x83bf3680 */

/* pBufferPM: filled in by sky1_rtt_init */
static RTT_CHANNEL _s_sky1_rtt_cfg[4] = {
	{ .pBufferJLINKBase = RTT_DRAM, .Offset = 0, .SizeOfBuffer = 0x100000,
	  .Flags = 1, .isUp = true, .index = 1 },
	{ .pBufferJLINKBase = RTT_DRAM, .Offset = 0x7f3800, .SizeOfBuffer = 0xc800,
	  .Flags = 1, .isUp = true, .index = 0 },
	{ .pBufferJLINKBase = RTT_DRAM, .Offset = 0x7f3700, .SizeOfBuffer = 0x100,
	  .Flags = 1, .isUp = false, .index = 0 },
	{ .pBufferJLINKBase = RTT_DRAM_STREAM, .Offset = 0, .SizeOfBuffer = 0x8000,
	  .Flags = 1, .isUp = true, .index = 2 },
};

/* Hub windows for the channels' DRAM, then the control block */
void sky1_rtt_init(void)
{
	uint32_t pm_ddr_space_base;

	for (uint32_t i = 0; i < ARRAY_SIZE(_s_sky1_rtt_cfg); i++) {
		RTT_CHANNEL *ch = &_s_sky1_rtt_cfg[i];

		pm_ddr_space_base = tlb_ctrl_dram_to_syshub(ch->pBufferJLINKBase, false);
		if (pm_ddr_space_base == 0)
			vAssertCalledWithTRC(0xf1b40064);
		ch->pBufferPM = pm_ddr_space_base + ch->Offset;
	}
	pm_ddr_space_base = tlb_ctrl_dram_to_syshub(RTT_DRAM, false);
	if (pm_ddr_space_base == 0)
		vAssertCalledWithTRC(0xf1b40069);
	RTT_Init((RTT_CB *)(uintptr_t)(pm_ddr_space_base + RTT_CB_OFFSET), _s_sky1_rtt_cfg,
		 (int)ARRAY_SIZE(_s_sky1_rtt_cfg));
}
