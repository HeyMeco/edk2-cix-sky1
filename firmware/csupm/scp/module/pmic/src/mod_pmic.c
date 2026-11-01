/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * CIX's mod_pmic.c (extmodules/pmic): the PSU driver for the EDP rails.
 * Voltages are read from and written to the DPM (dpm_edp_get_voltage,
 * dpm_edp_set_voltage: the rail's PMIC over I2C); the SOC rail (element
 * CONFIG_PMIC_ELEMENT_IDX_SOC) is not driven, its voltage only kept.
 *
 * A voltage change is asynchronous: CIX queues mod_pmic_switch_proc as a
 * job for its job task (md_jobs_append) and answers FWK_PENDING; the open
 * firmware has no tasks and posts an event to the element instead, which
 * runs the same procedure once the request has returned.
 */
#include <mod_pmic.h>
#include <mod_psu.h>

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

#include "rtos.h"
#include "scp_dpm.h"

enum mod_pmic_request {
	MOD_PMIC_REQUEST_NONE,
	MOD_PMIC_REQUEST_GET_ENABLED,
	MOD_PMIC_REQUEST_SET_ENABLED,
	MOD_PMIC_REQUEST_GET_VOLTAGE,
	MOD_PMIC_REQUEST_SET_VOLTAGE,
};

struct mod_pmic_operation {
	enum mod_pmic_request request;
	union {
		bool enabled;
		uint32_t voltage;
	};
};

/* a change on the PMIC worker */
struct mod_pmic_job {
	fwk_id_t element_id;
	uint8_t edp_idx;
	uint32_t voltage;
	bool ok;
};

struct mod_pmic_element_ctx {
	struct {
		bool enabled;
		uint32_t voltage;
	} state;
	struct mod_pmic_operation op;
	struct mod_pmic_job job;
	struct {
		const struct mod_psu_driver_response_api *hal;
	} apis;
};

static struct mod_pmic_mod_ctx {
	struct mod_pmic_element_ctx *elements;
} mod_pmic_ctx;

/* The SOC rail's voltage (mV), CIX's soc_pmic_voltage */
static uint32_t soc_pmic_voltage = 790;

static const fwk_id_t mod_pmic_event_id_switch =
	FWK_ID_EVENT_INIT(FWK_MODULE_IDX_PMIC, MOD_PMIC_EVENT_IDX_SWITCH);
static const fwk_id_t mod_pmic_event_id_done =
	FWK_ID_EVENT_INIT(FWK_MODULE_IDX_PMIC, MOD_PMIC_EVENT_IDX_DONE);

/* CIX's mod_pmic_get_cfg_ctx */
static int mod_pmic_get_cfg_ctx(fwk_id_t element_id, union mod_pmic_element_cfg *cfg,
				struct mod_pmic_element_ctx **ctx)
{
	if (fwk_id_get_module_idx(element_id) != FWK_MODULE_IDX_PMIC)
		return FWK_E_PARAM;
	if (ctx != NULL)
		*ctx = &mod_pmic_ctx.elements[fwk_id_get_element_idx(element_id)];
	if (cfg != NULL)
		cfg->pmic_data = (uint32_t)(uintptr_t)fwk_module_get_data(element_id);
	return FWK_SUCCESS;
}

static fwk_id_t psu_element(union mod_pmic_element_cfg cfg)
{
	return FWK_ID_ELEMENT(FWK_MODULE_IDX_PSU, cfg.psu_idx);
}

/* CIX's mod_pmic_init */
static int mod_pmic_init(fwk_id_t module_id, unsigned int element_count, const void *data)
{
	(void)module_id;
	(void)data;
	mod_pmic_ctx.elements = fwk_mm_calloc(element_count, sizeof(mod_pmic_ctx.elements[0]));
	return FWK_SUCCESS;
}

/* CIX's mod_pmic_element_init: the rail's voltage as the DPM has it */
static int mod_pmic_element_init(fwk_id_t element_id, unsigned int sub_element_count,
				 const void *data)
{
	union mod_pmic_element_cfg cfg = { .pmic_data = (uint32_t)(uintptr_t)data };
	struct mod_pmic_element_ctx *ctx;
	uint16_t voltage = 0;

	(void)sub_element_count;
	ctx = &mod_pmic_ctx.elements[fwk_id_get_element_idx(element_id)];
	if (!dpm_edp_get_voltage(cfg.edp_idx, &voltage))
		return FWK_E_SUPPORT;
	*ctx = (struct mod_pmic_element_ctx){
		.state = { .enabled = true, .voltage = voltage },
	};
	return FWK_SUCCESS;
}

/* CIX's mod_pmic_bind: the PSU element's response API */
static int mod_pmic_bind(fwk_id_t id, unsigned int round)
{
	union mod_pmic_element_cfg cfg;

	if (round != 0 || !fwk_id_is_type(id, FWK_ID_TYPE_ELEMENT))
		return FWK_SUCCESS;
	cfg.pmic_data = (uint32_t)(uintptr_t)fwk_module_get_data(id);
	return fwk_module_bind(psu_element(cfg), mod_psu_api_id_driver_response,
			       &mod_pmic_ctx.elements[fwk_id_get_element_idx(id)].apis.hal);
}

/* CIX's mod_pmic_get_enabled: the rails are always on */
static int mod_pmic_get_enabled(fwk_id_t element_id, bool *enabled)
{
	(void)element_id;
	if (enabled != NULL)
		*enabled = true;
	return FWK_SUCCESS;
}

/* CIX's mod_pmic_set_enabled: they cannot be turned off */
static int mod_pmic_set_enabled(fwk_id_t element_id, bool enabled)
{
	(void)element_id;
	return enabled ? FWK_SUCCESS : FWK_E_DEVICE;
}

/* CIX's mod_pmic_get_voltage */
static int mod_pmic_get_voltage(fwk_id_t element_id, uint32_t *voltage)
{
	union mod_pmic_element_cfg cfg;
	uint16_t mv = 0;
	int status;

	if (fwk_id_get_element_idx(element_id) == CONFIG_PMIC_ELEMENT_IDX_SOC) {
		if (voltage != NULL)
			*voltage = soc_pmic_voltage;
		return FWK_SUCCESS;
	}
	status = mod_pmic_get_cfg_ctx(element_id, &cfg, NULL);
	fwk_assert(status == FWK_SUCCESS);
	if (!dpm_edp_get_voltage(cfg.edp_idx, &mv) || mv == 0)
		return FWK_E_SUPPORT;
	if (voltage != NULL)
		*voltage = mv;
	return FWK_SUCCESS;
}

/* CIX's mod_pmic_set_voltage: the change runs from an event (CIX: a job) */
static int mod_pmic_set_voltage(fwk_id_t element_id, uint32_t voltage)
{
	struct mod_pmic_element_ctx *ctx;
	struct fwk_event_light event = {
		.source_id = element_id,
		.target_id = element_id,
		.id = mod_pmic_event_id_switch,
	};

	if (fwk_id_get_element_idx(element_id) == CONFIG_PMIC_ELEMENT_IDX_SOC) {
		soc_pmic_voltage = voltage;
		return FWK_SUCCESS;
	}
	fwk_assert(fwk_id_get_module_idx(element_id) == FWK_MODULE_IDX_PMIC);
	ctx = &mod_pmic_ctx.elements[fwk_id_get_element_idx(element_id)];
	if (ctx->op.request != MOD_PMIC_REQUEST_NONE)
		return FWK_E_BUSY;
	ctx->op.voltage = voltage;
	ctx->op.request = MOD_PMIC_REQUEST_SET_VOLTAGE;
	if (fwk_put_event(&event) == FWK_SUCCESS)
		return FWK_PENDING;
	ctx->op.request = MOD_PMIC_REQUEST_NONE;
	return FWK_E_STATE;
}

/* on the PMIC worker: the rail set, then the end back to the element */
static void mod_pmic_job_run(void *arg)
{
	struct mod_pmic_job *job = arg;

	job->ok = dpm_edp_set_voltage((DPM_EDP_DOMAIN)job->edp_idx, job->voltage);
}

static void mod_pmic_job_done(void *arg)
{
	struct mod_pmic_job *job = arg;
	struct fwk_event_light event = {
		.source_id = job->element_id,
		.target_id = job->element_id,
		.id = mod_pmic_event_id_done,
	};

	(void)fwk_put_event(&event);
}

/*
 * CIX's mod_pmic_switch_proc: set the rail, answer the PSU.  The rail is
 * set by the PMIC worker, so that the event loop goes on while the I2C
 * transfers do (CIX: its job ran in its own task); the answer comes with
 * the done event.
 */
static void mod_pmic_switch_proc(fwk_id_t element_id)
{
	struct mod_pmic_element_ctx *ctx;
	union mod_pmic_element_cfg cfg;
	int status;

	status = mod_pmic_get_cfg_ctx(element_id, &cfg, &ctx);
	fwk_assert(status == FWK_SUCCESS);
	fwk_assert(ctx->op.request == MOD_PMIC_REQUEST_SET_VOLTAGE);
	ctx->job = (struct mod_pmic_job){
		.element_id = element_id,
		.edp_idx = cfg.edp_idx,
		.voltage = ctx->op.voltage,
	};
	if (!rtos_submit(RTOS_PMIC, mod_pmic_job_run, &ctx->job, mod_pmic_job_done)) {
		/* the worker's queue full: wait for it */
		rtos_call(RTOS_PMIC, mod_pmic_job_run, &ctx->job);
		mod_pmic_job_done(&ctx->job);
	}
}

static void mod_pmic_done_proc(fwk_id_t element_id)
{
	struct mod_pmic_element_ctx *ctx;
	union mod_pmic_element_cfg cfg;
	struct mod_psu_driver_response response = { 0 };
	int status;

	status = mod_pmic_get_cfg_ctx(element_id, &cfg, &ctx);
	fwk_assert(status == FWK_SUCCESS);
	fwk_assert(ctx->op.request == MOD_PMIC_REQUEST_SET_VOLTAGE);
	if (ctx->job.ok) {
		ctx->state.voltage = ctx->op.voltage;
		response.status = FWK_SUCCESS;
	} else {
		response.status = FWK_E_DEVICE;
	}
	ctx->op.request = MOD_PMIC_REQUEST_NONE;
	ctx->apis.hal->respond(psu_element(cfg), response);
}

static int mod_pmic_process_event(const struct fwk_event *event, struct fwk_event *resp_event)
{
	(void)resp_event;
	if (fwk_id_is_equal(event->id, mod_pmic_event_id_switch))
		mod_pmic_switch_proc(event->target_id);
	else if (fwk_id_is_equal(event->id, mod_pmic_event_id_done))
		mod_pmic_done_proc(event->target_id);
	else
		return FWK_E_PARAM;
	return FWK_SUCCESS;
}

static const struct mod_psu_driver_api mod_pmic_driver_api = {
	.get_enabled = mod_pmic_get_enabled,
	.set_enabled = mod_pmic_set_enabled,
	.get_voltage = mod_pmic_get_voltage,
	.set_voltage = mod_pmic_set_voltage,
};

/* CIX's mod_pmic_process_bind_request */
static int mod_pmic_process_bind_request(fwk_id_t source_id, fwk_id_t target_id,
					 fwk_id_t api_id, const void **api)
{
	(void)source_id;
	if (!fwk_id_is_type(target_id, FWK_ID_TYPE_ELEMENT) ||
	    !fwk_id_is_equal(api_id, FWK_ID_API(FWK_MODULE_IDX_PMIC, MOD_PMIC_API_IDX_DRIVER)))
		return FWK_E_ACCESS;
	*api = &mod_pmic_driver_api;
	return FWK_SUCCESS;
}

const struct fwk_module module_pmic = {
	.type = FWK_MODULE_TYPE_DRIVER,
	.api_count = MOD_PMIC_API_IDX_COUNT,
	.event_count = MOD_PMIC_EVENT_IDX_COUNT,
	.init = mod_pmic_init,
	.element_init = mod_pmic_element_init,
	.bind = mod_pmic_bind,
	.process_bind_request = mod_pmic_process_bind_request,
	.process_event = mod_pmic_process_event,
};
