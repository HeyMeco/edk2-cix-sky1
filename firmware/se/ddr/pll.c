/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The DDR PLL and the controllers' clocks (CIX driver/ddr/src/Lib/pll.c):
 * clock tile 7 of the clock controller at SMN 0x06577000.  Translated from
 * CIX's SE firmware debug build, function by function.
 */
#include "mem_types.h"

#define DDRC_PLL		0x06577000
#define PLL_PARAMETER1		(DDRC_PLL + 0x00)	/* FBDIV */
#define PLL_PARAMETER2		(DDRC_PLL + 0x04)	/* FRAC, PLLEN, DSMEN */
#define PLL_PARAMETER3		(DDRC_PLL + 0x08)	/* POSTDIV1, POSTDIV2, REFDIV */
#define PLL_CTRL		(DDRC_PLL + 0x0c)	/* pll_lock_ddrc_pll */
#define PLL_SSMOD		(DDRC_PLL + 0x10)	/* spread spectrum */
#define PLL_BYPASS		(DDRC_PLL + 0x14)
#define PLL_DIV			(DDRC_PLL + 0x18)	/* AXI and DFI clock dividers */
#define PLL_GATER1		(DDRC_PLL + 0x1c)
#define PLL_GATER2		(DDRC_PLL + 0x20)

#define PLLEN			(1u << 24)
#define DSMEN			(1u << 28)
#define PLL_LOCK		(1u << 4)
#define PLL_REF_MHZ		24

/*
 * DdrcPllFrequencySet: the PLL for Freq (MHz) times the DFI ratio
 * (Axi2Dfi bits 7:4): the VCO as close to 3200 MHz as POSTDIV1 (1-7)
 * allows, POSTDIV2 1, reference divider 1, integer and fractional
 * feedback from the 24 MHz reference.  CIX writes FRAC first unless
 * fractional mode (DSMEN) is on.
 */
static void DdrcPllFrequencySet(uint32_t Freq)
{
	MEMORY_INIT_S *MemInitPtr = GetMemInitPtr();
	DEV_CGC_REG_CLOCK_TILE7_DDRC_PLL_PARAMETER1 pll_parameter1;
	DEV_CGC_REG_CLOCK_TILE7_DDRC_PLL_PARAMETER2 pll_parameter2;
	DEV_CGC_REG_CLOCK_TILE7_DDRC_PLL_PARAMETER3 pll_parameter3;
	uint32_t PllFreq, FBDIV, FRAC;
	uint8_t POSTDIV1;

	PllFreq = Freq * (MemInitPtr->Axi2Dfi >> 4);
	POSTDIV1 = 3200 / PllFreq;
	if (POSTDIV1 >= 7)
		POSTDIV1 = 7;
	if (POSTDIV1 < 1)
		POSTDIV1 = 1;
	PllFreq *= POSTDIV1;
	pll_parameter3.value = SmnRegRead32(PLL_PARAMETER3);
	pll_parameter3.fields.POSTDIV1 = POSTDIV1;
	pll_parameter3.fields.POSTDIV2 = 1;
	pll_parameter3.fields.REFDIV = 1;
	SmnRegWrite32(PLL_PARAMETER3, pll_parameter3.value);
	pll_parameter1.value = SmnRegRead32(PLL_PARAMETER1);
	FBDIV = PllFreq / PLL_REF_MHZ;
	pll_parameter1.fields.FBDIV = FBDIV;
	pll_parameter2.value = SmnRegRead32(PLL_PARAMETER2);
	FRAC = ((PllFreq - FBDIV * PLL_REF_MHZ) << 24) / PLL_REF_MHZ;
	pll_parameter2.fields.FRAC = FRAC;
	if (pll_parameter2.value & DSMEN) {
		SmnRegWrite32(PLL_PARAMETER1, pll_parameter1.value);
		SmnRegWrite32(PLL_PARAMETER2, pll_parameter2.value);
	} else {
		SmnRegWrite32(PLL_PARAMETER2, pll_parameter2.value);
		SmnRegWrite32(PLL_PARAMETER1, pll_parameter1.value);
	}
}

/* DdrcDfsPllSwitch: POSTDIV2 for a frequency set point (set point 2: 1,
 * else the DFS ratio), with the controllers' clocks gated meanwhile */
void DdrcDfsPllSwitch(uint8_t FreqSet)
{
	DEV_CGC_REG_CLOCK_TILE7_DDRC_PLL_PARAMETER3 pll_parameter3;
	uint32_t pll_gater1 = SmnRegRead32(PLL_GATER1);
	uint32_t pll_gater2 = SmnRegRead32(PLL_GATER2);

	SmnRegWrite32(PLL_GATER1, 0);
	SmnRegWrite32(PLL_GATER2, 0);
	pll_parameter3.value = SmnRegRead32(PLL_PARAMETER3);
	pll_parameter3.fields.POSTDIV2 = FreqSet == 2 ? 1 : GetMemInitPtr()->DfsRatio & 7;
	SmnRegWrite32(PLL_PARAMETER3, pll_parameter3.value);
	SmnRegWrite32(PLL_GATER1, pll_gater1);
	SmnRegWrite32(PLL_GATER2, pll_gater2);
}

/* DdrcPllFrequencyChange: relock the PLL at Freq with the clocks gated */
void DdrcPllFrequencyChange(uint32_t Freq)
{
	uint32_t pll_gater1 = SmnRegRead32(PLL_GATER1);
	uint32_t pll_gater2 = SmnRegRead32(PLL_GATER2);

	SmnRegWrite32(PLL_GATER1, 0);
	SmnRegWrite32(PLL_GATER2, 0);
	SmnRegWrite32(PLL_PARAMETER2, SmnRegRead32(PLL_PARAMETER2) & ~PLLEN);
	DdrcPllFrequencySet(Freq);
	SmnRegWrite32(PLL_PARAMETER2, SmnRegRead32(PLL_PARAMETER2) | PLLEN);
	while (!(SmnRegRead32(PLL_CTRL) & PLL_LOCK))
		;
	udelay(1);
	SmnRegWrite32(PLL_GATER1, pll_gater1);
	SmnRegWrite32(PLL_GATER2, pll_gater2);
}

/*
 * DdrcPllInit: the PLL at 50 MHz (times the DFI ratio) with spread
 * spectrum as configured, the AXI and DFI clock dividers, and the clocks
 * of the populated channels (GATER1: controller clocks, two bits per
 * channel; GATER2: the same, the PHY PLL clock depending on bypass).
 */
void DdrcPllInit(void)
{
	MEMORY_INIT_S *MemInitPtr = GetMemInitPtr();
	DEV_CGC_REG_CLOCK_TILE7_DDRC_PLL_DIV pll_div = { .value = 0 };
	uint32_t ssmod, pll_gater1, pll_gater2, ch;
	uint8_t SSC = GetMemFeatPtr()->SSC, PhyClk;

	if (SmnRegRead32(PLL_PARAMETER2) & PLLEN)
		SmnRegWrite32(PLL_PARAMETER2, SmnRegRead32(PLL_PARAMETER2) & ~PLLEN);
	if (MemGetBootEnv() != 0)
		SmnRegWrite32(PLL_BYPASS, PhyPllBypassEn() != 0);
	if (SSC) {
		SmnRegWrite32(PLL_PARAMETER2, SmnRegRead32(PLL_PARAMETER2) & ~DSMEN);
		ssmod = SmnRegRead32(PLL_SSMOD);
		ssmod = (ssmod & ~0x1f00u) | (SSC & 0x1f) << 8;		/* SPREAD */
		/* DOWNSPREAD, RESET, DISABLE_SSCG, RESETPTR off */
		SmnRegWrite32(PLL_SSMOD, ssmod & 0xeeeeffff);
		SmnRegWrite32(PLL_PARAMETER2, SmnRegRead32(PLL_PARAMETER2) | DSMEN);
	}
	DdrcPllFrequencySet(50);
	udelay(1);
	SmnRegWrite32(PLL_PARAMETER2, SmnRegRead32(PLL_PARAMETER2) | PLLEN);
	while (!(SmnRegRead32(PLL_CTRL) & PLL_LOCK))
		;
	SmnRegWrite32(PLL_DIV, 0x00040404);
	pll_div.fields.coef_div_cfg_dficlk = (MemInitPtr->Axi2Dfi >> 4) - 1;
	pll_div.fields.coef_div_cfg_axiclk = (MemInitPtr->Axi2Dfi & 0xf) - 1;
	if (MemInitPtr->DdrType == 1)
		pll_div.fields.coef_div_sel_4_8_ddr = 3;
	else if (MemInitPtr->DdrType == 0)
		pll_div.fields.coef_div_sel_4_8_ddr = 1;
	SmnRegWrite32(PLL_DIV, pll_div.value);
	pll_gater1 = 0;
	for (ch = 0; ch < 4; ch++)
		pll_gater1 |= (uint32_t)MemChannelPopulated(ch) << (4 * ch) | (uint32_t)MemChannelPopulated(ch) << (16 + 4 * ch);
	SmnRegWrite32(PLL_GATER1, pll_gater1);
	PhyClk = MemGetBootEnv() == 0 ? 1 : PhyPllBypassEn();
	pll_gater2 = 0;
	for (ch = 0; ch < 4; ch++) {
		pll_gater2 |= (uint32_t)((MemChannelPopulated(ch) ? PhyClk : 0) & 1) << (16 + 4 * ch);
		pll_gater2 |= (uint32_t)MemChannelPopulated(ch) << (4 * ch);
	}
	SmnRegWrite32(PLL_GATER2, pll_gater2);
}
