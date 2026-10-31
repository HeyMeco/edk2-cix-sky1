/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The PM configuration the BIOS leaves above DRAM1 (CIX's
 * Source/common/drivers/pm_config.c), as far as the V/F tables use it:
 * do_pm_config's check of the block, opp_table_config (OPP tables given by
 * the BIOS), vmin_config (whether the fused minimum voltages are used),
 * override_100M_opp.
 *
 * The block at 0x200bf000: a 16-bit version (3) at +0, its length at +8,
 * two sums at +0x10 and +0x14 (of the words, and of the running sums,
 * computed with both fields zero), then CIX's pm_config_t at +0x18, packed.
 * A config_data_t is valid when its bit 0 is clear (erased flash is all
 * ones), its value in bits 1-31.
 *
 * Left out for now, with the parts of the firmware they configure: the
 * board's rail layout (pmic_scheme_config), fans, PVT, SPT, watchdog,
 * GPIO, PSI7 mode, log UART.
 */
#include "dpm.h"

#define BIOS_CFG		0x200bf000u
#define BIOS_CFG_VERSION	3
#define BIOS_CFG_MAX		0x1000u
#define PM_CONFIG		(BIOS_CFG + 0x18)	/* pm_config_t */

/* offsets in pm_config_t */
#define CFG_OPP_VALID		0x80	/* pm_config_opp_t opp_config */
#define CFG_OPPS		0x81	/* domain_opp_config_t opps[13], 212 bytes each */
#define CFG_VMIN_DISABLE	0xceb
#define CFG_VMIN_PROFILE	0xcef
#define CFG_OPP_100M_ENABLE	0xd33
#define VMIN_ENABLE_MAGIC	0x67997cdau

bool bios_cfg_valid;

static uint8_t cfg8(uint32_t offset)
{
	return REG8(PM_CONFIG + offset);
}

/* CIX's pm_config_get()->field for a 32-bit field (packed, byte reads) */
uint32_t pm_config_get_word(uint32_t offset)
{
	return (uint32_t)cfg8(offset) | (uint32_t)cfg8(offset + 1) << 8 |
	       (uint32_t)cfg8(offset + 2) << 16 | (uint32_t)cfg8(offset + 3) << 24;
}

/* The BIOS's OPPs for DVFS element idx (domain_opp_config_t: size,
 * sustained index, 13 OPPs) */
const uint8_t *pm_config_get_opp(uint32_t idx)
{
	return (const uint8_t *)(uintptr_t)(PM_CONFIG + CFG_OPPS + 212 * idx);
}

/* CIX's do_pm_config_crc_check */
static bool do_pm_config_crc_check(void)
{
	volatile uint32_t *sum1 = (volatile uint32_t *)(BIOS_CFG + 0x10);
	volatile uint32_t *sum2 = (volatile uint32_t *)(BIOS_CFG + 0x14);
	uint32_t len = REG32(BIOS_CFG + 8), s1, s2, a = 0, b = 0;

	if (len > BIOS_CFG_MAX || REG16(BIOS_CFG) != BIOS_CFG_VERSION)
		return false;
	s1 = *sum1;
	s2 = *sum2;
	*sum2 = 0;
	*sum1 = 0;
	for (uint32_t i = 0; i < len; i += 4) {
		a += REG32(BIOS_CFG + i);
		b += a;
	}
	*sum2 = s2;
	*sum1 = s1;
	return s1 == a && s2 == b;
}

/* The OPP tables the BIOS gives: those whose size is set; bit 7 of
 * opp_valid limits the override to them, else all domains count as given */
static void opp_table_config(void)
{
	uint8_t opp_valid = cfg8(CFG_OPP_VALID);
	uint32_t mask = 0;

	if (opp_valid & 0x7f)
		return;
	for (uint32_t i = 0; i < 12; i++) {
		const uint8_t *o = pm_config_get_opp(i);

		if (((uint32_t)o[0] | (uint32_t)o[1] << 8) == 0xffff)
			continue;
		override_config_dvfs_opp_source((uint8_t)i);
		mask |= 1u << i;
	}
	set_config_dvfs_source(opp_valid & 0x80 ? mask | 1u << 30 : 0x80000000u);
}

/* The fused minimum voltages are used when the BIOS says so (a magic) */
static void vmin_config(void)
{
	uint32_t v = pm_config_get_word(CFG_VMIN_DISABLE);
	uint32_t p;

	if (!(v & 1)) {
		if ((v & ~1u) == VMIN_ENABLE_MAGIC)
			cfg_vf_fuse_enable();
		return;
	}
	p = pm_config_get_word(CFG_VMIN_PROFILE);
	if (!(p & 1))
		cfg_vf_fuse_set_profile(p >> 1);
}

/* CIX's do_pm_config, the parts above (power-on, before the fuses) */
void do_pm_config(void)
{
	bios_cfg_valid = do_pm_config_crc_check();
	if (!bios_cfg_valid)
		return;
	opp_table_config();
	vmin_config();
}

/* Without the BIOS's say-so the CPU clusters and DSU start at 200 MHz and
 * the GPU at 60, not 100 */
void override_100M_opp(void)
{
	if (bios_cfg_valid && !(cfg8(CFG_OPP_100M_ENABLE) & 1))
		return;
	for (uint32_t i = DPM_DxS_CORE_LIT; i <= DPM_DxS_DSU; i++)
		override_config_dvfs_minimum_level(i, 200, true);
	override_config_dvfs_minimum_level(DPM_DxS_GPU_CORE, 60, true);
	override_config_dvfs_minimum_level(DPM_DxS_GPU_TOP, 60, true);
}
