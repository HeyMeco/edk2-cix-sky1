/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Sensor fusion hub (CIX hal/common/hal_sfh.c): during suspend the SE
 * powers the SFH, copies its firmware (flash firmware table type 8) into
 * the SFH's SRAM and starts it; the firmware answers 0xaabb when up.
 */
#include "se.h"
#include "soc.h"
#include "storage.h"
#include "xloader.h"

#define SFH_REMAP		0x48000034u
#define SFH_CTRL		0x48080000u	/* the SFH's control block (0x50070000 seen from the SFH) */
#define SFH_HANDSHAKE		(SFH_CTRL + 0x100)
#define S5_SFH_PWR		0x56000008u
#define S5_SFH_CLK		0x5600001cu
#define S5_SFH_RST		0x5600030cu
#define S5_SFH_CORE_RST		0x56000308u
#define SFH_SRAM_WINDOW		0x60000000u	/* SYSHUB window mapped at the SFH's SRAM */

static uint32_t se_sfh_load;

static uint32_t hal_sfh_load(void)
{
	uint32_t size = 0, err;

	hal_printf_level(LOG_INFO, "START LOAD SFH FW \n");
	REG32(SFH_REMAP) = 0x50000000;
	REG32(S5_SFH_RST) = 0;
	hal_udelay(10);
	REG32(S5_SFH_RST) = 1;
	REG32(S5_SFH_PWR) = 1;
	REG32(S5_SFH_CLK) = 1;
	REG32(SFH_CTRL) |= 3;
	REG32(SFH_CTRL + 0x7c) = 0x20;
	REG32(SFH_CTRL + 0x10) |= 0x10;
	REG32(SFH_CTRL) |= 0x7800;
	REG32(SFH_CTRL + 0x14) &= 0xfffc3fff;
	hal_udelay(10);
	REG32(SFH_CTRL + 0x14) |= 0x3c000;
	fw_jumper_init(SEC_FW_TYPE_SFH);
	err = hal_stor_get_sub_img(SFH_FW_IMG_DATA_TYPE, (uint8_t *)SFH_SRAM_WINDOW, &size);
	if (err)
		return err;
	fw_jumper_init(SEC_FW_TYPE_PBL);
	REG32(S5_SFH_CORE_RST) = 0;
	hal_udelay(20);
	REG32(S5_SFH_CORE_RST) = 1;
	hal_printf_level(LOG_INFO, "END LOAD SFH FW \n");
	se_sfh_load = 1;
	return 0;
}

uint32_t hal_sfh_deinit(void)
{
	REG32(SFH_REMAP) = 0x50000000;
	REG32(SFH_CTRL) &= ~3u;
	REG32(S5_SFH_PWR) = 0;
	REG32(S5_SFH_CLK) = 0;
	se_sfh_load = 0;
	hal_printf_level(LOG_INFO, "DEINIT THE SFH FW \n");
	return 0;
}

uint32_t hal_sfh_is_on(void)
{
	return REG32(SFH_REMAP) == 0x50000000 && REG32(S5_SFH_PWR) && REG32(S5_SFH_CLK);
}

uint32_t hal_is_se_load_sfh(void)
{
	return se_sfh_load;
}

/* Load and start the SFH; five tries of 1.2 s for its handshake */
uint32_t hal_sfh_init(void)
{
	uint32_t tries, ms, err;

	if (hal_sfh_is_on())
		return 0;
	for (tries = 0, ms = 1200; tries < 5; tries++, ms = 0xffffffff) {
		err = hal_sfh_load();
		if (err)
			return err;
		for (;;) {
			if (REG32(SFH_HANDSHAKE) == 0xaabb) {
				REG32(SFH_HANDSHAKE) = 0;
				hal_printf_level(LOG_INFO, "SFH load success\n");
				return 0;
			}
			hal_mdelay(1);
			if (ms-- == 0)
				break;
		}
		err = hal_sfh_deinit();
		if (err)
			return err;
		hal_printf_level(LOG_INFO, "Wait timeout for SFH load\n");
	}
	return 0xffffffff;
}
