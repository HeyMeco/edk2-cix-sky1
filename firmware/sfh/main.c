/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Sensor fusion hub service: wake the system after a delay while it is
 * suspended to RAM.
 *
 *   AP (ap2sfh): SF_MSG_SUSPEND, seconds  -> SF_MSG_SUSPEND_ACK, wait, wake
 *                SF_MSG_RESUME            -> SF_MSG_RESUME_ACK, wake off
 *   PM (pm2sfh): SF_MSG_WAKE_AFTER, ms    -> wait, wake
 *
 * Replies go to the AP on sfh2ap.  Messages are three words: header (byte
 * length in bits 6:0), command, argument.  The waits run inside the
 * mailbox interrupt, as in the CIX image.
 */
#include "sfh.h"

#define MBOX_TYPE_FIFO_INDEX	9
#define CH_SFH2AP		0
#define CH_AP2SFH		1
#define CH_PM2SFH		3

static struct mbox_client clients[MBOX_CHANNELS];

static void reply(uint32_t cmd)
{
	uint32_t msg[3] = { 0x0c, cmd, 0 };

	mbox_send(CH_SFH2AP, msg);
}

static void rx(struct mbox_client *cl, const uint32_t *msg)
{
	unsigned int n = mbox_msg_words(msg);

	if (n != 3) {
		log_printf(LOG_ERR, "%s invalid msg len = %d\n", "mbox_rx_callback", n);
		return;
	}
	if (msg[1] < SF_MSG_MIN) {
		log_printf(LOG_ERR, "%s invalid msg, msg[MBOX_MSG_OFFSET] = %d\n", "mbox_rx_callback", msg[1]);
		return;
	}
	if (cl->channel == CH_AP2SFH) {
		if (msg[1] == SF_MSG_SUSPEND) {
			log_printf(LOG_DEBUG, "sf will suspend\n");
			sfh_log.enable = 0;
			sfh_state = 1;
			reply(SF_MSG_SUSPEND_ACK);
			timer_busy_wait_ms(msg[2] * 1000u);
			REG32(SFH_WAKE) = SFH_WAKE_REQUEST;
		} else if (msg[1] == SF_MSG_RESUME) {
			reply(SF_MSG_RESUME_ACK);
			REG32(SFH_WAKE) = 0;
			sfh_log.enable = 1;
			sfh_state = 2;
			log_set_level(LOG_DEBUG);
			log_printf(LOG_DEBUG, "sf will resume\n");
		}
	} else if (cl->channel == CH_PM2SFH && msg[1] == SF_MSG_WAKE_AFTER) {
		log_printf(LOG_DEBUG, "sf will delay %d ms and wakeup system...\n", msg[2]);
		timer_busy_wait_ms(msg[2]);
		REG32(SFH_WAKE) = SFH_WAKE_REQUEST;
	}
}

int main(void)
{
	log_set_level(LOG_DEBUG);
	log_printf(LOG_DEBUG, "Log mode is: %u\n", sfh_log.level);
	log_printf(LOG_DEBUG, "# open SFH firmware (edk2-cix)\n");

	for (unsigned int ch = 0; ch < MBOX_CHANNELS; ch++) {
		clients[ch].tx_timeout_ms = 100;
		clients[ch].tx_block = 1;
		clients[ch].rx = rx;
		if (mbox_request_channel(&clients[ch], ch, MBOX_TYPE_FIFO_INDEX))
			log_printf(LOG_ERR, "MBOX channel %d init failed!\n", ch);
		else
			log_printf(LOG_DEBUG, "MBOX channel %d initialized successfully.\n", ch);
	}
	timer_init();
	cpu_setup();

	REG32(SFH_HANDSHAKE) = SFH_HANDSHAKE_READY;
	log_printf(LOG_DEBUG, "sensor fusion init succeed!\n");
	for (;;)
		;
}
