/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Locked performance (CIX's config_locked_perf.c): the 12 DVFS domains */
#include <mod_locked_perf.h>

#include <fwk_module.h>

#include "scp_dpm.h"

const struct fwk_module_config config_locked_perf = {
	.data = &(const struct mod_locked_perf_config){
		.domain_count = 12,
		.put_event = dvfs_opp_put_event,
	},
};
