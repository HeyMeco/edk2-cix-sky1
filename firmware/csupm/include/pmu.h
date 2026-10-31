/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Performance counters the PM samples for bandwidth and activity
 * monitoring: the NI-700 hubs' PMUs, the CI-700 mesh's DTC and XP
 * watchpoints, the AP's AMU and MPMM throttling gears (CIX's perf streams).
 */
#ifndef PMU_H
#define PMU_H

#include "pm.h"

void pmu_reset_all(void);
void mpmm_set_gear(uint32_t mask, uint32_t gear, bool enable);

#endif
