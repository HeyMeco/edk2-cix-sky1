/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* The C library pieces the firmware uses */
#include "se.h"

void *memcpy(void *dst, const void *src, size_t n)
{
	uint8_t *d = dst;
	const uint8_t *s = src;

	if ((((uintptr_t)d | (uintptr_t)s) & 3) == 0) {
		for (; n >= 4; n -= 4, d += 4, s += 4)
			*(uint32_t *)d = *(const uint32_t *)s;
	}
	while (n--)
		*d++ = *s++;
	return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
	uint8_t *d = dst;
	const uint8_t *s = src;

	if (d <= s || d >= s + n)
		return memcpy(dst, src, n);
	while (n--)
		d[n] = s[n];
	return dst;
}

void *memset(void *dst, int c, size_t n)
{
	uint8_t *d = dst;

	while (n--)
		*d++ = (uint8_t)c;
	return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
	const uint8_t *p = a, *q = b;

	for (; n; n--, p++, q++)
		if (*p != *q)
			return *p - *q;
	return 0;
}

size_t strlen(const char *s)
{
	size_t n = 0;

	while (s[n])
		n++;
	return n;
}

int strcmp(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return (uint8_t)*a - (uint8_t)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
	for (; n; n--, a++, b++) {
		if (*a != *b)
			return (uint8_t)*a - (uint8_t)*b;
		if (!*a)
			break;
	}
	return 0;
}

static int lower(int c)
{
	return c >= 'A' && c <= 'Z' ? c + 'a' - 'A' : c;
}

int strcasecmp(const char *a, const char *b)
{
	while (*a && lower(*a) == lower(*b))
		a++, b++;
	return lower((uint8_t)*a) - lower((uint8_t)*b);
}

char *strcpy(char *dst, const char *src)
{
	char *d = dst;

	while ((*d++ = *src++))
		;
	return dst;
}

unsigned long strtoul(const char *s, char **end, int base)
{
	unsigned long v = 0;

	while (*s == ' ' || *s == '\t')
		s++;
	if ((base == 0 || base == 16) && s[0] == '0' && lower(s[1]) == 'x') {
		s += 2;
		base = 16;
	} else if (base == 0) {
		base = s[0] == '0' ? 8 : 10;
	}
	for (;; s++) {
		int c = lower(*s), d;

		if (c >= '0' && c <= '9')
			d = c - '0';
		else if (c >= 'a' && c <= 'z')
			d = c - 'a' + 10;
		else
			break;
		if (d >= base)
			break;
		v = v * base + d;
	}
	if (end)
		*end = (char *)s;
	return v;
}
