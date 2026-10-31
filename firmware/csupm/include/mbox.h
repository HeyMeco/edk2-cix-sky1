/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The ten mailboxes between the PM and the AP (CIX's dev_mailbox.c),
 * SoC 0x06590000 + 0x10000 * i.  Each has 32 data words (the SCMI shared
 * memory, TF-A's mailbox_mem_t layout), a doorbell, eight fast channels and
 * interrupt enables for both directions.  Mailbox 8 also carries the SE's
 * boot handshake.
 */
#ifndef MBOX_H
#define MBOX_H

#include "pm.h"

#define MBOX_N			10
#define MBOX_DATA(i, w)		(PM_MBOX(i) + 4 * (w))	/* 32 words */
#define MBOX_DOORBELL(i)	(PM_MBOX(i) + 0x80)	/* PM -> AP, bits 1:0 */
#define MBOX_LOCK(i)		(PM_MBOX(i) + 0x84)
#define MBOX_LOCK_KEY		0x55aa5a5au
#define MBOX_ID(i)		(PM_MBOX(i) + 0x88)
#define MBOX_FAST(i, c)		(PM_MBOX(i) + 0x8c + 4 * (c))	/* 8 fast channels */
#define MBOX_INT_EN(i, dir)	(PM_MBOX(i) + 0xbc + 4 * (dir))
#define MBOX_TO_AP		0
#define MBOX_TO_PM		1
#define MBOX_CLEAR(i)		(PM_MBOX(i) + 0xc4)
#define MBOX_STATUS(i)		(PM_MBOX(i) + 0xc8)
#define MBOX_CTRL(i)		(PM_MBOX(i) + 0xcc)

/* Status and enable bits */
#define MBOX_DOORBELL_BIT	BIT(0)
#define MBOX_TO_AP_BIT		BIT(1)
#define MBOX_FAST_BITS		(0xffu << 9)		/* a write to fast channel c sets bit 9 + c */
#define MBOX_ALL_BITS		0x1ffffu

void mbox_clear(unsigned i);
void mbox_route(unsigned i, unsigned dir, uint32_t keep, uint32_t bits);
void mbox_ring(unsigned i, uint32_t bits);
void mbox_init(void);

#endif
