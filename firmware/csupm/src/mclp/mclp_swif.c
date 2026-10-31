/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The DDR controllers' software interface (CIX's Source/common/mclp/
 * mclp_swif.c): registers, automatic low-power entry and exit, and the
 * low-power commands: a command goes to LP_CMD (DENALI_CTL_175, +0x2bc
 * [22:16]); LP_STATE (DENALI_CTL_190, +0x2f8 [14:8]) has bit 6 set when
 * the controller took it, and the state it reached in [5:0]; the
 * controller's low-power interrupt is acknowledged after each.
 */
#include "mclp/mclp.h"
#include "clk/clk.h"
#include "lcru.h"

#define DDRC_BASE(ch)		(0x4c000000u + 0x20000u * (ch))	/* g_rcsu_id2base[20 + ch] */
#define TZC_BASE(ch)		(0x4c0a0000u + 0x20000u * (ch))	/* g_rcsu_id2base[25 + ch] */
#define REGS_OFFSET		0x10000u
#define SOC			0x40000000u

#define LP_CMD			0x2bc
#define LP_STATE		0x2f8		/* DENALI_CTL_190 */
#define LP_STATE_DONE		BIT(14)
#define LP_INT_LOWPWR		2		/* _s_intmap's "LowPwrCtrl" */

uint32_t g_chEnMask = 0xf;

/* CIX's _s_lp_SwCmdStMap: the state each command reaches */
static const struct {
	uint8_t lp_cmd, lp_state;
} lp_cmd_state[] = {
	{ 0x02, 0 }, { 0x03, 1 }, { 0x23, 2 }, { 0x05, 3 }, { 0x25, 4 },
	{ 0x09, 11 }, { 0x29, 12 }, { 0x11, 13 }, { 0x31, 14 }, { 0x51, 15 },
};

/* CIX's _s_intmap: the controller's interrupt groups (status, mask, ack) */
static const struct {
	uint32_t active_field_mask, reg_int_status, reg_int_mask, reg_int_ack;
} intmap[16] = {
	[0] = { 0xffffffff, 0x57c, 0x5c4, 0x5a0 },	/* timeout */
	[1] = { 0xffffffff, 0x580, 0x5c8, 0x5a4 },	/* ECC */
	[2] = { 0x0000ffff, 0x584, 0x5cc, 0x5a8 },	/* low-power control */
	[5] = { 0xffffffff, 0x58c, 0x5d4, 0x5b0 },	/* training */
	[6] = { 0xffffffff, 0x590, 0x5d8, 0x5b4 },	/* user interface */
	[7] = { 0xffffffff, 0x594, 0x5dc, 0x5b8 },	/* misc logic */
};

static uintptr_t reg_addr(enum mem_reg_type type, uint32_t off, uint32_t ch)
{
	CLK_ASSERT(ch < MCLP_CHANNELS, 0xf14c0035);
	if (type == MEM_REG_DDRC)
		return (DDRC_BASE(ch) | SOC) + off + REGS_OFFSET;
	CLK_ASSERT(type == MEM_REG_TZC, 0xf14c0043);
	return (TZC_BASE(ch) | SOC) + off + REGS_OFFSET;
}

void mclp_swif_reg_read(enum mem_reg_type type, uint32_t off, uint32_t ch, uint32_t *v)
{
	*v = REG32(reg_addr(type, off, ch));
}

void mclp_swif_reg_write(enum mem_reg_type type, uint32_t off, uint32_t ch, uint32_t v)
{
	REG32(reg_addr(type, off, ch)) = v;
}

/* LP_AUTO_ENTRY_EN [19:16] off; the previous setting is returned */
uint32_t mclp_swif_disable_auto_enter_lp(uint32_t ch)
{
	uint32_t v;

	mclp_swif_reg_read(MEM_REG_DDRC, LP_STATE, ch, &v);
	mclp_swif_reg_write(MEM_REG_DDRC, LP_STATE, ch, v & 0xfff0ffffu);
	return (v >> 16) & 15;
}

/* LP_AUTO_EXIT_EN [27:24] off */
uint32_t mclp_swif_disable_auto_exit_lp(uint32_t ch)
{
	uint32_t v;

	mclp_swif_reg_read(MEM_REG_DDRC, LP_STATE, ch, &v);
	mclp_swif_reg_write(MEM_REG_DDRC, LP_STATE, ch, v & 0xf0ffffffu);
	return (v >> 24) & 15;
}

void mclp_swif_enable_auto_enter_lp(uint32_t ch, uint32_t mode)
{
	uint32_t v;

	mclp_swif_reg_read(MEM_REG_DDRC, LP_STATE, ch, &v);
	mclp_swif_reg_write(MEM_REG_DDRC, LP_STATE, ch, (v & 0xfff0ffffu) + ((mode & 15) << 16));
}

/* the previous command done, within timeout_us (1 us steps) */
bool mclp_swif_poll_cmd_complate(uint32_t ch, uint32_t timeout_us)
{
	uint32_t start = ccount(), v;

	for (;;) {
		mclp_swif_reg_read(MEM_REG_DDRC, LP_STATE, ch, &v);
		if (v & LP_STATE_DONE)
			return true;
		us_delay(1);
		if (ccount() - start >= core_hz / 1000000 * timeout_us)
			return false;
	}
}

/* the state the command reached, or -1 after timeout_us */
int32_t mclp_swif_poll_cmd_respond(uint32_t ch, uint32_t timeout_us)
{
	uint32_t start = ccount(), v;

	for (;;) {
		mclp_swif_reg_read(MEM_REG_DDRC, LP_STATE, ch, &v);
		if (v & LP_STATE_DONE)
			return (int32_t)((v >> 8) & 0x3f);
		us_delay(1);
		if (ccount() - start >= core_hz / 1000000 * timeout_us)
			return -1;
	}
}

void mclp_swif_set_lp_cmd(uint32_t ch, uint32_t cmd)
{
	uint32_t v;

	mclp_swif_reg_read(MEM_REG_DDRC, LP_CMD, ch, &v);
	mclp_swif_reg_write(MEM_REG_DDRC, LP_CMD, ch, (v & 0xff80ffffu) + ((cmd & 0x7f) << 16));
}

void mclp_swif_ack_interrupt(uint32_t ch, uint32_t idx)
{
	CLK_ASSERT(idx < 16, 0xf14c0219);
	mclp_swif_reg_write(MEM_REG_DDRC, intmap[idx].reg_int_ack, ch, intmap[idx].active_field_mask);
}

/*
 * CIX's mclp_swif_parallel_force_lp_cmd: on every channel at once, with
 * automatic entry off, wait for the controller, go to idle (command 2),
 * then send cmd and check each reaches its state; automatic entry back as
 * it was.  2 ms for each step.
 */
bool mclp_swif_parallel_force_lp_cmd(uint32_t cmd)
{
	uint32_t prev[MCLP_CHANNELS] = { 0 }, ch;
	int32_t state = -1;
	bool ok = false;

	for (ch = 0; ch < MCLP_CHANNELS; ch++) {
		if (g_chEnMask & BIT(ch))
			prev[ch] = mclp_swif_disable_auto_enter_lp(ch);
	}
	for (ch = 0; ch < MCLP_CHANNELS; ch++) {
		if ((g_chEnMask & BIT(ch)) && !mclp_swif_poll_cmd_complate(ch, 2000))
			goto out;
	}
	for (ch = 0; ch < MCLP_CHANNELS; ch++) {
		if (g_chEnMask & BIT(ch))
			mclp_swif_set_lp_cmd(ch, 2);
	}
	for (ch = 0; ch < MCLP_CHANNELS; ch++) {
		if (!(g_chEnMask & BIT(ch)))
			continue;
		if (mclp_swif_poll_cmd_respond(ch, 2000) != 0)
			goto out;
		mclp_swif_ack_interrupt(ch, LP_INT_LOWPWR);
	}
	for (ch = 0; ch < MCLP_CHANNELS; ch++) {
		if (g_chEnMask & BIT(ch))
			mclp_swif_set_lp_cmd(ch, cmd);
	}
	for (unsigned i = 0; i < ARRAY_SIZE(lp_cmd_state); i++) {
		if (lp_cmd_state[i].lp_cmd == cmd)
			state = lp_cmd_state[i].lp_state;
	}
	if (state < 0)
		goto out;
	for (ch = 0; ch < MCLP_CHANNELS; ch++) {
		if ((g_chEnMask & BIT(ch)) && mclp_swif_poll_cmd_respond(ch, 2000) != state)
			goto out;
	}
	for (ch = 0; ch < MCLP_CHANNELS; ch++) {
		if (g_chEnMask & BIT(ch))
			mclp_swif_ack_interrupt(ch, LP_INT_LOWPWR);
	}
	ok = true;
out:
	for (ch = 0; ch < MCLP_CHANNELS; ch++) {
		if (g_chEnMask & BIT(ch))
			mclp_swif_enable_auto_enter_lp(ch, prev[ch]);
	}
	return ok;
}
