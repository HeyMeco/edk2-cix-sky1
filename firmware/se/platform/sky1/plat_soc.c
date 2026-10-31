/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Sky1 SoC description (CIX platform/sky1/plat_soc.c) */
#include "se.h"
#include "platform.h"
#include "efuse.h"
#include "btcfg.h"

plat_id_t plat_get_plat_id(void) { return PLAT_SOC; }
uint32_t plat_get_ref_freq(void) { return 24; }		/* MHz */
uint32_t plat_is_se_only(void) { return 0; }
plat_fw_load_t plat_get_fm_load(void) { return PLAT_FW_LOAD_SPINOR; }
uint32_t plat_get_uart_baudrate(void) { return 115200; }
uint32_t plat_get_soc_apbclk_freq(void) { return 200; }	/* MHz */
uint32_t plat_get_rtl_revision(void) { return 0xa29b; }
uint32_t plat_get_star_wdt_freq(void) { return 24; }	/* MHz */

/* IP versions the drivers select register layouts by */
uint32_t plat_get_gpu_model(void) { return 10; }
uint32_t plat_get_ddr_model(void) { return 1; }
uint32_t plat_get_dpu_model(void) { return 3; }
uint32_t plat_get_dp_model(void) { return 3; }
uint32_t plat_get_npu_model(void) { return 3; }
uint32_t plat_get_csi_model(void) { return 3; }
uint32_t plat_get_pcie_model(void) { return 5; }
uint32_t plat_get_usb_model(void) { return 1; }
uint32_t plat_get_gmac_model(void) { return 2; }
uint32_t plat_get_vpu_model(void) { return 1; }
uint32_t plat_get_isp_model(void) { return 1; }
uint32_t plat_get_mmhub_model(void) { return 1; }
uint32_t plat_get_syshub_model(void) { return 1; }
uint32_t plat_get_pciehub_model(void) { return 1; }
uint32_t plat_get_sensor_model(void) { return 1; }
uint32_t plat_get_audio_model(void) { return 1; }
uint32_t plat_get_fch_model(void) { return 1; }

/* Core clock in MHz: 800 MHz divided by SE_SYSCTRL_CLKDIV[3:0] + 1 */
uint32_t plat_get_core_freq(void)
{
	return 800 / ((REG32(SE_SYSCTRL_CLKDIV) & 0xf) + 1);
}

/* UART clock in kHz */
uint32_t plat_get_uart_clk_speed(void)
{
	return 800000 / ((REG32(SE_CLKCTRL_BASE + 0x8) & 0xf) + 1);
}

uint32_t plat_get_info(plat_info_t type, uint32_t *info)
{
	if (type != PLAT_INFO_LIFE_CYCLE)
		return 0xffffffff;
	*info = REG32(SE_SYSCTRL_LIFECYCLE) & 7;
	return 0;
}

/*
 * AP core to boot: "ap.coreid" in the boot-time configuration (1-based),
 * else core 10 unless the eFuse harvests it, then the first core of
 * boot_core_order that is not harvested.
 */
static const uint8_t boot_core_order[12] = { 10, 11, 8, 9, 4, 5, 6, 7, 0, 1, 2, 3 };

uint32_t plat_get_ap_bootcore_index(void)
{
	uint32_t core = 0, harvest = 0;
	unsigned int i;

	if (btcfg_get_value((const char *)mntr_boot_cfg_item_table, "ap.coreid", &core) == 0 && core != 0)
		return core - 1;
	sec_efuse_read(&g_handle_efuse, 0x46, 4, 12, &harvest);
	if (!(harvest & BIT(10)))
		return 10;
	for (i = 1; i < ARRAY_SIZE(boot_core_order); i++)
		if (!(harvest & BIT(boot_core_order[i])))
			return boot_core_order[i];
	return 10;
}

void plat_clk_enable_soc_apbclk(void)
{
	REG32(0x46575000 + 0x314) |= 0x10;
}

uint32_t plat_get_boot_reason(void)
{
	return REG32(S5_BOOT_REASON);
}
