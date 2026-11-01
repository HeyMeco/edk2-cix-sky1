/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Register offsets of the Cadence (Denali) LPDDR5 controller, PI and PHY as
 * the DDR driver's MemDdrcRegRead/MemDdrcRegWrite take them: register n of
 * each block at 4n (controller 0, PI 0x2000, PHY 0x4000).  CIX's register types (DENALI_CTL_n, DENALI_PI_n,
 * DENALI_PHY_n, in mem_types.h) give the fields.
 */
#ifndef MEM_REGS_H
#define MEM_REGS_H

#define DENALI_CTL(n)	((n) * 4)
#define DENALI_PI(n)	(0x2000 + (n) * 4)
#define DENALI_PHY(n)	(0x4000 + (n) * 4)
/* the PHY's data slices repeat its first 256 registers */
#define PHY_SLICE(s)	((s) * 0x400)

/* MEMORY_INIT_S.DdrType */
#define MEM_LPDDR4X	0
#define MEM_LPDDR5	1

#endif
