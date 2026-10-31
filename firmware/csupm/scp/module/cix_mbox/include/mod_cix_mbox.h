/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Mailbox driver under SCP-firmware's transport module (the doorbell part
 * of CIX's ipc_arbiter): one element per mailbox carrying an SCMI channel.
 * The channel's shared memory is the mailbox's 32 data words.
 */
#ifndef MOD_CIX_MBOX_H
#define MOD_CIX_MBOX_H

#include <fwk_id.h>

#include <stdbool.h>
#include <stdint.h>

struct mod_cix_mbox_config {
	unsigned int mailbox;		/* 0-9 */
	bool from_ap;			/* the AP rings, the PM answers (a2p) */
};

enum mod_cix_mbox_api_idx {
	MOD_CIX_MBOX_API_IDX_TRANSPORT_DRIVER,
	MOD_CIX_MBOX_API_IDX_COUNT,
};

#endif
