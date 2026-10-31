/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * CIX's pll_ctrl.c translated: the registers of a PLL, and the PCIe PHY
 * PLL with its reference clock outputs (a custom clock node).
 *
 * A PLL's registers (pll_base):
 *   +0x0  FBDIV (11:0)
 *   +0x4  FRAC (23:0), PLLEN (24), DSMEN (28)
 *   +0x8  POSTDIV1 (2:0), POSTDIV2 (6:4), REFDIV (13:8)
 *   +0xc  BYPASS, DACEN, FOUT4PHASEEN, FOUTPOSTDIVEN, lock (4), ...
 */
#include <stdlib.h>

#include "clk.h"


#define PLL_REG(base, off)	REG32((uintptr_t)(base) + (off))
#define PLL_LOCK_TIMEOUT_US	1000

/*
 * The PCIe PHY PLL (node CLK_TREE_PCIE_PHY_PLL, index 0 here) and its
 * outputs: the reference clocks of 8 PCIe blocks and PHYs (1-8) and the
 * GMAC recovered clock (9), nodes CLK_TREE_PCIE_PHY_PLL + index.
 */
#define PCIE_PHY_CTRL		0xa0066100	/* power-downs (4:1) */
#define PCIE_PHY_DIV_REF	0xa0066144	/* the 25 MHz output's divider */
#define PCIE_PHY_DIV_PCIE	0xa0066148	/* the 100 MHz outputs' divider */
#define PCIE_PHY_EN		0xa006614c	/* PLL enable (0), GMAC clock (1) */
#define PCIE_PHY_BUF		0xa0066154	/* diff buffer enable (0) */
#define PCIE_PHY_BUF_CTRL	0xa0066158	/* + 4 * index: output enables */
#define PCIE_PHY_OUT_ON		0x11100
#define PCIE_PHY_HZ		2400000000u
#define PCIE_PHY_NUM		10

/*
 * The nodes under PLLs whose post divider is set here, refreshed after a
 * change.
 */
static struct {
	uint32_t pll_base;
	CLK_TREE_ENUM_IDX node_idx;
	TREE_NODE *node_ptr;
} postdiv_refres_nodes[3] = {
	{ 0xa0060500, CLK_TREE_DSU_PLL, NULL },
	{ 0xa0062000, CLK_TREE_MMHUB_PLL, NULL },
	{ 0xa0065200, CLK_TREE_CI700_PLL, NULL },
};

/* POSTDIV2 changes with the output gated (gate_mask bits cleared) */
void pll_ctrl_set_POSTDIV2_with_gating(uint32_t postdiv2, void *pll_base, uint32_t gate_base,
				       uint32_t gate_mask)
{
	uint32_t gate = REG32(gate_base), p3 = PLL_REG(pll_base, 8);
	uint32_t i;

	if (((p3 >> 4) & 7) != postdiv2) {
		REG32(gate_base) = gate & ~gate_mask;
		PLL_REG(pll_base, 8) = (p3 & ~0x70u) + ((postdiv2 & 7) << 4);
		REG32(gate_base) = gate;
	}

	for (i = 0; i < ARRAY_SIZE(postdiv_refres_nodes); i++) {
		if (postdiv_refres_nodes[i].pll_base == (uint32_t)(uintptr_t)pll_base)
			break;
	}
	if (i == ARRAY_SIZE(postdiv_refres_nodes))
		return;
	if (postdiv_refres_nodes[i].node_ptr == NULL) {
		CLK_TREE_STRUCT *st = clk_nodes_get(postdiv_refres_nodes[i].node_idx);

		postdiv_refres_nodes[i].node_ptr = &st->clk_tree_node;
	}
	if (tree_is_any_node_locked(postdiv_refres_nodes[i].node_ptr))
		return;
	tree_traverse(postdiv_refres_nodes[i].node_ptr, clk_tree_calc_state_of_node);
}

uint32_t pll_ctrl_set_freq(uint32_t desired, void *pll_base)
{
	uint32_t fbdiv, frac, refdiv, pd1, pd2, p1, p2, p3, err;

	err = pll_solve_freq_settings(false, desired, &fbdiv, &frac, &refdiv, &pd1, &pd2);
	CLK_ASSERT(err < 25, 0xf1590029);
	p1 = (PLL_REG(pll_base, 0) & ~0xfffu) + (fbdiv & 0xfff);
	p2 = (PLL_REG(pll_base, 4) & 0xff000000) + (frac & 0xffffff);
	p3 = (PLL_REG(pll_base, 8) & 0xffffc088) + (pd1 & 7) + ((pd2 & 7) << 4) +
	     ((refdiv & 0x3f) << 8);
	PLL_REG(pll_base, 0) = p1;
	PLL_REG(pll_base, 4) = p2;
	PLL_REG(pll_base, 8) = p3;
	if (pll_ctrl_get_state(pll_base))
		pll_ctrl_wait_for_lock(pll_base, PLL_LOCK_TIMEOUT_US);
	return pll_calc_freq_FoutPOSTDIV(fbdiv, frac, refdiv, pd1 * pd2);
}

/* timeout_us 0 waits for ever */
bool pll_ctrl_wait_for_lock(void *pll_base, uint32_t timeout_us)
{
	uint64_t t0, now;

	if (PLL_REG(pll_base, 0xc) & 0x10)
		return true;
	t0 = arch_getTimeNowUs();
	for (;;) {
		cc_delay(200);
		if (PLL_REG(pll_base, 0xc) & 0x10)
			return true;
		now = arch_getTimeNowUs();
		if (timeout_us && now - t0 >= timeout_us)
			return false;
	}
}

uint64_t pll_ctrl_set_FoutVCO(uint64_t desired_vco_freq, void *pll_base)
{
	uint32_t fbdiv, frac, refdiv, p1, p2, p3, err;

	if (desired_vco_freq == 1599999999) {
		desired_vco_freq = 1600000000;
	} else {
		CLK_ASSERT(desired_vco_freq >= 1600000000, 0xf1590075);
		CLK_ASSERT(desired_vco_freq <= 6400000000ull, 0xf1590076);
	}
	err = pll_solve_vco_settings(false, desired_vco_freq, &fbdiv, &frac, &refdiv);
	CLK_ASSERT(err < 25, 0xf1590083);
	p1 = (PLL_REG(pll_base, 0) & ~0xfffu) + (fbdiv & 0xfff);
	p2 = (PLL_REG(pll_base, 4) & 0xff000000) + (frac & 0xffffff);
	p3 = (PLL_REG(pll_base, 8) & 0xffffc0ff) + ((refdiv & 0x3f) << 8);
	PLL_REG(pll_base, 0) = p1;
	PLL_REG(pll_base, 4) = p2;
	PLL_REG(pll_base, 8) = p3;
	if (pll_ctrl_get_state(pll_base))
		pll_ctrl_wait_for_lock(pll_base, PLL_LOCK_TIMEOUT_US);
	return pll_calc_freq_FoutVCO(fbdiv, frac, refdiv);
}

uint32_t pll_ctrl_set_FoutPOSTDIV(uint32_t desired_postdiv_freq, void *pll_base)
{
	uint64_t vco = pll_ctrl_get_vco_freq(pll_base);
	uint32_t pd1, pd2;

	pll_solve_postdiv_settings(vco, desired_postdiv_freq, &pd1, &pd2);
	PLL_REG(pll_base, 8) = (PLL_REG(pll_base, 8) & ~0x77u) + (pd1 & 7) + ((pd2 & 7) << 4);
	return (uint32_t)(vco / (pd2 * pd1));
}

uint64_t pll_ctrl_get_vco_freq(void *pll_base)
{
	uint32_t p1 = PLL_REG(pll_base, 0), p2 = PLL_REG(pll_base, 4), p3 = PLL_REG(pll_base, 8);
	uint64_t vco = pll_calc_freq_FoutVCO(p1 & 0xfff, p2 & 0xffffff, (p3 >> 8) & 0x3f);

	CLK_ASSERT(vco >= 1599999999 && vco <= 6400000000ull, 0xf1590233);
	return vco;
}

uint32_t pll_ctrl_set_POSTDIV12(uint32_t postdiv1, uint32_t postdiv2, void *pll_base)
{
	PLL_REG(pll_base, 8) = ((postdiv2 & 7) << 4) + (postdiv1 & 7) +
			       (PLL_REG(pll_base, 8) & ~0x77u);
	return (uint32_t)(pll_ctrl_get_vco_freq(pll_base) / (postdiv2 * postdiv1));
}

uint32_t pll_ctrl_get_postdiv_freq(void *pll_base)
{
	uint32_t p1 = PLL_REG(pll_base, 0), p2 = PLL_REG(pll_base, 4), p3 = PLL_REG(pll_base, 8);

	return pll_calc_freq_FoutPOSTDIV(p1 & 0xfff, p2 & 0xffffff, (p3 >> 8) & 0x3f,
					 ((p3 >> 4) & 7) * (p3 & 7));
}

uint32_t pll_ctrl_get_postdiv_num(void *pll_base)
{
	uint32_t p3 = PLL_REG(pll_base, 8);

	return ((p3 >> 4) & 7) * (p3 & 7);
}

uint32_t pll_ctrl_get_postdiv1(void *pll_base)
{
	return PLL_REG(pll_base, 8) & 7;
}

uint32_t pll_ctrl_get_postdiv2(void *pll_base)
{
	return (PLL_REG(pll_base, 8) >> 4) & 7;
}

void pll_ctrl_set_state(uint32_t enable, void *pll_base)
{
	PLL_REG(pll_base, 4) = (PLL_REG(pll_base, 4) & ~BIT(24)) + ((enable != 0) << 24);
}

bool pll_ctrl_is_lock(void *pll_base)
{
	return (PLL_REG(pll_base, 0xc) & 0x10) >> 4;
}

uint32_t pll_ctrl_get_ctrlword(uint32_t offset, void *pll_base)
{
	if (offset > 16 || pll_base == NULL)
		return ~0u;
	return PLL_REG(pll_base, offset);
}

void pll_ctrl_set_ctrlword(uint32_t offset, void *pll_base, uint32_t ctrl_word)
{
	if (offset > 16 || pll_base == NULL)
		return;
	PLL_REG(pll_base, offset) = ctrl_word;
}

/* --- the PCIe PHY PLL ------------------------------------------------------ */

/* The PLL is enabled and not powered down */
static bool get_pcie_phy_pll_state(void)
{
	uint32_t ctrl = REG32(PCIE_PHY_CTRL), en = REG32(PCIE_PHY_EN);

	return en & ((ctrl & 0x16) == 0);
}

static bool get_diff_buf_stat(void)
{
	uint32_t buf = REG32(PCIE_PHY_BUF), ctrl = REG32(PCIE_PHY_BUF_CTRL);

	return buf & ((ctrl & 0x11) == 0x11);
}

static bool pcie_phy_clk_get_state(uint32_t idx)
{
	bool pll, buf;
	uint32_t out;

	CLK_ASSERT(idx < PCIE_PHY_NUM, 0xf1590464);
	pll = get_pcie_phy_pll_state();
	if (idx == 0)
		return pll;
	if (idx == 9)
		return (REG32(PCIE_PHY_EN) & 2) >> 1;
	buf = get_diff_buf_stat();
	out = REG32(PCIE_PHY_BUF_CTRL + 4 * idx);
	return buf & ((out & PCIE_PHY_OUT_ON) == PCIE_PHY_OUT_ON);
}

/* 2.4 GHz over the divider (0: undivided) */
static void pcie_phy_clk_get_freq(uint32_t idx, uint32_t *freq)
{
	uint32_t div = REG32(idx < 9 ? PCIE_PHY_DIV_PCIE : PCIE_PHY_DIV_REF) & 0xff;

	*freq = div ? PCIE_PHY_HZ / 2 / div : PCIE_PHY_HZ;
}

/* Refresh the state and rate of all ten nodes from the hardware */
static void pcie_phy_clk_update_state(uint32_t idx)
{
	bool running = get_pcie_phy_pll_state() && get_diff_buf_stat();
	uint32_t f_pcie, f_ref, i;
	CLK_TREE_STRUCT *st;
	bool on;

	(void)idx;
	pcie_phy_clk_get_freq(0, &f_pcie);
	pcie_phy_clk_get_freq(9, &f_ref);
	for (i = 0; i < 9; i++) {
		st = clk_nodes_get(CLK_TREE_PCIE_PHY_PLL + i);
		on = pcie_phy_clk_get_state(i);
		st->actual_state = on;
		st->actual_freq = on && running ? f_pcie : 0;
	}
	st = clk_nodes_get(CLK_TREE_GMAC_REC_CLK);
	on = pcie_phy_clk_get_state(9);
	st->actual_state = on;
	st->actual_freq = on && running ? f_ref : 0;
}

/* Turn output idx on or off; the critical sections keep the sequences tight */
static void pcie_phy_clk_set_state(uint32_t idx, bool enable)
{
	uint32_t f, ctrl, en, buf, bctrl, reg, v;

	if (idx == 0) {
		ctrl = REG32(PCIE_PHY_CTRL);
		en = REG32(PCIE_PHY_EN);
		if (enable) {
			REG32(PCIE_PHY_EN) = en | 1;
			/* out of power down, a block at a time */
			REG32(PCIE_PHY_CTRL) = ctrl & ~0x10u;
			REG32(PCIE_PHY_CTRL) = ctrl & ~0x14u;
			REG32(PCIE_PHY_CTRL) = ctrl & ~0x16u;
			f = clk_lock();
			buf = REG32(PCIE_PHY_BUF);
			bctrl = REG32(PCIE_PHY_BUF_CTRL);
			REG32(PCIE_PHY_BUF) = buf | 1;
			cc_delay(16);
			REG32(PCIE_PHY_BUF_CTRL) = bctrl | 0x10;
			REG32(PCIE_PHY_BUF_CTRL) = bctrl | 0x11;
		} else {
			REG32(PCIE_PHY_CTRL) = ctrl | 0x16;
			REG32(PCIE_PHY_EN) = en & ~1u;
			f = clk_lock();
			buf = REG32(PCIE_PHY_BUF);
			bctrl = REG32(PCIE_PHY_BUF_CTRL);
			REG32(PCIE_PHY_BUF) = buf & ~1u;
			REG32(PCIE_PHY_BUF_CTRL) = bctrl & ~0x11u;
		}
		clk_unlock(f);
	} else if (idx <= 8) {
		reg = PCIE_PHY_BUF_CTRL + 4 * idx;
		f = clk_lock();
		v = REG32(reg);
		if (enable) {
			REG32(reg) = v | 0x10100;
			REG32(reg) = v | 0x11100;
			us_delay(11);
		} else {
			REG32(reg) = v & ~PCIE_PHY_OUT_ON;
		}
		clk_unlock(f);
	} else {
		v = REG32(PCIE_PHY_EN);
		REG32(PCIE_PHY_EN) = enable ? v | 2 : v & ~2u;
	}
}

/*
 * The custom node's operations; para is the output's index, data the
 * argument (the state, a frequency in Hz, or where to put the answer).
 */
bool pcie_phy_clk_optor(CLK_NODE_OPTOR op, void *para, void *data)
{
	uint32_t idx = (uint32_t)(uintptr_t)para, freq, div;
	CLK_TREE_STRUCT *st = clk_nodes_get((uint16_t)(CLK_TREE_PCIE_PHY_PLL + idx)), *pll;
	CLK_RATES_DATA *rates;
	bool state;

	switch (op) {
	case clk_op_set_state:
		CLK_ASSERT(idx < PCIE_PHY_NUM, 0xf1590486);
		pll = clk_nodes_get(CLK_TREE_PCIE_PHY_PLL);
		pcie_phy_clk_set_state(idx, data != NULL);
		/* desired_state: 1 on, 3 off */
		st->desired_state = data ? 1 : 3;
		st->actual_state = data != NULL;
		/* an output runs only with the PLL */
		if (idx && data)
			st->actual_state = pll->actual_state;
		return true;
	case clk_op_get_state:
		state = pcie_phy_clk_get_state(idx);
		if (data)
			*(uint32_t *)data = state;
		return state;
	case clk_op_update_state:
		pcie_phy_clk_update_state(idx);
		return true;
	case clk_op_set_freq:
		freq = (uint32_t)(uintptr_t)data;
		if (freq - 5000000 > PCIE_PHY_HZ - 5000000)
			return false;
		div = PCIE_PHY_HZ / freq / 2;
		st->desired_freq = freq;
		REG32(idx < 9 ? PCIE_PHY_DIV_PCIE : PCIE_PHY_DIV_REF) = div;
		if (div)
			st->actual_freq = PCIE_PHY_HZ / 2 / div;
		pcie_phy_clk_update_state(idx);
		return true;
	case clk_op_get_freq:
		pcie_phy_clk_get_freq(idx, data);
		return true;
	case clk_op_emu_rates:
		CLK_ASSERT(idx < PCIE_PHY_NUM, 0xf1590616);
		CLK_ASSERT(data != NULL, 0xf1590617);
		rates = data;
		rates->mode = CLK_RATES_TYPE_DISCRETE;
		rates->discrete.rates_num = 1;
		rates->discrete.rates_array = malloc(sizeof(uint32_t));
		rates->discrete.rates_array[0] = idx < 9 ? 100000000 : 25000000;
		return true;
	default:
		return false;
	}
}
