/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The two Realtek RTS5453H USB-C PD controllers on SMBus F (0x67, 0x68),
 * set up as the vendor firmware does when the board reaches standby and
 * again in S0: each gets the vendor-command enable (01: DA 0B 01),
 * notifications (08: 01 00 40 40 00 00) and command 0E (01 00), each
 * followed by the ping status until the controller reports completion.
 * Commands follow Realtek's SMBus protocol (cmd, length, data; ping by
 * receive byte, bits 1:0 = 1 when done).
 */
#include "ec.h"

static __code const uint8_t cmd_en[5] = { 0x01, 0x03, 0xDA, 0x0B, 0x01 };
static __code const uint8_t cmd_ntf[8] = { 0x08, 0x06, 0x01, 0x00, 0x40, 0x40, 0x00, 0x00 };
static __code const uint8_t cmd_0e[4] = { 0x0E, 0x02, 0x01, 0x00 };
static __code const uint8_t cmd_status[5] = { 0x3A, 0x03, 0x00, 0x00, 0x14 };
static __code const uint8_t *const cmds[3] = { cmd_en, cmd_ntf, cmd_0e };
static __code const uint8_t lens[3] = { 5, 8, 4 };

/* PD1's firmware version (GET_IC_STATUS data bytes 3, 4), for 3E07 */
__xdata uint8_t pd_ver[2];
static uint8_t pd_status;		/* read the IC status after the set-up */

static __xdata uint8_t buf[8];
static uint8_t pd_i, pd_step, pd_busy_;
static uint16_t pd_at;

uint8_t pd_busy(void)
{
	return pd_busy_;
}

void pd_init_all_status(void)
{
	pd_init_all();
	pd_status = 1;
}

void pd_init_all(void)
{
	pd_status = 0;
	pd_i = 0;
	pd_step = 0;
	pd_busy_ = 1;
	pd_at = ms_now + 15;
}

/* one transaction per call, paced like the vendor's */
void pd_poll(void)
{
	uint8_t addr, i, n;

	if (!pd_busy_ || !after(pd_at))
		return;
	if (pd_i == 2) {			/* PD1's IC status, in S0 */
		static __xdata uint8_t st[21];
		static __code const uint8_t *const sc[2] = { cmd_en, cmd_status };

		if (pd_step < 4 && !(pd_step & 1)) {
			n = pd_step >> 1;
			for (i = 0; i < 5; i++)
				buf[i] = sc[n][i];
			if (i2c_xfer(SMB_F, 0x67, buf, 5, 0, 0)) {
				pd_at = ms_now + 27;
				return;
			}
			pd_step++;
			/* the controller takes a while to gather its status */
			pd_at = ms_now + (pd_step == 3 ? 565 : 64);
			return;
		}
		if (pd_step < 4) {		/* ping until done */
			if (i2c_xfer(SMB_F, 0x67, 0, 0, buf, 1) || (buf[0] & 3) != 1) {
				pd_at = ms_now + 27;
				return;
			}
			pd_step++;
			pd_at = ms_now + (pd_step == 4 ? 22 : 4);
			return;
		}
		buf[0] = 0x80;
		if (!i2c_xfer(SMB_F, 0x67, buf, 1, st, 21)) {
			pd_ver[0] = st[4];
			pd_ver[1] = st[5];
		}
		pd_busy_ = 0;
		return;
	}
	addr = pd_i ? 0x68 : 0x67;
	if (pd_step == 0) {
		/* the vendor reads the SMBus alert response address first */
		i2c_xfer(SMB_F, 0x0C, 0, 0, buf, 1);
		pd_step = 1;
		pd_at = ms_now + 3;
		return;
	}
	if (pd_step & 1) {			/* send command (step - 1) / 2 */
		n = lens[pd_step >> 1];
		for (i = 0; i < n; i++)
			buf[i] = cmds[pd_step >> 1][i];
		if (i2c_xfer(SMB_F, addr, buf, n, 0, 0)) {
			pd_at = ms_now + 27;	/* retry, as the vendor */
			return;
		}
		pd_step++;
		pd_at = ms_now + 64;
		return;
	}
	if (i2c_xfer(SMB_F, addr, 0, 0, buf, 1) || (buf[0] & 3) != 1) {
		pd_at = ms_now + 27;
		return;
	}
	pd_step++;
	pd_at = ms_now + 4;
	if (pd_step == 7) {
		pd_step = 0;
		if (++pd_i == 2 && !pd_status)
			pd_busy_ = 0;
		pd_at = ms_now + 15;
	}
}
