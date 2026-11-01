/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The DPM at run time (CIX's Source/common/dpm: dpm_init.c, dpm_process.c,
 * x_arbiter.c, x_clk.c, u_pid.c; Source/projects/sky1/dpm: dpm_dashboard.c,
 * cfg_dpm.c, cfg_pwr.c, cfg_spt.c, cfg_util.c, cfg_x_clk.c,
 * sky1_pwr_model.c, sky1_pwr_databoard.c).
 *
 * Every millisecond the performance streams' tick (src/perf) hands the
 * DPM the AP's activity counters; the DPM refreshes its dashboard
 * (frequencies, voltages, utilisation, a power model per core, cluster and
 * rail), uploads the power to the AP's data board, runs its thermal
 * arbiters (PID controllers on the die temperature, skin temperature and
 * socket power, each choosing a throttling phase), turns the phases into
 * frequency limits per domain and exports them as dpm_main_tab.opp_run,
 * the TDP frequencies scmi_perf and sky1soc_perf cap the levels to.
 *
 * Types and layouts are CIX's (from its DWARF), names and prototypes too.
 */
#ifndef DPM_RT_H
#define DPM_RT_H

#include "board/board.h"
#include "dpm.h"
#include "pvt.h"

/* CIX keeps the DPM's initialised tables in DRAM1 (.dram1.data), as here */
#define DPM_DRAM1		__attribute__((section(".dram1.data")))

/* --- u_pid.c ------------------------------------------------------------------- */

typedef struct {
	volatile float *pInput;
	float CurInput;
	float SetPoint;
	float SetAmend;
	float FeedBack;
	float in_alpha;
	float fb_scale;
	float err;
	float err_last;
	float integral;
	float u_sum;
	float input_last;
	float Kp;
	float Ki;
	float Kd;
} T_PID_DATA;

void pid_updateTimeConst(T_PID_DATA *pid, float TimeConst);
void pid_setParams(T_PID_DATA *pid, float Kp, float Ki, float Kd);
void pid_setSetPoint(T_PID_DATA *pid, float SetPoint);
void pid_setAmend(T_PID_DATA *pid, float SetAmend);
void pid_setFbScale(T_PID_DATA *pid, float FbScale);
float pid_iteration(T_PID_DATA *pid);

/* --- x_arbiter.c --------------------------------------------------------------- */

typedef struct {
	float phase_cfg[8][8];	/* per phase, a throttling coefficient per EDP domain */
} X_ARB_CFG;

typedef struct {
	bool is_enabled;
	const X_ARB_CFG *pCfg;
	uint32_t *domain_expect;
	uint32_t *domain_limit;
	uint8_t phase_off_bitmap;
	float phase_record;
	uint8_t phase_id;
	T_PID_DATA *pid;
	uint32_t domain_off_bitmap;
	float domain_throttle_coeff[8];
	char *name;
} X_ARB_CTRL;

void x_arbit_init(X_ARB_CTRL *ctrl);
void x_arbit_iteration(X_ARB_CTRL *ctrl);
bool x_arbit_redist(X_ARB_CTRL *ctrl);

/* --- x_clk.c, cfg_x_clk.c -------------------------------------------------------- */

typedef struct {
	uint32_t freq_min;
	uint32_t freq_max;
	uint32_t freq_step;
	uint32_t freq_effective;
} T_CLK_DOMAIN_CFG;

typedef struct {
	const T_CLK_DOMAIN_CFG *pCfg;
	T_PID_DATA *pPid;
	uint32_t freq_range;
	uint32_t freq_tracking;
	uint32_t freq_target;
	uint32_t freq_delivered;
	uint8_t utilization;
	bool stick_to_effec_level;
} X_CLK_CTRL_T;

bool x_clk_init(X_CLK_CTRL_T *domain);
void cfg_x_clk_init(void);

/* --- dpm_dashboard.c ------------------------------------------------------------- */

typedef struct {
	uint32_t delivered_elem_freq_khz[DPM_ELE_MAX];
	uint32_t delivered_dxs_freq_khz[DPM_DxS_MAX];
	uint32_t delivered_vot_mV[DPM_EDP_MAX];
	volatile float die_temp;
	volatile float skin_temp;
	volatile float util_elem[DPM_ELE_MAX];
	float util_dxs[DPM_DxS_MAX];
	int32_t power_elem_mW[DPM_ELE_MAX];
	int32_t power_elem_leakage_mW[DPM_ELE_MAX];
	int32_t power_dxs_mW[DPM_DxS_MAX];
	int32_t power_edp_mW[DPM_EDP_MAX + 1];		/* + memory */
	int32_t power_edp_leakage_mW[DPM_EDP_MAX + 1];
	float power_edp_leakage_t[DPM_EDP_MAX + 1];
	volatile float socket_power;
	volatile float ccx_power;
	uint32_t exp_dxs_clk_khz[3][DPM_DxS_MAX];
	uint32_t upb_dxs_clk_khz[3][DPM_DxS_MAX];
	uint32_t lob_dxs_clk_khz[1][DPM_DxS_MAX];
	uint32_t edp_limit[DPM_EDP_MAX];
	const float *freq2pwr_coef;
	uint32_t exp_dxs_pwr_mW[DPM_DxS_MAX];
	uint32_t exp_edp_pwr_mW[DPM_EDP_MAX];
	X_ARB_CTRL *spt_ctrls;
	uint32_t grant_power[DPM_DxS_MAX];
	uint32_t *min_dxs_pwr_mW;
	const float *pwr2freq_coef;
	uint32_t opp_grant[DPM_DxS_MAX];
	uint32_t debug_upper[DPM_DxS_MAX];
	uint32_t debug_lower[DPM_DxS_MAX];
	uint32_t opp_run[DPM_DxS_MAX];
	uint32_t ddr_accum_cnt;
	uint32_t ddr_regs[8];
	uint32_t amu[DPM_DxS_MAX][5];
	uint32_t npu_accum_cnt;
	uint32_t npu[5];
	uint64_t ts;
	uint32_t ts_delta;
	uint32_t last_off_cycle[12];
	uint32_t off_cycle_delta[12];
	bool refresh_pwr_data;
} DPM_DASHBOARD;

extern DPM_DASHBOARD dpm_main_tab;

void dpm_refresh_dashboard(void);
void dpm_update_edp_limit(void);
bool dpm_params_consistency_checker(void);

/* --- cfg_dpm.c, cfg_pwr.c, cfg_util.c, cfg_spt.c ------------------------------------ */

typedef struct {
	uint8_t elem0;		/* DPM_ELEMENTS */
	uint32_t elem_num;
} DPM_DXS_TO_ELEM_MAP;

typedef struct {
	uint8_t dxs0;		/* DPM_DXS_DOMAIN */
	uint32_t dxs_num;
} DPM_EDP_TO_DXS_MAP;

extern const uint8_t dpm_ele_to_dxs_map[DPM_ELE_MAX];
extern const uint8_t dpm_ele_to_edp_map[DPM_ELE_MAX];
extern const uint8_t dpm_dxs_to_edp_map[DPM_DxS_MAX];
extern const DPM_DXS_TO_ELEM_MAP dpm_dxs_to_elem_map[DPM_DxS_MAX];
extern const DPM_EDP_TO_DXS_MAP dpm_edp_to_dxs_map[DPM_EDP_MAX + 1];
extern const T_CLK_DOMAIN_CFG *const dxs_clk_cfg[DPM_DxS_MAX];
extern uint32_t min_dxs_pwr_mW[DPM_DxS_MAX];
extern X_ARB_CTRL spt_ctrls[4];
extern T_PID_DATA spt_pids[4];
extern X_ARB_CFG g_spt_opp_lv_cfg;

void dpm_calc_expect_opp(void);
void dpm_export_opp(void);
void dpm_restore_opp(void);
void dpm_stream_actual_power(void);
void dpm_enable_stream_pwr(bool enable, uint32_t interval_ms);
bool dpm_is_stream_enabled(void);

int32_t dpm_elem_get_power(DPM_ELEMENTS elem_id);
int32_t dpm_edp_get_leakage_power(DPM_EDP_DOMAIN edp_id, float *pT);
uint32_t dpm_elem_calc_minimum_power(DPM_ELEMENTS elem_id, float util);
int32_t dpm_dxs_get_power(DPM_DXS_DOMAIN dxs_id);
uint32_t dpm_dxs_calc_minimum_power(DPM_DXS_DOMAIN dxs_id, float util);
int32_t dpm_edp_get_power(DPM_EDP_DOMAIN edp_id);
uint32_t dpm_edp_calc_minimum_power(DPM_EDP_DOMAIN edp_id, float util);

float calc_dxs_util(DPM_DXS_DOMAIN dxs_id);

void spt_set_skin_margin(float margin);
float spt_get_skin_margin(void);
void dpm_set_spt_cfg(X_ARB_CFG *spt_cfg);
void dpm_spt_iteration(void);
void dpm_spt_arbitration(void);
void dpm_update_opp_limit(void);

/* --- sky1_pwr_model.c ------------------------------------------------------------ */

typedef float (*dpm_pwr_dynamic_fn)(float v, float f, uint32_t *m);

typedef struct {
	dpm_pwr_dynamic_fn dynamic_func;
	uint32_t *m;
} dpm_pwr_compute_t;

extern const dpm_pwr_compute_t pwr_model[DPM_ELE_MAX];

float pwr_dynamic_hunter0(float v, float f, uint32_t *m);
float pwr_dynamic_hunter1(float v, float f, uint32_t *m);
float pwr_dynamic_hayes(float v, float f, uint32_t *m);
float pwr_dynamic_dsu(float v, float f, uint32_t *m);
float pwr_dynamic_npu(float v, float f, uint32_t *m);
float pwr_dynamic_ddr(float v, float f, uint32_t *m);
float pwr_dynamic_soc_p1(float v, float f, uint32_t *m);
float pwr_dynamic_soc_p2(float v, float f, uint32_t *m);
float pwr_dynamic_null(float v, float f, uint32_t *m);
float pwr_leakage(float v, float t, DPM_EDP_DOMAIN edp);
float pwr_gpu_top_leakage(float v, float t);
float pwr_gpu_core_leakage(float v, float t);
void pwr_leakage_init(uint16_t *ibase);
void pwr_notify_gpu_level(uint32_t gpu_opp_level);

/* --- sky1_pwr_databoard.c --------------------------------------------------------- */

void sky1_pwr_databoard_init(void);
void sky1_pwr_databoard_upload_ap_dsu_power(uint32_t id, int32_t power, int32_t leakage);
void sky1_pwr_databoard_read_gpu_power(int32_t *power);
void sky1_pwr_databoard_upload_gpu_leakage_power(int32_t power);
uint32_t sky1_pwr_databoard_read_ap_off_cycle(uint32_t id);

/* --- dpm_init.c, dpm_process.c, cfg_sim.c ------------------------------------------- */

void dpm_ticker(void *pData);
void dpm_enable(void);
void dpm_disable(void);
bool dpm_init(void);
bool dpm_sim_init(void);
void dpm_iteration(void);
/* the DPM's task (CIX's dpm_proc): an iteration when ticked and enabled */
void dpm_proc(void);

#endif
