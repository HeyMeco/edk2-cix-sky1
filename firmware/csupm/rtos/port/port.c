/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * FreeRTOS port for CSU_PM: stacks, critical sections, the yield and the
 * tick (portmacro.h, portasm.S).
 *
 * The tick is CCOMPARE1 (interrupt 7, level 4), as CIX's FreeRTOS had it,
 * handled from the deferred-interrupt loop like any other line (never in
 * the vector).  It only runs while a task waits with a time-out
 * (port_tick_hold/release around the wait): nothing else here counts in
 * ticks, and a PM that waits for nothing sleeps until an interrupt as it
 * did without an RTOS.  A tick missed while the PM was busy is a tick
 * late, not lost: time-outs are at least what they ask for.
 */
#include "FreeRTOS.h"
#include "task.h"

#include "arch_scp.h"
#include "irq.h"
#include "lcru.h"
#include "rtos.h"

void port_switch(void);
void port_start_first(void);
void _port_task_tramp(void);
void port_task_start(TaskFunction_t fn, void *param);

static UBaseType_t nesting;
static uint32_t nesting_ps;

void vPortEnterCritical(void)
{
	uint32_t ps = port_irq_disable();

	if (nesting++ == 0)
		nesting_ps = ps;
}

void vPortExitCritical(void)
{
	configASSERT(nesting);
	if (--nesting == 0)
		port_irq_restore(nesting_ps);
}

/* the switch, with this task's interrupt state and critical nesting kept
 * on its stack */
void vPortYield(void)
{
	uint32_t ps = port_irq_disable(), saved_ps = nesting_ps;
	UBaseType_t n = nesting;

	port_switch();
	nesting = n;
	nesting_ps = saved_ps;
	port_irq_restore(ps);
}

void port_task_start(TaskFunction_t fn, void *param)
{
	nesting = 0;
	port_irq_enable();
	fn(param);
	rtos_assert_failed(__FILE__, __LINE__);	/* a task never returns */
}

/*
 * top is the stack's last word, aligned down to 16 bytes (tasks.c).  The
 * trampoline's frame has its stack pointer 16 bytes below; the frame of
 * the port_switch it seems to have called is 64 bytes, its return address
 * (call4 into the trampoline) at its stack pointer, the trampoline's
 * a0-a3 in the 16 bytes below it.
 */
StackType_t *pxPortInitialiseStack(StackType_t *top, TaskFunction_t fn, void *param)
{
	uintptr_t tramp_sp = ((uintptr_t)top & ~(uintptr_t)15) - 16;
	uint32_t *sw = (uint32_t *)(tramp_sp - 64);

	sw[0] = (1u << 30) | ((uintptr_t)_port_task_tramp & 0x3fffffffu);
	sw[-4] = 0;				/* a0: nothing to return to */
	sw[-3] = (uint32_t)tramp_sp;		/* a1 */
	sw[-2] = (uint32_t)(uintptr_t)param;	/* a2 */
	sw[-1] = (uint32_t)(uintptr_t)fn;	/* a3 */
	return (StackType_t *)sw;
}

/* --- the tick ------------------------------------------------------------------ */

#define TICK_CYCLES	(core_hz / configTICK_RATE_HZ)

static uint32_t tick_users;

static uint32_t ccount_now(void)
{
	uint32_t v;

	__asm__ volatile("rsr.ccount %0" : "=a"(v));
	return v;
}

static void ccompare1(uint32_t v)
{
	__asm__ volatile("wsr.ccompare1 %0; rsync" : : "a"(v) : "memory");
}

/* interrupt 7, from the deferred-interrupt loop (scp/src/arch_interrupt.c) */
static void tick_isr(void)
{
	if (!tick_users) {
		arch_irq_line_enable(IRQ_TIMER1, false);
		ccompare1(ccount_now() - 1);	/* clears it; next match ~10 s away */
		return;
	}
	ccompare1(ccount_now() + TICK_CYCLES);
	(void)xTaskIncrementTick();
}

/* before a wait with a time-out: the tick runs until the matching release */
void port_tick_hold(void)
{
	uint32_t ps = port_irq_disable();

	if (tick_users++ == 0) {
		ccompare1(ccount_now() + TICK_CYCLES);
		arch_irq_line_enable(IRQ_TIMER1, true);
	}
	port_irq_restore(ps);
}

void port_tick_release(void)
{
	uint32_t ps = port_irq_disable();

	if (tick_users)
		tick_users--;
	port_irq_restore(ps);
}

BaseType_t xPortStartScheduler(void)
{
	arch_irq_set_kernel_isr(IRQ_TIMER1, tick_isr);
	port_start_first();
	return pdFALSE;
}

void vPortEndScheduler(void)
{
}
