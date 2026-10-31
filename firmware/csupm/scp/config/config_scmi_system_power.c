/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* SCMI system power (CIX's config_scmi_system_power.c) */
#include <mod_scmi_system_power.h>

#include <fwk_id.h>
#include <fwk_module.h>

const struct fwk_module_config config_scmi_system_power = {
	.data = &(const struct mod_scmi_system_power_config){
		.system_view = MOD_SCMI_SYSTEM_VIEW_FULL,
		.wakeup_power_domain_id = { 0 },	/* invalid */
		.wakeup_composite_state = 0,
		.system_suspend_state = 3,
		.alarm_id = FWK_ID_NONE_INIT,
		.graceful_timeout = 0,
	},
};
