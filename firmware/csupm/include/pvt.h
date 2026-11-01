/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The 13 PVT (process, voltage, temperature) sensors at 0xa0070000 (CIX's
 * dev_pvt.c, app_pvt.c, pvt_event.c).
 */
#ifndef PVT_H
#define PVT_H

#include "pm.h"

#define PVT_N			13

/* CIX's DEV_PVT_DATA_TYPE: the three readings of a sensor */
enum dev_pvt_data_type { PVT_DATA_MAX, PVT_DATA_MIN, PVT_DATA_LATEST };
/* CIX's DEV_WORK_MODE */
enum dev_work_mode {
	PVT_MODE_TEMP, PVT_MODE_VOLTAGE, PVT_MODE_P0, PVT_MODE_P1, PVT_MODE_P2, PVT_MODE_OFF,
};
/* CIX's DEV_PVT_CONTROL_MODE: the thresholds (in degrees C), the filter,
 * the weight, the common low-temperature threshold and its enable */
enum dev_pvt_control_mode {
	PVT_CTRL_HT_WARNING, PVT_CTRL_HT_SAFE, PVT_CTRL_LT_WARNING, PVT_CTRL_LT_SAFE,
	PVT_CTRL_FILTER, PVT_CTRL_WEIGHT, PVT_CTRL_LT_ALL, PVT_CTRL_LT_ALL_EN,
};

/* CIX's sky1_pvt_init */
void pvt_init(void);
/* CIX's sky1_pvt_data_config: pm_config_pvt_t (thermal trip, weights, the
 * board's sensors) */
void sky1_pvt_data_config(const uint8_t *cfg);
void sky1_pvt_fixed_polling_enable(void);
void sky1_pvt_fixed_polling_disable(void);
float sky1_pvt_get_value(uint32_t inst);
/* SCMI's sensor numbering: 1-13 the sensors, 14 the mean of 2 and 3 (GPU) */
int pvt_temperature(unsigned int id, float *celsius);

void dev_pvt_trim_set(uint32_t inst, uint32_t trimg, uint32_t trimo);
bool dev_pvt_set_control_mode(uint32_t inst, enum dev_pvt_control_mode mode, int32_t data);
void dev_pvt_enable_sensor(uint32_t inst);
void dev_pvt_set_mode(uint32_t inst, enum dev_work_mode mode);
enum dev_work_mode dev_pvt_get_mode(uint32_t inst);
void dev_pvt_reset_max_min_threshold(uint32_t inst);
uint32_t dev_pvt_get_raw_data(uint32_t inst, enum dev_pvt_data_type type);
float dev_pvt_get_data(uint32_t inst, enum dev_pvt_data_type type);
float dev_pvt_get_average_data(enum dev_pvt_data_type type);
/* the high-temperature interrupts (aggregator lines 28 and 29) */
void pvt_safe_irq(void);
void pvt_warning_irq(void);

/* CIX's pvt_event.c: the sensors the AP's performance stream wants */
struct pvt_event {
	uint32_t data_min, data_max, data_avg;
};
void pvt_event_stream_setup(uint16_t pvt_sensor_mask);
void pvt_event_stream_clear(void);
uint8_t pvt_event_fetch(struct pvt_event *event, uint8_t prev_idx);

#endif
