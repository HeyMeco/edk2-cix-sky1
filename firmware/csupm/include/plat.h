/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * What the PM knows about the chip and board it runs on (CIX's
 * app_plat_id.c): the package straps, the PM strap the SE leaves behind,
 * the boot core.  Read once at power-on (plat_init).
 */
#ifndef PLAT_H
#define PLAT_H

#include "pm.h"

/* Package straps (SoC 0x05040100/4): [18:17] package; plat_id_4 bytes 1-3
 * say which subsystems are present */
extern uint32_t plat_id_0, plat_id_4;
/* Set when the straps read 0xe8e8e8e8: an instruction-set simulator */
extern uint32_t plat_id_iss;
/* The PM strap the SE writes before starting the PM (0xa0000800, 64 bits:
 * checksum, board, V/F table version, DVFS mode, PVT average at [41:32]) */
extern bool boot_strap_valid;
extern uint64_t boot_strap;
/* The boot core: core_info[1] >> 4 is its DPM element */
extern bool core_info_valid;
extern uint8_t core_info[4];

void plat_init(void);

#endif
