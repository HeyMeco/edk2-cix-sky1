/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The fixed polling that feeds the DPM (CIX's
 * Source/projects/sky1/app_pmucnt.c).
 *
 * pm_ready starts three streams: the AP's activity monitors (all 12 cores
 * and the DSU), the DDR controllers' and the NPU's utilisation.  Every
 * tick the AMU increments are added up in one of two buffers; every fifth
 * tick the buffers swap and the DPM (dpm_refresh_dashboard) gets the
 * completed one with the AP's counter at the swap.  From the cores' busy
 * cycles and the memory's power CIX decides whether the SoC idles (with
 * the idle switch on, PM message 0x803: DDR to low power, the core
 * slower) and keeps the time it spent idle.  Every tick ends with the
 * DPM's (dpm_ticker).  CIX's dump_dfs_gen (debug CLI) is left out.
 */
#include "board/board.h"
#include "dpm/dpm_rt.h"
#include "lcru.h"
#include "mclp/mclp.h"
#include "perf.h"

#include "dev_counters.h"

/* CIX's PMU_POLLING_CTX */
static const struct {
	PERF_STREAM_CONFIG config;
	PERF_STREAM_TYPE type;
} _s_polling_ctx[3] = {
	{ .type = PERF_STREAM_DDR },
	{ .config.amu.core_mask = 0x1fff, .type = PERF_STREAM_AP_AMU },
	{ .type = PERF_STREAM_NPU },
};

static uint64_t _s_cnts_ts[2];			/* the AP's counter at each swap */
static uint64_t amu1_history[10];		/* sky1_check_dense_polling's */
static uint32_t _s_amu_cnts[2][AP_AMU_CORES][5];
static uint64_t amu1_sum;
static uint64_t ts_last_check;
static uint64_t soc_idle_residence, soc_busy_residence;
static bool _s_polling_init_done, _s_polling_enabled;
static uint8_t _s_ping_pong;
static bool _s_data_avail;
static bool soc_idle_switch;
static uint8_t amu1_index;
static bool soc_idle_indicator;
static uint8_t runtime_stream[3];
static float latest_idle_residence;
static uint32_t go_to_idle_hysteresis;
static uint32_t global_residence_update_interval;

/* min_interval (ms), amu1_thro0, amu1_thro1, ddr_dym_pwr_thro (mW), hysteresis */
uint32_t soc_idle_min_interval[5] = { 3000 };

/* the longest post-handler run (cycles), in a PM scratch word */
#define SCRATCH_POST_HANDLER_CC	(PM_SCRATCH + 0x14)

void sky1_pmu_fixed_polling_enable(void)
{
	_s_polling_init_done = false;
	perf_stream_reset();
	_s_polling_enabled = true;
	for (uint32_t i = 0; i < 3; i++) {
		if (_s_polling_ctx[i].type)
			runtime_stream[i] = perf_stream_create(_s_polling_ctx[i].type, 1, &_s_polling_ctx[i].config);
		else
			runtime_stream[i] = PERF_STREAM_INVALID;
	}
	memset(_s_amu_cnts, 0, sizeof(_s_amu_cnts));
	_s_polling_init_done = true;
}

void sky1_pmu_fixed_polling_disable(void)
{
	for (uint32_t i = 0; i < 3; i++)
		(void)perf_stream_release(runtime_stream[i]);
	_s_polling_enabled = false;
	_s_polling_init_done = false;
}

bool sky1_pmu_is_fixed_polling_enabled(void)
{
	return _s_polling_enabled;
}

/* Each tick with the fixed polling on: the AMU increments added up; every
 * fifth tick the buffers swap; the DPM's turn */
void app_stream_event_post_handler(uint32_t perf_tick, uint64_t sys_cnt)
{
	uint32_t cc_start = ccount(), cc;

	if (!_s_polling_init_done)
		return;
	for (uint8_t c = 0; c < AP_AMU_CORES; c++) {
		if (!ap_amu_contain_data(c))
			continue;
		for (uint8_t k = 0; k < 5; k++)
			_s_amu_cnts[_s_ping_pong][c][k] += ap_get_and_clr_amu_event_incre(c, k);
	}
	if (perf_tick % 5 == 0) {
		uint8_t pp = _s_ping_pong;

		_s_data_avail = true;
		_s_cnts_ts[pp] = sys_cnt;
		_s_ping_pong = !pp;
		memset(_s_amu_cnts[!pp], 0, sizeof(_s_amu_cnts[0]));
		cc = ccount() - cc_start;
		if (REG32(SCRATCH_POST_HANDLER_CC) < cc)
			REG32(SCRATCH_POST_HANDLER_CC) = cc;
	}
	dpm_ticker(NULL);
}

bool perf_soc_idle_switch_set_enable(bool to_set)
{
	if (to_set) {
		if (!mclp_hwif_is_dfs_supported())
			return false;	/* CIX logs it */
		soc_idle_switch = to_set;
		dpdpu_trigger_ddr_dfs_compatible_checker();
	} else {
		soc_idle_switch = to_set;
		perf_soc_idle_to_busy();
	}
	return true;
}

void perf_soc_idle_switch_set_thro(uint32_t amu1_thro0, uint32_t amu1_thro1, uint32_t ddr_dym_pwr_thro,
				   uint32_t hysteresis, uint32_t min_interval)
{
	soc_idle_min_interval[1] = amu1_thro0;
	soc_idle_min_interval[2] = amu1_thro1;
	soc_idle_min_interval[3] = ddr_dym_pwr_thro;
	soc_idle_min_interval[4] = hysteresis;
	soc_idle_min_interval[0] = min_interval;
}

void perf_soc_idle_switch_get_thro(uint32_t *amu1_thro0, uint32_t *amu1_thro1, uint32_t *ddr_dym_pwr_thro,
				   uint32_t *hysteresis, uint32_t *min_interval)
{
	if (amu1_thro0)
		*amu1_thro0 = soc_idle_min_interval[1];
	if (amu1_thro1)
		*amu1_thro1 = soc_idle_min_interval[2];
	if (ddr_dym_pwr_thro)
		*ddr_dym_pwr_thro = soc_idle_min_interval[3];
	if (hysteresis)
		*hysteresis = soc_idle_min_interval[4];
	if (min_interval)
		*min_interval = soc_idle_min_interval[0];
}

bool perf_soc_idle_switch_get_enable(void)
{
	return soc_idle_switch;
}

float perf_soc_idle_residence_get(void)
{
	return latest_idle_residence;
}

/*
 * The DPM's copy of the completed buffer (CIX's sky1_amu_copy_counter),
 * false if there is none new.  Then CIX's sky1_check_dense_polling: the
 * cores' busy cycles (AMU counter 1) over the last ten copies and the
 * memory's power say whether the SoC idles (with hysteresis); the time
 * idle and busy, their ratio every 400 copies; with the idle switch on,
 * the switch.
 */
bool sky1_amu_copy_counter(uint32_t *amu, uint64_t *sys_cnt, uint32_t *sys_cnt_delta)
{
	uint8_t done;
	uint64_t sum = 0, now, d;
	int32_t ddr_mw;
	bool compat;

	if (!_s_data_avail)
		return false;
	done = !_s_ping_pong;
	memcpy(amu, _s_amu_cnts[done], sizeof(_s_amu_cnts[0]));
	*sys_cnt_delta = (uint32_t)_s_cnts_ts[done] - (uint32_t)_s_cnts_ts[!done];
	*sys_cnt = _s_cnts_ts[done];
	_s_data_avail = false;

	for (uint32_t c = 0; c < 12; c++)
		sum += amu[5 * c + 1];
	amu1_history[amu1_index] = sum;
	amu1_index = amu1_index + 1 > 9 ? 0 : amu1_index + 1;
	amu1_sum = amu1_sum + sum - amu1_history[amu1_index];
	ddr_mw = dpm_main_tab.power_elem_mW[DPM_ELE_MEMORY];
	if (amu1_sum <= soc_idle_min_interval[1] && ddr_mw < (int32_t)soc_idle_min_interval[3]) {
		if (!go_to_idle_hysteresis)
			soc_idle_indicator = true;
		else
			go_to_idle_hysteresis--;
	} else if (!(ddr_mw < (int32_t)soc_idle_min_interval[3] && amu1_sum < soc_idle_min_interval[2])) {
		soc_idle_indicator = false;
		go_to_idle_hysteresis = soc_idle_min_interval[4];
	}

	now = dev_cnts_get_ap_free_counter();
	d = now - ts_last_check;
	if (soc_idle_indicator)
		soc_idle_residence += d;
	else
		soc_busy_residence += d;
	ts_last_check = now;
	npu_ddr_dfs_set_compatible(!npu_check_power_state());
	compat = ddr_dfs_check_compatible();
	if (global_residence_update_interval) {
		global_residence_update_interval--;
	} else {
		uint64_t total = soc_idle_residence + soc_busy_residence;

		latest_idle_residence = total ? (float)soc_idle_residence / (float)total : 0.0f;
		global_residence_update_interval = 400;
		soc_idle_residence = 0;
		soc_busy_residence = 0;
	}
	if (soc_idle_switch) {
		if (compat && soc_idle_indicator)
			perf_soc_busy_to_idle();
		else
			perf_soc_idle_to_busy();
	}
	return true;
}
