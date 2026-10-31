/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Clock driver over the clock tree for upstream's clock module (CIX's
 * extmodules/clkpll): element i is SCMI clock i, the tree node
 * clk_nodes_export_to_os[i].
 */
#ifndef MOD_CLKPLL_H
#define MOD_CLKPLL_H

enum mod_clkpll_api_idx {
	MOD_CLKPLL_API_IDX_DRIVER,
	MOD_CLKPLL_API_IDX_COUNT,
};

#endif
