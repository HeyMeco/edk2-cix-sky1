/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* The C library pieces the firmware uses */
#include "pm.h"

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

	for (; n; n--, p++, q++) {
		if (*p != *q)
			return *p - *q;
	}
	return 0;
}

size_t strlen(const char *s)
{
	size_t n = 0;

	while (s[n])
		n++;
	return n;
}
