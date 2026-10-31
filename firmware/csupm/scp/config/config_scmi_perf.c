/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * SCMI performance (CIX's config_scmi_perf.c): the 12 DVFS domains, in
 * order; fast channels for the GPU core and the five CPU clusters, the
 * level-set ones in mailbox 0's registers (0x8c on), the level-get ones in
 * mailbox 1's; the GPU top and the DSU have empty tables.  Two plugins
 * with the full view: locked_perf, then sky1soc_perf.
 */
#include <mod_scmi_perf.h>

#include <fwk_id.h>
#include <fwk_module.h>
#include <fwk_module_idx.h>

#include "scp_dpm.h"

/* the PM's (0xa008xxxx: mailbox 0, 0xa009xxxx: mailbox 1) and the AP's view */
#define FC_SCP(off)	(uint64_t[]){ 0xa0080000 + (off), 0, 0xa0090000 + (off), 0 }
#define FC_AP(off)	(uint64_t[]){ 0x06590000 + (off), 0, 0x065a0000 + (off), 0 }
#define FC(off)		.fast_channels_addr_scp = FC_SCP(off), .fast_channels_addr_ap = FC_AP(off)
#define NO_FC		.fast_channels_addr_scp = (uint64_t[4]){ 0 }, \
			.fast_channels_addr_ap = (uint64_t[4]){ 0 }

static const struct mod_scmi_perf_domain_config domains[] = {
	{ FC(0x8c) },	/* GPU core */
	{ NO_FC },	/* GPU top */
	{ FC(0x94) },	/* little */
	{ FC(0x98) },	/* big 0 */
	{ FC(0x9c) },	/* big 1 */
	{ FC(0xa0) },	/* mid 0 */
	{ FC(0xa4) },	/* mid 1 */
	{ NO_FC },	/* DSU */
	{ 0 },		/* NPU */
	{ 0 },		/* VPU */
	{ 0 },		/* CI-700 */
	{ 0 },		/* MMHUB */
};

static const struct mod_scmi_plugin_config plugins_table[] = {
	{ .id = FWK_ID_MODULE_INIT(FWK_MODULE_IDX_LOCKED_PERF),
	  .dom_type = PERF_PLUGIN_DOM_TYPE_FULL },
	{ .id = FWK_ID_MODULE_INIT(FWK_MODULE_IDX_SKY1SOC_PERF),
	  .dom_type = PERF_PLUGIN_DOM_TYPE_FULL },
};

/* written by override_config_scmi_perf */
static struct mod_scmi_perf_config scmi_perf_config = {
	.domains = &domains,
	.perf_doms_count = FWK_ARRAY_SIZE(domains),
	.fast_channels_alarm_id = FWK_ID_NONE_INIT,
	.fast_channels_rate_limit = 1500,
	.plugins = plugins_table,
	.plugins_count = FWK_ARRAY_SIZE(plugins_table),
	.perf_doms_enabled = UINT32_MAX,
	.tdp_opp_run = dpm_opp_run,
};

/* CIX's override_config_scmi_perf */
void override_config_scmi_perf(uint32_t perf_doms_enabled)
{
	scmi_perf_config.perf_doms_enabled = perf_doms_enabled;
}

const struct fwk_module_config config_scmi_perf = {
	.data = &scmi_perf_config,
};
