/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The dvfs module's configuration (CIX's Thirdparty/scpfw/product/sky1/
 * scp_ramfw/config_dvfs.c): the twelve DVFS domains, their OPP tables
 * picked and patched at power-on and finished at init (create_element_table),
 * the boot levels (apply_config_dvfs_sustained_level), and the accessors
 * the performance modules and the DPM use.  scp/config/config_dvfs.c hands
 * dvfs_get_element_table to the framework.
 *
 * CIX's asserts that the tables are (not yet) built are left out, with its
 * debug dumps (dvfs_dump_opp_table).  A domain without OPP table (a BIOS
 * configuration that claims all domains and gives none) is skipped where
 * CIX's firmware reads through a null pointer and asserts.  CIX's clock_id names a sub-element of
 * its clock_lite module (element 0, sub-element the SCMI clock); the open
 * firmware's clock module has one element per SCMI clock (config_clock.c),
 * so the domains name that element.
 */
#include <fwk_module_idx.h>
#include <mod_dvfs.h>
#ifdef BUILD_HAS_MOD_SKY1SOC_PERF
#include <mod_sky1soc_perf.h>
#endif

#include "clk/clk.h"
#include "dpm.h"
#include "plat.h"

/* the psu module's elements, once it is built (scp/config/config_psu.c) */
#ifdef BUILD_HAS_MOD_PSU
#define PSU(n)			FWK_ID_ELEMENT_INIT(FWK_MODULE_IDX_PSU, n)
#else
#define PSU(n)			FWK_ID_NONE_INIT
#endif
/* alarm n of the timer (config_timer.c: 17 alarms) */
#define ALARM(n)		FWK_ID_SUB_ELEMENT_INIT(FWK_MODULE_IDX_TIMER, 0, n)

#define DOMAIN(psu, alarm, lat, sus, table) {				\
	.psu_id = PSU(psu),						\
	.alarm_id = ALARM(alarm),					\
	.retry_ms = 3,							\
	.latency = lat,							\
	.sustained_idx = sus,						\
	.opps = table,							\
}

static struct mod_dvfs_domain_config gpu_core = DOMAIN(5, 6, 1200, 0, gpu_core_opp_perf);
static struct mod_dvfs_domain_config gpu_top = DOMAIN(5, 5, 1000, 0, gpu_top_opp_perf);
static struct mod_dvfs_domain_config little = DOMAIN(0, 0, 1000, 0, NULL);
static struct mod_dvfs_domain_config big_g0 = DOMAIN(1, 1, 1000, 0, NULL);
static struct mod_dvfs_domain_config big_g1 = DOMAIN(2, 2, 1000, 0, NULL);
static struct mod_dvfs_domain_config mid_g0 = DOMAIN(3, 3, 1000, 0, NULL);
static struct mod_dvfs_domain_config mid_g1 = DOMAIN(4, 4, 1000, 0, NULL);
static struct mod_dvfs_domain_config dsu = DOMAIN(6, 7, 1000, 0, NULL);
static struct mod_dvfs_domain_config npu = DOMAIN(7, 8, 1000, 2, npu_opp);
static struct mod_dvfs_domain_config vpu = DOMAIN(7, 9, 1000, 4, vpu_opp);
static struct mod_dvfs_domain_config ci700 = DOMAIN(7, 10, 1000, 1, ci700_opp);
static struct mod_dvfs_domain_config mmhub = DOMAIN(7, 11, 1000, 2, mmhub_opp);

/* in DVFS element order (enum dvfs_element_idx) */
static struct fwk_element element_table[DVFS_ELEMENT_IDX_COUNT + 1] = {
	[DVFS_ELEMENT_IDX_GPU_CORE] = { .name = "gpu_core", .data = &gpu_core },
	[DVFS_ELEMENT_IDX_GPU_TOP] = { .name = "gpu_top", .data = &gpu_top },
	[DVFS_ELEMENT_IDX_LITTLE] = { .name = "lit0_perf", .data = &little },
	[DVFS_ELEMENT_IDX_BIG_G0] = { .name = "big0_perf", .data = &big_g0 },
	[DVFS_ELEMENT_IDX_BIG_G1] = { .name = "big1_perf", .data = &big_g1 },
	[DVFS_ELEMENT_IDX_MID_G0] = { .name = "mid0_perf", .data = &mid_g0 },
	[DVFS_ELEMENT_IDX_MID_G1] = { .name = "mid1_perf", .data = &mid_g1 },
	[DVFS_ELEMENT_IDX_DSU] = { .name = "dsu0_perf", .data = &dsu },
	[DVFS_ELEMENT_IDX_NPU] = { .name = "npu_dfs", .data = &npu },
	[DVFS_ELEMENT_IDX_VPU] = { .name = "vpu_dfs", .data = &vpu },
	[DVFS_ELEMENT_IDX_CI700] = { .name = "ci700_dfs", .data = &ci700 },
	[DVFS_ELEMENT_IDX_MMHUB] = { .name = "mmhub_dfs", .data = &mmhub },
	[DVFS_ELEMENT_IDX_COUNT] = { 0 },
};

static bool table_init_flag;
/* which domains the BIOS configuration gives OPPs (opp_table_config): 0 none,
 * 0x80000000 all, else bit 30 and a bit per DVFS element */
static uint32_t opp_cfg_source;
/* the OPPs the BIOS configuration gives (CIX allocates them) */
static struct mod_dvfs_opp pmconfig_opps[DVFS_ELEMENT_IDX_COUNT][14];

/* The CPU clusters' performance per MHz (DMIPS), little first, for the
 * levels reported to the OS */
static const uint32_t cluster_dmips[5] = { 403, 1024, 1024, 1024, 1024 };

static struct mod_dvfs_domain_config *elem_cfg(uint32_t idx)
{
	return (struct mod_dvfs_domain_config *)element_table[idx].data;
}

const struct fwk_element *dvfs_get_element_table(fwk_id_t module_id)
{
	(void)module_id;
	return element_table;
}

struct mod_dvfs_domain_config *dxs_id_to_dvfs_domain_config(uint32_t dxs_id)
{
	for (uint32_t i = 0; i < DVFS_ELEMENT_IDX_COUNT; i++) {
		if (dvfs_elem_id_map_tab[i].dxs_id == dxs_id)
			return elem_cfg(i);
	}
	return NULL;
}

/* Whether the BIOS configuration gives the domain's OPPs */
static bool dvfs_pmconfig_specified(uint32_t dxs_id)
{
	if (!opp_cfg_source)
		return false;
	if (opp_cfg_source == 0x80000000u)
		return true;
	if (dxs_id >= 12)
		return false;
	return (opp_cfg_source >> dpm_dxs_to_dvfs_elem_map[dxs_id]) & 1;
}

/* The sustained OPP: the last one at or below the level */
void override_config_dvfs_sustained_level(uint32_t dxs_id, uint32_t sustained_level, bool force)
{
	struct mod_dvfs_domain_config *cfg = dxs_id_to_dvfs_domain_config(dxs_id);

	if (!cfg || !cfg->opps || (dvfs_pmconfig_specified(dxs_id) && !force))
		return;
	for (uint32_t i = 0; cfg->opps[i].level - 1 < sustained_level; i++)
		cfg->sustained_idx = i;
}

uint32_t get_config_dvfs_sustained_level(uint32_t dxs_id)
{
	struct mod_dvfs_domain_config *cfg = dxs_id_to_dvfs_domain_config(dxs_id);

	return cfg && cfg->opps ? cfg->opps[cfg->sustained_idx].level : 0;
}

uint16_t get_reference_perf_level(uint32_t dxs_id)
{
	struct mod_dvfs_domain_config *cfg = dxs_id_to_dvfs_domain_config(dxs_id);

	return cfg ? cfg->reference_perf_level : 0;
}

/* Frequency of a level, or with exact_match 0 when the table has no such
 * level; otherwise the frequency of the highest level below it (0 if none) */
static uint32_t __get_dvfs_freq_from_level(const struct mod_dvfs_opp *opp, uint32_t level,
					   bool exact_match)
{
	uint32_t freq = 0, prev = 0;

	for (; opp->level; opp++) {
		if (opp->level == level)
			return opp->frequency;
		if (opp->level >= level)
			break;
		prev = opp->level;
		freq = opp->frequency;
	}
	if (exact_match && (prev != level || !level))
		freq = 0;
	return freq;
}

uint32_t get_dvfs_freq_from_level(uint32_t dxs_id, uint32_t level)
{
	struct mod_dvfs_domain_config *cfg = dxs_id_to_dvfs_domain_config(dxs_id);

	return cfg && cfg->opps ? __get_dvfs_freq_from_level(cfg->opps, level, false) : 0;
}

bool check_opp_level_existing(uint32_t dxs_id, uint32_t level)
{
	struct mod_dvfs_domain_config *cfg = dxs_id_to_dvfs_domain_config(dxs_id);

	return cfg && cfg->opps && __get_dvfs_freq_from_level(cfg->opps, level, true) > 0;
}

/* Drop the OPPs below a level (the table moves down) */
void override_config_dvfs_minimum_level(uint32_t dxs_id, uint32_t minimum_level, bool force)
{
	struct mod_dvfs_domain_config *cfg = dxs_id_to_dvfs_domain_config(dxs_id);
	struct mod_dvfs_opp *opp, *from;

	if (!cfg || !cfg->opps || (dvfs_pmconfig_specified(dxs_id) && !force))
		return;
	opp = cfg->opps;
	for (from = opp; from->level && from->level < minimum_level; from++)
		;
	if (from == opp)
		return;
	do
		*opp++ = *from;
	while (from++->level);
}

/*
 * Cap a table at a level: OPPs above it lose their level (0 ends the table
 * there), and unless the level is in the table the first OPP above the last
 * one below it (or the first OPP) takes it.
 */
void override_config_dvfs_maximum_level(uint32_t dxs_id, uint32_t maximum_level, bool force)
{
	struct mod_dvfs_domain_config *cfg = dxs_id_to_dvfs_domain_config(dxs_id);
	struct mod_dvfs_opp *opp;
	int32_t below = -1;
	uint32_t n = 0;
	bool exact = false;

	if (!cfg || !cfg->opps || (dvfs_pmconfig_specified(dxs_id) && !force))
		return;
	opp = cfg->opps;
	for (; opp[n].level; n++) {
		if (opp[n].level > maximum_level)
			opp[n].level = 0;
		else if (opp[n].level == maximum_level)
			exact = true;
		else
			below = (int32_t)n;
	}
	if (exact)
		return;
	if (below == -1)
		opp[0].level = maximum_level;
	else if ((uint32_t)below + 1 < n)
		opp[below + 1].level = maximum_level;
	else
		opp[below].level = maximum_level;
}

/* OPPs whose frequency is in the range lose their voltage (to be taken
 * from the power map by dvfs_patch_opp_table) */
void clear_config_dvfs_opp_table_voltage(uint32_t dxs_id, uint32_t freq_khz_left,
					 uint32_t freq_khz_right)
{
	struct mod_dvfs_domain_config *cfg = dxs_id_to_dvfs_domain_config(dxs_id);

	if (!cfg || !cfg->opps || dvfs_pmconfig_specified(dxs_id))
		return;
	for (struct mod_dvfs_opp *opp = cfg->opps; opp->level; opp++) {
		uint32_t f = opp->frequency ? opp->frequency : opp->level * 1000;

		if (f >= freq_khz_left && f <= freq_khz_right)
			opp->voltage = 0;
	}
}

/* A domain's OPPs and sustained OPP from the BIOS configuration */
void override_config_dvfs_opp_source(uint8_t dvfs_elem_idx)
{
	struct mod_dvfs_domain_config *cfg;
	const uint8_t *src;
	struct mod_dvfs_opp *opps;
	uint32_t n;

	if (dvfs_elem_idx >= DVFS_ELEMENT_IDX_COUNT || table_init_flag)
		return;
	cfg = elem_cfg(dvfs_elem_idx);
	src = pm_config_get_opp(dvfs_elem_idx);
	n = (uint32_t)src[0] | (uint32_t)src[1] << 8;
	/* CIX allocates n + 1 OPPs; its configuration has room for 13 */
	if (n > 13)
		n = 13;
	opps = pmconfig_opps[dvfs_elem_idx];
	for (uint32_t i = 0; i < n; i++) {
		const uint8_t *e = src + 4 + 16 * i;
		uint32_t w[4];

		for (uint32_t k = 0; k < 4; k++)
			w[k] = (uint32_t)e[4 * k] | (uint32_t)e[4 * k + 1] << 8 |
			       (uint32_t)e[4 * k + 2] << 16 | (uint32_t)e[4 * k + 3] << 24;
		opps[i] = (struct mod_dvfs_opp){
			.level = w[0], .voltage = w[1], .frequency = w[2], .power = w[3],
		};
	}
	opps[n] = (struct mod_dvfs_opp){ 0 };
	cfg->opps = opps;
	cfg->sustained_idx = (uint32_t)src[2] | (uint32_t)src[3] << 8;
	cfg->opp_level_num = (uint16_t)n;
}

void set_config_dvfs_source(uint32_t opp_source)
{
	opp_cfg_source = opp_source;
}

uint32_t get_config_dvfs_source(void)
{
	return opp_cfg_source;
}

bool copy_config_dvfs_opp_levels(uint32_t dxs_id, uint32_t *opp_array, uint32_t *length)
{
	struct mod_dvfs_domain_config *cfg = dxs_id_to_dvfs_domain_config(dxs_id);
	uint32_t n = 0;

	if (!cfg)
		return false;
	for (const struct mod_dvfs_opp *opp = cfg->opps; opp && n < *length && opp->level; opp++)
		opp_array[n++] = opp->level;
	*length = n;
	return true;
}

/*
 * The CPU clusters' and DSU's tables (elements 2-7): one of the MP tables
 * (table_selector 0-3) for production parts, of the test tables (0-8) for
 * samples.  A domain that has a table already (BIOS configuration) keeps it.
 */
bool allocate_config_dvfs_opp_tables(bool is_mp, uint32_t table_selector)
{
	if (is_mp ? table_selector >= 4 : table_selector > 8)
		return false;
	for (uint32_t d = 0; d < 6; d++) {
		struct mod_dvfs_domain_config *cfg = elem_cfg(DVFS_ELEMENT_IDX_LITTLE + d);

		if (!cfg->opps)
			cfg->opps = is_mp ? mp_opp_tables[d][table_selector] :
					    test_opp_tables[d][table_selector];
	}
	return true;
}

const char *get_config_dvfs_opp_table_name(bool is_mp, uint32_t table_selector)
{
	if (is_mp && table_selector < 4)
		return mp_opp_tab_name[table_selector];
	if (table_selector <= 8)
		return test_opp_tab_name[table_selector];
	return "unknown";
}

uint32_t get_config_dvfs_opp_maximum_level(uint32_t dxs_id)
{
	struct mod_dvfs_domain_config *cfg = dxs_id_to_dvfs_domain_config(dxs_id);
	uint32_t level = 0;

	if (!cfg || !cfg->opps)
		return 0;
	for (const struct mod_dvfs_opp *opp = cfg->opps; opp->level; opp++)
		level = opp->level;
	return level;
}

uint32_t get_config_dvfs_opp_nth_freq(uint32_t dxs_id, uint32_t nth)
{
	struct mod_dvfs_domain_config *cfg = dxs_id_to_dvfs_domain_config(dxs_id);

	if (!cfg || !cfg->opps)
		return 0;
	if (nth < cfg->opp_level_num)
		return cfg->opps[nth].frequency;
	/* CIX's: with no OPPs this reads the entry before the table */
	return cfg->opp_level_num ? cfg->opps[cfg->opp_level_num - 1].frequency : 0;
}

uint32_t get_clk_idx_from_dxs_id(DPM_DXS_DOMAIN dxs_id)
{
	for (uint32_t i = 0; i < DVFS_ELEMENT_IDX_COUNT; i++) {
		if (dvfs_elem_id_map_tab[i].dxs_id == dxs_id)
			return dvfs_elem_id_map_tab[i].clk_id;
	}
	return CLK_TREE_MAX_IDX;
}

/*
 * A domain's table made ready (at init): cut at the board's maximum, the
 * fused voltages computed, frequencies from the levels (MHz), voltages and
 * power from the power maps where the table has none, voltages made
 * non-decreasing (all the highest when the strap's DVFS switch is 3).  The
 * sustained OPP is kept in the table; the DSU's is the first at half the
 * fastest CPU cluster (max_ap_freq, from the clusters before it).  Returns
 * the number of OPPs.
 */
static size_t dvfs_patch_opp_table(uint32_t idx, struct mod_dvfs_domain_config *pCfg,
				   uint32_t *max_ap_freq)
{
	struct mod_dvfs_opp *opp = pCfg->opps;
	uint32_t dxs = dvfs_elem_id_map_tab[idx].dxs_id;
	bool measured = !(dxs == DPM_DxS_DSU || dxs > DPM_DxS_GPU_CORE);
	uint32_t opp_max, n = 0, max_voltage = 0;

	if (!opp) {
		pCfg->sustained_idx = (size_t)-1;
		return 0;
	}
	opp_max = dpm_dxs_get_opp_max(dxs);
	for (; opp[n].level; n++) {
		if (opp[n].level > opp_max) {
			opp[n] = (struct mod_dvfs_opp){ 0 };
			break;
		}
	}
	cfg_vf_calc_vmin_voltage((DPM_EDP_DOMAIN)dxs, opp, n);
	for (struct mod_dvfs_opp *p = opp; p->level; p++) {
		uint32_t vot = 0;
		float pwr = 0.0f;
		bool ok;

		if (!p->frequency)
			p->frequency = p->level * 1000;
		ok = dpm_dxs_calc_scmi_opp(dxs, p->frequency, &vot, &pwr);
		if (measured)
			pwr = (float)u_pwr_get_measured_power(dxs, p->level);
		if (!p->voltage || ok)
			p->voltage = vot;
		if (!p->power)
			p->power = (uint32_t)pwr;
		if (p->voltage > max_voltage)
			max_voltage = p->voltage;
	}
	cfg_vf_ensure_voltage_increase(opp, n);
	if (boot_strap_valid && ((boot_strap >> 24) & 0xf) == 3) {
		for (uint32_t i = 0; i < n; i++)
			opp[i].voltage = max_voltage;
	}
	if (!n) {
		pCfg->sustained_idx = (size_t)-1;
		return 0;
	}
	if (pCfg->sustained_idx >= n)
		pCfg->sustained_idx = n - 1;
	if (idx >= DVFS_ELEMENT_IDX_LITTLE && idx <= DVFS_ELEMENT_IDX_MID_G1) {
		if (*max_ap_freq < opp[n - 1].frequency)
			*max_ap_freq = opp[n - 1].frequency;
	} else if (idx == DVFS_ELEMENT_IDX_DSU) {
		uint32_t half = *max_ap_freq >> 1;

		*max_ap_freq = half;
		if (half >= opp[n - 1].frequency) {
			pCfg->sustained_idx = n - 1;
		} else {
			for (uint32_t i = 0; i < n; i++) {
				if (opp[i].frequency >= half) {
					pCfg->sustained_idx = i;
					break;
				}
			}
		}
	}
	return n;
}

/* MHz per level step of each CPU cluster: the fastest cluster's top level
 * spread over 1024 x 8 steps, scaled by the cluster's DMIPS/MHz */
static bool dvfs_calc_perf_granularity(uint32_t *freq_max_array, const uint32_t *dmips_array,
				       float *granularityMhz, uint32_t domain_num, uint32_t scale)
{
	uint32_t m = 1;

	if (!freq_max_array || !granularityMhz)
		return false;
	for (uint32_t i = 0; i < domain_num; i++)
		if (freq_max_array[i] > m)
			m = freq_max_array[i];
	for (uint32_t i = 0; i < domain_num; i++)
		granularityMhz[i] = (float)m / ((float)dmips_array[i] * (float)scale);
	return true;
}

/*
 * A CPU cluster's levels in steps of its granularity (comparable performance
 * across clusters); returns the level of 1000 MHz, the reference.  CIX
 * compares the granularity with 1e-6 in double precision.
 */
static uint16_t dvfs_standardize_opp_level(uint32_t dxs_id, struct mod_dvfs_opp *opp,
					   size_t opp_count, float *granularityMhz)
{
	/* granularityMhz is in element order: little, big0, big1, mid0, mid1 */
	static const uint8_t gidx[5] = { 0, 3, 4, 1, 2 };
	float g = dxs_id < 5 ? granularityMhz[gidx[dxs_id]] : 0.0f;
	float a = g > 0.0f ? g : -g;

	if (a < 1e-6f)
		return 1000;
	for (size_t i = 0; i < opp_count; i++)
		opp[i].level = (uint32_t)((float)opp[i].level / g);
	return (uint16_t)(uint32_t)(1000.0f / g);
}

/*
 * The table, once (CIX's create_element_table): with the BIOS
 * configuration's operating mode 2, the GPU's power-budget tables; each
 * domain's clock; the tables patched; the CPU clusters' levels
 * standardised.
 */
static void create_element_table(void)
{
	uint32_t max_ap_freq = 0;
	uint32_t freq_max[5] = { 0 };
	float granularity[5];

	if (table_init_flag)
		return;
	if (pm_config_get_word(0xd43) == 2) {
		gpu_core.opps = gpu_core_opp_budget;
		gpu_top.opps = gpu_top_opp_budget;
	}
	for (uint32_t i = 0; i < DVFS_ELEMENT_IDX_COUNT; i++) {
		struct mod_dvfs_domain_config *cfg = elem_cfg(i);
		uint32_t scmi = clk_nodes_enum_idx_to_scmi_idx(dvfs_elem_id_map_tab[i].clk_id);

		cfg->clock_id = FWK_ID_ELEMENT(FWK_MODULE_IDX_CLOCK, scmi);
		if (!dvfs_pmconfig_specified(dvfs_elem_id_map_tab[i].dxs_id)) {
			cfg->opp_level_num = (uint16_t)dvfs_patch_opp_table(i, cfg, &max_ap_freq);
			cfg->reference_perf_level = 1000;
		}
	}
	for (uint32_t k = 0; k < 5; k++) {
		struct mod_dvfs_domain_config *cfg = elem_cfg(DVFS_ELEMENT_IDX_LITTLE + k);

		if (cfg->opp_level_num)
			freq_max[k] = cfg->opps[cfg->opp_level_num - 1].level;
	}
	if (dvfs_calc_perf_granularity(freq_max, cluster_dmips, granularity, 5, 8)) {
		for (uint32_t k = 0; k < 5; k++) {
			uint32_t dxs = dvfs_elem_id_map_tab[DVFS_ELEMENT_IDX_LITTLE + k].dxs_id;
			struct mod_dvfs_domain_config *cfg = elem_cfg(DVFS_ELEMENT_IDX_LITTLE + k);

			if (!dvfs_pmconfig_specified(dxs))
				cfg->reference_perf_level =
					dvfs_standardize_opp_level(dxs, cfg->opps, cfg->opp_level_num,
								   granularity);
		}
	}
	table_init_flag = true;
}

/*
 * The boot levels (CIX's ss_init_enter calls this after the clock tree):
 * each domain's clock to its sustained OPP's frequency, then its rail to
 * the OPP's voltage (CIX's order, the frequency first).
 */
bool apply_config_dvfs_sustained_level(void)
{
	create_element_table();
	for (uint32_t i = 0; i < DVFS_ELEMENT_IDX_COUNT; i++) {
		const struct mod_dvfs_domain_config *cfg = elem_cfg(i);
		const struct dvfs_elem_id_map_tab *m = &dvfs_elem_id_map_tab[i];
		const struct mod_dvfs_opp *opp;
		const struct rail *rail;
		CLK_TREE_STRUCT *node;

		if (cfg->sustained_idx == (size_t)-1 || !cfg->opps)
			continue;
		opp = &cfg->opps[cfg->sustained_idx];
		node = clk_nodes_get((CLK_TREE_ENUM_IDX)m->clk_id);
		clk_api_set_freq(&node->clk_tree_node, opp->frequency * 1000);
		rail = dpm_edp_get_pwr_rail_config((DPM_EDP_DOMAIN)m->edp_id);
		if (!rail || rail->type == PMIC_NONE)
			continue;
		dpm_edp_set_voltage((DPM_EDP_DOMAIN)m->edp_id, opp->voltage);
	}
	return true;
}

/*
 * Not CIX's: after a resume, the EDP domains whose rail could not be set
 * again (dpm_edp_restore_voltage): clk_nodes_restore has put their clocks
 * back as they were before the suspend, on a rail that may hold less.
 * Each DVFS element on one goes to its lowest OPP's frequency, recorded
 * so by its owner (sky1soc_perf for the CPU clusters and the DSU, dvfs
 * otherwise); the rail is unsure, so the next change writes it first.
 */
void dvfs_rails_lost(uint32_t edp_mask)
{
	uint32_t owned = 0;

	create_element_table();
	for (uint32_t i = 0; i < DVFS_ELEMENT_IDX_COUNT; i++) {
		const struct mod_dvfs_domain_config *cfg = elem_cfg(i);
		const struct dvfs_elem_id_map_tab *m = &dvfs_elem_id_map_tab[i];
		CLK_TREE_STRUCT *node;

		if (!(edp_mask & (1u << m->edp_id)) || !cfg->opps || cfg->opps[0].frequency == 0)
			continue;
#ifdef BUILD_HAS_MOD_SKY1SOC_PERF
		if (sky1soc_perf_owns(i)) {
			owned |= 1u << i;
			continue;
		}
#endif
		node = clk_nodes_get((CLK_TREE_ENUM_IDX)m->clk_id);
		(void)clk_api_set_freq(&node->clk_tree_node, cfg->opps[0].frequency * 1000);
		dvfs_domain_lost(i, &cfg->opps[0]);
	}
#ifdef BUILD_HAS_MOD_SKY1SOC_PERF
	if (owned)
		sky1soc_perf_rails_lost(owned);
#else
	(void)owned;
#endif
}

/*
 * The GPU core and top frequencies (MHz) for a core level, and the top's
 * level: the top follows the core up to its own maximum; a level that is
 * not a multiple of 10 asks for a core frequency of 10 x level / (level % 10)
 * (CIX's encoding).  False if either level is not in its table.
 */
bool search_for_freq(uint32_t core_level, uint32_t *core_freq, uint32_t *top_level,
		     uint32_t *top_freq)
{
	static uint32_t gpu_core_fmax, gpu_top_fmax;
	uint32_t rem, cf, tf, tl;

	if (!gpu_core_fmax)
		gpu_core_fmax = get_dvfs_freq_from_level(DPM_DxS_GPU_CORE,
				get_config_dvfs_opp_maximum_level(DPM_DxS_GPU_CORE)) / 1000;
	if (!gpu_top_fmax)
		gpu_top_fmax = get_dvfs_freq_from_level(DPM_DxS_GPU_TOP,
				get_config_dvfs_opp_maximum_level(DPM_DxS_GPU_TOP)) / 1000;
	if (core_level - 1 >= gpu_core_fmax)
		return false;
	rem = core_level % 10;
	if (rem) {
		uint32_t x = (core_level - rem) * 10;

		cf = x / rem;
		if (x < rem || gpu_core_fmax < cf)
			return false;
		tf = cf < gpu_top_fmax ? cf : gpu_top_fmax;
		tl = gpu_top_fmax < cf ? gpu_top_fmax : core_level;
	} else {
		cf = core_level;
		tf = gpu_top_fmax < core_level ? gpu_top_fmax : core_level;
		tl = tf;
	}
	if (!check_opp_level_existing(DPM_DxS_GPU_TOP, tl) ||
	    !check_opp_level_existing(DPM_DxS_GPU_CORE, core_level))
		return false;
	*core_freq = cf;
	*top_freq = tf;
	*top_level = tl;
	return true;
}
