/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * SoC bring-up order (CIX hal/common/hal_soc.c, driver/mpu/drv_mpu.c).
 *
 * pre:  PLLs, MPU (DRAM windows non-cacheable), clocks/resets/power,
 *       interconnect, memory configuration, the CPU cluster's power, the
 *       system counter;
 * post: DRAM training, the interconnect's memory map, the multimedia hub,
 *       the reset/clock security units, crash handling.
 * On resume from suspend (is_str) DRAM keeps its training.
 */
#include "se.h"
#include "arch.h"
#include "ci700.h"
#include "ddr.h"
#include "drivers.h"
#include "memcfg.h"
#include "platform.h"
#include "soc.h"

typedef struct {
	uint16_t idx;
	uint8_t attr_a;		/* RBAR: SH, AP, XN */
	uint8_t attr_b;		/* MAIR byte */
	uint32_t start, end;
} mpu_region_t;

/* Normal non-cacheable, execute never: DRAM the SE writes for later stages */
static const mpu_region_t g_mpu_param[4] = {
	{ 0, 0x1b, 0x44, 0x60000000, 0x60200000 },
	{ 1, 0x1b, 0x44, 0x60500000, 0x62500000 },
	{ 2, 0x1b, 0x44, 0x63c00000, 0x63e00000 },
	{ 3, 0x1b, 0x44, 0x63e00000, 0x63f00000 },
};

static uint8_t soc_init_done;

static uint32_t mpu_regions(void)
{
	return (REG32(MPU_TYPE) & 0xffff) >> 8;
}

static uint32_t mpu_region_enable(uint32_t idx, int on)
{
	if (idx >= mpu_regions())
		return 0xffffffff;
	REG32(MPU_RNR) = idx;
	if (on)
		REG32(MPU_RLAR) |= 1;
	else
		REG32(MPU_RLAR) &= ~1u;
	return 0;
}

static uint32_t mpu_config_region(const mpu_region_t *r)
{
	uint32_t shift = (r->idx & 3) * 8, mair = r->idx < 4 ? MPU_MAIR0 : MPU_MAIR1;

	if (r->idx >= mpu_regions())
		return 0xffffffff;
	REG32(MPU_RNR) = r->idx;
	REG32(MPU_RBAR) = (r->start & ~0x1fu) | r->attr_a;
	REG32(MPU_RLAR) = ((r->end - 1) & ~0x1fu) | r->idx << 1 | (REG32(MPU_RLAR) & 1);
	REG32(mair) &= ~(0xffu << shift);
	REG32(mair) |= (uint32_t)r->attr_b << shift;
	return 0;
}

static void hal_soc_mpu_init(void)
{
	uint32_t i, n, err;

	REG32(MPU_CTRL) &= ~1u;
	n = mpu_regions();
	for (i = 0; i < n; i++) {
		err = mpu_region_enable(i, 0);
		if (err) {
			SE_ERR(err);
			return;
		}
	}
	for (i = 0; i < ARRAY_SIZE(g_mpu_param); i++) {
		err = mpu_config_region(&g_mpu_param[i]);
		if (!err)
			err = mpu_region_enable(g_mpu_param[i].idx, 1);
		if (err) {
			SE_ERR(err);
			return;
		}
	}
	REG32(MPU_CTRL) |= 5;		/* enable, default map for privileged */
}

uint32_t hal_soc_init_pre(uint32_t is_str)
{
	hal_printf_level(LOG_ERR, "[SE_FW]# drv_soc_init_pre start\n");
	drv_pll_init();
	hal_printf_level(LOG_INFO, "#######soc init with base power/clk/reset #########\n");
	hal_soc_mpu_init();
	drv_soc_cru_init(is_str);
	drv_ci700_init_pre();
	REG32(0x44210000 + 0x8) = 1;
	REG32(0x44210000 + 0xc) = 1;
	if (!is_str)
		drv_memcfg_init();
	drv_soc_dsu_power_on();
	if (!is_str)
		drv_syscounter_clk_enable();
	hal_printf_level(LOG_ERR, "[SE_FW]# drv_soc_init_pre end\n");
	return 0;
}

uint32_t hal_soc_init_post(uint32_t is_str)
{
	uint32_t ch, err;

	hal_printf_level(LOG_ERR, "[SE_FW]# drv_soc_init_post start\n");
	if (plat_get_ddr_model() && !is_str) {
		hal_printf_level(LOG_ERR, "[SE_FW]# drv_ddr_init start\n");
		drv_ddr_init();
		hal_printf_level(LOG_ERR, "[SE_FW]# drv_ddr_init end\n");
	}
	drv_ci700_init();
	if (plat_get_ddr_model() && !is_str && plat_get_plat_id() == PLAT_SOC) {
		hal_printf_level(LOG_INFO, "drv_ddr_postci \n");
		drv_ddr_postci();
	}
	ch = drv_ddr_get_ch_num();
	if (ch - 1 < 2 && plat_get_mmhub_model()) {
		hal_printf_level(LOG_INFO, "drv_mmhub_init \n");
		drv_mmhub_init();
	}
	if (!is_str)
		drv_ci700_update_mem_size();
	err = hal_rcsu_init(is_str);
	if (err) {
		SE_ERR(err);
		return err;
	}
	err = hal_crash_init(is_str);
	if (err) {
		SE_ERR(err);
		return err;
	}
	soc_init_done = 1;
	hal_printf_level(LOG_ERR, "[SE_FW]# drv_soc_init_post end\n");
	return 0;
}

uint8_t hal_soc_init_is_done(void)
{
	return soc_init_done;
}
