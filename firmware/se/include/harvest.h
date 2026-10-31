/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Fused-off ("harvested") cores and blocks (framework/harvest.c) */
#ifndef HARVEST_H
#define HARVEST_H

#include <stdint.h>

typedef enum {
	HVST_TYPE_CPU_FREQ, HVST_TYPE_GPU, HVST_TYPE_VPU, HVST_TYPE_CPU_HW, HVST_TYPE_REMAIN_IP_HW,
} hvst_t;

uint32_t fw_harvest(hvst_t t);
uint32_t fw_get_cpu_mask_by_harvest(void);	/* bit n: AP core n present */

#endif
