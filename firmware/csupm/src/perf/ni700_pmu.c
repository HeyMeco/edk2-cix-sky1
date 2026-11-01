/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The NI-700 hubs' PMUs (CIX's Source/common/soc/ni700/ni700_pmu.c and the
 * hub table of ni700_topology.c).
 *
 * A hub has its configuration at smn_base (a component per port at
 * +0x4000 + 4 KiB * port, whose +0xc and +0x10 bytes select the event each
 * of the PMU's 8 counters counts there) and its PMU at pmu_base: +0x400
 * counter's node, +0x610 snapshot status, +0x614 overflows, +0x618 cycle
 * counter, +0x620 counters, +0x6f0 snapshot, +0xc00/0xc20 counter enables
 * set/clear, +0xc60 interrupt enables clear, +0xc80 overflows clear,
 * +0xe04 control (bit 0 on, 1-2 reset).  A counter counts one event of one
 * port; streams asking for the same share it (a reference count).  Each
 * coarse tick snapshots the counters, the streams' work reads them into
 * one of two copies (ping-pong) and takes the increments.  The MM hub is
 * there when the package's plat_id_4 bit 6 says so, the PCIe hub with bit 4.
 */
#include "perf.h"
#include "plat.h"
#include "soc.h"

#define NI_COUNTERS		8
#define NI_FREE			0xff

#define NI_COMP(ni, port)	((ni)->smn_base + 0x4000 + ((port) << 12))
#define NI_COMP_SEL(cnt)	((cnt) >> 2 ? 0x10 : 0x0c)
#define NI_PMEVTYPER(cnt)	(0x400 + 4 * (cnt))
#define NI_PMSSSR		0x610
#define NI_PMOVSSSR		0x614
#define NI_PMCCNTRSR		0x618
#define NI_PMEVCNTRSR(cnt)	(0x620 + 4 * (cnt))
#define NI_PMSSRR		0x6f0
#define NI_PMCNTENSET		0xc00
#define NI_PMCNTENCLR		0xc20
#define NI_PMINTENCLR		0xc60
#define NI_PMOVSCLR		0xc80
#define NI_PMCR			0xe04
#define NI_ALL_COUNTERS		0x800000ffu	/* cycle counter and counters 0-7 */

/* CIX's NI700_INSTANCE */
struct ni700_instance {
	const char *name;
	uint32_t smn_base;
	uint32_t smn_clk0;		/* the PMU */
	uint32_t clk_tree_idx;
	uint32_t data_beat_size;
	uint8_t pingpang_idx;
	uint64_t cycle_cnt[2];
	uint32_t cnt_reading[2][NI_COUNTERS];
	uint32_t overflow_flags[2];
	uint8_t cnt_ref[NI_COUNTERS];
	uint8_t node_sel[NI_COUNTERS];
	uint8_t event_id[NI_COUNTERS];
	uint32_t port_num;
};

#define HUB(nm, base, pmu, clk, beat, ports) { \
	.name = (nm), .smn_base = (base), .smn_clk0 = (pmu), .clk_tree_idx = (clk), \
	.data_beat_size = (beat), .event_id = { NI_FREE, NI_FREE, NI_FREE, NI_FREE, \
	NI_FREE, NI_FREE, NI_FREE, NI_FREE }, .port_num = (ports) }

__attribute__((section(".dram1.data"))) static NI700_INSTANCE ni700_mmhub = HUB("mmhub", 0x0d040000, 0x0d063000, 0x0c2, 0x100, 31);
__attribute__((section(".dram1.data"))) static NI700_INSTANCE ni700_pcie = HUB("pcie", 0x0d010000, 0x0d021000, 0x11e, 0x100, 13);
__attribute__((section(".dram1.data"))) static NI700_INSTANCE ni700_syshub = HUB("syshub", 0x0d090000, 0x0d0ad000, 0x0c4, 0x80, 25);
__attribute__((section(".dram1.data"))) static NI700_INSTANCE ni700_smnhub = HUB("smnhub", 0x0d0d0000, 0x0d113000, 0x01b, 0x80, 63);

NI700_INSTANCE *const _s_ni700_hubs[4] = { &ni700_mmhub, &ni700_pcie, &ni700_syshub, &ni700_smnhub };

/* ports are numbered from 32 when set (CIX's debug CLI sets it) */
static uint8_t _s_smn_port_base;

#define NI_ASSERT(c, code)	do { if (!(c)) __builtin_trap(); } while (0)

uint32_t ni700_clk_tree_idx(const NI700_INSTANCE *ni_inst)
{
	return ni_inst->clk_tree_idx;
}

void ni700_set_port_index_base(uint8_t base)
{
	_s_smn_port_base = base;
}

static uint32_t port_base(void)
{
	return _s_smn_port_base ? 32 : 0;
}

/* Is the hub in this package? */
static bool _chk_instence(const NI700_INSTANCE *ni_inst)
{
	if (ni_inst == &ni700_mmhub && !(plat_id_4 & BIT(6)))
		return false;
	return ni_inst != &ni700_pcie || (plat_id_4 & BIT(4));
}

static uint32_t sel_byte(uint32_t v, uint32_t cnt, uint32_t event)
{
	uint32_t sh = 8 * (cnt & 3);

	return (v & ~(0xffu << sh)) | ((((v >> sh) & 0xc0) | event) & 0xff) << sh;
}

/* Is a counter counting event at port?  (*cnt_idx: which) */
static bool ni700_is_pmu_event_enabled(NI700_INSTANCE *ni_inst, uint32_t port, uint8_t event_id,
				       uint32_t *cnt_idx)
{
	if (!_chk_instence(ni_inst))
		return false;
	NI_ASSERT(ni_inst && port < ni_inst->port_num, 0xf1680314);
	if (!event_id)
		return false;
	for (uint32_t i = 0; i < NI_COUNTERS; i++) {
		if (ni_inst->node_sel[i] == port && ni_inst->event_id[i] == event_id) {
			if (cnt_idx)
				*cnt_idx = i;
			return true;
		}
	}
	return false;
}

/* Counter cnt to count event_id at port (a counter that already does: one
 * more user); the PMU on */
static bool ni700_set_pmu_event(NI700_INSTANCE *ni_inst, uint32_t port, uint32_t cnt, uint8_t event_id)
{
	uint32_t pmu, sel, v;

	if (!_chk_instence(ni_inst))
		return false;
	NI_ASSERT(ni_inst && cnt < NI_COUNTERS && port < ni_inst->port_num, 0xf1680223);
	if (ni_inst->node_sel[cnt] == port && ni_inst->event_id[cnt] == event_id) {
		if (ni_inst->cnt_ref[cnt] == 0xff)
			return false;
		ni_inst->cnt_ref[cnt]++;
		return true;
	}
	if (ni_inst->event_id[cnt] != NI_FREE)
		return false;
	pmu = ni_inst->smn_clk0;
	sel = NI_COMP(ni_inst, port) + NI_COMP_SEL(cnt);
	soc_write32(sel, sel_byte(soc_read32(sel), cnt, event_id));
	v = soc_read32(NI_COMP(ni_inst, port));		/* the component's type and id */
	soc_write32(pmu + NI_PMEVTYPER(cnt), ((v & 0xf) << 9) + ((v >> 16) & 0x1ff) + BIT(31));
	soc_write32(pmu + NI_PMCNTENSET, BIT(cnt) | BIT(31));
	v = soc_read32(pmu + NI_PMCR);
	if (!(v & 1))
		soc_write32(pmu + NI_PMCR, (v >> 3) * 8 + 1);
	ni_inst->node_sel[cnt] = (uint8_t)port;
	ni_inst->event_id[cnt] = event_id;
	ni_inst->cnt_ref[cnt] = 0;
	return true;
}

/* One user less of counter cnt; the last gives it back */
static bool ni700_clr_pmu_event(NI700_INSTANCE *ni_inst, uint32_t cnt)
{
	uint32_t pmu, sel, v;

	if (!_chk_instence(ni_inst))
		return false;
	NI_ASSERT(ni_inst && cnt < NI_COUNTERS, 0xf1680333);
	if (ni_inst->cnt_ref[cnt]) {
		ni_inst->cnt_ref[cnt]--;
		return true;
	}
	if (ni_inst->event_id[cnt] == NI_FREE)
		return false;
	pmu = ni_inst->smn_clk0;
	sel = NI_COMP(ni_inst, ni_inst->node_sel[cnt]) + NI_COMP_SEL(cnt);
	v = soc_read32(sel);
	soc_write32(sel, sel_byte(v, cnt, 0));
	soc_write32(pmu + NI_PMEVTYPER(cnt), 0);
	soc_write32(pmu + NI_PMCNTENCLR, BIT(cnt));
	soc_write32(pmu + NI_PMINTENCLR, BIT(cnt));
	soc_write32(pmu + NI_PMOVSCLR, BIT(cnt));
	ni_inst->cnt_ref[cnt] = 0;
	ni_inst->node_sel[cnt] = 0;
	ni_inst->event_id[cnt] = NI_FREE;
	for (uint32_t i = 0; i < NI_COUNTERS; i++) {
		if (ni_inst->event_id[i] != NI_FREE)
			return true;
	}
	/* CIX's oddity kept: with no counter left it writes the control's
	 * enable and resets only when the PMU is off */
	v = soc_read32(pmu + NI_PMCR);
	if (!(v & 1))
		soc_write32(pmu + NI_PMCR, v | 7);
	return true;
}

/*
 * event_id at each port of port_mask, on a counter that counts it already
 * or a free one; *cnt_bitmap the counters.  If one cannot be had, those
 * taken are given back.
 */
bool ni700_setup_pmu_events(NI700_INSTANCE *ni_inst, uint32_t port_mask, uint8_t event_id,
			    uint32_t *cnt_bitmap)
{
	uint32_t taken = 0, next_port = 0, free_cnt = 0, cnt = 0, p;

	if (!_chk_instence(ni_inst))
		return false;
	while (!((p = u_misc_u32lowestOneBit(port_mask, next_port)) & 0x80)) {
		uint32_t port = p + port_base();

		next_port = p + 1;
		if (ni700_is_pmu_event_enabled(ni_inst, port, event_id, &cnt)) {
			if (!ni700_set_pmu_event(ni_inst, p + port_base(), cnt, event_id))
				goto fail;
			taken |= BIT(cnt);
			continue;
		}
		while (free_cnt < NI_COUNTERS && ni_inst->event_id[free_cnt] != NI_FREE)
			free_cnt++;
		if (free_cnt == NI_COUNTERS ||
		    !ni700_set_pmu_event(ni_inst, p + port_base(), free_cnt, event_id))
			goto fail;
		taken |= BIT(free_cnt);
	}
	if (cnt_bitmap)
		*cnt_bitmap = taken;
	return true;
fail:
	/* CIX's oddity kept: with a port base the counter number gets it too */
	for (uint32_t q = 0; !((p = u_misc_u32lowestOneBit(taken, q)) & 0x80); q = p + 1)
		(void)ni700_clr_pmu_event(ni_inst, p + port_base());
	return false;
}

bool ni700_free_pmu_events(NI700_INSTANCE *ni_inst, uint32_t cnt_bitmap)
{
	uint32_t p;

	if (!_chk_instence(ni_inst))
		return false;
	for (uint32_t q = 0; !((p = u_misc_u32lowestOneBit(cnt_bitmap & 0xff, q)) & 0x80); q = p + 1)
		(void)ni700_clr_pmu_event(ni_inst, p + port_base());
	return true;
}

/* Every counter of the hub stopped, cleared and given back (CIX's overlay) */
void ni700_free_all_pmu_counters(NI700_INSTANCE *ni_inst)
{
	uint32_t pmu = ni_inst->smn_clk0;

	if (!_chk_instence(ni_inst))
		return;
	for (uint32_t k = 0; k < ni_inst->port_num; k++) {
		soc_write32(NI_COMP(ni_inst, k) + 0xc, 0);
		soc_write32(NI_COMP(ni_inst, k) + 0x10, 0);
	}
	for (uint32_t i = 0; i < NI_COUNTERS; i++)
		soc_write32(pmu + NI_PMEVTYPER(i), 0);
	soc_write32(pmu + NI_PMCNTENCLR, NI_ALL_COUNTERS);
	soc_write32(pmu + NI_PMINTENCLR, NI_ALL_COUNTERS);
	soc_write32(pmu + NI_PMOVSCLR, NI_ALL_COUNTERS);
	soc_write32(pmu + NI_PMCR, 6);
	ni_inst->pingpang_idx = 0;
	for (uint32_t i = 0; i < NI_COUNTERS; i++) {
		ni_inst->cnt_reading[1][i] = 0;
		ni_inst->cnt_reading[0][i] = 0;
		ni_inst->cnt_ref[i] = 0;
		ni_inst->node_sel[i] = 0;
		ni_inst->event_id[i] = NI_FREE;
	}
	ni_inst->overflow_flags[1] = 0;
	ni_inst->overflow_flags[0] = 0;
	ni_inst->cycle_cnt[0] = 0;
	ni_inst->cycle_cnt[1] = 0;
}

/* The coarse tick: the counters copied to their snapshot registers */
void ni700_take_pmu_cnt_snapshot(NI700_INSTANCE *ni_inst)
{
	if (!_chk_instence(ni_inst))
		return;
	NI_ASSERT(ni_inst, 0xf1680051);
	soc_write32(ni_inst->smn_clk0 + NI_PMSSRR, 1);
	for (int i = 0; i < 4; i++) {
		if (soc_read32(ni_inst->smn_clk0 + NI_PMSSSR) & 1)
			break;
	}
}

/* The snapshot into this tick's copy */
void ni700_fetch_pmu_cnt_snapshot(NI700_INSTANCE *ni_inst)
{
	uint8_t pp;

	if (!_chk_instence(ni_inst))
		return;
	NI_ASSERT(ni_inst, 0xf1680076);
	pp = ni_inst->pingpang_idx;
	for (uint32_t i = 0; i < NI_COUNTERS; i++)
		ni_inst->cnt_reading[pp][i] = soc_read32(ni_inst->smn_clk0 + NI_PMEVCNTRSR(i));
	ni_inst->overflow_flags[pp] = soc_read32(ni_inst->smn_clk0 + NI_PMOVSSSR);
	ni_inst->cycle_cnt[pp] = soc_read64(ni_inst->smn_clk0 + NI_PMCCNTRSR);
	ni_inst->pingpang_idx = !pp;
}

static uint64_t ni700_pmu_event_summation(const NI700_INSTANCE *ni_inst, uint32_t cnt_bitmap, uint8_t pp)
{
	uint64_t s = 0;
	uint32_t c;

	for (uint32_t q = 0; (c = u_misc_u32lowestOneBit(cnt_bitmap, q)) < NI_COUNTERS; q = c + 1)
		s += ni_inst->cnt_reading[pp][c];
	return s;
}

/* The counters' increment between the last two snapshots */
uint32_t ni700_calc_event_increment(NI700_INSTANCE *ni_inst, uint32_t cnt_bitmap)
{
	uint8_t pp = ni_inst->pingpang_idx;

	return (uint32_t)(ni700_pmu_event_summation(ni_inst, cnt_bitmap, !pp) -
			  ni700_pmu_event_summation(ni_inst, cnt_bitmap, pp));
}
