/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * CIX's power model of Sky1 (Source/projects/sky1/dpm/sky1_pwr_model.c).
 *
 * Dynamic power per DPM element from its rail voltage (V), frequency (GHz;
 * the memory's in MHz at double rate) and activity counters (the AMU's for
 * the cores, the DDR controllers' and the NPU's): cubic polynomials in V
 * and f times a linear one in the counters, with CIX's coefficients
 * (fitted per board for the memory and SoC: the Cloudbook layout, PMIC
 * topology 5, or the EVB's).  Leakage per rail from its voltage and
 * temperature, scaled by the part's leakage current (ibase, from the
 * eFuses: pwr_leakage_init).
 *
 * Where CIX's code computes in double precision (its compiler uses the
 * core's double FPU) so does this (src/dpm/dfloat.c in software, the same
 * IEEE operations); everything else is single precision with CIX's order
 * of operations, so the results are the same to the bit.
 */
#include "dpm_rt.h"

#define CLDBK		5	/* PMIC topology (pmic_type_id) with its own fits */

/* pwr_dynamic_hunter0.v_coeff */
static const float pwr_dynamic_hunter0_v_coeff[3] = { -4.45400001e-05f, 7.16200011e-05f, -3.17900012e-05f };
/* pwr_dynamic_hunter0.f_coeff */
static const float pwr_dynamic_hunter0_f_coeff[3] = { 1.40810871f, -7.74311638f, 12.6203022f };
/* pwr_dynamic_hunter0.m_coeff */
static const float pwr_dynamic_hunter0_m_coeff[6] = { -0.00632483f, 0.00132056f, -0.000265970011f, 0.00240115006f, 0.0449039899f, 1.83613694f };
/* pwr_dynamic_hunter1.v_coeff */
static const float pwr_dynamic_hunter1_v_coeff[3] = { -4.46699996e-05f, 6.89099979e-05f, -3.07100017e-05f };
/* pwr_dynamic_hunter1.f_coeff */
static const float pwr_dynamic_hunter1_f_coeff[3] = { -0.0404804312f, 0.204780906f, -0.302896321f };
/* pwr_dynamic_hunter1.m_coeff */
static const float pwr_dynamic_hunter1_m_coeff[6] = { 0.215403154f, -0.0574830696f, 0.00549336011f, -0.0687563568f, -3.89632988f, 0.441567749f };
/* pwr_dynamic_hayes.f_coeff */
static const float pwr_dynamic_hayes_f_coeff[3] = { 0.00487369997f, -0.0160920005f, 0.0161450002f };
/* pwr_dynamic_hayes.m_coeff */
static const float pwr_dynamic_hayes_m_coeff[5] = { 3.96889982e-06f, 4.96610028e-07f, 9.01159979e-07f, -6.58909994e-06f, 4.90460014f };
/* pwr_dynamic_dsu.m_coeff */
static const float pwr_dynamic_dsu_m_coeff[6] = { -0.00556252012f, -0.00260374998f, -2.62000003e-06f, -0.00447223987f, -0.00135505002f, -4570.13086f };
/* pwr_dynamic_npu.f_coeff */
static const float pwr_dynamic_npu_f_coeff[3] = { -0.0633399263f, 0.160897389f, -0.128885671f };
/* pwr_dynamic_npu.m_coeff */
static const float pwr_dynamic_npu_m_coeff[6] = { -0.000243140006f, -8.39999984e-07f, -2.51500005e-05f, -1.30000004e-07f, -0.000868190022f, -4.99100637f };
/* pwr_dynamic_ddr_cldbk.m_coeff */
static const float pwr_dynamic_ddr_cldbk_m_coeff[5] = { 2.78202415f, 4.19572401f, 3.87234902f, 2.73274684f, 206463.953f };
/* pwr_dynamic_ddr_evb.m_coeff */
static const float pwr_dynamic_ddr_evb_m_coeff[9] = { -2.50600006e-05f, 3.43200009e-05f, 3.66200002e-05f, -9.83999962e-06f, 1.44300002e-05f, 5.59999989e-07f, 3.93199989e-05f, 1.38799996e-05f, 3.09323359f };
/* pwr_dynamic_soc_p1_cldbk.f_coeff */
static const float pwr_dynamic_soc_p1_cldbk_f_coeff[3] = { -0.120174699f, 0.847263157f, -1.16757751f };
/* pwr_dynamic_soc_p1_cldbk.m_coeff */
static const float pwr_dynamic_soc_p1_cldbk_m_coeff[5] = { -0.0466138311f, 1.18180692f, 0.408088952f, -0.53159678f, 9040.74219f };
/* pwr_dynamic_soc_p1_evb.v_coeff */
static const float pwr_dynamic_soc_p1_evb_v_coeff[3] = { -5.54500008f, 1.48485351f, 0.818350196f };
/* pwr_dynamic_soc_p1_evb.f_coeff */
static const float pwr_dynamic_soc_p1_evb_f_coeff[3] = { 0.00824170001f, -0.146507055f, 0.898111045f };
/* pwr_dynamic_soc_p2_evb.v_coeff */
static const float pwr_dynamic_soc_p2_evb_v_coeff[3] = { 0.803607881f, 0.858558297f, 0.850878358f };
/* pwr_dynamic_soc_p2_evb.f_coeff */
static const float pwr_dynamic_soc_p2_evb_f_coeff[3] = { 1.07144022f, -2.46731138f, 1.91564071f };
static const float var_little[6] = { -0.745570004f, -0.054990001f, 0.82198f, -6.91152f, 19.3266296f, -17.9094601f };
static const float const_little[7] = { -388.969238f, 8.13053036f, -18.3751507f, -4.09332991f, 4.79069996f, -3.97837996f, -0.00922000036f };
static const float var_m0[6] = { 2.48604012f, -0.189779997f, 0.662299991f, -5.57998991f, 15.6339102f, -14.5138197f };
static const float const_m0[7] = { 120.203773f, -1.06950998f, -54.6271515f, 2.80211997f, 8.64990044f, -0.499119997f, 0.141489998f };
static const float var_m1[6] = { 1.59934998f, -0.0439799987f, 0.84928f, -7.13173008f, 19.9280891f, -18.4602699f };
static const float const_m1[7] = { 77.2112122f, -0.652620018f, -31.4430809f, 1.62846005f, 5.37952995f, 0.548150003f, 0.0744500011f };
static const float var_b0[6] = { 2.66569996f, -0.424739987f, 0.997659981f, -8.43142033f, 23.6856403f, -22.0356197f };
static const float const_b0[7] = { 76.3793869f, -0.657779992f, -36.7998199f, 1.79673004f, 7.23908997f, -0.448949993f, 0.103019997f };
static const float var_b1[6] = { 3.96862006f, -0.911809981f, 1.04398f, -8.87615013f, 25.0546093f, -23.3986492f };
static const float const_b1[7] = { 192.131958f, -1.41848004f, -94.667717f, 4.00187016f, 14.7380304f, -2.43016005f, 0.211390004f };
static const float var_dsu[6] = { 1.6983f, -0.473179996f, 0.760590017f, -6.47680998f, 18.29035f, -17.0686798f };
static const float const_dsu[7] = { 64.3324738f, -2.42198992f, -48.2691689f, 4.97869015f, 11.6069002f, 0.458339989f, 0.308169991f };
static const float var_soc[6] = { -0.0466299988f, -0.0164599996f, -0.771600008f, 6.41722012f, -17.7680092f, 16.3220901f };
static const float const_soc[7] = { -34.5046005f, 0.0465800017f, 138.843658f, -2.55932999f, -150.63446f, 66.1099014f, -3.90704989f };
static const float var_ddr[6] = { 1.25252998f, -0.185190007f, 0.278180003f, -2.23520994f, 6.04383993f, -5.45281982f };
static const float const_ddr[7] = { -11.75986f, 0.191970006f, 17.6574192f, -0.739790022f, -11.6629f, 6.77292013f, 0.0925299972f };
static const float const_gpu_top[7] = { -33.1769218f, 0.0516199991f, 3.84949994f, -0.0449200012f, -0.119329996f, 0.0607400015f, -0.000169999999f };
static const float const_gpu_stack0[7] = { -3.4161799f, -0.0331300013f, 2.55174994f, -0.0522099994f, -0.701070011f, 1.20912004f, 0.00249999994f };
static const float const_gpu_stack4[7] = { -6.95809984f, -0.0509099998f, 3.09220004f, -0.0271700006f, -0.460940003f, 0.709339976f, -0.00079999998f };
static const float var_leak_gpu[3][7] = {
	{ 19.5813007f, 19.5538597f, 18.4643898f, 0.927110016f, -8.01167011f, 23.2221794f, -22.5293407f },
	{ 3.10307002f, 4.21274996f, 2.14838004f, 0.180189997f, -1.52943003f, 4.37355995f, -4.1996398f },
	{ 4.5971899f, 4.12993002f, 3.24359989f, 0.233919993f, -1.9763f, 5.62879992f, -5.38612986f },
};

/* CIX's ldata: per rail the leakage's voltage/temperature terms, the fit of
 * its scale to the leakage currents, the scale (pwr_leakage_init) */
struct leakage_data {
	const float *var_coeff;
	const float *const_coeff;
	float ibase_data;
};

DPM_DRAM1 static struct leakage_data ldata[12] = {
	{ var_little, const_little, 0.0f },
	{ var_m0, const_m0, 0.0f },
	{ var_m1, const_m1, 0.0f },
	{ var_b0, const_b0, 0.0f },
	{ var_b1, const_b1, 0.0f },
	{ var_dsu, const_dsu, 0.0f },
	{ NULL, NULL, 0.0f },
	{ var_soc, const_soc, 0.0f },
	{ var_ddr, const_ddr, 0.0f },
	{ NULL, const_gpu_top, 0.0f },
	{ NULL, const_gpu_stack0, 0.0f },
	{ NULL, const_gpu_stack4, 0.0f },
};

/* GPU core stacks powered, by level (high nibble: 3-core, low: 2-core) */
static const uint8_t gpu_corestack_cfg[10] = { 0x01, 0x01, 0x10, 0x02, 0x11, 0x20, 0x12, 0x21, 0x22, 0x22 };
static uint8_t gpu_3core_stack = 2;
static uint8_t gpu_2core_stack = 2;

/* x^3 c0 + x^2 c1 + x c2 */
static float pwr_cubic_poly(float val, const float *coeff)
{
	float x2 = val * val;

	return x2 * val * coeff[0] + x2 * coeff[1] + coeff[2] * val;
}

/* the counters weighted, plus a constant */
static float pwr_poly(const uint32_t *m, const float *coeff, uint8_t cnt)
{
	float s = 0.0f;
	uint32_t i;

	for (i = 0; i < cnt; i++)
		s += coeff[i] * (float)m[i];
	return s + coeff[cnt];
}

float pwr_dynamic_hunter0(float v, float f, uint32_t *m)
{
	float pv = pwr_cubic_poly(v, pwr_dynamic_hunter0_v_coeff);
	float pf = pwr_cubic_poly(f, pwr_dynamic_hunter0_f_coeff);

	return pv * pf * pwr_poly(m, pwr_dynamic_hunter0_m_coeff, 5);
}

float pwr_dynamic_hunter1(float v, float f, uint32_t *m)
{
	float pv = pwr_cubic_poly(v, pwr_dynamic_hunter1_v_coeff);
	float pf = pwr_cubic_poly(f, pwr_dynamic_hunter1_f_coeff);

	return pv * pf * pwr_poly(m, pwr_dynamic_hunter1_m_coeff, 5);
}

float pwr_dynamic_hayes(float v, float f, uint32_t *m)
{
	float pf = pwr_cubic_poly(f, pwr_dynamic_hayes_f_coeff);

	(void)v;
	return pf * pwr_poly(m, pwr_dynamic_hayes_m_coeff, 4);
}

float pwr_dynamic_dsu(float v, float f, uint32_t *m)
{
	float g = (float)((double)f * -0x1.0a931bd116de7p-5 + (double)(f * f) * 0x1.419afff619f82p-6 +
			  -0x1.42afe30720db9p-5);

	(void)v;
	return pwr_poly(m, pwr_dynamic_dsu_m_coeff, 5) / 1000.0f * g;
}

float pwr_dynamic_ddr(float v, float f, uint32_t *m)
{
	if (dpm_edp_cfg_get_pmic_topology_id() == CLDBK) {
		float fm = f / 1000.0f;
		float p = pwr_poly(&m[4], pwr_dynamic_ddr_cldbk_m_coeff, 4) / 100000.0f;

		return (fm * 0.117295623f + fm * fm * -0.00382698327f) * p;
	}
	return v * 0.000322279986f * (f * 0.916816294f + f * f * -5.81099994e-05f) *
	       pwr_poly(m, pwr_dynamic_ddr_evb_m_coeff, 8);
}

float pwr_dynamic_npu(float v, float f, uint32_t *m)
{
	float g = (float)((double)pwr_cubic_poly(f, pwr_dynamic_npu_f_coeff) + -0x1.163882fe4ca1ap-2);

	(void)v;
	return pwr_poly(m, pwr_dynamic_npu_m_coeff, 5) * g;
}

/* The SoC's first part, from the DDR controllers' counters */
float pwr_dynamic_soc_p1(float v, float f, uint32_t *m)
{
	if (dpm_edp_cfg_get_pmic_topology_id() == CLDBK) {
		float g = (float)((double)pwr_cubic_poly(f, pwr_dynamic_soc_p1_cldbk_f_coeff) +
				  -0x1.58d08444fc3c0p-5);

		return pwr_poly(&m[4], pwr_dynamic_soc_p1_cldbk_m_coeff, 4) / 10000.0f * g;
	} else {
		float pv = pwr_cubic_poly(v, pwr_dynamic_soc_p1_evb_v_coeff);
		float pf = pwr_cubic_poly(f, pwr_dynamic_soc_p1_evb_f_coeff);
		uint32_t sum = m[0] + m[2] + (m[4] + m[6]);
		double d = (double)sum * -0x1.191006de40929p-3 / 10000.0 + -0x1.70b3debf3d981p-1;

		return pv * pf * (float)d;
	}
}

/* The SoC's second part, from the NPU's counters */
float pwr_dynamic_soc_p2(float v, float f, uint32_t *m)
{
	if (dpm_edp_cfg_get_pmic_topology_id() == CLDBK) {
		double a = (double)m[0] * -0x1.1e5dd76fae1e3p-11 / 1000.0;
		double b = ((double)m[1] * 0x1.a222f4a8873dep-7 + (double)m[2] * 0x1.e62c5a8016fa3p-8) /
			   100000.0;

		return (float)(b + a + 0x1.37bcb02965f07p-5);
	} else {
		float pv = pwr_cubic_poly(v, pwr_dynamic_soc_p2_evb_v_coeff);
		float pf = pwr_cubic_poly(f, pwr_dynamic_soc_p2_evb_f_coeff);
		double a = (double)m[0] * 0x1.3c21be40c2e8dp-1 / 1000.0;
		double b = ((double)m[1] * 0x1.3073756d9559cp-5 + (double)m[2] * 0x1.cde892253111fp-1) /
			   100000.0;

		return pv * pf * (float)(a + b);
	}
}

float pwr_dynamic_null(float v, float f, uint32_t *m)
{
	(void)v;
	(void)f;
	(void)m;
	return 0.0f;
}

/* Temperature (C) to kelvin / 100, in double precision */
static float pwr_norm_temp(float t)
{
	return (float)(((double)t + 273.15) / 100.0);
}

/* A rail's leakage (W) at v (V) and t (C) */
float pwr_leakage(float v, float t, DPM_EDP_DOMAIN edp)
{
	const struct leakage_data *ld;
	float tn, vt;

	if (edp == EDP_GPU || edp > 8)
		__builtin_trap();	/* CIX: vAssertCalledWithTRC(0xf1b10258) */
	ld = &ldata[edp];
	if (!ld->var_coeff)
		return 0.0f;
	tn = pwr_norm_temp(t);
	vt = v * v * ld->var_coeff[0] + ld->var_coeff[1] * v;
	return ld->ibase_data * (vt * (pwr_cubic_poly(tn, &ld->var_coeff[2]) + ld->var_coeff[5]));
}

/* A part of the GPU (top, the 3-core stacks, the 2-core stacks) */
static float pwr_leakage_gpu_partial(float v, float t, uint32_t part)
{
	const float *vl = var_leak_gpu[part];
	float tn = pwr_norm_temp(t);
	float vt = vl[2] + (v * v * vl[0] + vl[1] * v);

	return ldata[9 + part].ibase_data * (vt * (pwr_cubic_poly(tn, &vl[3]) + vl[6]));
}

float pwr_gpu_top_leakage(float v, float t)
{
	return pwr_leakage_gpu_partial(v, t, 0);
}

float pwr_gpu_core_leakage(float v, float t)
{
	float p = 0.0f;

	if (gpu_3core_stack)
		p = pwr_leakage_gpu_partial(v, t, 1) * (float)gpu_3core_stack + 0.0f;
	if (gpu_2core_stack)
		p = p + pwr_leakage_gpu_partial(v, t, 2) * (float)gpu_2core_stack;
	return p;
}

/*
 * The leakage scales from the parts' leakage currents at 25 and 85 C
 * (ibase, eFuses: cfg_fuse.c's ibase_fuse_init) through CIX's fit, in double
 * precision as CIX's; the GPU's rail (6) has none.
 */
void pwr_leakage_init(uint16_t *ibase)
{
	for (uint32_t i = 0; i < 12; i++, ibase += 2) {
		const float *c = ldata[i].const_coeff;
		double x, y, x2, y2, s;

		if (i == 6)
			continue;
		x = (double)ibase[0] / 10000.0;
		x2 = x * x;
		y = (double)ibase[1] / 10000.0;
		y2 = y * y;
		s = x * x2 * (double)c[0] + y * y2 * (double)c[1];
		s = x2 * (double)c[2] + s;
		s = y2 * (double)c[3] + s;
		s = s + (double)(c[4] * (float)ibase[0]) / 10000.0;
		s = (double)(c[5] * (float)ibase[1]) / 10000.0 + s;
		ldata[i].ibase_data = (float)(s + (double)c[6]);
	}
}

/* The GPU's level: which core stacks the GPU's leakage counts */
void pwr_notify_gpu_level(uint32_t gpu_opp_level)
{
	uint32_t idx = 0;

	if (dpm_edp_cfg_get_pmic_topology_id() != CLDBK) {
		uint32_t r = gpu_opp_level % 10;

		idx = r ? r - 1 : 9;
	}
	gpu_3core_stack = gpu_corestack_cfg[idx] >> 4;
	gpu_2core_stack = gpu_corestack_cfg[idx] & 0xf;
}

const dpm_pwr_compute_t pwr_model[DPM_ELE_MAX] = {
	{ pwr_dynamic_hayes, dpm_main_tab.amu[0] },
	{ pwr_dynamic_hayes, dpm_main_tab.amu[1] },
	{ pwr_dynamic_hayes, dpm_main_tab.amu[2] },
	{ pwr_dynamic_hayes, dpm_main_tab.amu[3] },
	{ pwr_dynamic_hunter0, dpm_main_tab.amu[4] },
	{ pwr_dynamic_hunter0, dpm_main_tab.amu[5] },
	{ pwr_dynamic_hunter0, dpm_main_tab.amu[6] },
	{ pwr_dynamic_hunter0, dpm_main_tab.amu[7] },
	{ pwr_dynamic_hunter1, dpm_main_tab.amu[8] },
	{ pwr_dynamic_hunter1, dpm_main_tab.amu[9] },
	{ pwr_dynamic_hunter1, dpm_main_tab.amu[10] },
	{ pwr_dynamic_hunter1, dpm_main_tab.amu[11] },
	{ pwr_dynamic_dsu, dpm_main_tab.amu[12] },
	{ pwr_dynamic_null, NULL },
	{ pwr_dynamic_null, NULL },
	{ pwr_dynamic_null, NULL },
	{ pwr_dynamic_null, NULL },
	{ pwr_dynamic_null, NULL },
	{ pwr_dynamic_npu, dpm_main_tab.npu },
	{ pwr_dynamic_ddr, dpm_main_tab.ddr_regs },
};
