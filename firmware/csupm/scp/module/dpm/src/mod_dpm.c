/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The performance streams' coarse tick and the DPM's task as SCP-firmware
 * sees them (mod_dpm.h).  CIX's perf_stream_init allocates an md_timer
 * (1000 us, its callback perf_stream_coarse_tick_handler in the timer's
 * interrupt) and an md_event (perf_stream_event_handler, in CIX's event
 * task); the event handler ends with dpm_ticker, which notifies the "DPM"
 * FreeRTOS task (dpm_proc, below the SCMI task's priority).  Here the
 * alarm's callback is the timer's, and the md_event a software interrupt
 * whose handler does the streams' work, then dpm_proc: CIX's order.  Like
 * CIX's tasks it runs outside SCP-firmware's event processing, so the
 * events the DPM sends scmi_perf (on_fast_channel_interrupt, on_tdp_notify)
 * keep their source (sent while another module's event is processed they
 * would take that module's, and scmi_perf would drop them).  A signal
 * while the work is pending is one (CIX's event bits); a clear drops it.
 * The timer is started and stopped from wherever CIX does it; the alarm
 * API refuses interrupt context, so from there it goes through an event.
 */
#include <mod_dpm.h>
#include <mod_timer.h>

#include <fwk_core.h>
#include <fwk_event.h>
#include <fwk_id.h>
#include <fwk_interrupt.h>
#include <fwk_module.h>
#include <fwk_module_idx.h>
#include <fwk_status.h>

#include "dpm/dpm_rt.h"
#include "perf/dev_counters.h"
#include "perf/perf.h"

enum dpm_event_idx {
	DPM_EVENT_IDX_TIMER,		/* start or stop the alarm */
	DPM_EVENT_IDX_COUNT,
};

static struct {
	const struct mod_dpm_config *config;
	const struct mod_timer_alarm_api *alarm;
	bool started;			/* the alarm may be used */
	bool timer_on;			/* what CIX's md_timer would be */
	bool tick_pending;
} dpm_ctx;

static const fwk_id_t timer_event_id =
	FWK_ID_EVENT_INIT(FWK_MODULE_IDX_DPM, DPM_EVENT_IDX_TIMER);

static void coarse_tick(uintptr_t param)
{
	(void)param;
	perf_stream_coarse_tick_handler(NULL);
}

static void apply_timer(void)
{
	if (!dpm_ctx.started)
		return;
	if (dpm_ctx.timer_on)
		(void)dpm_ctx.alarm->start(dpm_ctx.config->alarm_id, dpm_ctx.config->tick_ms,
					   MOD_TIMER_ALARM_TYPE_PERIODIC, coarse_tick, 0);
	else
		(void)dpm_ctx.alarm->stop(dpm_ctx.config->alarm_id);
}

static void put(fwk_id_t id)
{
	struct fwk_event e = {
		.source_id = FWK_ID_MODULE(FWK_MODULE_IDX_DPM),
		.target_id = FWK_ID_MODULE(FWK_MODULE_IDX_DPM),
		.id = id,
	};

	(void)fwk_put_event(&e);
}

static void set_timer(bool on)
{
	if (dpm_ctx.timer_on == on)
		return;
	dpm_ctx.timer_on = on;
	if (fwk_is_interrupt_context())
		put(timer_event_id);
	else
		apply_timer();
}

/* CIX's md_timer_enable(perf_stream_tick_timer_id) */
void perf_tick_timer_enable(void)
{
	set_timer(true);
}

/* CIX's md_timer_disable */
void perf_tick_timer_disable(void)
{
	set_timer(false);
}

/* CIX's md_event_signal(perf event, 1, true) */
void perf_tick_event_signal(void)
{
	if (dpm_ctx.tick_pending)
		return;
	dpm_ctx.tick_pending = true;
	(void)fwk_interrupt_set_pending(dpm_ctx.config->work_irq);
}

/* The streams' work (CIX's md_event callback), then the DPM's task */
static void tick_work(void)
{
	if (!dpm_ctx.tick_pending)
		return;		/* cleared meanwhile */
	dpm_ctx.tick_pending = false;
	perf_stream_event_handler(0, 1);
	dpm_proc();
}

/* CIX's md_event_clear(perf event, all bits) */
void perf_tick_event_clear(void)
{
	dpm_ctx.tick_pending = false;
}

static void cnts_irq0(void)
{
	dev_cnts_intr0(NULL);
}

static void cnts_irq1(void)
{
	dev_cnts_intr1(NULL);
}

static int dpm_mod_init(fwk_id_t module_id, unsigned int count, const void *data)
{
	(void)module_id;
	(void)count;
	dpm_ctx.config = data;
	return data ? FWK_SUCCESS : FWK_E_PARAM;
}

static int dpm_bind(fwk_id_t id, unsigned int round)
{
	(void)id;
	if (round)
		return FWK_SUCCESS;
	return fwk_module_bind(dpm_ctx.config->alarm_id, MOD_TIMER_API_ID_ALARM, &dpm_ctx.alarm);
}

static int dpm_start(fwk_id_t id)
{
	(void)id;
	(void)fwk_interrupt_set_isr(dpm_ctx.config->work_irq, tick_work);
	(void)fwk_interrupt_enable(dpm_ctx.config->work_irq);
	(void)fwk_interrupt_set_isr(dpm_ctx.config->cnts_irq[0], cnts_irq0);
	(void)fwk_interrupt_enable(dpm_ctx.config->cnts_irq[0]);
	(void)fwk_interrupt_set_isr(dpm_ctx.config->cnts_irq[1], cnts_irq1);
	(void)fwk_interrupt_enable(dpm_ctx.config->cnts_irq[1]);
	dpm_ctx.started = true;
	apply_timer();
	return FWK_SUCCESS;
}

static int dpm_process_event(const struct fwk_event *event, struct fwk_event *resp)
{
	(void)resp;
	switch (fwk_id_get_event_idx(event->id)) {
	case DPM_EVENT_IDX_TIMER:
		apply_timer();
		return FWK_SUCCESS;
	default:
		return FWK_E_PARAM;
	}
}

const struct fwk_module module_dpm = {
	.type = FWK_MODULE_TYPE_SERVICE,
	.event_count = DPM_EVENT_IDX_COUNT,
	.init = dpm_mod_init,
	.bind = dpm_bind,
	.start = dpm_start,
	.process_event = dpm_process_event,
};
