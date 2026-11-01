/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The DPM's run-time tables, CIX's initial values read from its image
 * (scripts/open/dwarf_dump.py): the dashboard (dpm_dashboard.c's
 * dpm_main_tab), the element/domain/rail maps (cfg_dpm.c), the clock
 * ranges per domain (cfg_x_clk.c's t_clk_*), the minimum powers
 * (cfg_pwr.c), the arbiters and their PID controllers (cfg_spt.c), the
 * clock controllers (cfg_x_clk.c).
 */
#include "dpm_rt.h"

DPM_DRAM1 DPM_DASHBOARD dpm_main_tab = {
	.freq2pwr_coef = dpm_freq2pwr_util_coef,
	.spt_ctrls = spt_ctrls,
	.min_dxs_pwr_mW = min_dxs_pwr_mW,
	.pwr2freq_coef = dpm_freq2pwr_opp_table_coef,
};

const uint8_t dpm_ele_to_dxs_map[DPM_ELE_MAX] = {
	DPM_DxS_CORE_LIT, DPM_DxS_CORE_LIT, DPM_DxS_CORE_LIT, DPM_DxS_CORE_LIT,
	DPM_DxS_CORE_GM0, DPM_DxS_CORE_GM0, DPM_DxS_CORE_GM1, DPM_DxS_CORE_GM1,
	DPM_DxS_CORE_GB0, DPM_DxS_CORE_GB0, DPM_DxS_CORE_GB1, DPM_DxS_CORE_GB1,
	DPM_DxS_DSU, DPM_DxS_GPU_TOP, DPM_DxS_GPU_CORE, DPM_DxS_SOC_CI,
	DPM_DxS_SOC_MMHUB, DPM_DxS_SOC_VPU, DPM_DxS_SOC_NPU, DPM_DxS_SOC_MEMORY,
};

const uint8_t dpm_ele_to_edp_map[DPM_ELE_MAX] = {
	EDP_CPU_LIT, EDP_CPU_LIT, EDP_CPU_LIT, EDP_CPU_LIT, EDP_CPU_GM0, EDP_CPU_GM0,
	EDP_CPU_GM1, EDP_CPU_GM1, EDP_CPU_GB0, EDP_CPU_GB0, EDP_CPU_GB1, EDP_CPU_GB1,
	EDP_DSU, EDP_GPU, EDP_GPU, EDP_SOC, EDP_SOC, EDP_SOC, EDP_SOC, EDP_SOC,
};

const uint8_t dpm_dxs_to_edp_map[DPM_DxS_MAX] = {
	EDP_CPU_LIT, EDP_CPU_GM0, EDP_CPU_GM1, EDP_CPU_GB0, EDP_CPU_GB1, EDP_DSU,
	EDP_GPU, EDP_GPU, EDP_SOC, EDP_SOC, EDP_SOC, EDP_SOC, EDP_SOC,
};

const DPM_DXS_TO_ELEM_MAP dpm_dxs_to_elem_map[DPM_DxS_MAX] = {
	{ DPM_ELE_CPU_L0, 4 }, { DPM_ELE_CPU_M0, 2 }, { DPM_ELE_CPU_M2, 2 },
	{ DPM_ELE_CPU_B0, 2 }, { DPM_ELE_CPU_B2, 2 }, { DPM_ELE_DSU, 1 },
	{ DPM_ELE_GPU_TOP, 1 }, { DPM_ELE_GPU_CORE, 1 }, { DPM_ELE_CI, 1 },
	{ DPM_ELE_MMHUB, 1 }, { DPM_ELE_VPU, 1 }, { DPM_ELE_NPU, 1 }, { DPM_ELE_MEMORY, 1 },
};

/*
 * The ninth entry is what CIX's dpm_refresh_dashboard reads past the end of
 * its table for the memory's rail (8): the first bytes of the next table
 * (dpm_element_to_clk_id_table: clock ids 0x4e, then 0x50 and 0x51).  Its
 * loop over those "domains" stops at the first whose highest level is not
 * 0, the 8-bit domain number wrapping to 0 after 178 steps.
 */
const DPM_EDP_TO_DXS_MAP dpm_edp_to_dxs_map[DPM_EDP_MAX + 1] = {
	{ DPM_DxS_CORE_LIT, 1 }, { DPM_DxS_CORE_GM0, 1 }, { DPM_DxS_CORE_GM1, 1 },
	{ DPM_DxS_CORE_GB0, 1 }, { DPM_DxS_CORE_GB1, 1 }, { DPM_DxS_DSU, 1 },
	{ DPM_DxS_GPU_TOP, 2 }, { DPM_DxS_SOC_CI, 5 }, { 0x4e, 0x510050 },
};

/* kHz: min, max, step, effective */
static const T_CLK_DOMAIN_CFG t_clk_cpu_lit = { 400000, 1800000, 100000, 800000 };
static const T_CLK_DOMAIN_CFG t_clk_cpu_mid = { 400000, 2600000, 100000, 800000 };
static const T_CLK_DOMAIN_CFG t_clk_cpu_big = { 400000, 3200000, 100000, 800000 };
static const T_CLK_DOMAIN_CFG t_clk_cpu_dsu = { 400000, 1800000, 100000, 0 };
static const T_CLK_DOMAIN_CFG t_clk_gpu_top = { 350000, 1100000, 50000, 350000 };
static const T_CLK_DOMAIN_CFG t_clk_gpu_core = { 350000, 1100000, 50000, 350000 };
static const T_CLK_DOMAIN_CFG t_clk_ci = { 400000, 1200000, 100000, 700000 };
static const T_CLK_DOMAIN_CFG t_clk_mmhub = { 600000, 2400000, 200000, 1800000 };
static const T_CLK_DOMAIN_CFG t_clk_vpu = { 400000, 2000000, 100000, 700000 };
static const T_CLK_DOMAIN_CFG t_clk_npu = { 600000, 2400000, 200000, 1800000 };
static const T_CLK_DOMAIN_CFG t_clk_mem = { 800000, 3200000, 400000, 1600000 };

const T_CLK_DOMAIN_CFG *const dxs_clk_cfg[DPM_DxS_MAX] = {
	&t_clk_cpu_lit, &t_clk_cpu_mid, &t_clk_cpu_mid, &t_clk_cpu_big, &t_clk_cpu_big,
	&t_clk_cpu_dsu, &t_clk_gpu_top, &t_clk_gpu_core, &t_clk_ci, &t_clk_mmhub,
	&t_clk_vpu, &t_clk_npu, &t_clk_mem,
};

/* mW; dpm_params_consistency_checker fills an entry that is 0 */
DPM_DRAM1 uint32_t min_dxs_pwr_mW[DPM_DxS_MAX] = {
	1464, 622, 622, 622, 622, 1767, 1883, 1883, 401, 610, 401, 610, 1296,
};

/* --- cfg_spt.c: the arbiters (socket power fast and slow, skin, die) --------------- */

DPM_DRAM1 X_ARB_CFG g_spt_opp_lv_cfg = { .phase_cfg = {
	{ 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f },
	{ 0.6f, 0.6f, 0.6f, 0.6f, 0.6f, 0.8f, 0.8f, 0.8f },
	{ 0.5f, 0.5f, 0.5f, 0.6f, 0.6f, 0.8f, 0.8f, 0.8f },
	{ 0.5f, 0.4f, 0.4f, 0.5f, 0.5f, 0.8f, 0.8f, 0.8f },
	{ 0.4f, 0.3f, 0.3f, 0.3f, 0.3f, 0.5f, 0.8f, 0.8f },
	{ 0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 0.3f, 0.3f, 0.3f },
	{ 0.1f, 0.1f, 0.1f, 0.1f, 0.1f, 0.1f, 0.1f, 0.2f },
	{ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.1f },
} };

/* input, set point, input filter, feedback scale, Kp, Ki, Kd */
#define PID(in, sp, alpha, scale, kp, ki, kd) { \
	.pInput = (in), .SetPoint = (sp), .in_alpha = (alpha), .fb_scale = (scale), \
	.Kp = (kp), .Ki = (ki), .Kd = (kd) }

DPM_DRAM1 T_PID_DATA spt_pids[4] = {
	PID(&dpm_main_tab.socket_power, 35000.0f, 0.1f, 0.001f, -0.018f, 0.0f, 0.005f),
	PID(&dpm_main_tab.socket_power, 28000.0f, 0.001f, 1e-05f, -0.018f, 0.0f, 0.005f),
	PID(&dpm_main_tab.skin_temp, 25.0f, 0.0002f, 0.005f, -0.018f, 0.0f, 0.001f),
	PID(&dpm_main_tab.die_temp, 90.0f, 0.002f, 0.005f, -0.015f, 0.0f, 0.001f),
};

#define ARB(en, p, nm) { \
	.is_enabled = (en), .pCfg = &g_spt_opp_lv_cfg, \
	.domain_expect = dpm_main_tab.exp_edp_pwr_mW, .domain_limit = dpm_main_tab.edp_limit, \
	.pid = (p), .domain_throttle_coeff = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f }, \
	.name = (nm) }

DPM_DRAM1 X_ARB_CTRL spt_ctrls[4] = {
	ARB(false, &spt_pids[0], "fast"),
	ARB(false, &spt_pids[1], "slow"),
	ARB(false, &spt_pids[2], "SKIN"),
	ARB(true, &spt_pids[3], "Tdie"),
};

/* --- cfg_x_clk.c: clock controllers per domain (initialised, not iterated) --------- */

DPM_DRAM1 static T_PID_DATA x_clk_pids[10] = {
	PID(&dpm_main_tab.util_dxs[0], 0.75f, 0.4f, 1.0f, -0.018f, 0.0f, 0.005f),
	PID(&dpm_main_tab.util_dxs[1], 0.75f, 0.4f, 1.0f, -0.018f, 0.0f, 0.005f),
	PID(&dpm_main_tab.util_dxs[2], 0.75f, 0.4f, 1.0f, -0.018f, 0.0f, 0.005f),
	PID(&dpm_main_tab.util_dxs[3], 0.75f, 0.4f, 1.0f, -0.018f, 0.0f, 0.005f),
	PID(&dpm_main_tab.util_dxs[4], 0.75f, 0.4f, 1.0f, -0.018f, 0.0f, 0.005f),
	PID(&dpm_main_tab.util_dxs[6], 0.75f, 0.4f, 1.0f, -0.018f, 0.0f, 0.005f),
	PID(&dpm_main_tab.util_dxs[8], 0.75f, 0.4f, 1.0f, -0.018f, 0.0f, 0.005f),
	PID(&dpm_main_tab.util_dxs[9], 0.75f, 0.4f, 1.0f, -0.018f, 0.0f, 0.005f),
	PID(&dpm_main_tab.util_dxs[10], 0.75f, 0.4f, 1.0f, -0.018f, 0.0f, 0.005f),
	PID(&dpm_main_tab.util_dxs[11], 0.75f, 0.4f, 1.0f, -0.018f, 0.0f, 0.005f),
};

DPM_DRAM1 X_CLK_CTRL_T x_clk_ctrl[10] = {
	{ &t_clk_cpu_lit, &x_clk_pids[0], 1400000, 0, 0, 0, 0, true },
	{ &t_clk_cpu_mid, &x_clk_pids[1], 2200000, 0, 0, 0, 0, true },
	{ &t_clk_cpu_mid, &x_clk_pids[2], 2200000, 0, 0, 0, 0, true },
	{ &t_clk_cpu_big, &x_clk_pids[3], 2800000, 0, 0, 0, 0, true },
	{ &t_clk_cpu_big, &x_clk_pids[4], 2800000, 0, 0, 0, 0, true },
	{ &t_clk_gpu_top, &x_clk_pids[5], 750000, 0, 0, 0, 0, true },
	{ &t_clk_ci, &x_clk_pids[6], 800000, 0, 0, 0, 0, true },
	{ &t_clk_mmhub, &x_clk_pids[7], 1800000, 0, 0, 0, 0, true },
	{ &t_clk_vpu, &x_clk_pids[8], 1600000, 0, 0, 0, 0, true },
	{ &t_clk_npu, &x_clk_pids[9], 1800000, 0, 0, 0, 0, true },
};
