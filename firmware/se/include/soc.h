/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* SoC bring-up (hal/soc.c and the drivers it calls) */
#ifndef SOC_H
#define SOC_H

#include <stdint.h>

uint32_t hal_soc_init_pre(uint32_t is_str);
uint32_t hal_soc_init_post(uint32_t is_str);
uint8_t hal_soc_init_is_done(void);

uint32_t drv_pll_init(void);
void drv_pll_pixel_div_toggle(void);
uint32_t drv_soc_cru_init(uint32_t is_str);
uint32_t drv_soc_wait_power_state(uint32_t reg, uint32_t mask, uint32_t state, uint32_t us);
void drv_soc_dsu_power_on(void);
void drv_soc_release_ap(void);
uint32_t drv_pm_init(void);
void drv_soc_release_pm(void);
void drv_mmhub_init(void);
uint32_t hal_rcsu_init(uint32_t is_str);

uint32_t hal_crash_init(uint32_t is_str);
void hal_crash_deinit(void);
void hal_crash_feed_dog(void);
void *hal_crash_get_eaddr(const char *name, uint32_t size);
void clk_save_einfo(void);
void pwr_save_einfo(void);
void reboot(uint8_t subreason);
void fw_err_handler(uint32_t err);

uint32_t hal_sfh_init(void);
uint32_t hal_sfh_deinit(void);
uint32_t hal_is_se_load_sfh(void);

void plat_save_wakeup_source(void);
int plat_wakeup_source_init(void);

#endif
