/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* One clkpll element per SCMI clock; they need no configuration */
#include <fwk_element.h>
#include <fwk_module.h>

#include "scp_config.h"

static const struct fwk_element *get_elements(fwk_id_t module_id)
{
	static struct fwk_element elements[SCP_CLOCK_COUNT + 1];

	(void)module_id;
	for (unsigned int i = 0; i < SCP_CLOCK_COUNT; i++)
		elements[i] = (struct fwk_element){ .name = "", .data = elements };
	return elements;
}

const struct fwk_module_config config_clkpll = {
	.elements = FWK_MODULE_DYNAMIC_ELEMENTS(get_elements),
};
