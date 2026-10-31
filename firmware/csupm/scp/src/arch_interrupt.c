/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * SCP-firmware's interrupt driver for the PM's core.
 *
 * Interrupt numbers 0-31 are the core's lines; 32-63 exist only here, for
 * drivers that hand an interrupt on (set_pending makes them pending, the
 * loop runs them like the others).  The vectors (arch/vectors.S)
 * only mask a pending line and note it in arch_irq_pending; arch_irq_run
 * calls the handlers from the main loop, with interrupts masked and
 * fwk_is_interrupt_context() true, then unmasks the lines the framework
 * keeps enabled.  A handler has to clear its source before it returns.
 */
#include <fwk_arch.h>
#include <fwk_status.h>

#include <arch_helpers.h>

#include "arch_scp.h"
#include "irq.h"

#define NO_IRQ			UINT32_MAX
#define N_IRQ			64
#define SOFT(irq)		((irq) >= 32)
#define SBIT(irq)		BIT((irq) - 32)

static struct {
	void (*fn)(uintptr_t param);
	void (*fn_noparam)(void);
	uintptr_t param;
} isr[N_IRQ];
static uint32_t enabled;		/* lines the framework has enabled */
static uint32_t soft_enabled, soft_pending;
static uint32_t current = NO_IRQ;

static uint32_t rsr_intenable(void)
{
	uint32_t v;

	__asm__ volatile("rsr.intenable %0" : "=a"(v));
	return v;
}

static void wsr_intenable(uint32_t v)
{
	__asm__ volatile("wsr.intenable %0; rsync" : : "a"(v) : "memory");
}

static int global_enable(void)
{
	__asm__ volatile("rsil a2, 0" : : : "a2", "memory");
	return FWK_SUCCESS;
}

static int global_disable(void)
{
	(void)arch_interrupts_disable();
	return FWK_SUCCESS;
}

static int is_enabled(unsigned int irq, bool *on)
{
	if (irq >= N_IRQ)
		return FWK_E_PARAM;
	*on = SOFT(irq) ? soft_enabled & SBIT(irq) : enabled & BIT(irq);
	return FWK_SUCCESS;
}

static int enable(unsigned int irq)
{
	unsigned int ps;

	if (irq >= N_IRQ || irq == IRQ_NMI)
		return FWK_E_PARAM;
	ps = arch_interrupts_disable();
	if (SOFT(irq)) {
		soft_enabled |= SBIT(irq);
		arch_interrupts_enable(ps);
		return FWK_SUCCESS;
	}
	enabled |= BIT(irq);
	if (!(arch_irq_pending & BIT(irq)))
		wsr_intenable(rsr_intenable() | BIT(irq));
	arch_interrupts_enable(ps);
	return FWK_SUCCESS;
}

static int disable(unsigned int irq)
{
	unsigned int ps;

	if (irq >= N_IRQ)
		return FWK_E_PARAM;
	ps = arch_interrupts_disable();
	if (SOFT(irq)) {
		soft_enabled &= ~SBIT(irq);
		arch_interrupts_enable(ps);
		return FWK_SUCCESS;
	}
	enabled &= ~BIT(irq);
	wsr_intenable(rsr_intenable() & ~BIT(irq));
	arch_interrupts_enable(ps);
	return FWK_SUCCESS;
}

static int is_pending(unsigned int irq, bool *pending)
{
	uint32_t v;

	if (irq >= N_IRQ)
		return FWK_E_PARAM;
	if (SOFT(irq)) {
		*pending = soft_pending & SBIT(irq);
		return FWK_SUCCESS;
	}
	__asm__ volatile("rsr.interrupt %0" : "=a"(v));
	*pending = (v | arch_irq_pending) & BIT(irq);
	return FWK_SUCCESS;
}

static int set_pending(unsigned int irq)
{
	if (irq >= N_IRQ)
		return FWK_E_PARAM;
	if (SOFT(irq)) {
		unsigned int ps = arch_interrupts_disable();

		soft_pending |= SBIT(irq);
		arch_interrupts_enable(ps);
		return FWK_SUCCESS;
	}
	__asm__ volatile("wsr.intset %0; rsync" : : "a"(BIT(irq)) : "memory");
	return FWK_SUCCESS;
}

static int clear_pending(unsigned int irq)
{
	unsigned int ps;

	if (irq >= N_IRQ)
		return FWK_E_PARAM;
	ps = arch_interrupts_disable();
	if (SOFT(irq)) {
		soft_pending &= ~SBIT(irq);
		arch_interrupts_enable(ps);
		return FWK_SUCCESS;
	}
	/* a line the vector masked comes back: if its source is still
	 * asserted it fires again, as a level interrupt would */
	if ((arch_irq_pending & BIT(irq)) && (enabled & BIT(irq)))
		wsr_intenable(rsr_intenable() | BIT(irq));
	arch_irq_pending &= ~BIT(irq);
	__asm__ volatile("wsr.intclear %0; rsync" : : "a"(BIT(irq)) : "memory");
	arch_interrupts_enable(ps);
	return FWK_SUCCESS;
}

static int set_isr_irq(unsigned int irq, void (*fn)(void))
{
	if (irq >= N_IRQ)
		return FWK_E_PARAM;
	isr[irq].fn = NULL;
	isr[irq].fn_noparam = fn;
	return FWK_SUCCESS;
}

static int set_isr_irq_param(unsigned int irq, void (*fn)(uintptr_t), uintptr_t param)
{
	if (irq >= N_IRQ)
		return FWK_E_PARAM;
	isr[irq].fn = fn;
	isr[irq].fn_noparam = NULL;
	isr[irq].param = param;
	return FWK_SUCCESS;
}

/* The NMI and faults stop the core in arch_panic */
static int set_isr_nmi(void (*fn)(void))
{
	(void)fn;
	return FWK_E_SUPPORT;
}

static int set_isr_nmi_param(void (*fn)(uintptr_t), uintptr_t param)
{
	(void)fn;
	(void)param;
	return FWK_E_SUPPORT;
}

static int set_isr_fault(void (*fn)(void))
{
	(void)fn;
	return FWK_E_SUPPORT;
}

static int get_current(unsigned int *irq)
{
	if (current == NO_IRQ)
		return FWK_E_STATE;
	*irq = current;
	return FWK_SUCCESS;
}

static bool is_interrupt_context(void)
{
	return current != NO_IRQ;
}

static const struct fwk_arch_interrupt_driver driver = {
	.global_enable = global_enable,
	.global_disable = global_disable,
	.is_enabled = is_enabled,
	.enable = enable,
	.disable = disable,
	.is_pending = is_pending,
	.set_pending = set_pending,
	.clear_pending = clear_pending,
	.set_isr_irq = set_isr_irq,
	.set_isr_irq_param = set_isr_irq_param,
	.set_isr_nmi = set_isr_nmi,
	.set_isr_nmi_param = set_isr_nmi_param,
	.set_isr_fault = set_isr_fault,
	.get_current = get_current,
	.is_interrupt_context = is_interrupt_context,
};

int arch_interrupt_init(const struct fwk_arch_interrupt_driver **out)
{
	*out = &driver;
	return FWK_SUCCESS;
}

static void call(unsigned int i)
{
	current = i;
	if (isr[i].fn)
		isr[i].fn(isr[i].param);
	else if (isr[i].fn_noparam)
		isr[i].fn_noparam();
	current = NO_IRQ;
}

bool arch_irq_run(void)
{
	unsigned int ps = arch_interrupts_disable();
	bool any = false;

	for (;;) {
		uint32_t pending = arch_irq_pending, soft = soft_pending & soft_enabled;

		if (!pending && !soft)
			break;
		any = true;
		arch_irq_pending = 0;
		soft_pending &= ~soft;
		for (unsigned i = 0; i < 32; i++) {
			if (pending & BIT(i))
				call(i);
		}
		for (unsigned i = 0; i < 32; i++) {
			if (soft & BIT(i))
				call(32 + i);
		}
		wsr_intenable(rsr_intenable() | (pending & enabled));
	}
	arch_interrupts_enable(ps);
	return any;
}

void arch_irq_wait(void)
{
	unsigned int ps = arch_interrupts_disable();

	/* WAITI lowers PS.INTLEVEL and sleeps in one step: nothing is missed */
	if (!arch_irq_pending && !(soft_pending & soft_enabled))
		__asm__ volatile("waiti 0" : : : "memory");
	arch_interrupts_enable(ps);
}
