/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * vsnprintf for SCP-firmware's log and string helpers: flags - 0 + space,
 * width and precision (also *), length hh h l ll z j t, conversions
 * d i u x X o p s c %.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "pm.h"

struct out {
	char *buf;
	size_t size, n;
};

static void put(struct out *o, char c)
{
	if (o->n + 1 < o->size)
		o->buf[o->n] = c;
	o->n++;
}

static void pad(struct out *o, char c, int n)
{
	while (n-- > 0)
		put(o, c);
}

static void number(struct out *o, unsigned long long v, bool neg, unsigned base, bool upper,
		   int width, int prec, bool left, bool zero, char sign)
{
	char tmp[24];
	int len = 0, total;
	const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";

	do {
		tmp[len++] = digits[v % base];
		v /= base;
	} while (v);
	if (prec >= 0) {
		zero = false;
		if (prec == 0 && len == 1 && tmp[0] == '0')
			len = 0;
	}
	total = (len > prec ? len : prec) + (neg || sign);
	if (!left && !zero)
		pad(o, ' ', width - total);
	if (neg)
		put(o, '-');
	else if (sign)
		put(o, sign);
	if (!left && zero)
		pad(o, '0', width - total);
	pad(o, '0', prec - len);
	while (len)
		put(o, tmp[--len]);
	if (left)
		pad(o, ' ', width - total);
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
	struct out o = { buf, size, 0 };

	for (; *fmt; fmt++) {
		bool left = false, zero = false;
		char sign = 0;
		int width = 0, prec = -1, lng = 0;

		if (*fmt != '%') {
			put(&o, *fmt);
			continue;
		}
		for (;; fmt++) {
			if (fmt[1] == '-')
				left = true;
			else if (fmt[1] == '0')
				zero = true;
			else if (fmt[1] == '+')
				sign = '+';
			else if (fmt[1] == ' ')
				sign = sign ? sign : ' ';
			else
				break;
		}
		fmt++;
		if (*fmt == '*') {
			width = va_arg(ap, int);
			if (width < 0) {
				left = true;
				width = -width;
			}
			fmt++;
		}
		while (*fmt >= '0' && *fmt <= '9')
			width = width * 10 + *fmt++ - '0';
		if (*fmt == '.') {
			fmt++;
			prec = 0;
			if (*fmt == '*') {
				prec = va_arg(ap, int);
				fmt++;
			}
			while (*fmt >= '0' && *fmt <= '9')
				prec = prec * 10 + *fmt++ - '0';
		}
		for (;; fmt++) {
			if (*fmt == 'l')
				lng++;
			else if (*fmt == 'h')
				lng = lng > 0 ? lng : lng - 1;
			else if (*fmt == 'z' || *fmt == 'j' || *fmt == 't')
				lng = *fmt == 'j' ? 2 : 1;
			else
				break;
		}
		switch (*fmt) {
		case 'd':
		case 'i': {
			long long v = lng >= 2 ? va_arg(ap, long long) :
				      lng == 1 ? va_arg(ap, long) : va_arg(ap, int);

			if (lng == -1)
				v = (short)v;
			else if (lng <= -2)
				v = (signed char)v;
			number(&o, v < 0 ? -(unsigned long long)v : (unsigned long long)v, v < 0, 10,
			       false, width, prec, left, zero, sign);
			break;
		}
		case 'u':
		case 'x':
		case 'X':
		case 'o': {
			unsigned long long v = lng >= 2 ? va_arg(ap, unsigned long long) :
					       lng == 1 ? va_arg(ap, unsigned long) :
					       va_arg(ap, unsigned int);

			if (lng == -1)
				v = (unsigned short)v;
			else if (lng <= -2)
				v = (unsigned char)v;
			number(&o, v, false, *fmt == 'u' ? 10 : *fmt == 'o' ? 8 : 16, *fmt == 'X',
			       width, prec, left, zero, 0);
			break;
		}
		case 'p':
			put(&o, '0');
			put(&o, 'x');
			number(&o, (uintptr_t)va_arg(ap, void *), false, 16, false, 0, -1, false,
			       false, 0);
			break;
		case 's': {
			const char *s = va_arg(ap, const char *);
			int len;

			if (s == NULL)
				s = "(null)";
			len = (int)strnlen(s, prec >= 0 ? (size_t)prec : (size_t)-1);
			if (!left)
				pad(&o, ' ', width - len);
			for (int i = 0; i < len; i++)
				put(&o, s[i]);
			if (left)
				pad(&o, ' ', width - len);
			break;
		}
		case 'c':
			if (!left)
				pad(&o, ' ', width - 1);
			put(&o, (char)va_arg(ap, int));
			if (left)
				pad(&o, ' ', width - 1);
			break;
		case '%':
			put(&o, '%');
			break;
		default:
			put(&o, '%');
			if (*fmt)
				put(&o, *fmt);
			else
				fmt--;
			break;
		}
	}
	if (size)
		buf[o.n < size ? o.n : size - 1] = 0;
	return (int)o.n;
}

int snprintf(char *buf, size_t size, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(buf, size, fmt, ap);
	va_end(ap);
	return n;
}
