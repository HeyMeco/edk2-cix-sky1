/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * 64-bit division helpers GCC calls (the toolchain's libgcc is built for
 * another endianness than the core's configuration).  Shift and subtract:
 * the firmware divides 64-bit numbers rarely (time conversions, printf).
 */
#include <stdint.h>

static uint64_t udivmod(uint64_t n, uint64_t d, uint64_t *rem)
{
	uint64_t q = 0, r = 0;

	if (d == 0) {
		__builtin_trap();
	}
	if ((n >> 32) == 0 && (d >> 32) == 0) {
		if (rem)
			*rem = (uint32_t)n % (uint32_t)d;
		return (uint32_t)n / (uint32_t)d;
	}
	/* constant shifts only: a variable 64-bit shift is another libgcc call */
	for (int i = 0; i < 64; i++) {
		r = r << 1 | n >> 63;
		n <<= 1;
		q <<= 1;
		if (r >= d) {
			r -= d;
			q |= 1;
		}
	}
	if (rem)
		*rem = r;
	return q;
}

uint64_t __ashldi3(uint64_t v, int n);
uint64_t __lshrdi3(uint64_t v, int n);
int64_t __ashrdi3(int64_t v, int n);
uint64_t __muldi3(uint64_t a, uint64_t b);

/* 64-bit shifts by a variable count, from 32-bit ones */
uint64_t __ashldi3(uint64_t v, int n)
{
	uint32_t lo = (uint32_t)v, hi = (uint32_t)(v >> 32);

	if (n == 0)
		return v;
	if (n >= 32)
		return (uint64_t)(lo << (n - 32)) << 32;
	return (uint64_t)(hi << n | lo >> (32 - n)) << 32 | (uint32_t)(lo << n);
}

uint64_t __lshrdi3(uint64_t v, int n)
{
	uint32_t lo = (uint32_t)v, hi = (uint32_t)(v >> 32);

	if (n == 0)
		return v;
	if (n >= 32)
		return hi >> (n - 32);
	return (uint64_t)(hi >> n) << 32 | (lo >> n | hi << (32 - n));
}

int64_t __ashrdi3(int64_t v, int n)
{
	int32_t hi = (int32_t)((uint64_t)v >> 32);
	uint32_t lo = (uint32_t)v;

	if (n == 0)
		return v;
	if (n >= 32)
		return (int64_t)(hi >> (n - 32 > 31 ? 31 : n - 32));
	return (int64_t)((uint64_t)(uint32_t)(hi >> n) << 32 | (lo >> n | (uint32_t)hi << (32 - n)));
}

/* 64 x 64 -> 64 from 32 x 32 -> 64 partial products */
static uint64_t mul32x32(uint32_t a, uint32_t b)
{
	uint32_t al = a & 0xffff, ah = a >> 16, bl = b & 0xffff, bh = b >> 16;
	uint32_t ll = al * bl, lh = al * bh, hl = ah * bl, hh = ah * bh;
	uint32_t mid = (ll >> 16) + (lh & 0xffff) + (hl & 0xffff);

	return (uint64_t)(hh + (lh >> 16) + (hl >> 16) + (mid >> 16)) << 32 |
	       (uint32_t)((mid << 16) | (ll & 0xffff));
}

uint64_t __muldi3(uint64_t a, uint64_t b)
{
	uint32_t al = (uint32_t)a, ah = (uint32_t)(a >> 32), bl = (uint32_t)b, bh = (uint32_t)(b >> 32);

	return mul32x32(al, bl) + ((uint64_t)(al * bh + ah * bl) << 32);
}

uint64_t __udivdi3(uint64_t n, uint64_t d);
uint64_t __umoddi3(uint64_t n, uint64_t d);
int64_t __divdi3(int64_t n, int64_t d);
int64_t __moddi3(int64_t n, int64_t d);

uint64_t __udivdi3(uint64_t n, uint64_t d)
{
	return udivmod(n, d, 0);
}

uint64_t __umoddi3(uint64_t n, uint64_t d)
{
	uint64_t r;

	udivmod(n, d, &r);
	return r;
}

int64_t __divdi3(int64_t n, int64_t d)
{
	uint64_t q = udivmod(n < 0 ? -(uint64_t)n : (uint64_t)n,
			     d < 0 ? -(uint64_t)d : (uint64_t)d, 0);

	return (n < 0) != (d < 0) ? -(int64_t)q : (int64_t)q;
}

int64_t __moddi3(int64_t n, int64_t d)
{
	uint64_t r;

	udivmod(n < 0 ? -(uint64_t)n : (uint64_t)n, d < 0 ? -(uint64_t)d : (uint64_t)d, &r);
	return n < 0 ? -(int64_t)r : (int64_t)r;
}
