/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * System power requests from the power domain module (CIX's
 * rcsu_pd_shutdown): cold and warm resets and shutdowns (also forced) set
 * the target of the PM's power sequence (src/hsm), which runs from the
 * idle loop.
 */
#include <mod_rcsu.h>

#include "hsm/hsm.h"

void rcsu_system_shutdown_request(enum mod_pd_system_shutdown type)
{
	if (type == MOD_PD_SYSTEM_COLD_RESET || type == MOD_PD_SYSTEM_WARM_RESET)
		trigger_RST();
	else
		trigger_SD();
}
