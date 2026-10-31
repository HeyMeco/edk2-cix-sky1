/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* SCMI clocks (CIX's config_scmi_clock.c): all 273 for the OS, none for the others */
#include <mod_scmi_clock.h>

#include <fwk_element.h>
#include <fwk_id.h>
#include <fwk_module.h>
#include <fwk_module_idx.h>

#include <string.h>

#include "scp_config.h"

static struct mod_scmi_clock_device devices[SCP_CLOCK_COUNT];
static struct mod_scmi_clock_agent agents[SCP_AGENT_COUNT];

/* Filled before the framework reads the configuration */
static void __attribute__((constructor)) fill_devices(void)
{
	for (unsigned int i = 0; i < SCP_CLOCK_COUNT; i++) {
		const struct mod_scmi_clock_device d = {
			.element_id = FWK_ID_ELEMENT_INIT(FWK_MODULE_IDX_CLOCK, i),
		};

		memcpy(&devices[i], &d, sizeof(d));
	}
	agents[SCP_AGENT_OSPM] = (struct mod_scmi_clock_agent){
		.device_table = devices,
		.device_count = SCP_CLOCK_COUNT,
	};
}

const struct fwk_module_config config_scmi_clock = {
	.data = &(const struct mod_scmi_clock_config){
		.max_pending_transactions = 8,
		.agent_table = agents,
		.agent_count = SCP_AGENT_COUNT,
	},
};
