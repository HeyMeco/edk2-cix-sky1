/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Requests from the AP by function ID (CIX framework/fw_dispatcher.c) */
#include "se.h"
#include "mailbox.h"
#include "services.h"

#define MAX_SERVICES	255

static struct {
	uint32_t fid;
	fw_service_cb cb;
} services_cbs[MAX_SERVICES];

static void ipc_echo_request_handler(void *param, uint32_t plen, void *resp, uint32_t *rlen)
{
	*rlen = 0;
}

static void ipc_set_power_handler(void *param, uint32_t plen, void *resp, uint32_t *rlen)
{
	*rlen = plen + 4;
}

static const ipc_handle *current;

bool ipc_request_from_tee(void)
{
	return current && current->tee;
}

static void fw_dispatcher_ipc_cb(ipc_handle *h)
{
	unsigned int i;

	for (i = 0; h->fid && i < MAX_SERVICES; i++) {
		if (services_cbs[i].fid == h->fid) {
			current = h;
			services_cbs[i].cb(h->param, h->plen, h->resp, &h->rlen);
			current = NULL;
			return;
		}
	}
	SE_INFO("Mismatch function ID: 0x%x, it's not registered\n", (unsigned)h->fid);
	SE_INFO("Dispatcher 0x%x fail\n", (unsigned)h->fid);
}

uint32_t fw_dispatcher_reg_handler(uint32_t fid, fw_service_cb cb)
{
	unsigned int i;

	if (fid == 0 || cb == NULL)
		return 0x9101;
	for (i = 0; i < MAX_SERVICES; i++) {
		if (services_cbs[i].fid == 0) {
			services_cbs[i].fid = fid;
			services_cbs[i].cb = cb;
			SE_INFO("Register functio ID: 0x%x successful\n", (unsigned)fid);
			return 0;
		}
	}
	SE_INFO("Register handle fail, services_cbs was full\n");
	return 0x9102;
}

uint32_t fw_dispatcher_init(void)
{
	uint32_t err;

	hal_ipc_register_dispatcher_callback(fw_dispatcher_ipc_cb);
	memset(services_cbs, 0, sizeof(services_cbs));
	err = fw_dispatcher_reg_handler(0x82000001, ipc_echo_request_handler);
	if (err) {
		SE_ERR(err);
		return err;
	}
	hal_pc_trace(0x9015);
	err = fw_dispatcher_reg_handler(0x82000005, ipc_set_power_handler);
	if (err) {
		SE_ERR(err);
		return err;
	}
	hal_pc_trace(0x9016);
	return 0;
}
