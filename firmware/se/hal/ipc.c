/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * IPC with the AP (CIX hal/common/hal_ipc.c): requests from the REE and
 * the TEE on register channels, answered in place; the SE's own messages
 * to the REE through a FIFO channel.
 */
#include "se.h"
#include "mailbox.h"

static ipc_handle ipc_handle_ap2se_ree, ipc_handle_ap2se_tee, ipc_handle_se2ap_ree;

static void ipc_rx_callback(mbox_client *cl, void *in, void *out)
{
	ipc_handle *h = (ipc_handle *)cl;
	uint32_t *msg = in, *rsp = out;

	if (msg == NULL || h->dispatch == NULL)
		return;
	h->fid = msg[1];
	h->param = msg + 2;
	h->plen = (msg[0] & 0x7f) - 8;
	if (rsp)
		h->resp = rsp + 2;
	h->dispatch(h);
	if (rsp) {
		rsp[1] = h->fid;
		rsp[0] = (rsp[0] & ~0x7fu) | ((h->rlen + 8) & 0x7f);
	}
}

static void ipc_tx_done(mbox_client *cl)
{
	hal_printf_level(LOG_INFO, "ipc tx done\n");
}

uint32_t hal_ipc_init(void)
{
	ipc_handle_ap2se_ree.cl.rx_callback = ipc_rx_callback;
	drv_mbox_init(&ipc_handle_ap2se_ree.cl, MBOX_CHN_AP2SE_REE, 10);
	ipc_handle_ap2se_tee.cl.rx_callback = ipc_rx_callback;
	ipc_handle_ap2se_tee.tee = true;
	drv_mbox_init(&ipc_handle_ap2se_tee.cl, MBOX_CHN_AP2SE_TEE, 10);
	ipc_handle_se2ap_ree.cl.tx_done = ipc_tx_done;
	ipc_handle_se2ap_ree.cl.tx_block = true;
	return drv_mbox_init(&ipc_handle_se2ap_ree.cl, MBOX_CHN_SE2AP_REE, 9);
}

uint32_t hal_ipc_uninit(void)
{
	drv_mbox_deinit(ipc_handle_ap2se_ree.cl.chan);
	drv_mbox_deinit(ipc_handle_ap2se_tee.cl.chan);
	return drv_mbox_deinit(ipc_handle_se2ap_ree.cl.chan);
}

void hal_ipc_resume(void)
{
	hal_ipc_init();
}

void hal_ipc_suspend(void)
{
	hal_ipc_uninit();
}

uint32_t hal_ipc_send_data(mbox_chan_id chan, const void *msg)
{
	return mbox_send_data_sync(chan, msg);
}

void hal_ipc_register_dispatcher_callback(void (*cb)(ipc_handle *h))
{
	ipc_handle_ap2se_ree.dispatch = cb;
	ipc_handle_ap2se_tee.dispatch = cb;
}
