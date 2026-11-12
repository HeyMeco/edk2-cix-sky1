/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The power LED (BLINK_L on PWM1, active low): on in S0 with SYS_S0_PG,
 * "breathing" in suspend to RAM, off otherwise.  As the vendor: in STR
 * the duty moves by one percent every 40 ms, 0 to 100 and back
 * (DCR1 = percent * 255 / 100).
 */
#include "ec.h"

static uint8_t pct, down, ms40, was_str;

void led_1ms(void)
{
	uint8_t v;

	if (power_state == PS_STR) {
		was_str = 1;
		if (++ms40 < 40)
			return;
		ms40 = 0;
		if (down) {
			if (--pct == 0)
				down = 0;
		} else if (++pct == 100) {
			down = 1;
		}
		DCR1 = (uint8_t)(((uint16_t)pct * 255) / 100);
		return;
	}
	/* resumed: as the vendor, the LED follows SYS_S0_PG's edge */
	if (was_str && power_state == PS_S0 && !SYS_S0_PG())
		return;
	was_str = 0;
	pct = 0;
	down = 0;
	ms40 = 0;
	v = power_state == PS_S0 && SYS_S0_PG() ? 0x00 : 0xFF;
	if (DCR1 != v)
		DCR1 = v;
}
