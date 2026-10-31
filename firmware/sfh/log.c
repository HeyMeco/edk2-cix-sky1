/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Log output on a PL011.  As in the CIX image it is off from reset (the
 * enable flag and UART base start at zero and nothing sets them), so the
 * firmware runs silently unless SFH_LOG_UART is defined at build time.
 */
#include <stdarg.h>
#include "sfh.h"

struct sfh_log sfh_log;
volatile uint32_t sfh_state;

#ifdef SFH_LOG_UART
static uint32_t uart_base = SFH_LOG_UART;
#else
static uint32_t uart_base;
#endif

static void uart_putc(char ch)
{
	int n;

	if (sfh_state == 1 || !uart_base)
		return;
	for (n = 0; n <= 10000 && (REG32(uart_base + 0x18) & 0x20); n++)
		;
	if (n <= 10000)
		REG32(uart_base) = (uint8_t)ch;
}

static void out(char ch)
{
	if (ch == '\n')
		uart_putc('\r');
	uart_putc(ch);
}

static void out_num(unsigned long long v, unsigned int base, int width, char pad, int neg)
{
	char buf[24];
	int n = 0;

	do {
		buf[n++] = "0123456789abcdef"[v % base];
		v /= base;
	} while (v);
	if (neg)
		buf[n++] = '-';
	while (n < width--)
		out(pad);
	while (n)
		out(buf[--n]);
}

void log_set_level(uint32_t level)
{
	if (sfh_log.enable && level < 5)
		sfh_log.level = level;
}

int log_printf(uint32_t level, const char *fmt, ...)
{
	va_list ap;

	if (sfh_log.enable != 1 || level > sfh_log.level)
		return 0;
	REG32(SFH_LOG_CTRL) = 0;
	va_start(ap, fmt);
	for (; *fmt; fmt++) {
		int width = 0, lng = 0;
		char pad = ' ';

		if (*fmt != '%') {
			out(*fmt);
			continue;
		}
		fmt++;
		if (*fmt == '0')
			pad = '0';
		while (*fmt >= '0' && *fmt <= '9')
			width = width * 10 + (*fmt++ - '0');
		while (*fmt == 'l') {
			lng++;
			fmt++;
		}
		switch (*fmt) {
		case 'd':
		case 'i': {
			long long v = lng > 1 ? va_arg(ap, long long) : va_arg(ap, int);

			out_num(v < 0 ? -v : v, 10, width, pad, v < 0);
			break;
		}
		case 'u':
			out_num(lng > 1 ? va_arg(ap, unsigned long long) : va_arg(ap, unsigned int), 10, width, pad, 0);
			break;
		case 'x':
		case 'X':
			out_num(lng > 1 ? va_arg(ap, unsigned long long) : va_arg(ap, unsigned int), 16, width, pad, 0);
			break;
		case 'p':
			out('0');
			out('x');
			out_num((uintptr_t)va_arg(ap, void *), 16, width, pad, 0);
			break;
		case 'c':
			out((char)va_arg(ap, int));
			break;
		case 's':
			for (const char *s = va_arg(ap, const char *); s && *s; s++)
				out(*s);
			break;
		case '%':
			out('%');
			break;
		default:
			return 0;
		}
	}
	va_end(ap);
	return 0;
}
