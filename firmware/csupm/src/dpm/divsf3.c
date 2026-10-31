/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Single-precision division, which GCC calls for '/' on floats (the
 * toolchain's libgcc is not linked: lib/libgcc.c).  The core's divide
 * steps in the sequence of CIX's __divsf3 (its xclib's): reciprocal
 * estimate (DIV0.S), two Newton steps, the quotient refined and rounded
 * by DIVN.S, so the result is the correctly rounded quotient, as CIX's
 * firmware gets it.  Weak, so that a C library function can replace it.
 */

float __divsf3(float a, float b);

__attribute__((weak)) float __divsf3(float a, float b)
{
	float q, f2, f3, f4, f5, f6, f7, f8;

	__asm__("div0.s    %[f3], %[b]\n\t"
		"nexp01.s  %[f4], %[b]\n\t"
		"const.s   %[f5], 1\n\t"
		"maddn.s   %[f5], %[f4], %[f3]\n\t"
		"mov.s     %[f6], %[f3]\n\t"
		"mov.s     %[f7], %[b]\n\t"
		"nexp01.s  %[f2], %[a]\n\t"
		"maddn.s   %[f6], %[f5], %[f6]\n\t"
		"const.s   %[f5], 1\n\t"
		"const.s   %[q], 0\n\t"
		"neg.s     %[f8], %[f2]\n\t"
		"maddn.s   %[f5], %[f4], %[f6]\n\t"
		"maddn.s   %[q], %[f8], %[f3]\n\t"
		"mkdadj.s  %[f7], %[a]\n\t"
		"maddn.s   %[f6], %[f5], %[f6]\n\t"
		"maddn.s   %[f8], %[f4], %[q]\n\t"
		"const.s   %[f3], 1\n\t"
		"maddn.s   %[f3], %[f4], %[f6]\n\t"
		"maddn.s   %[q], %[f8], %[f6]\n\t"
		"neg.s     %[f2], %[f2]\n\t"
		"maddn.s   %[f6], %[f3], %[f6]\n\t"
		"maddn.s   %[f2], %[f4], %[q]\n\t"
		"addexpm.s %[q], %[f7]\n\t"
		"addexp.s  %[f6], %[f7]\n\t"
		"divn.s    %[q], %[f2], %[f6]"
		: [q] "=&f"(q), [f2] "=&f"(f2), [f3] "=&f"(f3), [f4] "=&f"(f4),
		  [f5] "=&f"(f5), [f6] "=&f"(f6), [f7] "=&f"(f7), [f8] "=&f"(f8)
		: [a] "f"(a), [b] "f"(b));
	return q;
}
