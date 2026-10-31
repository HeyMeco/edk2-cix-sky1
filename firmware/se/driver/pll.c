/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * PLLs in the CSU_PM clock generator (CIX driver/pll/drv_pll.c).
 *
 * Each PLL block: +0x04 control (bit 24 output enable), +0x08 feedback
 * divider [6:0], +0x0c configuration (bit 5 start, bit 4 locked).  CPU
 * PLLs of harvested cores stay off.  The register writes keep CIX's order.
 */
#include "se.h"
#include "harvest.h"
#include "platform.h"
#include "soc.h"

#define CGC			0x46570000u	/* SoC 0x06570000 */
#define CPU_PLL(n)		(CGC + (n) * 0x100)
#define DSU_PLL			(CGC + 0x500)
#define CPU_800M		(CGC + 0x600)
#define PIXEL_PLL(n)		(CGC + 0x1000 + (n) * 0x100)
#define MMHUB_PLL		(CGC + 0x2000)
#define MMSYS_PLL		(CGC + 0x2100)
#define CI700_PLL		(CGC + 0x5200)
#define SYSTEM_PLL		(CGC + 0x5300)
#define PCIE_AXI_PLL		(CGC + 0x6000)
#define PCIE_PHY		(CGC + 0x6100)
#define DDRC_PLL		(CGC + 0x7000)

#define PLL_CTRL		0x04
#define PLL_FBDIV		0x08
#define PLL_CFG			0x0c
#define PLL_OUT_EN		BIT(24)
#define PLL_START		BIT(5)
#define PLL_LOCKED		BIT(4)

static void pll_set_div(uint32_t pll, uint32_t div)
{
	REG32(pll + PLL_FBDIV) = (REG32(pll + PLL_FBDIV) & ~0x7fu) | div;
}

static void pll_start(uint32_t pll)
{
	REG32(pll + PLL_CFG) |= PLL_START;
	REG32(pll + PLL_CTRL) |= PLL_OUT_EN;
}

static void pll_wait(uint32_t reg, uint32_t bit, uint32_t loops, const char *who, const char *name)
{
	uint32_t n = plat_get_core_freq() * loops;

	do {
		if (REG32(reg) & bit)
			return;
	} while (--n);
	hal_printf_level(LOG_ERR, "[SE_FW]%s:%s lock failed \n", who, name);
}

#define WAIT(pll, name)	pll_wait((pll) + PLL_CFG, PLL_LOCKED, 2000, __func__, name)

/* Restart the pixel PLLs' post dividers so the outputs line up */
void drv_pll_pixel_div_toggle(void)
{
	static const char *const name[5] = {
		"SMN_BASE_CSUPM_CGC_PIXEL_PLL0", "SMN_BASE_CSUPM_CGC_PIXEL_PLL1",
		"SMN_BASE_CSUPM_CGC_PIXEL_PLL2", "SMN_BASE_CSUPM_CGC_PIXEL_PLL3",
		"SMN_BASE_CSUPM_CGC_PIXEL_PLL4",
	};
	static const uint16_t div_reg[5] = { 0x18, 0x18, 0x18, 0x18, 0x1c };
	uint32_t i, v;

	for (i = 0; i < 5; i++)
		pll_start(PIXEL_PLL(i));
	for (i = 0; i < 5; i++)
		WAIT(PIXEL_PLL(i), name[i]);
	for (i = 0; i < 5; i++) {
		v = REG32(PIXEL_PLL(i) + div_reg[i]);
		REG32(PIXEL_PLL(i) + div_reg[i]) = ~v;
		REG32(PIXEL_PLL(i) + div_reg[i]) = v;
	}
	for (i = 0; i < 5; i++)
		REG32(PIXEL_PLL(i) + PLL_CTRL) &= ~PLL_OUT_EN;
	hal_mdelay(1);
}

uint32_t drv_pll_init(void)
{
	uint32_t cpus = fw_get_cpu_mask_by_harvest(), i, v;

	REG32(CGC + 0x40) = (REG32(CGC + 0x40) & ~BIT(8)) | BIT(4);
	pll_set_div(MMHUB_PLL, 0x14);
	REG32(MMHUB_PLL + PLL_CFG) = 0x2c;
	pll_start(MMHUB_PLL);
	if (cpus & 0x300)
		pll_start(CPU_PLL(0));
	if (cpus & 0xc00) {
		pll_set_div(CPU_PLL(1), 0x11);
		hal_printf_level(LOG_INFO, "set core10/core11 as 2400M \n");
		pll_start(CPU_PLL(1));
	}
	if (cpus & 0x30)
		pll_start(CPU_PLL(2));
	if (cpus & 0xc0)
		pll_start(CPU_PLL(3));
	if (cpus & 0xf)
		pll_start(CPU_PLL(4));
	pll_set_div(DSU_PLL, 0x12);
	REG32(DSU_PLL + PLL_CFG) = 0x28;
	pll_start(DSU_PLL);
	REG32(CPU_800M + PLL_CFG) = 0x2c;
	pll_start(CPU_800M);
	pll_set_div(CI700_PLL, 0x13);
	pll_start(CI700_PLL);
	pll_start(DDRC_PLL);
	pll_set_div(MMSYS_PLL, 0x55);
	REG32(MMSYS_PLL + PLL_CFG) = 0x2c;
	pll_start(MMSYS_PLL);
	pll_start(PCIE_AXI_PLL);
	REG32(PCIE_PHY + 0x4c) |= 3;
	REG32(PCIE_PHY + 0x00) &= ~0x14u;
	REG32(PCIE_PHY + 0x54) = 1;
	REG32(PCIE_PHY + 0x58) |= 0x12;
	for (i = 0x5c; i <= 0x78; i += 4)
		REG32(PCIE_PHY + i) |= 0x11000;

	if (cpus & 0x300)
		WAIT(CPU_PLL(0), "SMN_BASE_CSUPM_CGC_CPU_PLL0");
	if (cpus & 0xc00)
		WAIT(CPU_PLL(1), "SMN_BASE_CSUPM_CGC_CPU_PLL1");
	if (cpus & 0x30)
		WAIT(CPU_PLL(2), "SMN_BASE_CSUPM_CGC_CPU_PLL2");
	if (cpus & 0xc0)
		WAIT(CPU_PLL(3), "SMN_BASE_CSUPM_CGC_CPU_PLL3");
	if (cpus & 0xf)
		WAIT(CPU_PLL(4), "SMN_BASE_CSUPM_CGC_CPU_PLL4");
	WAIT(DSU_PLL, "SMN_BASE_CSUPM_CGC_DSU_PLL");
	WAIT(CPU_800M, "SMN_BASE_CSUPM_CGC_CPU_800M");
	WAIT(MMHUB_PLL, "SMN_BASE_CSUPM_CGC_MMHUB_PLL");
	WAIT(CI700_PLL, "SMN_BASE_CSUPM_CGC_CI700_PLL");
	WAIT(SYSTEM_PLL, "SMN_BASE_CSUPM_CGC_SYSTEM_PLL");
	WAIT(DDRC_PLL, "SMN_BASE_CSUPM_CGC_DDRC_PLL");
	WAIT(MMSYS_PLL, "SMN_BASE_CSUPM_CGC_MMSYS_PLL");
	pll_wait(PCIE_PHY + 0x00, BIT(6), 100, __func__, "SMN_BASE_CSUPM_CGC_PCIE_PHY_PLL");
	WAIT(PCIE_AXI_PLL, "SMN_BASE_CSUPM_CGC_PCIE_AXI_PLL");

	v = REG32(MMHUB_PLL + 0x114);
	REG32(MMHUB_PLL + 0x114) = ~v;
	REG32(MMHUB_PLL + 0x114) = v;
	hal_udelay(10);
	v = REG32(MMHUB_PLL + 0x118);
	REG32(MMHUB_PLL + 0x118) = ~v;
	REG32(MMHUB_PLL + 0x118) = v;
	hal_udelay(10);
	drv_pll_pixel_div_toggle();
	return 0;
}
