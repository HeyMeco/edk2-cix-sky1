/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * SCP-firmware on the PM: built with BUILD_HAS_SUB_SYSTEM_MODE, so
 * fwk_arch_init initialises, binds and starts the modules, processes the
 * events that raises and returns; the loop here then serves interrupts
 * and events, then the PM's power sequence when one is asked for
 * (src/hsm, CIX's idle hook), sleeping in WAITI between them.
 */
#include <fwk_arch.h>
#include <fwk_assert.h>
#include <fwk_status.h>

#include <internal/fwk_core.h>

#include "arch_scp.h"
#include "hsm/hsm.h"

static const struct fwk_arch_init_driver init_driver = {
	.interrupt = arch_interrupt_init,
};

void scp_init(void)
{
	int status = fwk_arch_init(&init_driver);

	fwk_assert(status == FWK_SUCCESS);
}

void scp_run(void)
{
	for (;;) {
		arch_irq_run();
		fwk_process_event_queue();
		if (pm_idle())
			arch_irq_wait();
	}
}
