/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Sky1's performance plugin (CIX's extmodules/plugin_sky1soc): the CPU
 * clusters and the DSU change voltage and frequency together, in an order
 * that keeps every online cluster within 200 mV of the DSU; only clusters
 * with cores online take part.  TDP limits from the DPM cap the levels.
 */
#ifndef MOD_SKY1SOC_PERF_H
#define MOD_SKY1SOC_PERF_H

#include <fwk_id.h>

#include <stdbool.h>
#include <stdint.h>

#include "clk/clk.h"

/* An AP domain (CPU cluster), or the DSU as the last one */
struct mod_sky1soc_ap_domain_config {
	fwk_id_t psu_id;		/* its supply */
	uint8_t domain_id;		/* its DVFS element */
	uint8_t ap_core_count;		/* cores, all online at start */
	uint16_t ap_core_bitmask;	/* which cores */
	CLK_TREE_ENUM_IDX clock_enum_id;	/* its PLL */
};

struct mod_sky1soc_perf_config {
	uint8_t top_perf_domain_idx;	/* GPU top's DVFS element */
	uint8_t core_perf_domain_idx;	/* GPU core's */
	uint8_t dsu_perf_domain_idx;	/* the DSU's */
	uint8_t tdp_opp_size;		/* entries of tdp_opp */
	uint8_t dxs2dvfs_size;		/* entries of dxs2dvfs_tbl */
	const uint32_t *tdp_opp;	/* per DxS domain, the TDP frequency (kHz) */
	const uint8_t *dxs2dvfs_tbl;	/* DxS domain -> DVFS element */
	const struct mod_sky1soc_ap_domain_config *ap_dom_cfg;	/* then the DSU */
	uint8_t ap_dom_count;
};

enum mod_sky1soc_perf_api_idx {
	MOD_SKY1SOC_PERF_API_IDX_PLUGIN,	/* perf_plugins_api, for scmi_perf */
	MOD_SKY1SOC_PERF_API_IDX_COUNT,
};

enum mod_sky1soc_perf_event_idx {
	MOD_SKY1SOC_PERF_EVENT_IDX_DVFS_DONE = 4,	/* from dvfs, ignored */
	MOD_SKY1SOC_PERF_EVENT_IDX_RUN_OPS,		/* run the operations queued */
	MOD_SKY1SOC_PERF_EVENT_IDX_AP_CORE,		/* a core goes on or offline */
	MOD_SKY1SOC_PERF_EVENT_IDX_DSU_DVFS,		/* DSU follows the clusters */
	MOD_SKY1SOC_PERF_EVENT_IDX_COUNT,
};

/* A core goes online or offline (CIX: a message from the AP) */
bool sky1soc_notify_ap_core_change(uint8_t ap_core_idx, bool online);

/* The DSU's frequency follows the clusters' (half the fastest) */
/*
 * Not CIX's: the DVFS elements this module drives (the AP domains and the
 * DSU), whose levels scmi_perf leaves to its passes; and the level it
 * has for one (false: not one of them, or none yet).
 */
bool sky1soc_perf_owns(unsigned int dvfs_idx);
bool sky1soc_perf_get_level(unsigned int dvfs_idx, uint32_t *level);

void sky1soc_set_dsu_dvfs(bool enable);
bool sky1soc_get_dsu_dvfs(void);

/* config_sky1soc_perf.c: take an AP domain out (no cores) */
void override_config_sky1soc_perf(uint32_t domaid_id_for_disable);

#endif
