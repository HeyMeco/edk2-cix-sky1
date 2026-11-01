/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The CI-700 mesh's PMU (CIX's Source/common/soc/ci700/ci700_pmu.c,
 * ci700_lib.c, ci700_topology.c) and its trace into the DFD block
 * (ci700_dfd.c).
 *
 * The crosspoints (XP) and the devices on their ports are CIX's
 * ci700_topology; the counters are the DTC's global ones and the XPs'
 * local ones, counting a device's event or a watchpoint's matches.
 */
#include "perf.h"
#include "rtt/rtt.h"
#include "soc.h"

/* --- CI-700 ---------------------------------------------------------- */

/* A crosspoint and the devices on its ports (type 0: none) */
struct ci700_xp {
	uint32_t base;
	struct {
		uint8_t type;
		uint32_t base;
	} dev[4];
};

static const struct ci700_xp ci700_xps[8] = {
	{ 0x10030000, { { 0x10, 0x100b0000 }, { 0x02, 0x10390000 }, { 0x40, 0x10120000 }, { 0x04, 0x101a0000 } } },
	{ 0x10430000, { { 0x10, 0x104b0000 }, { 0x02, 0x10790000 }, { 0x40, 0x10520000 }, { 0, 0 } } },
	{ 0x11030000, { { 0x10, 0x110b0000 }, { 0x02, 0x11390000 }, { 0x40, 0x11120000 }, { 0, 0 } } },
	{ 0x11430000, { { 0x10, 0x114b0000 }, { 0x02, 0x11790000 }, { 0x40, 0x11520000 }, { 0, 0 } } },
	{ 0x12030000, { { 0x20, 0x12050000 }, { 0x01, 0x12160000 }, { 0, 0 }, { 0, 0 } } },
	{ 0x12430000, { { 0x20, 0x12450000 }, { 0x20, 0x12460000 }, { 0, 0 }, { 0, 0 } } },
	{ 0x13030000, { { 0x08, 0x13110000 }, { 0x08, 0x13190000 }, { 0x04, 0x13120000 }, { 0, 0 } } },
	{ 0x13430000, { { 0x04, 0x13510000 }, { 0x20, 0x13590000 }, { 0x08, 0x13520000 }, { 0, 0 } } },
};

/* The debug and trace controller (DTC) */
#define CI_DT_CTL		0x120e0a00u	/* bit 0: enable */
#define CI_DTC_CTL		0x120e2100u	/* bit 0: enable */
#define CI_DTC_PMEVCNT(i)	(0x120e2000u + 0x10 * (i))	/* 0x040-0x090 used */
#define CI_PMU_EVENT_SEL	0x2000		/* in a device or XP */
#define CI_DTM_CTL		0x2100		/* in an XP; bit 0: enable */
/* Watchpoints, four per direction pair (ports 0-1 at 0x21a0, 2-3 at 0x23a0) */
#define CI_WP_CFG(dir, wp)	(((dir) < 2 ? 0x21a0 : 0x23a0) + 0x18 * (wp))
#define CI_WP_VAL		0x08
#define CI_WP_MASK		0x10
#define CI_WP_IDLE_VAL		0xccccccccccccccccull

static void ci700_dt_enable(uint32_t reg, bool on)
{
	uint64_t v = soc_read64(reg);

	soc_write64(reg, (v & ~1ull) | on);
}

static void ci700_clear_watchpoint(uint32_t xp, unsigned dir, unsigned wp)
{
	uint32_t r = xp + CI_WP_CFG(dir, wp);

	soc_write64(r, 0);
	soc_write64(r + CI_WP_VAL, CI_WP_IDLE_VAL);
	soc_write64(r + CI_WP_MASK, ~0ull);
}

/* CIX's dfd_port.c dfd_ci700_free_trace: the DFD block's funnel and ETF off
 * if the trace powered them (only its debug CLI does), the CI-700's trace
 * output off */
static bool _s_dfd_pwr_up;

static void dfd_ci700_free_trace(void)
{
	if (_s_dfd_pwr_up) {
		soc_write32(0x180d0000, 0);
		soc_write32(0x180c0020, 0);
	}
	soc_write32(0x1a000b00, 0);
	_s_dfd_pwr_up = false;
}

void ci700_free_all_pmu_counters(void)
{
	/* XP registers the PM's counters use, in CIX's order */
	static const uint16_t xp_regs[] = { 0x2000, 0x2210, 0x2410, 0x2220, 0x2240, 0x2420, 0x2440 };

	ci700_dt_enable(CI_DT_CTL, false);
	ci700_dt_enable(CI_DTC_CTL, false);
	soc_write64(CI_DTC_PMEVCNT(4), 0);
	soc_write64(CI_DTC_PMEVCNT(9), 0);
	for (unsigned i = 0; i < ARRAY_SIZE(ci700_xps); i++) {
		const struct ci700_xp *xp = &ci700_xps[i];

		for (unsigned d = 0; d < 4; d++) {
			if (xp->dev[d].type)
				soc_write64(xp->dev[d].base + CI_PMU_EVENT_SEL, 0);
		}
		for (unsigned wp = 0; wp < 4; wp++) {
			ci700_clear_watchpoint(xp->base, 0, wp);
			ci700_clear_watchpoint(xp->base, 2, wp);
		}
		for (unsigned r = 0; r < ARRAY_SIZE(xp_regs); r++)
			soc_write64(xp->base + xp_regs[r], 0);
		ci700_dt_enable(xp->base + CI_DTM_CTL, false);
	}
	for (unsigned i = 5; i <= 8; i++)
		soc_write64(CI_DTC_PMEVCNT(i), 0);
	dfd_ci700_free_trace();
}

/* --- event collections: not ported yet -------------------------------------- */

CI700_PMU_EVT_COLLECTION *ci700_create_pmu_event_collection(uint32_t node_mask, uint8_t event_encode,
							    uint16_t cascade_mode)
{
	(void)node_mask;
	(void)event_encode;
	(void)cascade_mode;
	return NULL;
}

bool ci700_free_pmu_event_collection(CI700_PMU_EVT_COLLECTION *coll)
{
	(void)coll;
	return false;
}

uint32_t ci700_increment_pmu_event_collection(CI700_PMU_EVT_COLLECTION *coll)
{
	(void)coll;
	return 0;
}

void ci700_take_pmu_cnt_snapshot(void)
{
}

void ci700_fetch_pmu_cnt_snapshot(void)
{
}

/* --- the trace into the DFD block (ci700_dfd.c) ------------------------------- */

static struct {
	uint32_t etr;		/* the trace's ETR registers, through the hub (+92) */
	uint32_t buf;		/* the trace buffer (+96) */
	uint32_t tail;		/* (+100) */
} _s_req;

/* CIX's dfd_port.c: the CI-700 trace is set up only from its debug CLI */
static bool dfd_ci700_trace_enabled(void)
{
	return false;
}

/* The trace's DRAM through the hub: the ETR's block and the buffer */
void ci700_dfd_trace_init(void)
{
	_s_req.buf = tlb_ctrl_dram_to_syshub(0x83400000ull, false);
	_s_req.etr = tlb_ctrl_dram_to_syshub(0x83600000ull, false);
	if (!_s_req.etr || !_s_req.buf)
		__builtin_trap();	/* CIX: vAssertCalledWithTRC(0xf1610165) */
	_s_req.buf += 0x7f1680;
}

/* The coarse tick: the ETR's write pointer (CIX reads it, nobody uses it) */
void ci700_dfd_read_sampling(void)
{
	if (dfd_ci700_trace_enabled())
		(void)REG32(_s_req.etr + 48);
}

/* A sample of the trace for the PMU package (CIX's overlay); with no trace
 * set up there is none */
bool ci700_dfd_trace_process(void *buf)
{
	(void)buf;
	return dfd_ci700_trace_enabled();
}
