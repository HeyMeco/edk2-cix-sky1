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

char *strchr(const char *s, int c)
{
	for (;; s++) {
		if (*s == (char)c)
			return (char *)s;
		if (!*s)
			return NULL;
	}
}

size_t strnlen(const char *s, size_t max)
{
	size_t n = 0;

	while (n < max && s[n])
		n++;
	return n;
}

char *strncpy(char *dst, const char *src, size_t n)
{
	size_t i = 0;

	for (; i < n && src[i]; i++)
		dst[i] = src[i];
	for (; i < n; i++)
		dst[i] = 0;
	return dst;
}

int strncmp(const char *a, const char *b, size_t n)
{
	for (; n; n--, a++, b++) {
		if (*a != *b || !*a)
			return (unsigned char)*a - (unsigned char)*b;
	}
	return 0;
}

int strcmp(const char *a, const char *b)
{
	return strncmp(a, b, (size_t)-1);
}
