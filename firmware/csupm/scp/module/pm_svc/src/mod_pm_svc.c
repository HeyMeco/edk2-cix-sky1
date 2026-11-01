/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The board services' timers and jobs (mod_pm_svc.h): CIX's md_timer.c and
 * md_jobs.c replaced by one alarm of SCP-firmware's timer module, armed for
 * the timer due first, and the module's events.
 */
#include <mod_pm_svc.h>
#include <mod_timer.h>

#include <fwk_core.h>
#include <fwk_event.h>
#include <fwk_id.h>
#include <fwk_interrupt.h>
#include <fwk_module.h>
#include <fwk_module_idx.h>
#include <fwk_status.h>

#include <stddef.h>

#include "clk/clk.h"

#define TIMERS			12
#define JOBS			16

enum { EVT_TIMERS, EVT_JOBS, EVT_COUNT };

static struct pm_svc_timer {
	pm_svc_proc cb;
	void *data;
	uint32_t us;
	uint64_t due;			/* arch_getTimeNowUs() */
	bool used, enabled, repeat;
} timers[TIMERS];

static struct {
	pm_svc_proc proc;
	void *arg;
} jobs[JOBS];
static unsigned job_head, job_tail;

static const struct mod_timer_alarm_api *alarm_api;
static fwk_id_t alarm_id;
static bool started, suspended, jobs_event_pending, timers_event_pending;

static void put(unsigned id, bool *pending)
{
	struct fwk_event e = {
		.source_id = FWK_ID_MODULE_INIT(FWK_MODULE_IDX_PM_SVC),
		.target_id = FWK_ID_MODULE_INIT(FWK_MODULE_IDX_PM_SVC),
		.id = FWK_ID_EVENT_INIT(FWK_MODULE_IDX_PM_SVC, id),
	};

	if (!started || *pending)
		return;
	*pending = fwk_put_event(&e) == FWK_SUCCESS;
}

static void alarm_cb(uintptr_t param)
{
	(void)param;
	put(EVT_TIMERS, &timers_event_pending);
}

/* the alarm for the timer due first; SCP-firmware's alarms cannot be set
 * from an interrupt handler (PROCHOT's starts its timer there): then from
 * the module's event */
static void arm(void)
{
	uint64_t now, first = UINT64_MAX;

	if (!started || suspended)
		return;
	if (fwk_is_interrupt_context()) {
		put(EVT_TIMERS, &timers_event_pending);
		return;
	}
	for (unsigned i = 0; i < TIMERS; i++) {
		if (timers[i].used && timers[i].enabled && timers[i].due < first)
			first = timers[i].due;
	}
	(void)alarm_api->stop(alarm_id);
	if (first == UINT64_MAX)
		return;
	now = arch_getTimeNowUs();
	(void)alarm_api->start(alarm_id,
			       first > now ? (unsigned int)((first - now + 999) / 1000) : 0,
			       MOD_TIMER_ALARM_TYPE_ONCE, alarm_cb, 0);
}

uint8_t pm_svc_timer_alloc(uint32_t us, pm_svc_proc cb, void *data, bool enable, bool repeat)
{
	for (uint8_t i = 0; i < TIMERS; i++) {
		if (timers[i].used)
			continue;
		timers[i] = (struct pm_svc_timer){
			.cb = cb, .data = data, .us = us, .used = true, .repeat = repeat,
		};
		if (enable)
			pm_svc_timer_enable(i);
		return i;
	}
	return PM_SVC_TIMER_NONE;
}

void pm_svc_timer_enable(uint8_t id)
{
	if (id >= TIMERS || !timers[id].used)
		return;
	timers[id].enabled = true;
	timers[id].due = arch_getTimeNowUs() + timers[id].us;
	arm();
}

void pm_svc_timer_disable(uint8_t id)
{
	if (id >= TIMERS)
		return;
	timers[id].enabled = false;
	arm();
}

bool pm_svc_timer_is_enabled(uint8_t id)
{
	return id < TIMERS && timers[id].enabled;
}

void pm_svc_timer_update_interval(uint8_t id, uint32_t us)
{
	if (id < TIMERS)
		timers[id].us = us;
}

bool pm_svc_job_append(pm_svc_proc proc, void *arg)
{
	unsigned next = (job_tail + 1) % JOBS;

	if (next == job_head)
		return false;
	jobs[job_tail].proc = proc;
	jobs[job_tail].arg = arg;
	job_tail = next;
	put(EVT_JOBS, &jobs_event_pending);
	return true;
}

void pm_svc_pts(void)
{
	suspended = true;
	if (started)
		(void)alarm_api->stop(alarm_id);
}

void pm_svc_run(void)
{
	suspended = false;
	arm();
}

/* the timers that are due, each once, a repeating one due again a period
 * after it was due */
static void run_timers(void)
{
	uint64_t now = arch_getTimeNowUs();

	for (unsigned i = 0; i < TIMERS && !suspended; i++) {
		struct pm_svc_timer *t = &timers[i];

		if (!t->used || !t->enabled || t->due > now)
			continue;
		if (t->repeat) {
			t->due += t->us;
			if (t->due <= now)
				t->due = now + t->us;
		} else {
			t->enabled = false;
		}
		t->cb(t->data);
	}
	arm();
}

static void run_jobs(void)
{
	while (job_head != job_tail) {
		pm_svc_proc proc = jobs[job_head].proc;
		void *arg = jobs[job_head].arg;

		job_head = (job_head + 1) % JOBS;
		proc(arg);
	}
}

static int pm_svc_init(fwk_id_t module_id, unsigned int element_count, const void *data)
{
	(void)module_id;
	(void)element_count;
	alarm_id = *(const fwk_id_t *)data;
	return FWK_SUCCESS;
}

static int pm_svc_bind(fwk_id_t id, unsigned int round)
{
	if (round || !fwk_id_is_type(id, FWK_ID_TYPE_MODULE))
		return FWK_SUCCESS;
	return fwk_module_bind(alarm_id, MOD_TIMER_API_ID_ALARM, &alarm_api);
}

static int pm_svc_start(fwk_id_t id)
{
	if (!fwk_id_is_type(id, FWK_ID_TYPE_MODULE))
		return FWK_SUCCESS;
	started = true;
	board_irq_init();
	arm();
	if (job_head != job_tail)
		put(EVT_JOBS, &jobs_event_pending);
	return FWK_SUCCESS;
}

static int pm_svc_process_event(const struct fwk_event *event, struct fwk_event *resp)
{
	(void)resp;
	switch (fwk_id_get_event_idx(event->id)) {
	case EVT_TIMERS:
		timers_event_pending = false;
		run_timers();
		break;
	case EVT_JOBS:
		jobs_event_pending = false;
		run_jobs();
		break;
	default:
		return FWK_E_PARAM;
	}
	return FWK_SUCCESS;
}

const struct fwk_module module_pm_svc = {
	.type = FWK_MODULE_TYPE_SERVICE,
	.event_count = EVT_COUNT,
	.init = pm_svc_init,
	.bind = pm_svc_bind,
	.start = pm_svc_start,
	.process_event = pm_svc_process_event,
};
