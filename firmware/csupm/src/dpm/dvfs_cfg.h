/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The DVFS configuration as the rest of the firmware sees it: CIX's
 * config_dvfs.c accessors (the dvfs, scmi_perf and sky1soc_perf modules
 * and the DPM call them), the EDP rail voltages (CIX's dpm_edp_set_voltage
 * and dpm_edp_get_voltage, which its pmic module calls), the boot levels.
 * CIX's names and prototypes.
 *
 * Domains are numbered three ways, as in CIX's firmware: DVFS elements
 * (the dvfs module's elements, enum dvfs_element_idx), DxS domains
 * (DPM_DXS_DOMAIN, what most accessors take) and EDP domains (power rails,
 * DPM_EDP_DOMAIN, src/pmic.h's enum edp_domain).
 */
#ifndef DVFS_CFG_H
#define DVFS_CFG_H

/* the dvfs module's types, whether or not the module is built */
#if __has_include(<mod_dvfs.h>)
#include <mod_dvfs.h>
#else
#include "../../scp/module/dvfs/include/mod_dvfs.h"
#endif
#include <fwk_element.h>
#include <fwk_id.h>

#include <stdbool.h>
#include <stdint.h>

#include "pmic.h"

typedef enum {
	DPM_DxS_CORE_LIT,	/* little cores */
	DPM_DxS_CORE_GM0,	/* medium cores, pairs 0 and 1 */
	DPM_DxS_CORE_GM1,
	DPM_DxS_CORE_GB0,	/* big cores, pairs 0 and 1 */
	DPM_DxS_CORE_GB1,
	DPM_DxS_DSU,
	DPM_DxS_GPU_TOP,
	DPM_DxS_GPU_CORE,
	DPM_DxS_SOC_CI,
	DPM_DxS_SOC_MMHUB,
	DPM_DxS_SOC_VPU,
	DPM_DxS_SOC_NPU,
	DPM_DxS_SOC_MEMORY,
	DPM_DxS_MAX,
} DPM_DXS_DOMAIN;

/* EDP domains are src/pmic.h's enum edp_domain (EDP_CPU_LIT ... EDP_SOC) */
typedef enum edp_domain DPM_EDP_DOMAIN;
#define DPM_EDP_MAX		EDP_N

/* The dvfs module's elements (config_dvfs.c element_table) */
enum dvfs_element_idx {
	DVFS_ELEMENT_IDX_GPU_CORE,
	DVFS_ELEMENT_IDX_GPU_TOP,
	DVFS_ELEMENT_IDX_LITTLE,
	DVFS_ELEMENT_IDX_BIG_G0,
	DVFS_ELEMENT_IDX_BIG_G1,
	DVFS_ELEMENT_IDX_MID_G0,
	DVFS_ELEMENT_IDX_MID_G1,
	DVFS_ELEMENT_IDX_DSU,
	DVFS_ELEMENT_IDX_NPU,
	DVFS_ELEMENT_IDX_VPU,
	DVFS_ELEMENT_IDX_CI700,
	DVFS_ELEMENT_IDX_MMHUB,
	DVFS_ELEMENT_IDX_COUNT,
};

/*
 * The dvfs module's elements (CIX's static dvfs_get_element_table, the
 * generator of config_dvfs).  The table is built once, by
 * apply_config_dvfs_sustained_level at init (CIX's create_element_table):
 * OPPs, sustained OPP, levels, clocks.  Element i's data is a
 * struct mod_dvfs_domain_config.
 */
const struct fwk_element *dvfs_get_element_table(fwk_id_t module_id);

/* At init, after the clock tree: each domain's clock and rail to its
 * sustained OPP (builds the table first) */
bool apply_config_dvfs_sustained_level(void);

/* The domain configuration of a DxS domain, NULL if it has none */
struct mod_dvfs_domain_config *dxs_id_to_dvfs_domain_config(uint32_t dxs_id);
/* Level of the sustained OPP */
uint32_t get_config_dvfs_sustained_level(uint32_t dxs_id);
/* Performance level of the sustained OPP as reported to the OS (1000 when the
 * levels are frequencies in MHz) */
uint16_t get_reference_perf_level(uint32_t dxs_id);
/* Frequency (kHz) of a level: the level's, or the highest below it */
uint32_t get_dvfs_freq_from_level(uint32_t dxs_id, uint32_t level);
bool check_opp_level_existing(uint32_t dxs_id, uint32_t level);
/* The levels, at most *length; *length is set to how many */
bool copy_config_dvfs_opp_levels(uint32_t dxs_id, uint32_t *opp_array, uint32_t *length);
uint32_t get_config_dvfs_opp_maximum_level(uint32_t dxs_id);
/* Frequency (kHz) of the nth OPP, of the last when nth is past it */
uint32_t get_config_dvfs_opp_nth_freq(uint32_t dxs_id, uint32_t nth);
/* Clock tree node (CLK_TREE_ENUM_IDX) of a DxS domain */
uint32_t get_clk_idx_from_dxs_id(DPM_DXS_DOMAIN dxs_id);
/* GPU core level to the GPU core and top frequencies (MHz) and top level
 * (scmi_perf's fast channels) */
bool search_for_freq(uint32_t core_level, uint32_t *core_freq, uint32_t *top_level,
		     uint32_t *top_freq);
/* Measured power (mW) at a frequency (MHz), interpolated */
uint32_t u_pwr_get_measured_power(uint32_t dxs_id, uint32_t freq_MHz);

/* Highest level (MHz) the board allows a DxS domain (cfg_dpm.c) */
uint16_t dpm_dxs_get_opp_max(DPM_DXS_DOMAIN dxs_id);

/* EDP rails (CIX's dpm_dashboard.c, pwr_rail_bind.c): set a rail to mV (the
 * board's IR-drop offset added, at most 980 mV), and the voltage last set
 * (or the boot voltage of a rail without regulator) */
bool dpm_edp_set_voltage(DPM_EDP_DOMAIN edp_id, uint32_t vol_mV);
bool dpm_edp_get_voltage(DPM_EDP_DOMAIN edp_id, uint16_t *pVol_mV);
/* The rail of an EDP domain on this board (CIX's DPM_PWR_RAIL_CFG; src/pmic.c's
 * layout), NULL past the last */
const struct rail *dpm_edp_get_pwr_rail_config(DPM_EDP_DOMAIN edp_id);

/*
 * Defined by the scmi_perf and sky1soc_perf configurations (CIX's
 * config_scmi_perf.c, config_sky1soc_perf.c): cpu_harvest_setup at power-on
 * takes the clusters whose cores are all fused off out of SCMI (bit per
 * DVFS element) and out of sky1soc_perf (DVFS element index).
 */
void override_config_scmi_perf(uint32_t perf_domain_enable_mask);
void override_config_sky1soc_perf(uint32_t domaid_id_for_disable);

/* Power-on (CIX's ss_por_enter): fuses, then after the PMICs the V/F tables */
void dpm_por_fuses(void);
void dpm_por_vf_tables(void);

#endif
