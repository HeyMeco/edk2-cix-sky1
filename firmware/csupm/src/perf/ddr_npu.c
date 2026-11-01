/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The DDR controllers' and the NPU's utilisation for the DPM's power model
 * (CIX's Source/common/soc/ddr/ddr.c and soc/npu/npu.c).
 *
 * Every fine tick (50 us, weighted by the ticks it stands for) two 7-bit
 * utilisation fields of each DDR channel present are accumulated, and the
 * NPU's command pool idle bit while the NPU is powered (its power state 3);
 * every DPM refresh takes and clears the sums, with the NPU's cycle
 * counter and its SMMU's (TCU, two TBUs) increments.  The NPU's registers
 * are shared with the AP: read under hardware mutex 47 (PM ids 48 and 50).
 */
#include "mclp/mclp.h"
#include "perf.h"
#include "soc.h"

/* per channel, the two utilisation registers */
static const uint32_t ddr_stats_reg_addr[MCLP_CHANNELS][2] = {
	{ 0x4c010b4c, 0x4c010b50 }, { 0x4c030b4c, 0x4c030b50 },
	{ 0x4c050b4c, 0x4c050b50 }, { 0x4c070b4c, 0x4c070b50 },
};
static uint32_t ddr_counter[MCLP_CHANNELS][2];
static uint32_t ddr_accum_cnt;

void ddr_util_tick_handler(uint32_t sample_increase)
{
	for (uint32_t ch = 0; ch < MCLP_CHANNELS; ch++) {
		uint32_t a, b;

		if (!(g_chEnMask & BIT(ch)))
			continue;
		a = (soc_read32(ddr_stats_reg_addr[ch][0]) >> 16) & 0x7f;
		b = (soc_read32(ddr_stats_reg_addr[ch][1]) >> 8) & 0x7f;
		ddr_counter[ch][0] += a * sample_increase;
		ddr_counter[ch][1] += b * sample_increase;
	}
	ddr_accum_cnt += sample_increase;
}

void ddr_util_get_reg_accum(uint32_t *regs, uint32_t *cnt)
{
	memcpy(regs, ddr_counter, sizeof(ddr_counter));
	memset(ddr_counter, 0, sizeof(ddr_counter));
	*cnt = ddr_accum_cnt;
	ddr_accum_cnt = 0;
}

#define NPU_PWR_STATE		0x5425020cu	/* [14:12]: 3 powered */
#define NPU_PWR_ON		(3u << 12)
#define NPU_CMD_POOL		0x54260804u	/* bit 6: idle */
#define NPU_CYCLES_LO		0x54260060u
#define NPU_CYCLES_HI		0x54260064u
#define NPU_SMMU_TCU		0x4b1d2000u
#define NPU_SMMU_TBU0		0x4b3e2000u
#define NPU_SMMU_TBU1		0x4b402000u
#define NPU_MUTEX		47
#define NPU_MUTEX_TICK		48
#define NPU_MUTEX_ACCUM		50

static struct {
	uint32_t cycles_lo, cycles_hi;	/* CIX's last_tick */
	uint32_t cmd_pool_idle;
	uint32_t tick_accum_cnt;
	uint32_t last_smmu_tcu, last_smmu_tbu0, last_smmu_tbu1;
} npu_acc;

bool npu_check_power_state(void)
{
	return soc_read32(NPU_PWR_STATE) & 1;
}

void npu_tick_handler(uint32_t sample_increase)
{
	uint32_t v;

	if (!hw_mutex_try_lock(NPU_MUTEX, NPU_MUTEX_TICK))
		return;
	if ((soc_read32(NPU_PWR_STATE) & (7u << 12)) != NPU_PWR_ON) {
		(void)hw_mutex_unlock(NPU_MUTEX, NPU_MUTEX_TICK);
		return;
	}
	v = soc_read32(NPU_CMD_POOL);
	(void)hw_mutex_unlock(NPU_MUTEX, NPU_MUTEX_TICK);
	npu_acc.cmd_pool_idle += ((v >> 6) & 1) * sample_increase;
	npu_acc.tick_accum_cnt++;
}

/* m: SMMU TCU, TBU0, TBU1 and cycle increments, idle samples; *accum_cnt the ticks */
void npu_get_reg_accum(uint32_t *m, uint32_t *accum_cnt)
{
	if (!hw_mutex_try_lock(NPU_MUTEX, NPU_MUTEX_ACCUM)) {
		memset(m, 0, 4 * sizeof(*m));
	} else if ((soc_read32(NPU_PWR_STATE) & (7u << 12)) != NPU_PWR_ON) {
		(void)hw_mutex_unlock(NPU_MUTEX, NPU_MUTEX_ACCUM);
		memset(m, 0, 4 * sizeof(*m));
	} else {
		uint32_t hi, lo, tcu, tbu0, tbu1;
		int n = 6;

		/* the cycle counter's halves, read again while the high one moved
		 * (CIX's: or while both are equal), at most six times */
		for (;;) {
			uint32_t hi2;

			n--;
			hi = soc_read32(NPU_CYCLES_HI);
			lo = soc_read32(NPU_CYCLES_LO);
			if (!n)
				break;
			hi2 = soc_read32(NPU_CYCLES_HI);
			if (hi != lo && hi == hi2)
				break;
		}
		(void)hw_mutex_unlock(NPU_MUTEX, NPU_MUTEX_ACCUM);
		tcu = soc_read32(NPU_SMMU_TCU);
		tbu0 = soc_read32(NPU_SMMU_TBU0);
		tbu1 = soc_read32(NPU_SMMU_TBU1);
		m[3] = lo - npu_acc.cycles_lo;
		npu_acc.cycles_lo = lo;
		npu_acc.cycles_hi = hi;
		m[0] = tcu - npu_acc.last_smmu_tcu;
		npu_acc.last_smmu_tcu = tcu;
		m[1] = tbu0 - npu_acc.last_smmu_tbu0;
		npu_acc.last_smmu_tbu0 = tbu0;
		m[2] = tbu1 - npu_acc.last_smmu_tbu1;
		npu_acc.last_smmu_tbu1 = tbu1;
	}
	m[4] = npu_acc.cmd_pool_idle;
	npu_acc.cmd_pool_idle = 0;
	*accum_cnt = npu_acc.tick_accum_cnt;
	npu_acc.tick_accum_cnt = 0;
}

/* The SMMU's performance counters on (TCU, TBU0, TBU1: four registers each) */
void npu_cntr_setup(void)
{
	static const uint32_t init_regs[12] = {
		0x4b1b2400, 0x4b1b2a00, 0x4b1b2c00, 0x4b1b2e04, 0x4b3d2400, 0x4b3d2a00,
		0x4b3d2c00, 0x4b3d2e04, 0x4b3f2400, 0x4b3f2a00, 0x4b3f2c00, 0x4b3f2e04,
	};
	static const uint8_t init_vals[12] = { 1, 0x1e, 1, 1, 1, 0x1e, 1, 1, 1, 0x1e, 1, 1 };

	for (uint32_t i = 0; i < 12; i++)
		soc_write32(init_regs[i], init_vals[i]);
}
