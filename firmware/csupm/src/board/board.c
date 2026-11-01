/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The board services' start (CIX's pm_ready and ss_run_enter) and their
 * interrupts (CIX's csu_irq_aggregator_map: the aggregator lines and the
 * handler of each source, run by its _intr_lv2_dispatcher until nothing
 * is pending).
 *
 *   21  mixed: 0 the watchdog, 1 UART, 2 GPIO, 3 the timer watchdog
 *   24  I2C, a source per controller (the slave's: src/i2c.c)
 *   28  PVT back below the safe threshold, a source per sensor (src/pvt.c)
 *   29  PVT above the warning threshold
 *   30  PROCHOT (one source)
 * Lines 22 and 23 (the counters: the fans' tachometers) are
 * src/perf/dev_counters.c's, 25 the mailboxes', 18 the S5 controller's.
 */
#include <mod_pm_svc.h>

#include <fwk_interrupt.h>

#include "board/board.h"
#include "fan/fan.h"
#include "i2c.h"
#include "irq.h"
#include "pvt.h"

#define MIXED_LINE		21
#define I2C_LINE		24
#define PVT_SAFE_LINE		28
#define PVT_WARN_LINE		29
#define PROCHOT_LINE		30
#define MIXED_WDT		0
#define MIXED_GPIO		2
#define WDT_FIRED		0x200bef38u	/* CIX's scratch word for the AP */

void config_board_sensor_start(void);

/* CIX's wdt_intr_handler: the watchdog's first time-out, reloaded and
 * counted where the AP can see it */
void wdt_intr_handler(void)
{
	static uint32_t total_wdt_fired;

	if (!(REG32(AGG_STATUS(MIXED_LINE)) & BIT(MIXED_WDT)))
		return;
	dev_wdt_refresh();
	REG32(WDT_FIRED) = ++total_wdt_fired;
}

/* CIX's mixed_intr_isr for each source pending (UART's and the timer
 * watchdog's handlers are empty in CIX's image) */
static void mixed_isr(void)
{
	uint32_t p;

	while ((p = REG32(AGG_STATUS(MIXED_LINE)) & REG32(AGG_ENABLE(MIXED_LINE))) != 0) {
		if (p & BIT(MIXED_WDT))
			wdt_intr_handler();
		if (p & BIT(MIXED_GPIO))
			gpio_intr_handler();
		if (REG32(AGG_STATUS(MIXED_LINE)) & REG32(AGG_ENABLE(MIXED_LINE)) & p)
			break;		/* a source no handler clears */
	}
}

static void isr(unsigned int line, void (*fn)(void))
{
	if (fwk_interrupt_set_isr(line, fn) == FWK_SUCCESS)
		(void)fwk_interrupt_enable(line);
}

/* the handlers, as the services' module starts (CIX: intr_platform_init) */
void board_irq_init(void)
{
	isr(MIXED_LINE, mixed_isr);
	isr(I2C_LINE, dev_i2c_isr);
	isr(PVT_SAFE_LINE, pvt_safe_irq);
	isr(PVT_WARN_LINE, pvt_warning_irq);
	isr(PROCHOT_LINE, prochot_outside_isr);
	config_board_sensor_start();
}

/* CIX's ss_run_enter: PROCHOT's timer, the EC's handshake line, the
 * heartbeat (each time RUN is entered, as CIX's) */
void board_services_run(void)
{
	init_prochot_timer();
	dev_gpio_ec_handshake_init();
	dev_gpio_heartbeat_init();
}

/* CIX's pm_ready, once the AP may run: the SoC idle switch and DSU mode as
 * the configuration says, the fans, the PVT polling */
static void pm_ready_services(void *arg)
{
	(void)arg;
	noc_idle_config();
	op_mode_config();
	fan_ctrl_start();
	sky1_pvt_fixed_polling_enable();
}

/* CIX's pm_ready runs from its event worker once the PM first idles after
 * the handshake; here a millisecond after it */
void board_services_start(void)
{
	(void)pm_svc_timer_alloc(1000, pm_ready_services, NULL, true, false);
}
