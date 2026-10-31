/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Voltage/frequency table selection from the die's PVT data (driver/pvt.c) */
#ifndef PVT_H
#define PVT_H

#include <stdint.h>

typedef struct {
	uint16_t vf_m_ver, vf_s_ver;
	uint16_t wafer, x_axis, y_axis;
	uint16_t average;
} PVT_INFO_S;

uint32_t drv_pvt_get_vf_ver(uint32_t *major, uint32_t *minor);
const PVT_INFO_S *drv_pvt_get_vf_info(void);

#endif
