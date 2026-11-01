/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Break points (CIX driver/ddr/src/Feat/BP/MemBreakPoint.c).  Translated
 * from CIX's SE firmware debug build.
 */
#include "mem_types.h"

/* MemBpCheckpoint: CIX's only prints the check point (log class 4,
 * "BP[%08x]"); debug printing is left out here. */
void MemBpCheckpoint(uint32_t breakpoint)
{
}
