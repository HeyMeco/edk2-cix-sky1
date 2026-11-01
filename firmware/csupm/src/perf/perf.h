/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Performance streams (CIX's Source/common/soc/activities/act_streams.c,
 * Source/projects/sky1/app_pmucnt.c) and their sources: the AP's activity
 * monitors and throttling controls (soc/ap/ap_event.c), the CI-700 mesh's
 * and NI-700 hubs' PMUs (soc/ci700, soc/ni700), the DDR controllers' and
 * NPU's utilisation (soc/ddr/ddr.c, soc/npu/npu.c), the OPP trace
 * (soc/dvfs/dvfs_opp.c); the counter blocks (drivers/dev_counters.c,
 * dev_counters.h) and CIX's hardware mutexes (utilities/u_mutex.c).
 *
 * A stream samples a source every tick (1 ms, the coarse tick; the DDR
 * and NPU sources also every 50 us, the fine tick) and either feeds the
 * DPM (the fixed polling pm_ready starts: AMU, DDR, NPU) or, when the AP
 * asks for streams (PM message 0x04), packs the increments into packages
 * on RTT channel 1.  CIX's names, types and prototypes.
 */
#ifndef PERF_H
#define PERF_H

#include "clk/clk.h"
#include "pm.h"

/* --- act_streams.c ---------------------------------------------------------------- */

typedef enum {
	PERF_STREAM_NONE = 0,
	PERF_STREAM_NI700_BASE_IDX = 1,
	PERF_STREAM_NI700_MM_PMU = 1,
	PERF_STREAM_NI700_PCIE_PMU = 2,
	PERF_STREAM_NI700_SYS_PMU = 3,
	PERF_STREAM_NI700_SMN_PMU = 4,
	PERF_STREAM_CI700_PMU = 5,
	PERF_STREAM_CLOCK_SINGLE_NODE = 6,
	PERF_STREAM_CLOCK_ALL_IN_ONE = 7,
	PERF_STREAM_AP_AMU = 8,
	PERF_STREAM_DVFS_OPP_SET = 9,
	PERF_STREAM_PMIC = 10,
	PERF_STREAM_PVT = 11,
	PERF_STREAM_DDR = 12,
	PERF_STREAM_NPU = 13,
	PERF_STREAM_TYPE_MAX = 13,
} PERF_STREAM_TYPE;

typedef union {
	struct {
		uint32_t port_mask;
		uint8_t event_id;
	} ni700;
	struct {
		uint32_t node_mask;
		uint8_t event_encode;
		uint16_t cascade_mode;
	} ci700;
	struct {
		uint32_t core_mask;
	} amu;
	struct {
		uint32_t clk_enum_idx;
	} clock;
	struct {
		uint32_t domain_mask;
	} dvfs_opp;
	struct {
		uint16_t pvt_idx_bitmask;
	} pvt;
	uint32_t raw[2];
} PERF_STREAM_CONFIG;

#define PERF_STREAM_INVALID	0xff

void perf_stream_init(void);
void perf_stream_reset(void);
void perf_stream_suspend(void);
void perf_stream_restore(void);
uint8_t perf_stream_create(PERF_STREAM_TYPE type, uint16_t interval_ms, const PERF_STREAM_CONFIG *cfg);
bool perf_stream_release(uint8_t stream_id);
uint32_t perf_stream_lost_pkg(void);
void perf_stream_set_log_flag(uint8_t flag);
bool perf_stream_is_active(void);
void perf_stream_track_clk_change(uint32_t clk_id, uint32_t clk_freq_khz);
uint32_t perf_stream_calc_delta_of_pmu(uint8_t stream_id);
void *perf_stream_get_pkg(void);
void perf_stream_put_pkg(void *pkg);
bool perf_soc_is_in_idle_state(void);
void perf_soc_busy_to_idle(void);
void perf_soc_idle_to_busy(void);

/* The coarse tick's two halves (scp/module/dpm): the snapshot, where CIX
 * takes it (its timer's interrupt), and the streams' work (CIX's event) */
void perf_stream_coarse_tick_handler(void *pData);
void perf_stream_event_handler(uint32_t evtid, uint32_t signaledBits);

/* The coarse tick's timer and event (CIX's md_timer and md_event; the
 * timer module's alarm in scp/module/dpm) */
void perf_tick_timer_enable(void);
void perf_tick_timer_disable(void);
void perf_tick_event_signal(void);
void perf_tick_event_clear(void);

/* --- app_pmucnt.c --------------------------------------------------------------- */

void sky1_pmu_fixed_polling_enable(void);
void sky1_pmu_fixed_polling_disable(void);
bool sky1_pmu_is_fixed_polling_enabled(void);
void app_stream_event_post_handler(uint32_t perf_tick, uint64_t sys_cnt);
bool perf_soc_idle_switch_set_enable(bool to_set);
void perf_soc_idle_switch_set_thro(uint32_t amu1_thro0, uint32_t amu1_thro1, uint32_t ddr_dym_pwr_thro,
				   uint32_t hysteresis, uint32_t min_interval);
void perf_soc_idle_switch_get_thro(uint32_t *amu1_thro0, uint32_t *amu1_thro1, uint32_t *ddr_dym_pwr_thro,
				   uint32_t *hysteresis, uint32_t *min_interval);
bool perf_soc_idle_switch_get_enable(void);
float perf_soc_idle_residence_get(void);
bool sky1_amu_copy_counter(uint32_t *amu, uint64_t *sys_cnt, uint32_t *sys_cnt_delta);

/* --- ap_event.c ------------------------------------------------------------------- */

#define AP_AMU_CORES		13	/* 12 cores and the DSU */

bool ap_is_clk_on(uint32_t ele);
uint64_t amu_read(uint32_t core_idx, uint32_t cnt);
void ap_setup_amu_cntr(uint32_t core_mask);
void ap_clear_amu_cntr(void);
void ap_fetch_amu_cntr_snapshot(void);
bool ap_amu_contain_data(uint8_t core_idx);
uint32_t ap_get_and_clr_amu_event_incre(uint8_t core_idx, uint8_t cnt);
bool ap_halt_by_clk_gate(void);
void ap_resume_by_clk_gate(void);
void ap_set_mpmm_gear(uint32_t mask, uint32_t gear);
void ap_clr_mpmm_gear(uint32_t mask);
void ap_set_dispatch_block(uint32_t mask);
void ap_clr_dispatch_block(uint32_t mask);

/* --- ddr.c, npu.c ----------------------------------------------------------------- */

void ddr_util_tick_handler(uint32_t sample_increase);
void ddr_util_get_reg_accum(uint32_t *regs, uint32_t *cnt);
bool npu_check_power_state(void);
void npu_tick_handler(uint32_t sample_increase);
void npu_get_reg_accum(uint32_t *m, uint32_t *accum_cnt);
void npu_cntr_setup(void);

/* --- dvfs_opp.c ------------------------------------------------------------------- */

void dvfs_opp_evt_stream_init(void);
void dvfs_opp_setup_cntr(uint16_t domain_mask);
void dvfs_opp_clear_all_cntr(void);
void dvfs_opp_fetch_snapshot(void);
void dvfs_opp_free_snapshot(void);
uint8_t dvfs_opp_item_cnt(void);
const void *dvfs_opp_get_item(uint8_t idx);
void dvfs_opp_put_event(uint8_t domain, uint32_t level);

/* --- ni700_pmu.c, ci700_*.c --------------------------------------------------------- */

typedef struct ni700_instance NI700_INSTANCE;
extern NI700_INSTANCE *const _s_ni700_hubs[4];

bool ni700_setup_pmu_events(NI700_INSTANCE *ni_inst, uint32_t port_mask, uint8_t event_id,
			    uint32_t *cnt_bitmap);
bool ni700_free_pmu_events(NI700_INSTANCE *ni_inst, uint32_t cnt_bitmap);
void ni700_free_all_pmu_counters(NI700_INSTANCE *ni_inst);
void ni700_take_pmu_cnt_snapshot(NI700_INSTANCE *ni_inst);
void ni700_fetch_pmu_cnt_snapshot(NI700_INSTANCE *ni_inst);
uint32_t ni700_calc_event_increment(NI700_INSTANCE *ni_inst, uint32_t cnt_bitmap);
uint32_t ni700_clk_tree_idx(const NI700_INSTANCE *ni_inst);

typedef struct ci700_pmu_evt_collection CI700_PMU_EVT_COLLECTION;

CI700_PMU_EVT_COLLECTION *ci700_create_pmu_event_collection(uint32_t node_mask, uint8_t event_encode,
							    uint16_t cascade_mode);
bool ci700_free_pmu_event_collection(CI700_PMU_EVT_COLLECTION *coll);
uint32_t ci700_increment_pmu_event_collection(CI700_PMU_EVT_COLLECTION *coll);
void ci700_free_all_pmu_counters(void);
void ci700_take_pmu_cnt_snapshot(void);
void ci700_fetch_pmu_cnt_snapshot(void);
void ci700_dfd_trace_init(void);
void ci700_dfd_read_sampling(void);
bool ci700_dfd_trace_process(void *buf);

/* --- u_mutex.c -------------------------------------------------------------------- */

bool hw_mutex_try_lock(uint32_t mutex_idx, uint8_t entity_id);
bool hw_mutex_unlock(uint32_t mutex_idx, uint8_t entity_id);

#endif
