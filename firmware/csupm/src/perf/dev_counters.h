/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The counter blocks (CIX's Source/common/drivers/dev_counters.c): three
 * instances (the PM's two at 0xa0040000 and 0xa0048000, the SoC's at
 * 0x44110000) of eight nodes (4 KiB each) that count, time, make PWM or
 * measure a tachometer's pulses (a pair of nodes, 4-5 or 6-7), and the
 * AP's free-running counter.  CIX's names and prototypes.
 */
#ifndef DEV_COUNTERS_H
#define DEV_COUNTERS_H

#include "pm.h"

/* CIX's DEV_CNTS_ID: node (bits 0-2) of instance (bits 3-9) */
typedef union {
	uint32_t cntId;
	struct {
		uint32_t nodeIdx : 3;
		uint32_t instIdx : 7;
		uint32_t reserved : 22;
	};
} DEV_CNTS_ID;

/* a node's use (dev_cnts_alloc_node): 0 timer, 1 64-bit timer (nodes 0-1),
 * 2 free-running (a pair), 3 PWM, 4 tachometer (a pair from node 4), 0x8000
 * a pair reserved; 7 free */
#define CNTS_MODE_NONE		7

void dev_cnts_init(void);
void dev_cnts_set_instance(uint32_t mmioAddr, uint32_t instIdx, uint32_t refClkFreq);
bool dev_cnts_alloc_node(DEV_CNTS_ID id, uint32_t mode);
bool dev_cnts_free_node(DEV_CNTS_ID id);
bool dev_cnts_pwm_setup(DEV_CNTS_ID id, uint32_t freqHz, uint32_t duty256);
bool dev_cnts_pwm_start(DEV_CNTS_ID id);
bool dev_cnts_pwm_update_freq(DEV_CNTS_ID id, uint32_t freqHz);
bool dev_cnts_pwm_update_duty(DEV_CNTS_ID id, uint32_t duty256);
/* the callback runs from the interrupt every monitorIntervalMs with para */
bool dev_cnts_tach_setup(DEV_CNTS_ID id, uint32_t monitorIntervalMs, uint32_t squarePerCycle,
			 void (*callback)(uint32_t), uint32_t para);
bool dev_cnts_tach_start(DEV_CNTS_ID id);
bool dev_cnts_tach_read_raw(DEV_CNTS_ID id, uint32_t *pOutput);
bool dev_cnts_tach_read_rpm(DEV_CNTS_ID id, uint32_t *pOutput);
uint64_t dev_cnts_get_ap_free_counter(void);
/* the interrupts of instances 0 and 1 (aggregator lines 22 and 23) */
void dev_cnts_intr0(void *pdata);
void dev_cnts_intr1(void *pdata);

#endif
