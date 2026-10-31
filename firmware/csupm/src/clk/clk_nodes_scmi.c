/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The clocks the OS sees through SCMI: SCMI clock i is the node
 * clk_nodes_export_to_os[i] (CIX's Source/projects/sky1/clktree/
 * clk_nodes_scmi.c).
 */
#include "clk.h"

#define SCMI_CLOCKS	(sizeof(clk_nodes_export_to_os) / sizeof(clk_nodes_export_to_os[0]))

size_t clk_nodes_get_scmi_element_number(void)
{
	return SCMI_CLOCKS;
}

void *clk_nodes_scmi_idx_to_ctrl_struct(uint32_t scmi_idx)
{
	if (scmi_idx >= SCMI_CLOCKS) {
		clk_assert_failed(0xf1a60301);
		return NULL;
	}
	return clk_nodes_get(clk_nodes_export_to_os[scmi_idx]);
}

uint32_t clk_nodes_scmi_idx_to_ctrl_idx(uint32_t scmi_idx)
{
	if (scmi_idx >= SCMI_CLOCKS) {
		clk_assert_failed(0xf1a60311);
		return UINT32_MAX;
	}
	return clk_nodes_export_to_os[scmi_idx];
}

uint32_t clk_nodes_enum_idx_to_scmi_idx(CLK_TREE_ENUM_IDX enum_idx)
{
	const CLK_TREE_STRUCT *p = clk_nodes_get(enum_idx);

	if (!p->is_scmi_node) {
		clk_assert_failed(0xf1a60324);
		return UINT32_MAX;
	}
	return p->scmi_idx;
}

/* The same without the node's word for it */
uint32_t clk_nodes_search_for_scmi_index(CLK_TREE_ENUM_IDX enum_idx)
{
	uint32_t i;

	for (i = 0; i < SCMI_CLOCKS; i++) {
		if (clk_nodes_export_to_os[i] == enum_idx)
			return i;
	}
	return UINT32_MAX;
}
