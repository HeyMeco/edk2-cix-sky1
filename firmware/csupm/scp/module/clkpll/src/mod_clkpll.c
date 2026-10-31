/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * CIX's mod_clkpll.c: the clock module's driver for the clock tree.
 *
 * CIX's build queues rate changes to a FreeRTOS job and answers the clock
 * module later (its dbg_clkpll_testcase is set); the open firmware takes
 * CIX's other path and changes the rate in line (a PLL lock takes at most
 * a millisecond).
 */
#include <mod_clkpll.h>
#include <mod_clock.h>

#include <fwk_id.h>
#include <fwk_module.h>
#include <fwk_status.h>

#include "clk/clk.h"

/* Clocks CIX lets the OS "turn off" even when the tree keeps them on */
static const uint16_t turnoff_ok[] = { 0x6e, 0xc9 };

static TREE_NODE *node(fwk_id_t clock_id)
{
	CLK_TREE_STRUCT *st = clk_nodes_scmi_idx_to_ctrl_struct(fwk_id_get_element_idx(clock_id));

	return &st->clk_tree_node;
}

static int clkpll_set_rate(fwk_id_t clock_id, uint64_t rate, enum mod_clock_round_mode round_mode)
{
	(void)round_mode;
	return clk_api_set_freq(node(clock_id), (uint32_t)rate) ? FWK_SUCCESS : FWK_E_DEVICE;
}

static int clkpll_get_rate(fwk_id_t clock_id, uint64_t *rate)
{
	*rate = clk_api_get_freq(node(clock_id), true);
	return FWK_SUCCESS;
}

static int clkpll_get_rate_from_index(fwk_id_t clock_id, unsigned int rate_index, uint64_t *rate)
{
	TREE_NODE *n = node(clock_id);

	if (rate_index >= clk_tree_get_rates_num(n))
		return FWK_E_PARAM;
	*rate = clk_tree_get_rate_from_index(n, rate_index);
	return FWK_SUCCESS;
}

static int clkpll_set_state(fwk_id_t clock_id, enum mod_clock_state state)
{
	uint32_t ctrl;

	if (clk_api_set_state(node(clock_id), state != MOD_CLOCK_STATE_STOPPED))
		return FWK_SUCCESS;
	if (state != MOD_CLOCK_STATE_STOPPED)
		return FWK_E_DEVICE;
	ctrl = clk_nodes_scmi_idx_to_ctrl_idx(fwk_id_get_element_idx(clock_id));
	for (unsigned int i = 0; i < FWK_ARRAY_SIZE(turnoff_ok); i++) {
		if (turnoff_ok[i] == ctrl)
			return FWK_SUCCESS;
	}
	return FWK_E_DEVICE;
}

static int clkpll_get_state(fwk_id_t clock_id, enum mod_clock_state *state)
{
	*state = clk_api_get_state(node(clock_id)) ? MOD_CLOCK_STATE_RUNNING :
						      MOD_CLOCK_STATE_STOPPED;
	return FWK_SUCCESS;
}

static int clkpll_get_range(fwk_id_t clock_id, struct mod_clock_range *range)
{
	uint32_t is_continuous, min, max, step, count;

	if (!clk_tree_get_rate_info(node(clock_id), &is_continuous, &min, &max, &step, &count))
		return FWK_E_DEVICE;
	range->rate_type = is_continuous ? MOD_CLOCK_RATE_TYPE_CONTINUOUS : MOD_CLOCK_RATE_TYPE_DISCRETE;
	range->min = min;
	range->max = max;
	range->step = step;
	range->rate_count = count;
	return FWK_SUCCESS;
}

static int clkpll_process_power_transition(fwk_id_t clock_id, unsigned int state)
{
	(void)clock_id;
	(void)state;
	return FWK_SUCCESS;
}

static const struct mod_clock_drv_api driver_api = {
	.name = "clkpll",
	.set_rate = clkpll_set_rate,
	.get_rate = clkpll_get_rate,
	.get_rate_from_index = clkpll_get_rate_from_index,
	.set_state = clkpll_set_state,
	.get_state = clkpll_get_state,
	.get_range = clkpll_get_range,
	.process_power_transition = clkpll_process_power_transition,
};

static int clkpll_init(fwk_id_t module_id, unsigned int count, const void *data)
{
	(void)module_id;
	(void)data;
	return count <= clk_nodes_get_scmi_element_number() ? FWK_SUCCESS : FWK_E_PARAM;
}

static int clkpll_element_init(fwk_id_t id, unsigned int sub, const void *data)
{
	(void)id;
	(void)sub;
	(void)data;
	return FWK_SUCCESS;
}

static int clkpll_process_bind_request(fwk_id_t source_id, fwk_id_t target_id,
				       fwk_id_t api_id, const void **api)
{
	(void)source_id;
	(void)target_id;
	if (fwk_id_get_api_idx(api_id) != MOD_CLKPLL_API_IDX_DRIVER)
		return FWK_E_PARAM;
	*api = &driver_api;
	return FWK_SUCCESS;
}

const struct fwk_module module_clkpll = {
	.type = FWK_MODULE_TYPE_DRIVER,
	.api_count = MOD_CLKPLL_API_IDX_COUNT,
	.init = clkpll_init,
	.element_init = clkpll_element_init,
	.process_bind_request = clkpll_process_bind_request,
};
