/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Which agent may use which protocol (CIX's config_resource_perms.c): the
 * OS everything, TF-A base, power domains and system power, the two VTR
 * agents everything but clocks.  The message masks are CIX's.
 */
#include <mod_resource_perms.h>

#include <fwk_module.h>

#include "scp_config.h"

/* indexed by agent id - 1 */
static struct mod_res_agent_protocol_permissions protocols[SCP_AGENT_COUNT] = {
	[SCP_AGENT_OSPM - 1] = { .protocols = MOD_RES_PERMS_SCMI_ALL_PROTOCOLS_ALLOWED },
	[SCP_AGENT_PSCI - 1] = { .protocols = MOD_RES_PERMS_SCMI_PERF_PROTOCOL_DENIED |
					       MOD_RES_PERMS_SCMI_CLOCK_PROTOCOL_DENIED |
					       MOD_RES_PERMS_SCMI_SENSOR_PROTOCOL_DENIED },
	[SCP_AGENT_VTR0 - 1] = { .protocols = MOD_RES_PERMS_SCMI_CLOCK_PROTOCOL_DENIED },
	[SCP_AGENT_VTR1 - 1] = { .protocols = MOD_RES_PERMS_SCMI_CLOCK_PROTOCOL_DENIED },
};

static struct mod_res_agent_msg_permissions messages[SCP_AGENT_COUNT] = {
	[SCP_AGENT_OSPM - 1] = { .messages = { [3] = 0x00c } },
	[SCP_AGENT_PSCI - 1] = { .messages = { [3] = 0x1fd } },
	[SCP_AGENT_VTR0 - 1] = { .messages = { [3] = 0x00c } },
	[SCP_AGENT_VTR1 - 1] = { .messages = { [3] = 0x00c } },
};

const struct fwk_module_config config_resource_perms = {
	.data = &(const struct mod_res_resource_perms_config){
		.agent_count = SCP_AGENT_COUNT,
		.protocol_count = 6,
		.agent_permissions = (uintptr_t)&(const struct mod_res_agent_permission){
			.agent_protocol_permissions = protocols,
			.agent_msg_permissions = messages,
		},
	},
};
