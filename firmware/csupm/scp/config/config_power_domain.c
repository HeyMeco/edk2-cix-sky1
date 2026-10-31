/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Power domains as CIX's scp_sky1_emu_core.c declares them: ISP0 and
 * GPUTOP, which SCMI exposes, under SYSTOP.
 *
 * CIX's table gives both ISP0 and GPUTOP parent index 0 (ISP0 itself)
 * rather than SYSTOP, so the framework never lets either power on: CIX's
 * firmware answers DENIED to SCMI POWER_STATE_SET ON for both, and the OS
 * runs with that.  This table keeps CIX's parents until powering them on
 * has been tried on a board.
 */
#include <mod_power_domain.h>
#include <mod_rcsu.h>

#include <fwk_element.h>
#include <fwk_id.h>
#include <fwk_macros.h>
#include <fwk_module.h>
#include <fwk_module_idx.h>

#include "scp_config.h"

/* Allowed states of a domain by state of its parent (OFF, ON, ...) */
static const uint32_t device_allowed[] = {
	[MOD_PD_STATE_OFF] = MOD_PD_STATE_OFF_MASK,
	[MOD_PD_STATE_ON] = MOD_PD_STATE_OFF_MASK | MOD_PD_STATE_ON_MASK,
	[MOD_PD_STATE_SLEEP] = 0,
	[3] = MOD_PD_STATE_OFF_MASK,
	[4] = MOD_PD_STATE_OFF_MASK,
};

static const uint32_t systop_allowed[] = {
	[MOD_PD_STATE_OFF] = 0x1b,
};

#define PD(n, i, parent, type, allowed)						\
	[i] = { .name = n, .data = &(const struct mod_power_domain_element_config){	\
		.parent_idx = parent,							\
		.driver_id = FWK_ID_ELEMENT_INIT(FWK_MODULE_IDX_RCSU, i),		\
		.api_id = FWK_ID_API_INIT(FWK_MODULE_IDX_RCSU, MOD_RCSU_API_IDX_PD_DRIVER), \
		.attributes.pd_type = type,						\
		.allowed_state_mask_table = allowed,					\
		.allowed_state_mask_table_size = FWK_ARRAY_SIZE(allowed),		\
	} }

static const struct fwk_element elements[SCP_PD_COUNT + 1] = {
	PD("ISP0", SCP_PD_ISP0, SCP_PD_ISP0, MOD_PD_TYPE_DEVICE, device_allowed),
	PD("GPUTOP", SCP_PD_GPUTOP, SCP_PD_ISP0, MOD_PD_TYPE_DEVICE, device_allowed),
	PD("SYSTOP", SCP_PD_SYSTOP, UINT32_MAX, MOD_PD_TYPE_SYSTEM, systop_allowed),
	[SCP_PD_COUNT] = { 0 },
};

const struct fwk_module_config config_power_domain = {
	.data = &(const struct mod_power_domain_config){ 0 },
	.elements = FWK_MODULE_STATIC_ELEMENTS_PTR(elements),
};
