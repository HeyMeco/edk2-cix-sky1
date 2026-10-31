/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The 20 temperature sensors SCMI lists (CIX's g_sensor_name and
 * get_reg_sensor_element_table): 13 SoC PVT sensors, then the board's.
 */
#include <mod_soc_sensor.h>

#include <fwk_element.h>
#include <fwk_module.h>

#include "scp_config.h"

#define PVT(n, i)	[i] = { .name = n, .data = &(const struct mod_soc_sensor_config){ .pvt = i + 1 } }
#define BOARD(n, i, b)	[i] = { .name = n, .data = &(const struct mod_soc_sensor_config){ .board = b } }

const struct fwk_element scp_sensor_elements[SCP_SENSOR_COUNT + 1] = {
	PVT("VPU", 0), PVT("GPU_btm", 1), PVT("GPU_top", 2), PVT("SOC_BRC", 3),
	PVT("DDR_btm", 4), PVT("DDR_top", 5), PVT("CI", 6), PVT("NPU", 7),
	PVT("CPU_M1", 8), PVT("CPU_B1", 9), PVT("CPU_M0", 10), PVT("CPU_B0", 11),
	PVT("SOC_TRC", 12), PVT("GPU_AVE", 13),
	BOARD("PCB_SOC", 14, 0), BOARD("PCB_BRDG", 15, 1), BOARD("PCB_CHG", 16, 2),
	BOARD("PCB_DRAM", 17, 3), BOARD("PCB_USB", 18, 4), BOARD("PCB_PMIC", 19, 5),
	[SCP_SENSOR_COUNT] = { 0 },
};

const struct fwk_module_config config_soc_sensor = {
	.elements = FWK_MODULE_STATIC_ELEMENTS_PTR(scp_sensor_elements),
};
