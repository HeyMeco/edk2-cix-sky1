/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * SCMI agents and services as in CIX's config_scmi.c: four agents, each
 * with an a2p service and (but for TF-A) a p2a one for notifications.
 */
#include <mod_scmi.h>
#include <mod_transport.h>

#include <fwk_element.h>
#include <fwk_id.h>
#include <fwk_module.h>
#include <fwk_module_idx.h>

#include "scp_config.h"

#define SERVICE(n, i, ch, agent, p2a)						\
	[i] = { .name = n, .data = &(const struct mod_scmi_service_config){	\
		.transport_id = FWK_ID_ELEMENT_INIT(FWK_MODULE_IDX_TRANSPORT, ch),	\
		.transport_api_id = FWK_ID_API_INIT(FWK_MODULE_IDX_TRANSPORT,	\
					MOD_TRANSPORT_API_IDX_SCMI_TO_TRANSPORT),	\
		.transport_notification_init_id = FWK_ID_NOTIFICATION_INIT(	\
					FWK_MODULE_IDX_TRANSPORT,			\
					MOD_TRANSPORT_NOTIFICATION_IDX_INITIALIZED),	\
		.scmi_agent_id = agent,						\
		.scmi_p2a_id = p2a,						\
		.scmi_entity_role = MOD_SCMI_ROLE_PLATFORM,			\
	} }

#define P2A(svc)	FWK_ID_ELEMENT_INIT(FWK_MODULE_IDX_SCMI, svc)

static const struct fwk_element services[SCP_SVC_COUNT + 1] = {
	SERVICE("OSPM", SCP_SVC_OSPM, SCP_CH_OSPM, SCP_AGENT_OSPM, P2A(SCP_SVC_OSPM_P2A)),
	SERVICE("PSCI", SCP_SVC_PSCI, SCP_CH_PSCI, SCP_AGENT_PSCI, FWK_ID_NONE_INIT),
	SERVICE("VTR0", SCP_SVC_VTR0, SCP_CH_VTR0, SCP_AGENT_VTR0, P2A(SCP_SVC_VTR0_P2A)),
	SERVICE("VTR1", SCP_SVC_VTR1, SCP_CH_VTR1, SCP_AGENT_VTR1, P2A(SCP_SVC_VTR1_P2A)),
	SERVICE("OSPM-P2A", SCP_SVC_OSPM_P2A, SCP_CH_OSPM_P2A, SCP_AGENT_OSPM, FWK_ID_NONE_INIT),
	SERVICE("PSCI-P2A", SCP_SVC_PSCI_P2A, SCP_CH_PSCI_P2A, SCP_AGENT_PSCI, FWK_ID_NONE_INIT),
	SERVICE("VTR0-P2A", SCP_SVC_VTR0_P2A, SCP_CH_VTR0_P2A, SCP_AGENT_VTR0, FWK_ID_NONE_INIT),
	SERVICE("VTR1-P2A", SCP_SVC_VTR1_P2A, SCP_CH_VTR1_P2A, SCP_AGENT_VTR1, FWK_ID_NONE_INIT),
	[SCP_SVC_COUNT] = { 0 },
};

static const struct mod_scmi_agent agents[SCP_AGENT_COUNT] = {
	[SCP_AGENT_OSPM] = { .type = SCMI_AGENT_TYPE_OSPM, .name = "OSPM" },
	[SCP_AGENT_PSCI] = { .type = SCMI_AGENT_TYPE_PSCI, .name = "PSCI" },
	[SCP_AGENT_VTR0] = { .type = SCMI_AGENT_TYPE_OSPM, .name = "VTR0" },
	[SCP_AGENT_VTR1] = { .type = SCMI_AGENT_TYPE_OSPM, .name = "VTR1" },
};

const struct fwk_module_config config_scmi = {
	.data = &(const struct mod_scmi_config){
		.protocol_count_max = 9,
		.agent_count = SCP_AGENT_COUNT - 1,
		.agent_table = agents,
		.vendor_identifier = "cix",
		.sub_vendor_identifier = "cix",
	},
	.elements = FWK_MODULE_STATIC_ELEMENTS_PTR(services),
};
