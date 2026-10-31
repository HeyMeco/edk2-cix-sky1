/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The PLL programming policy: CIX's Source/projects/sky1/clktree/
 * pll_policy.c, translated.
 *
 * Each of the 23 PLLs in pll_dev[] (clk_data.c) is driven by a small
 * hierarchical state machine.  Its states are the nodes of this tree
 * (pll_hsm[]), each with an enter and an exit handler (hsm_ctrl_nodes[]):
 *
 *   INIT
 *    +- STATIC_CFG              PLL off: dividers may be written
 *    +- PLL_ENABLE              PLL on
 *        +- NEW_FREQ            dividers rewritten with the PLL on
 *        |                      ("offline stepping")
 *        +- CONFIRMED_LOCK      locked, output retimed
 *            +- SUBTREE_ATTACHED  the clocks it feeds switched back to it
 *                +- TUNE_ONTHEFLY   FBDIV stepped by the PLL's sequencer
 *                |                  ("online stepping")
 *                +- STABLE_RUNNING
 *
 * Going from one state to another exits every state up to the common
 * parent and enters every state down to the target (tree_traverse_path).
 * A frequency change from STABLE_RUNNING is one of:
 *  - the default: back to STATIC_CFG (the subtree moves to another source,
 *    the PLL is switched off) and forward to STABLE_RUNNING again (the
 *    dividers are written, the PLL is switched on, waited for, retimed and
 *    the subtree switched back);
 *  - offline stepping (bitmap_allow_OFFLINE_STEPPING): to NEW_FREQ and back,
 *    the PLL stays on while its subtree is on another source;
 *  - online stepping (bitmap_allow_ONLINE_STEPPING, PLLs with a sequencer
 *    whose REFDIV and POSTDIVs do not change): to TUNE_ONTHEFLY and back, the
 *    sequencer walks FBDIV to the target in steps of 2 with the subtree
 *    attached.
 * Spread spectrum (SSMOD) and the PSC clock ramp on glitch-detecting PLLs
 * are switched along the way when their bitmaps allow it.
 *
 * Without an RTOS.  CIX runs this under FreeRTOS.  When CONFIRMED_LOCK is
 * entered from clk_api_* (which hold the scheduler lock) outside STR, CIX
 * refreshes and locks the PLL's clock subtree, takes the PLL's mutex,
 * registers the PLL in bitmap_pll_to_poll and sleeps on a task notification
 * for at most one tick (1 ms, the tick is 1 kHz); _PollLockBit, a 250 us
 * md_timer, polls the lock bits of the registered PLLs and notifies the
 * waiting task (_CheckIfPllIsNotLocked skips the sleep if the PLL locked in
 * the meantime).  Otherwise (STR, or no scheduler lock) CIX busy-waits on
 * the lock bit with pll_ctrl_wait_for_lock.  The open firmware is single
 * threaded and does everything in line: it always busy-waits on the lock
 * bit, for 1000 us (the tick) or, in STR context, for CIX's busy-wait time.
 * The subtree lock, the per-PLL mutexes (xSemph, pll_policy_lock_entry/
 * unlock_entry), the task handles (tskHandle), the timer, _PollLockBit,
 * _CheckIfPllIsNotLocked and bitmap_pll_to_poll only served other tasks
 * and are left out.  CIX's critical sections (md_tasks_enterCritical)
 * become clk_lock()/clk_unlock().  The register writes are CIX's, in its
 * order.
 */
#include "clk.h"
#include "lcru.h"

#define PLL_NUM			23	/* entries of pll_dev[] */

/* pll_dev[] entries with special handling */
#define PLL_LOC_DSU		5
#define PLL_LOC_GPUTOP		17
#define PLL_LOC_GPUCORE		18
#define PLL_LOC_CI700		19

/* PLL registers (offsets from CLK_PLL_ATTR.pll_base) */
#define PLL_P1			0x00	/* PLL_FREQ_P1: FBDIV 11:0 */
#define PLL_P2			0x04	/* PLL_FREQ_P2: FRAC 23:0, PLLEN 24, DSMEN 28 */
#define PLL_P3			0x08	/* PLL_FREQ_P3: POSTDIV1 2:0, POSTDIV2 6:4, REFDIV 13:8 */
#define PLL_CTRL_REG		0x0c	/* PLL_CTRL */
#define PLL_SSMOD_REG		0x10	/* PLL_SSMOD */
#define PLL_SEQ_TARGET_FBDIV	0x14	/* sequencer (PLLs with has_seqcr) */
#define PLL_SEQ_START_FBDIV	0x18
#define PLL_SEQ_STEP_SIZE	0x1c
#define PLL_SEQ_STEP_COUNT	0x20
#define PLL_SEQ_TARGET_FRAC	0x24
#define PLL_SEQ_HW_SEL		0x28
#define PLL_GLICHDET		0x2c	/* PLL_DROOP_DET (PLLs with has_glichdet) */
#define PLL_PSC1		0x30
#define PLL_PSC2		0x34

#define P1_FBDIV		0xfffu
#define P2_FRAC			0xffffffu
#define P2_DSMEN		BIT(28)
#define P3_REFDIV		(0x3fu << 8)
#define P3_POSTDIV		0x77u
#define CTRL_DACEN		BIT(1)
#define CTRL_EN_RETIMING	BIT(6)
#define SSMOD_SPREAD		(0x1fu << 8)
#define SSMOD_DOWNSPREAD	BIT(16)
#define SSMOD_RESET		BIT(20)
#define SSMOD_DISABLE_SSCG	BIT(24)
#define SSMOD_RESETPTR		BIT(28)
#define SEQ_MAX_SM_CNT		0xfu
#define GLICHDET_PD		BIT(16)
#define PSC1_RAMP_REQ		BIT(0)

/* SSMOD as CIX sets it: center spread, SPREAD = 10 */
#define SSMOD_SPREAD_CIX	(10u << 8)
/* PSC2 as CIX sets it: pscStartStepDly = 15, the rest 0 */
#define PSC2_CIX		0xf00u
/* The sequencer: FBDIV steps of 2, max SM count 4, 80 cycles a step */
#define SEQ_STEP		2
#define SEQ_SM_CNT		4
#define SEQ_STEP_CYCLES		80

/*
 * PLLs whose delta-sigma modulator STATIC_CFG turns off (unless spread
 * spectrum or a fractional FBDIV needs it): CPU_PLL_800M, MMHUB, MMSYS,
 * CI-700, SYS and PCIe (CIX: a switch on the index).
 */
#define PLL_DSM_OFF		(BIT(6) | BIT(12) | BIT(13) | BIT(19) | BIT(20) | BIT(21))

/* Bits cleared while the PLL behind them changes (CIX gives no names) */
#define MMHUB_POSTDIV_GATE	0xa0062014u	/* bit 4 */
#define DSU_PLL_GATE		0xa006052cu	/* bit 0 */
#define CI700_PLL_GATE		0xa0065214u	/* bit 0 */

/* pll_solve_*: the rate error must stay below this */
#define PLL_SOLVE_MAX_ERR	25

/* Lock timeout: one FreeRTOS tick; CIX's busy-wait clamps to [5, 1000] us */
#define PLL_LOCK_TIMEOUT_US	1000
#define PLL_LOCK_MIN_US		5
/* Cycles between switching a PLL off and on again */
#define PLL_OFF_CYCLES		800

/* clk_port.c */

static PLL_POLICY_STATE pll_state_transition(uint32_t local_idx, PLL_POLICY_STATE target_state);
static PLL_POLICY_STATE get_state(uint32_t local_idx);
static void _pll_set_freq(uint64_t target_vco, uint32_t target_freq, CLK_TREE_STRUCT *pSt);
static bool pll_enable_enter(uint32_t loc_idx);
static bool pll_enable_exit(uint32_t loc_idx);
static bool apply_new_freq_enter(uint32_t loc_idx);
static bool apply_new_freq_exit(uint32_t loc_idx);
static bool confirm_lock_enter(uint32_t loc_idx);
static bool confirm_lock_exit(uint32_t loc_idx);
static bool subtree_attach_enter(uint32_t loc_idx);
static bool subtree_attach_exit(uint32_t loc_idx);
static bool stepping_on_the_fly_enter(uint32_t loc_idx);
static bool stepping_on_the_fly_exit(uint32_t loc_idx);
static bool stable_running_enter(uint32_t loc_idx);
static bool stable_running_exit(uint32_t loc_idx);
static bool _hsm_exit_node(TREE_NODE *node, void *data);
static bool _hsm_enter_node(TREE_NODE *node, void *data);
static PLL_POLICY_STATE node_to_state(TREE_NODE *node);
static void set_state(uint32_t local_idx, PLL_POLICY_STATE new_st);

/* --- state ----------------------------------------------------------------- */

/* Read by clk_tree.c: PLL rates and states go through the policy */
bool g_enable_pll_policy = true;

/*
 * What the policy may do, one bit per pll_dev[] entry.  CIX's
 * pll_policy_config would set them from pm_config_pll_t, but it is not in
 * CIX's image (do_pm_config does not call it): these are the values CIX
 * runs with.  Glitch detection with clock ramping is on for the CPU PLLs
 * 0-4 and GPUCORE (18); spread spectrum and stepping are off.
 */
static uint32_t bitmap_allow_GLICD = 0x4001f;
static uint32_t bitmap_allow_SSMOD;
static uint32_t bitmap_allow_OFFLINE_STEPPING;
static uint32_t bitmap_allow_ONLINE_STEPPING;

/* Set across STR save/restore: CIX then busy-waits for lock */
static bool _s_is_str_save_restore_context;

static PLL_POLICY_HSM_NODE hsm_ctrl_nodes[PLL_HSM_MAX] = {
	[PLL_HSM_INIT] = { "init", pll_policy_init, pll_policy_deinit },
	[PLL_HSM_STATIC_CFG] = { "stc_cfg", pll_static_cfg_enter, pll_static_cfg_exit },
	[PLL_HSM_PLL_ENABLE] = { "pll_en_", pll_enable_enter, pll_enable_exit },
	[PLL_HSM_NEW_FREQ] = { "newfreq", apply_new_freq_enter, apply_new_freq_exit },
	[PLL_HSM_CONFIRMED_LOCK] = { "cfm_lok", confirm_lock_enter, confirm_lock_exit },
	[PLL_HSM_SUBTREE_ATTACHED] = { "subatch", subtree_attach_enter, subtree_attach_exit },
	[PLL_HSM_TUNE_ONTHEFLY] = { "set_otf", stepping_on_the_fly_enter, stepping_on_the_fly_exit },
	[PLL_HSM_STABLE_RUNNING] = { "running", stable_running_enter, stable_running_exit },
};

/* The state tree; pll_hsm_init links it */
static TREE_NODE pll_hsm[PLL_HSM_MAX] = {
	[PLL_HSM_INIT] = { .node_data = &hsm_ctrl_nodes[PLL_HSM_INIT] },
	[PLL_HSM_STATIC_CFG] = { .node_data = &hsm_ctrl_nodes[PLL_HSM_STATIC_CFG] },
	[PLL_HSM_PLL_ENABLE] = { .node_data = &hsm_ctrl_nodes[PLL_HSM_PLL_ENABLE] },
	[PLL_HSM_NEW_FREQ] = { .node_data = &hsm_ctrl_nodes[PLL_HSM_NEW_FREQ] },
	[PLL_HSM_CONFIRMED_LOCK] = { .node_data = &hsm_ctrl_nodes[PLL_HSM_CONFIRMED_LOCK] },
	[PLL_HSM_SUBTREE_ATTACHED] = { .node_data = &hsm_ctrl_nodes[PLL_HSM_SUBTREE_ATTACHED] },
	[PLL_HSM_TUNE_ONTHEFLY] = { .node_data = &hsm_ctrl_nodes[PLL_HSM_TUNE_ONTHEFLY] },
	[PLL_HSM_STABLE_RUNNING] = { .node_data = &hsm_ctrl_nodes[PLL_HSM_STABLE_RUNNING] },
};

/* --- helpers ----------------------------------------------------------------- */

static inline void *pll_ptr(const CLK_PLL_ATTR *cfg)
{
	return (void *)(uintptr_t)cfg->pll_base;
}

static inline CLK_TREE_STRUCT *clk_node(uint32_t enum_idx)
{
	return clk_nodes_get((CLK_TREE_ENUM_IDX)enum_idx);
}

static inline bool ssmod_allowed(const PLL_CTRL_POLICY *p, uint32_t loc_idx)
{
	return !p->cfg.no_ssmod && (bitmap_allow_SSMOD & BIT(loc_idx));
}

/* CIX's arch_getTimeStamp: CCOUNT extended to 64 bits, in cycles */
static uint64_t pll_timestamp(void)
{
	static uint32_t high, last;
	uint32_t ps = clk_lock(), c = ccount();

	if (c < last)
		high++;
	last = c;
	clk_unlock(ps);
	return (uint64_t)high << 32 | c;
}

/*
 * CIX's soc_pull_register: wait until (REG32(addr) & mask) == (target &
 * mask), checking every 20 cycles (every 200 for timeouts over 1000), at
 * most timeout_cc cycles (0: forever).
 */
static bool pull_register(uint32_t addr, uint32_t mask, uint32_t target,
			  uint32_t timeout_cc)
{
	uint32_t t0, step = timeout_cc > 1000 ? 200 : 20;

	target &= mask;
	if ((REG32(addr) & mask) == target)
		return true;
	t0 = ccount();
	do {
		cc_delay(step);
		if ((REG32(addr) & mask) == target)
			return true;
	} while (timeout_cc == 0 || ccount() - t0 < timeout_cc);
	return false;
}

/* --- set up, STR -------------------------------------------------------------- */

void pll_policy_set_str_context(bool is_str_context)
{
	_s_is_str_save_restore_context = is_str_context;
}

/* INIT is the root: it is never left */
bool pll_policy_deinit(uint32_t loc_idx)
{
	(void)loc_idx;
	clk_assert_failed(0xf1a71387);
	return false;
}

/* STATIC_CFG: the glitch detector is powered down while the PLL is off */
bool pll_static_cfg_enter(uint32_t loc_idx)
{
	PLL_CTRL_POLICY *p = &pll_dev[loc_idx];

	if (p->cfg.has_glichdet) {
		uint32_t base = p->cfg.pll_base, v = REG32(base + PLL_GLICHDET);

		if (!(v & GLICHDET_PD))
			REG32(base + PLL_GLICHDET) = v | GLICHDET_PD;
	}
	return true;
}

/*
 * Leaving STATIC_CFG (the PLL is still off): DAC on, spread spectrum set
 * up if allowed (it needs the delta-sigma modulator), the modulator on or
 * off by PLL, then the dividers prepared by _pll_set_freq (or saved by
 * pll_policy_save) written; a fractional FBDIV needs the modulator too.
 */
bool pll_static_cfg_exit(uint32_t loc_idx)
{
	PLL_CTRL_POLICY *p = &pll_dev[loc_idx];
	uint32_t base = p->cfg.pll_base, v, p1, p2, p3;

	v = REG32(base + PLL_CTRL_REG);
	if (!(v & CTRL_DACEN))
		REG32(base + PLL_CTRL_REG) = v | CTRL_DACEN;
	p1 = REG32(base + PLL_P1);
	p2 = REG32(base + PLL_P2);
	p3 = REG32(base + PLL_P3);
	if (ssmod_allowed(p, loc_idx)) {
		v = REG32(base + PLL_SSMOD_REG);
		REG32(base + PLL_SSMOD_REG) = (v & ~(SSMOD_SPREAD | SSMOD_DOWNSPREAD)) |
					      SSMOD_SPREAD_CIX;
		p2 |= P2_DSMEN;
	} else {
		p2 &= ~P2_DSMEN;
		if (!(PLL_DSM_OFF & BIT(loc_idx)))
			p2 |= P2_DSMEN;
	}
	REG32(base + PLL_P2) = p2;
	if (p->target_FBDIV) {
		p1 = (p1 & ~P1_FBDIV) | (p->target_FBDIV & P1_FBDIV);
		p2 = (p2 & ~P2_FRAC) | (p->target_frac & P2_FRAC);
		if (p->target_frac & P2_FRAC)
			p2 |= P2_DSMEN;
		p3 = (p3 & ~P3_REFDIV) | (p->target_REFDIV & 0x3f) << 8;
		REG32(base + PLL_P1) = p1;
		REG32(base + PLL_P2) = p2;
		REG32(base + PLL_P3) = p3;
	}
	if (p->target_POSTDIV1)
		REG32(base + PLL_P3) = (p3 & ~P3_POSTDIV) | (p->target_POSTDIV1 & 7) |
				       (p->target_POSTDIV2 & 7) << 4;
	return true;
}

/*
 * The initial state: a PLL already running (or one without an enable)
 * starts in STABLE_RUNNING, one that is off in STATIC_CFG.
 */
bool pll_policy_init(uint32_t loc_idx)
{
	PLL_CTRL_POLICY *p = &pll_dev[loc_idx];
	PLL_POLICY_STATE st = PLL_HSM_STABLE_RUNNING;

	if (!p->cfg.no_pllen && pll_ctrl_get_state(pll_ptr(&p->cfg)) == 0) {
		bool ok = hsm_ctrl_nodes[PLL_HSM_STATIC_CFG].enter(loc_idx);

		st = PLL_HSM_STATIC_CFG;
		CLK_ASSERT(ok, 0xf1a71554);
	}
	p->pll_st = st;
	return true;
}

/* Links the state tree and puts every PLL in its initial state */
void pll_hsm_init(void)
{
	uint32_t i;

	for (i = 0; i < PLL_HSM_MAX; i++) {
		const void *data = pll_hsm[i].node_data;

		memset(&pll_hsm[i], 0, sizeof(pll_hsm[i]));
		pll_hsm[i].node_data = data;
		CLK_ASSERT(data != NULL, 0xf1a71579);
	}
	tree_add_child(&pll_hsm[PLL_HSM_INIT], &pll_hsm[PLL_HSM_STATIC_CFG]);
	tree_add_child(&pll_hsm[PLL_HSM_INIT], &pll_hsm[PLL_HSM_PLL_ENABLE]);
	tree_add_child(&pll_hsm[PLL_HSM_PLL_ENABLE], &pll_hsm[PLL_HSM_NEW_FREQ]);
	tree_add_child(&pll_hsm[PLL_HSM_PLL_ENABLE], &pll_hsm[PLL_HSM_CONFIRMED_LOCK]);
	tree_add_child(&pll_hsm[PLL_HSM_CONFIRMED_LOCK], &pll_hsm[PLL_HSM_SUBTREE_ATTACHED]);
	tree_add_child(&pll_hsm[PLL_HSM_SUBTREE_ATTACHED], &pll_hsm[PLL_HSM_TUNE_ONTHEFLY]);
	tree_add_child(&pll_hsm[PLL_HSM_SUBTREE_ATTACHED], &pll_hsm[PLL_HSM_STABLE_RUNNING]);
	for (i = 0; i < PLL_NUM; i++) {
		bool ok;

		pll_dev[i].pll_st = PLL_HSM_INIT;
		ok = hsm_ctrl_nodes[PLL_HSM_INIT].enter(i);
		CLK_ASSERT(ok, 0xf1a71608);
	}
}

/* &tab[i].cfg for the entry of enum_idx, NULL if none */
static CLK_PLL_ATTR *find_cfg(CLK_TREE_ENUM_IDX enum_idx, PLL_CTRL_POLICY *tab,
			      uint32_t length)
{
	for (; length; length--, tab++) {
		if (tab->pll_idx == enum_idx)
			return &tab->cfg;
	}
	return NULL;
}

/*
 * Points the PLL nodes of the clock tree at their pll_dev[] entries (VCO
 * outputs) or pll_post[] entries (post-divider outputs); cfg_idx is the
 * index in the table the entry was found in.
 */
void pll_fill_config(CLK_TREE_STRUCT *pSt, uint32_t length)
{
	uint32_t i;

	for (i = 0; i < CLK_TREE_MAX_IDX && i < length; i++, pSt++) {
		PLL_CTRL_POLICY *first, *second;
		uint32_t n_first, n_second;

		if (pSt->type == CLK_TYPE_PLL || pSt->type == CLK_TYPE_PLL_VCO) {
			first = pll_dev;
			n_first = ARRAY_SIZE(pll_dev);
			second = pll_post;
			n_second = ARRAY_SIZE(pll_post);
		} else if (pSt->type == CLK_TYPE_PLL_POSTDIV) {
			first = pll_post;
			n_first = ARRAY_SIZE(pll_post);
			second = pll_dev;
			n_second = ARRAY_SIZE(pll_dev);
		} else {
			continue;
		}
		pSt->pll = find_cfg((CLK_TREE_ENUM_IDX)i, first, n_first);
		if (pSt->pll == NULL) {
			first = second;
			pSt->pll = find_cfg((CLK_TREE_ENUM_IDX)i, first, n_second);
		}
		if (pSt->pll == NULL) {
			clk_assert_failed(pSt->type == CLK_TYPE_PLL_POSTDIV ? 0xf1a71738 : 0xf1a71725);
			continue;
		}
		pSt->cfg_idx = ((uintptr_t)pSt->pll - (uintptr_t)first) / sizeof(PLL_CTRL_POLICY);
	}
}

/*
 * STR suspend: the dividers of every PLL are read back as its targets and
 * every PLL is put in STATIC_CFG (it is off across S3); pll_policy_restore
 * walks each back to the state it was in, which writes them again.
 */
void pll_policy_save(void)
{
	uint32_t i;

	if (!g_enable_pll_policy)
		return;
	_s_is_str_save_restore_context = true;
	for (i = 0; i < PLL_NUM; i++) {
		PLL_CTRL_POLICY *p = &pll_dev[i];
		uint32_t base = p->cfg.pll_base;

		if (base) {
			uint32_t p3;

			p->target_FBDIV = REG32(base + PLL_P1) & P1_FBDIV;
			p->target_frac = REG32(base + PLL_P2) & P2_FRAC;
			p3 = REG32(base + PLL_P3);
			p->target_REFDIV = (p3 >> 8) & 0x3f;
			p->target_POSTDIV1 = p3 & 7;
			p->target_POSTDIV2 = (p3 >> 4) & 7;
		}
		p->saved = p->pll_st;
		p->pll_st = PLL_HSM_STATIC_CFG;
	}
}

void pll_policy_restore(void)
{
	uint32_t i;

	if (!g_enable_pll_policy)
		return;
	for (i = 0; i < PLL_NUM; i++) {
		PLL_POLICY_STATE st = pll_state_transition(i, pll_dev[i].saved);

		CLK_ASSERT(st == pll_dev[i].saved, 0xf1a71957);
	}
	_s_is_str_save_restore_context = false;
}

/* --- transitions ------------------------------------------------------------ */

/*
 * Walks the state tree from the current state to target_state, exiting and
 * entering states on the way; a handler that fails stops the walk, which
 * is then retried along the same path until the target is reached or the
 * walk fails at once.  Returns the state reached.
 */
static PLL_POLICY_STATE pll_state_transition(uint32_t local_idx,
					     PLL_POLICY_STATE target_state)
{
	PLL_POLICY_STATE st = get_state(local_idx);
	TREE_NODE *from = &pll_hsm[st], *to = &pll_hsm[target_state];

	if (st == target_state)
		return st;
	while (get_state(local_idx) != target_state &&
	       tree_traverse_path(from, to, (void *)(uintptr_t)local_idx,
				  _hsm_exit_node, _hsm_enter_node, NULL))
		;
	return get_state(local_idx);
}

static PLL_POLICY_STATE get_state(uint32_t local_idx)
{
	CLK_ASSERT(local_idx < PLL_NUM, 0xf1a71618);
	return pll_dev[local_idx].pll_st;
}

/*
 * A PLL's output rate.  For a post-divider output only the post divider
 * changes (MMHUB's output gated meanwhile); for a PLL, see _pll_set_freq.
 */
uint32_t pll_policy_set_freq(uint32_t desired, CLK_TREE_ENUM_IDX enum_idx)
{
	CLK_TREE_STRUCT *pSt = clk_node(enum_idx);

	if (pSt->type == CLK_TYPE_PLL_POSTDIV) {
		uint32_t ps = 0, gate = 0;

		if (enum_idx == CLK_TREE_MMHUB_PLL) {
			uint32_t v;

			ps = clk_lock();
			v = REG32(MMHUB_POSTDIV_GATE);
			if (v & BIT(4))
				REG32(MMHUB_POSTDIV_GATE) = v & ~BIT(4);
			gate = v & BIT(4);
		}
		pll_ctrl_set_FoutPOSTDIV(desired, pll_ptr(pSt->pll));
		if (gate)
			REG32(MMHUB_POSTDIV_GATE) |= gate;
		if (enum_idx == CLK_TREE_MMHUB_PLL)
			clk_unlock(ps);
	} else if (pSt->type == CLK_TYPE_PLL) {
		_pll_set_freq(0, desired, pSt);
	} else {
		clk_assert_failed(0xf1a72003);
	}
	return pll_ctrl_get_postdiv_freq(pll_ptr(pSt->pll));
}

/* Online stepping only moves FBDIV/FRAC: REFDIV and POSTDIVs must stay */
static bool same_ref_and_postdiv(uint32_t base, const PLL_CTRL_POLICY *p)
{
	uint32_t p3 = REG32(base + PLL_P3);

	return (p3 & 7) == p->target_POSTDIV1 && ((p3 >> 4) & 7) == p->target_POSTDIV2 &&
	       ((p3 >> 8) & 0x3f) == p->target_REFDIV;
}

/*
 * Solves the dividers for a VCO rate, an output rate or both (0: not
 * given), records them as the PLL's targets and applies them: a PLL that
 * is off only moves to STATIC_CFG (the dividers are written when it is
 * switched on); a running one is stepped on the fly, stepped offline or
 * switched off and on again (see the top of the file).  The DSU and CI-700
 * PLLs run the whole change with interrupts off and a bit of theirs
 * cleared.
 */
static void _pll_set_freq(uint64_t target_vco, uint32_t target_freq, CLK_TREE_STRUCT *pSt)
{
	uint32_t loc = pSt->cfg_idx;
	PLL_CTRL_POLICY *p = &pll_dev[loc];
	uint32_t base = p->cfg.pll_base;
	uint32_t FBDIV = 0, frac = 0, REFDIV = 0, PSTDIV1 = 0, PSTDIV2 = 0, err;
	uint32_t gate_reg = 0, gate = 0, ps = 0;
	bool irq_off = false;
	PLL_POLICY_STATE st;

	if (loc == PLL_LOC_DSU || loc == PLL_LOC_CI700) {
		uint32_t v;

		gate_reg = loc == PLL_LOC_DSU ? DSU_PLL_GATE : CI700_PLL_GATE;
		ps = clk_lock();
		irq_off = true;
		v = REG32(gate_reg);
		if (v & BIT(0))
			REG32(gate_reg) = v & ~BIT(0);
		gate = v & BIT(0);
	}

	if (target_vco && target_freq) {
		err = pll_solve_vco_settings(false, target_vco, &FBDIV, &frac, &REFDIV);
		CLK_ASSERT(err < PLL_SOLVE_MAX_ERR, 0xf1a70662);
		err = pll_solve_postdiv_settings(target_vco, target_freq, &PSTDIV1, &PSTDIV2);
		CLK_ASSERT(err < PLL_SOLVE_MAX_ERR, 0xf1a70664);
		p->freq_vco = target_vco;
		p->freq_fout = target_freq;
	} else if (target_freq) {
		err = pll_solve_freq_settings(false, target_freq, &FBDIV, &frac, &REFDIV,
					      &PSTDIV1, &PSTDIV2);
		CLK_ASSERT(err < PLL_SOLVE_MAX_ERR, 0xf1a70674);
		p->freq_fout = target_freq;
	} else if (target_vco) {
		err = pll_solve_vco_settings(false, target_vco, &FBDIV, &frac, &REFDIV);
		CLK_ASSERT(err < PLL_SOLVE_MAX_ERR, 0xf1a70679);
		p->freq_vco = target_vco;
	}
	if (FBDIV) {
		p->target_FBDIV = FBDIV;
		p->target_frac = frac;
		p->target_REFDIV = REFDIV;
	}
	if (PSTDIV1) {
		p->target_POSTDIV1 = PSTDIV1;
		p->target_POSTDIV2 = PSTDIV2;
	}

	st = get_state(loc);
	if (st < PLL_HSM_PLL_ENABLE) {
		st = pll_state_transition(loc, PLL_HSM_STATIC_CFG);
		CLK_ASSERT(st == PLL_HSM_STATIC_CFG, 0xf1a71783);
	} else if (st >= PLL_HSM_SUBTREE_ATTACHED && p->cfg.has_seqcr &&
		   (REG32(base + PLL_SEQ_HW_SEL) & 1) &&
		   (bitmap_allow_ONLINE_STEPPING & BIT(loc)) &&
		   same_ref_and_postdiv(base, p)) {
		st = pll_state_transition(loc, PLL_HSM_TUNE_ONTHEFLY);
		CLK_ASSERT(st == PLL_HSM_TUNE_ONTHEFLY, 0xf1a71870);
		st = pll_state_transition(loc, PLL_HSM_STABLE_RUNNING);
		CLK_ASSERT(st == PLL_HSM_STABLE_RUNNING, 0xf1a71873);
	} else if (bitmap_allow_OFFLINE_STEPPING & BIT(loc)) {
		st = pll_state_transition(loc, PLL_HSM_NEW_FREQ);
		CLK_ASSERT(st == PLL_HSM_NEW_FREQ, 0xf1a71897);
		st = pll_state_transition(loc, PLL_HSM_STABLE_RUNNING);
		CLK_ASSERT(st == PLL_HSM_STABLE_RUNNING, 0xf1a71900);
	} else {
		st = pll_state_transition(loc, PLL_HSM_STATIC_CFG);
		CLK_ASSERT(st == PLL_HSM_STATIC_CFG, 0xf1a71903);
		st = pll_state_transition(loc, PLL_HSM_STABLE_RUNNING);
		CLK_ASSERT(st == PLL_HSM_STABLE_RUNNING, 0xf1a71906);
	}

	if (gate)
		REG32(gate_reg) |= gate;
	if (irq_off)
		clk_unlock(ps);
}

uint64_t pll_policy_set_FoutVCO(uint64_t desired_vco_freq, CLK_TREE_ENUM_IDX enum_idx)
{
	CLK_TREE_STRUCT *pSt = clk_node(enum_idx);

	CLK_ASSERT(pSt->type == CLK_TYPE_PLL || pSt->type == CLK_TYPE_PLL_VCO, 0xf1a72013);
	_pll_set_freq(desired_vco_freq, 0, pSt);
	return pll_ctrl_get_vco_freq(pll_ptr(pSt->pll));
}

uint32_t pll_policy_set_FoutPOSTDIV(uint32_t desired_postdiv_freq, CLK_TREE_ENUM_IDX enum_idx)
{
	return pll_policy_set_freq(desired_postdiv_freq, enum_idx);
}

/* On: STABLE_RUNNING; off: STATIC_CFG */
bool pll_policy_set_state(bool enable, CLK_TREE_ENUM_IDX enum_idx)
{
	CLK_TREE_STRUCT *pSt = clk_node(enum_idx);
	PLL_POLICY_STATE target = enable ? PLL_HSM_STABLE_RUNNING : PLL_HSM_STATIC_CFG;

	CLK_ASSERT(pSt->type == CLK_TYPE_PLL || pSt->type == CLK_TYPE_PLL_VCO, 0xf1a72028);
	return pll_state_transition(pSt->cfg_idx, target) == target;
}

/* CIX takes and gives the PLL's mutex: single threaded, nothing to take */
void pll_policy_lock_entry(uint32_t pll_idx)
{
	CLK_ASSERT(pll_idx < PLL_NUM, 0xf1a72044);
}

void pll_policy_unlock_entry(uint32_t pll_idx)
{
	CLK_ASSERT(pll_idx < PLL_NUM, 0xf1a72052);
}

uint32_t pll_policy_num_to_base(uint32_t pll_num)
{
	if (pll_num >= PLL_NUM)
		return 0;
	return pll_dev[pll_num].cfg.pll_base;
}

uint32_t pll_policy_num_to_enum_idx(uint32_t pll_num)
{
	if (pll_num >= PLL_NUM)
		return 0;
	return pll_dev[pll_num].pll_idx;
}

const char *pll_policy_num_to_name(uint32_t pll_num)
{
	if (pll_num >= PLL_NUM)
		return "<null>";
	return clk_node(pll_dev[pll_num].pll_idx)->name;
}

/* --- the states' handlers ----------------------------------------------------- */

/*
 * PLL_ENABLE: spread spectrum out of reset if allowed, then the PLL on, at
 * least PLL_OFF_CYCLES after it was switched off.
 */
static bool pll_enable_enter(uint32_t loc_idx)
{
	PLL_CTRL_POLICY *p = &pll_dev[loc_idx];
	uint32_t base = p->cfg.pll_base;

	if (ssmod_allowed(p, loc_idx))
		REG32(base + PLL_SSMOD_REG) &= ~(SSMOD_RESET | SSMOD_DISABLE_SSCG);
	if (p->ts) {
		while (pll_timestamp() <= p->ts + PLL_OFF_CYCLES)
			;
	}
	pll_ctrl_set_state(1, pll_ptr(&p->cfg));
	p->ts = pll_timestamp();
	return true;
}

/* Spread spectrum back in reset and the PLL off (if it has an enable) */
static bool pll_enable_exit(uint32_t loc_idx)
{
	PLL_CTRL_POLICY *p = &pll_dev[loc_idx];
	uint32_t base = p->cfg.pll_base;

	if (p->cfg.no_pllen)
		return true;
	if (!p->cfg.no_ssmod)
		REG32(base + PLL_SSMOD_REG) |= SSMOD_RESET | SSMOD_DISABLE_SSCG;
	pll_ctrl_set_state(0, pll_ptr(&p->cfg));
	p->ts = pll_timestamp();
	return true;
}

/* NEW_FREQ: the target dividers written with the PLL running */
static bool apply_new_freq_enter(uint32_t loc_idx)
{
	PLL_CTRL_POLICY *p = &pll_dev[loc_idx];
	uint32_t base = p->cfg.pll_base;
	uint32_t p1 = REG32(base + PLL_P1), p2 = REG32(base + PLL_P2), p3 = REG32(base + PLL_P3);

	if (p->target_FBDIV) {
		p3 = (p3 & ~P3_REFDIV) | (p->target_REFDIV & 0x3f) << 8;
		REG32(base + PLL_P1) = (p1 & ~P1_FBDIV) | (p->target_FBDIV & P1_FBDIV);
		REG32(base + PLL_P2) = (p2 & ~P2_FRAC) | (p->target_frac & P2_FRAC);
		REG32(base + PLL_P3) = p3;
	}
	if (p->target_POSTDIV1)
		REG32(base + PLL_P3) = (p3 & ~P3_POSTDIV) | (p->target_POSTDIV1 & 7) |
				       (p->target_POSTDIV2 & 7) << 4;
	return true;
}

static bool apply_new_freq_exit(uint32_t loc_idx)
{
	(void)loc_idx;
	return true;
}

/*
 * CONFIRMED_LOCK: waits for the lock bit (see the top of the file: CIX
 * goes on whether it locked or not), takes the modulator's pointer out of
 * reset if spread spectrum is allowed, turns output retiming on and, 134
 * cycles later, hands FBDIV to the sequencer if online stepping is
 * allowed.
 */
static bool confirm_lock_enter(uint32_t loc_idx)
{
	PLL_CTRL_POLICY *p = &pll_dev[loc_idx];
	uint32_t base = p->cfg.pll_base;

	if (!pll_ctrl_is_lock(pll_ptr(&p->cfg))) {
		uint32_t timeout_us = PLL_LOCK_TIMEOUT_US;

		/* CIX's busy-wait: the time since the PLL was switched on */
		if (_s_is_str_save_restore_context && p->ts) {
			uint32_t us = (pll_timestamp() - p->ts) / (core_hz / 1000000);

			timeout_us = us < PLL_LOCK_TIMEOUT_US ? us : PLL_LOCK_TIMEOUT_US;
			if (timeout_us < PLL_LOCK_MIN_US)
				timeout_us = PLL_LOCK_MIN_US;
		}
		pll_ctrl_wait_for_lock(pll_ptr(&p->cfg), timeout_us);
	}
	p->ts = 0;
	if (ssmod_allowed(p, loc_idx))
		REG32(base + PLL_SSMOD_REG) &= ~SSMOD_RESETPTR;
	REG32(base + PLL_CTRL_REG) |= CTRL_EN_RETIMING;
	cc_delay(134);
	if (p->cfg.has_seqcr && (bitmap_allow_ONLINE_STEPPING & BIT(loc_idx)))
		REG32(base + PLL_SEQ_HW_SEL) |= 1;
	return true;
}

/* The reverse, 89 cycles settling */
static bool confirm_lock_exit(uint32_t loc_idx)
{
	PLL_CTRL_POLICY *p = &pll_dev[loc_idx];
	uint32_t base = p->cfg.pll_base;

	if (ssmod_allowed(p, loc_idx))
		REG32(base + PLL_SSMOD_REG) |= SSMOD_RESETPTR;
	REG32(base + PLL_CTRL_REG) &= ~CTRL_EN_RETIMING;
	if (p->cfg.has_seqcr)
		REG32(base + PLL_SEQ_HW_SEL) &= ~1u;
	cc_delay(89);
	return true;
}

/*
 * SUBTREE_ATTACHED: the PLL's mux goes back to its parent (the PLL).  On
 * glitch-detecting PLLs with clock ramping allowed, a pending ramp request
 * is waited out (1600 cycles at most) and the switch ramps the clock up
 * through the PSC.
 */
static bool subtree_attach_enter(uint32_t loc_idx)
{
	PLL_CTRL_POLICY *p = &pll_dev[loc_idx];
	uint32_t psc = 0, i;
	CLK_TREE_STRUCT *mux;

	if (p->cfg.has_glichdet && (bitmap_allow_GLICD & BIT(loc_idx))) {
		uint32_t base = p->cfg.pll_base;

		if (REG32(base + PLL_PSC1) & PSC1_RAMP_REQ)
			(void)pull_register(base + PLL_PSC1, PSC1_RAMP_REQ, 0, 1600);
		REG32(base + PLL_PSC2) = PSC2_CIX;
		psc = base + PLL_PSC1;
	}
	if (p->mux_idx == CLK_TREE_ROOT)
		return true;
	mux = clk_node(p->mux_idx);
	CLK_ASSERT(mux->type == CLK_TYPE_MUX, 0xf1a70978);
	for (i = 0; i < 6; i++) {
		if (mux->mux.src_clk[i] == mux->parent_idx) {
			bool ok = clk_tree_mux_switch_with_psc_ramp_up(&mux->clk_tree_node, i, psc);

			CLK_ASSERT(ok, 0xf1a70982);
			break;
		}
	}
	return true;
}

/*
 * Leaving SUBTREE_ATTACHED: the PLL's mux moves to another source while
 * the PLL changes.  The GPU PLLs use source 2 (GPUCORE's ramp request is
 * cleared first); otherwise, or if source 2 has no rate, the fastest
 * allowed source that is not the parent and not faster than the PLL's
 * output (5 Hz slack).
 */
static bool subtree_attach_exit(uint32_t loc_idx)
{
	PLL_CTRL_POLICY *p = &pll_dev[loc_idx];
	uint32_t base = p->cfg.pll_base, cur, best = 0, sel = 6, i;
	CLK_TREE_STRUCT *pll, *mux;

	if (p->mux_idx == CLK_TREE_ROOT)
		return true;
	pll = clk_node(p->pll_idx);
	CLK_ASSERT(pll->type == CLK_TYPE_PLL, 0xf1a71003);
	cur = pll_ctrl_get_postdiv_freq(pll_ptr(&p->cfg));
	mux = clk_node(p->mux_idx);
	CLK_ASSERT(mux->type == CLK_TYPE_MUX, 0xf1a71009);
	if (loc_idx == PLL_LOC_GPUTOP || loc_idx == PLL_LOC_GPUCORE) {
		CLK_ASSERT(p->mux_idx == CLK_TREE_GPUTOP_PLL_MUX ||
			   p->mux_idx == CLK_TREE_GPUCORE_PLL_MUX, 0xf1a71022);
		if (loc_idx == PLL_LOC_GPUCORE)
			REG32(base + PLL_PSC1) = 0;
		best = clk_tree_get_freq(&clk_node(mux->mux.src_clk[2])->clk_tree_node);
		if (best)
			sel = 2;
	}
	if (sel == 6) {
		for (i = 0; i < 6; i++) {
			uint32_t src = mux->mux.src_clk[i], f;

			if (!(mux->mux.src_bitmap & BIT(i)) || src == mux->parent_idx)
				continue;
			f = clk_tree_get_freq(&clk_node(src)->clk_tree_node);
			if (f > best && (f <= cur || f - cur < 5)) {
				sel = i;
				best = f;
			}
		}
		if (sel == 6) {
			clk_assert_failed(0xf1a71066);
			return false;
		}
	}
	if (!clk_tree_mux_switch(&mux->clk_tree_node, sel))
		clk_assert_failed(0xf1a71062);
	return true;
}

/*
 * TUNE_ONTHEFLY: the sequencer steps FBDIV from its current value to the
 * target in steps of 2 (max SM count 4) and FRAC to its target; the
 * dividers are written with the targets too.  The steps take 80 cycles
 * each: ts is when they are done.
 */
static bool stepping_on_the_fly_enter(uint32_t loc_idx)
{
	PLL_CTRL_POLICY *p = &pll_dev[loc_idx];
	uint32_t base = p->cfg.pll_base;
	uint32_t p1 = REG32(base + PLL_P1), p2 = REG32(base + PLL_P2);
	uint32_t sm = REG32(base + p->offset_seqcr_ms_cnt), cur, diff, steps;

	CLK_ASSERT(p->offset_seqcr_ms_cnt != 0, 0xf1a70733);
	cur = p1 & P1_FBDIV;
	REG32(base + PLL_SEQ_START_FBDIV) = cur;
	REG32(base + PLL_SEQ_STEP_SIZE) = SEQ_STEP;
	diff = cur < p->target_FBDIV ? p->target_FBDIV - cur : cur - p->target_FBDIV;
	steps = diff / SEQ_STEP + 1;
	REG32(base + PLL_SEQ_STEP_COUNT) = steps;
	REG32(base + PLL_SEQ_TARGET_FBDIV) = p->target_FBDIV;
	REG32(base + p->offset_seqcr_ms_cnt) = (sm & ~SEQ_MAX_SM_CNT) | SEQ_SM_CNT;
	REG32(base + PLL_SEQ_TARGET_FRAC) = p->target_frac;
	REG32(base + PLL_P1) = (p1 & ~P1_FBDIV) | (p->target_FBDIV & P1_FBDIV);
	REG32(base + PLL_P2) = (p2 & ~P2_FRAC) | (p->target_frac & P2_FRAC);
	p->ts = pll_timestamp() + steps * SEQ_STEP_CYCLES;
	return true;
}

/* Waits for the sequencer's steps */
static bool stepping_on_the_fly_exit(uint32_t loc_idx)
{
	PLL_CTRL_POLICY *p = &pll_dev[loc_idx];

	while (pll_timestamp() <= p->ts)
		cc_delay(30);
	p->ts = 0;
	return true;
}

/* CIX reads PSC1 here (glitch detection allowed) only to log it */
static bool stable_running_enter(uint32_t loc_idx)
{
	(void)loc_idx;
	return true;
}

static bool stable_running_exit(uint32_t loc_idx)
{
	(void)loc_idx;
	return true;
}

/* --- walking the state tree ------------------------------------------------- */

/* tree_traverse_path's visit_up: exit the state, now in its parent */
static bool _hsm_exit_node(TREE_NODE *node, void *data)
{
	const PLL_POLICY_HSM_NODE *ctrl = node->node_data;
	uint32_t loc_idx = (uintptr_t)data;

	if (ctrl->exit && !ctrl->exit(loc_idx))
		return false;
	CLK_ASSERT(node->parent != NULL, 0xf1a71649);
	set_state(loc_idx, node_to_state(node->parent));
	return true;
}

static PLL_POLICY_STATE node_to_state(TREE_NODE *node)
{
	uint32_t st = (const PLL_POLICY_HSM_NODE *)node->node_data - hsm_ctrl_nodes;

	CLK_ASSERT(st < PLL_HSM_MAX, 0xf1a71633);
	return (PLL_POLICY_STATE)st;
}

static void set_state(uint32_t local_idx, PLL_POLICY_STATE new_st)
{
	CLK_ASSERT(local_idx < PLL_NUM, 0xf1a71624);
	pll_dev[local_idx].pll_st = new_st;
}

/* tree_traverse_path's visit_down: enter the state */
static bool _hsm_enter_node(TREE_NODE *node, void *data)
{
	const PLL_POLICY_HSM_NODE *ctrl = node->node_data;
	uint32_t loc_idx = (uintptr_t)data;

	if (ctrl->enter && !ctrl->enter(loc_idx))
		return false;
	set_state(loc_idx, node_to_state(node));
	return true;
}
