/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * What the eFuses say about the part (CIX's cfg_fuse.c): its part number
 * (OPN), the test phase it was characterised in (ATE phase), the minimum
 * voltages measured at a few frequencies per CPU cluster (V/F curves and
 * patches) and the leakage currents (ibase).  From them, the voltages of
 * the OPPs whose table entry says "from the fuses" (cfg_vf_calc_vmin_voltage)
 * and whether the DSU may run at its higher frequency (calc_dsu_fmax).
 *
 * The SE leaves a copy of the eFuses at 0x200be700 (pm.ld); while the copy
 * starts with its marker words the fuses were not distributed and nothing
 * is read (as in the emulator).  CIX's firmware reads the V/F data only
 * when the BIOS configuration enables it (vmin_config): vf_curve_loaded
 * starts at 3, "disabled".
 *
 * The printing functions (opn_fuse_printing, cfg_vf_fuse_dump,
 * ibase_fuse_dump) are left out.  pvt_trim_fuse_init is src/pvt.c's.
 * ibase_fuse_init reads the leakage currents; CIX hands them to its power
 * model (pwr_leakage_init, double precision), which the DPM uses and which
 * is not ported yet.
 */
#include "dpm.h"

#define EFUSE_COPY		0x200be700u
#define EFUSE_UNSET		0xa8763271u
#define VMIN_MAX		980u	/* mV */
#define VMIN_DEFAULT		950u	/* mV, a curve point without data */

OPN_FUSE_T fuse_opn;
uint8_t g_vf_fuse_curve_fit_method;
static EM_ATE_PHACE ate_phace;
static uint8_t dsu_fmax;		/* the DSU may run at 1300 MHz */
uint8_t opn_check_flag;
uint8_t vf_patch_loaded;
uint8_t vf_curve_loaded = 3;
static struct {
	uint16_t freq, vmin;
} dsu_vf_curve;
static bool ibase_loaded;
uint16_t fuse_ibase[12][2];

/* core_vf_fuse (dpm_data.c) is CIX's core_vf_patch[2] then core_vf_curve[4] */
#define core_vf_patch		(&core_vf_fuse[0])
#define core_vf_curve		(&core_vf_fuse[2])

/* CIX's u_misc_extract_uint (u_misc.c): bit_width bits at bit_offset, least
 * significant first */
uint32_t u_misc_extract_uint(const uint8_t *buf, uint32_t bit_offset, uint8_t bit_width)
{
	uint32_t v = 0;

	for (uint32_t i = 0; i < bit_width; i++)
		v |= (uint32_t)((buf[(bit_offset + i) / 8] >> ((bit_offset + i) % 8)) & 1) << i;
	return v;
}

/* CIX's get_fuse_base: the eFuse copy, NULL while it holds the marker */
static const uint8_t *get_fuse_base(void)
{
	for (unsigned int i = 0; i < 4; i++) {
		if (REG32(EFUSE_COPY + 4 * i) == EFUSE_UNSET)
			return NULL;
	}
	return (const uint8_t *)EFUSE_COPY;
}

EM_ATE_PHACE cfg_vf_get_ate_phase(void)
{
	return ate_phace;
}

uint32_t cfg_vf_get_dsu_fmax(void)
{
	return dsu_fmax ? 1300 : 1200;
}

bool cfg_fuse_opn_valid(void)
{
	return opn_check_flag == 3;
}

bool opn_fuse_get(uint32_t *opn_high, uint32_t *opn_low)
{
	const uint8_t *base = get_fuse_base();

	if (!base)
		return false;
	*opn_low = u_misc_extract_uint(base, 173 * 32, 32);
	*opn_high = u_misc_extract_uint(base, 174 * 32, 32);
	return true;
}

bool load_and_check_opn_valid(void)
{
	uint32_t opn_h32 = 0, opn_l32 = 0;
	bool valid = opn_fuse_get(&opn_h32, &opn_l32) && (opn_h32 | opn_l32);

	if (valid) {
		fuse_opn.opn_l32 = opn_l32;
		fuse_opn.opn_h32 = opn_h32;
		opn_check_flag = 3;
	} else {
		fuse_opn.opn_fuse64 = 0;
		opn_check_flag = 1;
	}
	return valid;
}

/*
 * The ATE phase from which points are fused: no DSU curve, ES or DVT;
 * then by the GB1 curve's frequencies and the GB0 patch.  When none of the
 * rules applies the phase stays as it was (CIX's).
 */
static void detect_chip_phase(const uint8_t *fuse_data_base)
{
	uint32_t b1_top2_freq = core_vf_curve[3].raw[2].freq;
	uint32_t b1_top0_freq = core_vf_curve[3].raw[0].freq;
	uint32_t b0_patch_freq = core_vf_patch[0].raw[0].freq;
	EM_ATE_PHACE mp2 = b1_top0_freq == 2200 ? ATE_PHACE_MP3 : ATE_PHACE_MP2;

	if (!fuse_data_base)
		ate_phace = ATE_PHACE_ES;
	if (!dsu_vf_curve.freq || !dsu_vf_curve.vmin) {
		ate_phace = b1_top2_freq ? ATE_PHACE_DVT : ATE_PHACE_ES;
		return;
	}
	if (b0_patch_freq) {
		ate_phace = b1_top2_freq ? mp2 : ATE_PHACE_MP3;
		return;
	}
	if (b1_top0_freq == 2200)
		ate_phace = mp2;
	else if (b1_top2_freq == 2300)
		ate_phace = ATE_PHACE_PVT;
	else if (b1_top2_freq == 2200)
		ate_phace = ATE_PHACE_MP;
}

/* Whether the DSU may run at 1300 MHz: by part number, or by the DSU's and
 * the clusters' minimum voltages at their top frequency */
static void calc_dsu_fmax(void)
{
	uint32_t dsu_vmin = dsu_vf_curve.vmin;
	uint32_t vmin_m0 = core_vf_curve[0].raw[0].vmin;
	uint32_t vmin_b1 = core_vf_curve[3].raw[0].vmin;
	uint32_t vmin_b0, vmin_m1;

	if (vf_curve_loaded != 1)
		return;
	if (cfg_fuse_opn_valid()) {
		if ((fuse_opn.opn_h32 & 0x01f00000) == 0x00800000)
			dsu_fmax = 1;
		return;
	}
	if (!dsu_vmin)
		return;
	if (ate_phace >= ATE_PHACE_MP2) {
		/* the GB0 and GM1 points are the patches */
		vmin_b0 = core_vf_patch[0].raw[0].vmin;
		vmin_m1 = core_vf_patch[1].raw[0].vmin;
		if (dsu_vmin <= 769)
			dsu_fmax = 1;
		else if (dsu_vmin >= 782 || vmin_b1 > 900)
			dsu_fmax = 0;
		else
			dsu_fmax = vmin_m0 + vmin_b1 + vmin_b0 + vmin_m1 < 3530;
		return;
	}
	vmin_b0 = core_vf_curve[2].raw[0].vmin;
	vmin_m1 = core_vf_curve[1].raw[0].vmin;
	if (dsu_vmin >= 780)
		dsu_fmax = 0;
	else if (dsu_vmin < 770)
		dsu_fmax = 1;
	else if (vmin_b0 > 900 || vmin_b1 >= 901)
		dsu_fmax = 0;
	else
		dsu_fmax = vmin_m0 + vmin_b1 + vmin_b0 + vmin_m1 <=
			   (ate_phace != ATE_PHACE_MP ? 3460u : 3480u);
}

/* Three points (MHz from 5 bits x 100, mV from 10 bits + 400) at a bit offset,
 * stride bits apart */
static void vf_fuse_read(const uint8_t *base, VF_FUSE_DATA *v, uint32_t stride)
{
	for (uint32_t p = 0; p < 3; p++) {
		uint32_t at = v->bit_offset + p * stride;

		v->raw[p].freq = (uint16_t)(u_misc_extract_uint(base, at, 5) * 100);
		v->raw[p].vmin = (uint16_t)(u_misc_extract_uint(base, at + 5, 10) + 400);
	}
}

void cfg_vf_fuse_init(void)
{
	const uint8_t *base;

	if (vf_curve_loaded == 3)
		return;
	base = get_fuse_base();
	if (!base)
		return;
	/* the four curves' points are interleaved, 60 bits apart */
	for (uint32_t c = 0; c < 4; c++) {
		if (!(u_misc_extract_uint(base, core_vf_curve[c].bit_offset, 5) & 0xffff))
			continue;
		vf_fuse_read(base, &core_vf_curve[c], 60);
		core_vf_curve[c].valid = true;
	}
	for (uint32_t c = 0; c < 2; c++) {
		vf_fuse_read(base, &core_vf_patch[c], 15);
		core_vf_patch[c].valid = core_vf_patch[c].raw[0].freq != 0;
	}
	vf_curve_loaded = core_vf_curve[0].valid || core_vf_curve[1].valid ||
			  core_vf_curve[2].valid ? 1 : core_vf_curve[3].valid;
	vf_patch_loaded = core_vf_patch[1].valid || core_vf_patch[0].valid;
	dsu_vf_curve.freq = (uint16_t)(u_misc_extract_uint(base, 0x727, 5) * 100);
	dsu_vf_curve.vmin = (uint16_t)(u_misc_extract_uint(base, 0x72c, 10) + 400);
	if (!dsu_vf_curve.freq || dsu_vf_curve.vmin <= 400)
		dsu_vf_curve.freq = dsu_vf_curve.vmin = 0;
	detect_chip_phase(base);
	calc_dsu_fmax();
}

bool cfg_vf_fuse_loaded(void)
{
	return vf_curve_loaded == 1;
}

bool cfg_vf_fuse_has_vmin_data_exists(void)
{
	const uint8_t *base;

	if (vf_curve_loaded == 1)
		return true;
	base = get_fuse_base();
	if (!base)
		return false;
	for (uint32_t c = 0; c < 4; c++) {
		if (u_misc_extract_uint(base, core_vf_curve[c].bit_offset, 5) & 0xffff)
			return true;
	}
	return false;
}

/* Minimum voltage at freq_MHz along a curve: the lowest-frequency point
 * still at or above it (the points descend) */
static uint32_t vf_curve_vmin(const VF_FUSE_DATA *v, uint32_t freq_MHz)
{
	uint32_t vmin = v->raw[0].vmin ? v->raw[0].vmin : VMIN_DEFAULT;

	for (uint32_t p = 1; p < 3; p++) {
		if (!v->raw[p].vmin || v->raw[p].freq < freq_MHz)
			break;
		vmin = v->raw[p].vmin;
	}
	return vmin;
}

/*
 * A cluster's minimum voltage at a frequency from its curve, or for GM1 and
 * GB0 from their patch when use_2nd_vmin.  CIX's oddities kept: the little
 * cluster (domain 0) reads the GM1 patch as its curve, and a patch without a
 * curve gives 0.
 */
static uint32_t find_raw_data(DPM_EDP_DOMAIN domain, uint32_t freq_MHz, bool use_2nd_vmin,
			      bool *is_2nd_vmin)
{
	const VF_FUSE_DATA *curve = &core_vf_fuse[domain + 1];	/* core_vf_curve[domain - 1] */
	uint32_t raw_vmin = curve->valid ? vf_curve_vmin(curve, freq_MHz) : 0;
	uint32_t merged_result = raw_vmin ? raw_vmin : VMIN_DEFAULT;
	bool is_patch_vmin = false;

	if ((domain & ~1u) == 2 && core_vf_patch[domain == EDP_CPU_GM1].valid) {
		uint32_t patch_vmin = vf_curve_vmin(&core_vf_patch[domain == EDP_CPU_GM1], freq_MHz);

		if (raw_vmin && (patch_vmin & 0xffff)) {
			merged_result = use_2nd_vmin ? patch_vmin : raw_vmin;
			is_patch_vmin = use_2nd_vmin;
		} else if (!raw_vmin && (patch_vmin & 0xffff)) {
			merged_result = 0;
			is_patch_vmin = true;
		}
	}
	if (is_2nd_vmin)
		*is_2nd_vmin = is_patch_vmin;
	return merged_result;
}

bool is_lower_opp_run_on_770mV(void)
{
	if (ate_phace < ATE_PHACE_MP2 || vf_curve_loaded != 1)
		return false;
	return find_raw_data(EDP_CPU_GB1, 2600, true, NULL) +
	       find_raw_data(EDP_CPU_GM0, 2300, true, NULL) < 1736;
}

/* modifier x (raw + offset) mV, rounded up to 5 mV */
static uint32_t vf_target(uint32_t raw, int32_t offset, float modifier)
{
	float target_f32 = modifier * (float)(raw + (uint32_t)offset);
	uint32_t target_u32 = (uint32_t)target_f32;
	float target_frac = target_f32 - (float)target_u32;
	uint32_t encode = (uint32_t)(target_f32 / 5.0f);

	return encode * 5 + (target_frac > 0.01f || target_u32 % 5 ? 5 : 0);
}

/* The test frequency (MHz) of each cluster's first and second fused points */
static const uint32_t ate_freq_checkpoint[2][4] = {
	{ 2300, 2200, 2500, 2600 },	/* GM0, GM1, GB0, GB1 */
	{ 2200, 2200, 2400, 2500 },
};

/* CIX's vf_get_vmin: parts characterised before MP2 */
static uint32_t vf_get_vmin(DPM_EDP_DOMAIN d, uint32_t idx)
{
	uint32_t check_freq = 0, raw, target;
	uint32_t offset = 10;
	float modifier = 1.01f;
	bool is_patched_vmin;

	if (idx == 1) {
		check_freq = ate_freq_checkpoint[0][d - 1];
		modifier = dsu_fmax ? 1.01f : 1.02f;
	} else if (idx == 2) {
		uint32_t raw_b1_25 = find_raw_data(EDP_CPU_GB1, 2500, false, NULL);
		uint32_t raw_b0_24 = find_raw_data(EDP_CPU_GB0, 2400, false, NULL);
		uint32_t vmin_boundary = ate_phace != ATE_PHACE_MP ? 1710 : 1720;

		check_freq = ate_freq_checkpoint[1][d - 1];
		modifier = raw_b0_24 + raw_b1_25 > vmin_boundary ? 1.02f : 1.01f;
	} else if (idx == 3 && (d == EDP_CPU_GB0 || d == EDP_CPU_GB1)) {
		raw = find_raw_data(d, 2200, false, &is_patched_vmin);
		if (raw < 760) {
			raw = 760;
			offset = 20;
			modifier = 1.03f;
		} else if (raw < 810) {
			offset = 20;
			modifier = 1.03f;
		}
		goto calc;
	}
	raw = find_raw_data(d, check_freq, false, &is_patched_vmin);
calc:
	target = vf_target(raw, (int32_t)offset, modifier);
	if (d == EDP_CPU_GB0 && idx == 1)
		target += 30;
	else if (d == EDP_CPU_GB0 && idx == 2)
		target += 50;
	return target < VMIN_MAX ? target : VMIN_MAX;
}

/* CIX's vf_get_vmin2: parts characterised in MP2 and later, the patches used */
static uint32_t vf_get_vmin2(DPM_EDP_DOMAIN d, uint32_t idx)
{
	uint32_t check_freq = 0, raw, target;
	int32_t offset = 10;
	float modifier = 1.01f;
	bool is_patched_vmin;

	if (ate_phace == ATE_PHACE_MP3 && (d == EDP_CPU_GB0 || d == EDP_CPU_GB1) &&
	    (idx == 2 || idx == 3)) {
		modifier = 1.0f;
		if (d == EDP_CPU_GB0) {
			raw = find_raw_data(d, 2500, true, &is_patched_vmin);
			raw = raw > 900 ? raw : 900;
			offset = idx == 3 ? -70 : 0;
		} else {
			raw = find_raw_data(d, 2600, true, &is_patched_vmin);
			if (idx == 3) {
				raw = raw > 890 ? raw : 890;
				offset = -65;
			} else {
				raw = raw > 870 ? raw : 870;
				offset = 5;
			}
		}
		goto calc;
	}
	if (idx == 1 || (idx == 2 && d == EDP_CPU_GM0)) {
		if (d == EDP_CPU_GM1) {
			raw = find_raw_data(d, 2200, true, &is_patched_vmin);
			offset = (int32_t)((raw > 850 ? raw : 850) - raw);
			if (!is_lower_opp_run_on_770mV())
				offset += 10;
			modifier = 1.0f;
			goto calc;
		}
		if (d == EDP_CPU_GM0 || d == EDP_CPU_GB1)
			modifier = dsu_fmax ? 1.01f : 1.02f;
		if (d == EDP_CPU_GB0)
			modifier = 1.0f;
		check_freq = ate_freq_checkpoint[0][d - 1];
		offset = d == EDP_CPU_GB0 ? 15 : 10;
	} else if (idx == 2) {
		if (d == EDP_CPU_GB1) {
			check_freq = 2500;
			modifier = 1.02f;
		} else if (d == EDP_CPU_GB0) {
			check_freq = 2400;
			offset = 20;
			modifier = 1.0f;
		}
	} else if (idx == 3) {
		if (d == EDP_CPU_GB1) {
			raw = find_raw_data(d, 2200, true, &is_patched_vmin);
			offset = raw < 810 ? 20 : 10;
			modifier = raw < 810 ? 1.03f : 1.01f;
			raw = raw > 760 ? raw : 760;
			goto calc;
		}
		offset = d == EDP_CPU_GB0 ? 15 : 10;
		check_freq = d == EDP_CPU_GM0 ? 2300 : d == EDP_CPU_GB0 ? 2200 : 0;
		modifier = d == EDP_CPU_GB0 ? 1.0f : 1.01f;
	}
	raw = find_raw_data(d, check_freq, true, &is_patched_vmin);
calc:
	target = vf_target(raw, offset, modifier);
	return target < VMIN_MAX ? target : VMIN_MAX;
}

/*
 * The voltages of a CPU cluster's OPPs that its table takes from the fuses:
 * an entry's voltage that is not a multiple of 5 is a base (the multiple of
 * 5 below it, the least it may get) plus the index of the fused point to
 * use.  Only the GM and GB clusters have fused points.
 */
bool cfg_vf_calc_vmin_voltage(DPM_EDP_DOMAIN domain, struct mod_dvfs_opp *opp, uint32_t size)
{
	if (domain < EDP_CPU_GM0 || domain > EDP_CPU_GB1)
		return false;
	for (uint32_t i = 0; i < size; i++) {
		uint32_t vmin_idx = opp[i].voltage % 5;
		uint32_t vmin_base = opp[i].voltage - vmin_idx;
		uint32_t calc_vmin;

		if (!vmin_idx)
			continue;
		if (ate_phace >= ATE_PHACE_MP2)
			calc_vmin = vf_get_vmin2(domain, vmin_idx);
		else
			calc_vmin = vf_get_vmin(domain, vmin_idx);
		opp[i].voltage = calc_vmin > vmin_base ? calc_vmin : vmin_base;
	}
	return true;
}

/* No OPP below the one before it */
void cfg_vf_ensure_voltage_increase(struct mod_dvfs_opp *opp, uint32_t size)
{
	uint32_t last_voltage = opp[0].voltage;

	for (uint32_t i = 1; i < size; i++) {
		if (opp[i].voltage < last_voltage)
			opp[i].voltage = last_voltage;
		last_voltage = opp[i].voltage;
	}
}

/* The fused points are not to be used (after they were read) */
void cfg_vf_fuse_clear(void)
{
	if (vf_curve_loaded != 1)
		return;
	vf_curve_loaded = 2;
	for (uint32_t c = 0; c < 4; c++)
		core_vf_curve[c].valid = false;
	if (vf_patch_loaded == 1) {
		vf_patch_loaded = 2;
		core_vf_patch[0].valid = false;
		core_vf_patch[1].valid = false;
	}
}

void cfg_vf_fuse_enable(void)
{
	vf_curve_loaded = 0;
}

void cfg_vf_fuse_set_profile(uint32_t profile)
{
	uint32_t p = profile & 0x0fff0000;

	if ((p | 0x10000) == 0x10000)
		g_vf_fuse_curve_fit_method = (uint8_t)(p >> 16);
}

/*
 * Leakage currents at 25 and 85 C per domain (CIX's ibase_fuse_init): the
 * defaults, then the fused values of the eight clusters' and the GPU's
 * parts (a GPU part without fuses takes the DSU's, entry 6).
 */
void ibase_fuse_init(void)
{
	const uint8_t *base;

	for (uint32_t i = 0; i < 12; i++) {
		fuse_ibase[i][0] = 700;
		fuse_ibase[i][1] = 5000;
	}
	fuse_ibase[0][0] = 180;
	fuse_ibase[0][1] = 1420;
	fuse_ibase[7][0] = 1800;
	fuse_ibase[7][1] = 14200;
	base = get_fuse_base();
	if (!base || !u_misc_extract_uint(base, bit_offsets[1], 16))
		return;
	ibase_loaded = true;
	for (uint32_t i = 0; i < 8; i++) {
		fuse_ibase[i + 1][0] = (uint16_t)u_misc_extract_uint(base, bit_offsets[i + 1], 16);
		fuse_ibase[i + 1][1] = (uint16_t)(u_misc_extract_uint(base, bit_offsets[i + 1] + 128, 16) * 10);
	}
	for (uint32_t g = 0; g < 3; g++) {
		uint32_t i25c = u_misc_extract_uint(base, gpu_bit_offsets[g], 16);
		uint32_t i85c = u_misc_extract_uint(base, gpu_bit_offsets[g] + 80, 16);

		if (i25c | i85c) {
			fuse_ibase[9 + g][0] = (uint16_t)i25c;
			fuse_ibase[9 + g][1] = (uint16_t)i85c;
		} else if (fuse_ibase[6][0] | fuse_ibase[6][1]) {
			fuse_ibase[9 + g][0] = fuse_ibase[6][0];
			fuse_ibase[9 + g][1] = fuse_ibase[6][1];
		}
	}
}
