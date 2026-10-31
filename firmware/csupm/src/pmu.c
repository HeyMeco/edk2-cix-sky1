/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Counter reset before the perf streams start (CIX's _sw_stream_reset,
 * ni700_pmu.c, ci700_pmu.c, ci700_dfd.c, ap_event.c): every counter the PM
 * may have claimed is stopped and cleared, at power-on and when the
 * streams are restored or re-armed.
 *
 * The layouts are those of CIX's ni700_topology.c and ci700_topology.c.
 */
#include "pmu.h"
#include "soc.h"

/* --- NI-700 ---------------------------------------------------------- */

struct ni700_hub {
	uint32_t base;		/* configuration space; components from +0x4000, 4 KiB each */
	uint32_t pmu;
	uint16_t clock;		/* CIX clock-tree node */
	uint8_t components;
	bool clocked;		/* see ni700_clocked() */
};

/*
 * Whether a hub's clock runs.  CIX asks its clock tree (a non-zero rate
 * for the node); a hub without a clock would stall the bus.  Until the
 * clock tree is here this is what CIX's tree reports at boot under
 * pm_emu: the PCIe and SMN hubs run, the MM and system hubs do not.
 */
static const struct ni700_hub ni700_hubs[] = {
	{ 0x0d040000, 0x0d063000, 0x0c2, 31, false },	/* mmhub */
	{ 0x0d010000, 0x0d021000, 0x11e, 13, true },	/* pcie */
	{ 0x0d090000, 0x0d0ad000, 0x0c4, 25, false },	/* syshub */
	{ 0x0d0d0000, 0x0d113000, 0x01b, 63, true },	/* smnhub */
};

#define NI_COMP(h, k)		((h)->base + 0x4000 + 0x1000 * (k))
#define NI_PMEVCNTR(i)		(0x400 + 4 * (i))	/* 8 counters */
#define NI_PMCNTENCLR		0xc20
#define NI_PMINTENCLR		0xc60
#define NI_PMOVSCLR		0xc80
#define NI_PMCR			0xe04
#define NI_PMCR_P		BIT(1)		/* reset event counters */
#define NI_PMCR_C		BIT(2)		/* reset cycle counter */
#define NI_ALL_COUNTERS		0x800000ffu	/* cycle counter and counters 0-7 */

static void ni700_free_all(const struct ni700_hub *h)
{
	for (unsigned k = 0; k < h->components; k++) {
		/* the component's PMU event selection */
		soc_write32(NI_COMP(h, k) + 0xc, 0);
		soc_write32(NI_COMP(h, k) + 0x10, 0);
	}
	for (unsigned i = 0; i < 8; i++)
		soc_write32(h->pmu + NI_PMEVCNTR(i), 0);
	soc_write32(h->pmu + NI_PMCNTENCLR, NI_ALL_COUNTERS);
	soc_write32(h->pmu + NI_PMINTENCLR, NI_ALL_COUNTERS);
	soc_write32(h->pmu + NI_PMOVSCLR, NI_ALL_COUNTERS);
	soc_write32(h->pmu + NI_PMCR, NI_PMCR_P | NI_PMCR_C);
}

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

static void ci700_free_all(void)
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
}

/* The CI-700 trace into the DFD block; the PM has not powered it up */
#define DFD_TRACE_CTL		0x1a000b00u

static void ci700_free_trace(void)
{
	soc_write32(DFD_TRACE_CTL, 0);
}

/* --- AP -------------------------------------------------------------- */

#define AP_AMU_CTL		0x0f040c00u

/* MPMM: a 4-bit gear per domain (0-7 at +4, 8-11 at +8), enables at +0 */
#define MPMM_ENABLE		(PM_LOCAL + 0x0)
#define MPMM_GEAR_LO		(PM_LOCAL + 0x4)
#define MPMM_GEAR_HI		(PM_LOCAL + 0x8)
#define MPMM_DOMAINS		12

void mpmm_set_gear(uint32_t mask, uint32_t gear, bool enable)
{
	uint32_t lo = REG32(MPMM_GEAR_LO), hi = REG32(MPMM_GEAR_HI);

	for (unsigned i = 0; i < 8; i++) {
		if (mask & BIT(i))
			lo = (lo & ~(0xfu << 4 * i)) | (enable ? gear << 4 * i : 0);
	}
	for (unsigned i = 0; i < 4; i++) {
		if (mask & BIT(8 + i))
			hi = (hi & ~(0xfu << 4 * i)) | (enable ? gear << 4 * i : 0);
	}
	REG32(MPMM_GEAR_LO) = lo;
	REG32(MPMM_GEAR_HI) = hi;
	mask &= BIT(MPMM_DOMAINS) - 1;
	if (enable)
		REG32(MPMM_ENABLE) |= mask;
	else
		REG32(MPMM_ENABLE) &= ~mask;
}

void pmu_reset_all(void)
{
	for (unsigned i = 0; i < ARRAY_SIZE(ni700_hubs); i++) {
		if (ni700_hubs[i].clocked)
			ni700_free_all(&ni700_hubs[i]);
	}
	ci700_free_all();
	ci700_free_trace();
	soc_write32(AP_AMU_CTL, 0);
	mpmm_set_gear(BIT(MPMM_DOMAINS) - 1, 0, false);
}
