/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Voltage and power of an OPP from the power maps (CIX's cfg_pwr.c:
 * dpm_dxs_calc_scmi_opp, dpm_dxs_override_Vset, app_pwr_init; u_power.c:
 * u_pwr_initMap, u_pwr_freq2Vot, u_pwr_calcPower; config_dvfs.c:
 * u_pwr_get_measured_power).
 *
 * A map lists a domain's voltage, dynamic coefficient and static power at
 * some frequencies, ascending; between two points they are interpolated
 * linearly.  CIX computes in single precision (its __divsf3 for the
 * divisions); the expressions keep its order of operations so that the
 * results are the same to the bit.
 */
#include "dpm.h"

/* First point at or above freq_khz (binary search, as CIX's) */
static uint32_t u_pwr_search(uint32_t freq_khz, const U_PWR_COEF_MAP *map, uint32_t last)
{
	uint32_t lo = 0, hi = last;

	while (lo < hi) {
		uint32_t mid = (lo + hi) >> 1;

		if (map[mid].freq_khz < freq_khz)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo;
}

/* Dynamic power at each point (mW), for the DPM */
void u_pwr_initMap(U_PWR_COEF_MAP *pwr_coef_map, uint32_t map_size)
{
	for (uint32_t i = 0; i < map_size; i++) {
		U_PWR_COEF_MAP *e = &pwr_coef_map[i];
		float v = (float)e->volt_mV;

		e->dynmc_pwr = e->pwr_coef * (float)e->freq_khz * v * v / 1e9f;
	}
}

/*
 * Voltage at a frequency.  CIX's oddity kept: past the second-to-last point
 * the last point's voltage is returned without interpolating.
 */
uint32_t u_pwr_freq2Vot(uint32_t freq_khz, const U_PWR_COEF_MAP *pwr_coef_map, uint32_t map_size)
{
	const U_PWR_COEF_MAP *p0, *p1;
	uint32_t last, i;

	if (!pwr_coef_map || !map_size)
		return 0;
	last = map_size - 1;
	if (!freq_khz)
		return pwr_coef_map[0].volt_mV;
	i = u_pwr_search(freq_khz, pwr_coef_map, last);
	if (i == 0)
		return pwr_coef_map[0].volt_mV;
	if (i == last)
		return pwr_coef_map[last].volt_mV;
	p0 = &pwr_coef_map[i - 1];
	p1 = &pwr_coef_map[i];
	return (uint32_t)(((float)freq_khz - (float)p0->freq_khz) *
			  ((float)p1->volt_mV - (float)p0->volt_mV) /
			  ((float)p1->freq_khz - (float)p0->freq_khz) + (float)p0->volt_mV);
}

/* Power (mW) at a frequency and utilisation, at least at vot_mV */
float u_pwr_calcPower(uint32_t freq_khz, uint32_t vot_mV, float util,
		      const U_PWR_COEF_MAP *pwr_coef_map, uint32_t map_size)
{
	uint32_t last, i, v;
	float coef, stat, vf;

	if (!pwr_coef_map || !map_size)
		return 0.0f;
	last = map_size - 1;
	if (!freq_khz)
		return pwr_coef_map[0].pwr_static;
	i = u_pwr_search(freq_khz, pwr_coef_map, last);
	if (i == 0 || i == last) {
		v = pwr_coef_map[i].volt_mV;
		stat = pwr_coef_map[i].pwr_static;
		coef = pwr_coef_map[i].pwr_coef;
	} else {
		const U_PWR_COEF_MAP *p0 = &pwr_coef_map[i - 1], *p1 = &pwr_coef_map[i];
		float df = (float)freq_khz - (float)p0->freq_khz;
		float span = (float)p1->freq_khz - (float)p0->freq_khz;

		coef = p0->pwr_coef + df * (p1->pwr_coef - p0->pwr_coef) / span;
		v = (uint32_t)(df * ((float)p1->volt_mV - (float)p0->volt_mV) / span +
			       (float)p0->volt_mV);
		stat = p0->pwr_static + df * (p1->pwr_static - p0->pwr_static) / span;
	}
	vf = (float)(v > vot_mV ? v : vot_mV);
	return stat + coef * util * (float)freq_khz * vf * vf / 1e9f;
}

/*
 * The voltage and power the OPP tables get where they have none: from the
 * domain's map at its OPP-table utilisation.  CIX's returns false always
 * (dvfs_patch_opp_table then keeps a voltage the table has).
 */
bool dpm_dxs_calc_scmi_opp(DPM_DXS_DOMAIN dxs_id, uint32_t freq_khz, uint32_t *vot_mV_ptr,
			   float *pwr_mW_ptr)
{
	if (pwr_mW_ptr)
		*pwr_mW_ptr = u_pwr_calcPower(freq_khz, 0, dpm_freq2pwr_opp_table_coef[dxs_id],
					      dxs_pwrmap_ptr[dxs_id], dxs_pwrmap_size[dxs_id]);
	if (vot_mV_ptr)
		*vot_mV_ptr = u_pwr_freq2Vot(freq_khz, dxs_pwrmap_ptr[dxs_id], dxs_pwrmap_size[dxs_id]);
	return false;
}

/*
 * A voltage for a frequency range of a domain (both ends 0: all): in its
 * map, and the OPPs in the range lose theirs so that dvfs_patch_opp_table
 * takes the map's.
 */
void dpm_dxs_override_Vset(DPM_DXS_DOMAIN dxs_id, uint32_t freq_khz_left,
			   uint32_t freq_khz_right, uint32_t Vset_mV)
{
	if (dxs_id >= DPM_DxS_MAX)
		return;
	for (uint32_t i = 0; i < dxs_pwrmap_size[dxs_id]; i++) {
		U_PWR_COEF_MAP *e = &dxs_pwrmap_ptr[dxs_id][i];

		if (!(freq_khz_left | freq_khz_right) ||
		    (e->freq_khz >= freq_khz_left && e->freq_khz <= freq_khz_right))
			e->volt_mV = Vset_mV;
	}
	clear_config_dvfs_opp_table_voltage(dxs_id, freq_khz_left, freq_khz_right);
}

void app_pwr_init(void)
{
	u_pwr_initMap(cpu_big0_pwr, 21);
	u_pwr_initMap(cpu_big1_pwr, 21);
	u_pwr_initMap(cpu_mid0_pwr, 19);
	u_pwr_initMap(cpu_mid1_pwr, 19);
	u_pwr_initMap(cpu_lit_pwr, 5);
	u_pwr_initMap(dsu_pwr, 3);
	u_pwr_initMap(gpu_pwr, 7);
	u_pwr_initMap(dummy_pwr, 9);
}

/*
 * Measured power of a CPU cluster or the GPU at a frequency (MHz): the
 * measurement, or interpolated between the two around it (the table is
 * highest first; past either end the two nearest are extrapolated).
 */
uint32_t u_pwr_get_measured_power(uint32_t dxs_id, uint32_t freq_MHz)
{
	const pwr_data_t *t = measured_data[dxs_id], *e0, *e1;
	uint32_t i;

	for (i = 0; i < 7; i++) {
		if (t[i].freq_MHz == freq_MHz) {
			if (t[i].pwr_mW)
				return t[i].pwr_mW;
			break;
		}
		if (t[i].freq_MHz < freq_MHz)
			break;
	}
	if (i == 7)
		i = 6;
	else if (i == 0)
		i = 1;
	e1 = &t[i];
	e0 = &t[i - 1];
	return (uint32_t)(((float)freq_MHz - (float)e1->freq_MHz) *
			  ((float)e0->pwr_mW - (float)e1->pwr_mW) /
			  ((float)e0->freq_MHz - (float)e1->freq_MHz) + (float)e1->pwr_mW);
}
