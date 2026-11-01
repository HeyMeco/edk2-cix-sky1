/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The controllers' interrupts after training (CIX
 * driver/ddr/src/Lib/MemIrq.c): each one is reported and acknowledged.
 * Translated from CIX's SE firmware debug build, function by function.
 */
#include "mem_types.h"

/* interrupt groups: status register, acknowledge register, the bits */
static DDR_IRQ_TLB_ENTRY DdrcIrqTlb[25] = {
	{ IRQ_DDRC_TIMEOUT, "TIMEOUT", 0x57c, 0x5a0, 0xffffffff, 1 },
	{ IRQ_DDRC_ECC, "ECC", 0x580, 0x5a4, 0xffffc1f0, 1 },
	{ IRQ_DDRC_IECC_SBE, "IECC_SBE", 0x580, 0x5a4, 0x3, 1 },
	{ IRQ_DDRC_IECC_DBE, "IECC_DBE", 0x580, 0x5a4, 0xc, 1 },
	{ IRQ_DDRC_ECC_SBE_WR, "ECC_SBE_WR", 0x580, 0x5a4, 0x200, 1 },
	{ IRQ_DDRC_ECC_DBE_WR, "ECC_DBE_WR", 0x580, 0x5a4, 0x400, 1 },
	{ IRQ_DDRC_ECC_SBE_RD, "ECC_SBE_RD", 0x580, 0x5a4, 0x800, 1 },
	{ IRQ_DDRC_ECC_DBE_RD, "ECC_DBE_RD", 0x580, 0x5a4, 0x1000, 1 },
	{ IRQ_DDRC_ECC_DBE_RMW, "ECC_DBE_RMW", 0x580, 0x5a4, 0x2000, 1 },
	{ IRQ_DDRC_LOWPOWER, "LOWPOWER", 0x584, 0x5a8, 0xffff, 1 },
	{ IRQ_DDRC_TRAINING, "TRAINING", 0x58c, 0x5b0, 0xffffffff, 1 },
	{ IRQ_DDRC_USERIF, "USERIF", 0x590, 0x5b4, 0xffffffff, 1 },
	{ IRQ_DDRC_MISC, "MISC", 0x594, 0x5b8, 0xffffffff, 1 },
	{ IRQ_DDRC_BIST_DONE, "BIST_DONE", 0x598, 0x5bc, 0x1, 0 },
	{ IRQ_DDRC_BIST_RSV, "BIST_RSV", 0x598, 0x5bc, 0xfe, 1 },
	{ IRQ_DDRC_DFI, "DFI", 0x598, 0x5bc, 0xff0000, 1 },
	{ IRQ_DDRC_FREQ, "FREQ", 0x59c, 0x5c0, 0xff, 1 },
	{ IRQ_DDRC_MRR_ERR, "MRR_ERR", 0x59c, 0x5c0, 0x10000, 1 },
	{ IRQ_DDRC_MRR_DONE, "MRR_DONE", 0x59c, 0x5c0, 0x40000, 0 },
	{ IRQ_DDRC_MRW_DONE, "MRW_DONE", 0x59c, 0x5c0, 0x80000, 0 },
	{ IRQ_DDRC_MODE_RSV, "MODE_RSV", 0x59c, 0x5c0, 0xf20000, 1 },
	{ IRQ_DDRC_PARITY, "PARITY", 0x59c, 0x5c0, 0xff000000, 1 },
	{ IRQ_DDRC_INIT_RST_VALID, "INIT_RST_VALID", 0x59c, 0x5c0, 0x100, 1 },
	{ IRQ_DDRC_INIT_COMP, "INIT_COMP", 0x59c, 0x5c0, 0x200, 1 },
	{ IRQ_DDRC_INIT_SW_MODE, "INIT_SW_MODE", 0x59c, 0x5c0, 0x800, 1 },
};

/*
 * ddrc_irq_handler: report and acknowledge what is pending.  CIX's also
 * dumps the inline ECC error address registers (0x428-0x444) and decodes
 * the failing byte of a link ECC read error (0x2a8-0x2b4) to the log; those
 * register dumps are left out here, the report line is kept.
 */
void ddrc_irq_handler(void *ctx)
{
	uint8_t ch = *(uint8_t *)ctx;
	uint32_t index, status_val, ack_val;
	DDR_IRQ_TLB_ENTRY *e;

	for (index = 0; index < 25; index++) {
		e = &DdrcIrqTlb[index];
		status_val = MemDdrcRegRead(e->status_reg_offset, ch);
		if (!(e->irq_mask & status_val))
			continue;
		if (e->log_en)
			log_print(0x4000, "CH%d DDRC IRQ %s detected. st 0x%x\n", ch, e->irq_str,
				  e->irq_mask & status_val);
		ack_val = MemDdrcRegRead(e->ack_reg_offset, ch);
		MemDdrcRegWrite(e->ack_reg_offset, ch, (status_val & e->irq_mask) | ack_val);
	}
}

/* SwitchDdrcIrqLog: turn the report of one interrupt on or off; returns
 * whether it was on */
bool SwitchDdrcIrqLog(DDR_IRQ_TYPE type, bool log_en)
{
	uint32_t index;
	uint8_t ori_log_en;

	for (index = 0; index < 25; index++) {
		if ((uint8_t)DdrcIrqTlb[index].irq_type == (uint8_t)type) {
			ori_log_en = DdrcIrqTlb[index].log_en;
			DdrcIrqTlb[index].log_en = log_en;
			return ori_log_en != 0;
		}
	}
	return false;
}

/* RegisterDdrcIrq */
void RegisterDdrcIrq(void)
{
	sys_register_ddrc_irq(GetMemInitPtr()->ChannelMask, ddrc_irq_handler);
}
