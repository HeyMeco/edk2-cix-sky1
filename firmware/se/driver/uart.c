/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* SE debug UART, an Arm PL011 (CIX driver/uart/drv_uart.c) */
#include "se.h"
#include "drivers.h"
#include "platform.h"

#define UART_DR		(SE_UART_BASE + 0x00)
#define UART_FR		(SE_UART_BASE + 0x18)
#define UART_IBRD	(SE_UART_BASE + 0x24)
#define UART_FBRD	(SE_UART_BASE + 0x28)
#define UART_LCR_H	(SE_UART_BASE + 0x2c)
#define UART_CR		(SE_UART_BASE + 0x30)
#define UART_IMSC	(SE_UART_BASE + 0x38)
#define FR_RXFE		BIT(4)
#define FR_TXFF		BIT(5)

#define UART_TIMEOUT	10000

/* input_clock in Hz */
uint32_t drv_uart_init(uint32_t baud_rate, uint32_t input_clock)
{
	uint32_t div = (input_clock * 4 + baud_rate / 2) / baud_rate;	/* 64ths */

	REG32(UART_CR) = 0;
	REG32(UART_IBRD) = div >> 6;
	REG32(UART_FBRD) = div & 0x3f;
	REG32(UART_LCR_H) = 0x70;		/* 8N1, FIFOs */
	REG32(UART_IMSC) = 0x50;		/* RX and RX timeout */
	REG32(UART_CR) = 0x301;			/* UART, TX, RX enable */
	return 0;
}

uint32_t drv_pl011_putchar(char ch)
{
	int i;

	for (i = 0; i <= UART_TIMEOUT; i++) {
		if (!(REG32(UART_FR) & FR_TXFF)) {
			REG32(UART_DR) = ch;
			return 0;
		}
	}
	return 0x9101;
}

/*
 * Wait for a character; tmr_out polls, or ~0 for no limit.  While waiting
 * without a limit, keep the crash watchdog fed and give up with a newline
 * once the SE is asked to idle.
 */
void hal_crash_feed_dog(void);

static uint32_t rc_wdt_wait;

static char _drv_pl011_getchar(uint32_t tmr_out)
{
	uint32_t left = tmr_out;

	for (;;) {
		if (left == 0 && tmr_out != 0xffffffff)
			return -1;
		if (left == 0 || tmr_out == 0xffffffff) {
			if (rc_wdt_wait == 0) {
				hal_crash_feed_dog();
				rc_wdt_wait = plat_get_core_freq() * 2000;
			}
			rc_wdt_wait--;
			if (tmr_out == 0xffffffff && REG32(SE_SYSCTRL_IDLE) != 0) {
				rc_wdt_wait = 0;
				return '\n';
			}
		} else {
			left--;
		}
		if (!(REG32(UART_FR) & FR_RXFE)) {
			rc_wdt_wait = 0;
			return (char)REG32(UART_DR);
		}
	}
}

char drv_pl011_getchar(void)
{
	return _drv_pl011_getchar(0xffffffff);
}

int8_t drv_pl011_getchar_poll(uint32_t wait)
{
	return _drv_pl011_getchar(wait);
}
