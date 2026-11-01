/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * FreeRTOS port for CSU_PM (Xtensa LX7, windowed ABI, XEA2, one core).
 *
 * The scheduler is co-operative (FreeRTOSConfig.h), so a task switch only
 * ever happens at a call: portYIELD, from task code.  At a call the
 * windowed ABI leaves nothing live outside the register windows (SAR, the
 * loop registers, the booleans and the FPU registers are all the caller's
 * to save), so the switch spills the windows to the stacks and changes
 * stack pointer (portasm.S).  No vector ever switches.
 */
#ifndef PORTMACRO_H
#define PORTMACRO_H

#include <stdint.h>

#define portCHAR		char
#define portFLOAT		float
#define portDOUBLE		double
#define portLONG		long
#define portSHORT		short
#define portSTACK_TYPE		uint32_t
#define portBASE_TYPE		long

typedef portSTACK_TYPE StackType_t;
typedef long BaseType_t;
typedef unsigned long UBaseType_t;
typedef uint32_t TickType_t;

#define portMAX_DELAY		((TickType_t)0xffffffffu)
#define portTICK_TYPE_IS_ATOMIC	1
#define portSTACK_GROWTH	(-1)
#define portTICK_PERIOD_MS	((TickType_t)1000 / configTICK_RATE_HZ)
#define portBYTE_ALIGNMENT	16
#define portNOP()		__asm__ volatile("nop")
#define portMEMORY_BARRIER()	__asm__ volatile("" ::: "memory")

/* interrupts: PS.INTLEVEL 15 (what arch_interrupts_disable does) */
static inline uint32_t port_irq_disable(void)
{
	uint32_t ps;

	__asm__ volatile("rsil %0, 15" : "=a"(ps) : : "memory");
	return ps;
}

static inline void port_irq_restore(uint32_t ps)
{
	__asm__ volatile("wsr.ps %0; rsync" : : "a"(ps) : "memory");
}

static inline void port_irq_enable(void)
{
	uint32_t ps;

	__asm__ volatile("rsil %0, 0" : "=a"(ps) : : "memory");
	(void)ps;
}

void vPortEnterCritical(void);
void vPortExitCritical(void);
void vPortYield(void);

#define portDISABLE_INTERRUPTS()			(void)port_irq_disable()
#define portENABLE_INTERRUPTS()				port_irq_enable()
#define portENTER_CRITICAL()				vPortEnterCritical()
#define portEXIT_CRITICAL()				vPortExitCritical()
#define portSET_INTERRUPT_MASK_FROM_ISR()		port_irq_disable()
#define portCLEAR_INTERRUPT_MASK_FROM_ISR(x)		port_irq_restore(x)
#define portYIELD()					vPortYield()
#define portYIELD_FROM_ISR(x)				((void)(x))
#define portEND_SWITCHING_ISR(x)			((void)(x))

#define portTASK_FUNCTION_PROTO(fn, params)		void fn(void *params)
#define portTASK_FUNCTION(fn, params)			void fn(void *params)

#endif
