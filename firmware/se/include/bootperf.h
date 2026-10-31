/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef BOOTPERF_H
#define BOOTPERF_H

typedef enum {
	BROM_PHASE, SE_PHASE, PM_PHASE, PBL_PHASE, TFA_PHASE, TEE_PHASE, BLOADER_PHASE, GRUB_PHASE,
} FW_BOOT_PHASE;

typedef enum { RECORD_START, RECORD_END } RECORD_POINT;

void cix_set_boot_phase(FW_BOOT_PHASE phase, RECORD_POINT point);
void cix_boot_perf_init(FW_BOOT_PHASE phase);
void cix_boot_perf_uninit(FW_BOOT_PHASE phase);

#endif
