/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Starting the CPU cluster and CSU_PM (CIX driver/dsu/drv_dsu.c,
 * driver/pm/drv_pm.c), and the AP debug blocks' addresses
 * (driver/dfd/drv_dfd.c).
 */
#include "se.h"
#include "bootperf.h"
#include "platform.h"
#include "soc.h"

#define CPU_PWR_CTRL		0x4fc50000u
#define CLUSTER_PPU		0x4f030000u
#define CORE_PPU(n)		(0x4f080000u + (n) * 0x100000)
#define PPU_PWPR		0x0
#define PPU_PWSR		0x8
#define S5_PM_RUN		0x56000704u
#define S5_PM_RESET		0x56000304u

/* DSU cluster on (cores stay off) */
void drv_soc_dsu_power_on(void)
{
	uint32_t mode = plat_get_plat_id() == PLAT_FPGA ? 0x1000100 : 0x70008;

	REG32(CPU_PWR_CTRL + 0x278) = 0x55555555;
	REG32(CPU_PWR_CTRL + 0x27c) = 0x55555555;
	REG32(CPU_PWR_CTRL + 0x280) = 0x55555555;
	REG32(CPU_PWR_CTRL + 0x284) = (REG32(CPU_PWR_CTRL + 0x284) & ~0xffu) | 0x55;
	REG32(CLUSTER_PPU + PPU_PWPR) = mode;
	while (REG32(CLUSTER_PPU + PPU_PWSR) != mode)
		;
	hal_printf_level(LOG_INFO, "cluster-ppu %08x \n", (unsigned)mode);
}

/* Power on the boot core: BL2 runs */
void drv_soc_release_ap(void)
{
	uint32_t core;

	hal_printf_level(LOG_INFO, "Start to release AP core\n");
	core = plat_get_ap_bootcore_index();
	cix_set_boot_phase(SE_PHASE, RECORD_END);
	cix_boot_perf_uninit(SE_PHASE);
	REG32(CORE_PPU(core) + PPU_PWPR) = 8;
	while (REG32(CORE_PPU(core) + PPU_PWSR) != 8)
		;
	hal_printf_level(LOG_INFO, "core[%d]-ppu %08x \n", (int)core, 8u);
}

/* cmd 1: hold, 2: pulse reset, 0: run */
static uint32_t drv_pm_control(uint32_t cmd)
{
	switch (cmd) {
	case 0:
		REG32(S5_PM_RUN) &= ~1u;
		return 0;
	case 1:
		REG32(S5_PM_RUN) |= 1;
		return 0;
	case 2:
		REG32(S5_PM_RESET) &= ~1u;
		hal_udelay(10);
		REG32(S5_PM_RESET) |= 1;
		return 0;
	}
	return 0xa502;
}

uint32_t drv_pm_init(void)
{
	uint32_t err = drv_pm_control(1);

	if (!err)
		drv_pm_control(2);
	return err;
}

void drv_soc_release_pm(void)
{
	drv_pm_control(0);
}

/* Each AP core's debug and CTI blocks (for crash dumps) */
static const uint32_t g_cores_dbg_base[12][2] = {
	{ 0x18890000, 0x188f0000 }, { 0x18910000, 0x18970000 }, { 0x18990000, 0x189f0000 },
	{ 0x18a10000, 0x18a70000 }, { 0x18a90000, 0x18af0000 }, { 0x18b10000, 0x18b70000 },
	{ 0x18b90000, 0x18bf0000 }, { 0x18c10000, 0x18c70000 }, { 0x18c90000, 0x18cf0000 },
	{ 0x18d10000, 0x18d70000 }, { 0x18d90000, 0x18df0000 }, { 0x18e10000, 0x18e70000 },
};

static struct {
	uint32_t dbg_base, cti_base;
	uint32_t state[4];
} g_ap_dpm_desc[12];

void drv_dfd_init(void)
{
	unsigned int i;

	hal_printf_level(LOG_INFO, "sky1 dfd init... \n");
	for (i = 0; i < ARRAY_SIZE(g_ap_dpm_desc); i++) {
		g_ap_dpm_desc[i].dbg_base = g_cores_dbg_base[i][0];
		g_ap_dpm_desc[i].cti_base = g_cores_dbg_base[i][1];
	}
}
