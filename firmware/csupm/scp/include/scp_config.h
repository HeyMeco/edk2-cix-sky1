/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * SCMI channels of the PM (CIX's config_ipc_arbiter.c and config_scmi.c).
 *
 * Mailboxes 0-7 are four agents' channels: the even mailbox carries the
 * agent's requests (a2p), the odd one the PM's notifications to it (p2a).
 */
#ifndef SCP_CONFIG_H
#define SCP_CONFIG_H

enum scp_channel {
	SCP_CH_OSPM,		/* mailbox 0: the OS */
	SCP_CH_OSPM_P2A,	/* 1 */
	SCP_CH_PSCI,		/* 2: TF-A, secure */
	SCP_CH_PSCI_P2A,	/* 3: CIX calls it "PSCI (backup)" */
	SCP_CH_VTR0,		/* 4 */
	SCP_CH_VTR0_P2A,	/* 5 */
	SCP_CH_VTR1,		/* 6 */
	SCP_CH_VTR1_P2A,	/* 7 */
	SCP_CH_COUNT,
};

/* SCMI agent ids (0 is the platform) */
enum scp_agent {
	SCP_AGENT_OSPM = 1,
	SCP_AGENT_PSCI,
	SCP_AGENT_VTR0,
	SCP_AGENT_VTR1,
	SCP_AGENT_COUNT,
};

/* SCMI services: the a2p channels, then the p2a ones */
enum scp_service {
	SCP_SVC_OSPM,
	SCP_SVC_PSCI,
	SCP_SVC_VTR0,
	SCP_SVC_VTR1,
	SCP_SVC_OSPM_P2A,
	SCP_SVC_PSCI_P2A,
	SCP_SVC_VTR0_P2A,
	SCP_SVC_VTR1_P2A,
	SCP_SVC_COUNT,
};

/* Power domains (the first two are SCMI's 0 and 1) */
enum scp_pd {
	SCP_PD_ISP0,
	SCP_PD_GPUTOP,
	SCP_PD_SYSTOP,
	SCP_PD_COUNT,
};

#define SCP_SENSOR_COUNT	20
#define SCP_CLOCK_COUNT		273	/* clk_nodes_export_to_os */

#endif
