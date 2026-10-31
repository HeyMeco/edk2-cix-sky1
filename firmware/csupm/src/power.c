/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * System power requests from SCMI (shutdown, reset).  CIX's firmware
 * records the target (stdby_target: 5 shutdown, 0 reset) for its system
 * power state machine (pwr_hsm), which runs the sequence with the SE.
 * That sequence is not here yet: the request is kept and reported on the
 * debug UART.
 */
#include <mod_rcsu.h>

#include "uart.h"

volatile int power_request = -1;

void rcsu_system_shutdown_request(enum mod_pd_system_shutdown type)
{
	power_request = (int)type;
	uart_puts(type == MOD_PD_SYSTEM_SHUTDOWN || type == MOD_PD_SYSTEM_FORCED_SHUTDOWN ?
		  "CSU_PM: shutdown requested (not implemented)\n" :
		  "CSU_PM: reset requested (not implemented)\n");
}
