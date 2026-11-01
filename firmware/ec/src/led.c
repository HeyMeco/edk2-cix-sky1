/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* The power LED (BLINK_L on PWM1, active low): on in S0, off otherwise */
#include "ec.h"

void led_1ms(void)
{
	uint8_t v = power_state == PS_S0 ? 0x00 : 0xFF;

	if (DCR1 != v)
		DCR1 = v;
}
