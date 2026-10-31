/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The state tree and its walk (CIX's Source/common/sshsm/pwr_hsm.c with the
 * tree_traverse_path it calls from u_tree).  CIX's hsm_node table is a
 * generic tree of nodes; here the parent of each state is a table.  The
 * current state is pm_ss_state (src/clk/clk_port.c, CIX's current_state),
 * which CIX's clock code reads through ss_hsm_get.
 */
#include "hsm/hsm.h"

static volatile SS_HSM_STATE target_state = HSM_ST_RUN;

/* CIX's root_cfg, init_cfg, run_cfg, pts_cfg, s3_cfg, s5_cfg */
static const struct {
	SS_HSM_STATE parent;
	void (*enter)(void);
	void (*exit)(void);
} node[HMS_ST_MAX] = {
	[HSM_ST_POR] = { HMS_ST_MAX, NULL, NULL },	/* ss_por_enter runs from main only */
	[HSM_ST_INIT] = { HSM_ST_POR, ss_init, ss_init_exit },
	[HSM_ST_RUN] = { HSM_ST_INIT, ss_run, ss_run_exit },
	[HSM_ST_PTS] = { HSM_ST_INIT, ss_pts_enter, ss_pts_exit },
	[HSM_ST_S3] = { HSM_ST_PTS, ss_s3_enter, ss_s3_exit },
	[HSM_ST_S5] = { HSM_ST_PTS, ss_s5_enter, ss_s5_exit },
};

static unsigned depth(SS_HSM_STATE s)
{
	unsigned d = 0;

	while (node[s].parent != HMS_ST_MAX) {
		s = node[s].parent;
		d++;
	}
	return d;
}

/* CIX's ss_hsm_init: the walk starts at power-on */
void ss_hsm_init(void)
{
	pm_ss_state = HSM_ST_POR;
	target_state = HSM_ST_POR;
}

void ss_hsm_set(SS_HSM_STATE state)
{
	target_state = state;
}

/*
 * CIX's ss_hsm_run and tree_traverse_path: leave the states up to the
 * common parent (_hsm_move_up: the state's exit, then the parent is
 * current), then enter those down to the target (_hsm_move_down: the
 * state's enter, then it is current).  Returns the state reached.
 */
SS_HSM_STATE ss_hsm_run(void)
{
	while (pm_ss_state != target_state) {
		SS_HSM_STATE from = pm_ss_state, to = target_state, a = from, b = to;
		SS_HSM_STATE path[HMS_ST_MAX];
		unsigned n = 0;

		while (depth(a) > depth(b))
			a = node[a].parent;
		while (depth(b) > depth(a))
			b = node[b].parent;
		while (a != b) {
			a = node[a].parent;
			b = node[b].parent;
		}
		for (SS_HSM_STATE s = from; s != a; s = node[s].parent) {
			if (node[s].exit)
				node[s].exit();
			pm_ss_state = node[s].parent;
		}
		for (SS_HSM_STATE s = to; s != a; s = node[s].parent)
			path[n++] = s;
		while (n--) {
			if (node[path[n]].enter)
				node[path[n]].enter();
			pm_ss_state = path[n];
		}
	}
	return pm_ss_state;
}
