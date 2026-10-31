/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * DRAM across suspend (CIX's Source/common/mclp/mclp_retention.c).
 *
 * While DRAM self-refreshes the controllers lose their state, and after a
 * wake-up they have to be set up again before DRAM can be read: so their
 * state is kept in PM SRAM.  ddr_s3_enter borrows five blocks of the PM's
 * RAM for it (4 x 0x7cf0 bytes, a channel's controller, PI and the PHY's
 * per-frequency, per-rank registers; then 0x400 bytes for the four TZC-400s),
 * saving what they held to DRAM (0x83610000, through a TLB window) first;
 * then DRAM goes to self-refresh (command 0x51) and the PHYs' clocks and
 * the DDR PLL's gates off.  ddr_s3_exit undoes it: controllers out of
 * reset, the DDR PLL locked, the state written back and checked against
 * its checksums, the controllers started, DRAM out of self-refresh, then
 * the borrowed RAM restored from DRAM (checked too).
 *
 * The blocks start at the higher of 0x237b4 bytes below the stack pointer
 * and the heap (CIX: its FreeRTOS heap), and step over 8 KB on either side
 * of the stack.  CIX also saves its overlays' 32 KB SRAM (0x83608000); the
 * open firmware has no overlays and does not use that SRAM.
 */
#include "mclp/mclp.h"
#include "clk/clk.h"
#include "lcru.h"
#include "rtt/rtt.h"

#define PHY_FREQ_SEL		0x5404		/* DENALI_PHY_1281 */
#define PHY_RANK(i)		(0x4018 + 0x400 * (i))	/* DENALI_PHY_6, 262, 518, 774 */
#define LP_STATE		0x2f8		/* DENALI_CTL_190 */
#define DDRC_CTRL(ch)		(0x4c000300u + 0x20000u * (ch))	/* controller reset, SoC window */
#define DDR_PLL			0xa0067000u
#define DDR_PLL_GATE_CTRL	0xa0067020u
#define S5_DDR_RESET		0x56000400u	/* bit 0 */
#define S5_DDR_RET		0x56000700u	/* bit ch: channel ch's IO retention */
#define SOC			0x40000000u

#define BLOCKS			5
#define BLOCK_DDRC		0x7cf0u
#define BLOCK_TZC		0x400u
#define STACK_SKIP		0x2000u

/* CIX's ddrc_190 and the PHY registers it keeps per channel */
static uint32_t ddrc_190[MCLP_CHANNELS];
static uint32_t phy_rank[4][MCLP_CHANNELS];
static uint32_t phy_1281[MCLP_CHANNELS];
static uint64_t ddrc_sum[MCLP_CHANNELS + 1];	/* + the TZCs' */

/* CIX's _s_backup_blk */
static struct backup_block {
	uint32_t *sram_base;
	uint32_t sram_size;
	uint64_t chksum;
	bool clear;
} backup_blk[BLOCKS];
static bool backup_blk_nr;			/* blocks saved */
static uint32_t ddr4pm;				/* the DRAM save area, PM address */

extern char _heap_start[], _stack_top[];

/*
 * CIX's double_check_sum: a Fletcher-like sum of 32-bit words (the high
 * word the running sum, the low word the sum of sums), stored or checked.
 */
bool double_check_sum(void *start, uint32_t length, uint64_t *sum, bool check)
{
	const volatile uint32_t *p = start;
	uint32_t a = 0, b = 0;

	CLK_ASSERT(!((uintptr_t)start & 3), 0xf1830236);
	for (uint32_t i = 0; i < length; i += 4) {
		a += p[i / 4];
		b += a;
	}
	if (!check) {
		if (sum)
			*sum = (uint64_t)a << 32 | b;
		return true;
	}
	return sum && (uint32_t)*sum == b && (uint32_t)(*sum >> 32) == a;
}

/* CIX's ddrc_memcpy: word by word, to or from registers */
static void ddrc_memcpy(uintptr_t dst, uintptr_t src, uint32_t n)
{
	for (uint32_t i = 0; i < n; i += 4)
		REG32(dst + i) = REG32(src + i);
}

/* CIX's tzc400_addr: (offset << 8) | registers */
static const uint32_t tzc400_addr[13] = {
	0x401, 0x801, 0xc01, 0x11001, 0x11401, 0x12006, 0x14006, 0x16006,
	0x18006, 0x1a006, 0x1c006, 0x1e006, 0x20006,
};

/* CIX's tzc400_channel_process: a channel's TZC-400 regions read or written */
static void tzc400_channel_process(uint32_t *buf, uint32_t ch, bool write)
{
	uint32_t k = 0;

	for (unsigned i = 0; i < ARRAY_SIZE(tzc400_addr); i++) {
		uint32_t off = tzc400_addr[i] >> 8;

		for (uint32_t n = tzc400_addr[i] & 0xff; n; n--, off += 4, k++) {
			if (write)
				mclp_swif_reg_write(MEM_REG_TZC, off, ch, buf[k]);	/* TZC400_WR */
			else
				mclp_swif_reg_read(MEM_REG_TZC, off, ch, &buf[k]);	/* TZC400_RD */
		}
	}
}

/* CIX's switch_opp_and_rank: the PHY's registers for a frequency copy
 * and a rank */
static void switch_opp_and_rank(uint32_t ch, uint32_t freq, uint32_t rank)
{
	phy_1281[ch] = (phy_1281[ch] & 0xfffcffffu) + ((freq & 3) << 16);
	mclp_swif_reg_write(MEM_REG_DDRC, PHY_FREQ_SEL, ch, phy_1281[ch]);
	cc_delay(200);
	for (unsigned i = 0; i < 4; i++) {
		phy_rank[i][ch] = (phy_rank[i][ch] & 0xfcfeffffu) + ((rank & 3) << 24);
		mclp_swif_reg_write(MEM_REG_DDRC, PHY_RANK(i), ch, phy_rank[i][ch]);
	}
	cc_delay(200);
}

/* CIX's ddr_sr_enter: self-refresh, long, with memory and controller
 * clocks gated */
bool ddr_sr_enter(void)
{
	return mclp_swif_parallel_force_lp_cmd(0x51);
}

/* a channel's registers in a block: the controller (word 0 without its
 * start bit), the PI likewise, the PHY for each frequency copy and rank */
#define BLK_CTL			0x0000u
#define BLK_CTL_LEN		0xb54u
#define BLK_PI			0x0b54u
#define BLK_PI_LEN		0x1a0cu
#define BLK_PHY(f, r)		(0x2560u + 0x15a4u * (2 * (1 - (f)) + (r)))
#define BLK_PHY_LEN		0x15a4u
#define REG_PHY			0x4000u

static uintptr_t ddrc_regs(uint32_t ch)
{
	return (0x4c000000u + 0x20000u * ch + 0x10000u) | SOC;
}

static void save_channel(uint32_t ch, uint32_t *blk)
{
	uintptr_t regs = ddrc_regs(ch), b = (uintptr_t)blk;
	uint32_t v;

	mclp_swif_reg_write(MEM_REG_DDRC, PHY_FREQ_SEL, ch, 0x10000);
	mclp_swif_reg_read(MEM_REG_DDRC, 0, ch, &v);
	blk[0] = v & ~1u;
	ddrc_memcpy(b + 4, regs + 4, BLK_CTL_LEN - 4);
	mclp_swif_reg_read(MEM_REG_DDRC, 0x2000, ch, &v);
	blk[BLK_PI / 4] = v & ~1u;
	ddrc_memcpy(b + BLK_PI + 4, regs + 0x2004, BLK_PI_LEN - 4);
	mclp_swif_reg_read(MEM_REG_DDRC, PHY_FREQ_SEL, ch, &phy_1281[ch]);
	for (unsigned i = 0; i < 4; i++)
		mclp_swif_reg_read(MEM_REG_DDRC, PHY_RANK(i), ch, &phy_rank[i][ch]);
	for (uint32_t f = 2; f--;) {
		for (uint32_t r = 0; r < 2; r++) {
			switch_opp_and_rank(ch, f, r);
			ddrc_memcpy(b + BLK_PHY(f, r), regs + REG_PHY, BLK_PHY_LEN);
		}
	}
	(void)double_check_sum(blk, BLOCK_DDRC, &ddrc_sum[ch], false);
}

static void restore_channel(uint32_t ch, uint32_t *blk)
{
	uintptr_t regs = ddrc_regs(ch), b = (uintptr_t)blk;

	mclp_swif_reg_write(MEM_REG_DDRC, PHY_FREQ_SEL, ch, 0x10100);
	ddrc_memcpy(regs, b + BLK_CTL, BLK_CTL_LEN);
	ddrc_memcpy(regs + 0x2000, b + BLK_PI, BLK_PI_LEN);
	for (uint32_t f = 2; f--;) {
		for (uint32_t r = 0; r < 2; r++) {
			switch_opp_and_rank(ch, f, r);
			ddrc_memcpy(regs + REG_PHY, b + BLK_PHY(f, r), BLK_PHY_LEN);
		}
	}
	phy_1281[ch] = (phy_1281[ch] & 0xfffcffffu) + 0x10000;
	mclp_swif_reg_write(MEM_REG_DDRC, PHY_FREQ_SEL, ch, phy_1281[ch]);
}

/* the blocks of PM RAM borrowed, out of the stack's way */
static void layout_blocks(void)
{
	uintptr_t sp = (uintptr_t)__builtin_frame_address(0);
	uintptr_t lo = sp - STACK_SKIP + 12, hi = sp + STACK_SKIP + 12;
	uintptr_t a = sp - 0x237b4u;
	bool moved = false;

	if (a < (uintptr_t)_heap_start)
		a = (uintptr_t)_heap_start;
	a = (a + 7) & ~(uintptr_t)7;
	for (unsigned i = 0; i < BLOCKS; i++) {
		uint32_t size = i == BLOCKS - 1 ? BLOCK_TZC : BLOCK_DDRC;

		if (!moved && a < hi && lo < a + size) {
			a = hi;
			moved = true;
		}
		a = (a + 7) & ~(uintptr_t)7;
		backup_blk[i].sram_base = (uint32_t *)a;
		backup_blk[i].sram_size = size;
		backup_blk[i].clear = false;
		a += size;
	}
	/* not CIX's: the blocks stay in RAM (pm.ld checks the heap's size) */
	CLK_ASSERT(a <= (uintptr_t)_stack_top, 0xf14b0927);
}

void ddr_s3_enter(void)
{
	uint32_t off = 0, v, ch;

	if (!ddr4pm) {
		ddr4pm = tlb_ctrl_dram_to_syshub(DDR4PM_DRAM, false);
		CLK_ASSERT(ddr4pm, 0xf14b0926);
	}
	for (ch = 0; ch < MCLP_CHANNELS; ch++) {
		if (!(g_chEnMask & BIT(ch)))
			continue;
		mclp_swif_reg_read(MEM_REG_DDRC, LP_STATE, ch, &ddrc_190[ch]);
		(void)mclp_swif_disable_auto_enter_lp(ch);
	}
	/* CIX: its overlays' 32 KB SRAM to 0x83608000 here */
	layout_blocks();
	backup_blk_nr = true;
	for (unsigned i = 0; i < BLOCKS; i++) {
		struct backup_block *b = &backup_blk[i];

		(void)double_check_sum(b->sram_base, b->sram_size, &b->chksum, false);
		ddrc_memcpy(ddr4pm + off, (uintptr_t)b->sram_base, b->sram_size);
		if (b->clear)
			memset(b->sram_base, 0, b->sram_size);
		off += b->sram_size;
	}
	for (ch = 0; ch < MCLP_CHANNELS; ch++) {
		if (g_chEnMask & BIT(ch))
			save_channel(ch, backup_blk[ch].sram_base);
	}
	for (ch = 0; ch < MCLP_CHANNELS; ch++) {
		if (g_chEnMask & BIT(ch))
			tzc400_channel_process(backup_blk[4].sram_base + 0x40 * ch, ch, false);
	}
	(void)double_check_sum(backup_blk[4].sram_base, BLOCK_TZC, &ddrc_sum[MCLP_CHANNELS], false);
	for (ch = 0; ch < MCLP_CHANNELS; ch++) {
		if (g_chEnMask & BIT(ch))
			(void)mclp_swif_disable_auto_exit_lp(ch);
	}
	(void)ddr_sr_enter();
	for (ch = 0; ch < MCLP_CHANNELS; ch++) {
		if (g_chEnMask & BIT(ch))
			v = REG32((ddrc_regs(ch)) + LP_STATE);
	}
	REG32(S5_DDR_RET) |= 0xf;
	for (ch = 0; ch < MCLP_CHANNELS; ch++) {
		if (g_chEnMask & BIT(ch))
			v = REG32((ddrc_regs(ch)) + LP_STATE);
	}
	(void)v;
	REG32(DDR_PLL_GATE_CTRL) &= 0xffffeeeeu;
	REG32(S5_DDR_RESET) &= ~1u;
}

void ddr_s3_exit(void)
{
	uint32_t off = 0, v, ch;
	bool ok = true;

	for (ch = 0; ch < MCLP_CHANNELS; ch++)
		REG32(DDRC_CTRL(ch) | SOC) = (REG32(DDRC_CTRL(ch) | SOC) >> 2 << 2) + 1;
	(void)pll_ctrl_wait_for_lock((void *)DDR_PLL, 1000);
	REG32(S5_DDR_RESET) |= 1;
	us_delay(1);
	REG32(S5_DDR_RESET) &= ~1u;
	us_delay(1);
	REG32(S5_DDR_RESET) |= 1;
	CLK_ASSERT(double_check_sum(backup_blk[4].sram_base, BLOCK_TZC, &ddrc_sum[MCLP_CHANNELS], true),
		   0xf14b0482);
	for (ch = 0; ch < MCLP_CHANNELS; ch++) {
		if (g_chEnMask & BIT(ch))
			tzc400_channel_process(backup_blk[4].sram_base + 0x40 * ch, ch, true);
	}
	for (ch = 0; ch < MCLP_CHANNELS; ch++) {
		if (!(g_chEnMask & BIT(ch)))
			continue;
		ok &= double_check_sum(backup_blk[ch].sram_base, BLOCK_DDRC, &ddrc_sum[ch], true);
		restore_channel(ch, backup_blk[ch].sram_base);
	}
	CLK_ASSERT(ok, 0xf14b0560);
	for (ch = 0; ch < MCLP_CHANNELS; ch++) {
		if (!(g_chEnMask & BIT(ch)))
			continue;
		mclp_swif_reg_read(MEM_REG_DDRC, 0x544c, ch, &v);
		mclp_swif_reg_write(MEM_REG_DDRC, 0x544c, ch, v | 1);
	}
	/* each controller started, DRAM out of self-refresh, IO retention off */
	for (ch = 0; ch < MCLP_CHANNELS; ch++) {
		if (!(g_chEnMask & BIT(ch)))
			continue;
		mclp_swif_reg_read(MEM_REG_DDRC, 0x20, ch, &v);
		mclp_swif_reg_write(MEM_REG_DDRC, 0x20, ch, v | 1);
		mclp_swif_reg_read(MEM_REG_DDRC, 0x20, ch, &v);
		mclp_swif_reg_write(MEM_REG_DDRC, 0x20, ch, v | BIT(16));
		mclp_swif_reg_read(MEM_REG_DDRC, 0x200c, ch, &v);
		mclp_swif_reg_write(MEM_REG_DDRC, 0x200c, ch, v & ~BIT(16));
		mclp_swif_reg_read(MEM_REG_DDRC, 0x25d8, ch, &v);
		mclp_swif_reg_write(MEM_REG_DDRC, 0x25d8, ch, v | 0x100);
		mclp_swif_reg_read(MEM_REG_DDRC, 0x25d8, ch, &v);
		mclp_swif_reg_write(MEM_REG_DDRC, 0x25d8, ch, v | 1);
		mclp_swif_reg_read(MEM_REG_DDRC, 0x25c8, ch, &v);
		mclp_swif_reg_write(MEM_REG_DDRC, 0x25c8, ch, v | BIT(24));
		us_delay(1);
		mclp_swif_reg_read(MEM_REG_DDRC, 0x2000, ch, &v);
		mclp_swif_reg_write(MEM_REG_DDRC, 0x2000, ch, v | 1);
		us_delay(1);
		mclp_swif_reg_read(MEM_REG_DDRC, 0, ch, &v);
		mclp_swif_reg_write(MEM_REG_DDRC, 0, ch, v | 1);
		cc_delay(350);
		REG32(S5_DDR_RET) = ~1u << ch;
	}
	/* each controller's initialisation done (10 ms): its interrupt masked */
	for (ch = 0; ch < MCLP_CHANNELS; ch++) {
		uint32_t start;

		if (!(g_chEnMask & BIT(ch)))
			continue;
		start = ccount();
		for (;;) {
			mclp_swif_reg_read(MEM_REG_DDRC, 0x59c, ch, &v);
			if (v & BIT(9)) {
				mclp_swif_reg_read(MEM_REG_DDRC, 0x5c0, ch, &v);
				mclp_swif_reg_write(MEM_REG_DDRC, 0x5c0, ch, v | 0x200);
				break;
			}
			us_delay(10);
			if (ccount() - start >= core_hz / 1000000 * 10000)
				break;		/* CIX logs the timeout */
		}
	}
	ddr4pm = tlb_ctrl_dram_to_syshub(DDR4PM_DRAM, false);
	CLK_ASSERT(ddr4pm, 0xf14b0217);
	ok = true;
	for (unsigned i = 0; backup_blk_nr && i < BLOCKS; i++) {
		struct backup_block *b = &backup_blk[i];

		ddrc_memcpy((uintptr_t)b->sram_base, ddr4pm + off, b->sram_size);
		off += b->sram_size;
		ok &= double_check_sum(b->sram_base, b->sram_size, &b->chksum, true);
	}
	CLK_ASSERT(ok, 0xf14b0265);
	/* CIX: its overlays' 32 KB SRAM back from 0x83608000 */
	for (ch = 0; ch < MCLP_CHANNELS; ch++) {
		if (g_chEnMask & BIT(ch))
			mclp_swif_reg_write(MEM_REG_DDRC, LP_STATE, ch, ddrc_190[ch]);
	}
}
