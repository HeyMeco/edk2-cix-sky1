/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The clocks SCMI exposes (CIX's config_clock_lite.c): element i is the
 * tree node clk_nodes_export_to_os[i], named as the node, driven by
 * clkpll element i.
 */
#include <mod_clkpll.h>
#include <mod_clock.h>

#include <fwk_element.h>
#include <fwk_id.h>
#include <fwk_module.h>
#include <fwk_module_idx.h>

#include "clk/clk.h"
#include "scp_config.h"

static struct fwk_element elements[SCP_CLOCK_COUNT + 1];
static struct mod_clock_dev_config configs[SCP_CLOCK_COUNT];

static const struct fwk_element *get_elements(fwk_id_t module_id)
{
	(void)module_id;
	for (unsigned int i = 0; i < SCP_CLOCK_COUNT; i++) {
		const struct mod_clock_dev_config c = {
			.driver_id = FWK_ID_ELEMENT_INIT(FWK_MODULE_IDX_CLKPLL, i),
			.api_id = FWK_ID_API_INIT(FWK_MODULE_IDX_CLKPLL, MOD_CLKPLL_API_IDX_DRIVER),
			.pd_source_id = FWK_ID_NONE_INIT,
		};

		memcpy(&configs[i], &c, sizeof(c));
		elements[i] = (struct fwk_element){
			.name = clk_tree_st[clk_nodes_export_to_os[i]].name,
			.data = &configs[i],
		};
	}
	return elements;
}

const struct fwk_module_config config_clock = {
	/* the clocks follow no power domain (pd_source_id none) */
	.data = &(const struct mod_clock_config){
		.pd_transition_notification_id = FWK_ID_NONE_INIT,
		.pd_pre_transition_notification_id = FWK_ID_NONE_INIT,
	},
	.elements = FWK_MODULE_DYNAMIC_ELEMENTS(get_elements),
};
