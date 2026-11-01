/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The thermal and power arbiters (CIX's cfg_spt.c) and the utilisation of
 * a DxS domain (cfg_util.c).
 *
 * Four arbiters (socket power fast and slow, skin, die temperature; only the
 * die's is on in CIX's configuration) each choose a throttling phase; per
 * EDP domain the lowest coefficient of the arbiters on (the skin's
 * excepted) times ten picks the OPP the domain's frequency limit comes
 * from.  CIX's debug log every 2000 iterations is left out.
 */
#include "dpm_rt.h"

static float max_skin_margin = 5000.0f;

void spt_set_skin_margin(float margin)
{
	max_skin_margin = margin;
}

float spt_get_skin_margin(void)
{
	return max_skin_margin;
}

void dpm_set_spt_cfg(X_ARB_CFG *spt_cfg)
{
	for (uint32_t i = 0; i < 4; i++)
		spt_ctrls[i].pCfg = spt_cfg;
}

void dpm_spt_iteration(void)
{
	static uint32_t it_cnt;
	DPM_DASHBOARD *t = &dpm_main_tab;

	it_cnt++;
	for (uint32_t i = 0; i < 4; i++) {
		X_ARB_CTRL *c = &t->spt_ctrls[i];

		if (!c->is_enabled)
			continue;
		x_arbit_iteration(c);
		/*
		 * The skin arbiter's phase sets the margin of a set point.  CIX's
		 * oddity kept: it amends the slow socket-power arbiter's PID
		 * (spt_ctrls[1]), not the skin's.
		 */
		if (i == 2)
			pid_setAmend(t->spt_ctrls[1].pid,
				     max_skin_margin * (float)(int32_t)(7 - t->spt_ctrls[2].phase_id) / 7.0f);
	}
}

void dpm_spt_arbitration(void)
{
	for (uint32_t i = 0; i < 4; i++) {
		if (dpm_main_tab.spt_ctrls[i].is_enabled)
			(void)x_arbit_redist(&dpm_main_tab.spt_ctrls[i]);
	}
}

/* The frequency limit of each DxS domain: the OPP picked by its rail's coefficient */
void dpm_update_opp_limit(void)
{
	DPM_DASHBOARD *t = &dpm_main_tab;

	for (uint32_t e = 0; e < DPM_EDP_MAX; e++) {
		float coef = 1.0f;
		uint32_t nth;

		for (uint32_t i = 0; i < 4; i++) {
			float c;

			if (i == 2 || !t->spt_ctrls[i].is_enabled)
				continue;
			c = t->spt_ctrls[i].domain_throttle_coeff[e];
			coef = coef < c ? coef : c;
		}
		nth = (uint32_t)(coef * 10.0f);
		for (uint32_t k = 0; k < dpm_edp_to_dxs_map[e].dxs_num; k++) {
			uint32_t d = (uint8_t)(dpm_edp_to_dxs_map[e].dxs0 + k);

			t->opp_grant[d] = get_config_dvfs_opp_nth_freq(d, nth);
		}
	}
}

/*
 * A domain's utilisation: the mean of its elements' (those above 0); the
 * DSU's from the cores', the medium ones weighing 1.5, the big ones 1.8,
 * scaled to 17.7 (in double precision, as CIX's) plus 0.08, at most 1.
 */
float calc_dxs_util(DPM_DXS_DOMAIN dxs_id)
{
	DPM_DASHBOARD *t = &dpm_main_tab;
	float s = 0.0f;

	if (dxs_id == DPM_DxS_DSU) {
		float u;

		for (uint32_t i = 0; i < 4; i++) {
			if (t->util_elem[i] > 0.0f)	/* CIX: !ULE.S */
				s += t->util_elem[i];
		}
		for (uint32_t i = 4; i < 8; i++) {
			if (t->util_elem[i] > 0.0f)	/* CIX: !ULE.S */
				s += t->util_elem[i] * 1.5f;
		}
		for (uint32_t i = 8; i < 12; i++) {
			if (t->util_elem[i] > 0.0f)	/* CIX: !ULE.S */
				s += t->util_elem[i] * 1.8f;
		}
		u = (float)((double)s / 17.7) + 0.08f;
		return 1.0f < u ? 1.0f : u;
	}
	for (uint32_t k = 0; k < dpm_dxs_to_elem_map[dxs_id].elem_num; k++) {
		uint32_t e = (uint8_t)(dpm_dxs_to_elem_map[dxs_id].elem0 + k);

		if (t->util_elem[e] > 0.0f)	/* CIX: !ULE.S */
			s += t->util_elem[e];
	}
	return s / (float)dpm_dxs_to_elem_map[dxs_id].elem_num;
}
