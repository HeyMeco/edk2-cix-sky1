/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Toolchain check: code that exercises every core option the compiler can
 * use (multiply, divide, NSA, min/max, sign extension, booleans, loops,
 * the FPU) and some it must not (saturation, 64-bit and double arithmetic
 * from libgcc).  "make check" builds it and xtensa_isa.py checks that
 * every instruction is one CSU_PM's core runs.
 */
#include <stdint.h>

#define R ((volatile uint32_t *)0x50000000)

int t_sat(int x) { return x < -128 ? -128 : x > 127 ? 127 : x; }
uint32_t t_mul(uint32_t a, uint32_t b) { return a * b + a / b + a % b + (uint32_t)(((uint64_t)a * b) >> 32); }
int32_t t_smul(int32_t a, int32_t b)
{
	return a / b + a % b + (int32_t)(((int64_t)a * b) >> 32) + (a < b) + (int16_t)a * (int16_t)b;
}
int t_bits(uint32_t x) { return __builtin_clz(x) + __builtin_ctz(x) + __builtin_popcount(x); }
int t_minmax(int a, int b, unsigned c, unsigned d)
{
	return (a < b ? a : b) + (a > b ? a : b) + (int)(c < d ? c : d) + (int)(c > d ? c : d) + (int8_t)a + (int16_t)b;
}
float t_float(float a, float b, int i, unsigned u)
{
	return a * b + a / b - (float)i + (float)u + (a < b) + (a == b) + (int)a + (unsigned)b + a * b + i;
}
double t_double(double a, double b) { return a * b / a + b; }
uint64_t t_u64(uint64_t a, uint64_t b) { return a / b + a * b + (a << b) + (a >> 3); }
void t_copy(uint32_t *d, const uint32_t *s, int n) { for (int i = 0; i < n; i++) d[i] = s[i] + R[i & 3]; }
float t_sumsq(const float *p, int n) { float s = 0; for (int i = 0; i < n; i++) s += p[i] * p[i]; return s; }
int t_switch(int x)
{
	switch (x) {
	case 0: return 3; case 1: return 7; case 2: return 9; case 3: return 1;
	case 4: return 22; case 5: return 8; default: return -1;
	}
}
