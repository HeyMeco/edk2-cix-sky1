/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The PM's message ports on the mailboxes (CIX's Source/projects/sky1/
 * messages/app_msginit.c): app_msgInit, the part of _on_mbox_event that
 * hands a doorbell to md_msg before SCMI, and the mailboxes' state kept
 * across suspend (app_msgSave in ss_run_exit, app_msgRestore in
 * ss_run_enter).
 */
#include "msg/msg.h"
#include "irq.h"
#include "mbox.h"

/* CIX's _s_mbox_cfg: the direction each mailbox interrupts (pinout) and
 * the other one (pinoutOpposite), whose enables app_msgSave keeps */
static const uint8_t pinout_opposite[MBOX_N] = {
	MBOX_TO_AP, MBOX_TO_PM, MBOX_TO_AP, MBOX_TO_PM, MBOX_TO_AP,
	MBOX_TO_PM, MBOX_TO_AP, MBOX_TO_PM, MBOX_TO_PM, MBOX_TO_AP,
};

/* CIX's _s_dvfs_fc_cfg: the OS's performance fast channels, level get in
 * mailbox 1 and level set in mailbox 0 */
#define FC_N		8
#define FC_GET(i)	(PM_MBOX(1) + 0x8c + 4 * (i))
#define FC_SET(i)	(PM_MBOX(0) + 0x8c + 4 * (i))

/* CIX's _s_mbox_free (0x80000000: nothing saved) and the two buffers it
 * allocates for the interrupt enables and the fast channels */
static uint32_t mbox_free = 0x80000000u;
static uint32_t saved_int_en[MBOX_N];
static uint32_t saved_fc[FC_N];
static bool int_en_saved, fc_saved;

/*
 * CIX's app_msgInit: the mailboxes cleared (all but the SE's, 8) and
 * routed (src/mbox.c), each a message port with the default profile on
 * its data words, CIX's message table on the AP's request mailboxes 0, 2,
 * 4, 6; their interrupts (and mailbox 9's) on the aggregator; then the
 * SE's handshake word cleared.  (CIX also creates the
 * ports' event and job queues, and starts its PCC service, src/pcc.)
 */
void app_msgInit(void)
{
	mbox_init();
	for (unsigned i = 0; i < MBOX_N; i++)
		md_msg_port_set_default_profile(i, i, PM_MBOX(i));
	for (unsigned i = 0; i < MBOX_N; i += 2) {
		if (i == 8)
			continue;
		md_msg_port_register_table(i, _portX_msg);
		md_msg_port_enable(i);
	}
	/* intr_lv2_enableIrq: the request mailboxes on aggregator line 25 */
	for (unsigned i = 0; i < MBOX_N; i++) {
		if (i == 9 || (!(i & 1) && i != 8))
			REG32(AGG_ENABLE(IRQ_MBOX)) |= BIT(i);
	}
	REG32(PM_MAGIC) = 0;
}

/*
 * The doorbell part of CIX's _on_mbox_event: a request mailbox (even, or
 * 9) first goes to the message ports; false when it holds SCMI.
 */
bool app_msg_on_doorbell(unsigned int mailbox)
{
	if (mailbox != 9 && (mailbox & 1))
		return false;
	return md_msg_port_dispatch(mailbox);
}

/* CIX's app_msgSave: which channels are free, the interrupt enables, the
 * fast channels */
void app_msgSave(void)
{
	mbox_free = 0;
	for (unsigned i = 0; i < MBOX_N; i++)
		mbox_free |= (REG32(MBOX_DATA(i, 1)) & 1) << i;
	for (unsigned i = 0; i < MBOX_N; i++)
		saved_int_en[i] = REG32(MBOX_INT_EN(i, pinout_opposite[i]));
	int_en_saved = true;
	for (unsigned i = 0; i < FC_N; i++)
		saved_fc[i] = REG32(FC_SET(i));
	fc_saved = true;
}

/* CIX's app_msgRestore, after app_msgInit cleared the mailboxes */
void app_msgRestore(void)
{
	if (mbox_free != 0x80000000u) {
		for (unsigned i = 0; i < MBOX_N; i++) {
			uint32_t v = REG32(MBOX_DATA(i, 1));

			REG32(MBOX_DATA(i, 1)) = (mbox_free >> i) & 1 ? v | 1 : v & ~1u;
		}
	}
	if (fc_saved) {
		for (unsigned i = 0; i < FC_N; i++)
			REG32(FC_GET(i)) = saved_fc[i];
		for (unsigned i = 0; i < FC_N; i++)
			REG32(FC_SET(i)) = saved_fc[i];
		fc_saved = false;
	}
	if (int_en_saved) {
		for (unsigned i = 0; i < MBOX_N; i++)
			REG32(MBOX_INT_EN(i, pinout_opposite[i])) = saved_int_en[i];
		int_en_saved = false;
	}
}
