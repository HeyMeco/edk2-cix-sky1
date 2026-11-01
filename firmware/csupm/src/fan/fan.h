/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Fans (CIX's Source/common/drivers/dev_fan.c) */
#ifndef FAN_H
#define FAN_H

#include "pm.h"

#define FAN_N			3

/* CIX's FAN_CTRL_POLICY: by temperature with one of three tables (the
 * modes Linux picks: 1, 2 and 0), the user's table, a fixed speed, a
 * fixed duty */
enum fan_ctrl_policy {
	FAN_CTRL_POLICY_TEMP, FAN_CTRL_POLICY_PERF, FAN_CTRL_POLICY_QUIET, FAN_CTRL_POLICY_USER,
	FAN_CTRL_POLICY_RPM, FAN_CTRL_POLICY_DUTY, FAN_CTRL_POLICY_MAX,
};

/* CIX's fan_state_t (PM message 0x801) */
struct fan_state {
	int32_t temprature, expect_rpm, real_rpm, duty, policy;
};

/* CIX's fan_data_config (do_pm_config): pm_config_fan_t fan_config[3] */
void fan_data_config(const uint8_t *config_array);
/* pm_ready and after resume: the pads, then each configured fan's PWM and
 * tachometer */
void fan_ctrl_start(void);
/* before suspend: their counters freed */
void fan_ctrl_suspend(void);
int32_t dev_fan_set_ctrl_policy(uint32_t fan_id, enum fan_ctrl_policy policy, uint32_t data1,
				uint32_t data2);
bool fan_config_user_table(uint32_t fan_id, uint32_t user_items, const uint32_t *item_ptr,
			   uint32_t ops);
void fan_stat_get(uint32_t fan_id, struct fan_state *state, bool dump);

#endif
