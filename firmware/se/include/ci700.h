/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* CI-700 interconnect (driver/ci700.c) */
#ifndef CI700_H
#define CI700_H

#include <stdint.h>

void drv_ci700_init_pre(void);
void drv_ci700_init(void);
uint32_t drv_ci700_update_mem_size(void);
void drv_ci700_slc_flush(uint64_t start, uint64_t size);
void drv_ci700_flush_mte_tagcache(void);

#endif
