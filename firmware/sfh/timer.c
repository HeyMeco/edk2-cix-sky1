/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * SFH timers: register block n at SFH_TIMER_BASE + n * 0x1000; the 64-bit
 * timers are pairs of blocks (0/1, 2/3), the 32-bit ones blocks 4..7.
 * Register use as in the CIX image: 0x00 load, 0x1c counter, 0x20 second
 * load (reload low word for 64-bit pairs), 0x24 control (bit 17 enable),
 * 0x30 status, 0x34 status clear.
 */
#include "sfh.h"

#define T_LOAD		0x00
#define T_COUNT		0x1c
#define T_LOAD2		0x20
#define T_CTRL		0x24
#define T_STATUS	0x30
#define T_CLEAR		0x34
#define T_CTRL_EN	(1u << 17)

struct timer {
	uint32_t	base;
	uint32_t	ctrl_block;	/* SFH_CTRL */
	uint32_t	mode;
	uint32_t	index;
};

static struct timer timer64[2];
static struct timer timer32[4];
static volatile uint32_t timer32_fired[4];
static volatile uint32_t timer32_oneshot[4];

static uint32_t blk64(const struct timer *t, int half)
{
	return t->base + (2 * t->index + half) * 0x1000u;
}

static uint32_t blk32(const struct timer *t)
{
	return t->base + (t->index + 4) * 0x1000u;
}

static void t64_clear(struct timer *t)
{
	for (int h = 0; h < 2; h++) {
		uint32_t st = REG32(blk64(t, h) + T_STATUS);

		if (st)
			REG32(blk64(t, h) + T_CLEAR) = st;
	}
}

static void t64_irq(void *arg)
{
	t64_clear(arg);
}

static void t64_set_load(struct timer *t, uint64_t v)
{
	REG32(blk64(t, 0) + T_LOAD) = (uint32_t)v;
	REG32(blk64(t, 1) + T_LOAD) = (uint32_t)(v >> 32);
}

static void t64_enable(struct timer *t, int on)
{
	for (int h = 0; h < 2; h++) {
		uint32_t c = REG32(blk64(t, h) + T_CTRL);

		REG32(blk64(t, h) + T_CTRL) = on ? (c | T_CTRL_EN) : (c & ~T_CTRL_EN);
	}
	t64_clear(t);
}

static void t64_setup(struct timer *t)
{
	t->ctrl_block = SFH_CTRL;
	t->base = SFH_TIMER_BASE;
	t64_set_load(t, 0);
	/* both halves get the low word, as in the CIX image */
	REG32(blk64(t, 0) + T_LOAD2) = 0xffffffffu;
	REG32(blk64(t, 1) + T_LOAD2) = 0xffffffffu;
	REG32(blk64(t, 0) + T_CTRL) = 0x9032;
	REG32(blk64(t, 1) + T_CTRL) = 0x9032;
}

static void t32_clear(struct timer *t)
{
	uint32_t st = REG32(blk32(t) + T_STATUS);

	if (st)
		REG32(blk32(t) + T_CLEAR) = st;
}

static void t32_enable(struct timer *t, int on)
{
	uint32_t c = REG32(blk32(t) + T_CTRL);

	REG32(blk32(t) + T_CTRL) = on ? (c | T_CTRL_EN) : (c & ~T_CTRL_EN);
	t32_clear(t);
}

static void t32_config(struct timer *t)
{
	uint32_t c = (t->mode & ~0x1000u) | 0x30;

	if (t->mode == 2)
		c = (t->mode & ~0x1000u) | 0x8030;
	else if (t->mode == 0)
		c = (t->mode & ~0x1000u) | 0xb0;
	REG32(blk32(t) + T_CTRL) = c | 0x40;
}

static void t32_reset(struct timer *t)
{
	uint32_t c = REG32(blk32(t) + T_CTRL);

	REG32(blk32(t) + T_CTRL) = c & 0xfffdffbf;
	REG32(blk32(t) + T_CTRL) = (c & 0xfffdffbf) | 0x40000;
	REG32(blk32(t) + T_CTRL) = c & 0xfff9ffbf;
}

static void t32_setup(struct timer *t)
{
	t->ctrl_block = SFH_CTRL;
	t->base = SFH_TIMER_BASE;
	REG32(blk32(t) + T_LOAD) = 0;
	REG32(blk32(t) + T_LOAD2) = 0;
	t32_config(t);
}

static void t32_irq(void *arg)
{
	struct timer *t = arg;

	timer32_fired[t->index] = 1;
	t32_clear(t);
	if (timer32_oneshot[t->index] == 1)
		t32_enable(t, 0);
}

void timer_init(void)
{
	log_printf(LOG_DEBUG, "start to init timer\n");
	REG32(SFH_CLK_EN) |= 0x408;
	REG32(SFH_CLK_EN2) |= 0x2000;

	for (uint32_t i = 0; i < 2; i++) {
		struct timer *t = &timer64[i];

		t->index = i;
		t->mode = 0;
		irq_register(IRQ_TIMER64(i), t64_irq, t);
		irq_enable(IRQ_TIMER64(i));
		t64_setup(t);
		t64_set_load(t, 250000000u);
		t64_enable(t, 0);
	}
	for (uint32_t i = 0; i < 4; i++) {
		struct timer *t = &timer32[i];

		t->index = i;
		t->mode = 0;
		irq_register(IRQ_TIMER32(i), t32_irq, t);
		irq_enable(IRQ_TIMER32(i));
		t32_setup(t);
		REG32(blk32(t) + T_LOAD) = 250000000u;
		t32_enable(t, 0);
	}
}

/* One-shot on 32-bit timer 1, polled: also usable inside interrupts */
void timer_busy_wait_ms(uint32_t ms)
{
	struct timer *t = &timer32[1];

	timer32_oneshot[1] = 1;
	t->index = 1;
	t->mode = 0;
	t->base = SFH_TIMER_BASE;
	t32_reset(t);
	t32_setup(t);
	t32_config(t);
	REG32(blk32(t) + T_LOAD) = ms * (SFH_TIMER_HZ / 1000);
	REG32(blk32(t) + T_LOAD2) = 0;
	t32_enable(t, 1);
	while (REG32(blk32(t) + T_COUNT) > 0xfe)
		;
}
