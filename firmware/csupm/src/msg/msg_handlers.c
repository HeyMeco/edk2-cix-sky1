/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * CIX's PM messages (the table _portX_msg of Source/projects/sky1/messages/
 * app_msginit.c) and their handlers: Source/common/messages/msg_test.c,
 * msg_sysctrl.c, Source/projects/sky1/messages/app_msg_plat.c and
 * app_msg_uefi_options.c, msg_clk.c's.  Who sends them:
 *
 *   0x00  channel test (CIX's tests)            0x50-0x52  SMN read, write, read-modify-write
 *   0x01  firmware version (UEFI)               0x60  platform information (UEFI options)
 *   0x02  deferred notification (tests)         0x61  SCMI clock reference count - 1 (UEFI)
 *   0x03  suspend (CIX's tests)                 0x62  the S3 boot script's table (UEFI, S3SaveStateDxe)
 *   0x04  performance streams (tools)           0x63  PMIC versions (UEFI, SetupManagerDxe)
 *   0x05  locked performance (Linux)            0x400 PMIC events (Linux, after the PM's notification)
 *   0x40  core online (Linux)                   0x800-0x802 fans (Linux; UEFI sends its fan table)
 *   0x41  core offline (Linux)                  0x803 NoC/DSU DVFS mode (Linux)
 *   0x42  the boot core, before suspend (TF-A)  0x804, 0x805 EDP voltages read, set (Linux)
 *
 * A handler gets the input length in words and sets the output's; it
 * returns the status code; the trace code it sets is CIX's (md_msg then
 * puts its own).
 */
#include "msg/msg.h"
#include "board/board.h"
#include "clk/clk.h"
#include "dpm/dvfs_cfg.h"
#include "fan/fan.h"
#include "hsm/hsm.h"
#include "log/log.h"
#include "perf/perf.h"
#include "pmic.h"
#include "s3script/s3script.h"
#include "soc.h"

#include <mod_locked_perf.h>
#include <mod_sky1soc_perf.h>

#define MSG_NOT_SUPPORTED	1u
#define MSG_INVALID		2u
#define MSG_NOT_FOUND		4u
#define MSG_FAILED		8u
#define MSG_BAD_LENGTH		10u

static void trace(uint32_t *t, uint32_t code)
{
	if (t)
		*t = code;
}

static void set_len(uint32_t *len, uint32_t n)
{
	if (len)
		*len = n;
}

/* --- msg_test.c ---------------------------------------------------------- */

/* the sum of the two words */
static uint32_t msg_channelTest(uint32_t port, uint32_t *len, void *in, void *out, uint32_t *t)
{
	const uint32_t *a = in;

	(void)port;
	if (!len || *len != 2) {
		set_len(len, 0);
		trace(t, 0xf18b0061);
		return MSG_BAD_LENGTH;
	}
	((uint32_t *)out)[0] = a[0] + a[1];
	*len = 1;
	trace(t, 0);
	return 0;
}

/*
 * The firmware's version: 0x00100002 (the message format), then the
 * build date as 0x20yymmdd and its time as 0x00hhmmss, from the version
 * strings 3 and 4 (src/log/md_fwver.c), digit by digit.
 */
static uint32_t msg_firmwareVersion(uint32_t port, uint32_t *len, void *in, void *out, uint32_t *t)
{
	const uint8_t *date, *time;
	uint32_t d = 0x20000000, h = 0, *o = out;

	(void)port;
	(void)in;
	if (!len || *len) {
		set_len(len, 0);
		trace(t, 0xf18b0109);
		return MSG_BAD_LENGTH;
	}
	date = md_version_stringPtr(3);
	time = md_version_stringPtr(4);
	for (int sh = 20, n = 6; n && date && time; n--, sh -= 4) {
		uint8_t a = *date++, b = *time++;

		if (!a || !b)
			break;
		d += (uint32_t)(a - '0') << sh;
		h += (uint32_t)(b - '0') << sh;
	}
	o[0] = 0x00100002;
	o[1] = d;
	o[2] = h;
	*len = 3;
	trace(t, 0);
	return 0;
}

/* counts notifications 1000-1019 (a test of deferred messages) */
static uint8_t testcnt[20];

static uint32_t msg_deferredNotify(uint32_t port, uint32_t *len, void *in, void *out, uint32_t *t)
{
	uint32_t n;

	(void)port;
	(void)out;
	if (!len || *len != 1) {
		set_len(len, 0);
		trace(t, 0xf18b0340);
		return MSG_BAD_LENGTH;
	}
	n = ((uint32_t *)in)[0] - 1000;
	if (n <= 19)
		testcnt[n]++;
	*len = 0;
	trace(t, 0);
	return 0;
}

/*
 * Performance streams for the AP's tools (CIX's msg_stream_act; src/perf):
 * in[0] the action: 0 a stream (in[1] its type, in[2] the ports or nodes,
 * in[3] the event, in[4] the interval in ticks, in[5] the CI-700's
 * cascading; out[0] its id), 1 release stream in[1], 2 all reset, 3 the
 * log flag in[1], 4 the log flag off.  CIX's oddity kept: the type must
 * have one bit set, so only the MM, PCIe and SMN hubs' PMU streams can be
 * asked for.
 */
static uint32_t msg_stream_act(uint32_t port, uint32_t *len, void *in, void *out, uint32_t *t)
{
	const uint32_t *a = in;
	uint32_t ret = 0, n = 0;

	(void)port;
	switch (a[0]) {
	case 0: {
		PERF_STREAM_CONFIG cfg = { .raw = { 0, 0 } };
		uint8_t id;

		if (a[1] >= 6 || u_misc_u32BitCount(a[1]) != 1) {
			ret = MSG_INVALID;
			break;
		}
		if ((uint8_t)(a[1] - 1) < 4) {
			cfg.ni700.port_mask = a[2];
			cfg.ni700.event_id = (uint8_t)a[3];
		} else if (a[1] == PERF_STREAM_CI700_PMU) {
			cfg.ci700.node_mask = a[2];
			cfg.ci700.event_encode = (uint8_t)a[3];
			cfg.ci700.cascade_mode = (uint16_t)a[5];
		}
		id = perf_stream_create((PERF_STREAM_TYPE)a[1], (uint16_t)a[4], &cfg);
		((uint32_t *)out)[0] = id;
		n = 1;
		ret = id == PERF_STREAM_INVALID ? MSG_FAILED : 0;
		break;
	}
	case 1:
		ret = perf_stream_release((uint8_t)a[1]) ? 0 : MSG_FAILED;
		break;
	case 2:
		perf_stream_reset();
		break;
	case 3:
		perf_stream_set_log_flag((uint8_t)a[1]);
		break;
	case 4:
		perf_stream_set_log_flag(0);
		break;
	default:
		ret = MSG_FAILED;
		break;
	}
	set_len(len, n);
	trace(t, ret ? 0xf18b0237 : 0);
	return ret;
}

/* the SMN window: SoC addresses below 0x20000000 */
static uint32_t msg_smn_read(uint32_t port, uint32_t *len, void *in, void *out, uint32_t *t)
{
	uint32_t addr = ((uint32_t *)in)[0], ret = MSG_INVALID;

	(void)port;
	if (addr <= 0x1fffffff) {
		((uint32_t *)out)[0] = REG32(addr + SOC_WINDOW);
		ret = 0;
	}
	set_len(len, 1);
	trace(t, ret ? 0xf18b0264 : 0);
	return ret;
}

static uint32_t msg_smn_write(uint32_t port, uint32_t *len, void *in, void *out, uint32_t *t)
{
	const uint32_t *a = in;
	uint32_t ret = MSG_INVALID;

	(void)port;
	(void)out;
	if (a[0] <= 0x1fffffff) {
		REG32(a[0] + SOC_WINDOW) = a[1];
		ret = 0;
	}
	set_len(len, 0);
	trace(t, ret ? 0xf18b0290 : 0);
	return ret;
}

static uint32_t msg_smn_rmw(uint32_t port, uint32_t *len, void *in, void *out, uint32_t *t)
{
	const uint32_t *a = in;
	uint32_t ret = MSG_INVALID;

	(void)port;
	(void)out;
	if (a[0] <= 0x1fffffff) {
		uint32_t v = REG32(a[0] + SOC_WINDOW);

		REG32(a[0] + SOC_WINDOW) = (v & ~a[2]) | (a[1] & a[2]);
		ret = 0;
	}
	set_len(len, 0);
	trace(t, ret ? 0xf18b0324 : 0);
	return ret;
}

/* a domain's level or limits locked: domain, level, minimum, maximum */
static uint32_t msg_locked_perf_confg(uint32_t port, uint32_t *len, void *in, void *out, uint32_t *t)
{
	const uint32_t *a = in;
	struct mod_locked_perf_domain d = { a[1], a[2], a[3] };
	bool ok = mod_locked_perf_set((uint8_t)a[0], &d);

	(void)port;
	(void)out;
	set_len(len, 0);
	trace(t, ok ? 0 : 0xf18b0398);
	return ok ? 0 : MSG_INVALID;
}

/* --- msg_sysctrl.c ------------------------------------------------------- */

static uint32_t msg_trigger_str(uint32_t port, uint32_t *len, void *in, void *out, uint32_t *t)
{
	(void)port;
	(void)in;
	(void)out;
	trigger_STR();
	set_len(len, 0);
	trace(t, 0);
	return 0;
}

static uint32_t cpu_change(uint32_t *len, void *in, uint32_t *t, bool online, uint32_t trc)
{
	bool ok = sky1soc_notify_ap_core_change(((uint8_t *)in)[0], online);

	set_len(len, 0);
	trace(t, trc);
	return ok ? 0 : MSG_FAILED;
}

static uint32_t msg_cpu_online(uint32_t port, uint32_t *len, void *in, void *out, uint32_t *t)
{
	(void)port;
	(void)out;
	return cpu_change(len, in, t, true, 0xf18c0111);
}

static uint32_t msg_cpu_offline(uint32_t port, uint32_t *len, void *in, void *out, uint32_t *t)
{
	(void)port;
	(void)out;
	return cpu_change(len, in, t, false, 0xf18c0138);
}

/* TF-A's STR_BOOT_CORE_NOTIFY: the core that suspends the system */
static uint32_t msg_cpu_bootcore_id(uint32_t port, uint32_t *len, void *in, void *out, uint32_t *t)
{
	uint32_t core = ((uint32_t *)in)[0];

	(void)port;
	(void)out;
	set_len(len, 0);
	if (core >= 12) {
		trace(t, 0xf18c0165);
		return MSG_FAILED;
	}
	boot_core_id = core;
	trace(t, 0);
	return 0;
}

/* UEFI's S3 boot script: its DRAM address and checksum, 64 bits each */
static uint32_t msg_register_s3script(uint32_t port, uint32_t *len, void *in, void *out, uint32_t *t)
{
	const uint32_t *a = in;
	bool ok = s3script_register_table(a[0], a[1], a[2], a[3]);

	(void)port;
	(void)out;
	set_len(len, 0);
	trace(t, 0xf18c0204);
	return ok ? 0 : MSG_FAILED;
}

/* the PMICs' revisions: how many, then four */
static uint32_t msg_pmic_get_ver(uint32_t port, uint32_t *len, void *in, void *out, uint32_t *t)
{
	uint32_t *o = out;
	uint16_t ver[4];
	unsigned n = pmic_get_versions(ver);

	(void)port;
	(void)in;
	for (unsigned i = 0; i < 4; i++)
		o[1 + i] = i < n ? ver[i] : 0;
	o[0] = n & 0xff;
	set_len(len, 5);
	trace(t, 0);
	return 0;
}

/* --- app_msg_uefi_options.c ---------------------------------------------- */

static uint32_t msg_plat_info(uint32_t port, uint32_t *len, void *in, void *out, uint32_t *t)
{
	(void)port;
	(void)in;
	(void)out;
	set_len(len, 0);
	trace(t, 0);
	return 0;
}

/* --- msg_clk.c ----------------------------------------------------------- */

/* UEFI is done with an SCMI clock it had enabled */
static uint32_t msg_clk_decreaseScmiClkNodeRefCnt(uint32_t port, uint32_t *len, void *in, void *out,
						   uint32_t *t)
{
	uint32_t id = ((uint32_t *)in)[0];

	(void)port;
	(void)out;
	set_len(len, 0);
	if (id >= clk_nodes_get_scmi_element_number()) {
		trace(t, 0xf1580060);
		return MSG_NOT_FOUND;
	}
	clk_api_decrease_ref_cnt(&((CLK_TREE_STRUCT *)clk_nodes_scmi_idx_to_ctrl_struct(id))->clk_tree_node);
	trace(t, 0);
	return 0;
}

/* --- app_msg_plat.c ------------------------------------------------------ */

/* the PMICs' fault status, accumulated since boot (CIX's report format) */
static uint32_t msg_pmic_get_events(uint32_t port, uint32_t *len, void *in, void *out, uint32_t *t)
{
	uint32_t buf[8] = { 0 }, n;

	(void)port;
	(void)in;
	(void)dev_pmic_read_status(PMIC_STATUS_RUN);
	n = dev_pmic_build_msg_report_buffer(PMIC_STATUS_ALL, buf, sizeof(buf)) >> 2;
	for (uint32_t i = 0; i < n; i++)
		((uint32_t *)out)[i] = buf[i];
	set_len(len, n);
	trace(t, 0);
	return 0;
}

/*
 * Fans (src/fan): CIX's msg_fan_get_num always answers not supported,
 * whatever fans are configured; the status of a fan (0-2) is its
 * temperature, the speeds expected and measured, the duty in percent and
 * the policy; a mode set takes the mode (0-2: Linux's quiet, temperature,
 * performance, told to the EC too; 3-5: the user's table, a speed, a
 * duty), the fan and its data (for the user's table, the number of rows
 * and the rows after it).
 */
static uint32_t msg_fan_get_num(uint32_t port, uint32_t *len, void *in, void *out, uint32_t *t)
{
	(void)port;
	(void)in;
	(void)out;
	set_len(len, 0);
	trace(t, 0xf18e0063);
	return MSG_NOT_SUPPORTED;
}

static uint32_t msg_fan_get_status(uint32_t port, uint32_t *len, void *in, void *out, uint32_t *t)
{
	uint32_t ret = MSG_INVALID;

	(void)port;
	if (((uint32_t *)in)[0] < FAN_N) {
		struct fan_state st = { 0 };

		fan_stat_get(((uint32_t *)in)[0], &st, false);
		memcpy(out, &st, 16);
		ret = 0;
	}
	set_len(len, 4);
	trace(t, ret ? 0xf18e0098 : 0);
	return ret;
}

static uint32_t msg_fan_set_mode(uint32_t port, uint32_t *len, void *in, void *out, uint32_t *t)
{
	const uint32_t *a = in;
	uint32_t mode = a[0], fan = a[1];
	int32_t r;

	(void)port;
	(void)out;
	dev_gpio_clear_ec_event();
	if (mode < 3) {
		/* Linux's 0, 1, 2: the quiet, temperature and performance tables */
		DEV_REG[CUS_FAN_MODE_REG][1] = (uint8_t)(mode + 2);
		DEV_REG[CUS_STAT_REG1][1] |= 1;
		dev_gpio_signal_ec_event();
		mode = mode == 0 ? FAN_CTRL_POLICY_QUIET :
		       mode == 2 ? FAN_CTRL_POLICY_PERF : FAN_CTRL_POLICY_TEMP;
	} else if (mode >= FAN_CTRL_POLICY_MAX) {
		goto invalid;
	}
	if (fan >= FAN_N)
		goto invalid;
	if (mode == FAN_CTRL_POLICY_USER && !fan_config_user_table(fan, a[3], &a[4], a[2]))
		goto invalid;
	r = dev_fan_set_ctrl_policy(fan, (enum fan_ctrl_policy)mode, a[2], a[3]);
	set_len(len, 0);
	trace(t, r ? 0xf18e0155 : 0);
	return (uint32_t)r;
invalid:
	set_len(len, 0);
	trace(t, 0xf18e0158);
	return MSG_INVALID;
}

/* bit 0: the SoC idle switch (CIX's DPM runtime) on or off as bit 0 of the
 * second word says; bit 1: the DSU stepped with the clusters, bit 1 */
static uint32_t msg_noc_dfs_mode(uint32_t port, uint32_t *len, void *in, void *out, uint32_t *t)
{
	const uint32_t *a = in;

	(void)port;
	(void)out;
	if (a[0] & 1)
		perf_soc_idle_switch_set_enable(a[1] & 1);
	if (a[0] & 2)
		sky1soc_set_dsu_dvfs((a[1] & 2) != 0);
	set_len(len, 0);
	trace(t, 0);
	return 0;
}

/* the eight EDP rails' voltages as the regulators say (mV) */
static uint32_t msg_edp_get_voltage(uint32_t port, uint32_t *len, void *in, void *out, uint32_t *t)
{
	(void)port;
	(void)in;
	for (unsigned d = 0; d < EDP_N; d++) {
		const struct rail *r = dpm_edp_get_pwr_rail_config(d);
		uint16_t mv;

		((uint32_t *)out)[d] = r && r->type && dev_pmic_readVoltage(r, &mv) ? mv : 0;
	}
	set_len(len, EDP_N);
	trace(t, 0);
	return 0;
}

/* and as the PM set them */
static uint32_t msg_edp_get_vset(uint32_t port, uint32_t *len, void *in, void *out, uint32_t *t)
{
	(void)port;
	(void)in;
	for (unsigned d = 0; d < EDP_N; d++) {
		const struct rail *r = dpm_edp_get_pwr_rail_config(d);
		uint16_t mv = 0;

		if (r && r->type && !dpm_edp_get_voltage(d, &mv))
			mv = 0;
		((uint32_t *)out)[d] = mv;
	}
	set_len(len, EDP_N);
	trace(t, 0);
	return 0;
}

/* CIX's _portX_msg, sorted by id */
const MD_MSG_TABLE _portX_msg[] = {
	{ 0x0000, 0, 4, 0, msg_channelTest },
	{ 0x0001, 1, 3, 0, msg_firmwareVersion },
	{ 0x0002, 2, 2, 4, msg_deferredNotify },
	{ 0x0003, 0, 4, 0, msg_trigger_str },
	{ 0x0004, 1, 4, 0, msg_stream_act },
	{ 0x0005, 1, 4, 0, msg_locked_perf_confg },
	{ 0x0040, 1, 4, 0, msg_cpu_online },
	{ 0x0041, 1, 4, 0, msg_cpu_offline },
	{ 0x0042, 1, 4, 0, msg_cpu_bootcore_id },
	{ 0x0050, 0, 2, 0, msg_smn_read },
	{ 0x0051, 0, 2, 0, msg_smn_write },
	{ 0x0052, 0, 2, 0, msg_smn_rmw },
	{ 0x0060, 0, 4, 0, msg_plat_info },
	{ 0x0061, 1, 4, 0, msg_clk_decreaseScmiClkNodeRefCnt },
	{ 0x0062, 1, 4, 0, msg_register_s3script },
	{ 0x0063, 1, 4, 0, msg_pmic_get_ver },
	{ 0x0400, 1, 4, 0, msg_pmic_get_events },
	{ 0x0800, 1, 4, 0, msg_fan_get_num },
	{ 0x0801, 1, 4, 0, msg_fan_get_status },
	{ 0x0802, 1, 4, 0, msg_fan_set_mode },
	{ 0x0803, 1, 4, 0, msg_noc_dfs_mode },
	{ 0x0804, 1, 4, 0, msg_edp_get_voltage },
	{ 0x0805, 1, 4, 0, msg_edp_get_vset },
	{ 0xffff, 0, 0, 0, NULL },
};
