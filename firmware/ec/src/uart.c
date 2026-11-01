/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* UART1 (16550 at 0x2700): the debug console, 115200 8N1 as the vendor's */
#include "ec.h"

void uart_init(void)
{
	UART_LCR = 0x80;
	UART_2708 = 0x02;
	UART_DLL = 0x01;
	UART_DLM = 0x00;
	UART_LCR = 0x03;
	UART_MCR = 0x08;
	UART_FCR = 0xC7;
	UART_IER = 0x00;
}

void putch(char c)
{
	while (!(UART_LSR & 0x20))
		;
	UART_THR = c;
}

void puts_(const char *s)
{
	while (*s)
		putch(*s++);
}

static __code const char hexd[] = "0123456789abcdef";

void puthex(uint8_t v)
{
	putch(hexd[v >> 4]);
	putch(hexd[v & 15]);
}

void putdec(uint16_t v)
{
	char b[6];
	uint8_t n = 0;

	do {
		b[n++] = '0' + v % 10;
		v /= 10;
	} while (v);
	while (n)
		putch(b[--n]);
}
