/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Orion O6 EC (ITE IT5570E) firmware: start-up, the 1 ms tick, the
 * interrupt dispatch and the main loop.
 *
 * Behaviour follows Radxa's EC V1.07 (CIX's EC code base) as observed in
 * scripts/open/ec_emu.py; only what the O6 uses is here: power sequencing,
 * the power button, the host interface (SMBus B slave, CIX's CrOS-style
 * host commands), the fan, the PD controllers' set-up and the power LED.
 */
#include "ec.h"

/* the vectors and ITE's e-flash signature are in vectors.s */

volatile uint16_t ms_now;
static volatile uint8_t ticks;

/* Timer 0, mode 1, reloaded to 0xFD00: 768 counts of 9.2 MHz / 12 = 1 ms */
void timer0_isr(void) __interrupt __using(1)
{
	TL0 = 0x00;
	TH0 = 0xFD;
	ms_now++;
	ticks++;
}

/* INT1: the IT5570 interrupt controller; IVECT = 0x10 + source */
void intc_isr(void) __interrupt
{
	uint8_t v = IVECT;

	if (v == 0x10 + 10) {		/* SMBus B: the host interface */
		ISR(1) = 0x04;
		host_isr();
	}
}

void delay_ms(uint16_t n)
{
	uint16_t t = ms_now + n;

	while (!after(t))
		;
}

static void timer_init(void)
{
	TR0 = 0;
	TF0 = 0;
	TL0 = 0x00;
	TH0 = 0xFD;
	TMOD = 0x11;
	TR0 = 1;
	ET0 = 1;
}

/* the chip set-up the vendor firmware does before its own init */
static void chip_init(void)
{
	XREG(0x1001) = 0x3F;
	XREG(0x1005) |= 0x80;
	RSTS = (RSTS & 0xFC) | 0x03;
	XREG(0x1D00) = 0;
	XREG(0x1D01) = 0;
	XREG(0x1D03) = 0;
	XREG(0x1007) = 0x1F;
	XREG(0x200D) = 0x43;
	XREG(0x200A) = 0x02;
	XREG(0x1408) = 0x4E;
	XREG(0x140A) = 0x00;
	RSTS = 0x84;
	XREG(0x1302) = 0x00;
	XREG(0x1300) = 0x48;
	XREG(0x1506) = 0x41;
	XREG(0x1516) = 0x41;
	XREG(0x1700) = 0x11;
	XREG(0x1701) = 0x11;
	XREG(0x1702) = 0x11;
	XREG(0x1D02) = 0x05;
	XREG(0x1D05) = 0x04;
	XREG(0x1E04) = 0x00;
	XREG(0x2010) = 0x1F;
	XREG(0x2011) = 0x06;
}

void main(void)
{
	/* the vendor's hooks: 100 ms (at x90 ms), 2 s (2.3 s, 4.3 s, ...) */
	uint16_t next100 = 250, next2s = 2300;

	P1 = (P1 & 0xF8) | 0x01;	/* code bank 1, as the vendor image runs */
	chip_init();
	gpio_init();
	uart_init();
	timer_init();
	smbus_init();
	fan_init();
	host_init();
	EX1 = 1;
	EA = 1;

	puts_("\r\nOrion O6 EC " VERSION_STRING "\r\n");
	power_init();

	for (;;) {
		if (!ticks) {
			PCON = 0x01;		/* idle until the next interrupt */
			continue;
		}
		EA = 0;
		ticks--;
		EA = 1;

		power_1ms();
		host_poll();
		led_1ms();
		if (after(next100)) {
			next100 += 100;
			host_poll_100ms();
		}
		if (after(next2s)) {
			next2s += 2000;
			power_2s();
			fan_poll_2s();
		}
	}
}
