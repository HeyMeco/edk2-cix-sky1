/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * CCOUNT/CCOMPARE timer driver.
 *
 * CCOUNT wraps every 2^32 cycles (about 11 s at 400 MHz).  The high word
 * is kept here and advanced whenever a read finds CCOUNT below the last
 * one; for that to see every wrap, CCOMPARE is never set more than 2^31
 * cycles ahead and fires even while the timer is disabled.  The CCOMPARE
 * interrupt is the driver's own: only when the alarm is due does it make
 * the timer module's interrupt (a software line) pending, so the module
 * never sees an early or spurious one.  Writing CCOMPARE clears its
 * interrupt.
 */
#include <mod_timer.h>
#include <mod_xt_timer.h>

#include <fwk_id.h>
#include <fwk_interrupt.h>
#include <fwk_module.h>
#include <fwk_status.h>

#include <arch_helpers.h>

#include "lcru.h"

#define HALF		0x80000000u

static struct {
	const struct mod_xt_timer_config *config;
	uint32_t high, last;
	uint64_t target;
	bool enabled;
} xt_timer;

static void wsr_ccompare(uint32_t v)
{
	switch (xt_timer.config->ccompare) {
	case 0:
		__asm__ volatile("wsr.ccompare0 %0; rsync" : : "a"(v));
		break;
	case 1:
		__asm__ volatile("wsr.ccompare1 %0; rsync" : : "a"(v));
		break;
	default:
		__asm__ volatile("wsr.ccompare2 %0; rsync" : : "a"(v));
		break;
	}
}

/* With interrupts masked */
static uint64_t counter(void)
{
	uint32_t c = ccount();

	if (c < xt_timer.last)
		xt_timer.high++;
	xt_timer.last = c;
	return (uint64_t)xt_timer.high << 32 | c;
}

/*
 * Program CCOMPARE for the target, or half a wrap ahead if it is later.
 * A target closer than MARGIN cycles counts as due: CCOUNT could pass it
 * before the write lands, and CCOMPARE only matches when CCOUNT equals it,
 * so the alarm would wait for the next wrap (an alarm asked for 0 ms from
 * now, as pm_svc's for a timer already due, came that close).
 */
#define MARGIN		256u

static void program(void)
{
	uint64_t now = counter();
	uint32_t c = (uint32_t)now;

	if (xt_timer.enabled && xt_timer.target < now + MARGIN)
		wsr_ccompare(c + MARGIN);		/* due: fire right away */
	else if (xt_timer.enabled && xt_timer.target - now < HALF)
		wsr_ccompare((uint32_t)xt_timer.target);
	else
		wsr_ccompare(c + HALF);
}

static void ccompare_isr(void)
{
	uint64_t now = counter();

	if (xt_timer.enabled && xt_timer.target <= now) {
		xt_timer.enabled = false;
		(void)fwk_interrupt_set_pending(xt_timer.config->alarm_irq);
	}
	program();
}

static int timer_enable(fwk_id_t id)
{
	unsigned int ps = arch_interrupts_disable();

	(void)id;
	xt_timer.enabled = true;
	program();
	arch_interrupts_enable(ps);
	return FWK_SUCCESS;
}

static int timer_disable(fwk_id_t id)
{
	unsigned int ps = arch_interrupts_disable();

	(void)id;
	xt_timer.enabled = false;
	program();
	arch_interrupts_enable(ps);
	return FWK_SUCCESS;
}

static int timer_set(fwk_id_t id, uint64_t timestamp)
{
	unsigned int ps = arch_interrupts_disable();

	(void)id;
	xt_timer.target = timestamp;
	program();
	arch_interrupts_enable(ps);
	return FWK_SUCCESS;
}

static int timer_get(fwk_id_t id, uint64_t *timestamp)
{
	(void)id;
	*timestamp = xt_timer.target;
	return FWK_SUCCESS;
}

static int timer_counter(fwk_id_t id, uint64_t *value)
{
	unsigned int ps = arch_interrupts_disable();

	(void)id;
	*value = counter();
	arch_interrupts_enable(ps);
	return FWK_SUCCESS;
}

static int timer_frequency(fwk_id_t id, uint32_t *value)
{
	(void)id;
	*value = core_hz;
	return FWK_SUCCESS;
}

static const struct mod_timer_driver_api driver_api = {
	.name = "CCOUNT",
	.enable = timer_enable,
	.disable = timer_disable,
	.set_timer = timer_set,
	.get_timer = timer_get,
	.get_counter = timer_counter,
	.get_frequency = timer_frequency,
};

static int xt_timer_init(fwk_id_t module_id, unsigned int count, const void *data)
{
	(void)module_id;
	(void)data;
	return count == 1 ? FWK_SUCCESS : FWK_E_PARAM;
}

static int xt_timer_element_init(fwk_id_t id, unsigned int sub, const void *data)
{
	(void)id;
	(void)sub;
	if (data == NULL)
		return FWK_E_PARAM;
	xt_timer.config = data;
	xt_timer.last = ccount();
	return FWK_SUCCESS;
}

static int xt_timer_process_bind_request(fwk_id_t source_id, fwk_id_t target_id,
					 fwk_id_t api_id, const void **api)
{
	(void)source_id;
	(void)target_id;
	(void)api_id;
	*api = &driver_api;
	return FWK_SUCCESS;
}

static int xt_timer_start(fwk_id_t id)
{
	int status;

	if (!fwk_id_is_type(id, FWK_ID_TYPE_ELEMENT))
		return FWK_SUCCESS;
	(void)timer_disable(id);
	status = fwk_interrupt_set_isr(xt_timer.config->irq, ccompare_isr);
	if (status != FWK_SUCCESS)
		return status;
	return fwk_interrupt_enable(xt_timer.config->irq);
}

const struct fwk_module module_xt_timer = {
	.type = FWK_MODULE_TYPE_DRIVER,
	.api_count = MOD_XT_TIMER_API_IDX_COUNT,
	.init = xt_timer_init,
	.element_init = xt_timer_element_init,
	.start = xt_timer_start,
	.process_bind_request = xt_timer_process_bind_request,
};
