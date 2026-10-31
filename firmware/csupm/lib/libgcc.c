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
