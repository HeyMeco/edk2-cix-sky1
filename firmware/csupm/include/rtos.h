/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The PM's tasks (src/rtos.c): SCP-firmware's, the I2C workers, idle.
 */
#ifndef RTOS_H
#define RTOS_H

#include <stdbool.h>
#include <stdint.h>

/* the workers: each runs its jobs in order, one at a time */
enum rtos_worker {
	RTOS_PMIC,		/* every PMIC transfer at run time */
	RTOS_SENSOR,		/* the board sensors (TMP75s) */
	RTOS_WORKERS,
};

typedef void (*rtos_job_fn)(void *arg);

/* main, once the PM is up: the tasks created, the scheduler started */
void rtos_start(void) __attribute__((__noreturn__));

/* the scheduler runs and the power sequence is not under way: I2C waits
 * asleep, PMIC work goes to its worker */
bool rtos_async(void);
bool rtos_in_scp_task(void);
/* a task runs that is not SCP-firmware's (fwk_is_interrupt_context) */
bool rtos_outside_scp_task(void);

/* the SCP-firmware task: sleep until there is something to do; wake it */
void rtos_scp_wait(void);
void rtos_scp_kick(void);

/*
 * fn(arg) on the worker, then done(arg) there too (done may only do what
 * an interrupt handler may: put an event, append a pm_svc job).  False
 * when the worker's queue is full.  Without rtos_async(), both run now.
 */
bool rtos_submit(enum rtos_worker w, rtos_job_fn fn, void *arg, rtos_job_fn done);
/* fn(arg) on the worker, the caller asleep until it is done (now, when
 * the caller is that worker or without rtos_async()) */
void rtos_call(enum rtos_worker w, rtos_job_fn fn, void *arg);

/* around the power sequence (src/hsm, from the SCP-firmware task): the
 * workers' jobs finished, then I2C polled and run where it is asked */
void rtos_quiesce(void);
void rtos_unquiesce(void);

/* the tick, while a task waits with a time-out (rtos/port/port.c) */
void port_tick_hold(void);
void port_tick_release(void);

/* the stacks' unused words, for the tests (scp, pmic, sensor, idle) */
uint32_t rtos_stack_free(unsigned int task);

#endif
