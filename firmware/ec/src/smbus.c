/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * SMBus host channels as I2C masters (ITE's I2C-compatible byte mode, as
 * the vendor firmware talks to CSU_PM): C to CSU_PM's slave (address 4),
 * F to the two RTS5453H PD controllers (0x67, 0x68).  Timing and
 * channel set-up are the vendor's.
 */
#include "ec.h"

struct smb {
	uint16_t hosta, hoctl, hocmd, trasla, d0, hobdb, hoctl2;
};

static __code const struct smb ch_regs[6] = {
	{ 0x1C00, 0x1C01, 0x1C02, 0x1C03, 0x1C04, 0x1C06, 0x1C10 },
	{ 0x1C11, 0x1C12, 0x1C13, 0x1C14, 0x1C15, 0x1C17, 0x1C21 },
	{ 0x1C29, 0x1C2A, 0x1C2B, 0x1C2C, 0x1C2D, 0x1C2F, 0x1C32 },
	{ 0x1C35, 0x1C36, 0x1C37, 0x1C38, 0x1C39, 0x1C3B, 0x1C3E },
	{ 0x1CA0, 0x1CA1, 0x1CA2, 0x1CA3, 0x1CA4, 0x1CA7, 0x1CAA },
	{ 0x1CB0, 0x1CB1, 0x1CB2, 0x1CB3, 0x1CB4, 0x1CB7, 0x1CBA },
};

#define R(c, f)	XREG(ch_regs[c].f)

void smbus_init(void)
{
	static __code const uint8_t sclk[6] = { 0x40, 0x41, 0x42, 0x43, 0xAB, 0xBB };
	uint8_t c;

	XREG(0x1C22) = 0x00;		/* timing: 4.7 us, 4.0 us, 300 ns, 250 ns, 25 ms, 45.3 us */
	XREG(0x1C23) = 0x25;
	XREG(0x1C24) = 0x03;
	XREG(0x1C25) = 0x29;
	XREG(0x1C26) = 0xFA;
	XREG(0x1C27) = 0xB8;
	XREG(0x1C28) = 0x01;
	XREG(0x1C33) = 0x00;
	for (c = 0; c < 6; c++) {
		R(c, hoctl2) = 0x01;
		R(c, hoctl) = 0x03;
		R(c, hoctl) = 0x01;
		R(c, hosta) = 0xFF;
		XREG(c < 4 ? 0x1C40 + c : (c == 4 ? 0x1CAB : 0x1CBB)) = 0x10;
	}
}

static void reset(uint8_t c)
{
	if (R(c, hosta) & 0x01) {
		R(c, hoctl) |= 0x02;		/* kill */
		while (R(c, hosta) & 0x01)
			;
	}
	R(c, hosta) = 0xFF;
	R(c, hoctl) = 0;
	R(c, hoctl2) = 0x01;
}

/* wait for byte done (0) or an error / timeout (-1) */
static int8_t wait_byte(uint8_t c)
{
	uint16_t t = ms_now + 26;
	uint8_t s;

	for (;;) {
		s = R(c, hosta);
		if (s & 0x7C)
			return -1;
		if (s & 0x80)
			return 0;
		if (after(t))
			return -1;
	}
}

/*
 * One I2C transaction: wn bytes written, then (repeated start) rn bytes
 * read; either may be zero.  0 on success, -1 on NACK or timeout.
 */
int8_t i2c_xfer(uint8_t c, uint8_t addr, const __xdata uint8_t *wr, uint8_t wn,
		__xdata uint8_t *rd, uint8_t rn)
{
	uint8_t i;

	reset(c);
	R(c, hoctl2) = 0x13;
	if (wn) {
		R(c, trasla) = addr << 1;
		for (i = 0; i < wn; i++) {
			R(c, hobdb) = wr[i];
			if (i == 0)
				R(c, hoctl) = 0x5C;	/* start, I2C-compatible */
			else
				R(c, hosta) = 0xFC;
			if (wait_byte(c))
				goto fail;
		}
		if (rn)
			R(c, hoctl2) = 0x17;	/* direction switch: repeated start */
	} else {
		R(c, trasla) = (addr << 1) | 1;
		R(c, hoctl) = rn == 1 ? 0x7C : 0x5C;
		if (wait_byte(c))
			goto fail;
	}
	for (i = 0; i < rn; i++) {
		if (i || wn) {
			if (i + 1 == rn)
				R(c, hoctl) |= 0x20;	/* last byte */
			R(c, hosta) = 0xFC;
			if (wn && !i)
				R(c, hoctl2) = 0x13;
			if (wait_byte(c))
				goto fail;
		}
		rd[i] = R(c, hobdb);
	}
	R(c, hoctl2) = 0x11;			/* I2C mode off: STOP */
	R(c, hosta) = 0xFC;
	reset(c);
	return 0;
fail:
	R(c, hoctl2) = 0x11;
	reset(c);
	return -1;
}
