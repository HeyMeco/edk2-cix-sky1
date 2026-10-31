/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * RCSU power-domain driver (CIX's mod_rcsu.c).
 */
#include <mod_power_domain.h>
#include <mod_rcsu.h>

#include <fwk_id.h>
#include <fwk_mm.h>
#include <fwk_module.h>
#include <fwk_module_idx.h>
#include <fwk_status.h>

#include "lcru.h"

struct rcsu_pd_ctx {
	const struct mod_rcsu_pd_config *config;
	fwk_id_t bound_id;		/* the power domain */
	unsigned int state;
	const struct mod_pd_driver_input_api *pd_api;
};

static struct rcsu_pd_ctx *rcsu_ctx;

/* ISP1's strap registers are lost when ISP0 powers off */
#define ISP1_STRAP0		0x54350300u
#define ISP1_STRAP1		0x54350304u

static struct {
	uint32_t strap[2];
	bool saved;
} isp_strap;

static void isp_strap_save_restore(bool restore)
{
	if (!isp_strap.saved) {
		isp_strap.strap[0] = REG32(ISP1_STRAP0);
		isp_strap.strap[1] = REG32(ISP1_STRAP1);
		isp_strap.saved = true;
	}
	if (restore) {
		REG32(ISP1_STRAP0) = isp_strap.strap[0];
		REG32(ISP1_STRAP1) = isp_strap.strap[1];
	} else {
		isp_strap.strap[0] = REG32(ISP1_STRAP0);
		isp_strap.strap[1] = REG32(ISP1_STRAP1);
	}
}

/* Wait until (*reg & mask) == value, at most timeout_us */
static bool poll(uintptr_t reg, uint32_t mask, uint32_t value, uint32_t timeout_us)
{
	uint32_t start = ccount(), limit = core_hz / 1000000 * timeout_us;

	while ((REG32(reg) & mask) != (value & mask)) {
		if (ccount() - start > limit)
			return false;
	}
	return true;
}

/* SRAM repair: wait for the controller to be idle, start it, wait for done */
#define REPAIR_DONE		0x10
#define REPAIR_START		0x14
#define REPAIR_BUSY		0x18

static void mem_repair(uintptr_t base, uint32_t enable)
{
	(void)poll(base + REPAIR_BUSY, 0xffff0000, 0, 100000);
	REG32(base + REPAIR_START) = enable;
	(void)poll(base + REPAIR_DONE, 6, 6, 800000);
	REG32(base + REPAIR_START) = 0;
}

static void set_power_mode(struct rcsu_pd_ctx *c, bool on)
{
	const struct mod_rcsu *r = &c->config->rcsu;

	if (r->reg_base == 0)
		return;
	if (on) {
		mem_repair(r->mem_repair_base, r->mem_repair_enable_bit);
		REG32(r->reg_base) |= r->power_on_mask;
		if (c->config->isp_strap)
			isp_strap_save_restore(true);
	} else {
		if (c->config->isp_strap)
			isp_strap_save_restore(false);
		REG32(r->reg_base) &= ~r->power_down_mask;
	}
}

static int rcsu_pd_set_state(fwk_id_t id, unsigned int state)
{
	struct rcsu_pd_ctx *c = &rcsu_ctx[fwk_id_get_element_idx(id)];

	if (state != MOD_PD_STATE_ON && state != MOD_PD_STATE_OFF)
		return FWK_E_PARAM;
	c->state = state;
	set_power_mode(c, state == MOD_PD_STATE_ON);
	return c->pd_api->report_power_state_transition(c->bound_id, state);
}

static int rcsu_pd_get_state(fwk_id_t id, unsigned int *state)
{
	struct rcsu_pd_ctx *c = &rcsu_ctx[fwk_id_get_element_idx(id)];

	*state = c->config->always_on ? MOD_PD_STATE_ON : c->state;
	return FWK_SUCCESS;
}

static int rcsu_pd_reset(fwk_id_t id)
{
	(void)id;
	return FWK_SUCCESS;
}

static int rcsu_pd_shutdown(fwk_id_t id, enum mod_pd_system_shutdown type)
{
	(void)id;
	switch (type) {
	case MOD_PD_SYSTEM_SHUTDOWN:
	case MOD_PD_SYSTEM_FORCED_SHUTDOWN:
	case MOD_PD_SYSTEM_COLD_RESET:
	case MOD_PD_SYSTEM_WARM_RESET:
		rcsu_system_shutdown_request(type);
		return FWK_SUCCESS;
	default:
		return FWK_E_SUPPORT;
	}
}

static const struct mod_pd_driver_api pd_driver = {
	.set_state = rcsu_pd_set_state,
	.get_state = rcsu_pd_get_state,
	.reset = rcsu_pd_reset,
	.shutdown = rcsu_pd_shutdown,
};

static int rcsu_init(fwk_id_t module_id, unsigned int count, const void *data)
{
	(void)module_id;
	(void)data;
	rcsu_ctx = fwk_mm_calloc(count, sizeof(rcsu_ctx[0]));
	return FWK_SUCCESS;
}

static int rcsu_element_init(fwk_id_t id, unsigned int sub, const void *data)
{
	struct rcsu_pd_ctx *c = &rcsu_ctx[fwk_id_get_element_idx(id)];

	(void)sub;
	if (data == NULL)
		return FWK_E_PARAM;
	c->config = data;
	c->bound_id = FWK_ID_NONE;
	c->state = c->config->default_power_on ? MOD_PD_STATE_ON : MOD_PD_STATE_OFF;
	return FWK_SUCCESS;
}

static int rcsu_bind(fwk_id_t id, unsigned int round)
{
	struct rcsu_pd_ctx *c;

	if (round == 0 || !fwk_id_is_type(id, FWK_ID_TYPE_ELEMENT))
		return FWK_SUCCESS;
	c = &rcsu_ctx[fwk_id_get_element_idx(id)];
	if (fwk_id_is_equal(c->bound_id, FWK_ID_NONE))
		return FWK_SUCCESS;
	if (fwk_id_get_module_idx(c->bound_id) != FWK_MODULE_IDX_POWER_DOMAIN)
		return FWK_E_ACCESS;
	return fwk_module_bind(c->bound_id, mod_pd_api_id_driver_input, &c->pd_api);
}

static int rcsu_process_bind_request(fwk_id_t source_id, fwk_id_t target_id,
				     fwk_id_t api_id, const void **api)
{
	struct rcsu_pd_ctx *c;

	if (fwk_id_get_api_idx(api_id) != MOD_RCSU_API_IDX_PD_DRIVER ||
	    !fwk_module_is_valid_element_id(target_id))
		return FWK_E_PARAM;
	c = &rcsu_ctx[fwk_id_get_element_idx(target_id)];
	if (fwk_id_get_module_idx(source_id) == FWK_MODULE_IDX_POWER_DOMAIN)
		c->bound_id = source_id;
	*api = &pd_driver;
	return FWK_SUCCESS;
}

const struct fwk_module module_rcsu = {
	.type = FWK_MODULE_TYPE_DRIVER,
	.api_count = MOD_RCSU_API_IDX_COUNT,
	.init = rcsu_init,
	.element_init = rcsu_element_init,
	.bind = rcsu_bind,
	.process_bind_request = rcsu_process_bind_request,
};
