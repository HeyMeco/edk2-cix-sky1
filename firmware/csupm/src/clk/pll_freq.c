/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * CIX's pll_freq.c translated: PLL settings for a frequency.
 *
 * The PLLs run from the 24 MHz reference:
 *   FoutVCO     = 24 MHz * (FBDIV + FRAC / 2^24) / REFDIV, 1.6 to 6.4 GHz
 *   FoutPOSTDIV = FoutVCO / (POSTDIV1 * POSTDIV2), each 1 to 7
 * FBDIV is at most 4095 and at least 16 (20 with a fraction).
 */
#include "clk.h"

#define PLL_REF_HZ	24000000u
#define VCO_MIN		1600000000ull
#define VCO_MAX		6400000000ull

/* the post-divider "table" when solving for the VCO alone */
static uint8_t _s_no_post_div = 1;

uint64_t pll_calc_freq_FoutVCO(uint32_t FBDIV, uint32_t frac, uint32_t REFDIV)
{
	uint64_t f = (uint64_t)FBDIV * PLL_REF_HZ + (((uint64_t)frac * PLL_REF_HZ) >> 24);

	return f / (REFDIV ? REFDIV : 1);
}

/* Rounded to the nearest Hz */
uint32_t pll_calc_freq_FoutPOSTDIV(uint32_t FBDIV, uint32_t frac, uint32_t REFDIV, uint32_t POSTDIV)
{
	uint64_t vco = pll_calc_freq_FoutVCO(FBDIV, frac, REFDIV);
	uint32_t div = POSTDIV ? POSTDIV : 1;
	uint64_t q = vco / div;
	uint32_t rem = (uint32_t)vco - (uint32_t)q * div;

	q += div < rem * 2;
	CLK_ASSERT((q >> 32) == 0, 0xf15a0051);
	return (uint32_t)q;
}

/* POSTDIV1 * POSTDIV2, POSTDIV1 the largest factor up to 7 */
static void _factorize_postdiv(uint32_t postdiv, uint32_t *pF1, uint32_t *pF2)
{
	uint32_t f;

	for (f = 7; f; f--) {
		if (postdiv / f * f == postdiv) {
			*pF1 = f;
			*pF2 = postdiv / f;
			return;
		}
	}
	CLK_ASSERT(0, 0xf15a0083);
}

/*
 * The settings giving the output closest to desired: for each post
 * divider, FBDIV and FRAC for desired * postdiv.  Of equally close ones an
 * integer setting wins, then the smallest FBDIV.  In fast mode the first
 * within 1 Hz is taken.  Returns the error in Hz.
 */
static uint32_t _pll_solve_settings(bool fastmode, uint64_t desired, uint8_t *postdiv_array,
				    uint32_t postdiv_length, uint32_t *FBDIV, uint32_t *frac,
				    uint32_t *REFDIV, uint32_t *PSTDIV1, uint32_t *PSTDIV2)
{
	uint32_t best_err = ~0u, best_fbdiv = ~0u, best_frac = 0, best_refdiv = 1, best_pd = 1;
	uint32_t refdiv, i, f1, f2;

	for (refdiv = 1; refdiv < 2; refdiv++) {
		for (i = 0; i < postdiv_length; i++) {
			uint32_t pd = postdiv_array[i], fb, fr, err;
			uint64_t vco = (uint64_t)pd * desired, q, fout;

			if (vco - VCO_MIN > VCO_MAX - VCO_MIN)
				continue;
			q = ((uint64_t)refdiv << 24) * vco / PLL_REF_HZ;
			fb = (uint32_t)(q >> 24);
			fr = (uint32_t)q & 0xffffff;
			if (fb > 4095 || fb < (fr ? 20u : 16u))
				continue;

			fout = pll_calc_freq_FoutVCO(fb, fr, refdiv) / pd;
			err = (uint32_t)(fout > desired ? fout - desired : desired - fout);
			if (fastmode && err < 2) {
				best_err = err;
				best_fbdiv = fb;
				best_frac = fr;
				best_refdiv = refdiv;
				best_pd = pd;
				goto found;
			}
			if (err < best_err ||
			    (err == best_err &&
			     ((best_frac && !fr) ||
			      (fb <= best_fbdiv && !(!best_frac && fr))))) {
				best_err = err;
				best_fbdiv = fb;
				best_frac = fr;
				best_refdiv = refdiv;
				best_pd = pd;
			}
			if (best_err == 0)
				goto found;
		}
	}
	CLK_ASSERT(best_err != ~0u, 0xf15a0179);
found:
	_factorize_postdiv(best_pd, &f1, &f2);
	if (FBDIV)
		*FBDIV = best_fbdiv;
	if (frac)
		*frac = best_frac;
	if (REFDIV)
		*REFDIV = best_refdiv;
	if (PSTDIV1)
		*PSTDIV1 = f1;
	if (PSTDIV2)
		*PSTDIV2 = f2;
	return best_err;
}

uint32_t pll_solve_freq_settings(bool fastmode, uint32_t desired, uint32_t *FBDIV, uint32_t *frac,
				 uint32_t *REFDIV, uint32_t *PSTDIV1, uint32_t *PSTDIV2)
{
	CLK_ASSERT(desired >= 32700000 && desired <= 4000000000u, 0xf15a0213);
	return _pll_solve_settings(fastmode, desired, _s_postdiv, ARRAY_SIZE(_s_postdiv),
				   FBDIV, frac, REFDIV, PSTDIV1, PSTDIV2);
}

uint32_t pll_solve_vco_settings(bool fastmode, uint64_t desired_vco, uint32_t *FBDIV,
				uint32_t *frac, uint32_t *REFDIV)
{
	CLK_ASSERT(desired_vco - VCO_MIN <= VCO_MAX - VCO_MIN, 0xf15a0234);
	return _pll_solve_settings(fastmode, desired_vco, &_s_no_post_div, 1,
				   FBDIV, frac, REFDIV, NULL, NULL);
}

/* The post dividers closest to desired from FoutVCO; returns the error */
uint32_t pll_solve_postdiv_settings(uint64_t FoutVCO, uint32_t desired, uint32_t *PSTDIV1,
				    uint32_t *PSTDIV2)
{
	uint32_t best_err = ~0u, best_pd = 1, i, f1, f2;

	for (i = 0; i < ARRAY_SIZE(_s_postdiv); i++) {
		uint32_t pd = _s_postdiv[i];
		uint32_t f = (uint32_t)(FoutVCO / pd);
		uint32_t err = f > desired ? f - desired : desired - f;

		if (err < best_err) {
			best_err = err;
			best_pd = pd;
		}
		if (best_err < 2)
			break;
	}
	_factorize_postdiv(best_pd, &f1, &f2);
	if (PSTDIV1)
		*PSTDIV1 = f1;
	if (PSTDIV2)
		*PSTDIV2 = f2;
	return best_err;
}
