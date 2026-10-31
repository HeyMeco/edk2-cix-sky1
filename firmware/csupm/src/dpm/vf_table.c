/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Which V/F tables the part gets, at power-on (the V/F part of CIX's
 * Source/projects/sky1/subsys_hsm.c: builtin_perf_profile_setup,
 * build_vf_table_part_1, build_vf_table_part_2, build_vf_table_apply_fmax,
 * calc_opp_limit_selector, cpu_harvest_setup; and of ss_por_enter, the
 * order they run in).
 *
 * The choice follows the part number fuse (OPN), the PM strap the SE leaves
 * (src/plat.c boot_strap: board, OPP profile, DVFS switch, PVT average,
 * patches), the fused V/F data and the board layout:
 *   g_mp_sel             production part (MP tables) or sample (test tables)
 *   g_opp_table_selector which of those tables
 *   g_opp_limit_selector which row of opp_limit caps the levels
 * then the tables are allocated, adjusted (minimum, maximum and sustained
 * levels, voltages of some ranges) and the clusters whose cores are all
 * fused off are taken out.
 */
#include "dpm.h"
#include "plat.h"

/* SoC register: bit 4 asks for GB1 at 2.8 GHz (CD parts); CIX clears it */
#define GB1_BOOST_REG		0x5600050cu

uint8_t g_mp_sel, g_opp_table_selector;
uint8_t g_opp_limit_selector = 0xff;
bool gpu_voltage, dsu_voltage, gb1_boost_to_2P8G_for_CD, boot_with_core_fmax;

/* boot strap fields (CIX's csu_pm_boot_strap_t) */
static uint32_t strap_bits(unsigned int pos, unsigned int n)
{
	return (uint32_t)(boot_strap >> pos) & ((1u << n) - 1);
}
#define STRAP_LOW20		strap_bits(0, 20)	/* checksum, board, OPP profile */
#define STRAP_OPP_PROFILE	strap_bits(16, 4)
#define STRAP_DVFS_SWITCH	strap_bits(24, 4)
#define STRAP_OPP_SUB_PROFILE	strap_bits(28, 2)
#define STRAP_PVT_AVERAGE	strap_bits(32, 10)
#define STRAP_OPP_PATCH0	strap_bits(42, 4)
#define STRAP_OPP_PATCH1	strap_bits(46, 4)

/* OPN fields as CIX tests them (bits of the high word) */
#define OPN_SEGMENT		((fuse_opn.opn_h32 >> 20) & 0x1f)

/* The row of opp_limit for the part: by OPN segment, else by the strap's
 * OPP profile */
static void calc_opp_limit_selector(void)
{
	uint8_t sel;

	if (cfg_fuse_opn_valid()) {
		uint32_t h = fuse_opn.opn_h32;
		uint32_t seg = OPN_SEGMENT;

		if (seg == 7)
			sel = 5;
		else if (seg == 8)
			sel = 7;
		else if (seg == 2 && (h & 0xffff0) == 0x81800)
			sel = 6;
		else if ((h & 0x1fffff0) == 0x261800)
			sel = 8;
		else
			sel = seg == 11 ? 8 : seg == 1 ? 9 : 6;
	} else {
		uint32_t p = (STRAP_OPP_PROFILE + 4) & 0xf;

		sel = 6;
		/* profiles 0xc-0xf: 8, 6, 7, 5 */
		if (boot_strap_valid && p < 4)
			sel = (uint8_t)(0x05070608u >> (8 * p));
	}
	g_opp_limit_selector = sel;
}

/*
 * Production parts: the strap's board and profile say MP, or the OPN does;
 * fused V/F data selects table 3 (MP2 and later) unless the DVFS switch is
 * 1, else table 2 (fixed voltages).  Samples: by board (Phecda) and the
 * strap's DVFS switch, profile and PVT average.
 */
static void build_vf_table_part_1(void)
{
	uint32_t topology;

	if ((boot_strap_valid && STRAP_LOW20 > 0xbffff) || cfg_fuse_opn_valid()) {
		g_mp_sel = 1;
		calc_opp_limit_selector();
		if ((boot_strap_valid && STRAP_OPP_PROFILE == 0xc) ||
		    (fuse_opn.opn_h32 & 0x1fffff0) == 0x261800 ||
		    (fuse_opn.opn_h32 & 0x1f00000) == 0xb00000) {
			gpu_voltage = true;
			dsu_voltage = true;
		}
		if ((boot_strap_valid && STRAP_OPP_PROFILE == 0xf) || OPN_SEGMENT == 7) {
			uint32_t r = REG32(GB1_BOOST_REG);

			if (r & 0x10) {
				REG32(GB1_BOOST_REG) = r & ~0x10u;
				gb1_boost_to_2P8G_for_CD = true;
			}
		}
		if (cfg_fuse_opn_valid() && cfg_vf_fuse_loaded() &&
		    !(boot_strap_valid && STRAP_DVFS_SWITCH == 1) &&
		    cfg_vf_get_ate_phase() >= ATE_PHACE_MP2) {
			g_opp_table_selector = 3;
			return;
		}
		g_opp_table_selector = 2;
		cfg_vf_fuse_clear();
		return;
	}

	g_mp_sel = 0;
	g_opp_limit_selector = 1;
	topology = dpm_edp_cfg_get_pmic_topology_id();
	if (!(boot_strap_valid && STRAP_DVFS_SWITCH == 2))
		cfg_vf_fuse_clear();
	if (topology == PMIC_PHECDA) {
		if (cfg_vf_fuse_loaded()) {
			g_mp_sel = 1;
			g_opp_table_selector = cfg_vf_get_ate_phase() >= ATE_PHACE_MP2 ? 3 : 1;
			g_opp_limit_selector = 7;
		} else if (cfg_vf_fuse_has_vmin_data_exists()) {
			g_mp_sel = 1;
			g_opp_table_selector = 2;
			g_opp_limit_selector = 6;
			cfg_vf_fuse_clear();
		}
	}
	if (g_mp_sel)
		return;
	if (topology != PMIC_PHECDA) {
		g_opp_limit_selector = 0;
		g_opp_table_selector = 3;
		boot_with_core_fmax = true;
		return;
	}
	if (!boot_strap_valid) {
		g_opp_table_selector = 8;
		g_opp_limit_selector = 4;
		return;
	}
	if (STRAP_DVFS_SWITCH) {
		uint32_t pvt = STRAP_PVT_AVERAGE;

		/* PVT average to table and limit (fast parts first) */
		if (pvt > 434) {
			g_opp_table_selector = 4;
			g_opp_limit_selector = 1;
		} else if (pvt > 419) {
			g_opp_table_selector = 5;
			g_opp_limit_selector = 2;
		} else if (pvt > 393) {
			g_opp_table_selector = 6;
			g_opp_limit_selector = 2;
		} else if (pvt > 365) {
			g_opp_table_selector = 7;
			g_opp_limit_selector = 3;
		} else {
			g_opp_table_selector = 8;
			g_opp_limit_selector = 4;
		}
		return;
	}
	/* DVFS switch 0: the strap's OPP profile and sub-profile */
	{
		uint32_t profile = STRAP_OPP_PROFILE, sub = STRAP_OPP_SUB_PROFILE;
		uint8_t limit;

		if (profile == 5) {
			g_opp_table_selector = 1;
			limit = 1;
		} else if (profile == 4 && sub <= 1) {
			g_opp_table_selector = (uint8_t)sub;
			limit = 1;
		} else if (profile == 4 && sub == 2) {
			g_opp_table_selector = 2;
			limit = 2;
		} else {
			g_opp_table_selector = 3;
			limit = 365 < (STRAP_PVT_AVERAGE & 0x3fe) ? 3 : 4;
		}
		g_opp_limit_selector = limit;
		boot_with_core_fmax = true;
	}
}

/*
 * The tables, then the adjustments: the strap's patches (or the OPN's CD
 * segment), by board and PVT average for samples on Phecda, the fused DSU
 * maximum, CIX's fixed voltages for parts that need more (gpu_voltage,
 * dsu_voltage).
 */
static void build_vf_table_part_2(void)
{
	allocate_config_dvfs_opp_tables(g_mp_sel != 0, g_opp_table_selector);

	if ((boot_strap_valid && STRAP_OPP_PROFILE == 0xf) || OPN_SEGMENT == 7) {
		uint32_t patch0 = boot_strap_valid ? STRAP_OPP_PATCH0 : 0;
		uint32_t patch1 = boot_strap_valid ? STRAP_OPP_PATCH1 : 0;

		if (patch0 == 1) {
			override_config_dvfs_maximum_level(DPM_DxS_CORE_GB0, 2400, false);
			override_config_dvfs_maximum_level(DPM_DxS_CORE_GB1, 2500, false);
		} else if (patch0 == 2 && gb1_boost_to_2P8G_for_CD) {
			override_config_dvfs_maximum_level(DPM_DxS_CORE_GB1, 2800, false);
			dpm_dxs_override_Vset(DPM_DxS_CORE_GB1, 2500001, 2800000, 960);
		}
		if (patch1 == 1) {
			override_config_dvfs_minimum_level(DPM_DxS_GPU_TOP, 350, false);
			override_config_dvfs_minimum_level(DPM_DxS_GPU_CORE, 350, false);
		} else if (patch1 == 2) {
			override_config_dvfs_minimum_level(DPM_DxS_GPU_TOP, 900, false);
			override_config_dvfs_minimum_level(DPM_DxS_GPU_CORE, 1000, false);
		}
	} else if (!cfg_fuse_opn_valid()) {
		uint32_t topology = dpm_edp_cfg_get_pmic_topology_id();

		if (g_mp_sel && cfg_vf_fuse_loaded()) {
			override_config_dvfs_maximum_level(DPM_DxS_DSU, cfg_vf_get_dsu_fmax(), false);
		} else if (topology == PMIC_PHECDA) {
			uint32_t pvt = boot_strap_valid ? STRAP_PVT_AVERAGE : 0;

			if (pvt >= 398 && pvt <= 404) {
				override_config_dvfs_maximum_level(DPM_DxS_DSU, 1200, false);
				dpm_dxs_override_Vset(DPM_DxS_DSU, 0, 4000000, 810);
				dpm_dxs_override_Vset(DPM_DxS_CORE_LIT, 0, 4000000, 810);
			} else if (pvt >= 394 && pvt <= 397) {
				override_config_dvfs_maximum_level(DPM_DxS_DSU, 1100, false);
				dpm_dxs_override_Vset(DPM_DxS_DSU, 0, 4000000, 800);
				dpm_dxs_override_Vset(DPM_DxS_CORE_LIT, 0, 4000000, 810);
				override_config_dvfs_maximum_level(DPM_DxS_CORE_GB0, 2400, false);
				override_config_dvfs_maximum_level(DPM_DxS_CORE_GB1, 2500, false);
				override_config_dvfs_maximum_level(DPM_DxS_CORE_GM0, 2200, false);
				override_config_dvfs_maximum_level(DPM_DxS_CORE_GM1, 2100, false);
			} else if (pvt <= 393) {
				override_config_dvfs_maximum_level(DPM_DxS_DSU, 1000, false);
				dpm_dxs_override_Vset(DPM_DxS_DSU, 0, 4000000, 820);
				dpm_dxs_override_Vset(DPM_DxS_CORE_LIT, 0, 4000000, 810);
				override_config_dvfs_maximum_level(DPM_DxS_CORE_GB0, 2200, false);
				override_config_dvfs_maximum_level(DPM_DxS_CORE_GB1, 2300, false);
				override_config_dvfs_maximum_level(DPM_DxS_CORE_GM0, 2100, false);
				override_config_dvfs_maximum_level(DPM_DxS_CORE_GM1, 2000, false);
			}
		}
	}

	if (gpu_voltage) {
		dpm_dxs_override_Vset(DPM_DxS_GPU_TOP, 0, 4000000, 780);
		dpm_dxs_override_Vset(DPM_DxS_GPU_CORE, 0, 4000000, gpu_voltage ? 780 : 0);
	}
	if (dsu_voltage)
		dpm_dxs_override_Vset(DPM_DxS_DSU, 0, 4000000, 790);
}

/*
 * The part's limits (opp_limit row) and the board's (dpm_dxs_set_opp_max):
 * each domain's table capped, the CPU clusters' sustained OPP at most 1.8
 * GHz (little) or 1.5 GHz unless the profile boots at the maximum, the
 * others' at their maximum.
 */
static void build_vf_table_apply_fmax(void)
{
	if (g_opp_limit_selector >= 10)
		calc_opp_limit_selector();
	for (uint32_t i = 0; i < DPM_DxS_MAX; i++) {
		uint32_t max_level_OPP = get_config_dvfs_opp_maximum_level(i);
		uint32_t max_level_OPN, sustained_level;

		if (!max_level_OPP)
			continue;
		max_level_OPN = opp_limit[g_opp_limit_selector][i];
		sustained_level = max_level_OPP;
		if (max_level_OPN < max_level_OPP) {
			override_config_dvfs_maximum_level(i, max_level_OPN, true);
			sustained_level = max_level_OPN;
		}
		dpm_dxs_set_opp_max(i, (uint16_t)sustained_level);
		if (i <= DPM_DxS_CORE_GB1 && !boot_with_core_fmax) {
			uint32_t boot_freq = i ? 1500 : 1800;

			if (sustained_level > boot_freq)
				sustained_level = boot_freq;
		}
		override_config_dvfs_sustained_level(i, sustained_level, true);
	}
}

/* CIX's builtin_perf_profile_setup */
static void builtin_perf_profile_setup(void)
{
	if ((get_config_dvfs_source() & 0xc0000000u) != 0x80000000u) {
		build_vf_table_part_1();
		build_vf_table_part_2();
	}
	override_100M_opp();
	build_vf_table_apply_fmax();
	if (is_lower_opp_run_on_770mV()) {
		for (uint32_t i = DPM_DxS_CORE_GM0; i <= DPM_DxS_CORE_GB1; i++)
			dpm_dxs_override_Vset(i, 0, 1200000, 770);
	}
}

/*
 * Clusters whose cores are all fused off (core_info's available cores, or
 * whose rail the board does not power): out of SCMI and sky1soc_perf, and
 * their maximum level 0.
 */
static void cpu_harvest_setup(void)
{
	static const struct {
		uint32_t cores;		/* mask of DPM elements (cores) */
		uint8_t elem;		/* DVFS element */
		uint8_t dxs;
	} clusters[] = {
		{ 0x00f, DVFS_ELEMENT_IDX_LITTLE, DPM_DxS_CORE_LIT },
		{ 0x030, DVFS_ELEMENT_IDX_MID_G0, DPM_DxS_CORE_GM0 },
		{ 0x0c0, DVFS_ELEMENT_IDX_MID_G1, DPM_DxS_CORE_GM1 },
		{ 0x300, DVFS_ELEMENT_IDX_BIG_G0, DPM_DxS_CORE_GB0 },
		{ 0xc00, DVFS_ELEMENT_IDX_BIG_G1, DPM_DxS_CORE_GB1 },
	};
	uint32_t domain_mask = 0xfff, core_online_mask, offline_cores;
	uint32_t perf_doms_enable = 0xffffffffu;

	for (uint32_t edp_id = 0; edp_id < DPM_EDP_MAX; edp_id++) {
		const struct rail *pCfg = dpm_edp_get_pwr_rail_config((DPM_EDP_DOMAIN)edp_id);

		if (!pCfg || edp_id >= EDP_DSU || pCfg->limit)
			continue;
		/* a CPU rail without current: its cores are off */
		domain_mask &= ~(((1u << dpm_edp_to_elem_map[edp_id].elem_num) - 1)
				 << dpm_edp_to_elem_map[edp_id].elem0);
	}
	core_online_mask = core_info_valid ? (core_info[0] | (uint32_t)core_info[1] << 8) & 0xfff : 0xfff;
	core_online_mask &= domain_mask;
	if (core_online_mask != 0xfff) {
		offline_cores = core_online_mask ^ 0xfff;
		for (uint32_t c = 0; c < ARRAY_SIZE(clusters); c++) {
			if ((offline_cores & clusters[c].cores) != clusters[c].cores)
				continue;
			perf_doms_enable &= ~(1u << clusters[c].elem);
			override_config_sky1soc_perf(clusters[c].elem);
			dpm_dxs_set_opp_max(clusters[c].dxs, 0);
		}
	}
	override_config_scmi_perf(perf_doms_enable);
}

/* ss_por_enter, before the PMICs: the BIOS configuration and the fuses */
void dpm_por_fuses(void)
{
	do_pm_config();
	ibase_fuse_init();
	load_and_check_opn_valid();
	cfg_vf_fuse_init();
}

/* ss_por_enter, after the PMICs: the tables */
void dpm_por_vf_tables(void)
{
	builtin_perf_profile_setup();
	cpu_harvest_setup();
}
