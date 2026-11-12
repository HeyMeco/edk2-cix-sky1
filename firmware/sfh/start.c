/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Vector table and reset for the SFH core.  Every interrupt goes through
 * one dispatcher (irq.c), as in the CIX image.
 */
#include "sfh.h"

extern uint32_t __bss_start[], __bss_end[], __stack_top[];
int main(void);

void reset_handler(void)
{
	for (uint32_t *p = __bss_start; p < __bss_end; p++)
		*p = 0;
	main();
	for (;;)
		;
}

/* NMI: leave a marker in SRAM and stop */
static void nmi_handler(void)
{
	REG32(0x20060000u) = 0x11;
	for (;;)
		;
}

/*
 * HardFault: stop.  (CIX's handler logs the fault and stops in the function
 * that does; the restart its vector code has after that call is never
 * reached.  A restart from here would run the firmware inside the fault,
 * where no interrupt is taken.)
 */
static void fault_handler(void)
{
	for (;;)
		;
}

static void hang(void)
{
	for (;;)
		;
}

#define N_IRQ 64
__attribute__((section(".vectors"), used))
const void *const vectors[16 + N_IRQ] = {
	[0] = __stack_top,
	[1] = reset_handler,
	[2] = nmi_handler,
	[3] = fault_handler,
	[4] = hang, [5] = hang, [6] = hang, [7] = hang,
	[11] = hang,
	[13] = irq_dispatch, [14] = irq_dispatch, [15] = irq_dispatch,
	[16 ... 16 + N_IRQ - 1] = irq_dispatch,
};
