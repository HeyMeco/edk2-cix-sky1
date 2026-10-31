/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Sky1's performance plugin (CIX's config_sky1soc_perf.c): the five CPU
 * clusters (DVFS elements 2-6, each with its PSU and PLL), then the DSU;
 * the GPU's top and core; the DPM's TDP frequencies per DxS domain.
 */
#include <mod_pmic.h>
#include <mod_sky1soc_perf.h>

#include <fwk_id.h>
#include <fwk_module.h>
#include <fwk_module_idx.h>

#include "clk/clk.h"
#include "scp_dpm.h"

#define AP_DOM(psu, dom, count, mask, clk) {					\
	.psu_id = FWK_ID_ELEMENT_INIT(FWK_MODULE_IDX_PSU, psu),			\
	.domain_id = dom, .ap_core_count = count, .ap_core_bitmask = mask,	\
	.clock_enum_id = clk,							\
}

/* written by override_config_sky1soc_perf */
static struct mod_sky1soc_ap_domain_config ap_dom_cfg[] = {
	AP_DOM(0, 2, 4, 0x00f, CLK_TREE_CPU_PLL_LIT),	/* little, cores 0-3 */
	AP_DOM(1, 3, 2, 0x300, CLK_TREE_CPU_PLL0),	/* big 0, cores 8-9 */
	AP_DOM(2, 4, 2, 0xc00, CLK_TREE_CPU_PLL1),	/* big 1, cores 10-11 */
	AP_DOM(3, 5, 2, 0x030, CLK_TREE_CPU_PLL2),	/* mid 0, cores 4-5 */
	AP_DOM(4, 6, 2, 0x0c0, CLK_TREE_CPU_PLL3),	/* mid 1, cores 6-7 */
	AP_DOM(6, 7, 0, 0x000, CLK_TREE_DSU_PLL),	/* the DSU */
};

/* CIX's override_config_sky1soc_perf: a cluster without cores */
void override_config_sky1soc_perf(uint32_t domaid_id_for_disable)
{
	for (unsigned int i = 0; i < sizeof(ap_dom_cfg) / sizeof(ap_dom_cfg[0]); i++) {
		if (ap_dom_cfg[i].domain_id == domaid_id_for_disable) {
			ap_dom_cfg[i].ap_core_bitmask = 0;
			ap_dom_cfg[i].ap_core_count = 0;
		}
	}
}

const struct fwk_module_config config_sky1soc_perf = {
	.data = &(const struct mod_sky1soc_perf_config){
		.top_perf_domain_idx = 1,
		.core_perf_domain_idx = 0,
		.dsu_perf_domain_idx = 7,
		.tdp_opp_size = 8,
		.dxs2dvfs_size = 8,
		.tdp_opp = dpm_opp_run,
		.dxs2dvfs_tbl = dpm_dxs_to_dvfs_elem_map,
		.ap_dom_cfg = ap_dom_cfg,
		.ap_dom_count = 5,
	},
};
