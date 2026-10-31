/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The clock tree's nodes: building the tree from clk_tree_st, platform
 * overrides of the power-on settings, save and restore around suspend
 * (CIX's Source/projects/sky1/clktree/clk_nodes.c, without its dumps).
 */
#include "clk.h"
#include "plat.h"

static TREE_NODE *root;


/* CIX's dpm_element_to_clk_id_table (dpm.c) */
static CLK_TREE_ENUM_IDX dpm_elem_get_clk_id(uint32_t elem)
{
	static const uint16_t clk_id[20] = {
		CLK_TREE_CPU_CLK_LC0, CLK_TREE_CPU_CLK_LC1, CLK_TREE_CPU_CLK_LC2,
		CLK_TREE_CPU_CLK_LC3, CLK_TREE_CPU_CLK_MC0, CLK_TREE_CPU_CLK_MC1,
		CLK_TREE_CPU_CLK_MC2, CLK_TREE_CPU_CLK_MC3, CLK_TREE_CPU_CLK_BC0,
		CLK_TREE_CPU_CLK_BC1, CLK_TREE_CPU_CLK_BC2, CLK_TREE_CPU_CLK_BC3,
		CLK_TREE_DSU_CLK, CLK_TREE_GPU_TOP_CLK, CLK_TREE_GPU_CORE_CLK,
		CLK_TREE_CI700_GCLK0, CLK_TREE_MM_NI700_CLK, CLK_TREE_VPU_APBCLK,
		CLK_TREE_NPU_SYSCLK, CLK_TREE_DDRC_PLL,
	};

	CLK_ASSERT(elem < 20, 0xf1af0566);
	return clk_id[elem];
}

void clk_node_override_default(void)
{
	uint32_t elem = core_info_valid ? core_info[1] >> 4 : 10;
	CLK_TREE_ENUM_IDX idx = dpm_elem_get_clk_id(elem);
	CLK_TREE_STRUCT *p;

	/* The clock of the core core_info names stays on (CIX's: CPU_CLK_BC2) */
	if ((plat_id_0 & 0x60000) == 0x60000)
		idx = CLK_TREE_CPU_CLK_LC0;
	p = clk_nodes_get(idx);
	CLK_ASSERT(p->type == CLK_TYPE_GATE, 0xf1a41991);
	p->set_ref_cnt = 1;
	p->gate.por_force = 1;
	p->gate.por_state = 1;

	/* The DDR controller dividers start at settings 3 and 4 */
	p = clk_nodes_get(CLK_TREE_DDRC_ACLK_DIV);
	CLK_ASSERT(p->type == CLK_TYPE_DIV, 0xf1a42012);
	p->duty_div.por_force = 1;
	p->duty_div.por_divcfg = 3;
	p = clk_nodes_get(CLK_TREE_DDRC_DFICLK_DIV);
	CLK_ASSERT(p->type == CLK_TYPE_DIV, 0xf1a42017);
	p->duty_div.por_force = 1;
	p->duty_div.por_divcfg = 4;

	/* Unless the strap's PVT average is above 0x185, cpu_pll_800M starts at 600 MHz */
	if (boot_strap_valid && ((boot_strap >> 32) & 0x3ff) > 0x185)
		return;
	p = clk_nodes_get(CLK_TREE_CPU_PLL_800M);
	CLK_ASSERT(p->type == CLK_TYPE_PLL, 0xf1a42024);
	p->pll->por_freq = 600000000;
	p->pll->por_force = 1;
	p->pll->por_state = 1;
}

/*
 * CIX bounds the index with clk_tree_nodes_num(), the number of nodes
 * clk_tree_build() was given: CLK_TREE_MAX_IDX.
 */
void *clk_nodes_get(CLK_TREE_ENUM_IDX enum_idx)
{
	if (enum_idx >= CLK_TREE_MAX_IDX) {
		clk_assert_failed(0xf1a42252);
		return NULL;
	}
	return &clk_tree_st[enum_idx];
}

void clk_nodes_init(void)
{
	pll_fill_config(clk_tree_st, CLK_TREE_MAX_IDX);
	pll_hsm_init();
	mux_fill_config(clk_tree_st, CLK_TREE_MAX_IDX);
	root = clk_tree_build(clk_tree_st, CLK_TREE_MAX_IDX);
	pll_piexel_mux_init();
	clk_node_override_default();
	tree_traverse(root, clk_tree_apply_and_clear_por_setting);
	tree_traverse(root, clk_tree_calc_state_of_node);
	tree_traverse(root, clk_tree_list_rates_of_scmi_node);
	clk_tree_calc_ref_counter(clk_tree_st, CLK_TREE_MAX_IDX);
}

static void _clr_tracking(TREE_NODE *node)
{
	CLK_TREE_STRUCT *p;

	if (node == NULL)
		return;
	p = (CLK_TREE_STRUCT *)node->node_data;
	if (p->tracking_node)
		p->tracking_node = 0;
}

void clk_nodes_clr_tracking_bit(void)
{
	tree_traverse(root, _clr_tracking);
}

void clk_nodes_save(void)
{
	pll_policy_save();
	tree_traverse(root, clk_tree_save_as_por_setting);
}

/* Bumps a divider for a moment so that it takes its setting again */
static void div_kick(uint32_t reg, uint32_t inc, uint32_t us)
{
	uint32_t v = REG32(reg);

	REG32(reg) = v + inc;
	us_delay(us);
	REG32(reg) = v;
}

void clk_nodes_restore(void)
{
	static const uint16_t divs[15] = {
		CLK_TREE_DPU_DIV, CLK_TREE_DPU0_ACLK, CLK_TREE_DPU1_ACLK,
		CLK_TREE_DPU2_ACLK, CLK_TREE_DPU3_ACLK, CLK_TREE_DPU4_ACLK,
		CLK_TREE_NPU_DIV, CLK_TREE_NPU_DBGCLK, CLK_TREE_VPU_CLK,
		CLK_TREE_ISP_A_DIV, CLK_TREE_ISP_S_DIV, CLK_TREE_CSI_CTRL0_DIV,
		CLK_TREE_CSI_CTRL1_DIV, CLK_TREE_CSI_CTRL2_DIV, CLK_TREE_CSI_CTRL3_DIV,
	};
	uint32_t i, reg;

	pll_policy_restore();
	pll_policy_set_str_context(true);
	tree_traverse(root, clk_tree_apply_and_clear_por_setting);
	tree_traverse(root, clk_tree_calc_state_of_node);

	/* The DDR controller dividers */
	div_kick(0xa0067018, 0x1010c, 100);

	/*
	 * Meant for the pixel PLLs' output dividers (+0x18), but CIX steps by
	 * 100 rather than 0x100: 0xa0061018, 0xa006107c, 0xa00610e0,
	 * 0xa0061144 and 0xa00611a8.
	 */
	for (reg = 0xa0061018; reg != 0xa006120c; reg += 100)
		div_kick(reg, 1, 2);

	/* Dividers of the multimedia blocks, their clock on meanwhile */
	for (i = 0; i < 15; i++) {
		CLK_TREE_STRUCT *p = clk_nodes_get(divs[i]);
		TREE_NODE *node;
		uint32_t freq, mask, v, inc;

		CLK_ASSERT(p->type == CLK_TYPE_DIV, 0xf1a42295);
		node = &p->clk_tree_node;
		freq = clk_tree_get_freq(node);
		if (freq == 0)
			clk_tree_set_state(node, true);
		reg = p->duty_div.div_reg;
		mask = p->duty_div.div_mask;
		inc = 1u << u_misc_u32lowestOneBit(mask, 0);
		v = REG32(reg);
		REG32(reg) = (v & ~mask) | ((v + inc) & mask);
		us_delay(100);
		REG32(reg) = v;
		if (freq == 0)
			clk_tree_set_state(node, false);
	}

	/* Each pixel PLL enabled while its output divider (+0x18/+0x1c) is bumped */
	for (i = 0; i < 5; i++) {
		uint32_t pll = 0xa0061000 + 0x100 * i;
		uint32_t en = REG32(pll + 4);

		REG32(pll + 4) = en | 1u << 24;
		us_delay(100);
		div_kick(pll + (i == 4 ? 0x1c : 0x18), 1, 100);
		REG32(pll + 4) = en;
	}

	pll_policy_set_str_context(false);
}

CLK_TREE_ENUM_IDX clk_struct_to_idx(const CLK_TREE_STRUCT *pSt)
{
	uintptr_t a = (uintptr_t)pSt, base = (uintptr_t)clk_tree_st;

	if (a < base || a > (uintptr_t)&clk_tree_st[CLK_TREE_MAX_IDX - 1])
		return CLK_TREE_MAX_IDX;
	return (CLK_TREE_ENUM_IDX)((a - base) / sizeof(CLK_TREE_STRUCT));
}

CLK_TREE_ENUM_IDX clk_nodeptr_to_idx(const TREE_NODE *node)
{
	if (node == NULL)
		return CLK_TREE_MAX_IDX;
	return clk_struct_to_idx(node->node_data);
}
