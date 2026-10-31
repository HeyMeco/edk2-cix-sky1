/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <mod_cix_mbox.h>

#include <fwk_element.h>
#include <fwk_module.h>

#include "scp_config.h"

#define MBOX(n, i, a2p)	[i] = { .name = n, .data = &(const struct mod_cix_mbox_config){ \
				.mailbox = i, .from_ap = a2p } }

static const struct fwk_element elements[SCP_CH_COUNT + 1] = {
	MBOX("OSPM", SCP_CH_OSPM, true),
	MBOX("OSPM-P2A", SCP_CH_OSPM_P2A, false),
	MBOX("PSCI", SCP_CH_PSCI, true),
	MBOX("PSCI-P2A", SCP_CH_PSCI_P2A, false),
	MBOX("VTR0", SCP_CH_VTR0, true),
	MBOX("VTR0-P2A", SCP_CH_VTR0_P2A, false),
	MBOX("VTR1", SCP_CH_VTR1, true),
	MBOX("VTR1-P2A", SCP_CH_VTR1_P2A, false),
	[SCP_CH_COUNT] = { 0 },
};

const struct fwk_module_config config_cix_mbox = {
	.elements = FWK_MODULE_STATIC_ELEMENTS_PTR(elements),
};
