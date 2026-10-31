/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* The PM's architecture layer for SCP-firmware */
#ifndef ARCH_SCP_H
#define ARCH_SCP_H

#include <fwk_arch.h>

int arch_interrupt_init(const struct fwk_arch_interrupt_driver **driver);
bool arch_irq_run(void);	/* run the handlers of pending lines */
void arch_irq_wait(void);	/* sleep until an interrupt */

/* Initialise and start SCP-firmware's modules, then serve events forever */
void scp_init(void);
void scp_run(void) __attribute__((__noreturn__));

#endif
