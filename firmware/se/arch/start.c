/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Reset, vector table and interrupt dispatch (CIX arch/star_se).
 *
 * The boot ROM copies the image to the start of SRAM and jumps to the reset
 * vector with interrupts as it left them.  All external interrupts go
 * through Default_Handler, which calls the handler registered for the
 * interrupt with core_irq_register().
 */
#include "se.h"
#include "arch.h"

extern uint32_t __stack_top[], __stack_limit[];
extern uint32_t __data_start[], __data_end[], __bss_start[], __bss_end[];

void Reset_Handler(void);
void Default_Handler(void);
void main(void);
void hard_fault_policy(uint32_t exc_return, uint32_t *sp);

static struct {
	void (*isr)(void *ctx);
	void *ctx;
} g_irqn_context[256];

/* Faults: hand the stacked frame to the error policy (framework/fw_err_handler.c) */
#define FAULT_HANDLER(name) \
	__attribute__((naked)) void name(void) \
	{ \
		__asm__ volatile("push {r3, lr}\n" \
				 "mov r0, lr\n" \
				 "mov r1, sp\n" \
				 "bl hard_fault_policy\n" \
				 "pop {r3, pc}\n"); \
	}

FAULT_HANDLER(NMI_Handler)
FAULT_HANDLER(HardFault_Handler)
FAULT_HANDLER(MemManage_Handler)
FAULT_HANDLER(BusFault_Handler)
FAULT_HANDLER(UsageFault_Handler)
FAULT_HANDLER(SecureFault_Handler)
FAULT_HANDLER(DebugMon_Handler)
FAULT_HANDLER(PendSV_Handler)

void SVC_Handler(void)
{
	for (;;)
		;
}

void SysTick_Handler(void)
{
	for (;;)
		;
}

typedef void (*vector_t)(void);

__attribute__((section(".vectors"), used))
const vector_t __Vectors[16 + SE_IRQ_COUNT] = {
	[0] = (vector_t)__stack_top,
	[1] = Reset_Handler,
	[2] = NMI_Handler,
	[3] = HardFault_Handler,
	[4] = MemManage_Handler,
	[5] = BusFault_Handler,
	[6] = UsageFault_Handler,
	[7] = SecureFault_Handler,
	[11] = SVC_Handler,
	[12] = DebugMon_Handler,
	[14] = PendSV_Handler,
	[15] = SysTick_Handler,
	[16 ... 16 + SE_IRQ_COUNT - 1] = Default_Handler,
};

void core_init(void)
{
	REG32(SCB_VTOR) = (uint32_t)(uintptr_t)__Vectors;
	REG32(SCB_CPACR) |= 0xf00000;		/* CP10, CP11: FPU full access */
}

/*
 * The image is loaded with .data in place, and the ROM leaves .bss and the
 * stack zeroed only on cold boot, so clear .bss here.
 */
__attribute__((naked, noreturn)) void Reset_Handler(void)
{
	__asm__ volatile(
		"cpsid i\n"
		"ldr r3, =__stack_top\n"
		"msr msp, r3\n"
		"msr psp, r3\n"
		"ldr r3, =__stack_limit\n"
		"msr msplim, r3\n"
		"msr psplim, r3\n"
		"bl reset_c\n");
}

__attribute__((noreturn, used)) void reset_c(void)
{
	memset(__bss_start, 0, (uintptr_t)__bss_end - (uintptr_t)__bss_start);
	core_init();
	hal_pc_trace(0x9001);
	hal_pc_trace(0x9002);
	__asm__ volatile("cpsie i\ncpsie f");
	main();
	for (;;)
		;
}

void core_irq_register(int irq, void (*isr)(void *), void *ctx)
{
	g_irqn_context[irq].isr = isr;
	g_irqn_context[irq].ctx = ctx;
	dsb();
}

void core_irq_unregister(int irq)
{
	g_irqn_context[irq].isr = NULL;
	g_irqn_context[irq].ctx = NULL;
	dsb();
}

void core_irq_enable(int irq)
{
	if (irq >= 0)
		REG32(NVIC_ISER + (irq >> 5) * 4) = BIT(irq & 31);
	dsb();
}

void core_irq_disable(int irq)
{
	if (irq >= 0)
		REG32(NVIC_ICER + (irq >> 5) * 4) = BIT(irq & 31);
	dsb();
}

/* Mask interrupts; returns PRIMASK as it was, for core_irq_restore() */
uint32_t core_irq_save(void)
{
	uint32_t primask;

	__asm__ volatile("mrs %0, primask\ncpsid i" : "=r"(primask) :: "memory");
	dsb();
	return primask;
}

void core_irq_restore(uint32_t primask)
{
	if (primask == 0)
		__asm__ volatile("cpsie i" ::: "memory");
	else
		__asm__ volatile("cpsid i" ::: "memory");
	dsb();
}

void Default_Handler(void)
{
	uint32_t ipsr;
	unsigned int irq;

	__asm__ volatile("mrs %0, ipsr" : "=r"(ipsr));
	irq = (ipsr & 0x1ff) - 16;
	if (irq >= ARRAY_SIZE(g_irqn_context)) {
		hal_printf_level(LOG_ERR, "unknow nvic irq source %d \n", irq);
		return;
	}
	if (g_irqn_context[irq].isr)
		g_irqn_context[irq].isr(g_irqn_context[irq].ctx);
	dsb();
}

/* Priority in the top 3 bits; negative numbers are system exceptions */
void core_irq_set_priority(int irq, uint32_t prio)
{
	if (irq < 0)
		REG8(SCB_SHPR + (irq & 0xf) - 4) = prio << 5;
	else
		REG8(NVIC_IPR + irq) = prio << 5;
	dsb();
}

void core_irq_clear(int irq)
{
	if (irq >= 0)
		REG32(NVIC_ICPR + (irq >> 5) * 4) = BIT(irq & 31);
	dsb();
}
