/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * CSU_PM firmware, stage 1: start, tell the SE, wait for its SoC
 * initialisation, tell it to start the AP, idle.
 */
#include "pm.h"

/* The SE waits up to 5 s for each step (firmware/se/service/main.c) */
static void se_handshake(void)
{
	REG32(PM_MAGIC) = PM_MAGIC_UP;
	while (REG32(PM_MAGIC) != PM_MAGIC_SE_DONE)
		;
	REG32(PM_MAGIC) = 0;
	REG32(PM_MAGIC) = PM_MAGIC_READY;
}

__attribute__((noinline)) void arch_idle(void)
{
	/* nothing to wait for yet: no interrupt is enabled */
	__asm__ volatile("" ::: "memory");
}

int main(void)
{
	se_handshake();
	for (;;)
		arch_idle();
}
