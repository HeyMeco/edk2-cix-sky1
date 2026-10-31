/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * CIX's mod_locked_perf.c (extmodules/plugin_lockedperf): a performance
 * plugin of scmi_perf with the full view of the DVFS domains.  On each
 * fast-channel pass it overrides a locked domain's limits (both set) or
 * level (set: the limits become the level); on each level change scmi_perf
 * reports, it records the level for the AP's OPP trace (put_event).
 * CIX's logging is left out.
 */
#include <mod_locked_perf.h>
#include <mod_scmi_perf.h>

#include <fwk_id.h>
#include <fwk_mm.h>
#include <fwk_module.h>
#include <fwk_module_idx.h>
#include <fwk_status.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static struct mod_locked_perf_ctx {
	const struct mod_locked_perf_config *config;
	struct mod_locked_perf_domain *domain;
} locked_perf_ctx;

/* CIX's mod_locked_perf_set */
bool mod_locked_perf_set(uint8_t domain_id, const struct mod_locked_perf_domain *domain)
{
	if (domain_id >= locked_perf_ctx.config->domain_count)
		return false;
	locked_perf_ctx.domain[domain_id] = *domain;
	return true;
}

/* CIX's mod_locked_perf_get */
bool mod_locked_perf_get(uint32_t domain_id, struct mod_locked_perf_domain *domain)
{
	if (domain_id >= locked_perf_ctx.config->domain_count)
		return false;
	*domain = locked_perf_ctx.domain[domain_id];
	return true;
}

/* CIX's locked_perf_update: the locks into the snapshot */
static int locked_perf_update(struct perf_plugins_perf_update *data)
{
	for (uint32_t i = 0; i < locked_perf_ctx.config->domain_count; i++) {
		struct mod_locked_perf_domain lock = locked_perf_ctx.domain[i];

		if (lock.min_limit != 0 && lock.max_limit != 0) {
			data->adj_min_limit[i] = lock.min_limit;
			data->adj_max_limit[i] = lock.max_limit;
		}
		if (lock.level != 0) {
			data->level[i] = lock.level;
			data->adj_min_limit[i] = lock.level;
			data->adj_max_limit[i] = lock.level;
		}
	}
	return FWK_SUCCESS;
}

/* CIX's locked_perf_report: a level change to the OPP trace */
static int locked_perf_report(struct perf_plugins_perf_report *data)
{
	if (data->level != 0)
		locked_perf_ctx.config->put_event((uint8_t)fwk_id_get_element_idx(data->dep_dom_id),
						  data->level);
	return FWK_SUCCESS;
}

/* CIX's locked_perf_is_enabled: is any domain locked? */
static bool locked_perf_is_enabled(void)
{
	for (uint32_t i = 0; i < locked_perf_ctx.config->domain_count; i++) {
		const struct mod_locked_perf_domain *lock = &locked_perf_ctx.domain[i];

		if (lock->level != 0 || (lock->min_limit != 0 && lock->max_limit != 0))
			return true;
	}
	return false;
}

static const struct perf_plugins_api perf_plugins_api = {
	.update = locked_perf_update,
	.report = locked_perf_report,
};

static const struct mod_locked_perf_api locked_perf_api = {
	.locked_perf_enabled = locked_perf_is_enabled,
};

/* CIX's locked_perf_init */
static int locked_perf_init(fwk_id_t module_id, unsigned int element_count, const void *data)
{
	const struct mod_locked_perf_config *config = data;

	(void)module_id;
	if (element_count != 0)
		return FWK_E_PARAM;
	locked_perf_ctx.config = config;
	locked_perf_ctx.domain = fwk_mm_calloc(config->domain_count, sizeof(locked_perf_ctx.domain[0]));
	if (locked_perf_ctx.domain == NULL)
		return FWK_E_NOMEM;
	memset(locked_perf_ctx.domain, 0, config->domain_count * sizeof(locked_perf_ctx.domain[0]));
	return FWK_SUCCESS;
}

/* CIX's locked_perf_start */
static int locked_perf_start(fwk_id_t id)
{
	(void)id;
	return FWK_SUCCESS;
}

/* CIX's locked_perf_process_bind_request: the API by who asks */
static int locked_perf_process_bind_request(fwk_id_t source_id, fwk_id_t target_id,
					    fwk_id_t api_id, const void **api)
{
	(void)target_id;
	(void)api_id;
	if (fwk_id_is_equal(source_id, fwk_module_id_scmi_perf))
		*api = &perf_plugins_api;
	else if (fwk_id_is_equal(source_id, fwk_module_id_sky1soc_perf))
		*api = &locked_perf_api;
	else
		return FWK_E_ACCESS;
	return FWK_SUCCESS;
}

const struct fwk_module module_locked_perf = {
	.type = FWK_MODULE_TYPE_SERVICE,
	.api_count = MOD_LOCKED_PERF_API_IDX_COUNT,
	.init = locked_perf_init,
	.start = locked_perf_start,
	.process_bind_request = locked_perf_process_bind_request,
};
