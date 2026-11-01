/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The PM's local GPIOs (CIX's Source/common/drivers/dev_gpio.c; dev_gpio_init
 * is src/lcru.c's): pins the BIOS configuration gives a function (gpio_config
 * in pm_config.c): a heartbeat LED toggled every second, and a line that
 * tells the EC a fan mode changed (the EC then reads it over the I2C slave,
 * i2c_slave.c).  No board's configuration gives either; both are off
 * (pin 3) then.
 *
 * Registers at 0xa0020000: +0x04 [pin] input enable (cleared for an
 * output), +0x08 [pin] output enable (also interrupt mask), +0x0c output,
 * +0x10 input, +0x14 and +0x1c interrupt status and its clear, +0x18 enable,
 * +0x20 raw status, +0x24-+0x2c trigger type.
 */
#include <mod_pm_svc.h>

#include "board/board.h"
#include "lcru.h"

#define GPIO_IN_EN		(LOCAL_GPIO + 0x04)
#define GPIO_OUT_EN		(LOCAL_GPIO + 0x08)
#define GPIO_OUT		(LOCAL_GPIO + 0x0c)
#define GPIO_IN			(LOCAL_GPIO + 0x10)
#define GPIO_INT_CLR		(LOCAL_GPIO + 0x1c)
#define GPIO_INT_RAW		(LOCAL_GPIO + 0x20)
#define GPIO_PINS		32
#define GPIO_FUNC_PINS		3
#define HEARTBEAT_US		1000000u

uint32_t gpio_heartbeat = GPIO_FUNC_PINS;
uint32_t gpio_to_ec = GPIO_FUNC_PINS;

void dev_gpio_set(uint32_t pin, uint32_t val)
{
	if (pin >= GPIO_PINS)
		return;
	if (val)
		REG32(GPIO_OUT) |= BIT(pin);
	else
		REG32(GPIO_OUT) &= ~BIT(pin);
}

uint32_t dev_gpio_get(uint32_t pin)
{
	return pin < GPIO_PINS ? (REG32(GPIO_IN) >> pin) & 1 : 0;
}

void dev_gpio_signal_ec_event(void)
{
	if (gpio_to_ec < GPIO_FUNC_PINS)
		dev_gpio_set(gpio_to_ec, 1);
}

void dev_gpio_clear_ec_event(void)
{
	if (gpio_to_ec < GPIO_FUNC_PINS)
		dev_gpio_set(gpio_to_ec, 0);
}

/* a pin an output */
static void gpio_output(uint32_t pin)
{
	REG32(GPIO_IN_EN) &= ~BIT(pin);
	REG32(GPIO_OUT_EN) |= BIT(pin);
}

/* CIX's dev_gpio_ec_handshake_init: the fan mode the EC reads starts at 3
 * (Linux's mode 1, temperature), the event line low */
void dev_gpio_ec_handshake_init(void)
{
	DEV_REG[CUS_FAN_MODE_REG][1] = 3;
	if (gpio_to_ec >= GPIO_FUNC_PINS)
		return;
	gpio_output(gpio_to_ec);
	dev_gpio_set(gpio_to_ec, 0);
}

/* CIX's dev_gpio_heartbeat */
static void dev_gpio_heartbeat(void *arg)
{
	static uint8_t led_status;

	(void)arg;
	led_status ^= 1;
	dev_gpio_set(gpio_heartbeat, led_status);
}

/* each time RUN is entered; CIX allocates another timer each time (after a
 * resume two toggle the LED), here the one keeps running */
void dev_gpio_heartbeat_init(void)
{
	static uint8_t timer = PM_SVC_TIMER_NONE;

	if (gpio_heartbeat >= GPIO_FUNC_PINS)
		return;
	gpio_output(gpio_heartbeat);
	if (timer == PM_SVC_TIMER_NONE)
		timer = pm_svc_timer_alloc(HEARTBEAT_US, dev_gpio_heartbeat, NULL, true, true);
}

/* CIX's gpio_intr_handler (aggregator line 21, source 2): every pin's
 * interrupt cleared, the raw status logged */
void gpio_intr_handler(void)
{
	(void)REG32(GPIO_INT_RAW);
	REG32(GPIO_INT_CLR) = 0xffffffffu;
}
