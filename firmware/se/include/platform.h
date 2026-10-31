/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Sky1 platform description (CIX platform/sky1) */
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>

typedef enum {
	PLAT_SIM, PLAT_EMU_VIRTUAL, PLAT_EMU_ICE, PLAT_FPGA, PLAT_SOC, PLAT_UNKNOWN,
} plat_id_t;

typedef enum {
	PLAT_INFO_LIFE_CYCLE, PLAT_INFO_ATE_MODE, PLAT_INFO_STALL_MODE,
} plat_info_t;

typedef enum { PLAT_FW_BKDOOR, PLAT_FW_LOAD_SPINOR } plat_fw_load_t;

/* S5 power controller (wake-up sources, boot reason) */
#define S5_PMCTRL_BASE		0x56000000u
#define S5_WAKEUP_SOURCE	(S5_PMCTRL_BASE + 0x210)
#define S5_WAKEUP_TYPE		(S5_PMCTRL_BASE + 0x214)
#define S5_BOOT_REASON		(S5_PMCTRL_BASE + 0x218)
#define S5_GPIO_BASE		0x56004000u	/* three S5 GPIO banks, 0x1000 apart */

plat_id_t plat_get_plat_id(void);
uint32_t plat_get_ref_freq(void);
uint32_t plat_get_core_freq(void);
uint32_t plat_is_se_only(void);
plat_fw_load_t plat_get_fm_load(void);
uint32_t plat_get_uart_clk_speed(void);
uint32_t plat_get_uart_baudrate(void);
uint32_t plat_get_gpu_model(void);
uint32_t plat_get_ddr_model(void);
uint32_t plat_get_dpu_model(void);
uint32_t plat_get_dp_model(void);
uint32_t plat_get_npu_model(void);
uint32_t plat_get_csi_model(void);
uint32_t plat_get_pcie_model(void);
uint32_t plat_get_usb_model(void);
uint32_t plat_get_gmac_model(void);
uint32_t plat_get_vpu_model(void);
uint32_t plat_get_isp_model(void);
uint32_t plat_get_mmhub_model(void);
uint32_t plat_get_syshub_model(void);
uint32_t plat_get_pciehub_model(void);
uint32_t plat_get_sensor_model(void);
uint32_t plat_get_audio_model(void);
uint32_t plat_get_fch_model(void);
uint32_t plat_get_info(plat_info_t type, uint32_t *info);
uint32_t plat_get_ap_bootcore_index(void);
void plat_clk_enable_soc_apbclk(void);
uint32_t plat_get_soc_apbclk_freq(void);
uint32_t plat_get_rtl_revision(void);
uint32_t plat_get_star_wdt_freq(void);
uint32_t plat_get_boot_reason(void);
void plat_save_wakeup_source(void);
void plat_send_wakeup_src(void);
int plat_wakeup_source_init(void);

#endif
