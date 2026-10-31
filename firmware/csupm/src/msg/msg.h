/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * CIX's PM messages (Source/common/modules/md_msg.c and the message tables
 * of Source/projects/sky1/messages): a format of CIX's own in the SCMI
 * channels' shared memory, told apart from SCMI by the route field of the
 * first word (msgAttr [15:8], 0x0c; TF-A's scmi_send_msg_to_pm).
 *
 *   word 0  msgAttr: [6:0] buffer length (0x3f from TF-A), [15:8] route
 *           (0x0c; the PM's answer sets [15:8] to 0xc0)
 *   word 1  status: bit 0 free (the PM owns the channel while it is clear)
 *   word 2  status code of the answer
 *   word 3  trace code of the answer (CIX's internal codes)
 *   word 4  flags: bit 0, answer with an interrupt
 *   word 5  length: 4 + 4 * payload words
 *   word 6  header: [15:0] message id, [27:18] token
 *   word 7  payload: 12 words in, the answer's from word 19 (payload[12])
 *
 * A port is a mailbox the AP rings (0, 2, 4, 6 with CIX's message table,
 * the others inactive).  Messages run at once; CIX defers some to a job
 * (answered when it ran, or at once and run later), which here run in
 * line in the same order.
 */
#ifndef MSG_H
#define MSG_H

#include "pm.h"

#define MSG_PORTS		10

typedef uint32_t (*MD_MSG_FUNCTION)(uint32_t port, uint32_t *len, void *in, void *out,
				    uint32_t *trace);

typedef struct {
	uint16_t msgId;
	uint8_t type;			/* 0 at once, 1 answered when done, 2 answered first */
	uint8_t priority;		/* CIX's job priority */
	uint32_t msgFlags;		/* bit 0 disabled, 1 run in a critical zone, 2 force append */
	MD_MSG_FUNCTION msgProc;
} MD_MSG_TABLE;

/* md_msg.c */
bool md_msg_port_register_table(uint32_t port, const MD_MSG_TABLE *tab);
bool md_msg_port_enable(uint32_t port);
void md_msg_port_set_default_profile(uint32_t port, uint32_t mbox_inst_id, uintptr_t chan);
bool md_msg_port_dispatch(uint32_t port);

/* app_msg.c: CIX's app_msginit.c */
void app_msgInit(void);
void app_msgSave(void);
void app_msgRestore(void);
bool app_msg_on_doorbell(unsigned int mailbox);
extern const MD_MSG_TABLE _portX_msg[];

#endif
