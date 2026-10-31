/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Locked performance (CIX's extmodules/plugin_lockedperf): a performance
 * plugin of scmi_perf (full view) that pins a DVFS domain to a level, or
 * to limits, set by the platform (CIX: a message from the AP), whatever the
 * agents ask; and that reports every level change to the OPP trace.
 */
#ifndef MOD_LOCKED_PERF_H
#define MOD_LOCKED_PERF_H

#include <stdbool.h>
#include <stdint.h>

struct mod_locked_perf_config {
	uint32_t domain_count;				/* DVFS domains */
	void (*put_event)(uint8_t domain, uint32_t level);	/* level changes */
};

/* A domain's lock: a level, or limits; zero for none */
struct mod_locked_perf_domain {
	uint32_t level;
	uint32_t min_limit;
	uint32_t max_limit;
};

struct mod_locked_perf_api {
	bool (*locked_perf_enabled)(void);	/* is any domain locked? */
};

enum mod_locked_perf_api_idx {
	MOD_LOCKED_PERF_API_IDX_PLUGIN,		/* perf_plugins_api, for scmi_perf */
	MOD_LOCKED_PERF_API_IDX_LOCKED_PERF,	/* mod_locked_perf_api, for sky1soc_perf */
	MOD_LOCKED_PERF_API_IDX_COUNT,
};

bool mod_locked_perf_set(uint8_t domain_id, const struct mod_locked_perf_domain *domain);
bool mod_locked_perf_get(uint32_t domain_id, struct mod_locked_perf_domain *domain);

#endif
