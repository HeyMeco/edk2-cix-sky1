/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * CIX's DDR driver, linked in as it is (ddr/ddr_island.S, generated from
 * CIX's firmware by scripts/open/se_ddr_island.py): its entry points.
 */
#ifndef DDR_H
#define DDR_H

#include <stdint.h>

enum { DDR_BIT_DROP_2_BITS, DDR_BIT_DROP_1_BIT_ON_BIT7, DDR_BIT_DROP_1_BIT_ON_BIT8, DDR_BIT_DROP_NONE };

uint32_t drv_ddr_init(void);
uint32_t drv_ddr_postci(void);
uint32_t drv_ddr_get_mem_size(void);		/* MiB */
uint32_t drv_ddr_get_ch_num(void);
void drv_ddr_set_avail_mem_size(uint32_t mb);
uint8_t MemGetBootEnv(void);
void drv_ddr_assert(void);

#endif
