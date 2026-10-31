/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The core's 32 interrupt lines (XEA2) and their levels, as CIX's image
 * configures them (Xthal_intlevel): lines 12-30 come from the interrupt
 * aggregator at 0xa0000500, which ORs up to 32 sources per line.
 */
#ifndef IRQ_H
#define IRQ_H

#define EXCCAUSE_LEVEL1_INTERRUPT	4

#define IRQ_LEVEL1_MASK		0x7ffff04f	/* 0-3, 6, 12-30 */
#define IRQ_LEVEL2_MASK		0x00000010	/* 4 */
#define IRQ_LEVEL3_MASK		0x00000020	/* 5 */
#define IRQ_LEVEL4_MASK		0x00000280	/* 7 (CCOMPARE1), 9 */
#define IRQ_LEVEL5_MASK		0x00000d00	/* 8 (CCOMPARE2), 10, 11 */
#define IRQ_NMI			31

#define IRQ_TIMER0		6		/* CCOMPARE0 */
#define IRQ_TIMER1		7
#define IRQ_TIMER2		8

/* Lines past 31 exist only in software (scp/src/arch_interrupt.c) */
#define IRQ_SOFT_TIMER		32		/* the timer module's alarms */

/* Aggregator: line L (12-30) has raw status at +4 (L - 12), enables 0x80 above */
#define AGG_BASE		0xa0000500u
#define AGG_FIRST		12
#define AGG_LAST		30
#define AGG_STATUS(l)		(AGG_BASE + 4 * ((l) - AGG_FIRST))
#define AGG_ENABLE(l)		(AGG_BASE + 0x80 + 4 * ((l) - AGG_FIRST))
#define IRQ_MBOX		25		/* source i: mailbox i */

#ifndef __ASSEMBLER__
#include "pm.h"

extern volatile uint32_t arch_irq_pending;
#endif

#endif
