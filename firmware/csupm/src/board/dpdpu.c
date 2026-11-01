/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Whether DRAM may change frequency (DFS) as far as the display allows
 * (CIX's Source/common/soc/dpdpu/dpdpu.c).
 *
 * The SoC idle switch (CIX's DPM runtime) lowers the DDR frequency when
 * the SoC idles; each of the blocks that stream from DRAM must allow it:
 * the display (here), the VPU, ISP, GPU and NPU (their drivers set theirs).
 * When the switch is turned on the display is checked 200 ms later: any
 * active port of the five DPUs (two each) makes it incompatible; the one
 * with the highest pixel rate is the port DFS would align its switch with.
 */
#include <mod_pm_svc.h>

#include "board/board.h"
#include "clk/clk.h"
#include "clk/clk_cix.h"

#define DPU_N			5
#define DPU_PORTS		2
#define DFS_CHECK_US		200000u
#define DPU_PWR			0x56000400u	/* S5: bit 16 + DPU, on */
#define DPU_CTRL_PWR		0x210		/* RCSU: bit 1, the controller powered */
#define DPU_PORT(dpu, p)	(dpu_rcsu_base[dpu] + 0x2000 * (p))
#define DPU_TIMING0		0x11ee0		/* [28:16] active lines, [12:0] active pixels */
#define DPU_TIMING1		0x11ee4
#define DPU_TIMING2		0x11ee8
#define DPU_TIMING3		0x11eec
#define DPU_DOU_STATUS		0x118b0		/* [13:12] */
#define CLK_DPU_PIXEL(p)	(167 + (p))	/* the ports' pixel clocks */
#define MIN_PIXELS		(225u << 14)	/* 3686400: below 2560x1440 */
#define MIN_FRAME_RATE		60.8f

static const uint32_t dpu_rcsu_base[DPU_N] = {
	0x54000000, 0x54070000, 0x540e0000, 0x54150000, 0x541c0000,
};

/* CIX's is_ddr_dfs_compatible (all but the display allow DFS until told
 * otherwise): DPU, VPU, ISP, GPU, NPU; the flag (on) that
 * aligns DFS with the busiest port, that port (0xff none), the timer */
static bool is_ddr_dfs_compatible[5] = { false, true, true, true, true };
static bool align_flag = true;
static uint8_t highest_port = 0xff;
static uint8_t dfs_timer = PM_SVC_TIMER_NONE;

bool dpdpu_check_dpu_ctrl_power_state(uint32_t dpu_id)
{
	return (SocRegRead32(dpu_rcsu_base[dpu_id] + DPU_CTRL_PWR) & 2) >> 1;
}

bool dpdpu_check_reset_and_power(uint32_t dpu_id)
{
	if (!(REG32(DPU_PWR) & BIT(16 + dpu_id)))
		return false;
	return dpdpu_check_dpu_ctrl_power_state(dpu_id);
}

/*
 * CIX's dpdpu_get_port_frame_info: from the port's pixel clock and its
 * four timing registers, the active size and the frame rate; nothing for
 * a port whose clock is off or whose rate is above 299 Hz.  Each total is
 * the sum of four fields, as CIX adds them (and a zero total, which CIX
 * would divide by, is no frame).
 */
bool dpdpu_get_port_frame_info(uint32_t port_id, uint32_t *v_active, uint32_t *h_active,
			       float *frame_rate)
{
	CLK_TREE_STRUCT *p;
	uint32_t freq, base, r0, r1, r2, r3, v_total, h_total, rate;

	if (port_id > 10)
		return false;
	p = clk_nodes_get((CLK_TREE_ENUM_IDX)((CLK_DPU_PIXEL(port_id)) & 0xffff));
	if (!p || !(freq = clk_tree_get_freq(&p->clk_tree_node)))
		return false;
	base = dpu_rcsu_base[port_id >> 1] + 0x2000 * (port_id & 1);
	r0 = SocRegRead32(base + DPU_TIMING0);
	r1 = SocRegRead32(base + DPU_TIMING1);
	r2 = SocRegRead32(base + DPU_TIMING2);
	r3 = SocRegRead32(base + DPU_TIMING3);
	v_total = (r2 & 0x3fff) + ((r0 >> 16) & 0x1fff) + ((r2 >> 16) & 0xff) + ((r3 >> 16) & 0xff);
	h_total = (r1 & 0xfff) + (r0 & 0x1fff) + ((r1 >> 16) & 0x3ff) + (r3 & 0x3ff);
	if (!v_total || !h_total)
		return false;
	rate = freq / (v_total * h_total);
	if (rate > 299 || freq < v_total * h_total)
		return false;
	if (v_active)
		*v_active = (r0 >> 16) & 0x1fff;
	if (h_active)
		*h_active = r0 & 0x1fff;
	if (frame_rate)
		*frame_rate = (float)rate;
	return true;
}

/*
 * CIX's examine_ddr_dfs_compatible: 200 ms after the switch went on.  A
 * port counts when its rate is above 60.8 Hz or it has 2560x1440 pixels or
 * more.  CIX's oddity kept: the busiest port is found but only stored when
 * there is none (0xff), so aligning with it never waits.
 */
static void examine_ddr_dfs_compatible(void *arg)
{
	uint32_t active = 0, best = 0;
	uint8_t port = 0xff;

	(void)arg;
	for (uint32_t dpu = 0; dpu < DPU_N; dpu++) {
		if (!dpdpu_check_reset_and_power(dpu))
			continue;
		for (uint32_t k = 0; k < DPU_PORTS; k++) {
			uint32_t v = 0, h = 0, load;
			float fps = 0;

			if (!dpdpu_get_port_frame_info(2 * dpu + k, &v, &h, &fps))
				continue;
			if (!(fps > MIN_FRAME_RATE) && v * h < MIN_PIXELS)
				continue;
			load = h * (uint32_t)fps * v;
			if (load > best)
				port = (uint8_t)(2 * dpu + k);
			best = load > best ? load : best;
			active++;
		}
	}
	if (!active)
		highest_port = port;
	is_ddr_dfs_compatible[0] = active == 0;
}

/* CIX's dpdpu_trigger_ddr_dfs_compatible_checker: the display counts as
 * incompatible until the check */
void dpdpu_trigger_ddr_dfs_compatible_checker(void)
{
	is_ddr_dfs_compatible[0] = false;
	highest_port = 0xff;
	if (dfs_timer == PM_SVC_TIMER_NONE)
		dfs_timer = pm_svc_timer_alloc(DFS_CHECK_US, examine_ddr_dfs_compatible, NULL,
					       false, false);
	pm_svc_timer_disable(dfs_timer);
	pm_svc_timer_enable(dfs_timer);
}

bool ddr_dfs_check_compatible(void)
{
	for (unsigned i = 0; i < 5; i++) {
		if (!is_ddr_dfs_compatible[i])
			return false;
	}
	return true;
}

/* a bit per block that allows it */
uint8_t ddr_dfs_get_compatible_status(void)
{
	uint8_t v = 0;

	for (unsigned i = 0; i < 5; i++)
		v |= (uint8_t)(is_ddr_dfs_compatible[i] << i);
	return v;
}

void vpu_ddr_dfs_set_compatible(bool is_compatible)
{
	is_ddr_dfs_compatible[1] = is_compatible;
}

void isp_ddr_dfs_set_compatible(bool is_compatible)
{
	is_ddr_dfs_compatible[2] = is_compatible;
}

void gpu_ddr_dfs_set_compatible(bool is_compatible)
{
	is_ddr_dfs_compatible[3] = is_compatible;
}

void npu_ddr_dfs_set_compatible(bool is_compatible)
{
	is_ddr_dfs_compatible[4] = is_compatible;
}

void dpdpu_align_to_highest_resolution_port(bool flag)
{
	align_flag = flag;
}

/* CIX's dpdpu_wait_for_dou_status: the port's DOU state [13:12] going
 * from state_from to state_to (seen twice in a row), within timeout_cc */
static bool dpdpu_wait_for_dou_status(uint32_t port_id, uint32_t state_from, uint32_t state_to,
				      uint32_t timeout_cc)
{
	uint32_t reg, t0, prev, now;

	if (!dpdpu_check_reset_and_power(port_id >> 1))
		return true;
	reg = DPU_PORT(port_id >> 1, port_id & 1) + DPU_DOU_STATUS;
	t0 = get_c_count();
	prev = (SocRegRead32(reg) >> 12) & 3;
	for (;;) {
		uint32_t t = get_c_count();

		now = (SocRegRead32(reg) >> 12) & 3;
		if (prev == state_from && now == state_to &&
		    ((SocRegRead32(reg) >> 12) & 3) == state_to)
			return true;
		udelay_cycles(30);
		if (t - t0 > timeout_cc - 1)
			return false;
		prev = now;
	}
}

/* CIX's dpdpu_align_with_highest_resolution_port: wait for the busiest
 * port's frame to end (state 3 to 1) */
bool dpdpu_align_with_highest_resolution_port(uint32_t timeout_cc)
{
	if (!align_flag || highest_port >= 10)
		return true;
	return dpdpu_wait_for_dou_status(highest_port, 3, 1, timeout_cc);
}
