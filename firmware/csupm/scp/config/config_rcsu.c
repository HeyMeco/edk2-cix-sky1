/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* The RCSU power domains (CIX's config_rcsu.c) */
#include <mod_rcsu.h>

#include <fwk_element.h>
#include <fwk_module.h>

#include "scp_config.h"

static const struct fwk_element elements[SCP_PD_COUNT + 1] = {
	[SCP_PD_ISP0] = { .name = "RCSU_ISP0", .data = &(const struct mod_rcsu_pd_config){
		.pd_type = MOD_PD_TYPE_DEVICE,
		.rcsu = {
			.reg_base = 0x54330020,
			.power_on_mask = 0xffd,
			.power_down_mask = 0x1,
			.mem_repair_enable_bit = 1,
			.mem_repair_base = 0x54330000,
		},
		.isp_strap = true,
	} },
	[SCP_PD_GPUTOP] = { .name = "RCSU_GPU", .data = &(const struct mod_rcsu_pd_config){
		.pd_type = MOD_PD_TYPE_DEVICE,
		.rcsu = {
			.reg_base = 0x55000218,
			.power_on_mask = 0x1ffc,
			.power_down_mask = 0x1000,
			.mem_repair_enable_bit = 1,
			.mem_repair_base = 0x55000000,
		},
	} },
	[SCP_PD_SYSTOP] = { .name = "RCSU_SYSTOP", .data = &(const struct mod_rcsu_pd_config){
		.pd_type = MOD_PD_TYPE_DEVICE,
		.always_on = true,
	} },
	[SCP_PD_COUNT] = { 0 },
};

const struct fwk_module_config config_rcsu = {
	.elements = FWK_MODULE_STATIC_ELEMENTS_PTR(elements),
};
