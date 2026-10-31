/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* PL011 debug UART */
#include "lcru.h"
#include "uart.h"

#define DR		0x00
#define RSR		0x04
#define FR		0x18
#define IBRD		0x24
#define FBRD		0x28
#define LCR_H		0x2c
#define CR		0x30
#define FR_TXFF		BIT(5)

void uart_init(void)
{
	uint32_t div = UART_CLK_HZ * 4 / UART_BAUD;	/* 64ths of the baud divisor */

	lcru_reset(RESET_UART, false);
	lcru_reset(RESET_UART, true);
	REG16(UART_BASE + IBRD) = div >> 6;
	REG32(UART_BASE + FBRD) = div & 63;
	REG8(UART_BASE + RSR) = 0xff;			/* clear errors */
	REG16(UART_BASE + LCR_H) = 0x70;		/* 8N1, FIFOs */
	REG16(UART_BASE + CR) = 0x301;			/* UART, TX and RX on */
}

void uart_putc(char c)
{
	while (REG32(UART_BASE + FR) & FR_TXFF)
		;
	REG32(UART_BASE + DR) = (uint8_t)c;
}

void uart_puts(const char *s)
{
	for (; *s; s++) {
		if (*s == '\n')
			uart_putc('\r');
		uart_putc(*s);
	}
}
