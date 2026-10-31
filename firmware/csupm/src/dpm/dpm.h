/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * V/F tables and the DVFS configuration (CIX's Source/projects/sky1/dpm:
 * cfg_fuse.c, cfg_dpm.c, cfg_pwr.c; Source/common/dpm/u_power.c,
 * pwr_rail_bind.c; Source/common/drivers/pm_config.c; the V/F part of
 * subsys_hsm.c; Thirdparty/scpfw/product/sky1/scp_ramfw/config_dvfs.c).
 *
 * The types are CIX's (from its DWARF), the functions keep CIX's names and
 * prototypes; each .c file says which of CIX's sources it translates.  The
 * tables are generated from CIX's image (dpm_data.c, scripts/open/csupm_dpm.py).
 * What other parts of the firmware use is in dvfs_cfg.h.
 */
#ifndef DPM_H
#define DPM_H

#include "dvfs_cfg.h"
#include "pm.h"
#include "pmic.h"

/* --- CIX's types ------------------------------------------------------------ */

typedef enum {
	DPM_ELE_CPU_L0, DPM_ELE_CPU_L1, DPM_ELE_CPU_L2, DPM_ELE_CPU_L3,
	DPM_ELE_CPU_M0, DPM_ELE_CPU_M1, DPM_ELE_CPU_M2, DPM_ELE_CPU_M3,
	DPM_ELE_CPU_B0, DPM_ELE_CPU_B1, DPM_ELE_CPU_B2, DPM_ELE_CPU_B3,
	DPM_ELE_DSU, DPM_ELE_GPU_TOP, DPM_ELE_GPU_CORE, DPM_ELE_CI, DPM_ELE_MMHUB,
	DPM_ELE_VPU, DPM_ELE_NPU, DPM_ELE_MEMORY, DPM_ELE_MAX,
} DPM_ELEMENTS;

/* Board layouts (CIX's DPM_EDP_CONFIG_TYPE is src/pmic.c's enum pmic_preset) */
typedef enum pmic_preset DPM_EDP_CONFIG_TYPE;
#define DPM_EDP_CFG_MAX		PMIC_PRESETS

typedef enum {
	ATE_PHACE_ES, ATE_PHACE_DVT, ATE_PHACE_PVT, ATE_PHACE_MP, ATE_PHACE_MP2, ATE_PHACE_MP3,
} EM_ATE_PHACE;

/* The part number fuse (cfg_fuse.c) */
typedef union {
	uint64_t opn_fuse64;
	struct {
		uint32_t opn_l32;
		uint32_t opn_h32;
	};
} OPN_FUSE_T;

/* V/F points read from the eFuse copy (cfg_fuse.c core_vf_patch, core_vf_curve) */
typedef struct {
	uint16_t bit_offset;
	bool valid;
	struct {
		uint16_t freq;		/* MHz */
		uint16_t vmin;		/* mV */
	} raw[3];
} VF_FUSE_DATA;

/* Power of a domain against frequency (u_power.c) */
typedef struct {
	uint32_t freq_khz;
	float pwr_coef;
	uint32_t volt_mV;
	float pwr_static;
	float dynmc_pwr;
} U_PWR_COEF_MAP;

typedef struct {
	uint16_t freq_MHz;
	uint16_t pwr_mW;
} pwr_data_t;

struct dvfs_elem_id_map_tab {
	uint16_t clk_id;	/* CLK_TREE_ENUM_IDX */
	uint8_t dxs_id;		/* DPM_DXS_DOMAIN */
	uint8_t edp_id;		/* DPM_EDP_DOMAIN */
};

typedef struct {
	uint8_t elem0;		/* DPM_ELEMENTS */
	uint32_t elem_num;
} DPM_EDP_TO_ELEM_MAP;

/* --- tables (dpm_data.c, generated) ----------------------------------------- */

extern struct mod_dvfs_opp *const mp_opp_tables[6][4];
extern struct mod_dvfs_opp *const test_opp_tables[6][9];
extern const char *const mp_opp_tab_name[4];
extern const char *const test_opp_tab_name[9];
extern struct mod_dvfs_opp gpu_core_opp_perf[8], gpu_top_opp_perf[8];
extern struct mod_dvfs_opp gpu_core_opp_budget[5], gpu_top_opp_budget[5];
extern struct mod_dvfs_opp npu_opp[5], vpu_opp[7], ci700_opp[8], mmhub_opp[5];
extern const uint16_t opp_limit[10][DPM_DxS_MAX];
extern uint16_t dpm_edp_opp_max[DPM_EDP_CFG_MAX][DPM_DxS_MAX];
extern const struct dvfs_elem_id_map_tab dvfs_elem_id_map_tab[DVFS_ELEMENT_IDX_COUNT];
extern const uint8_t dpm_dxs_to_dvfs_elem_map[12];
extern const uint8_t dpm_dvfs_elem_to_dxs_map[12];
extern const DPM_EDP_TO_ELEM_MAP dpm_edp_to_elem_map[DPM_EDP_MAX];
extern U_PWR_COEF_MAP *const dxs_pwrmap_ptr[DPM_DxS_MAX];
extern const uint8_t dxs_pwrmap_size[DPM_DxS_MAX];
extern const float dpm_freq2pwr_opp_table_coef[DPM_DxS_MAX];
extern const float dpm_freq2pwr_util_coef[DPM_DxS_MAX];
extern const pwr_data_t measured_data[8][7];
extern VF_FUSE_DATA core_vf_fuse[6];
extern const uint16_t bit_offsets[9];
extern const uint16_t gpu_bit_offsets[3];
extern U_PWR_COEF_MAP cpu_lit_pwr[5], cpu_mid0_pwr[19], cpu_mid1_pwr[19];
extern U_PWR_COEF_MAP cpu_big0_pwr[21], cpu_big1_pwr[21], dsu_pwr[3], gpu_pwr[7], dummy_pwr[9];

/* --- cfg_fuse.c ----------------------------------------------------------------- */

extern OPN_FUSE_T fuse_opn;
extern uint8_t vf_curve_loaded;		/* 1 loaded, 2 cleared, 3 disabled (CIX's default) */
extern uint8_t vf_patch_loaded;
extern uint8_t opn_check_flag;		/* 1 no fuse, 3 valid */
extern uint8_t g_vf_fuse_curve_fit_method;
extern uint16_t fuse_ibase[12][2];

EM_ATE_PHACE cfg_vf_get_ate_phase(void);
uint32_t cfg_vf_get_dsu_fmax(void);
bool cfg_fuse_opn_valid(void);
bool load_and_check_opn_valid(void);
bool opn_fuse_get(uint32_t *opn_high, uint32_t *opn_low);
void cfg_vf_fuse_init(void);
bool cfg_vf_fuse_loaded(void);
bool cfg_vf_fuse_has_vmin_data_exists(void);
bool is_lower_opp_run_on_770mV(void);
bool cfg_vf_calc_vmin_voltage(DPM_EDP_DOMAIN domain, struct mod_dvfs_opp *opp, uint32_t size);
void cfg_vf_ensure_voltage_increase(struct mod_dvfs_opp *opp, uint32_t size);
void cfg_vf_fuse_clear(void);
void cfg_vf_fuse_enable(void);
void cfg_vf_fuse_set_profile(uint32_t profile);
void ibase_fuse_init(void);

/* --- cfg_dpm.c ------------------------------------------------------------------ */

DPM_EDP_CONFIG_TYPE dpm_edp_cfg_get_type(void);
uint32_t dpm_edp_cfg_get_pmic_topology_id(void);
bool dpm_dxs_set_opp_max(DPM_DXS_DOMAIN dxs_id, uint16_t opp_max);

/* --- cfg_pwr.c, u_power.c --------------------------------------------------------- */

bool dpm_dxs_calc_scmi_opp(DPM_DXS_DOMAIN dxs_id, uint32_t freq_khz, uint32_t *vot_mV_ptr,
			   float *pwr_mW_ptr);
void dpm_dxs_override_Vset(DPM_DXS_DOMAIN dxs_id, uint32_t freq_khz_left,
			   uint32_t freq_khz_right, uint32_t Vset_mV);
void app_pwr_init(void);
void u_pwr_initMap(U_PWR_COEF_MAP *pwr_coef_map, uint32_t map_size);
uint32_t u_pwr_freq2Vot(uint32_t freq_khz, const U_PWR_COEF_MAP *pwr_coef_map, uint32_t map_size);
float u_pwr_calcPower(uint32_t freq_khz, uint32_t vot_mV, float util,
		      const U_PWR_COEF_MAP *pwr_coef_map, uint32_t map_size);

/* --- pm_config.c (the BIOS's PM configuration) ------------------------------------------ */

extern bool bios_cfg_valid;
void do_pm_config(void);
uint32_t pm_config_get_word(uint32_t offset);
const uint8_t *pm_config_get_opp(uint32_t idx);
void override_100M_opp(void);

/* --- config_dvfs.c (dvfs_cfg.c) ---------------------------------------------------- */

void override_config_dvfs_sustained_level(uint32_t dxs_id, uint32_t sustained_level, bool force);
void override_config_dvfs_minimum_level(uint32_t dxs_id, uint32_t minimum_level, bool force);
void override_config_dvfs_maximum_level(uint32_t dxs_id, uint32_t maximum_level, bool force);
void clear_config_dvfs_opp_table_voltage(uint32_t dxs_id, uint32_t freq_khz_left,
					 uint32_t freq_khz_right);
void override_config_dvfs_opp_source(uint8_t dvfs_elem_idx);
void set_config_dvfs_source(uint32_t opp_source);
uint32_t get_config_dvfs_source(void);
bool allocate_config_dvfs_opp_tables(bool is_mp, uint32_t table_selector);
const char *get_config_dvfs_opp_table_name(bool is_mp, uint32_t table_selector);

/* --- vf_table.c (subsys_hsm.c) ----------------------------------------------------- */

extern uint8_t g_mp_sel, g_opp_table_selector, g_opp_limit_selector;
extern bool gpu_voltage, dsu_voltage, gb1_boost_to_2P8G_for_CD, boot_with_core_fmax;

#endif
