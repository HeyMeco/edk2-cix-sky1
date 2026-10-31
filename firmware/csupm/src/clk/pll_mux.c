/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * CIX's pll_mux.c translated: the source lists of the clock muxes
 * (g_mux_src) and switching a mux by hand.  The debug printing
 * (mux_show_config) is left out.
 */
#include "clk.h"

/* Give each mux node its source list and check its parent is a source */
void mux_fill_config(CLK_TREE_STRUCT *pSt, uint32_t length)
{
	uint32_t i, j, k;

	for (i = 0; i < CLK_TREE_MAX_IDX && i < length; i++) {
		CLK_TREE_STRUCT *st = &pSt[i];

		if (st->type != CLK_TYPE_MUX)
			continue;

		for (k = 0; k < ARRAY_SIZE(g_mux_src); k++) {
			if (g_mux_src[k].idx == (uint16_t)i)
				break;
		}
		if (k == ARRAY_SIZE(g_mux_src)) {
			st->mux.src_clk = NULL;
			CLK_ASSERT(0, 0xf15b0123);
		} else {
			st->mux.src_clk = g_mux_src[k].src_clk;
		}

		for (j = 0; j < 6; j++) {
			if (st->parent_idx != st->mux.src_clk[j])
				continue;
			CLK_ASSERT(st->mux.src_bitmap & BIT(j), 0xf15b0130);
			/* CIX reads the hardware's selection (checked in range) */
			mux_ctrl_get_src(st->mux.reg, st->mux.mask);
		}
	}
}

/* Switch mux idx of g_mux_src to its source sel */
void mux_set_config(uint32_t idx, uint32_t sel)
{
	CLK_TREE_STRUCT *st;

	if (idx >= ARRAY_SIZE(g_mux_src) || sel >= 6 || g_mux_src[idx].src_clk[sel] == 0)
		return;
	st = clk_nodes_get(g_mux_src[idx].idx);
	mux_ctrl_set_src(st->mux.reg, st->mux.mask, sel);
	tree_traverse(&st->clk_tree_node, clk_tree_calc_state_of_node);
}
