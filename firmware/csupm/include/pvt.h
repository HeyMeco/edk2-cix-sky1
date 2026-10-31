/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The 13 PVT (process, voltage, temperature) sensors at 0xa0070000 (CIX's
 * dev_pvt.c and app_pvt.c).
 */
#ifndef PVT_H
#define PVT_H

#include "pm.h"

#define PVT_N			13

void pvt_init(void);
/* SCMI's sensor numbering: 1-13 the sensors, 14 the mean of 2 and 3 (GPU) */
int pvt_temperature(unsigned int id, float *celsius);

#endif
