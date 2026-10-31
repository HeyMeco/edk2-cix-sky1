/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The clock tree: PLLs, dividers, gates and muxes of Sky1 as CSU_PM drives
 * them (CIX's Source/common/pllmgmt and Source/projects/sky1/clktree).
 *
 * The types (clk_cix.h) and tables (clk_data.c) are generated from CIX's
 * image by scripts/open/csupm_clk.py; the functions keep CIX's names and
 * prototypes (from its DWARF) so that each file is a translation of CIX's.
 * Debug printing and the CLI are left out.  Where CIX's code waits in a
 * FreeRTOS task (PLL lock polling) the open firmware polls in line.
 */
#ifndef CLK_H
#define CLK_H

#include "clk_cix.h"
#include "pm.h"
#include "soc.h"

/* --- tables (clk_data.c) ------------------------------------------------ */

extern CLK_TREE_STRUCT clk_tree_st[CLK_TREE_MAX_IDX];
extern CLK_TREE_STRUCT g_pixel_mux_nodes[8];
extern uint16_t g_pixel_src[6];
extern CLK_MUX_SRC g_mux_src[12];
extern const uint16_t clk_nodes_export_to_os[273];
extern uint8_t _s_postdiv[25];
extern PLL_CTRL_POLICY pll_dev[23];
extern PLL_CTRL_POLICY pll_post[6];

/* node types (mass_ctrl0 type) */
enum {
	CLK_TYPE_ROOT,
	CLK_TYPE_PLL,
	CLK_TYPE_PLL_VCO,
	CLK_TYPE_PLL_POSTDIV,
	CLK_TYPE_DIV,
	CLK_TYPE_FIXED_DIV,
	CLK_TYPE_GATE,
	CLK_TYPE_MUX,
	CLK_TYPE_CUSTOM,
};

/* --- CIX's functions ------------------------------------------------------ */

TREE_NODE * clk_tree_build(CLK_TREE_STRUCT *pSt, uint32_t length);
uint32_t clk_tree_max_depth(void);
void clk_tree_calc_ref_counter(CLK_TREE_STRUCT *pSt, uint32_t length);
bool is_pixel_mux_node(const TREE_NODE *node);
void clk_tree_print_layout(TREE_NODE *root);
void clk_tree_print_node(TREE_NODE *node);
bool clk_tree_enum_rates(TREE_NODE *node, CLK_RATES_DATA *rates_data);
bool clk_tree_list_possible_rates_array_from_continuous(uint32_t freq_min, uint32_t freq_max, uint32_t freq_step, uint64_t div_bitmap, uint32_t * *rates_out_array, uint32_t *rates_out_size);
void merge_rates_with_relloc(uint32_t * *rate_array, uint32_t *rate_used, uint32_t *rate_total, CLK_RATES_DATA *rates_data_parent);
bool clk_tree_list_possible_rates_array_from_discrete(uint32_t *rates_in_array, uint32_t rates_in_size, uint64_t div_bitmap, uint32_t * *rates_out_array, uint32_t *rates_out_size);
bool pll_set_stat(bool stat, CLK_TREE_STRUCT *pSt);
bool clk_tree_set_state(TREE_NODE *node, bool enable);
bool clk_tree_mux_switch_with_psc_ramp_up(TREE_NODE *node, uint32_t mux_clkB_id, uint32_t psc_reg_addr);
uint32_t clk_tree_get_freq(TREE_NODE *node);
bool clk_tree_mux_switch(TREE_NODE *node, uint32_t mux_clkB_id);
void clk_tree_save_as_por_setting(TREE_NODE *node);
void clk_tree_apply_and_clear_por_setting(TREE_NODE *node);
void clk_tree_calc_state_of_node(TREE_NODE *node);
void clk_tree_list_rates_of_scmi_node(TREE_NODE *node);
bool clk_tree_set_freq(TREE_NODE *node, uint32_t freq);
void clk_tree_state_refresh(uint32_t subtree);
void clk_tree_decrease_ref_cnt(TREE_NODE *node);
uint32_t clk_tree_get_rates_num(TREE_NODE *node);
uint32_t clk_tree_get_rate_from_index(TREE_NODE *node, uint32_t rate_index);
bool clk_tree_get_rate_info(TREE_NODE *node, uint32_t *isContinue, uint32_t *min, uint32_t *max, uint32_t *step, uint32_t *rate_count);
bool clk_api_set_state(TREE_NODE *node, bool enable);
bool clk_api_set_freq(TREE_NODE *node, uint32_t freq);
bool clk_api_get_state(TREE_NODE *node);
uint32_t clk_api_get_freq(TREE_NODE *node, bool apply_to_scmi30);
void clk_api_decrease_ref_cnt(TREE_NODE *node);
bool clk_api_set_tracking(uint32_t clk_enum_idx, bool enable);
void div_ctrl_set_divisor(uint32_t div_reg, uint32_t div_mask, uint32_t div);
uint32_t div_ctrl_get_divisor(uint32_t div_reg, uint32_t div_mask);
uint32_t div_ctrl_get_special_div(uint32_t divisor);
uint32_t div_ctrl_get_maxinum_div(uint32_t supported_divisors_bitmap);
uint32_t div_ctrl_calc_best_divisor(uint32_t input_freq, uint32_t desired_freq, uint32_t round_policy, uint32_t supported_divisors_bitmap);
void gate_ctrl_set_state(uint32_t reg, uint32_t mask, bool state);
bool gate_ctrl_get_state(uint32_t reg, uint32_t mask);
void mux_ctrl_set_src_with_psc_ramp_up(uint32_t reg, uint32_t mask, uint32_t source_idx, uint32_t psc_reg_addr);
void mux_ctrl_set_src(uint32_t reg, uint32_t mask, uint32_t source_idx);
uint32_t mux_ctrl_get_src(uint32_t reg, uint32_t mask);
void pll_ctrl_set_POSTDIV2_with_gating(uint32_t postdiv2, void *pll_base, uint32_t gate_base, uint32_t gate_mask);
uint32_t pll_ctrl_set_freq(uint32_t desired, void *pll_base);
bool pll_ctrl_wait_for_lock(void *pll_base, uint32_t timeout_us);
uint64_t pll_ctrl_set_FoutVCO(uint64_t desired_vco_freq, void *pll_base);
uint32_t pll_ctrl_set_FoutPOSTDIV(uint32_t desired_postdiv_freq, void *pll_base);
uint64_t pll_ctrl_get_vco_freq(void *pll_base);
uint32_t pll_ctrl_set_POSTDIV12(uint32_t postdiv1, uint32_t postdiv2, void *pll_base);
uint32_t pll_ctrl_get_postdiv_freq(void *pll_base);
uint32_t pll_ctrl_get_postdiv_num(void *pll_base);
uint32_t pll_ctrl_get_postdiv1(void *pll_base);
uint32_t pll_ctrl_get_postdiv2(void *pll_base);
void pll_ctrl_set_state(uint32_t enable, void *pll_base);
bool pll_ctrl_is_lock(void *pll_base);
uint32_t pll_ctrl_get_ctrlword(uint32_t offset, void *pll_base);
void pll_ctrl_set_ctrlword(uint32_t offset, void *pll_base, uint32_t ctrl_word);
bool pcie_phy_clk_optor(CLK_NODE_OPTOR op, void *para, void *data);
uint64_t pll_calc_freq_FoutVCO(uint32_t FBDIV, uint32_t frac, uint32_t REFDIV);
uint32_t pll_calc_freq_FoutPOSTDIV(uint32_t FBDIV, uint32_t frac, uint32_t REFDIV, uint32_t POSTDIV);
uint32_t pll_solve_freq_settings(bool fastmode, uint32_t desired, uint32_t *FBDIV, uint32_t *frac, uint32_t *REFDIV, uint32_t *PSTDIV1, uint32_t *PSTDIV2);
uint32_t pll_solve_vco_settings(bool fastmode, uint64_t desired_vco, uint32_t *FBDIV, uint32_t *frac, uint32_t *REFDIV);
uint32_t pll_solve_postdiv_settings(uint64_t FoutVCO, uint32_t desired, uint32_t *PSTDIV1, uint32_t *PSTDIV2);
void mux_fill_config(CLK_TREE_STRUCT *pSt, uint32_t length);
void mux_show_config(uint32_t idx);
void mux_set_config(uint32_t idx, uint32_t sel);
bool soc_pull_register(uint32_t pm_addr, uint32_t mask, uint32_t target, uint32_t timeout_cc);
uint8_t u_misc_u32BitCount(uint32_t d);
uint8_t u_misc_u64BitCount(uint64_t d);
uint8_t u_misc_u32lowestOneBit(uint32_t d, uint32_t startPos);
uint8_t u_misc_u64lowestOneBit(uint64_t d, uint32_t startPos);
uint32_t u_misc_quick_hash(const uint8_t *s, const int n);
void u_misc_mem_dump(void *ptr, uint32_t length);
bool double_check_sum(void *start, uint32_t length, uint64_t *sum64, bool do_check);
uint32_t u_misc_extract_uint(const uint8_t *buf, uint32_t bit_offset, uint8_t bit_width);
void tree_add_child(TREE_NODE *parent, TREE_NODE *child);
void tree_remove_childern(TREE_NODE *parent, TREE_NODE *child);
void tree_remove_childern1(TREE_NODE *parent, TREE_NODE *child, void (*post_handle)(TREE_NODE *));
void tree_traverse(TREE_NODE *node, void (*visit)(TREE_NODE *));
bool tree_traverse1(TREE_NODE *node, void *data, bool (*visit)(TREE_NODE *, void *));
uint32_t tree_is_any_node_locked(TREE_NODE *node);
bool tree_lock_subtree(TREE_NODE *node, uint32_t lock_id);
bool tree_unlock_subtree(TREE_NODE *node);
bool tree_find_lowest_common_parent(TREE_NODE *node_a, TREE_NODE *node_b, TREE_NODE * *common_parent);
bool tree_traverse_path(TREE_NODE *node_a, TREE_NODE *node_b, void *data, bool (*visit_up)(TREE_NODE *, void *), bool (*visit_down)(TREE_NODE *, void *), bool (*visit_parent)(TREE_NODE *, void *));
void clk_dump_tree(TREE_NODE *root);
void clk_dump_node(TREE_NODE *node);
void clk_node_override_default(void);
void * clk_nodes_get(CLK_TREE_ENUM_IDX enum_idx);
void clk_nodes_init(void);
void clk_nodes_clr_tracking_bit(void);
void clk_nodes_save(void);
void clk_nodes_restore(void);
CLK_TREE_ENUM_IDX clk_struct_to_idx(const CLK_TREE_STRUCT *pSt);
CLK_TREE_ENUM_IDX clk_nodeptr_to_idx(const TREE_NODE *node);
void pixel_pll_set_stat(bool stat, CLK_TREE_STRUCT *pnode);
uint32_t pll_pixel_get_mux_src(void *para);
bool pll_pixel_mux_optor(CLK_NODE_OPTOR op, void *para, void *data);
void pll_piexel_mux_init(void);
uint16_t * clk_nodes_get_scmi_element_table(void);
size_t clk_nodes_get_scmi_element_number(void);
void * clk_nodes_scmi_idx_to_ctrl_struct(uint32_t scmi_idx);
uint32_t clk_nodes_scmi_idx_to_ctrl_idx(uint32_t scmi_idx);
uint32_t clk_nodes_enum_idx_to_scmi_idx(CLK_TREE_ENUM_IDX enum_idx);
uint32_t clk_nodes_search_for_scmi_index(CLK_TREE_ENUM_IDX enum_idx);
void pll_policy_set_str_context(bool is_str_context);
void dbg_pll_fill_default(void);
void dbg_pll_check_default(void);
void pll_policy_config(pm_config_pll_t *config);
bool pll_policy_deinit(uint32_t loc_idx);
bool pll_static_cfg_enter(uint32_t loc_idx);
bool pll_static_cfg_exit(uint32_t loc_idx);
bool pll_policy_init(uint32_t loc_idx);
void pll_hsm_init(void);
void pll_fill_config(CLK_TREE_STRUCT *pSt, uint32_t length);
void pll_policy_save(void);
void pll_policy_restore(void);
uint32_t pll_policy_set_freq(uint32_t desired, CLK_TREE_ENUM_IDX enum_idx);
uint64_t pll_policy_set_FoutVCO(uint64_t desired_vco_freq, CLK_TREE_ENUM_IDX enum_idx);
uint32_t pll_policy_set_FoutPOSTDIV(uint32_t desired_postdiv_freq, CLK_TREE_ENUM_IDX enum_idx);
bool pll_policy_set_state(bool enable, CLK_TREE_ENUM_IDX enum_idx);
void pll_policy_lock_entry(uint32_t pll_idx);
void pll_policy_unlock_entry(uint32_t pll_idx);
void pll_policy_dump_settings(uint32_t pll_idx);
uint32_t pll_policy_num_to_base(uint32_t pll_num);
uint32_t pll_policy_num_to_enum_idx(uint32_t pll_num);
const char * pll_policy_num_to_name(uint32_t pll_num);
/* functions CIX's DWARF does not describe */
uint32_t clk_tree_nodes_num(void);		/* clk_tree.c */
uint32_t pll_ctrl_get_state(void *pll_base);	/* clk_port.c: 1 if enabled */
bool is_smn_space(uint32_t addr);
bool tolerance_equal(uint32_t a, uint32_t b);
uint32_t get_c_count(void);

/* PLLs go through the PLL policy (pll_policy.c); off while clk_nodes_init runs */
extern bool g_enable_pll_policy;

/* --- what the clock code uses from elsewhere ----------------------------- */

/* SoC registers by PM address (CIX's soc_reg_access.c) */
static inline void SocRegWrite32(uint32_t addr, uint32_t val)
{
	soc_write32(addr & ~SOC_WINDOW, val);
}

static inline uint32_t SocRegRead32(uint32_t addr)
{
	return soc_read32(addr & ~SOC_WINDOW);
}

/* Interrupts off and on (CIX: FreeRTOS critical sections, scheduler lock) */
uint32_t clk_lock(void);
void clk_unlock(uint32_t flags);

/* Busy-waits */
void cc_delay(uint32_t cycles);
void us_delay(uint32_t us);
uint64_t arch_getTimeNowUs(void);

/* The RCSU hardware mutexes guarding subsystem clock registers */
void smn_rcsu_lock(uint32_t smn_addr);
void smn_rcsu_unlock(uint32_t smn_addr);

/* The PM's system state (CIX's pwr_hsm.c) */
typedef enum {
	HSM_ST_POR = 0,
	HSM_ST_INIT = 1,
	HSM_ST_RUN = 2,
	HSM_ST_PTS = 3,
	HSM_ST_S3 = 4,
	HSM_ST_S5 = 5,
	HMS_ST_MAX = 6,
} SS_HSM_STATE;
SS_HSM_STATE ss_hsm_get(void);
extern SS_HSM_STATE pm_ss_state;	/* main moves it through the phases */

/* Hooks into other parts of the PM (no-ops until those parts are here) */
bool perf_soc_idle_switch_get_enable(void);
void dpdpu_trigger_ddr_dfs_compatible_checker(void);
void vpu_ddr_dfs_set_compatible(bool is_compatible);
void isp_ddr_dfs_set_compatible(bool is_compatible);
void gpu_ddr_dfs_set_compatible(bool is_compatible);
bool perf_stream_is_active(void);
void perf_stream_track_clk_change(uint32_t clk_idx, uint32_t freq);
void clk_assert_failed(uint32_t code);
#define CLK_ASSERT(cond, code)	do { if (!(cond)) clk_assert_failed(code); } while (0)

#endif
