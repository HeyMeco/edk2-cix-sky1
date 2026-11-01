/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * CIX's cfg_dpm.c: the board layout's DVFS limits (dpm_dxs_get_opp_max,
 * dpm_dxs_set_opp_max) and the DPM's outputs: the levels the AP asks for
 * (dpm_calc_expect_opp), the limits to scmi_perf and sky1soc_perf
 * (dpm_export_opp, dpm_restore_opp), the power data board and the power
 * stream (dpm_stream_actual_power, dpm_stream_fill_common,
 * dpm_enable_stream_pwr, dpm_is_stream_enabled).
 *
 * CIX keeps each layout's rails and OPP maxima in one table, dpm_edp_scheme;
 * the rails are src/board's (with CIX's dpm_edp_cfg_get_type and the rest of
 * the layout code), the maxima dpm_data.c's dpm_edp_opp_max.  CIX's
 * dpm_elem_get_clk_id is src/clk's; dpm_update_dxs_clk_cfg,
 * dpm_calc_expect_power and prochot_throttle_Fmax_Khz (src/board) are not
 * in CIX's cfg_dpm.c code (unused or elsewhere).
 */
#include <mod_scmi_perf.h>

#include "dpm_rt.h"
#include "perf/perf.h"

uint16_t dpm_dxs_get_opp_max(DPM_DXS_DOMAIN dxs_id)
{
	DPM_EDP_CONFIG_TYPE t = dpm_edp_cfg_get_type();

	if (dxs_id >= DPM_DxS_MAX || t >= DPM_EDP_CFG_MAX)
		return 0;
	return dpm_edp_opp_max[t][dxs_id];
}

bool dpm_dxs_set_opp_max(DPM_DXS_DOMAIN dxs_id, uint16_t opp_max)
{
	DPM_EDP_CONFIG_TYPE t = dpm_edp_cfg_get_type();

	if (dxs_id >= DPM_DxS_MAX || t >= DPM_EDP_CFG_MAX)
		return false;
	dpm_edp_opp_max[t][dxs_id] = opp_max;
	return true;
}

/* --- the DPM's outputs (the rest of CIX's cfg_dpm.c) ------------------------------ */

/* The AP's fast channels where it asks for a level (mailbox 0), per DxS
 * domain; the others' expected level is their sustained one */
static const uint32_t dpm_fc_set_addr[DPM_DxS_MAX] = {
	0xa0080094, 0xa00800a0, 0xa00800a4, 0xa0080098, 0xa008009c, 0xa00800a8, 0xa0080090, 0xa008008c,
};

/* What the AP asks for: the frequency of each domain's requested level */
void dpm_calc_expect_opp(void)
{
	DPM_DASHBOARD *t = &dpm_main_tab;

	for (uint32_t d = 0; d < DPM_DxS_MAX; d++) {
		uint32_t level;

		t->exp_dxs_clk_khz[0][d] = 0;
		t->exp_dxs_clk_khz[1][d] = 0;
		level = d < 8 ? REG32(dpm_fc_set_addr[d]) : get_config_dvfs_sustained_level(d);
		t->exp_dxs_clk_khz[2][d] = get_dvfs_freq_from_level(d, level);
	}
}

/*
 * The limits to scmi_perf and sky1soc_perf (dpm_main_tab.opp_run, kHz): the
 * granted frequency of each domain (the CPU clusters' at most PROCHOT's),
 * within the debug bounds, on the domain's step, within its range; 100 MHz
 * for all while the die arbiter is in its last phase.  A change makes
 * scmi_perf apply the levels again (on_fast_channel_interrupt); the NPU's
 * and VPU's are told by on_tdp_notify.  CIX's oddities kept: the DSU is
 * granted 0 (no limit); only the first 8 domains and those two are
 * exported, the others stay 0; the phase-7 counter counts each domain.
 */
void dpm_export_opp(void)
{
	static uint32_t hysteresis_counter;
	DPM_DASHBOARD *t = &dpm_main_tab;
	uint32_t opp[DPM_DxS_MAX], tdp = 0;
	bool changed = false;
	uint8_t phase;

	for (uint32_t d = 0; d < 5; d++) {
		if (prochot_throttle_Fmax_Khz() < t->opp_grant[d])
			t->opp_grant[d] = prochot_throttle_Fmax_Khz();
	}
	phase = t->spt_ctrls[3].phase_id;
	t->opp_grant[DPM_DxS_DSU] = 0;
	for (uint32_t d = 0; d < DPM_DxS_MAX; d++) {
		const T_CLK_DOMAIN_CFG *c = dxs_clk_cfg[d];
		uint32_t v = t->opp_grant[d];

		opp[d] = v;
		if (t->debug_lower[d])
			v = v > t->debug_lower[d] ? v : t->debug_lower[d];
		if (t->debug_upper[d])
			v = v < t->debug_upper[d] ? v : t->debug_upper[d];
		v -= v % c->freq_step;
		v = v < c->freq_max ? v : c->freq_max;
		opp[d] = !v ? 0 : v < c->freq_min ? c->freq_min : v;
		if (phase == 7) {
			hysteresis_counter++;
			opp[d] = 100000;
		} else if (hysteresis_counter) {
			hysteresis_counter = 0;
		}
	}
	if (memcmp(t->opp_run, opp, 8 * sizeof(uint32_t))) {
		memcpy(t->opp_run, opp, 8 * sizeof(uint32_t));
		changed = true;
	}
	if (t->opp_run[DPM_DxS_SOC_VPU] != opp[DPM_DxS_SOC_VPU]) {
		t->opp_run[DPM_DxS_SOC_VPU] = opp[DPM_DxS_SOC_VPU];
		tdp |= BIT(DVFS_ELEMENT_IDX_VPU);
	}
	if (t->opp_run[DPM_DxS_SOC_NPU] != opp[DPM_DxS_SOC_NPU]) {
		t->opp_run[DPM_DxS_SOC_NPU] = opp[DPM_DxS_SOC_NPU];
		tdp |= BIT(DVFS_ELEMENT_IDX_NPU);
	}
	if (changed)
		on_fast_channel_interrupt(false);
	if (tdp)
		on_tdp_notify(tdp);
}

/* No limits (CIX's dpm_disable) */
void dpm_restore_opp(void)
{
	memset(dpm_main_tab.opp_run, 0, sizeof(dpm_main_tab.opp_run));
	on_fast_channel_interrupt(false);
}

/* The power stream to the AP (CIX's runtime-power package, every
 * _s_stream_intvl_cnt fifth ticks while enabled) */
static struct {
	bool enabled;
	uint32_t cnt;
	uint32_t intvl;
} _s_stream_actual_pwr;

void dpm_enable_stream_pwr(bool enable, uint32_t interval_ms)
{
	_s_stream_actual_pwr.enabled = enable;
	_s_stream_actual_pwr.intvl = interval_ms / 5;
}

bool dpm_is_stream_enabled(void)
{
	return _s_stream_actual_pwr.enabled;
}

static void put_le32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

static uint32_t get_le32(const uint8_t *p)
{
	return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

typedef union {
	float f;
	uint32_t u;
} fbits;

static void put_float(uint8_t *p, float f)
{
	fbits x = { .f = f };

	put_le32(p, x.u);
}

/*
 * An element's record (20 bytes): dynamic power, its rail's leakage (a CPU
 * rail's shared by its cores), voltage, frequency (GHz), the rail's
 * temperature
 */
static void dpm_stream_fill_common(uint8_t *buf, uint32_t elem)
{
	const DPM_DASHBOARD *t = &dpm_main_tab;
	uint32_t edp = dpm_ele_to_edp_map[elem];
	float v = (float)t->delivered_vot_mV[edp] * 0.001f;
	float f = (float)t->delivered_elem_freq_khz[elem] / 1000000.0f;
	float leak = (float)t->power_edp_leakage_mW[edp];

	put_float(buf, (float)t->power_elem_mW[elem]);
	put_float(buf + 4, leak);
	put_float(buf + 8, v);
	put_float(buf + 12, f);
	put_float(buf + 16, t->power_edp_leakage_t[edp]);
	if (edp < 5)
		put_float(buf + 4, leak / (float)dpm_edp_to_elem_map[edp].elem_num);
}

/*
 * Each iteration with new power data: the cores' and DSU's power to the
 * data board; with the power stream on, every intvl such iterations, a
 * package (type 6, 688 bytes) to the AP's stream: per core and the DSU its
 * record, leakage and AMU counters; the GPU, NPU, memory and SoC; the DDR
 * and NPU counters; board and skin temperatures.
 */
void dpm_stream_actual_power(void)
{
	DPM_DASHBOARD *t = &dpm_main_tab;
	uint8_t *pkg;

	if (!t->refresh_pwr_data)
		return;
	for (uint32_t e = 0; e < DPM_DxS_MAX; e++)
		sky1_pwr_databoard_upload_ap_dsu_power(e, t->power_elem_mW[e], t->power_elem_leakage_mW[e]);
	if (!_s_stream_actual_pwr.enabled)
		return;
	if (++_s_stream_actual_pwr.cnt < _s_stream_actual_pwr.intvl)
		return;
	_s_stream_actual_pwr.cnt = 0;
	pkg = perf_stream_get_pkg();
	put_le32(pkg, 0xa5a7a6a8);			/* signature */
	pkg[4] = 0xb0;					/* length 0x2b0 */
	pkg[5] = 2;
	pkg[7] = (uint8_t)((pkg[7] & 0xf) | 0x60);	/* type 6 */
	put_le32(pkg + 8, (uint32_t)t->ts);
	put_le32(pkg + 12, (uint32_t)(t->ts >> 32));
	for (uint32_t c = 0; c < 12; c++) {
		uint8_t *r = pkg + 16 + 40 * c;

		dpm_stream_fill_common(r, c);
		for (uint32_t k = 0; k < 5; k++)
			put_le32(r + 20 + 4 * k, t->amu[c][k]);
		put_float(r + 4, (float)t->power_elem_leakage_mW[c]);
	}
	dpm_stream_fill_common(pkg + 0x1f0, DPM_ELE_DSU);
	for (uint32_t k = 0; k < 5; k++)
		put_le32(pkg + 0x204 + 4 * k, t->amu[DPM_ELE_DSU][k]);
	dpm_stream_fill_common(pkg + 0x218, DPM_ELE_GPU_TOP);
	{
		fbits gpu = { .u = get_le32(pkg + 0x218) };

		put_float(pkg + 0x218, gpu.f + (float)t->power_elem_mW[DPM_ELE_GPU_CORE]);
	}
	dpm_stream_fill_common(pkg + 0x22c, DPM_ELE_NPU);
	put_float(pkg + 0x22c, (float)t->power_edp_mW[EDP_SOC]);
	put_float(pkg + 0x240, (float)t->power_elem_mW[DPM_ELE_MEMORY]);
	put_float(pkg + 0x250, t->power_edp_leakage_t[8]);
	put_le32(pkg + 0x248, 0x3f400000);		/* 0.75 V */
	{
		float fm = (float)t->delivered_elem_freq_khz[DPM_ELE_MEMORY] / 1000.0f;

		put_float(pkg + 0x24c, fm + fm);
	}
	put_float(pkg + 0x244, (float)t->power_edp_leakage_mW[8]);
	put_le32(pkg + 0x254, t->ddr_accum_cnt);
	memcpy(pkg + 0x258, t->ddr_regs, sizeof(t->ddr_regs));
	dpm_stream_fill_common(pkg + 0x278, DPM_ELE_NPU);
	put_le32(pkg + 0x28c, t->npu_accum_cnt);
	put_float(pkg + 0x2a4, config_board_sensor_get(0));
	put_float(pkg + 0x2a8, config_board_sensor_get(1));
	put_float(pkg + 0x2ac, config_skin_temp_get());
	memcpy(pkg + 0x290, t->npu, sizeof(t->npu));
	perf_stream_put_pkg(pkg);
}
