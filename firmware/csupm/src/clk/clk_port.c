/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * What the clock code needs from the rest of CIX's firmware: the tree
 * helpers (u_tree.c), bit helpers (u_misc.c), the RCSU hardware mutexes
 * (sys_ctrl.c), delays and critical sections.
 */
#include "clk.h"
#include "lcru.h"

/* --- critical sections, time -------------------------------------------- */

uint32_t clk_lock(void)
{
	uint32_t ps;

	__asm__ volatile("rsil %0, 15" : "=a"(ps) : : "memory");
	return ps;
}

void clk_unlock(uint32_t ps)
{
	__asm__ volatile("wsr.ps %0; rsync" : : "a"(ps) : "memory");
}

void cc_delay(uint32_t cycles)
{
	udelay_cycles(cycles);
}

void us_delay(uint32_t us)
{
	udelay_cycles(us * (core_hz / 1000000));
}

/* microseconds since start-up: CCOUNT extended by the reads */
uint64_t arch_getTimeNowUs(void)
{
	static uint32_t high, last;
	uint32_t ps = clk_lock(), c = ccount();

	if (c < last)
		high++;
	last = c;
	clk_unlock(ps);
	return ((uint64_t)high << 32 | c) / (core_hz / 1000000);
}

uint32_t get_c_count(void)
{
	return ccount();
}

/* --- small helpers CIX's DWARF has no record of --------------------------- */

/* A PLL's enable (+4 bit 24); no PLL counts as on */
uint32_t pll_ctrl_get_state(void *pll_base)
{
	if (pll_base == NULL)
		return 1;
	return (REG32((uintptr_t)pll_base + 4) >> 24) & 1;
}

/* SoC registers seen through the PM's window */
bool is_smn_space(uint32_t addr)
{
	return (addr & 0xe0000000u) == SOC_WINDOW;
}

/* Rates within 5 Hz are the same */
bool tolerance_equal(uint32_t a, uint32_t b)
{
	return (a > b ? a - b : b - a) < 6;
}

/* --- bits (u_misc.c) ------------------------------------------------------ */

uint8_t u_misc_u32BitCount(uint32_t d)
{
	return (uint8_t)__builtin_popcount(d);
}

uint8_t u_misc_u64BitCount(uint64_t d)
{
	return (uint8_t)(u_misc_u32BitCount((uint32_t)d) + u_misc_u32BitCount((uint32_t)(d >> 32)));
}

/* The lowest set bit at or above startPos, 128 if none */
uint8_t u_misc_u32lowestOneBit(uint32_t d, uint32_t startPos)
{
	d >>= startPos;
	if (d == 0)
		return 128;
	return (uint8_t)(__builtin_ctz(d) + startPos);
}

/* --- trees (u_tree.c) ------------------------------------------------------ */

/* Children are a ring through brother_prev/next; childern is the first */
void tree_add_child(TREE_NODE *parent, TREE_NODE *child)
{
	TREE_NODE *first = parent->childern;

	child->parent = parent;
	child->depth = parent->depth + 1;
	if (first) {
		TREE_NODE *last = first->brother_prev;

		child->brother_prev = last;
		last->brother_next = child;
	} else {
		first = child;
		parent->childern = child;
	}
	first->brother_prev = child;
	child->brother_next = first;
}

void tree_traverse(TREE_NODE *node, void (*visit)(TREE_NODE *))
{
	TREE_NODE *c, *first;

	if (visit)
		visit(node);
	first = node->childern;
	if (first == NULL)
		return;
	tree_traverse(first, visit);
	for (c = first->brother_next; c != first; c = c->brother_next)
		tree_traverse(c, visit);
}

/* Stops at the first visit that returns false */
bool tree_traverse1(TREE_NODE *node, void *data, bool (*visit)(TREE_NODE *, void *))
{
	TREE_NODE *c, *first;

	if (visit == NULL || !visit(node, data))
		return false;
	first = node->childern;
	if (first == NULL)
		return true;
	if (!tree_traverse1(first, data, visit))
		return false;
	for (c = first->brother_next; c != first; c = c->brother_next) {
		if (!tree_traverse1(c, data, visit))
			return false;
	}
	return true;
}

/* The lock id of the first locked node in the subtree, 0 if none */
uint32_t tree_is_any_node_locked(TREE_NODE *node)
{
	TREE_NODE *c, *first = node->childern;
	uint32_t id;

	if (node->lock_id)
		return node->lock_id;
	if (first == NULL)
		return 0;
	if ((id = tree_is_any_node_locked(first)) != 0)
		return id;
	for (c = first->brother_next; c != first; c = c->brother_next) {
		if ((id = tree_is_any_node_locked(c)) != 0)
			return id;
	}
	return 0;
}

static bool set_lock_id(TREE_NODE *node, void *data)
{
	node->lock_id = *(uint32_t *)data;
	return true;
}

bool tree_lock_subtree(TREE_NODE *node, uint32_t lock_id)
{
	uint32_t ps = clk_lock();
	bool ok = false;

	if (tree_is_any_node_locked(node) == 0)
		ok = tree_traverse1(node, &lock_id, set_lock_id);
	clk_unlock(ps);
	return ok;
}

bool tree_unlock_subtree(TREE_NODE *node)
{
	uint32_t id = 0, ps = clk_lock();
	bool ok = tree_traverse1(node, &id, set_lock_id);

	clk_unlock(ps);
	return ok;
}

/* --- RCSU hardware mutexes (CIX's smn_rcsu_lock) ----------------------------- */

/*
 * Registers of a subsystem's RCSU are shared with the other processors;
 * read-modify-writes take the subsystem's hardware mutex: 100 at
 * 0xa0000900, taken by writing the owner's id until it reads back, given
 * back by writing it again.  The mutex is the RCSU's index in this table
 * of their bases (64 KiB each, PM addresses).
 */
#define HW_MUTEX(i)		(0xa0000900u + 4 * (i))
#define HW_MUTEX_PM		0x30

static const uint32_t rcsu_base[53] = {
	0x40000000, 0x45000000, 0x46000000, 0x47000000, 0x48000000, 0x49000000, 0x49070000,
	0x490e0000, 0x49150000, 0x491c0000, 0x49250000, 0x49280000, 0x492b0000, 0x492e0000,
	0x49310000, 0x4a000000, 0x4a060000, 0x4b000000, 0x4b0d0000, 0x4b1a0000, 0x4c000000,
	0x4c020000, 0x4c040000, 0x4c060000, 0x4c080000, 0x4c0a0000, 0x4c0c0000, 0x4c0e0000,
	0x4c100000, 0x4d000000, 0x4d030000, 0x4d080000, 0x00000000, 0x4e000000, 0x4fc50000,
	0x50000000, 0x54000000, 0x54050000, 0x54070000, 0x540c0000, 0x540e0000, 0x54130000,
	0x54150000, 0x541a0000, 0x541c0000, 0x54210000, 0x54230000, 0x54250000, 0x54270000,
	0x542d0000, 0x54330000, 0x54350000, 0x55000000,
};

/* CIX's binary search over the table (with its odd entry 32), 53 if none */
static uint32_t rcsu_id(uint32_t addr)
{
	uint32_t lo = 0, hi = 52;

	while (lo <= hi) {
		uint32_t mid = (lo + hi) / 2, base = rcsu_base[mid];

		if (addr < base) {
			if (mid == 0)
				break;
			hi = mid - 1;
		} else if (addr < base + 0x10000) {
			return mid;
		} else {
			lo = mid + 1;
		}
	}
	return 53;
}

void smn_rcsu_lock(uint32_t addr)
{
	uint32_t id;

	if (!is_smn_space(addr) || (id = rcsu_id(addr)) > 52)
		return;
	do
		REG32(HW_MUTEX(id)) = HW_MUTEX_PM;
	while (REG32(HW_MUTEX(id)) != HW_MUTEX_PM);
}

void smn_rcsu_unlock(uint32_t addr)
{
	uint32_t id;

	if (!is_smn_space(addr) || (id = rcsu_id(addr)) > 52)
		return;
	if (REG32(HW_MUTEX(id)) == HW_MUTEX_PM)
		REG32(HW_MUTEX(id)) = HW_MUTEX_PM;
}

/* --- hooks ------------------------------------------------------------------ */

/* The system state: set by main as it goes through CIX's phases */
SS_HSM_STATE pm_ss_state = HSM_ST_POR;

SS_HSM_STATE ss_hsm_get(void)
{
	return pm_ss_state;
}

bool perf_soc_idle_switch_get_enable(void)
{
	return false;
}

void dpdpu_trigger_ddr_dfs_compatible_checker(void)
{
}

void vpu_ddr_dfs_set_compatible(bool is_compatible)
{
	(void)is_compatible;
}

void isp_ddr_dfs_set_compatible(bool is_compatible)
{
	(void)is_compatible;
}

void gpu_ddr_dfs_set_compatible(bool is_compatible)
{
	(void)is_compatible;
}

bool perf_stream_is_active(void)
{
	return false;
}

void perf_stream_track_clk_change(uint32_t clk_idx, uint32_t freq)
{
	(void)clk_idx;
	(void)freq;
}

/* CIX's vAssertCalledWithTRC: stop where it can be seen */
void clk_assert_failed(uint32_t code)
{
	REG32(PM_SCRATCH + 0x3c) = code;
	__builtin_trap();
}
