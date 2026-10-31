/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* SRAM repair from eFuse data (hal/memory.c) */
#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>

typedef enum {
	MEMR_GROUP_ID_CPU = 1, MEMR_GROUP_ID_CI700, MEMR_GROUP_ID_DPU, MEMR_GROUP_ID_GPU,
	MEMR_GROUP_ID_NPU, MEMR_GROUP_ID_AUDIO, MEMR_GROUP_ID_ISP, MEMR_GROUP_ID_VPU,
	MEMR_GROUP_ID_CSU_PM, MEMR_GROUP_ID_CSU_SE, MEMR_GROUP_ID_MMHUB, MEMR_GROUP_ID_PCIE,
	MEMR_GROUP_ID_SENSOR, MEMR_GROUP_ID_DP, MEMR_GROUP_ID_GMAC, MEMR_GROUP_ID_MIPI,
	MEMR_GROUP_ID_SMMU_MM, MEMR_GROUP_ID_SMMU_PCIE, MEMR_GROUP_ID_SMMU_SYS, MEMR_GROUP_ID_USB,
	MEMR_GROUP_ID_FCH, MEMR_GROUP_ID_MAX = 21,
} MEMR_GROUP_ID;

uint32_t hal_mem_init(void);
uint32_t hal_memr_by_id(MEMR_GROUP_ID id, uint32_t index);
uint32_t hal_memr_distribute_by_id(MEMR_GROUP_ID id, uint32_t *valid);
uint32_t fw_memr_init(void);
uint32_t fw_memr_dbs2(void);
uint32_t fw_memr_str(void);

#endif
