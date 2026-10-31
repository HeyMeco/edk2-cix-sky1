/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * SCMI shared memory in the mailboxes' data words (TF-A's mailbox_mem_t);
 * the PM initialises the a2p ones once SYSTOP reports on, the TF-A
 * channel is secure.
 */
#include <mod_cix_mbox.h>
#include <mod_transport.h>

#include <fwk_element.h>
#include <fwk_id.h>
#include <fwk_module.h>
#include <fwk_module_idx.h>

#include "mbox.h"
#include "scp_config.h"

#define CHANNEL(n, i, type, pol)						\
	[i] = { .name = n, .data = &(const struct mod_transport_channel_config){	\
		.transport_type = MOD_TRANSPORT_CHANNEL_TRANSPORT_TYPE_OUT_BAND,	\
		.channel_type = MOD_TRANSPORT_CHANNEL_TYPE_##type,			\
		.policies = MOD_TRANSPORT_POLICY_INIT_MAILBOX | (pol),		\
		.out_band_mailbox_address = MBOX_DATA(i, 0),			\
		.out_band_mailbox_size = 32 * 4,					\
		.pd_source_id = FWK_ID_ELEMENT_INIT(FWK_MODULE_IDX_POWER_DOMAIN,	\
						    SCP_PD_SYSTOP),			\
		.driver_id = FWK_ID_ELEMENT_INIT(FWK_MODULE_IDX_CIX_MBOX, i),		\
		.driver_api_id = FWK_ID_API_INIT(FWK_MODULE_IDX_CIX_MBOX,		\
						 MOD_CIX_MBOX_API_IDX_TRANSPORT_DRIVER), \
	} }

static const struct fwk_element elements[SCP_CH_COUNT + 1] = {
	CHANNEL("OSPM", SCP_CH_OSPM, COMPLETER, 0),
	CHANNEL("OSPM-P2A", SCP_CH_OSPM_P2A, REQUESTER, 0),
	CHANNEL("PSCI", SCP_CH_PSCI, COMPLETER, MOD_TRANSPORT_POLICY_SECURE),
	CHANNEL("PSCI-P2A", SCP_CH_PSCI_P2A, REQUESTER, MOD_TRANSPORT_POLICY_SECURE),
	CHANNEL("VTR0", SCP_CH_VTR0, COMPLETER, 0),
	CHANNEL("VTR0-P2A", SCP_CH_VTR0_P2A, REQUESTER, 0),
	CHANNEL("VTR1", SCP_CH_VTR1, COMPLETER, 0),
	CHANNEL("VTR1-P2A", SCP_CH_VTR1_P2A, REQUESTER, 0),
	[SCP_CH_COUNT] = { 0 },
};

const struct fwk_module_config config_transport = {
	.elements = FWK_MODULE_STATIC_ELEMENTS_PTR(elements),
};
