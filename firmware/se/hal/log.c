/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Console and RAM log (CIX hal/common/hal_log.c, rlog.c).
 *
 * Messages at or below the current level (SE_SYSCTRL_LOG_LEVEL, kept in a
 * scratch register so it survives a firmware restart) go to the UART and,
 * once rlog_init() has given it a buffer, to the RAM log: a ring of
 * {"level":..,"time":..,"log":..}; records that the AP side can read.
 */
#include "se.h"
#include "drivers.h"
#include "rlog.h"

typedef void (*putc_fn)(char c, void *arg);

static void num_print(putc_fn put, void *arg, uint64_t n, unsigned int base, int upper,
		      char pad, int width, char sign)
{
	char buf[24];
	int len = 0;

	do {
		unsigned int d = n % base;

		buf[len++] = d < 10 ? '0' + d : (upper ? 'A' : 'a') + d - 10;
		n /= base;
	} while (n);
	if (sign && pad == '0')
		put(sign, arg);
	for (width -= len + (sign != 0); width > 0; width--)
		put(pad, arg);
	if (sign && pad != '0')
		put(sign, arg);
	while (len)
		put(buf[--len], arg);
}

/* %[0][-][width][l|ll|z](d|i|u|x|X|p|s|c|%) */
static void format(putc_fn put, void *arg, const char *fmt, va_list ap)
{
	for (; *fmt; fmt++) {
		char pad = ' ';
		int width = 0, lng = 0;
		uint64_t n;
		const char *s;

		if (*fmt != '%') {
			put(*fmt, arg);
			continue;
		}
		fmt++;
		while (*fmt == '0' || *fmt == '-' || *fmt == '#') {
			if (*fmt == '0')
				pad = '0';
			fmt++;
		}
		while (*fmt >= '0' && *fmt <= '9')
			width = width * 10 + *fmt++ - '0';
		for (; *fmt == 'l' || *fmt == 'z'; fmt++)
			lng += *fmt == 'l';
		switch (*fmt) {
		case 'd':
		case 'i': {
			int64_t v = lng >= 2 ? va_arg(ap, int64_t) : va_arg(ap, int32_t);

			num_print(put, arg, v < 0 ? -(uint64_t)v : (uint64_t)v, 10, 0, pad, width,
				  v < 0 ? '-' : 0);
			break;
		}
		case 'u':
		case 'x':
		case 'X':
			n = lng >= 2 ? va_arg(ap, uint64_t) : va_arg(ap, uint32_t);
			num_print(put, arg, n, *fmt == 'u' ? 10 : 16, *fmt == 'X', pad, width, 0);
			break;
		case 'p':
			n = (uintptr_t)va_arg(ap, void *);
			if (n) {
				put('0', arg);
				put('x', arg);
				width -= 2;
			}
			num_print(put, arg, n, 16, 0, pad, width, 0);
			break;
		case 's':
			for (s = va_arg(ap, const char *); s && *s; s++)
				put(*s, arg);
			break;
		case 'c':
			put((char)va_arg(ap, int), arg);
			break;
		case '%':
			put('%', arg);
			break;
		default:
			return;
		}
	}
}

static void uart_putc(char c, void *arg)
{
	if (c == '\n')
		drv_pl011_putchar('\r');
	drv_pl011_putchar(c);
	++*(int *)arg;
}

/* RAM log ------------------------------------------------------------------ */

static struct {
	char *buf;
	uint32_t pos;
	uint32_t size;
	uint8_t enable;
	const ramlog_ops *ops;
	uint32_t tick;
} g_rlog;

static void rlog_putc(char c, void *arg)
{
	g_rlog.buf[g_rlog.pos++] = c;
	if (g_rlog.pos >= g_rlog.size)
		g_rlog.pos = 0;
}

static void rlog_puts(const char *s)
{
	while (*s)
		rlog_putc(*s++, NULL);
}

void rlog_set_enable_disable(uint8_t enable)
{
	g_rlog.enable = enable;
}

uint8_t is_rlog_enable(void)
{
	return g_rlog.enable;
}

/*
 * The buffer's second half keeps the previous boot's log; the first half
 * (less 100 bytes of slack) is the ring for this one.
 */
void rlog_init(char *buf, uint32_t size, const ramlog_ops *ops)
{
	uint32_t half = size / 2;

	if (buf == NULL || ops == NULL || half < 100)
		return;
	g_rlog.ops = ops;
	memcpy(buf + half, buf, half);
	memset(buf, 0, half);
	g_rlog.buf = buf;
	g_rlog.size = half - 100;
	g_rlog.pos = 0;
	g_rlog.enable = 1;
}

static void rlog_printf(const char *fmt, va_list ap)
{
	static const char hex[] = "0123456789abcdef";
	uint32_t t;
	int i;

	if (!g_rlog.enable)
		return;
	if (g_rlog.ops->lock)
		g_rlog.ops->lock();
	rlog_puts("{\"level\":\"INFO\",");
	g_rlog.tick = g_rlog.ops->get_tick ? g_rlog.ops->get_tick() : g_rlog.tick + 1;
	rlog_puts("\"time\":\"");
	t = g_rlog.tick;
	for (i = 28; i > 0 && !(t >> i); i -= 4)
		;
	for (; i >= 0; i -= 4)
		rlog_putc(hex[(t >> i) & 0xf], NULL);
	rlog_puts("\",\"log\":\"");
	format(rlog_putc, NULL, fmt, ap);
	rlog_putc('}', NULL);
	rlog_putc(';', NULL);
	if (g_rlog.ops->unlock)
		g_rlog.ops->unlock();
}

/* Console ------------------------------------------------------------------ */

static uint32_t vlog(const char *fmt, va_list ap)
{
	int n = 0;
	va_list ap2;

	va_copy(ap2, ap);
	format(uart_putc, &n, fmt, ap);
	rlog_printf(fmt, ap2);
	va_end(ap2);
	return n;
}

uint32_t hal_printf(const char *fmt, ...)
{
	va_list ap;
	uint32_t n;

	va_start(ap, fmt);
	n = vlog(fmt, ap);
	va_end(ap);
	return n;
}

uint32_t hal_printf_level(uint32_t level, const char *fmt, ...)
{
	va_list ap;
	uint32_t n;

	if (level > REG32(SE_SYSCTRL_LOG_LEVEL))
		return 0;
	va_start(ap, fmt);
	n = vlog(fmt, ap);
	va_end(ap);
	return n;
}

void hal_log_set_level(uint32_t level)
{
	REG32(SE_SYSCTRL_LOG_LEVEL) = level;
}

void hexdump(const char *tag, const uint8_t *buf, uint32_t size)
{
	uint32_t i, j;

	hal_printf_level(LOG_INFO, "0x%x:\n", (unsigned)(uintptr_t)buf);
	for (i = 0; i < size; i += 16) {
		hal_printf_level(LOG_INFO, "%s %08d:", tag, (int)i);
		for (j = i; j < size && j < i + 16; j++)
			hal_printf_level(LOG_INFO, " 0x%02X,", buf[j]);
		hal_printf_level(LOG_INFO, "\n");
	}
}

/* Postcodes ----------------------------------------------------------------- */

void hal_pc_trace(uint32_t code)
{
	if (REG32(SE_SYSCTRL_PC_LOCK))
		return;
	drv_pc_trace(code);
	hal_printf_level(LOG_INFO, "PCODE: 0x%x\n", (unsigned)(code & 0xffff));
}

/* First error only: 0x9000 | module (err[15:9]) << 7 | line[6:0]; then freeze */
void hal_pc_log_ec(uint32_t err, uint32_t line)
{
	uint32_t code;

	if (REG32(SE_SYSCTRL_PC_LOCK))
		return;
	code = (((err & 0xff00) - 0x9000) >> 1 & 0xf80) | (line & 0x7f) | 0x9000;
	drv_pc_trace(code);
	hal_printf_level(LOG_INFO, "PCODE: 0x%x\n", (unsigned)code);
	REG32(SE_SYSCTRL_PC_LOCK) = 1;
}
