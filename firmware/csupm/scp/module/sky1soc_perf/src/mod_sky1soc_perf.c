/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * CIX's mod_sky1soc_perf.c (extmodules/plugin_sky1soc): a performance
 * plugin of scmi_perf (full view: every DVFS domain at once) that drives
 * the CPU clusters ("AP domains") and the DSU itself instead of through
 * dvfs.  On each fast-channel pass it caps the requested levels to the
 * DPM's TDP frequencies, picks the OPPs, and queues the voltage and
 * frequency changes in an order that keeps every cluster with cores online
 * within 200 mV of the DSU (frequencies down first, then the DSU's voltage
 * a gear at a time with the clusters', then frequencies up); a state
 * machine runs the queue, waiting at barriers for the PSU and clock
 * answers.  The DSU's frequency is its PLL's second post-divider.
 *
 * Where CIX blocks the AP's message task until the queue has run
 * (sky1soc_notify_ap_core_change), the open firmware, which has no tasks,
 * returns once the change is queued.  CIX's clock driver answers rate
 * changes later, with an event that reports the cluster's new level to
 * scmi_perf; the open firmware's answers at once, and the report is made
 * then.  CIX's logging is left out.
 *
 * Not CIX's: a voltage (or clock) answer that is an error ends the pass
 * at the barrier, so that no clock goes up on a supply that was not set
 * (the open firmware's PMIC writes give up on a dead bus; CIX's never do).
 */
#include <mod_clock.h>
#include <mod_dvfs.h>
#include <mod_psu.h>
#include <mod_scmi_perf.h>
#include <mod_sky1soc_perf.h>

#include <fwk_assert.h>
#include <fwk_core.h>
#include <fwk_event.h>
#include <fwk_id.h>
#include <fwk_mm.h>
#include <fwk_module.h>
#include <fwk_module_idx.h>
#include <fwk_status.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "clk/clk.h"
#include "scp_dpm.h"

/* Voltage step of the DSU and a cluster (mV) */
#define SKY1SOC_GEAR_MV		200

/* The DSU's PLL and the gate of its post-divider */
#define DSU_PLL_BASE		((void *)(uintptr_t)0xa0060500)
#define DSU_PLL_GATE		0xa006052c

/* The level a debug-forced operating point is given */
#define SPECIFIC_OPP_LEVEL	15

enum opp_search_type {
	SKY1SOC_FIND_OPP_BY_LEVEL,
	SKY1SOC_FIND_OPP_BY_FREQ,
};

enum sky1soc_op_type {
	SKY1SOC_OP_AP_FREQ,	/* a cluster's clock */
	SKY1SOC_OP_DSU_FREQ,	/* the DSU's post-divider */
	SKY1SOC_OP_VOLTAGE,	/* a supply */
};

enum sky1soc_state {
	SKY1SOC_STATE_IDLE,
	SKY1SOC_STATE_START,
	SKY1SOC_STATE_RUN,
	SKY1SOC_STATE_WAIT,	/* for answers before a barrier */
};

/* An AP domain's (or the DSU's, the last) state */
struct mod_sky1soc_dom_ctx {
	uint32_t curr_level;
	uint32_t curr_voltage;
	uint32_t curr_freq;
	uint32_t req_level;
	uint32_t req_voltage;
	uint32_t req_freq;
	uint32_t pass_level;	/* curr_level when the pass was planned */
	fwk_id_t clock_id;
	uint8_t active_ap_count;	/* cores online */
	const struct mod_psu_device_api *psu_api;
	const struct mod_clock_api *clock_api;
};

/* A queued change; at a barrier the queue waits for what came before */
struct ap_dsu_op {
	uint8_t domain_id;	/* index of the AP domain, or the DSU */
	uint8_t op_type;
	bool barrier;
	bool failed;		/* its answer was an error */
	uint32_t new_value;
	uint32_t old_value;
};

static struct mod_sky1soc_perf_ctx {
	const struct mod_sky1soc_perf_config *config;
	uint8_t gpu_top;
	uint8_t gpu_core;
	uint8_t ap_dom_begin;	/* DVFS element of the first AP domain */
	uint8_t ap_dom_count;	/* AP domains; ctx[ap_dom_count] is the DSU */
	uint8_t dsu;
	uint16_t ap_core_bitmap;	/* cores online */
	struct mod_sky1soc_dom_ctx *ctx;
	struct ap_dsu_op *ops;
	uint8_t ops_count;
	uint8_t ops_max;
	uint32_t dsu_fc_old;
	const uint32_t *dpm_opp_run;
	const uint8_t *dxs2dvfs_tbl;
	uint32_t *tdp_opp_run;	/* a copy of dpm_opp_run per pass */
	uint8_t tdp_opp_size;
	uint8_t dxs2dvfs_size;
	uint32_t dsu_pll;	/* the DSU PLL's frequency before post-divider 2, kHz */
	uint8_t state;
	uint8_t curr_idx;
	uint8_t group_idx;	/* the first change of those running */
	uint8_t op_pending_count;
	bool op_failed;		/* one of them failed: the rest is not run */
	bool fc_sched;		/* a fast-channel pass came while busy */
	bool dsu_dvfs;
	const struct mod_scmi_perf_updated_api *scmi_perf_updated_api;
	const struct mod_dvfs_domain_api *dvfs_api;
} g_ctx;

static const fwk_id_t sky1soc_event_id_run_ops =
	FWK_ID_EVENT_INIT(FWK_MODULE_IDX_SKY1SOC_PERF, MOD_SKY1SOC_PERF_EVENT_IDX_RUN_OPS);
static const fwk_id_t sky1soc_event_id_ap_core =
	FWK_ID_EVENT_INIT(FWK_MODULE_IDX_SKY1SOC_PERF, MOD_SKY1SOC_PERF_EVENT_IDX_AP_CORE);
static const fwk_id_t sky1soc_event_id_dsu_dvfs =
	FWK_ID_EVENT_INIT(FWK_MODULE_IDX_SKY1SOC_PERF, MOD_SKY1SOC_PERF_EVENT_IDX_DSU_DVFS);

static void sky1soc_perf_op_fsm(const struct fwk_event *event);

static fwk_id_t dvfs_element(unsigned int idx)
{
	return FWK_ID_ELEMENT(FWK_MODULE_IDX_DVFS, idx);
}

static struct fwk_event module_event(fwk_id_t id)
{
	return (struct fwk_event){
		.source_id = FWK_ID_MODULE_INIT(FWK_MODULE_IDX_SKY1SOC_PERF),
		.target_id = FWK_ID_MODULE_INIT(FWK_MODULE_IDX_SKY1SOC_PERF),
		.id = id,
	};
}

/* |a - b| in 16 bits, as CIX compares voltages */
static uint16_t diff16(uint32_t a, uint32_t b)
{
	uint16_t a16 = (uint16_t)a, b16 = (uint16_t)b;

	return (uint16_t)(a16 < b16 ? b16 - a16 : a16 - b16);
}

/* CIX's sky1soc_get_ap_domain_ref: the AP domain of a core */
static uint8_t sky1soc_get_ap_domain_ref(uint8_t ap_idx)
{
	if (ap_idx < 4)
		return 0;	/* little, cores 0-3 */
	if (ap_idx < 6)
		return 3;	/* mid 0 */
	if (ap_idx < 8)
		return 4;	/* mid 1 */
	return ap_idx < 10 ? 1 : 2;	/* big 0, big 1 */
}

/* CIX's sky1soc_restore_old_value: no change for this domain */
static void sky1soc_restore_old_value(const struct perf_plugins_perf_update *data,
				      uint8_t domain_id, struct mod_sky1soc_dom_ctx *ctx)
{
	ctx->req_level = ctx->curr_level;
	ctx->req_voltage = ctx->curr_voltage;
	ctx->req_freq = ctx->curr_freq;
	data->level[g_ctx.ap_dom_begin + domain_id] = ctx->curr_level;
}

/*
 * CIX's sky1soc_perf_find_opp: the first OPP of DVFS element domain_id at
 * or above val (a level or a frequency); with donot_fail, the last OPP if
 * there is none.
 */
static int sky1soc_perf_find_opp(uint8_t domain_id, uint32_t val, enum opp_search_type type,
				 struct mod_dvfs_opp *opp, bool donot_fail)
{
	fwk_id_t id = dvfs_element(domain_id);
	struct mod_dvfs_opp tmp = { 0 };
	size_t count, i;
	int status;

	status = g_ctx.dvfs_api->get_opp_count(id, &count);
	if (status != FWK_SUCCESS)
		return status;
	for (i = 0; i < count; i++) {
		status = g_ctx.dvfs_api->get_nth_opp(id, i, &tmp);
		fwk_assert(status == FWK_SUCCESS);
		if (type == SKY1SOC_FIND_OPP_BY_FREQ) {
			if (tmp.frequency >= val)
				break;
		} else if (type == SKY1SOC_FIND_OPP_BY_LEVEL) {
			if (tmp.level >= val)
				break;
		}
	}
	if (i == count && !donot_fail)
		return FWK_E_RANGE;
	*opp = tmp;
	return FWK_SUCCESS;
}

/*
 * CIX's sky1soc_add_op: queue a change and take it as done for the
 * planning.  A change waits for the earlier ones of its domain, and every
 * DSU change for all before it (the operation before it gets a barrier).
 */
static void sky1soc_add_op(uint8_t domain_id, uint8_t op_type, uint32_t op_value)
{
	struct mod_sky1soc_dom_ctx *ctx = &g_ctx.ctx[domain_id];
	uint8_t n = g_ctx.ops_count;
	struct ap_dsu_op *op = &g_ctx.ops[n];
	uint32_t *value = op_type == SKY1SOC_OP_VOLTAGE ? &ctx->curr_voltage : &ctx->curr_freq;
	bool wait = false;

	op->op_type = op_type;
	op->failed = false;
	op->new_value = op_value;
	op->domain_id = domain_id;
	op->old_value = *value;
	if (domain_id == g_ctx.ap_dom_count) {
		op->barrier = true;
		wait = true;
	} else {
		op->barrier = false;
		for (int k = n - 1; k >= 0; k--) {
			if (g_ctx.ops[k].barrier)
				break;
			if (g_ctx.ops[k].domain_id == domain_id) {
				wait = true;
				break;
			}
		}
	}
	if (wait && n != 0)
		g_ctx.ops[n - 1].barrier = true;
	g_ctx.ops_count = n + 1;
	fwk_assert(g_ctx.ops_count <= g_ctx.ops_max);
	*value = op_value;
}

/*
 * CIX's is_valid: would domain idx at new_voltage stay within a gear of
 * the DSU (or, for the DSU, of every cluster with cores online)?
 */
static bool is_valid(uint8_t idx, uint32_t new_voltage)
{
	uint8_t n = g_ctx.ap_dom_count;

	if (idx == n) {
		for (unsigned int i = 0; i < n; i++) {
			if (g_ctx.ctx[i].active_ap_count != 0 &&
			    diff16(g_ctx.ctx[i].curr_voltage, new_voltage) > SKY1SOC_GEAR_MV)
				return false;
		}
		return true;
	}
	if (g_ctx.ctx[idx].active_ap_count == 0)
		return true;
	return diff16(g_ctx.ctx[n].curr_voltage, new_voltage) <= SKY1SOC_GEAR_MV;
}

/*
 * CIX's sky1soc_gear_up_lowest: move the online cluster with the lowest
 * voltage not at its target towards it, by at most a gear above the DSU
 */
static bool sky1soc_gear_up_lowest(void)
{
	uint32_t lowest = UINT32_MAX, target;
	uint8_t n = g_ctx.ap_dom_count, sel = 0;
	struct mod_sky1soc_dom_ctx *ctx;

	for (uint8_t i = 0; i < n; i++) {
		ctx = &g_ctx.ctx[i];
		if (ctx->active_ap_count != 0 && ctx->curr_voltage < lowest &&
		    ctx->curr_voltage != ctx->req_voltage) {
			sel = i;
			lowest = ctx->curr_voltage;
		}
	}
	if (lowest == UINT32_MAX)
		return false;
	ctx = &g_ctx.ctx[sel];
	target = ctx->req_voltage;
	if (!is_valid(sel, target)) {
		target = g_ctx.ctx[n].curr_voltage + SKY1SOC_GEAR_MV;
		if (ctx->curr_voltage == target)
			return false;
		fwk_assert(is_valid(sel, target));
	}
	sky1soc_add_op(sel, SKY1SOC_OP_VOLTAGE, target);
	return true;
}

/* CIX's sky1soc_gear_down_highest: the same from the highest voltage down */
static bool sky1soc_gear_down_highest(void)
{
	uint32_t highest = 0, target;
	uint8_t n = g_ctx.ap_dom_count, sel = 0;
	struct mod_sky1soc_dom_ctx *ctx;

	for (uint8_t i = 0; i < n; i++) {
		ctx = &g_ctx.ctx[i];
		if (ctx->active_ap_count != 0 && ctx->curr_voltage > highest &&
		    ctx->curr_voltage != ctx->req_voltage) {
			sel = i;
			highest = ctx->curr_voltage;
		}
	}
	if (highest == 0)
		return false;
	ctx = &g_ctx.ctx[sel];
	target = ctx->req_voltage;
	if (!is_valid(sel, target)) {
		target = g_ctx.ctx[n].curr_voltage - SKY1SOC_GEAR_MV;
		if (ctx->curr_voltage == target)
			return false;
		fwk_assert(is_valid(sel, target));
	}
	sky1soc_add_op(sel, SKY1SOC_OP_VOLTAGE, target);
	return true;
}

/*
 * The TDP: cap each DxS domain's requested level to its highest OPP at or
 * below the DPM's frequency (or its lowest), within the adjusted limits.
 */
static void sky1soc_apply_tdp(struct perf_plugins_perf_update *data)
{
	for (uint8_t k = 0; k < g_ctx.dxs2dvfs_size; k++) {
		uint32_t freq = g_ctx.tdp_opp_run[k], best_diff = UINT32_MAX, level;
		uint8_t dom = g_ctx.dxs2dvfs_tbl[k];
		struct mod_dvfs_opp opp, best = { 0 };
		fwk_id_t id = dvfs_element(dom);
		size_t count;
		int status;

		if (freq == 0)
			continue;
		if (dom >= g_ctx.ap_dom_begin && dom < g_ctx.ap_dom_begin + g_ctx.ap_dom_count &&
		    g_ctx.ctx[dom - g_ctx.ap_dom_begin].active_ap_count == 0)
			continue;
		status = g_ctx.dvfs_api->get_opp_count(id, &count);
		fwk_assert(status == FWK_SUCCESS);
		for (size_t j = 0; j < count; j++) {
			status = g_ctx.dvfs_api->get_nth_opp(id, j, &opp);
			fwk_assert(status == FWK_SUCCESS);
			if (freq >= opp.frequency && freq - opp.frequency < best_diff) {
				best_diff = freq - opp.frequency;
				best = opp;
			}
		}
		if (best_diff == UINT32_MAX) {
			status = g_ctx.dvfs_api->get_nth_opp(id, 0, &best);
			fwk_assert(status == FWK_SUCCESS);
		}
		level = data->level[dom] < best.level ? data->level[dom] : best.level;
		if (data->adj_min_limit[dom] != 0 && data->adj_max_limit[dom] != 0) {
			if (level < data->adj_min_limit[dom])
				level = data->adj_min_limit[dom];
			if (level > data->adj_max_limit[dom])
				level = data->adj_max_limit[dom];
		}
		data->level[dom] = level;
	}
}

/* The DSU's targets, from its own level or (DSU DVFS) half the fastest cluster */
static void sky1soc_plan_dsu(struct perf_plugins_perf_update *data)
{
	uint8_t n = g_ctx.ap_dom_count;
	struct mod_sky1soc_dom_ctx *dsu = &g_ctx.ctx[n];
	struct mod_dvfs_opp opp;
	uint32_t freq = 0;
	int status;

	if (g_ctx.dsu_dvfs) {
		for (uint8_t i = 0; i < n; i++) {
			if (g_ctx.ctx[i].active_ap_count != 0 && g_ctx.ctx[i].req_freq > freq)
				freq = g_ctx.ctx[i].req_freq;
		}
		freq >>= 1;
	} else {
		freq = dsu->req_freq;
	}
	if (freq == 0 || dsu->req_level != g_ctx.dsu_fc_old) {
		g_ctx.dsu_fc_old = dsu->req_level;
		if (dsu->req_freq > freq)
			freq = dsu->req_freq;
	}
	if (g_ctx.dsu_dvfs) {
		sky1soc_restore_old_value(data, n, dsu);
		dsu->req_freq = freq;
		return;
	}
	status = sky1soc_perf_find_opp(g_ctx.dsu, freq, SKY1SOC_FIND_OPP_BY_FREQ, &opp, true);
	fwk_assert(status == FWK_SUCCESS);
	dsu->req_freq = opp.frequency;
	dsu->req_level = opp.level;
	dsu->req_voltage = opp.voltage;
	dsu->curr_level = opp.level;
	data->level[g_ctx.ap_dom_begin + n] = opp.level;
}

/* The DSU at least as high as a gear below the highest cluster, and back */
static void sky1soc_plan_voltages(void)
{
	uint8_t n = g_ctx.ap_dom_count;
	struct mod_sky1soc_dom_ctx *dsu = &g_ctx.ctx[n];
	uint32_t lo = UINT32_MAX, hi = 0;

	for (uint8_t i = 0; i < n; i++) {
		struct mod_sky1soc_dom_ctx *ctx = &g_ctx.ctx[i];

		if (ctx->active_ap_count != 0 && ctx->req_voltage > dsu->req_voltage) {
			if (ctx->req_voltage < lo)
				lo = ctx->req_voltage;
			if (ctx->req_voltage > hi)
				hi = ctx->req_voltage;
		}
	}
	if (dsu->req_voltage + SKY1SOC_GEAR_MV < hi) {
		dsu->req_voltage = hi - SKY1SOC_GEAR_MV;
		fwk_assert(diff16(lo, dsu->req_voltage) <= SKY1SOC_GEAR_MV);
	}
	for (uint8_t i = 0; i < n; i++) {
		struct mod_sky1soc_dom_ctx *ctx = &g_ctx.ctx[i];

		if (ctx->active_ap_count != 0 &&
		    ctx->req_voltage + SKY1SOC_GEAR_MV < dsu->req_voltage)
			ctx->req_voltage = dsu->req_voltage - SKY1SOC_GEAR_MV;
	}
}

/* The DSU's voltage to its target, a gear at a time with the clusters' */
static void sky1soc_queue_dsu_voltage(void)
{
	uint8_t n = g_ctx.ap_dom_count;
	struct mod_sky1soc_dom_ctx *dsu = &g_ctx.ctx[n];
	bool up = dsu->curr_voltage < dsu->req_voltage;
	uint32_t target, v;

	while (dsu->curr_voltage != dsu->req_voltage) {
		if (up) {
			while (sky1soc_gear_up_lowest())
				;
		} else {
			while (sky1soc_gear_down_highest())
				;
		}
		target = dsu->req_voltage;
		if (!is_valid(n, target)) {
			/* as far as the clusters allow (all of them, online or not) */
			v = up ? UINT32_MAX : 0;
			for (uint8_t i = 0; i < n; i++) {
				uint32_t c = g_ctx.ctx[i].curr_voltage;

				if (up ? c < v : c > v)
					v = c;
			}
			target = up ? v + SKY1SOC_GEAR_MV : v - SKY1SOC_GEAR_MV;
			fwk_assert(is_valid(n, target));
		}
		sky1soc_add_op(n, SKY1SOC_OP_VOLTAGE, target);
	}
}

/*
 * CIX's sky1soc_perf_update: plan a pass and start the state machine.
 * Answers FWK_PENDING (scmi_perf then leaves the levels to this module),
 * and while a pass runs, notes that another is wanted.
 */
static int sky1soc_perf_update(struct perf_plugins_perf_update *data)
{
	struct fwk_event start_event = module_event(sky1soc_event_id_run_ops);
	uint8_t n = g_ctx.ap_dom_count;
	int status;

	if (g_ctx.state != SKY1SOC_STATE_IDLE) {
		g_ctx.fc_sched = true;
		return FWK_PENDING;
	}
	memcpy(g_ctx.tdp_opp_run, g_ctx.dpm_opp_run, g_ctx.tdp_opp_size * sizeof(uint32_t));
	sky1soc_apply_tdp(data);

	status = fwk_put_event(&start_event);
	if (status != FWK_SUCCESS)
		return status;
	g_ctx.ops_count = 0;
	g_ctx.state = SKY1SOC_STATE_START;
	for (uint8_t i = 0; i <= n; i++)
		g_ctx.ctx[i].pass_level = g_ctx.ctx[i].curr_level;

	/* each AP domain with cores online, and the DSU: its OPP */
	for (uint8_t i = 0; i <= n; i++) {
		struct mod_sky1soc_dom_ctx *ctx = &g_ctx.ctx[i];
		uint8_t dom = g_ctx.ap_dom_begin + i;
		struct mod_dvfs_opp opp;
		uint32_t freq_khz, voltage_mv;

		if ((i < n && ctx->active_ap_count == 0) || data->level[dom] == 0) {
			sky1soc_restore_old_value(data, i, ctx);
			continue;
		}
		if (sky1soc_perf_find_opp(dom, data->level[dom], SKY1SOC_FIND_OPP_BY_LEVEL, &opp,
					  false) != FWK_SUCCESS) {
			sky1soc_restore_old_value(data, i, ctx);
			continue;
		}
		if (i != n && get_specific_operating_point(&freq_khz, &voltage_mv)) {
			opp = (struct mod_dvfs_opp){
				.level = SPECIFIC_OPP_LEVEL,
				.voltage = voltage_mv,
				.frequency = freq_khz,
				.power = 1000,
			};
		}
		ctx->req_freq = opp.frequency;
		ctx->req_level = opp.level;
		ctx->req_voltage = opp.voltage;
		ctx->curr_level = opp.level;
		data->level[dom] = opp.level;
	}
	sky1soc_plan_dsu(data);
	sky1soc_plan_voltages();

	/* frequencies down */
	for (uint8_t i = 0; i <= n; i++) {
		struct mod_sky1soc_dom_ctx *ctx = &g_ctx.ctx[i];

		if (i < n && ctx->active_ap_count == 0)
			continue;
		if (ctx->req_freq < ctx->curr_freq)
			sky1soc_add_op(i, i == n ? SKY1SOC_OP_DSU_FREQ : SKY1SOC_OP_AP_FREQ,
				       ctx->req_freq);
	}
	/* voltages */
	sky1soc_queue_dsu_voltage();
	while (sky1soc_gear_up_lowest())
		;
	while (sky1soc_gear_down_highest())
		;
	/* frequencies up */
	if (g_ctx.ctx[n].curr_freq < g_ctx.ctx[n].req_freq)
		sky1soc_add_op(n, SKY1SOC_OP_DSU_FREQ, g_ctx.ctx[n].req_freq);
	for (uint8_t i = 0; i < n; i++) {
		struct mod_sky1soc_dom_ctx *ctx = &g_ctx.ctx[i];

		if (ctx->active_ap_count != 0 && ctx->curr_freq < ctx->req_freq)
			sky1soc_add_op(i, SKY1SOC_OP_AP_FREQ, ctx->req_freq);
	}
	if (g_ctx.ops_count != 0)
		g_ctx.ops[g_ctx.ops_count - 1].barrier = true;
	return FWK_PENDING;
}

/* A cluster's clock is at its new rate: its level to scmi_perf */
static void sky1soc_report_level(uint8_t idx)
{
	g_ctx.scmi_perf_updated_api->notify_level_updated(
		dvfs_element(g_ctx.ap_dom_begin + idx), 0, g_ctx.ctx[idx].curr_level);
}

/* Run one queued change: FWK_SUCCESS, or FWK_PENDING for an answer to come */
static int sky1soc_run_op(const struct ap_dsu_op *op)
{
	struct mod_sky1soc_dom_ctx *ctx = &g_ctx.ctx[op->domain_id];
	uint32_t div;
	int status;

	switch (op->op_type) {
	case SKY1SOC_OP_DSU_FREQ:
		div = g_ctx.dsu_pll < op->new_value ? 1 : g_ctx.dsu_pll / op->new_value;
		if (div > 7)
			div = 7;
		pll_ctrl_set_POSTDIV2_with_gating(div, DSU_PLL_BASE, DSU_PLL_GATE, 1);
		return FWK_SUCCESS;
	case SKY1SOC_OP_AP_FREQ:
		status = ctx->clock_api->set_rate(ctx->clock_id,
						  (uint64_t)(op->new_value * 1000u),
						  MOD_CLOCK_ROUND_MODE_NONE);
		if (status == FWK_SUCCESS)
			sky1soc_report_level(op->domain_id);
		break;
	default:
		status = ctx->psu_api->set_voltage(g_ctx.config->ap_dom_cfg[op->domain_id].psu_id,
						   op->new_value);
		break;
	}
	fwk_assert(status == FWK_SUCCESS || status == FWK_PENDING);
	return status;
}

/*
 * Not CIX's: an answer that is an error marks its change failed; the
 * changes after the barrier are then not run (CIX's goes on: its voltage
 * writes never give up, so an error came only with the voltage read back
 * as set).  A voltage, a cluster's clock: the change running for the
 * element that answered.
 */
static void sky1soc_op_answer(fwk_id_t source_id, int status)
{
	bool found = false;

	if (status == FWK_SUCCESS)
		return;
	g_ctx.op_failed = true;
	for (uint8_t k = g_ctx.group_idx; k < g_ctx.curr_idx; k++) {
		struct ap_dsu_op *op = &g_ctx.ops[k];
		fwk_id_t id = op->op_type == SKY1SOC_OP_VOLTAGE ?
			g_ctx.config->ap_dom_cfg[op->domain_id].psu_id :
			g_ctx.ctx[op->domain_id].clock_id;

		if (op->op_type != SKY1SOC_OP_DSU_FREQ && fwk_id_is_equal(id, source_id)) {
			op->failed = true;
			found = true;
		}
	}
	/* not known whose: every voltage of those running */
	for (uint8_t k = g_ctx.group_idx; !found && k < g_ctx.curr_idx; k++) {
		if (g_ctx.ops[k].op_type == SKY1SOC_OP_VOLTAGE)
			g_ctx.ops[k].failed = true;
	}
}

/*
 * Not CIX's: the pass ends after a change failed.  The changes not run
 * leave their domains as they were (newest first, to each one's old
 * value; a clock not set, its level as before the pass).  A voltage that
 * failed may be anywhere from its old value to its new one, and is taken
 * as the lower of them, so that a clock is never raised for it before a
 * write that succeeded; a clock that failed, as the higher.
 */
static void sky1soc_abort_ops(void)
{
	for (int k = (int)g_ctx.ops_count - 1; k >= (int)g_ctx.curr_idx; k--) {
		const struct ap_dsu_op *op = &g_ctx.ops[k];
		struct mod_sky1soc_dom_ctx *ctx = &g_ctx.ctx[op->domain_id];

		if (op->op_type == SKY1SOC_OP_VOLTAGE) {
			ctx->curr_voltage = op->old_value;
		} else {
			ctx->curr_freq = op->old_value;
			ctx->curr_level = ctx->pass_level;
		}
	}
	for (uint8_t k = g_ctx.group_idx; k < g_ctx.curr_idx; k++) {
		const struct ap_dsu_op *op = &g_ctx.ops[k];
		struct mod_sky1soc_dom_ctx *ctx = &g_ctx.ctx[op->domain_id];
		uint32_t lo = op->old_value < op->new_value ? op->old_value : op->new_value;
		uint32_t hi = op->old_value < op->new_value ? op->new_value : op->old_value;

		if (!op->failed)
			continue;
		if (op->op_type == SKY1SOC_OP_VOLTAGE)
			ctx->curr_voltage = lo;
		else
			ctx->curr_freq = hi;
	}
	g_ctx.ops_count = g_ctx.curr_idx;
}

/*
 * CIX's sky1soc_perf_op_fsm: run the queue up to a barrier; with answers
 * outstanding, wait for them (each answer comes back here), then go on;
 * after an answer that is an error, not (sky1soc_abort_ops).
 */
static void sky1soc_perf_op_fsm(const struct fwk_event *event)
{
	(void)event;
	for (;;) {
		switch (g_ctx.state) {
		case SKY1SOC_STATE_START:
			g_ctx.state = SKY1SOC_STATE_RUN;
			g_ctx.curr_idx = 0;
			g_ctx.op_pending_count = 0;
			g_ctx.op_failed = false;
			break;
		case SKY1SOC_STATE_RUN:
			break;
		case SKY1SOC_STATE_WAIT:
			fwk_assert(g_ctx.op_pending_count != 0);
			if (--g_ctx.op_pending_count != 0)
				return;
			if (g_ctx.op_failed) {
				sky1soc_abort_ops();
				goto done;
			}
			if (g_ctx.curr_idx >= g_ctx.ops_count)
				goto done;
			g_ctx.state = SKY1SOC_STATE_RUN;
			break;
		default:
			fwk_unexpected();
			return;
		}
		g_ctx.group_idx = g_ctx.curr_idx;
		while (g_ctx.curr_idx < g_ctx.ops_count) {
			const struct ap_dsu_op *op = &g_ctx.ops[g_ctx.curr_idx];

			if (sky1soc_run_op(op) == FWK_PENDING)
				g_ctx.op_pending_count++;
			g_ctx.curr_idx++;
			if (op->barrier)
				break;
		}
		if (g_ctx.op_pending_count != 0) {
			g_ctx.state = SKY1SOC_STATE_WAIT;
			return;
		}
		if (g_ctx.curr_idx == g_ctx.ops_count)
			goto done;
	}
done:
	g_ctx.state = SKY1SOC_STATE_IDLE;
	if (g_ctx.fc_sched) {
		on_fast_channel_interrupt(false);
		g_ctx.fc_sched = false;
	}
}

/* CIX's sky1soc_notify_ap_core_change */
bool sky1soc_notify_ap_core_change(uint8_t ap_core_idx, bool online)
{
	struct fwk_event event = module_event(sky1soc_event_id_ap_core);

	event.params[0] = ap_core_idx;
	event.params[1] = online;
	if (fwk_put_event(&event) != FWK_SUCCESS)
		return false;
	on_fast_channel_interrupt(false);
	return true;
}

/* CIX's sky1soc_set_dsu_dvfs */
void sky1soc_set_dsu_dvfs(bool enable)
{
	struct fwk_event event = module_event(sky1soc_event_id_dsu_dvfs);

	event.params[0] = enable;
	(void)fwk_put_event(&event);
}

/* CIX's sky1soc_get_dsu_dvfs */
bool sky1soc_get_dsu_dvfs(void)
{
	return g_ctx.dsu_dvfs;
}

/* A core on or offline: its AP domain's count of cores online */
static void sky1soc_ap_core(uint8_t core, bool online)
{
	uint16_t bit = (uint16_t)(1u << core);
	struct mod_sky1soc_dom_ctx *ctx;

	if (online == ((g_ctx.ap_core_bitmap & bit) != 0))
		return;
	g_ctx.ap_core_bitmap ^= bit;
	ctx = &g_ctx.ctx[sky1soc_get_ap_domain_ref(core)];
	if (online) {
		ctx->active_ap_count++;
	} else {
		fwk_assert(ctx->active_ap_count != 0);
		ctx->active_ap_count--;
	}
}

/* CIX's sky1soc_perf_process_event */
static int sky1soc_perf_process_event(const struct fwk_event *event, struct fwk_event *resp_event)
{
	(void)resp_event;
	if (fwk_id_is_equal(event->id, mod_clock_event_id_set_rate_request)) {
		const struct mod_clock_resp_params *resp = (const void *)event->params;

		/* a cluster's clock answered: its level to scmi_perf */
		sky1soc_op_answer(event->source_id, resp->status);
		for (uint8_t i = 0; i <= g_ctx.ap_dom_count && resp->status == FWK_SUCCESS; i++) {
			if (fwk_id_is_equal(event->source_id, g_ctx.ctx[i].clock_id)) {
				sky1soc_report_level(i);
				break;
			}
		}
		sky1soc_perf_op_fsm(event);
	} else if (fwk_id_is_equal(event->id, mod_psu_event_id_set_voltage)) {
		const struct mod_psu_response *resp = (const void *)event->params;

		sky1soc_op_answer(event->source_id, resp->status);
		sky1soc_perf_op_fsm(event);
	} else if (fwk_id_is_equal(event->id, sky1soc_event_id_run_ops)) {
		sky1soc_perf_op_fsm(event);
	} else if (fwk_id_is_equal(event->id, sky1soc_event_id_dsu_dvfs)) {
		g_ctx.dsu_dvfs = event->params[0] != 0;
	} else if (fwk_id_is_equal(event->id, sky1soc_event_id_ap_core)) {
		sky1soc_ap_core(event->params[0], event->params[1] != 0);
	}
	return FWK_SUCCESS;
}

/* CIX's sky1soc_perf_init (an init-once overlay in CIX's image) */
static int sky1soc_perf_init(fwk_id_t module_id, unsigned int element_count, const void *data)
{
	const struct mod_sky1soc_perf_config *config = data;
	const struct mod_sky1soc_ap_domain_config *dom;

	(void)module_id;
	if (element_count != 0)
		return FWK_E_PARAM;
	fwk_assert(config != NULL);
	g_ctx.config = config;
	g_ctx.gpu_top = config->top_perf_domain_idx;
	g_ctx.gpu_core = config->core_perf_domain_idx;
	g_ctx.ap_dom_begin = config->ap_dom_cfg[0].domain_id;
	g_ctx.ap_dom_count = config->ap_dom_count;
	g_ctx.dsu = config->dsu_perf_domain_idx;
	/* the DSU follows the AP domains */
	fwk_assert(config->ap_dom_cfg[config->ap_dom_count - 1].domain_id + 1 == g_ctx.dsu);
	g_ctx.state = SKY1SOC_STATE_IDLE;
	g_ctx.fc_sched = false;
	g_ctx.dsu_dvfs = false;
	g_ctx.ctx = fwk_mm_calloc(g_ctx.ap_dom_count + 1, sizeof(g_ctx.ctx[0]));
	g_ctx.ops_max = (uint8_t)(4 * g_ctx.ap_dom_count + 4);
	g_ctx.ops = fwk_mm_calloc(g_ctx.ops_max, sizeof(g_ctx.ops[0]));
	g_ctx.tdp_opp_size = config->tdp_opp_size;
	g_ctx.dxs2dvfs_size = config->dxs2dvfs_size;
	g_ctx.dpm_opp_run = config->tdp_opp;
	g_ctx.dxs2dvfs_tbl = config->dxs2dvfs_tbl;
	g_ctx.tdp_opp_run = fwk_mm_calloc(g_ctx.tdp_opp_size, sizeof(uint32_t));
	g_ctx.ap_core_bitmap = 0;
	for (uint8_t i = 0; i <= g_ctx.ap_dom_count; i++) {
		dom = &config->ap_dom_cfg[i];
		g_ctx.ctx[i].active_ap_count = dom->ap_core_count;
		g_ctx.ap_core_bitmap |= dom->ap_core_bitmask;
		/* CIX: a sub-element of its clock_lite; here the SCMI clock */
		g_ctx.ctx[i].clock_id = FWK_ID_ELEMENT(FWK_MODULE_IDX_CLOCK,
						       clk_nodes_enum_idx_to_scmi_idx(dom->clock_enum_id));
	}
	return FWK_SUCCESS;
}

/* CIX's sky1soc_perf_bind (second round) */
static int sky1soc_perf_bind(fwk_id_t id, unsigned int round)
{
	int status;

	(void)id;
	if (round == 0)
		return FWK_SUCCESS;
	status = fwk_module_bind(fwk_module_id_scmi_perf,
				 FWK_ID_API(FWK_MODULE_IDX_SCMI_PERF, MOD_SCMI_PERF_DVFS_UPDATE_API),
				 &g_ctx.scmi_perf_updated_api);
	if (status != FWK_SUCCESS)
		return status;
	status = fwk_module_bind(fwk_module_id_dvfs, mod_dvfs_api_id_dvfs, &g_ctx.dvfs_api);
	if (status != FWK_SUCCESS)
		return status;
	for (uint8_t i = 0; i <= g_ctx.ap_dom_count; i++) {
		struct mod_sky1soc_dom_ctx *ctx = &g_ctx.ctx[i];

		status = fwk_module_bind(g_ctx.config->ap_dom_cfg[i].psu_id, mod_psu_api_id_device,
					 &ctx->psu_api);
		if (status != FWK_SUCCESS)
			return status;
		status = fwk_module_bind(ctx->clock_id,
					 FWK_ID_API(FWK_MODULE_IDX_CLOCK, MOD_CLOCK_API_TYPE_HAL),
					 &ctx->clock_api);
		if (status != FWK_SUCCESS)
			return status;
	}
	return FWK_SUCCESS;
}

/*
 * CIX's sky1soc_perf_start: each domain starts at its sustained OPP, as
 * dvfs leaves it; the DSU PLL's frequency.
 */
static int sky1soc_perf_start(fwk_id_t id)
{
	struct mod_dvfs_opp opp;
	uint32_t vco;

	(void)id;
	for (uint8_t i = 0; i <= g_ctx.ap_dom_count; i++) {
		struct mod_sky1soc_dom_ctx *ctx = &g_ctx.ctx[i];

		if (g_ctx.dvfs_api->get_sustained_opp(dvfs_element(g_ctx.ap_dom_begin + i), &opp) !=
		    FWK_SUCCESS)
			opp = (struct mod_dvfs_opp){ 0 };
		ctx->curr_level = opp.level;
		ctx->curr_voltage = opp.voltage;
		ctx->curr_freq = opp.frequency;
	}
	g_ctx.dsu_fc_old = g_ctx.ctx[g_ctx.ap_dom_count].curr_level;
	vco = (uint32_t)pll_ctrl_get_vco_freq(DSU_PLL_BASE);
	g_ctx.dsu_pll = vco / pll_ctrl_get_postdiv1(DSU_PLL_BASE) / 1000;
	return FWK_SUCCESS;
}

static const struct perf_plugins_api perf_plugins_api = {
	.update = sky1soc_perf_update,
};

/* CIX's sky1soc_perf_process_bind_request: for scmi_perf only */
static int sky1soc_perf_process_bind_request(fwk_id_t source_id, fwk_id_t target_id,
					     fwk_id_t api_id, const void **api)
{
	(void)target_id;
	(void)api_id;
	if (!fwk_id_is_equal(source_id, fwk_module_id_scmi_perf))
		return FWK_E_ACCESS;
	*api = &perf_plugins_api;
	return FWK_SUCCESS;
}

const struct fwk_module module_sky1soc_perf = {
	.type = FWK_MODULE_TYPE_SERVICE,
	.api_count = MOD_SKY1SOC_PERF_API_IDX_COUNT,
	.event_count = MOD_SKY1SOC_PERF_EVENT_IDX_COUNT,
	.init = sky1soc_perf_init,
	.bind = sky1soc_perf_bind,
	.start = sky1soc_perf_start,
	.process_bind_request = sky1soc_perf_process_bind_request,
	.process_event = sky1soc_perf_process_event,
};
