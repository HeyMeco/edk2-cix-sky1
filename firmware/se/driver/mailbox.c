/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * CIX mailbox (CIX driver/mailbox/mailbox.c).
 *
 * One block per direction and peer.  A channel works as a doorbell, as 32
 * message registers (the receiver answers in the same registers), as a
 * FIFO, or as single "fast" registers.  The AP's requests arrive on
 * register channels; the SE's own messages to the AP go through a FIFO.
 */
#include "se.h"
#include "arch.h"
#include "mailbox.h"

#define MBOX_TX_TRIGGER		0x80	/* bit 0 send, bit 1 acknowledge */
#define MBOX_RX_ACK		0x84
#define MBOX_FIFO_WR		0xac
#define MBOX_FIFO_RD		0xb0
#define MBOX_FIFO_STATUS	0xb4	/* bit 2 empty */
#define MBOX_FIFO_WM		0xb8
#define MBOX_TX_INT_EN		0xbc
#define MBOX_RX_INT_EN		0xc0
#define MBOX_INT_CLR		0xc4
#define MBOX_INT_STATUS		0xc8
#define MBOX_FIFO_RST		0xcc
#define MBOX_FAST(i)		(0x8c + (i) * 4)

#define REG(c, off)		REG32((c)->base + (off))

enum { MBOX_TYPE_DB, MBOX_TYPE_REG, MBOX_TYPE_FIFO, MBOX_TYPE_FAST };
enum { MBOX_DIR_TX, MBOX_DIR_RX };

typedef struct {
	uint32_t base;
	uint32_t clk_en_mask;
	uint32_t reset_mask;
	const char *name;
	uint8_t irq;
	uint8_t type;
	uint8_t index;
	uint8_t dir;
	bool in_use;
	mbox_client *cl;
	volatile bool tx_ack;
	bool tx_polling;
} mbox_context;

#define CHAN(n, b, m, i, d)	{ .base = (b), .clk_en_mask = (m), .reset_mask = (m), \
				  .name = (n), .irq = (i), .dir = (d) }

static mbox_context s_mbox_table[MBOX_CHN_MAX] = {
	[MBOX_CHN_AP2SE_REE]	   = CHAN("ap2se_ree", 0xa0050000, 0x010, 43, MBOX_DIR_RX),
	[MBOX_CHN_SE2AP_REE]	   = CHAN("se2ap_ree", 0xa0060000, 0x020, 42, MBOX_DIR_TX),
	[MBOX_CHN_AP2SE_TEE]	   = CHAN("ap2se_tee", 0xa0070000, 0x040, 41, MBOX_DIR_RX),
	[MBOX_CHN_SE2AP_TEE]	   = CHAN("se2ap_tee", 0xa0080000, 0x080, 40, MBOX_DIR_TX),
	[MBOX_CHN_AP2SE_RESERVED1] = CHAN("ap2se_vir0", 0xa0090000, 0x100, 39, MBOX_DIR_RX),
	[MBOX_CHN_SE2AP_RESERVED1] = CHAN("se2ap_vir0", 0xa00a0000, 0x200, 38, MBOX_DIR_TX),
	[MBOX_CHN_AP2SE_RESERVED2] = CHAN("ap2se_vir1", 0xa00b0000, 0x400, 37, MBOX_DIR_RX),
	[MBOX_CHN_SE2AP_RESERVED2] = CHAN("se2ap_vir1", 0xa00c0000, 0x800, 36, MBOX_DIR_TX),
	[MBOX_CHN_SE2PM]	   = CHAN("se2pm", 0xa00d0000, 0x1000, 35, MBOX_DIR_TX),
};

static void cix_mbox_isr(void *arg)
{
	mbox_context *c = arg;
	uint32_t data[32] = { 0 }, ack[32] = { 0 }, irq = core_irq_save(), st, n, i;

	if (c == NULL || !c->in_use) {
		hal_printf_level(LOG_WARN, "Invalid mbox context\n");
		goto out;
	}
	st = REG(c, MBOX_INT_STATUS);
	switch (c->type) {
	case MBOX_TYPE_DB:
	case MBOX_TYPE_REG:
		if (c->dir == MBOX_DIR_RX && (st & 1)) {
			REG(c, MBOX_INT_CLR) |= 1;
			if (c->type == MBOX_TYPE_REG) {
				data[0] = REG(c, 0);
				n = ((data[0] & 0x7f) + 3) / 4;
				for (i = 1; i < n && i < 32; i++)
					data[i] = REG(c, i * 4);
			}
			if (c->cl->rx_callback)
				c->cl->rx_callback(c->cl, c->type == MBOX_TYPE_REG ? data : NULL,
						   c->type == MBOX_TYPE_REG ? ack : NULL);
			if (c->type == MBOX_TYPE_REG) {
				n = ((ack[0] & 0x7f) + 3) / 4;
				for (i = 0; i < n && i < 32; i++)
					REG(c, i * 4) = ack[i];
			}
			REG(c, MBOX_RX_ACK) |= 1;
			if (!c->tx_polling)
				REG(c, MBOX_TX_TRIGGER) |= 2;
		} else if (c->dir == MBOX_DIR_TX && (st & 2)) {
			REG(c, MBOX_INT_CLR) |= 2;
			c->tx_ack = true;
			if (c->cl->tx_done)
				c->cl->tx_done(c->cl);
		}
		break;
	case MBOX_TYPE_FIFO:
		if (c->dir == MBOX_DIR_RX) {
			if (st & 0x14) {
				REG(c, MBOX_INT_CLR) |= 0x14;
				i = 0;
				do {
					ack[i] = REG(c, MBOX_FIFO_RD);
					if (i < 31)
						i++;
				} while (!(REG(c, MBOX_FIFO_STATUS) & 4));
				if (c->cl->rx_callback)
					c->cl->rx_callback(c->cl, ack, NULL);
			}
			if (st & 0x80) {
				hal_printf_level(LOG_WARN, "fifo underflow: int_stats %d\n",
						 (int)REG(c, MBOX_FIFO_STATUS));
				REG(c, MBOX_INT_CLR) |= 0x80;
			}
		} else {
			if (st & 8) {
				REG(c, MBOX_INT_CLR) |= 8;
				REG(c, MBOX_TX_INT_EN) &= ~8u;
				c->tx_ack = true;
				if (c->cl->tx_done)
					c->cl->tx_done(c->cl);
			}
			if (st & 0x40) {
				hal_printf_level(LOG_WARN, "fifo overflow: int_stats %d\n",
						 (int)REG(c, MBOX_FIFO_STATUS));
				REG(c, MBOX_INT_CLR) |= 0x40;
			}
		}
		break;
	case MBOX_TYPE_FAST:
		if (c->dir == MBOX_DIR_RX && (st & BIT(c->index + 9))) {
			REG(c, MBOX_INT_CLR) |= BIT(c->index + 9);
			ack[0] = REG(c, MBOX_FAST(c->index));
			if (c->cl->rx_callback)
				c->cl->rx_callback(c->cl, ack, NULL);
		}
		break;
	default:
		hal_printf_level(LOG_WARN, "Invalid channel type: %d\n", c->type);
	}
out:
	core_irq_restore(irq);
}

static void mbox_free_channel(mbox_chan_id chan)
{
	mbox_context *c;

	if (chan >= MBOX_CHN_MAX || s_mbox_table[chan].cl == NULL)
		return;
	c = &s_mbox_table[chan];
	if (c->dir == MBOX_DIR_TX)
		REG(c, MBOX_TX_INT_EN) = 0;
	else
		REG(c, MBOX_RX_INT_EN) = 0;
	memset(c->cl, 0, sizeof(*c->cl));
	c->cl = NULL;
	c->tx_ack = false;
	c->in_use = false;
	core_irq_disable(c->irq);
	core_irq_unregister(c->irq);
}

/* index: 8 doorbell, 9 FIFO, 10 registers, 0..7 fast channel */
static mbox_context *mbox_request_channel(mbox_client *cl, mbox_chan_id chan, int index)
{
	mbox_context *c = &s_mbox_table[chan];

	if (c->in_use) {
		hal_printf_level(LOG_WARN, "current channel is busy, %d\n", chan);
		return NULL;
	}
	c->cl = cl;
	cl->chan = chan;
	cl->chan_type = index;
	if (index == 8) {
		c->type = MBOX_TYPE_DB;
	} else if (index == 9) {
		c->type = MBOX_TYPE_FIFO;
	} else if (index == 10) {
		c->type = MBOX_TYPE_REG;
	} else if (index < 8) {
		c->type = MBOX_TYPE_FAST;
	} else {
		hal_printf_level(LOG_WARN, "Invalid index %d\n", index);
		goto fail;
	}
	c->index = index;
	hal_printf_level(LOG_INFO, "%s, %s@%p, irq %d, dir %d, type %d, index %d\n", "cix_mbox_startup",
			 c->name, (void *)(uintptr_t)c->base, c->irq, c->dir, c->type, index);
	switch (c->type) {
	case MBOX_TYPE_FIFO:
		REG(c, MBOX_FIFO_RST) |= 1;
		REG(c, MBOX_FIFO_WM) = 0x20;
		if (c->dir == MBOX_DIR_TX)
			REG(c, MBOX_TX_INT_EN) |= 0x40;
		else
			REG(c, MBOX_RX_INT_EN) |= 0x90;
		break;
	case MBOX_TYPE_DB:
	case MBOX_TYPE_REG:
		if (c->dir == MBOX_DIR_TX) {
			if (!c->tx_polling)
				REG(c, MBOX_TX_INT_EN) |= 2;
		} else {
			REG(c, MBOX_RX_INT_EN) |= 1;
		}
		break;
	case MBOX_TYPE_FAST:
		if (c->dir == MBOX_DIR_RX)
			REG(c, MBOX_RX_INT_EN) |= BIT(index + 9);
		break;
	}
	c->in_use = true;
	core_irq_register(c->irq, cix_mbox_isr, c);
	core_irq_enable(c->irq);
	return c;
fail:
	hal_printf_level(LOG_WARN, "Unable to startup the channel %d ret=%d\n", chan, 0x9a01);
	mbox_free_channel(chan);
	return NULL;
}

/* Send msg (size in bytes in bits [6:0] of the first word); wait for the peer's ack */
uint32_t mbox_send_data_sync(mbox_chan_id chan, const void *msg)
{
	mbox_context *c = &s_mbox_table[chan];
	const uint32_t *w = msg;
	uint32_t n, i;
	int tout;

	if (chan >= MBOX_CHN_MAX || msg == NULL) {
		hal_printf_level(LOG_WARN, "mbox_ctx or msg is NULL!\n");
		return MBOX_ERR_PARAM;
	}
	if (c->dir != MBOX_DIR_TX) {
		hal_printf_level(LOG_WARN, "Not tx channel!\n");
		return MBOX_ERR_PARAM;
	}
	if (!c->in_use) {
		hal_printf_level(LOG_WARN, "channel is not ready!\n");
		return MBOX_ERR_PARAM;
	}
	n = ((w[0] & 0x7f) + 3) / 4;
	if (n - 1 > 31 || (c->type == MBOX_TYPE_FAST && n != 1)) {
		hal_printf_level(LOG_WARN, "Failed! Invlaid msg len %d!\n", (int)n);
		return MBOX_ERR_PARAM;
	}
	if (c->cl->tx_prepare)
		c->cl->tx_prepare(c->cl, msg);
	c->tx_ack = false;
	switch (c->type) {
	case MBOX_TYPE_DB:
		REG(c, MBOX_TX_TRIGGER) |= 1;
		break;
	case MBOX_TYPE_REG:
		for (i = 0; i < n; i++)
			REG(c, i * 4) = w[i];
		REG(c, MBOX_TX_TRIGGER) |= 1;
		break;
	case MBOX_TYPE_FIFO:
		REG(c, MBOX_FIFO_WM) = n;
		for (i = 0; i < n; i++)
			REG(c, MBOX_FIFO_WR) = w[i];
		REG(c, MBOX_TX_INT_EN) |= 8;
		break;
	case MBOX_TYPE_FAST:
		REG(c, MBOX_FAST(c->index)) = w[0];
		return 0;
	}
	if (c->cl->tx_block)
		return 0;
	for (tout = c->cl->tx_tout ? (int)c->cl->tx_tout : 100; tout > 0; tout--) {
		hal_mdelay(1);
		if (c->tx_ack)
			return 0;
	}
	hal_printf_level(LOG_WARN, "Mbox Send time out!\n");
	return MBOX_ERR_TIMEOUT;
}

uint32_t drv_mbox_init(mbox_client *cl, mbox_chan_id chan, int index)
{
	mbox_context *c;

	if (chan >= MBOX_CHN_MAX) {
		hal_printf_level(LOG_WARN, "[%d]Get mbox instance failed!\n", chan);
		return MBOX_ERR_CHAN;
	}
	c = &s_mbox_table[chan];
	if (c->in_use) {
		hal_printf_level(LOG_WARN, "current channel is busy, %d\n", chan);
		return MBOX_ERR_BUSY;
	}
	REG32(SE_CLKCTRL_BASE) |= c->clk_en_mask;
	REG32(SE_CLKCTRL_BASE + 4) |= c->reset_mask;
	return mbox_request_channel(cl, chan, index) ? 0 : MBOX_ERR_CHAN;
}

uint32_t drv_mbox_deinit(mbox_chan_id chan)
{
	if (chan >= MBOX_CHN_MAX) {
		hal_printf_level(LOG_WARN, "[%d]Get mbox instance failed!\n", chan);
		return MBOX_ERR_CHAN;
	}
	if (!s_mbox_table[chan].in_use) {
		hal_printf_level(LOG_INFO, "current channel %d is not in used\n", chan);
		return 0;
	}
	mbox_free_channel(chan);
	REG32(SE_CLKCTRL_BASE) &= ~s_mbox_table[chan].clk_en_mask;
	return 0;
}
