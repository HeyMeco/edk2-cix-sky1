/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * CIX mailbox, FIFO channels (the only type the SFH uses).  Per channel
 * register block: 0xac TX FIFO, 0xb0 RX FIFO, 0xb4 FIFO status (bit 2:
 * empty), 0xb8 FIFO word count / threshold, 0xbc TX interrupt enable,
 * 0xc0 RX interrupt enable, 0xc4 interrupt clear, 0xc8 interrupt status.
 */
#include "sfh.h"

#define MB_TX_FIFO	0xac
#define MB_RX_FIFO	0xb0
#define MB_FIFO_STAT	0xb4
#define MB_FIFO_CNT	0xb8
#define MB_TX_INT_EN	0xbc
#define MB_RX_INT_EN	0xc0
#define MB_INT_CLR	0xc4
#define MB_INT_STAT	0xc8

#define MB_FIFO_EMPTY	(1u << 2)

enum { MBOX_TX = 0, MBOX_RX = 1 };

struct mbox_chan {
	uint32_t		base;
	uint32_t		clk_bit;	/* in SFH_CLK_EN */
	uint32_t		reset_bit;	/* in SFH_CLK_EN2 */
	const char		*name;
	int			irq;
	uint8_t			dir;
	uint8_t			ready;
	volatile uint8_t	tx_done;
	struct mbox_client	*cl;
};

#define CHAN(b, clk, rst, n, i, d) \
	{ .base = (b), .clk_bit = (clk), .reset_bit = (rst), .name = (n), .irq = (i), .dir = (d) }

static struct mbox_chan chans[MBOX_CHANNELS] = {
	CHAN(0x50080000u, 0x0800, 0x04000, "sfh2ap", IRQ_MBOX(0), MBOX_TX),
	CHAN(0x50090000u, 0x1000, 0x08000, "ap2sfh", IRQ_MBOX(1), MBOX_RX),
	CHAN(0x500a0000u, 0x2000, 0x10000, "sfh2pm", IRQ_MBOX(2), MBOX_TX),
	CHAN(0x500b0000u, 0x4000, 0x20000, "pm2sfh", IRQ_MBOX(3), MBOX_RX),
};

static void mbox_irq(void *arg)
{
	struct mbox_chan *c = arg;
	uint32_t st;

	if (!c || !c->ready) {
		log_printf(LOG_INFO, "Invalid mbox context\n");
		return;
	}
	st = REG32(c->base + MB_INT_STAT);
	if (c->dir == MBOX_RX) {
		if (st & 0x14) {
			uint32_t msg[MBOX_MSG_WORDS_MAX + 1];
			unsigned int n = 0;

			REG32(c->base + MB_INT_CLR) |= 0x14;
			do {
				uint32_t w = REG32(c->base + MB_RX_FIFO);

				if (n <= MBOX_MSG_WORDS_MAX)
					msg[n++] = w;
			} while (!(REG32(c->base + MB_FIFO_STAT) & MB_FIFO_EMPTY));
			if (c->cl->rx)
				c->cl->rx(c->cl, msg);
		}
		if (st & 0x80)
			REG32(c->base + MB_INT_CLR) |= 0x80;
	} else {
		if (st & 0x08) {
			REG32(c->base + MB_INT_CLR) |= 0x08;
			REG32(c->base + MB_TX_INT_EN) &= ~0x08u;
			c->tx_done = 1;
		}
		if (st & 0x40)
			REG32(c->base + MB_INT_CLR) |= 0x40;
	}
}

int mbox_request_channel(struct mbox_client *cl, unsigned int ch, unsigned int index)
{
	struct mbox_chan *c;

	if (ch >= MBOX_CHANNELS) {
		log_printf(LOG_INFO, "[%d]Get mbox instance failed!\n", ch);
		return -1;
	}
	c = &chans[ch];
	if (c->ready) {
		log_printf(LOG_INFO, "current channel is busy, %d\n", ch);
		return -4;
	}
	REG32(SFH_CLK_EN) |= c->clk_bit;
	cl->channel = (uint8_t)ch;
	cl->index = (uint8_t)index;
	c->cl = cl;
	log_printf(LOG_DEBUG, "%s, %s@%p, irq %d, dir %d, type %d, index %d\n",
		   "cix_mbox_startup", c->name, (void *)c->base, c->irq, c->dir, 2, index);
	REG32(c->base + MB_FIFO_CNT) = 3;
	if (c->dir == MBOX_TX)
		REG32(c->base + MB_TX_INT_EN) |= 0x40;
	else
		REG32(c->base + MB_RX_INT_EN) |= 0x90;
	c->ready = 1;
	irq_register(c->irq, mbox_irq, c);
	irq_enable(c->irq);
	return 0;
}

int mbox_send(unsigned int ch, const uint32_t *msg)
{
	struct mbox_chan *c = ch < MBOX_CHANNELS ? &chans[ch] : 0;
	unsigned int n;
	int left;

	if (!c || !msg) {
		log_printf(LOG_INFO, "mbox_ctx or msg is NULL!\n");
		return -2;
	}
	if (c->dir != MBOX_TX) {
		log_printf(LOG_INFO, "Not tx channel!\n");
		return -2;
	}
	if (c->ready != 1) {
		log_printf(LOG_INFO, "channel is not ready!\n");
		return -2;
	}
	n = mbox_msg_words(msg);
	if (n == 0 || n > MBOX_MSG_WORDS_MAX) {
		log_printf(LOG_INFO, "invalid msg len %d!\n", n);
		return -2;
	}
	c->tx_done = 0;
	REG32(c->base + MB_FIFO_CNT) = n;
	for (unsigned int i = 0; i < n; i++)
		REG32(c->base + MB_TX_FIFO) = msg[i];
	REG32(c->base + MB_TX_INT_EN) |= 0x08;

	if (c->cl->tx_block != 1)
		return 0;
	/* Called from interrupt context too: poll in 2 ms steps */
	left = c->cl->tx_timeout_ms ? (int)(c->cl->tx_timeout_ms >> 1) : 50;
	do {
		timer_busy_wait_ms(2);
		left--;
		if (c->tx_done == 1)
			break;
	} while (left > 0);
	if (left < 1) {
		log_printf(LOG_INFO, "Mbox Send time out!\n");
		return -3;
	}
	return 0;
}
