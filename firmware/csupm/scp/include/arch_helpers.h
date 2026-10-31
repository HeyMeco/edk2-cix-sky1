/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * SCP-firmware's architecture helpers for the PM's Xtensa core: critical
 * sections raise PS.INTLEVEL to mask every maskable level.
 */
#ifndef ARCH_HELPERS_H
#define ARCH_HELPERS_H

static inline unsigned int arch_interrupts_disable(void)
{
	unsigned int ps;

	__asm__ volatile("rsil %0, 15" : "=a"(ps) : : "memory");
	return ps;
}

static inline void arch_interrupts_enable(unsigned int ps)
{
	__asm__ volatile("wsr.ps %0; rsync" : : "a"(ps) : "memory");
}

/* The main loop sleeps itself (scp/src/arch_main.c) */
static inline void arch_suspend(void)
{
}

#endif
