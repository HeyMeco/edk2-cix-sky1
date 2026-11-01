/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The DPM's runtime on SCP-firmware: CIX's performance-stream tick (its
 * md_timer, every 1 ms, and the md_event its timer callback signals) and
 * the DPM task (dpm_proc) it wakes, as the timer module's alarm and this
 * module's event.  The streams and the DPM are src/perf and src/dpm; this
 * module only schedules them (perf.h's perf_tick_* functions).  It also
 * routes the counter blocks' interrupts (aggregator lines 22 and 23).
 */
#ifndef MOD_DPM_H
#define MOD_DPM_H

#include <fwk_id.h>

#include <stdint.h>

struct mod_dpm_config {
	fwk_id_t alarm_id;		/* the coarse tick's alarm */
	uint32_t tick_ms;		/* CIX's perf_stream tick: 1000 us */
	unsigned int work_irq;		/* a software line for the tick's work */
	unsigned int cnts_irq[2];	/* the counter blocks 0 and 1 */
};

#endif
