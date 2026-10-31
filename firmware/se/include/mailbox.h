/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Mailbox channels and the IPC on top of them (driver/mailbox.c, hal/ipc.c, framework/dispatcher.c) */
#ifndef MAILBOX_H
#define MAILBOX_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
	MBOX_CHN_AP2SE_REE, MBOX_CHN_SE2AP_REE, MBOX_CHN_AP2SE_TEE, MBOX_CHN_SE2AP_TEE,
	MBOX_CHN_AP2SE_RESERVED1, MBOX_CHN_SE2AP_RESERVED1, MBOX_CHN_AP2SE_RESERVED2,
	MBOX_CHN_SE2AP_RESERVED2, MBOX_CHN_SE2PM, MBOX_CHN_MAX,
} mbox_chan_id;

#define MBOX_ERR_PARAM		0x9a01
#define MBOX_ERR_TIMEOUT	0x9a02
#define MBOX_ERR_BUSY		0x9a03
#define MBOX_ERR_CHAN		0x9a04

typedef struct mbox_client mbox_client;
struct mbox_client {
	uint32_t tx_tout;		/* ms; 0: 100 */
	bool tx_block;			/* do not wait for the ack */
	uint8_t chan;
	uint8_t chan_type;
	uintptr_t shared_mem;
	void (*rx_callback)(mbox_client *cl, void *in, void *out);
	void (*tx_prepare)(mbox_client *cl, const void *msg);
	void (*tx_done)(mbox_client *cl);
};

/* Message: bits [6:0] of the first word are its size in bytes, header included */
typedef struct {
	uint32_t size : 7;
	uint32_t type : 3;
	uint32_t reserve1 : 22;
	uint32_t cmd_id;
	uint32_t data[30];
} mbox_msg_t;

uint32_t drv_mbox_init(mbox_client *cl, mbox_chan_id chan, int index);
uint32_t drv_mbox_deinit(mbox_chan_id chan);
uint32_t mbox_send_data_sync(mbox_chan_id chan, const void *msg);

/* IPC: a request is cmd_id (function ID) and parameters; handlers fill the response */
typedef void (*fw_service_cb)(void *param, uint32_t plen, void *resp, uint32_t *rlen);

typedef struct ipc_handle ipc_handle;
struct ipc_handle {
	mbox_client cl;
	void (*dispatch)(ipc_handle *h);
	uint32_t fid;
	void *param;
	void *resp;
	uint32_t plen;
	uint32_t rlen;
	bool tee;		/* the TEE's (secure) mailbox */
};

uint32_t hal_ipc_init(void);
uint32_t hal_ipc_uninit(void);
void hal_ipc_resume(void);
void hal_ipc_suspend(void);
uint32_t hal_ipc_send_data(mbox_chan_id chan, const void *msg);
void hal_ipc_register_dispatcher_callback(void (*cb)(ipc_handle *h));

uint32_t fw_dispatcher_init(void);
uint32_t fw_dispatcher_reg_handler(uint32_t fid, fw_service_cb cb);

#endif
