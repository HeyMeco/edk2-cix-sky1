/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "sfh.h"

#define NVIC_ISER(n)	(0xe000e100u + 4 * (n))
#define SCB_VTOR	0xe000ed08u
#define SCB_CCR		0xe000ed14u
#define SCB_SHCSR	0xe000ed24u
#define N_IRQ		64

static struct {
	irq_handler_t	handler;
	void		*arg;
} irq_table[N_IRQ];

void irq_register(int irq, irq_handler_t handler, void *arg)
{
	irq_table[irq].handler = handler;
	irq_table[irq].arg = arg;
	__asm__ volatile("dsb" ::: "memory");
}

void irq_enable(int irq)
{
	if (irq >= 0)
		REG32(NVIC_ISER(irq >> 5)) = 1u << (irq & 31);
	__asm__ volatile("dsb" ::: "memory");
}

void irq_dispatch(void)
{
	uint32_t ipsr;
	int irq;

	__asm__ volatile("mrs %0, ipsr" : "=r"(ipsr));
	irq = (int)ipsr - 16;
	if (irq < 0 || irq >= N_IRQ) {
		log_printf(LOG_ERR, "unknown nvic irq source %d\n", irq);
		return;
	}
	if (irq_table[irq].handler)
		irq_table[irq].handler(irq_table[irq].arg);
	__asm__ volatile("dsb" ::: "memory");
}

/* Vector table, process stack, interrupts and fault reporting on */
void cpu_setup(void)
{
	REG32(SCB_VTOR) = 0x20000000u;
	__asm__ volatile("dsb\n\tisb" ::: "memory");
	__asm__ volatile("msr psp, %0" :: "r"(0x2007dff0u));
	__asm__ volatile("cpsie i" ::: "memory");
	REG32(SCB_CCR) |= 0x18;		/* trap divide by zero and unaligned */
	REG32(SCB_SHCSR) |= 0x70000;	/* MemManage, BusFault, UsageFault */
	__asm__ volatile("cpsie f" ::: "memory");
}
