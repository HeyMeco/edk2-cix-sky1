/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The AP's cores as the performance streams and the PCC tables see them
 * (CIX's Source/common/soc/ap/ap_event.c): whether a core's clock runs,
 * its activity monitor counters (AMU; the DSU's too), the MPMM throttling
 * gears and the dispatch blocks in the PM's DSU fan-out registers, the
 * cores halted by gating their clocks.
 *
 * Per core (and the DSU) the AMU instance keeps two snapshots (ping-pong)
 * and the increments between them: a core's counters are read through
 * the SoC window while its hardware mutex (80 + core) is the PM's, from the
 * second snapshot on the increments add up, from the third the AP's
 * stream and the DPM take them.  CIX's PDP gear functions are not in its
 * image (unused).
 */
#include "clk/clk.h"
#include "dpm/dpm.h"
#include "perf.h"
#include "soc.h"

#define AP_AMU			0x4f090000u	/* core i at + 1 MB * i */
#define DSU_AMU			0x4f040000u
#define DSU_AMU_CTL		0x4f040c00u	/* the DSU's counters on (0x1f) */
#define AP_AMU_COUNTERS		7
#define DSU_AMU_COUNTERS	5

/* the PM's DSU fan-out: MPMM enables, gears (4 bits per core), dispatch blocks */
#define DSU_FANOUT_MPMMEN	(PM_LOCAL + 0x00)
#define DSU_FANOUT_GEAR_LO	(PM_LOCAL + 0x04)	/* cores 0-7 */
#define DSU_FANOUT_GEAR_HI	(PM_LOCAL + 0x08)	/* cores 8-11 */
#define DSU_FANOUT_DISPATCH	(PM_LOCAL + 0x10)	/* bits 4-11 */
#define DISPATCH_MASK		0xff0u

#define AMU_MUTEX(core)		(80 + (core))
#define AMU_MUTEX_OWNER		80

static const uint16_t ap_amu_reg_offsets[AP_AMU_COUNTERS] = { 0, 8, 0x10, 0x18, 0x100, 0x108, 0x110 };
static const uint16_t dsu_amu_reg_offsets[DSU_AMU_COUNTERS] = { 0, 8, 0x10, 0x18, 0x20 };

/* CIX's AP_AMU_INSTANCE; state 0 off, 1 set up, 2 one snapshot, 3 increments */
typedef struct {
	uint64_t cnt_reading[2][AP_AMU_COUNTERS];
	uint64_t increment[AP_AMU_COUNTERS];
	uint8_t pingpong_idx;
	uint8_t state;
} AP_AMU_INSTANCE;

static AP_AMU_INSTANCE ap_amu_evt[AP_AMU_CORES];
static int16_t _s_ap_clk_state_bitmap;	/* bit 15: halted; bit i: core i was running */

bool ap_is_clk_on(uint32_t ele)
{
	CLK_TREE_STRUCT *p = clk_nodes_get(dpm_elem_get_clk_id(ele));

	return clk_tree_get_freq(&p->clk_tree_node) > 0;
}

/*
 * A core's counter, never going backwards: CIX's keeps the last value read
 * and gives it again when a read is below it, unless a second read is at
 * least as high and the first was not small (a counter that restarted:
 * above 0x7ffff for the architected counters 0-3, above 255 for the
 * auxiliary ones), when it takes the second.  The DSU's counters are read
 * as they are.
 */
uint64_t amu_read(uint32_t core_idx, uint32_t cnt)
{
	static uint64_t amu_cache[12][AP_AMU_COUNTERS];
	uint64_t *cache, v, v2, small;
	uint32_t addr;

	if (core_idx == DPM_ELE_DSU)
		return soc_read64(DSU_AMU + dsu_amu_reg_offsets[cnt]);
	addr = AP_AMU + (core_idx << 20) + ap_amu_reg_offsets[cnt];
	cache = &amu_cache[core_idx][cnt];
	v = soc_read64(addr);
	if (v > *cache) {
		*cache = v;
	} else if (v != 0) {
		v2 = soc_read64(addr);
		small = cnt >= 4 ? 255 : 0x7ffff;
		if (v2 >= v && v > small)
			*cache = v2;
	}
	return *cache;
}

/* The cores (bits; 12 is the DSU) to sample from the next fetch on */
void ap_setup_amu_cntr(uint32_t core_mask)
{
	uint32_t idx = 0;

	while (core_mask) {
		uint32_t b = u_misc_u32lowestOneBit(core_mask, 0);

		idx += b;
		if (idx >= AP_AMU_CORES)
			__builtin_trap();	/* CIX: vAssertCalledWithTRC(0xf1600083) */
		if (!ap_amu_evt[idx].state) {
			ap_amu_evt[idx].pingpong_idx = 0;
			ap_amu_evt[idx].state = 1;
			if (idx == DPM_ELE_DSU)
				soc_write32(DSU_AMU_CTL, 0x1f);
		}
		core_mask >>= b + 1;
		idx++;
	}
}

void ap_clear_amu_cntr(void)
{
	for (uint32_t i = 0; i < AP_AMU_CORES; i++) {
		ap_amu_evt[i].state = 0;
		if (i == DPM_ELE_DSU)
			soc_write32(DSU_AMU_CTL, 0);
	}
}

/* Each coarse tick: a snapshot of every core sampled, its increments */
void ap_fetch_amu_cntr_snapshot(void)
{
	for (uint32_t i = 0; i < AP_AMU_CORES; i++) {
		AP_AMU_INSTANCE *inst = &ap_amu_evt[i];
		uint32_t n = i == DPM_ELE_DSU ? DSU_AMU_COUNTERS : AP_AMU_COUNTERS;
		uint8_t pp, next;
		bool locked;

		if (!inst->state)
			continue;
		if (!ap_is_clk_on(i)) {
			inst->state = 1;
			continue;
		}
		pp = inst->pingpong_idx;
		next = !pp;
		locked = hw_mutex_try_lock(AMU_MUTEX(i), AMU_MUTEX_OWNER);
		for (uint32_t k = 0; k < n; k++) {
			uint64_t v = locked ? amu_read(i, k) : inst->cnt_reading[next][k];

			inst->cnt_reading[pp][k] = v;
			if ((inst->state & 0xfe) == 2)
				inst->increment[k] += v - inst->cnt_reading[next][k];
		}
		if (locked)
			(void)hw_mutex_unlock(AMU_MUTEX(i), AMU_MUTEX_OWNER);
		inst->pingpong_idx = next;
		if (inst->state == 2)
			inst->state = 3;
		else if (inst->state == 1)
			inst->state = 2;
	}
}

bool ap_amu_contain_data(uint8_t core_idx)
{
	if (core_idx >= AP_AMU_CORES)
		__builtin_trap();	/* CIX: 0xf1600209 */
	return ap_amu_evt[core_idx].state == 3;
}

uint32_t ap_get_and_clr_amu_event_incre(uint8_t core_idx, uint8_t cnt)
{
	uint32_t v;

	if (core_idx >= AP_AMU_CORES || cnt >= AP_AMU_COUNTERS)
		__builtin_trap();	/* CIX: 0xf1600220, 0xf1600223 */
	v = (uint32_t)ap_amu_evt[core_idx].increment[cnt];
	ap_amu_evt[core_idx].increment[cnt] = 0;
	return v;
}

/*
 * The cores whose clock runs halted by gating it (once; bit 15 says so),
 * CIX's assertion that each core's clock node is a gate kept
 */
bool ap_halt_by_clk_gate(void)
{
	if (_s_ap_clk_state_bitmap < 0)
		return false;
	_s_ap_clk_state_bitmap = (int16_t)0x8000;
	for (uint32_t i = 0; i < 12; i++) {
		CLK_TREE_STRUCT *p;

		if (!ap_is_clk_on(i))
			continue;
		_s_ap_clk_state_bitmap |= (int16_t)BIT(i);
		p = clk_nodes_get(dpm_elem_get_clk_id(i));
		if ((p->mass_ctrl0 & (0xfu << 19)) != (3u << 20))
			__builtin_trap();	/* CIX: 0xf1600243 */
		gate_ctrl_set_state(p->gate.reg, p->gate.mask, false);
	}
	clk_tree_state_refresh(true);
	return true;
}

void ap_resume_by_clk_gate(void)
{
	if (_s_ap_clk_state_bitmap >= 0)
		return;
	for (uint32_t i = 0; i < 12; i++) {
		CLK_TREE_STRUCT *p;

		if (!(_s_ap_clk_state_bitmap & BIT(i)))
			continue;
		p = clk_nodes_get(dpm_elem_get_clk_id(i));
		if ((p->mass_ctrl0 & (0xfu << 19)) != (3u << 20))
			__builtin_trap();	/* CIX: 0xf1600261 */
		gate_ctrl_set_state(p->gate.reg, p->gate.mask, true);
	}
	clk_tree_state_refresh(true);
	_s_ap_clk_state_bitmap = 0;
}

/* MPMM: the cores' (bits 0-11) gears set and their throttling on, or off */
static void _set_mpmm_gear(uint32_t mask, uint32_t gear, bool enable)
{
	uint32_t lo = REG32(DSU_FANOUT_GEAR_LO), hi = REG32(DSU_FANOUT_GEAR_HI), en;

	for (uint32_t i = 0; i < 8; i++) {
		if (mask & BIT(i)) {
			lo ^= lo & (0xfu << 4 * i);
			if (enable)
				lo |= gear << 4 * i;
		}
	}
	for (uint32_t i = 0; i < 4; i++) {
		if ((mask >> i) & BIT(8)) {
			hi ^= hi & (0xfu << 4 * i);
			if (enable)
				hi |= gear << 4 * i;
		}
	}
	REG32(DSU_FANOUT_GEAR_LO) = lo;
	REG32(DSU_FANOUT_GEAR_HI) = hi;
	en = REG32(DSU_FANOUT_MPMMEN);
	REG32(DSU_FANOUT_MPMMEN) = enable ? en | (mask & 0xfff) : en ^ (en & mask & 0xfff);
}

void ap_set_mpmm_gear(uint32_t mask, uint32_t gear)
{
	_set_mpmm_gear(mask, gear, true);
}

void ap_clr_mpmm_gear(uint32_t mask)
{
	_set_mpmm_gear(mask, 0, false);
}

void ap_set_dispatch_block(uint32_t mask)
{
	REG32(DSU_FANOUT_DISPATCH) |= mask & DISPATCH_MASK;
}

void ap_clr_dispatch_block(uint32_t mask)
{
	REG32(DSU_FANOUT_DISPATCH) &= ~mask | ~DISPATCH_MASK;
}
