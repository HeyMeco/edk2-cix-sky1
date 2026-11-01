/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* The PM's architecture layer for SCP-firmware */
#ifndef ARCH_SCP_H
#define ARCH_SCP_H

#include <fwk_arch.h>

int arch_interrupt_init(const struct fwk_arch_interrupt_driver **driver);
bool arch_irq_run(void);	/* run the handlers of pending lines */
void arch_irq_wait(void);	/* sleep until an interrupt */

/*
 * The lines whose handlers may run while the SCP-firmware task is in the
 * middle of an event (src/rtos.c's idle task runs them): the RTOS tick
 * (7) and the I2C controllers (24).  Their handlers do only what an
 * interrupt handler may on any SCP-firmware platform (wake a task, put an
 * event).  Every other line, and the software ones, wait for the
 * SCP-firmware task's loop, as without an RTOS.
 */
#define ARCH_IRQ_KERNEL		((1u << 7) | (1u << 24))

/* the handlers of the pending lines in lines (and the software lines when
 * soft); the lines run, as a mask */
uint32_t arch_irq_run_mask(uint32_t lines, bool soft);
/* whether one of them is pending */
bool arch_irq_pending_in(uint32_t lines, bool soft);
/* sleep until an interrupt, unless one of them is pending */
void arch_irq_wait_for(uint32_t lines, bool soft);
/* a handler outside SCP-firmware's (the tick), its line enabled */
void arch_irq_set_kernel_isr(unsigned int line, void (*fn)(void));
void arch_irq_line_enable(unsigned int line, bool on);

/* Initialise and start SCP-firmware's modules, then serve events forever */
void scp_init(void);
void scp_run(void) __attribute__((__noreturn__));	/* the SCP-firmware task */

#endif
