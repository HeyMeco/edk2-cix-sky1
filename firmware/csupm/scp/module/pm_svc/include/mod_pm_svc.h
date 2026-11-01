/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The board services' timers and deferred jobs: what CIX's md_timer.c and
 * md_jobs.c give its drivers (PVT polling, PROCHOT's throttle steps, the
 * heartbeat, the DDR DFS check, I2C recovery, the fans' adjustments), on
 * one of SCP-firmware's alarms and its event queue.
 *
 * A timer's callback and a job run from the module's events, never in an
 * interrupt handler.  Timers count in microseconds as CIX's do; the alarm
 * has milliseconds, so a timer fires on the first millisecond at or after
 * it is due.  Timers and jobs may be set up before SCP-firmware starts;
 * they start with the module.
 */
#ifndef MOD_PM_SVC_H
#define MOD_PM_SVC_H

#include <stdbool.h>
#include <stdint.h>

#define PM_SVC_TIMER_NONE	0xff

typedef void (*pm_svc_proc)(void *arg);

/* as CIX's md_timer alloc (without the priority): a timer firing every us */
uint8_t pm_svc_timer_alloc(uint32_t us, pm_svc_proc cb, void *data, bool enable, bool repeat);
/* enabled: counting from now */
void pm_svc_timer_enable(uint8_t id);
void pm_svc_timer_disable(uint8_t id);
bool pm_svc_timer_is_enabled(uint8_t id);
/* a new interval, from the next start */
void pm_svc_timer_update_interval(uint8_t id, uint32_t us);
/* a job (CIX's md_jobs): proc(arg) from the event loop, in order */
bool pm_svc_job_append(pm_svc_proc proc, void *arg);
/* as CIX's md_timer around suspend: the timers stopped;
 * after it those due meanwhile fire at once */
void pm_svc_pts(void);
void pm_svc_run(void);

/* the services' interrupts, registered as the module starts (src/board) */
void board_irq_init(void);

#endif
