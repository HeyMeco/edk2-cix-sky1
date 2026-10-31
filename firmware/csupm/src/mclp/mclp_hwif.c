/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The DDR controllers' hardware interface (CIX's Source/common/mclp/
 * mclp_hwif.c): the frequency change (DFS) through the PM's DDR clock
 * control (0xa0000600 + 4 * ch: [3:2] the frequency copy, bit 1 ready,
 * bit 10 a pulse to request, bit 30 busy, bit 31 go, bit 11 done) and the
 * DDR PLL's second post divider, and the channels' last low-power state.
 */
#include "mclp/mclp.h"
#include "clk/clk.h"
#include "lcru.h"
#include "rtt/rtt.h"

#define DFS_CTRL(ch)		(0xa0000600u + 4 * (ch))
#define DDR_PLL			0xa0067000u
#define DDR_PLL_GATE		0xa006701cu
#define DDR_PLL_GATE_CTRL	0xa0067020u
#define DENALI_CTL_198		0x318		/* [1:0] current copy, [9:8] previous */
#define DENALI_DFS_CAP		0x1ca4		/* [2:1] both set: DFS possible */
#define LP_STATE_SRPD_SHORT	11

/* CIX's lp_cmd: each channel's last expected state, its last command */
static struct {
	uint8_t last_state[MCLP_CHANNELS];
	uint8_t last_cmd[MCLP_CHANNELS];
} lp_cmd;

/* scratch words other agents read: the longest vblank wait and DFS */
#define SCRATCH_DFS_STEP	(PM_SCRATCH + 0x2c)
#define SCRATCH_DFS_MAX		(PM_SCRATCH + 0x50)
#define SCRATCH_DFS_COUNT	(PM_SCRATCH + 0x54)
#define SCRATCH_VBLANK_MAX	(PM_SCRATCH + 0x60)

void mclp_hwif_init(void)
{
	for (unsigned ch = 0; ch < MCLP_CHANNELS; ch++) {
		lp_cmd.last_cmd[ch] = 0;
		lp_cmd.last_state[ch] = LP_STATE_SRPD_SHORT;
	}
}

/* after power-on a channel is in short self-refresh: now idle */
bool mclp_hwif_power_on_reset(uint32_t ch)
{
	if (lp_cmd.last_state[ch] != LP_STATE_SRPD_SHORT)
		return false;
	lp_cmd.last_state[ch] = 0;
	return true;
}

/* CIX's mclp_hwif_dfs: every channel to frequency copy copy_n */
bool mclp_hwif_dfs(uint32_t copy_n)
{
	uint32_t ch;

	for (ch = 0; ch < MCLP_CHANNELS; ch++) {
		if (g_chEnMask & BIT(ch))
			REG32(DFS_CTRL(ch)) = (REG32(DFS_CTRL(ch)) & 0xfffffc02u) | (copy_n & 3) << 2 | 0x141;
	}
	us_delay(1);
	for (ch = 0; ch < MCLP_CHANNELS; ch++) {
		while ((g_chEnMask & BIT(ch)) && !(REG32(DFS_CTRL(ch)) & BIT(1)))
			;
	}
	for (ch = 0; ch < MCLP_CHANNELS; ch++) {
		if (g_chEnMask & BIT(ch)) {
			uint32_t v = REG32(DFS_CTRL(ch));

			REG32(DFS_CTRL(ch)) = v | BIT(10);
			REG32(DFS_CTRL(ch)) = v & ~BIT(10);
		}
	}
	for (ch = 0; ch < MCLP_CHANNELS; ch++) {
		while ((g_chEnMask & BIT(ch)) && !(REG32(DFS_CTRL(ch)) & BIT(30)))
			;
	}
	REG32(DDR_PLL_GATE_CTRL) = 0;
	pll_ctrl_set_POSTDIV2_with_gating(copy_n == 1 ? 4 : 1, (void *)DDR_PLL, DDR_PLL_GATE, 0x11111111);
	REG32(DDR_PLL_GATE_CTRL) = 0x1111;
	for (ch = 0; ch < MCLP_CHANNELS; ch++) {
		if (g_chEnMask & BIT(ch))
			REG32(DFS_CTRL(ch)) |= BIT(31);
	}
	for (ch = 0; ch < MCLP_CHANNELS; ch++) {
		while ((g_chEnMask & BIT(ch)) && (REG32(DFS_CTRL(ch)) & BIT(30)))
			;
	}
	for (ch = 0; ch < MCLP_CHANNELS; ch++) {
		if (g_chEnMask & BIT(ch))
			REG32(DFS_CTRL(ch)) &= ~BIT(31);
	}
	for (ch = 0; ch < MCLP_CHANNELS; ch++) {
		while ((g_chEnMask & BIT(ch)) && !(REG32(DFS_CTRL(ch)) & BIT(11)))
			;
	}
	for (ch = 0; ch < MCLP_CHANNELS; ch++) {
		if (g_chEnMask & BIT(ch))
			REG32(DFS_CTRL(ch)) &= ~1u;
	}
	return true;
}

/*
 * CIX's mclp_hwif_parallel_run_lp_cmd: a frequency change on every
 * channel with automatic low power off, aligned to the displays' vertical
 * blank when asked (CIX's DP/DPU driver; not ported: no alignment), the
 * copy each controller reports checked.  lp_cmd and timeout_us are not
 * used (CIX's neither).
 */
bool mclp_hwif_parallel_run_lp_cmd(uint32_t lp_cmd_unused, uint32_t timeout_us, uint32_t copy_n,
				   bool align_to_vblank)
{
	uint32_t prev[MCLP_CHANNELS] = { 0 }, ch, v;
	uint64_t t0;
	bool ok = true;

	(void)lp_cmd_unused;
	(void)timeout_us;
	(void)align_to_vblank;
	for (ch = 0; ch < MCLP_CHANNELS; ch++) {
		if (g_chEnMask & BIT(ch))
			prev[ch] = mclp_swif_disable_auto_enter_lp(ch);
	}
	(void)tlb_ctrl_dram_to_syshub(DDR4PM_DRAM, false);	/* CIX's, unused */
	t0 = arch_getTimeNowUs();
	REG32(SCRATCH_DFS_STEP) = 0xf14a0841;
	(void)mclp_hwif_dfs(copy_n);
	REG32(SCRATCH_DFS_STEP) = 0xf14a0848;
	for (ch = 0; ch < MCLP_CHANNELS; ch++) {
		if (!(g_chEnMask & BIT(ch)))
			continue;
		mclp_swif_reg_read(MEM_REG_DDRC, DENALI_CTL_198, ch, &v);
		if ((v & 3) != copy_n || ((v >> 8) & 3) != copy_n - 1)
			ok = false;
	}
	v = (uint32_t)(arch_getTimeNowUs() - t0);
	if (REG32(SCRATCH_DFS_MAX) < v)
		REG32(SCRATCH_DFS_MAX) = v;
	REG32(SCRATCH_DFS_COUNT) += 1;
	for (ch = 0; ch < MCLP_CHANNELS; ch++) {
		if (g_chEnMask & BIT(ch))
			mclp_swif_enable_auto_enter_lp(ch, prev[ch]);
	}
	return ok;
}

bool mclp_hwif_is_dfs_supported(void)
{
	for (uint32_t ch = 0; ch < MCLP_CHANNELS; ch++) {
		uint32_t v = 0;

		if (!(g_chEnMask & BIT(ch)))
			continue;
		mclp_swif_reg_read(MEM_REG_DDRC, DENALI_DFS_CAP, ch, &v);
		if ((v & 6) != 6)
			return false;
	}
	return true;
}
