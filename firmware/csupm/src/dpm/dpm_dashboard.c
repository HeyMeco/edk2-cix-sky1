/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The DPM's dashboard (CIX's Source/projects/sky1/dpm/dpm_dashboard.c:
 * dpm_refresh_dashboard, dpm_update_edp_limit, dpm_params_consistency_checker).
 * The rails' voltages (dpm_edp_set_voltage and the rest of the file) are
 * src/board's; CIX's printing (dpm_print_dashboard, dpm_edp_desp_state, the
 * log every 1000 iterations and the utilisation average it keeps for its
 * log level bit 20) and pmic_stress (debug CLI) are left out.
 */
#include "clk/clk.h"
#include "dpm_rt.h"
#include "perf/perf.h"

/* CIX's rails' current limits (DPM_PWR_RAIL_CFG.pwr_cap) for the arbiters */
void dpm_update_edp_limit(void)
{
	for (uint32_t e = 0; e < DPM_EDP_MAX; e++) {
		const struct rail *r = dpm_edp_get_pwr_rail_config((DPM_EDP_DOMAIN)e);

		if (r)
			dpm_main_tab.edp_limit[e] = r->limit;
		else
			__builtin_trap();	/* CIX: vAssertCalledWithTRC(0xf1aa0051) */
	}
}

/*
 * The DPM's inputs, every iteration: frequencies and voltages delivered;
 * every fifth tick (when the AMU copy is new) the activity counters, the
 * utilisation of the cores, the cores' off cycles from the AP, and the power
 * model per element, domain and rail (dynamic and leakage, socket and CPU
 * complex); the die temperature (hottest PVT sensor) and the skin's.
 */
void dpm_refresh_dashboard(void)
{
	DPM_DASHBOARD *t = &dpm_main_tab;
	float tmax;

	for (uint32_t e = 0; e < DPM_ELE_MAX; e++) {
		CLK_TREE_STRUCT *p = clk_nodes_get(dpm_elem_get_clk_id(e));

		t->delivered_elem_freq_khz[e] = clk_tree_get_freq(&p->clk_tree_node) / 1000;
	}
	for (uint32_t d = 0; d < DPM_DxS_MAX; d++) {
		uint32_t m = 0;

		for (uint32_t k = 0; k < dpm_dxs_to_elem_map[d].elem_num; k++) {
			uint32_t f = t->delivered_elem_freq_khz[dpm_dxs_to_elem_map[d].elem0 + k];

			m = f > m ? f : m;
		}
		t->delivered_dxs_freq_khz[d] = m;
	}
	for (uint32_t e = 0; e < DPM_EDP_MAX; e++) {
		uint16_t mv;

		t->delivered_vot_mV[e] = dpm_edp_get_voltage((DPM_EDP_DOMAIN)e, &mv) ? mv : 790;
	}
	t->refresh_pwr_data = sky1_amu_copy_counter(&t->amu[0][0], &t->ts, &t->ts_delta);
	if (t->refresh_pwr_data) {
		int32_t gpu = 0, sum, leak;

		ddr_util_get_reg_accum(t->ddr_regs, &t->ddr_accum_cnt);
		npu_get_reg_accum(t->npu, &t->npu_accum_cnt);
		/* a core's utilisation: its AMU constant-frequency cycles over the period
		 * (the AP's counter at 100 MHz, ten per microsecond of 10 MHz ticks) */
		if (t->ts_delta) {
			float period = (float)t->ts_delta * 10.0f;

			for (uint32_t c = 0; c < 12; c++) {
				float u = (float)t->amu[c][1] / period;

				t->util_elem[c] = 1.0f < u ? 1.0f : u;
			}
		}
		for (uint32_t c = 0; c < 12; c++) {
			uint32_t off = sky1_pwr_databoard_read_ap_off_cycle(c);

			t->off_cycle_delta[c] = off - t->last_off_cycle[c];
			t->last_off_cycle[c] = off;
		}
		for (uint32_t e = 0; e < DPM_ELE_MAX; e++)
			t->power_elem_mW[e] = dpm_dxs_get_opp_max(dpm_ele_to_dxs_map[e]) ?
					      dpm_elem_get_power((DPM_ELEMENTS)e) : 0;
		for (uint32_t d = 0; d < DPM_DxS_MAX; d++) {
			t->util_dxs[d] = calc_dxs_util((DPM_DXS_DOMAIN)d);
			t->power_dxs_mW[d] = dpm_dxs_get_power((DPM_DXS_DOMAIN)d);
		}
		for (uint32_t e = 0; e <= DPM_EDP_MAX; e++) {
			t->power_edp_mW[e] = dpm_edp_get_power((DPM_EDP_DOMAIN)e);
			leak = 0;
			for (uint32_t k = 0; k < dpm_edp_to_dxs_map[e].dxs_num; k++) {
				if (dpm_dxs_get_opp_max((uint8_t)(dpm_edp_to_dxs_map[e].dxs0 + k))) {
					leak = dpm_edp_get_leakage_power((DPM_EDP_DOMAIN)e,
									 &t->power_edp_leakage_t[e]);
					break;
				}
			}
			t->power_edp_leakage_mW[e] = leak;
			if (e >= 5)
				continue;
			/* a CPU rail's leakage shared by its cores, less while they are
			 * off (cycles off of 500000 per period) */
			for (uint32_t k = 0; k < dpm_edp_to_elem_map[e].elem_num; k++) {
				uint32_t c = (uint8_t)(dpm_edp_to_elem_map[e].elem0 + k);
				uint32_t off = t->off_cycle_delta[c];
				uint32_t share = (uint32_t)t->power_edp_leakage_mW[e] /
						 dpm_edp_to_elem_map[e].elem_num;
				int32_t v;

				if (off - 1 <= 499998)
					v = (int32_t)((float)(int32_t)share * (float)(500000 - off) / 500000.0f);
				else if (off >= 500000)
					v = 0;
				else
					v = t->amu[c][1] ? (int32_t)share : 0;
				t->power_elem_leakage_mW[c] = v;
			}
		}
		t->power_elem_leakage_mW[DPM_ELE_MEMORY] = t->power_edp_leakage_mW[8];
		t->power_elem_leakage_mW[DPM_ELE_DSU] = t->power_edp_leakage_mW[EDP_DSU];
		sky1_pwr_databoard_read_gpu_power(&gpu);
		t->power_edp_mW[EDP_GPU] = gpu;
		t->power_elem_mW[DPM_ELE_GPU_TOP] = gpu / 2;
		t->power_elem_mW[DPM_ELE_GPU_CORE] = gpu / 2;
		sum = 0;
		for (uint32_t e = 0; e < 6; e++)
			sum += t->power_edp_mW[e];
		leak = 0;
		for (uint32_t c = 0; c < 12; c++)
			leak += t->power_elem_leakage_mW[c];
		sum += leak;
		t->ccx_power = (float)sum;
		sky1_pwr_databoard_upload_gpu_leakage_power(t->power_edp_leakage_mW[EDP_GPU]);
		sum = sum + gpu + t->power_edp_leakage_mW[EDP_GPU] +
		      (t->power_edp_mW[EDP_SOC] + t->power_edp_leakage_mW[EDP_SOC]) +
		      (t->power_edp_mW[8] + t->power_edp_leakage_mW[8]);
		t->socket_power = (float)sum;
	}
	tmax = 0.0f;
	for (uint32_t i = 0; i < 13; i++) {
		float tc = dev_pvt_get_data(i, PVT_DATA_LATEST);

		tmax = tmax < tc ? tc : tmax;
	}
	t->die_temp = tmax;
	t->skin_temp = config_skin_temp_get();
}

/*
 * The configuration checked once (CIX's dpm_sim_init): clock ranges, power
 * maps in ascending frequency, each domain's minimum power (filled in
 * where the table has none, else not above the domain's power), each rail's
 * within its limit, and the domains' minima adding up to the rails'.
 */
bool dpm_params_consistency_checker(void)
{
	DPM_DASHBOARD *t = &dpm_main_tab;
	uint32_t dxs_sum = 0, edp_sum = 0;

	for (uint32_t d = 0; d < DPM_DxS_MAX; d++) {
		const T_CLK_DOMAIN_CFG *c = dxs_clk_cfg[d];

		if (c->freq_max < c->freq_min || !c->freq_step)
			return false;
	}
	for (uint32_t d = 0; d < DPM_DxS_MAX; d++) {
		const U_PWR_COEF_MAP *m = dxs_pwrmap_ptr[d];

		if (!m || !dxs_pwrmap_size[d])
			continue;
		for (uint32_t i = 1; i < dxs_pwrmap_size[d]; i++) {
			if (m[i].freq_khz < m[i - 1].freq_khz)
				return false;
		}
	}
	for (uint32_t d = 0; d < DPM_DxS_MAX; d++) {
		uint32_t p = dpm_dxs_calc_minimum_power((DPM_DXS_DOMAIN)d, 1.0f);

		if (!t->min_dxs_pwr_mW[d])
			t->min_dxs_pwr_mW[d] = p;
		else if (t->power_dxs_mW[d] < (int32_t)p)
			return false;
		dxs_sum += p;
	}
	for (uint32_t e = 0; e < DPM_EDP_MAX; e++) {
		uint32_t p = dpm_edp_calc_minimum_power((DPM_EDP_DOMAIN)e, 1.0f);

		if (t->edp_limit[e] < p && t->edp_limit[e])
			return false;
		edp_sum += p;
	}
	return dxs_sum == edp_sum;
}
