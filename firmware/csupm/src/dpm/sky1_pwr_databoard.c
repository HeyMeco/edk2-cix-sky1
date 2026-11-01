/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The power data board shared with the AP (CIX's
 * Source/projects/sky1/dpm/sky1_pwr_databoard.c): 0x380 bytes of DRAM at
 * 0x83bf0300, seen through the hub window.  Per core and the DSU (64 bytes
 * each) the AP writes the cycles its core spent off (+0) and the PM a
 * filtered dynamic power (+56) and leakage (+60) when they change by more
 * than 4%; the AP writes the GPU's power (+0x340), the PM the GPU's leakage
 * (+0x344).  Nothing is exchanged while the SoC idles (perf streams).
 */
#include "dpm_rt.h"
#include "perf/perf.h"
#include "rtt/rtt.h"

#define DATABOARD_DRAM		0x83bf0300ull
#define DATABOARD_SIZE		0x380u
#define DB_CORE(i)		(0x40u * (i))
#define DB_OFF_CYCLE		0x00u
#define DB_POWER		0x38u
#define DB_LEAKAGE		0x3cu
#define DB_GPU_POWER		0x340u
#define DB_GPU_LEAKAGE		0x344u

static volatile uint8_t *databoard;
static int32_t last_power[DPM_DxS_MAX], last_leak[DPM_DxS_MAX];
static int32_t avg_power[DPM_DxS_MAX], avg_leak[DPM_DxS_MAX];

#define DB32(off)		(*(volatile int32_t *)(databoard + (off)))

void sky1_pwr_databoard_init(void)
{
	databoard = (volatile uint8_t *)tlb_ctrl_dram_to_syshub_addr(DATABOARD_DRAM, false);
	if (!databoard)
		__builtin_trap();	/* CIX: vAssertCalledWithTRC(0xf1b20040) */
	memset((void *)databoard, 0, DATABOARD_SIZE);
}

/* more than 4% away from the value last uploaded (of 1 if that was 0) */
static bool sky1_pwr_should_upload(int32_t last, int32_t now)
{
	int32_t d = now < last ? last - now : now - last;

	return 4 < d * 100 / (last ? last : 1);
}

/* A core's or the DSU's power and leakage (mW), filtered (9/10 old) */
void sky1_pwr_databoard_upload_ap_dsu_power(uint32_t id, int32_t power, int32_t leakage)
{
	if (!databoard || id > 12)
		return;
	avg_power[id] = (avg_power[id] * 9 + power) / 10;
	if (sky1_pwr_should_upload(last_power[id], avg_power[id])) {
		if (!perf_soc_is_in_idle_state())
			DB32(DB_CORE(id) + DB_POWER) = avg_power[id];
		last_power[id] = avg_power[id];
	}
	avg_leak[id] = (avg_leak[id] * 9 + leakage) / 10;
	if (sky1_pwr_should_upload(last_leak[id], avg_leak[id])) {
		if (!perf_soc_is_in_idle_state())
			DB32(DB_CORE(id) + DB_LEAKAGE) = avg_leak[id];
		last_leak[id] = avg_leak[id];
	}
}

/* The GPU's power from the AP; 0 while the SoC idles (CIX's oddity kept:
 * without a data board *power is left as it is) */
void sky1_pwr_databoard_read_gpu_power(int32_t *power)
{
	int32_t v = 0;

	if (!perf_soc_is_in_idle_state()) {
		if (!databoard)
			return;
		v = DB32(DB_GPU_POWER);
	}
	*power = v;
}

void sky1_pwr_databoard_upload_gpu_leakage_power(int32_t power)
{
	if (databoard && !perf_soc_is_in_idle_state())
		DB32(DB_GPU_LEAKAGE) = power;
}

uint32_t sky1_pwr_databoard_read_ap_off_cycle(uint32_t id)
{
	bool idle = perf_soc_is_in_idle_state();

	if (id >= 12 || !databoard || idle)
		return 0;
	return (uint32_t)DB32(DB_CORE(id) + DB_OFF_CYCLE);
}
