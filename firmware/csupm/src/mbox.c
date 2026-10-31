/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Mailboxes (CIX's dev_mailbox.c and the mailbox part of app_msgInit).
 */
#include "mbox.h"

/* Every word back to zero, the identity word and control to 1, status
 * cleared */
void mbox_clear(unsigned i)
{
	REG32(MBOX_LOCK(i)) = MBOX_LOCK_KEY;
	for (unsigned w = 0; w < 32; w++)
		REG32(MBOX_DATA(i, w)) = 0;
	for (unsigned c = 0; c < 8; c++)
		REG32(MBOX_FAST(i, c)) = 0;
	REG32(MBOX_ID(i)) = 1;
	REG32(MBOX_CTRL(i)) = 1;
	REG32(MBOX_CLEAR(i)) = MBOX_ALL_BITS;
}

void mbox_route(unsigned i, unsigned dir, uint32_t keep, uint32_t bits)
{
	REG32(MBOX_INT_EN(i, dir)) = (REG32(MBOX_INT_EN(i, dir)) & keep) | bits;
}

void mbox_ring(unsigned i, uint32_t bits)
{
	REG32(MBOX_DOORBELL(i)) = bits & 3;
}

/*
 * Even mailboxes and mailbox 9 carry requests from the AP and interrupt
 * the PM: those of the OS and TF-A (0 and 2) on the doorbell and on writes
 * to the fast channels, the others on the doorbell.  The odd ones and
 * mailbox 8 carry the PM's messages to the AP.  Mailbox 8 keeps its
 * contents: the SE's handshake is in it.
 */
void mbox_init(void)
{
	static const struct {
		uint8_t dir;
		uint32_t bits;
	} route[MBOX_N] = {
		{ MBOX_TO_PM, MBOX_DOORBELL_BIT | MBOX_FAST_BITS },
		{ MBOX_TO_AP, MBOX_TO_AP_BIT },
		{ MBOX_TO_PM, MBOX_DOORBELL_BIT | MBOX_FAST_BITS },
		{ MBOX_TO_AP, MBOX_TO_AP_BIT },
		{ MBOX_TO_PM, MBOX_DOORBELL_BIT },
		{ MBOX_TO_AP, MBOX_TO_AP_BIT },
		{ MBOX_TO_PM, MBOX_DOORBELL_BIT },
		{ MBOX_TO_AP, MBOX_TO_AP_BIT },
		{ MBOX_TO_AP, MBOX_TO_AP_BIT },
		{ MBOX_TO_PM, MBOX_DOORBELL_BIT },
	};

	for (unsigned i = 0; i < MBOX_N; i++) {
		if (i != 8)
			mbox_clear(i);
	}
	for (unsigned i = 0; i < MBOX_N; i++)
		mbox_route(i, route[i].dir, 0, route[i].bits);
}
