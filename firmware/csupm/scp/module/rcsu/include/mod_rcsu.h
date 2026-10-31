/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Power-domain driver for the subsystems' RCSU power controls (CIX's
 * extmodules/rcsu): a domain is a control register whose power-on bits
 * are set after the domain's SRAM repair, and whose power-down bits are
 * cleared to turn it off.  The root (SYSTOP) has no register and is
 * always on; asked to shut the system down or reset it, the driver hands
 * the request to the PM's system power sequence.
 */
#ifndef MOD_RCSU_H
#define MOD_RCSU_H

#include <mod_power_domain.h>

#include <stdbool.h>
#include <stdint.h>

struct mod_rcsu {
	uintptr_t reg_base;		/* control register (PM address), 0: none */
	uint32_t power_on_mask;		/* set to power on */
	uint32_t power_down_mask;	/* cleared to power off */
	uint32_t mem_repair_enable_bit;
	uintptr_t mem_repair_base;	/* SRAM repair controller (PM address) */
	unsigned int irq;
};

struct mod_rcsu_pd_config {
	enum mod_pd_type pd_type;
	bool has_ppu;
	struct mod_rcsu rcsu;
	bool default_power_on;
	bool always_on;			/* SYSTOP */
	bool isp_strap;			/* ISP0: keep the ISP1 straps across power-off */
};

enum mod_rcsu_api_idx {
	MOD_RCSU_API_IDX_PD_DRIVER,
	MOD_RCSU_API_IDX_COUNT,
};

/* The system power sequence (src/power.c, src/hsm): a shutdown or reset
 * request sets its target, as CIX's trigger_SD and trigger_RST */
void rcsu_system_shutdown_request(enum mod_pd_system_shutdown type);

#endif
