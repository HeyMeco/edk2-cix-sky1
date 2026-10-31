/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The CPU clusters' ACPI CPPC registers, through a PCC subspace in DRAM
 * (CIX's Source/common/pcc/pcc.c), and what it needs of the AP's cores
 * (Source/common/soc/ap/ap_event.c).
 */
#ifndef PCC_H
#define PCC_H

#include "pm.h"

/* The subspace (edk2-platforms' AcpiPlatform.h PCC_CPPC_BASE) */
#define PCC_DRAM_BASE		0x83bf1280u

/* At run time (after the mailboxes): the subspace, the tables */
void pcc_init(void);
/* A doorbell on mailbox 0's fast channel 1: command 0 updates the
 * counters and levels, 1 takes the desired levels (the highest per cluster)
 * to the OS's performance fast channels */
void pcc_cmd_handler(void);

/* ap_event.c: whether DPM element (core) ele has its clock, and its AMU
 * counter cnt (0 core cycles, 1 constant cycles, ...) */
bool ap_is_clk_on(uint32_t ele);
uint64_t amu_read(uint32_t core_idx, uint32_t cnt);

#endif
