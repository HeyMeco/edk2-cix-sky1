/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The board services CIX's firmware runs beyond DVFS and power: the GPIOs
 * (heartbeat, the EC's event line), the I2C slave the EC reads, the board's
 * temperature sensors, PROCHOT, the DDR DFS check with the display, the
 * watchdog's interrupt, and what pm_ready starts (src/main.c).
 */
#ifndef BOARD_H
#define BOARD_H

#include "pm.h"
#include "pmic.h"

/* --- what pm_ready starts once the AP may run (CIX's pm_ready) --------------- */

/* noc_idle_config, op_mode_config, fan_ctrl_start, PVT polling */
void board_services_start(void);
/* CIX's ss_run_enter: init_prochot_timer, dev_gpio_ec_handshake_init,
 * dev_gpio_heartbeat_init (the PVT sensors are src/pvt.c's) */
void board_services_run(void);

/* --- pm_config.c ---------------------------------------------------------------- */

/* CIX's do_pm_config (power-on, before the debug UART) */
void do_pm_config(void);
void noc_idle_config(void);
void op_mode_config(void);
void wdt_restore(void);
void dev_wdt_refresh(void);
/* CIX's pmic_config_preset (scp/config/config_pmic.c) */
void pmic_config_preset(enum pmic_preset preset_idx);

/* --- GPIO (CIX's dev_gpio.c; dev_gpio_init is src/lcru.c's) ----------------- */

enum pm_gpio_function {
	PM_GPIO_FUNC_NULL, PM_GPIO_FUNC_EC, PM_GPIO_FUNC_HEARTBEAT, PM_GPIO_FUNC_INPUT,
};
/* the pins of the two functions, 3 for none (pm_config.c's gpio_config) */
extern uint32_t gpio_heartbeat, gpio_to_ec;
void dev_gpio_set(uint32_t pin, uint32_t val);
uint32_t dev_gpio_get(uint32_t pin);
void dev_gpio_signal_ec_event(void);
void dev_gpio_clear_ec_event(void);
void dev_gpio_ec_handshake_init(void);
void dev_gpio_heartbeat_init(void);
void gpio_intr_handler(void);

/* --- the I2C slave the EC reads (CIX's i2c_slave_mode.c, DEV_REG) ------------ */

/* CIX's CUS_REG: 127 16-bit registers */
enum cus_reg {
	CUS_CTL_REG1 = 0,
	CUS_STAT_REG1 = 4,		/* byte 1 bit 0: the fan mode changed */
	CUS_CONFIG_REG = 8,
	CUS_TEMPR_REG1 = 16,		/* board sensors, CIX's tempr 1-6 */
	CUS_TEMPT_REG1 = 32,		/* the SoC's hottest PVT sensor: degrees, hundredths */
	CUS_FAN_MODE_REG = 33,		/* byte 1: the fan mode Linux asked for, + 2 */
	CUS_REG_MAX = 127,
};
extern volatile uint8_t DEV_REG[CUS_REG_MAX][2];
uint8_t cus_dev_reg_get(uint32_t reg_index, uint32_t byte_index);

/* --- board temperature sensors (CIX's config_sensor.c) -------------------------- */

/* CIX's overwrite_i2c_master: I2C controllers a sensor makes masters */
extern uint32_t overwrite_i2c_master;
/* CIX's config_board_sensor: two TI TMP75s on I2C from pm_config_pvt_t */
void config_board_sensor(const uint8_t *pvt_config);
float config_board_sensor_get(uint32_t idx);
float config_skin_temp_get(void);
void config_skin_temp_params_set(float kh, float ka, float c, float alpha);
void config_skin_temp_params_get(float *kh, float *ka, float *c, float *alpha);
float config_skin_alpha_get(void);

/* --- PROCHOT (CIX's intr_mgmt.c) ------------------------------------------------ */

void init_prochot_timer(void);
void prochot_outside_isr(void);
uint32_t prochot_throttle_Fmax_Khz(void);

/* --- the display and DDR DFS (CIX's dpdpu.c) ------------------------------------- */

void dpdpu_trigger_ddr_dfs_compatible_checker(void);
bool ddr_dfs_check_compatible(void);
uint8_t ddr_dfs_get_compatible_status(void);
void vpu_ddr_dfs_set_compatible(bool is_compatible);
void isp_ddr_dfs_set_compatible(bool is_compatible);
void gpu_ddr_dfs_set_compatible(bool is_compatible);
void npu_ddr_dfs_set_compatible(bool is_compatible);
void dpdpu_align_to_highest_resolution_port(bool align_flag);
bool dpdpu_align_with_highest_resolution_port(uint32_t timeout_cc);

/* --- the watchdog's and others' interrupt (CIX's mixed_intr_isr) ----------------- */

void wdt_intr_handler(void);

/* --- CIX's DPM runtime (src/dpm, app_pmucnt.c) -------------------------------------- */

bool perf_soc_idle_switch_set_enable(bool to_set);
void perf_soc_idle_switch_set_thro(uint32_t amu1_thro0, uint32_t amu1_thro1, uint32_t ddr_dym_pwr_thro,
				   uint32_t hysteresis, uint32_t min_interval);
void perf_soc_idle_switch_get_thro(uint32_t *amu1_thro0, uint32_t *amu1_thro1,
				   uint32_t *ddr_dym_pwr_thro, uint32_t *hysteresis,
				   uint32_t *min_interval);
/* scp/module/sky1soc_perf */
void sky1soc_set_dsu_dvfs(bool enable);

#endif
