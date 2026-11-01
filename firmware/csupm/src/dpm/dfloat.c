/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Double precision in software, for the DPM's power model.
 *
 * CIX's compiler knows the core's double-precision FPU (64-bit FP
 * registers: ADD.D, MUL.D, CVTD.S, ...; its __divdf3 uses the divide
 * steps) and CIX's power model computes parts of its expressions in double
 * precision: the leakage at start-up (pwr_leakage_init), temperatures
 * normalised to 100 K, polynomial terms of the SoC's and NPU's power.  GCC's
 * description of the core has single precision only, so GCC calls these
 * routines for 'double'.  They are IEEE 754 binary64 with round to nearest
 * even, as the hardware's ADD.D, MUL.D and CIX's __divdf3 are, so the DPM
 * computes the same bits as CIX's firmware.  Conversions to integers
 * truncate and saturate as the core's TRUNC.D does.
 *
 * Also the 64-bit integer to single conversion GCC calls (__floatundisf).
 *
 * The algorithms are the usual ones (operands unpacked to a significand
 * with three extra bits for rounding); no double arithmetic happens here.
 */
#include <stdbool.h>
#include <stdint.h>

typedef union {
	double d;
	uint64_t u;
} dbits;

typedef union {
	float f;
	uint32_t u;
} fbits;

#define SIGN		(1ull << 63)
#define FRAC		((1ull << 52) - 1)
#define IMPLICIT	(1ull << 52)
#define INF		0x7ff0000000000000ull
#define QNAN		0x7ff8000000000000ull
#define EXP(u)		((int)(((u) >> 52) & 0x7ff))

double __adddf3(double a, double b);
double __subdf3(double a, double b);
double __muldf3(double a, double b);
double __divdf3(double a, double b);
double __negdf2(double a);
double __extendsfdf2(float a);
float __truncdfsf2(double a);
double __floatsidf(int32_t i);
double __floatunsidf(uint32_t i);
int32_t __fixdfsi(double a);
uint32_t __fixunsdfsi(double a);
int __eqdf2(double a, double b);
int __nedf2(double a, double b);
int __ltdf2(double a, double b);
int __ledf2(double a, double b);
int __gtdf2(double a, double b);
int __gedf2(double a, double b);
int __unorddf2(double a, double b);
float __floatundisf(uint64_t v);

static inline double from_bits(uint64_t u)
{
	dbits x = { .u = u };

	return x.d;
}

static inline uint64_t to_bits(double d)
{
	dbits x = { .d = d };

	return x.u;
}

static int clz64(uint64_t v)
{
	int n = 0;

	if (!v)
		return 64;
	while (!(v & SIGN)) {
		v <<= 1;
		n++;
	}
	return n;
}

/* A subnormal's significand normalised (implicit bit at 52); its exponent */
static int normalize(uint64_t *sig)
{
	int shift = clz64(*sig) - clz64(IMPLICIT);

	*sig <<= shift;
	return 1 - shift;
}

/* v >> n, the bits shifted out ORed into bit 0 */
static uint64_t shr_sticky(uint64_t v, int n)
{
	if (n <= 0)
		return v;
	if (n >= 64)
		return v != 0;
	return v >> n | ((v << (64 - n)) != 0);
}

/*
 * Pack a result: sig has the implicit bit at 55 and guard, round and
 * sticky bits below bit 3; exp is biased and may be out of range.
 */
static uint64_t round_pack(uint64_t sign, int exp, uint64_t sig)
{
	uint64_t r;
	unsigned int grs;

	if (exp >= 0x7ff)
		return sign | INF;
	if (exp <= 0) {
		sig = shr_sticky(sig, 1 - exp);
		exp = 0;
	}
	grs = sig & 7;
	r = sign | (uint64_t)exp << 52 | ((sig >> 3) & FRAC);
	if (grs > 4 || (grs == 4 && (r & 1)))
		r++;		/* may carry into the exponent, to infinity */
	return r;
}

double __adddf3(double a, double b)
{
	uint64_t ar = to_bits(a), br = to_bits(b);
	uint64_t aa = ar & ~SIGN, ba = br & ~SIGN, asig, bsig, sign, t;
	int aexp, bexp, d;

	if (aa - 1 >= INF - 1 || ba - 1 >= INF - 1) {
		if (aa > INF)
			return from_bits(ar | (1ull << 51));
		if (ba > INF)
			return from_bits(br | (1ull << 51));
		if (aa == INF)
			return (ar ^ br) == SIGN ? from_bits(QNAN) : a;
		if (ba == INF)
			return b;
		if (!aa)
			return ba ? b : from_bits(ar & br);
		if (!ba)
			return a;
	}
	if (ba > aa) {
		t = ar;
		ar = br;
		br = t;
		t = aa;
		aa = ba;
		ba = t;
	}
	aexp = EXP(ar);
	bexp = EXP(br);
	asig = ar & FRAC;
	bsig = br & FRAC;
	if (!aexp)
		aexp = normalize(&asig);
	if (!bexp)
		bexp = normalize(&bsig);
	sign = ar & SIGN;
	asig = (asig | IMPLICIT) << 3;
	bsig = (bsig | IMPLICIT) << 3;
	d = aexp - bexp;
	bsig = shr_sticky(bsig, d);
	if ((ar ^ br) & SIGN) {
		asig -= bsig;
		if (!asig)
			return from_bits(0);
		if (asig < IMPLICIT << 3) {
			int shift = clz64(asig) - clz64(IMPLICIT << 3);

			asig <<= shift;
			aexp -= shift;
		}
	} else {
		asig += bsig;
		if (asig & IMPLICIT << 4) {
			asig = asig >> 1 | (asig & 1);
			aexp++;
		}
	}
	return from_bits(round_pack(sign, aexp, asig));
}

double __subdf3(double a, double b)
{
	return __adddf3(a, from_bits(to_bits(b) ^ SIGN));
}

double __negdf2(double a)
{
	return from_bits(to_bits(a) ^ SIGN);
}

/* NaN, infinity and zero operands of a product or quotient: true with
 * *res set when the result is known without computing */
static bool special(uint64_t ar, uint64_t br, bool div, uint64_t *res)
{
	uint64_t aa = ar & ~SIGN, ba = br & ~SIGN, sign = (ar ^ br) & SIGN;

	if (aa > INF || ba > INF) {
		*res = (aa > INF ? ar : br) | (1ull << 51);
		return true;
	}
	if (aa == INF) {
		*res = (div ? ba == INF : !ba) ? QNAN : sign | INF;
		return true;
	}
	if (ba == INF) {
		*res = div ? sign : (aa ? sign | INF : QNAN);
		return true;
	}
	if (!aa) {
		*res = (div && !ba) ? QNAN : sign;
		return true;
	}
	if (!ba) {
		*res = div ? sign | INF : sign;
		return true;
	}
	return false;
}

/* The significand with its implicit bit (at 52) and the exponent */
static int unpack(uint64_t r, uint64_t *sig)
{
	int exp = EXP(r);

	*sig = r & FRAC;
	if (!exp)
		return normalize(sig);
	*sig |= IMPLICIT;
	return exp;
}

double __muldf3(double a, double b)
{
	uint64_t ar = to_bits(a), br = to_bits(b), res, sign = (ar ^ br) & SIGN;
	uint64_t asig, bsig, lo, hi, mid1, mid2, p;
	uint32_t a0, a1, b0, b1;
	int exp;

	if (special(ar, br, false, &res))
		return from_bits(res);
	exp = unpack(ar, &asig) + unpack(br, &bsig) - 1023;
	/* the 106-bit product, from 32-bit halves */
	a0 = (uint32_t)asig;
	a1 = (uint32_t)(asig >> 32);
	b0 = (uint32_t)bsig;
	b1 = (uint32_t)(bsig >> 32);
	lo = (uint64_t)a0 * b0;
	mid1 = (uint64_t)a1 * b0;
	mid2 = (uint64_t)a0 * b1;
	hi = (uint64_t)a1 * b1;
	p = (lo >> 32) + (uint32_t)mid1 + (uint32_t)mid2;
	lo = (uint32_t)lo | p << 32;
	hi += (mid1 >> 32) + (mid2 >> 32) + (p >> 32);
	/* the product is in [2^104, 2^106): bring the implicit bit to 55 */
	if (hi & (1ull << 41)) {
		exp++;
		p = hi << 14 | lo >> 50 | ((lo << 14) != 0);
	} else {
		p = hi << 15 | lo >> 49 | ((lo << 15) != 0);
	}
	return from_bits(round_pack(sign, exp, p));
}

double __divdf3(double a, double b)
{
	uint64_t ar = to_bits(a), br = to_bits(b), res, sign = (ar ^ br) & SIGN;
	uint64_t r, bsig, q = 0;
	int exp;

	if (special(ar, br, true, &res))
		return from_bits(res);
	exp = unpack(ar, &r) - unpack(br, &bsig) + 1023;
	if (r < bsig) {
		r <<= 1;
		exp--;
	}
	/* 55 quotient bits by long division, the remainder as sticky */
	for (int i = 0; i < 55; i++) {
		q <<= 1;
		if (r >= bsig) {
			r -= bsig;
			q |= 1;
		}
		r <<= 1;
	}
	return from_bits(round_pack(sign, exp, q << 1 | (r != 0)));
}

double __extendsfdf2(float a)
{
	fbits x = { .f = a };
	uint64_t sign = (uint64_t)(x.u >> 31) << 63, sig = x.u & 0x7fffff;
	int exp = (x.u >> 23) & 0xff;

	if (exp == 0xff)
		return from_bits(sign | INF | sig << 29 | (sig ? 1ull << 51 : 0));
	if (!exp) {
		int k = 22;

		if (!sig)
			return from_bits(sign);
		while (!(sig & (1ull << k)))
			k--;
		/* sig * 2^-149 = 1.f * 2^(k - 149) */
		return from_bits(sign | (uint64_t)(k - 149 + 1023) << 52 | ((sig << (52 - k)) & FRAC));
	}
	return from_bits(sign | (uint64_t)(exp - 127 + 1023) << 52 | sig << 29);
}

float __truncdfsf2(double a)
{
	uint64_t ar = to_bits(a), sig = ar & FRAC, m, h;
	uint32_t sign = (uint32_t)(ar >> 32) & 0x80000000u, r;
	int exp = EXP(ar), shift = 29;
	fbits x;

	if (exp == 0x7ff) {
		x.u = sign | 0x7f800000u | (sig ? 0x400000u | (uint32_t)(sig >> 29) : 0);
		return x.f;
	}
	exp = exp ? exp - 1023 + 127 : -1000;	/* double subnormals round to 0 */
	if (exp >= 0xff) {
		x.u = sign | 0x7f800000u;
		return x.f;
	}
	sig |= IMPLICIT;
	if (exp <= 0) {
		shift += 1 - exp;
		exp = 0;
	}
	if (shift > 54) {
		x.u = sign;
		return x.f;
	}
	r = (uint32_t)(sig >> shift);
	m = sig & ((1ull << shift) - 1);
	h = 1ull << (shift - 1);
	r = sign | (exp ? (uint32_t)exp << 23 | (r & 0x7fffffu) : r);
	if (m > h || (m == h && (r & 1)))
		r++;			/* may carry into the exponent */
	x.u = r;
	return x.f;
}

double __floatunsidf(uint32_t i)
{
	uint64_t sig = i;
	int exp;

	if (!i)
		return from_bits(0);
	exp = 1023 + 52;
	while (!(sig & IMPLICIT)) {
		sig <<= 1;
		exp--;
	}
	return from_bits((uint64_t)exp << 52 | (sig & FRAC));
}

double __floatsidf(int32_t i)
{
	if (i >= 0)
		return __floatunsidf((uint32_t)i);
	return from_bits(to_bits(__floatunsidf(-(uint32_t)i)) | SIGN);
}

/* Toward zero; out of range and NaN saturate (TRUNC.D) */
int32_t __fixdfsi(double a)
{
	uint64_t ar = to_bits(a), sig = (ar & FRAC) | IMPLICIT;
	int exp = EXP(ar) - 1023;
	bool neg = ar & SIGN;
	uint64_t mag;

	if ((ar & ~SIGN) > INF)
		return INT32_MAX;
	if (exp < 0)
		return 0;
	if (exp >= 31)
		return neg && exp == 31 && !(ar & FRAC) ? INT32_MIN : (neg ? INT32_MIN : INT32_MAX);
	mag = sig >> (52 - exp);
	return neg ? -(int32_t)mag : (int32_t)mag;
}

uint32_t __fixunsdfsi(double a)
{
	uint64_t ar = to_bits(a), sig = (ar & FRAC) | IMPLICIT;
	int exp = EXP(ar) - 1023;

	if ((ar & ~SIGN) > INF)
		return UINT32_MAX;
	if ((ar & SIGN) || exp < 0)
		return 0;
	if (exp >= 32)
		return UINT32_MAX;
	return (uint32_t)(sig >> (52 - exp));
}

/* -1, 0, 1 as a <, ==, > b; 2 unordered */
static int compare(double a, double b)
{
	uint64_t ar = to_bits(a), br = to_bits(b);
	uint64_t aa = ar & ~SIGN, ba = br & ~SIGN;

	if (aa > INF || ba > INF)
		return 2;
	if (!(aa | ba))
		return 0;
	if ((int64_t)(ar & br) >= 0) {		/* not both negative */
		if ((int64_t)ar < (int64_t)br)
			return -1;
		return ar == br ? 0 : 1;
	}
	if ((int64_t)ar > (int64_t)br)
		return -1;
	return ar == br ? 0 : 1;
}

int __eqdf2(double a, double b)
{
	int c = compare(a, b);

	return c == 2 ? 1 : c;
}

int __nedf2(double a, double b)
{
	return __eqdf2(a, b);
}

int __ltdf2(double a, double b)
{
	int c = compare(a, b);

	return c == 2 ? 1 : c;
}

int __ledf2(double a, double b)
{
	return __ltdf2(a, b);
}

int __gtdf2(double a, double b)
{
	int c = compare(a, b);

	return c == 2 ? -1 : c;
}

int __gedf2(double a, double b)
{
	return __gtdf2(a, b);
}

int __unorddf2(double a, double b)
{
	return compare(a, b) == 2;
}

/* A 64-bit count to single precision, rounded to nearest even (CIX's
 * libgcc-like __floatundisf, which the idle residence uses) */
float __floatundisf(uint64_t v)
{
	fbits x;
	int msb, shift;
	uint64_t rem, half;

	if (!v)
		return 0.0f;
	msb = 63 - clz64(v);
	if (msb <= 23) {
		x.u = (uint32_t)(msb + 126) << 23;
		x.u += (uint32_t)v << (23 - msb);
		return x.f;
	}
	shift = msb - 23;
	x.u = ((uint32_t)(msb + 126) << 23) + (uint32_t)(v >> shift);
	rem = v & ((1ull << shift) - 1);
	half = 1ull << (shift - 1);
	if (rem > half || (rem == half && (x.u & 1)))
		x.u++;
	return x.f;
}
