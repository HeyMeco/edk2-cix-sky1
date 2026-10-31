/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The display pixel clocks: eight muxes (g_pixel_mux_nodes, registers at
 * 0xa0061414) each pick one of five pixel PLLs, and the custom nodes
 * pixel_mux0..7 of the tree drive them through pll_pixel_mux_optor,
 * sharing a PLL between muxes at the same rate and turning off those
 * no mux uses (CIX's Source/projects/sky1/clktree/pll_pixel_mux.c).
 */
#include <stdlib.h>

#include "clk.h"

#include "plat.h"

#define PIXEL_PLLS	5
#define PIXEL_MUXES	8

/* A pixel PLL's output divider, from its base */
static const uint8_t out_div_reg[PIXEL_PLLS] = { 0x18, 0x18, 0x18, 0x18, 0x1c };

/* Past boot, platforms 1 and 3 (plat_id_0 bits 17-18) leave the muxes be */
static bool mux_fixed(void)
{
	uint32_t plat = (plat_id_0 >> 17) & 3;

	return ss_hsm_get() >= HSM_ST_PTS && (plat == 1 || plat == 3);
}

static CLK_TREE_STRUCT *pixel_pll(uint32_t pll_id)
{
	return clk_nodes_get(CLK_TREE_PIXEL_PLL0 + pll_id);
}

static uint32_t pll_base(const CLK_TREE_STRUCT *pll)
{
	return pll->pll->pll_base;
}

void pixel_pll_set_stat(bool stat, CLK_TREE_STRUCT *pnode)
{
	if (g_enable_pll_policy)
		pll_policy_set_state(stat, clk_struct_to_idx(pnode));
	else
		pll_ctrl_set_state(stat, (void *)pll_base(pnode));
}

uint32_t pll_pixel_get_mux_src(void *para)
{
	CLK_TREE_STRUCT *m = &g_pixel_mux_nodes[(uintptr_t)para];

	return mux_ctrl_get_src(m->mux.reg, m->mux.mask);
}

/* What a pixel PLL puts out after its output divider, 0 if off */
static uint32_t pixel_pll_calculate_output_freq(uint32_t pll_id)
{
	CLK_TREE_STRUCT *pll;
	uint32_t freq, div;

	CLK_ASSERT(pll_id < PIXEL_PLLS, 0xf1a50114);
	pll = pixel_pll(pll_id);
	if (!pll_ctrl_get_state((void *)pll_base(pll)))
		return 0;
	freq = pll_ctrl_get_postdiv_freq((void *)pll_base(pll));
	div = div_ctrl_get_divisor(pll_base(pll) + out_div_reg[pll_id], 0xf);
	if (div > 1)
		freq /= div;
	return freq;
}

/* The state of the PLL the mux selects */
static bool pll_pixel_mux_get_actual_stat(uint32_t mux_id)
{
	CLK_TREE_STRUCT *m = &g_pixel_mux_nodes[mux_id];
	CLK_TREE_STRUCT *pll = pixel_pll(mux_ctrl_get_src(m->mux.reg, m->mux.mask));

	return pll->actual_state;
}

/* The mux, its node and its PLL take the PLL's state and rate */
static void pll_pixel_mux_update_state(uint32_t mux_id)
{
	CLK_TREE_STRUCT *pnode = clk_nodes_get(CLK_TREE_PIXEL_MUX0 + mux_id);
	CLK_TREE_STRUCT *m = &g_pixel_mux_nodes[mux_id], *pll;
	uint32_t src = mux_ctrl_get_src(m->mux.reg, m->mux.mask), freq;
	bool on;

	CLK_ASSERT(plat_id_iss || (m->mux.src_bitmap >> src & 1), 0xf1a50226);
	pll = pixel_pll(src);
	freq = pixel_pll_calculate_output_freq(src);
	on = pll_ctrl_get_state((void *)pll_base(pll)) != 0;
	if (m->desired_state == 0) {
		m->desired_state = 1;
		m->actual_state = on;
	}
	pll->actual_state = on;
	pnode->actual_state = on;
	pll->actual_freq = freq;
	pnode->actual_freq = freq;
	m->actual_freq = freq;
}

/* Turns off the pixel PLLs no enabled mux selects */
static void disable_unused_pixel_pll(void)
{
	uint32_t used[PIXEL_PLLS] = { 0 };
	uint32_t i, src;

	for (i = 0; i < PIXEL_MUXES; i++) {
		CLK_TREE_STRUCT *m = &g_pixel_mux_nodes[i];

		if (!m->actual_state)
			continue;
		src = mux_ctrl_get_src(m->mux.reg, m->mux.mask);
		CLK_ASSERT(src < PIXEL_PLLS, 0xf1a50255);
		used[src] = 1;
	}
	for (i = 0; i < PIXEL_PLLS; i++) {
		CLK_TREE_STRUCT *pll;

		if (used[i])
			continue;
		pll = pixel_pll(i);
		pixel_pll_set_stat(false, pll);
		pll->actual_freq = 0;
		pll->desired_state = 3;
		pll->actual_state = 0;
	}
	for (i = 0; i < PIXEL_MUXES; i++)
		pll_pixel_mux_update_state(i);
}

/*
 * The PLL runs at 32.7 MHz or more; lower rates double it up to 16 times
 * and divide it back.  Returns the rate put out.
 */
static uint32_t pixel_pll_ctrl_set_freq(uint32_t desired, uint32_t pll_id)
{
	CLK_TREE_STRUCT *pll;
	uint32_t div, freq;

	CLK_ASSERT(pll_id < PIXEL_PLLS, 0xf1a50134);
	pll = pixel_pll(pll_id);
	for (div = 1; div <= 14 && desired < 32700000; div <<= 1)
		desired <<= 1;
	if (desired < 32700000)
		return pixel_pll_calculate_output_freq(pll_id);
	div_ctrl_set_divisor(pll_base(pll) + out_div_reg[pll_id], 0xf, div);
	if (g_enable_pll_policy)
		freq = pll_policy_set_freq(desired, clk_struct_to_idx(pll));
	else
		freq = pll_ctrl_set_freq(desired, (void *)pll_base(pll));
	return freq / div;
}

/* Turns a pixel PLL on at freq */
static CLK_TREE_STRUCT *pixel_pll_start(uint32_t pll_id, uint32_t freq)
{
	CLK_TREE_STRUCT *pll = pixel_pll(pll_id);

	pixel_pll_set_stat(true, pll);
	pll->actual_freq = pixel_pll_ctrl_set_freq(freq, pll_id);
	pll->desired_freq = freq;
	pll->desired_state = 1;
	pll->actual_state = 1;
	return pll;
}

/*
 * A PLL already at the rate is shared; else the mux's PLL is retuned, or,
 * if another enabled mux uses it, the first PLL that is off.
 */
static bool pll_pixel_mux_set_freq(uint32_t mux_id, uint32_t freq)
{
	CLK_TREE_STRUCT *pnode = clk_nodes_get(CLK_TREE_PIXEL_MUX0 + mux_id);
	CLK_TREE_STRUCT *m = &g_pixel_mux_nodes[mux_id], *pll;
	uint32_t on[PIXEL_PLLS] = { 0 };
	uint32_t i, src, f;

	m->desired_freq = freq;
	pnode->desired_freq = freq;
	if (mux_fixed())
		return true;
	disable_unused_pixel_pll();
	if (pnode->desired_state == 0 && pnode->actual_state == 0)
		return true;

	for (i = 0; i < PIXEL_PLLS; i++) {
		if (!pll_ctrl_get_state((void *)pll_base(pixel_pll(i))))
			continue;
		on[i] = 1;
		f = pixel_pll_calculate_output_freq(i);
		if (tolerance_equal(freq, f)) {
			clk_tree_mux_switch(&m->clk_tree_node, i);
			m->actual_freq = f;
			pnode->actual_freq = f;
			disable_unused_pixel_pll();
			return true;
		}
	}

	src = mux_ctrl_get_src(m->mux.reg, m->mux.mask);
	for (i = 0; i < PIXEL_MUXES; i++) {
		CLK_TREE_STRUCT *o = &g_pixel_mux_nodes[i];

		if (i != mux_id && mux_ctrl_get_src(o->mux.reg, o->mux.mask) == src &&
		    o->actual_state)
			break;
	}
	if (i == PIXEL_MUXES) {
		pll = pixel_pll_start(src, freq);
	} else {
		for (src = 0; src < PIXEL_PLLS && on[src]; src++)
			;
		if (src == PIXEL_PLLS)
			return false;
		pll = pixel_pll_start(src, freq);
		clk_tree_mux_switch(&m->clk_tree_node, src);
	}
	m->actual_freq = pll->actual_freq;
	pnode->actual_freq = pll->actual_freq;
	return true;
}

/* para is the mux's number */
bool pll_pixel_mux_optor(CLK_NODE_OPTOR op, void *para, void *data)
{
	uint32_t mux_id = (uintptr_t)para, src, i;
	CLK_TREE_STRUCT *m = &g_pixel_mux_nodes[mux_id], *pnode;
	CLK_RATES_DATA *rates = data;
	uint32_t *freqs;
	bool en, st;

	switch (op) {
	case clk_op_set_state:
		pnode = clk_nodes_get(CLK_TREE_PIXEL_MUX0 + mux_id);
		if (mux_fixed()) {
			mux_ctrl_set_src(m->mux.reg, m->mux.mask, m->mux.por_select);
			return true;
		}
		en = data != NULL;
		m->actual_state = en;
		pnode->desired_state = en;
		if (!en)
			disable_unused_pixel_pll();
		else if (pnode->desired_freq)
			pll_pixel_mux_set_freq(mux_id, pnode->desired_freq);
		pnode->actual_state = pll_pixel_mux_get_actual_stat(mux_id);
		if (pnode->actual_state || !en)
			return true;
		src = mux_ctrl_get_src(m->mux.reg, m->mux.mask);
		pixel_pll_set_stat(true, pixel_pll(src));
		pnode->actual_state = pll_pixel_mux_get_actual_stat(mux_id);
		return true;

	case clk_op_get_state:
		if (mux_fixed())
			m->mux.por_select = mux_ctrl_get_src(m->mux.reg, m->mux.mask);
		st = pll_pixel_mux_get_actual_stat(mux_id);
		*(uint32_t *)data = st;
		return st;

	case clk_op_update_state:
		if (!plat_id_iss)
			pll_pixel_mux_update_state(mux_id);
		return true;

	case clk_op_set_freq:
		return pll_pixel_mux_set_freq(mux_id, (uintptr_t)data);

	case clk_op_get_freq:
		src = mux_ctrl_get_src(m->mux.reg, m->mux.mask);
		*(uint32_t *)data = pixel_pll(src)->actual_freq;
		return true;

	case clk_op_emu_rates:
		/* A PLL that is off can take any rate */
		for (i = 0; i < PIXEL_PLLS; i++) {
			CLK_TREE_STRUCT *pll = pixel_pll(i);

			if (pll_ctrl_get_state((void *)pll_base(pll)))
				continue;
			rates->mode = CLK_RATES_TYPE_CONINUTES;
			rates->continuous.min = pll->pll->freq_min;
			rates->continuous.max = pll->pll->freq_max;
			rates->continuous.step = pll->pll->freq_step;
			return true;
		}
		/* Else only the rates they run at */
		freqs = malloc(PIXEL_PLLS * sizeof(*freqs));
		CLK_ASSERT(freqs, 0xf1a50188);
		for (i = 0; i < PIXEL_PLLS; i++)
			freqs[i] = pixel_pll_calculate_output_freq(i);
		rates->discrete.rates_array = freqs;
		rates->mode = CLK_RATES_TYPE_DISCRETE;
		rates->discrete.rates_num = PIXEL_PLLS;
		return true;
	}
	return false;
}

void pll_piexel_mux_init(void)
{
	uint32_t i;

	for (i = 0; i < PIXEL_MUXES; i++)
		g_pixel_mux_nodes[i].clk_tree_node.node_data = &g_pixel_mux_nodes[i];
}
