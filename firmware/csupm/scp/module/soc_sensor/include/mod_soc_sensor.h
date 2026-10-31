/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Temperature sensors for the sensor module (CIX's reg_sensor, which CIX
 * changed to read its PVT sensors): a SoC sensor by its PVT index, or a
 * board temperature someone else reports.
 */
#ifndef MOD_SOC_SENSOR_H
#define MOD_SOC_SENSOR_H

#include <mod_sensor.h>

#include <stdint.h>

#define SOC_SENSOR_PVT_LAST	14	/* PVT indices 1-14 */

struct mod_soc_sensor_config {
	unsigned int pvt;		/* 1-14, or 0 */
	unsigned int board;		/* board temperature index, if pvt is 0 */
};

enum mod_soc_sensor_api_idx {
	MOD_SOC_SENSOR_API_IDX_DRIVER,
	MOD_SOC_SENSOR_API_IDX_COUNT,
};

/* Board temperatures in degrees C (CIX's g_board_temp), set when the board
 * reports them */
#define SOC_SENSOR_BOARD_N	6
extern float soc_sensor_board_temp[SOC_SENSOR_BOARD_N];

#endif
