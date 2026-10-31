/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Mailbox driver for the transport module.  Every mailbox interrupts the
 * PM on the same aggregator line; the handler finds the mailboxes whose
 * doorbell rang, clears them and signals their transport channel.
 *
 * A write to one of a mailbox's fast channels also sets a status bit
 * (mbox.h).  As CIX's ipc_arbiter (_on_mbox_event) does, writes to the OS's
 * performance fast channels (mailbox 0, all but channel 1) go to scmi_perf;
 * the others (channel 1 of mailbox 0, CIX's PCC commands, not handled yet,
 * and mailbox 2's) are cleared and dropped.
 */
#include <mod_cix_mbox.h>
#ifdef BUILD_HAS_MOD_SCMI_PERF
#include <mod_scmi_perf.h>
#endif
#include <mod_transport.h>

#include <fwk_id.h>
#include <fwk_interrupt.h>
#include <fwk_mm.h>
#include <fwk_module.h>
#include <fwk_module_idx.h>
#include <fwk_status.h>

#include "irq.h"
#include "mbox.h"

/* Mailbox 0's fast channel 1: CIX's PCC commands */
#define PCC_FAST_BIT	BIT(9 + 1)

struct mbox_ctx {
	const struct mod_cix_mbox_config *config;
	fwk_id_t channel;
	const struct mod_transport_driver_input_api *transport;
	bool bound;
};

static struct {
	struct mbox_ctx *ctx;
	unsigned int count;
} cix_mbox;

static void mbox_isr(void)
{
	for (unsigned int i = 0; i < cix_mbox.count; i++) {
		struct mbox_ctx *c = &cix_mbox.ctx[i];
		unsigned int m = c->config->mailbox;
		uint32_t st;

		if (!c->bound || !c->config->from_ap)
			continue;
		st = REG32(MBOX_STATUS(m)) & (MBOX_DOORBELL_BIT | MBOX_FAST_BITS);
		if (!st)
			continue;
		REG32(MBOX_CLEAR(m)) = st;
#ifdef BUILD_HAS_MOD_SCMI_PERF
		if (m == 0 && (st & MBOX_FAST_BITS & ~PCC_FAST_BIT))
			on_fast_channel_interrupt(false);
#endif
		if (st & MBOX_DOORBELL_BIT)
			(void)c->transport->signal_message(c->channel);
	}
}

/* Completion (a2p, when the agent asked for it) or a notification (p2a) */
static int trigger_event(fwk_id_t device_id)
{
	struct mbox_ctx *c = &cix_mbox.ctx[fwk_id_get_element_idx(device_id)];

	mbox_ring(c->config->mailbox, MBOX_TO_AP_BIT);
	return FWK_SUCCESS;
}

static const struct mod_transport_driver_api driver_api = {
	.trigger_event = trigger_event,
};

static int cix_mbox_init(fwk_id_t module_id, unsigned int count, const void *data)
{
	(void)module_id;
	(void)data;
	cix_mbox.ctx = fwk_mm_calloc(count, sizeof(cix_mbox.ctx[0]));
	cix_mbox.count = count;
	return FWK_SUCCESS;
}

static int cix_mbox_element_init(fwk_id_t id, unsigned int sub, const void *data)
{
	(void)sub;
	if (data == NULL)
		return FWK_E_PARAM;
	cix_mbox.ctx[fwk_id_get_element_idx(id)].config = data;
	return FWK_SUCCESS;
}

static int cix_mbox_bind(fwk_id_t id, unsigned int round)
{
	struct mbox_ctx *c;

	if (round != 1 || !fwk_id_is_type(id, FWK_ID_TYPE_ELEMENT))
		return FWK_SUCCESS;
	c = &cix_mbox.ctx[fwk_id_get_element_idx(id)];
	if (!c->bound)
		return FWK_SUCCESS;
	return fwk_module_bind(c->channel,
			       FWK_ID_API(FWK_MODULE_IDX_TRANSPORT, MOD_TRANSPORT_API_IDX_DRIVER_INPUT),
			       &c->transport);
}

static int cix_mbox_process_bind_request(fwk_id_t source_id, fwk_id_t target_id,
					 fwk_id_t api_id, const void **api)
{
	struct mbox_ctx *c;

	(void)api_id;
	if (!fwk_id_is_type(target_id, FWK_ID_TYPE_ELEMENT))
		return FWK_E_ACCESS;
	c = &cix_mbox.ctx[fwk_id_get_element_idx(target_id)];
	if (c->bound)
		return FWK_E_ACCESS;
	c->channel = source_id;
	c->bound = true;
	*api = &driver_api;
	return FWK_SUCCESS;
}

/*
 * A p2a mailbox starts free for the PM to send in, as CIX's ipc_arbiter
 * leaves it (the transport module initialises only the a2p ones, from
 * the agent's side of the protocol): SMT header words reserved, status
 * (free), reserved, flags, length, message header, all zero but status.
 */
static void init_p2a(unsigned int m)
{
	REG32(MBOX_DATA(m, 0)) = 0;
	REG32(MBOX_DATA(m, 1)) = 1;
	for (unsigned int w = 2; w < 8; w++)
		REG32(MBOX_DATA(m, w)) = 0;
}

/* Once every element is started: route the a2p mailboxes to the PM */
static int cix_mbox_start(fwk_id_t id)
{
	uint32_t sources = 0;
	int status;

	if (!fwk_id_is_type(id, FWK_ID_TYPE_MODULE))
		return FWK_SUCCESS;
	for (unsigned int i = 0; i < cix_mbox.count; i++) {
		if (!cix_mbox.ctx[i].bound)
			continue;
		if (cix_mbox.ctx[i].config->from_ap)
			sources |= BIT(cix_mbox.ctx[i].config->mailbox);
		else
			init_p2a(cix_mbox.ctx[i].config->mailbox);
	}
	status = fwk_interrupt_set_isr(IRQ_MBOX, mbox_isr);
	if (status != FWK_SUCCESS)
		return status;
	for (unsigned int m = 0; m < MBOX_N; m++) {
		if (sources & BIT(m))
			REG32(AGG_ENABLE(IRQ_MBOX)) |= BIT(m);
	}
	return fwk_interrupt_enable(IRQ_MBOX);
}

const struct fwk_module module_cix_mbox = {
	.type = FWK_MODULE_TYPE_DRIVER,
	.api_count = MOD_CIX_MBOX_API_IDX_COUNT,
	.init = cix_mbox_init,
	.element_init = cix_mbox_element_init,
	.bind = cix_mbox_bind,
	.start = cix_mbox_start,
	.process_bind_request = cix_mbox_process_bind_request,
};
