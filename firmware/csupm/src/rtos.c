/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The PM's tasks, on FreeRTOS (co-operative: rtos/FreeRTOSConfig.h).
 *
 *   scp     (3)  SCP-firmware's loop (scp/src/arch_main.c): every module,
 *                SCMI, the DPM, the board services' timers, PVT, fans,
 *                PROCHOT, the power sequence.  It never waits for I2C.
 *   pmic    (2)  the PMICs' transfers at run time: rail voltages (for
 *                scp/module/pmic, the DPM's rails and their offsets),
 *                fault status.  One worker for all PMICs, so that two
 *                voltages are never set at once (rails that share a buck
 *                look at each other's voltage) and a paged PMIC's page
 *                and register stay together.
 *   sensor  (1)  the board's I2C temperature sensors.
 *   idle    (0)  sleeps (WAITI); runs the tick's and the I2C controllers'
 *                handlers when the SCP-firmware task is not at its loop's
 *                end, all of them otherwise.
 *
 * A stuck bus stops its worker for at most a few transfers' time-outs
 * (src/i2c.c), then fails fast; SCP-firmware's task, and with it the DPM,
 * the thermal and fan control and SCMI, goes on meanwhile.  It waits only
 * at its loop's end and, for the few PMIC calls that answer at once (PM
 * messages 0x400 and 0x804: fault status, the rails' voltages read back),
 * for the PMIC worker.
 *
 * Memory is static.  SCP-firmware's task runs on the stack main had (the
 * top of DRAM1, below the 2 KB main still stands on as the scheduler
 * starts): ddr_s3_enter borrows PM RAM from 0x237b4 bytes below the stack
 * pointer, which pm.ld keeps clear of everything but the heap.
 */
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "arch_scp.h"
#include "i2c.h"
#include "log/log.h"
#include "rtos.h"
#include "uart.h"

#define PRIO_IDLE		0
#define PRIO_SENSOR		1
#define PRIO_PMIC		2
#define PRIO_SCP		3

#define WORKER_STACK		1024u		/* words */
#define IDLE_STACK		512u
#define WORKER_QUEUE		8

extern StackType_t _stack_bottom[], _scp_stack_top[];

struct job {
	rtos_job_fn fn, done;
	void *arg;
	TaskHandle_t waiter;		/* rtos_call's */
};

static struct worker {
	TaskHandle_t task;
	QueueHandle_t queue;
	bool busy;
	StaticTask_t tcb;
	StaticQueue_t queue_buf;
	uint8_t queue_store[WORKER_QUEUE * sizeof(struct job)];
	StackType_t stack[WORKER_STACK];
} workers[RTOS_WORKERS];		/* bss: the stacks are in here */

static const struct {
	const char *name;
	UBaseType_t prio;
} worker_cfg[RTOS_WORKERS] = {
	[RTOS_PMIC] = { "pmic", PRIO_PMIC },
	[RTOS_SENSOR] = { "sensor", PRIO_SENSOR },
};

static TaskHandle_t scp_task;
static StaticTask_t scp_tcb;
static StaticTask_t idle_tcb;
static StackType_t idle_stack[IDLE_STACK];

static bool running, quiesced;
static volatile bool scp_waiting;	/* at its loop's end (rtos_scp_wait) */
static TaskHandle_t quiesce_waiter;

void rtos_assert_failed(const char *file, int line)
{
	(void)file;
	fwlog_write(FWLOG_INTR, "RTOS assert %d", line);
	uart_puts("CSU_PM: RTOS assert\n");
	for (;;)
		__asm__ volatile("waiti 15");
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
	(void)task;
	(void)name;
	rtos_assert_failed(__FILE__, __LINE__);
}

void vApplicationGetIdleTaskMemory(StaticTask_t **tcb, StackType_t **stack,
				   configSTACK_DEPTH_TYPE *depth)
{
	*tcb = &idle_tcb;
	*stack = idle_stack;
	*depth = IDLE_STACK;
}

bool rtos_async(void)
{
	return running && !quiesced;
}

bool rtos_in_scp_task(void)
{
	return running && xTaskGetCurrentTaskHandle() == scp_task;
}

bool rtos_outside_scp_task(void)
{
	return running && xTaskGetCurrentTaskHandle() != scp_task;
}

/* --- SCP-firmware's task ------------------------------------------------------ */

void rtos_scp_kick(void)
{
	if (running)
		(void)xTaskNotifyGiveIndexed(scp_task, 0);
}

/* every worker waiting for a job, none queued (a job's sender has not
 * been run since: co-operative) */
static bool workers_idle(void)
{
	for (unsigned i = 0; i < RTOS_WORKERS; i++) {
		if (workers[i].busy || uxQueueMessagesWaiting(workers[i].queue))
			return false;
	}
	return true;
}

void rtos_scp_wait(void)
{
	if (!running) {
		arch_irq_wait();
		return;
	}
	/* no worker has a job: nothing but this task can run, so it sleeps
	 * itself (its loop runs what woke it) instead of switching to the idle
	 * task and back for every interrupt (the 50 µs fine tick's above all) */
	if (workers_idle()) {
		arch_irq_wait();
		return;
	}
	scp_waiting = true;
	(void)ulTaskNotifyTakeIndexed(0, pdTRUE, portMAX_DELAY);
	scp_waiting = false;
}

static void scp_main(void *arg)
{
	(void)arg;
	running = true;
	scp_run();
}

/*
 * FreeRTOS's idle task, after it has yielded to any task that is ready
 * (only while a worker has a job: rtos_scp_wait sleeps itself otherwise).
 * While SCP-firmware's task waits at its loop's end, any interrupt wakes
 * it (its loop runs the handlers, on its stack); while it waits in the
 * middle of something (for the PMIC worker, the power sequence's
 * quiescing) only the kernel's lines are served here.
 */
void vApplicationIdleHook(void)
{
	bool all = scp_waiting;

	arch_irq_wait_for(all ? ~0u : ARCH_IRQ_KERNEL, all);
	(void)arch_irq_run_mask(ARCH_IRQ_KERNEL, false);
	if (scp_waiting)
		rtos_scp_kick();
}

/* --- the workers --------------------------------------------------------------- */

static void worker_main(void *arg)
{
	struct worker *w = arg;
	struct job j;

	for (;;) {
		(void)xQueueReceive(w->queue, &j, portMAX_DELAY);
		w->busy = true;
		j.fn(j.arg);
		if (j.done)
			j.done(j.arg);
		w->busy = false;
		if (j.waiter)
			(void)xTaskNotifyGiveIndexed(j.waiter, 1);
		/* what done put for the framework */
		rtos_scp_kick();
		if (quiesce_waiter && !uxQueueMessagesWaiting(w->queue)) {
			TaskHandle_t t = quiesce_waiter;

			quiesce_waiter = NULL;
			(void)xTaskNotifyGiveIndexed(t, 1);
		}
	}
}

static bool on_worker(enum rtos_worker w)
{
	return xTaskGetCurrentTaskHandle() == workers[w].task;
}

bool rtos_submit(enum rtos_worker w, rtos_job_fn fn, void *arg, rtos_job_fn done)
{
	struct job j = { .fn = fn, .done = done, .arg = arg };

	if (!rtos_async() || on_worker(w)) {
		fn(arg);
		if (done)
			done(arg);
		return true;
	}
	return xQueueSend(workers[w].queue, &j, 0) == pdTRUE;
}

void rtos_call(enum rtos_worker w, rtos_job_fn fn, void *arg)
{
	struct job j = { .fn = fn, .arg = arg };

	if (!rtos_async() || on_worker(w)) {
		fn(arg);
		return;
	}
	j.waiter = xTaskGetCurrentTaskHandle();
	(void)xTaskNotifyStateClearIndexed(NULL, 1);
	(void)ulTaskNotifyValueClearIndexed(NULL, 1, ~0u);
	(void)xQueueSend(workers[w].queue, &j, portMAX_DELAY);
	(void)ulTaskNotifyTakeIndexed(1, pdTRUE, portMAX_DELAY);
}

void rtos_quiesce(void)
{
	if (!rtos_async())
		return;
	for (unsigned i = 0; i < RTOS_WORKERS; i++) {
		struct worker *w = &workers[i];

		while (w->busy || uxQueueMessagesWaiting(w->queue)) {
			quiesce_waiter = xTaskGetCurrentTaskHandle();
			(void)xTaskNotifyStateClearIndexed(NULL, 1);
			(void)ulTaskNotifyTakeIndexed(1, pdTRUE, portMAX_DELAY);
		}
	}
	quiesced = true;
}

void rtos_unquiesce(void)
{
	quiesced = false;
}

uint32_t rtos_stack_free(unsigned int task)
{
	TaskHandle_t t = task == 0 ? scp_task : task <= RTOS_WORKERS ? workers[task - 1].task : NULL;

	return t ? (uint32_t)uxTaskGetStackHighWaterMark(t) : 0;
}

/* --- start ---------------------------------------------------------------------- */

void rtos_start(void)
{
	uintptr_t sp = (uintptr_t)__builtin_frame_address(0);

	/* main's frames, and the calls below, stay above the task's stack */
	configASSERT(sp > (uintptr_t)_scp_stack_top + 0x400);
	scp_task = xTaskCreateStatic(scp_main, "scp", (uint32_t)(_scp_stack_top - _stack_bottom), NULL,
				     PRIO_SCP, _stack_bottom, &scp_tcb);
	for (unsigned i = 0; i < RTOS_WORKERS; i++) {
		struct worker *w = &workers[i];

		w->queue = xQueueCreateStatic(WORKER_QUEUE, sizeof(struct job), w->queue_store,
					      &w->queue_buf);
		w->task = xTaskCreateStatic(worker_main, worker_cfg[i].name, WORKER_STACK, w,
					    worker_cfg[i].prio, w->stack, &w->tcb);
	}
	i2c_rtos_init();
	vTaskStartScheduler();
	rtos_assert_failed(__FILE__, __LINE__);
	for (;;)
		;
}
