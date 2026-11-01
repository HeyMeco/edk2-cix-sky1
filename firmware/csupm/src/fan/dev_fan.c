/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Fans (CIX's Source/common/drivers/dev_fan.c, and fan_iomux_set).
 *
 * Up to three fans, each a PWM output (counter block 2, node 3 + fan) and
 * a tachometer input (blocks 0 and 1, nodes 4 and 6) of CIX's counters
 * (src/perf/dev_counters.c).  The BIOS's PM configuration says which fans
 * there are (fan_data_config; the Orion O6N's, MS-R1's and Orange Pi 6's
 * do, the Orion O6's and Orange Pi 6 Plus's leave their fan to the EC), with
 * up to three tables of (RPM, up temperature, down temperature) rows, which
 * sensor to follow (a PVT sensor, a board sensor, or 255: the hottest PVT
 * sensor as polled), the PWM frequency and the tachometer's pulses per
 * turn.
 *
 * Every 500 ms the tachometer's interrupt (its callback, tach_cb) queues
 * adjust_fan_rpm: the sensor's temperature moves the row of the table up
 * (above the next row's up temperature) or down (below this row's down
 * temperature), the row gives the speed wanted, and the duty steps towards
 * it from the speed measured (calculate_duty_from_rpm_table).  Linux sets
 * the policy (PM message 0x802): one of the three tables, its own table
 * (USER, which falls back to the temperature table after 2 minutes at 75 C
 * or more when it asks so), a speed, or a duty.
 */
#include <mod_pm_svc.h>

#include "board/board.h"
#include "fan/fan.h"
#include "irq.h"
#include "perf/dev_counters.h"
#include "pvt.h"

#define FAN_TABLES		3		/* and the user's, the fourth */
#define FAN_ROWS		9
#define TACH_INTERVAL_MS	500
#define CNTS_MODE_PWM		3
#define CNTS_MODE_TACH		4
#define DUTY_FULL		256
#define DUTY_KICK		64		/* a fan that stands gets a quarter */
#define SENSOR_HOTTEST		0xff
#define USER_HOT_C		75.0f
#define USER_HOT_TICKS		240

/* CIX's rpm_entry_t */
typedef struct {
	uint16_t rpm;
	int8_t up_temp;
	int8_t down_temp;
} rpm_entry_t;

/* CIX's fan_ctrl_config_t */
typedef struct {
	rpm_entry_t *fan_rpm_table;	/* [4][9]: three tables, then the user's */
	uint16_t table_items[4];
	int16_t polarity;		/* tachometer pulses per turn */
	int16_t scaleup_margin;
	uint32_t pwm_freq;
	DEV_CNTS_ID pwm_id;
	DEV_CNTS_ID tach_id;
	uint8_t policy;
	int16_t policy_data;
	uint8_t policy_new;
	int16_t policy_data_new;
	float temprature;
	int16_t duty_to_set;
	int16_t real_rpm;
	int16_t expect_rpm;
	uint8_t using_item;
	uint8_t sensor_id;
	uint8_t pwm_reversed;
	uint8_t usermode;
	uint16_t user_data_record;
} fan_ctrl_config_t;

/* CIX's defaults: fan 0 with the CRB's table; the BIOS's configuration
 * replaces what it gives (in DRAM1, as CIX keeps its initialised tables) */
__attribute__((section(".dram1.data"))) static rpm_entry_t cpu_fan0_rpm[4 * FAN_ROWS] = {
	{ 0, 0, 20 }, { 0, 48, 43 }, { 1600, 50, 46 }, { 2700, 60, 56 }, { 4100, 65, 62 },
	{ 5500, 72, 67 },
};
static rpm_entry_t cpu_fan1_rpm[4 * FAN_ROWS];
static rpm_entry_t cpu_fan2_rpm[4 * FAN_ROWS];

#define CNTS_ID(inst, node)	{ .cntId = (inst) << 3 | (node) }
__attribute__((section(".dram1.data"))) static fan_ctrl_config_t s_fan_config[FAN_N] = {
	{ .fan_rpm_table = cpu_fan0_rpm, .table_items = { 9, 9, 9, 0 }, .polarity = 1,
	  .pwm_freq = 1500, .pwm_id = CNTS_ID(2, 3), .tach_id = CNTS_ID(0, 4), .duty_to_set = 2 },
	{ .fan_rpm_table = cpu_fan1_rpm, .pwm_id = CNTS_ID(2, 4), .tach_id = CNTS_ID(0, 6) },
	{ .fan_rpm_table = cpu_fan2_rpm, .pwm_id = CNTS_ID(2, 5), .tach_id = CNTS_ID(1, 4),
	  .sensor_id = 13 },
};

static uint32_t le32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint16_t le16(const uint8_t *p)
{
	return (uint16_t)(p[0] | p[1] << 8);
}

static rpm_entry_t *row(fan_ctrl_config_t *fc, unsigned table, unsigned item)
{
	return &fc->fan_rpm_table[FAN_ROWS * table + item];
}

/* CIX's copy_rpm_table: entries rows of the configuration into a table */
static void copy_rpm_table(fan_ctrl_config_t *fc, const uint8_t *from, uint32_t entries,
			   uint32_t item)
{
	for (uint32_t i = 0; i < entries && i < FAN_ROWS; i++) {
		rpm_entry_t *r = row(fc, item, i);

		r->rpm = le16(from + 4 * i);
		r->up_temp = (int8_t)from[4 * i + 2];
		r->down_temp = (int8_t)from[4 * i + 3];
	}
	fc->table_items[item] = (uint16_t)entries;
}

/*
 * CIX's fan_data_config: per fan of pm_config_fan_t (138 bytes: fan_valid,
 * rpm_table_valid[3], rpm_table_items[3], rpm_table[3][9], then fan_id,
 * sensor_id, fan_polarity, scaleup_margin, pwm_freq as config_data_t), when
 * valid: its tables (a table it does not give is the first one, unless it
 * has rows of its own), its counters (fan_id 0-3 picks them), the
 * tachometer's pulses (bits 16 up: the PWM inverted), margin, frequency,
 * sensor.
 */
void fan_data_config(const uint8_t *config_array)
{
	for (unsigned f = 0; f < FAN_N; f++) {
		const uint8_t *c = config_array + 138 * f;
		fan_ctrl_config_t *fc = &s_fan_config[f];
		uint32_t v;

		if (c[0] & 1)
			continue;
		for (unsigned t = 0; t < FAN_TABLES; t++) {
			if (c[4 + t] == 0) {
				copy_rpm_table(fc, c + 10 + 36 * t, c[7 + t], t);
			} else if (t && row(fc, t, 1)->rpm == 0) {
				memcpy(row(fc, t, 0), row(fc, 0, 0), FAN_ROWS * sizeof(rpm_entry_t));
				fc->table_items[t] = fc->table_items[0];
			}
		}
		v = le32(c + 0x76);
		if (!(v & ~6u)) {
			uint32_t id = v >> 1;

			fc->pwm_id.cntId = (fc->pwm_id.cntId & ~0x3ffu) + 16 + ((id + 3) & 7);
			fc->tach_id.cntId = (fc->tach_id.cntId & ~0x3ffu) + 4 + (v & 2) +
					    ((v << 1) & 0x3f8);
		}
		v = le32(c + 0x7e);
		if (!(v & 1)) {
			fc->pwm_reversed = (uint8_t)((int32_t)v >> 17);
			fc->polarity = (int16_t)(v >> 1);
		}
		v = le32(c + 0x82);
		if (!(v & 1))
			fc->scaleup_margin = (int16_t)(v >> 1);
		v = le32(c + 0x86);
		if (!(v & 1))
			fc->pwm_freq = v >> 1;
		v = le32(c + 0x7a);
		if (!(v & 1))
			fc->sensor_id = (uint8_t)(v >> 1);
	}
}

/* CIX's is_fan_configed: a fan with a speed in its table's second row */
static bool is_fan_configed(uint32_t id)
{
	return id < FAN_N && s_fan_config[id].fan_rpm_table && s_fan_config[id].fan_rpm_table[1].rpm;
}

/*
 * CIX's fan_iomux_set (app_misc): the tachometers' aggregator lines (22 and
 * 23) on, the PWM and tachometer pads to their fan function
 */
static void fan_iomux_set(void)
{
	static const uint32_t pads[] = { 0x4417014c, 0x44170150, 0x44170144, 0x44170148, 0x44170154,
					 0x44170158 };

	REG32(AGG_ENABLE(22)) = 0x7fff;
	REG32(AGG_ENABLE(23)) = 0x7fff;
	for (unsigned i = 0; i < ARRAY_SIZE(pads); i++)
		REG32(pads[i]) = (REG32(pads[i]) & ~0x180u) + 0x80;
}

static void adjust_fan_rpm(void *arg);

/* CIX's tach_cb: from the tachometer's interrupt, every 500 ms */
static void tach_cb(uint32_t data)
{
	if (data < FAN_N)
		(void)pm_svc_job_append(adjust_fan_rpm, (void *)(uintptr_t)data);
}

/*
 * CIX's fan_ctrl_start: each configured fan back to the first table at
 * duty 0, its tables cut after their last row with a speed, its PWM and
 * tachometer counters set up and started
 */
void fan_ctrl_start(void)
{
	fan_iomux_set();
	for (uint32_t f = 0; f < FAN_N; f++) {
		fan_ctrl_config_t *fc = &s_fan_config[f];

		if (!is_fan_configed(f))
			continue;
		fc->policy = FAN_CTRL_POLICY_TEMP;
		fc->policy_data = 0;
		fc->duty_to_set = 0;
		fc->using_item = 0;
		for (unsigned t = 0; t < FAN_TABLES + 1; t++) {
			uint16_t n = 1;

			for (unsigned i = FAN_ROWS - 1; i > 0; i--) {
				if (row(fc, t, i)->rpm) {
					n = (uint16_t)(i + 1);
					break;
				}
			}
			if (n < fc->table_items[t])
				fc->table_items[t] = n;
		}
		dev_cnts_free_node(fc->pwm_id);
		dev_cnts_free_node(fc->tach_id);
		dev_cnts_alloc_node(fc->pwm_id, CNTS_MODE_PWM);
		dev_cnts_pwm_setup(fc->pwm_id, fc->pwm_freq, (uint32_t)fc->duty_to_set);
		dev_cnts_pwm_start(fc->pwm_id);
		dev_cnts_pwm_update_duty(fc->pwm_id, 0);
		dev_cnts_alloc_node(fc->tach_id, CNTS_MODE_TACH);
		dev_cnts_tach_setup(fc->tach_id, TACH_INTERVAL_MS, (uint32_t)fc->polarity, tach_cb, f);
		dev_cnts_tach_start(fc->tach_id);
	}
}

/* CIX's fan_ctrl_suspend */
void fan_ctrl_suspend(void)
{
	for (uint32_t f = 0; f < FAN_N; f++) {
		if (!is_fan_configed(f))
			continue;
		dev_cnts_free_node(s_fan_config[f].pwm_id);
		dev_cnts_free_node(s_fan_config[f].tach_id);
	}
}

/*
 * CIX's dev_fan_set_ctrl_policy: from the next adjustment.  USER needs the
 * user's table; RPM takes a speed (at most 16383); DUTY a percentage (and,
 * from 1 kHz, a PWM frequency, at most 2 MHz).
 */
int32_t dev_fan_set_ctrl_policy(uint32_t fan_id, enum fan_ctrl_policy policy, uint32_t data1,
				uint32_t data2)
{
	fan_ctrl_config_t *fc;

	if (fan_id >= FAN_N || policy >= FAN_CTRL_POLICY_MAX)
		return -1;
	fc = &s_fan_config[fan_id];
	if (policy == FAN_CTRL_POLICY_USER) {
		if (!fc->table_items[3])
			return -1;
		fc->policy_new = FAN_CTRL_POLICY_USER;
		return 0;
	}
	fc->policy_new = (uint8_t)policy;
	if (policy == FAN_CTRL_POLICY_RPM) {
		fc->policy_data_new = (int16_t)(data1 < 0x3fff ? data1 : 0x3fff);
	} else if (policy == FAN_CTRL_POLICY_DUTY) {
		uint32_t freq = data2 < 2000000 ? data2 : 2000000;

		fc->policy_data_new = (int16_t)(((data1 < 100 ? data1 : 100) << 8) / 100);
		if (freq >= 1000) {
			fc->pwm_freq = freq;
			dev_cnts_pwm_update_freq(fc->pwm_id, freq);
		}
	}
	return 0;
}

/*
 * CIX's fan_config_user_table: the user's table (the fourth), from words
 * of (RPM << 16 | up << 8 | down).  When the first word has a speed the
 * user's rows follow a first row of (0, 0, 1); the rows must rise in speed
 * and in both temperatures, the first standing.  ops is kept for user_mode_policy (bit
 * 0: fall back when hot).
 */
bool fan_config_user_table(uint32_t fan_id, uint32_t user_items, const uint32_t *item_ptr,
			   uint32_t ops)
{
	fan_ctrl_config_t *fc;
	rpm_entry_t *t;
	unsigned first, room, n;

	if (user_items - 1 > 8 || fan_id >= FAN_N || !item_ptr)
		return false;
	fc = &s_fan_config[fan_id];
	t = row(fc, 3, 0);
	t[0] = (rpm_entry_t){ 0, 0, 1 };
	first = item_ptr[0] > 0xffff;
	room = first ? 8 : 9;
	for (unsigned i = 0; i < user_items && i < room; i++) {
		uint32_t w = item_ptr[i];

		t[first + i].rpm = (uint16_t)(w >> 16);
		t[first + i].up_temp = (int8_t)(w >> 8);
		t[first + i].down_temp = (int8_t)w;
	}
	n = first + user_items;
	fc->table_items[3] = (uint16_t)((n & ~1u) > 9 ? 9 : n);
	n = fc->table_items[3];
	if (n && t[0].rpm == 0) {
		unsigned i;

		for (i = 0; i + 1 < n; i++) {
			if (!t[i + 1].rpm || t[i].up_temp >= t[i + 1].up_temp ||
			    t[i].down_temp >= t[i + 1].down_temp || t[i].rpm >= t[i + 1].rpm)
				break;
		}
		if (i + 1 >= n) {
			fc->policy_new = FAN_CTRL_POLICY_USER;
			fc->usermode |= (uint8_t)ops;
			return true;
		}
	}
	memset(t, 0, FAN_ROWS * sizeof(rpm_entry_t));
	fc->table_items[3] = 0;
	return false;
}

/* CIX's user_mode_policy: USER with bit 0 falls back to the temperature
 * table after 240 adjustments (2 minutes) in a row at 75 C or more */
static void user_mode_policy(fan_ctrl_config_t *fc, float now)
{
	if (fc->policy != FAN_CTRL_POLICY_USER || !(fc->usermode & 1))
		return;
	if (now < USER_HOT_C) {
		fc->user_data_record = 0;
		return;
	}
	if (++fc->user_data_record < USER_HOT_TICKS)
		return;
	fc->policy_new = FAN_CTRL_POLICY_TEMP;
	fc->policy_data_new = 0;
	fc->usermode = 0;
}

/*
 * CIX's calculate_duty_from_rpm_table: the duty that brings the measured
 * speed towards the expected one.  A table whose speeds are all below 100
 * gives percentages (duty = expect * 256 / 100); otherwise within 50 RPM
 * the duty stays, below the table's second row it moves by 1, else by 2, or
 * by 20 when off by more than the margin (from the third row up; CIX looks
 * at the first table's rows whatever the policy); it stays
 * within 0-256, and a fan standing (below 5 RPM) gets at least 64.
 */
static int32_t calculate_duty_from_rpm_table(int32_t rpm_now, int32_t rpm_expect,
					     fan_ctrl_config_t *fc)
{
	unsigned t = fc->policy >> 2 ? 0 : fc->policy, n = fc->table_items[t];
	uint32_t rpm_max = n < 2 ? 0xffff : (uint16_t)(row(fc, t, n - 1)->rpm - 1);
	int32_t delta, step, duty;
	const rpm_entry_t *t0 = fc->fan_rpm_table;

	if (rpm_expect == 0)
		return 0;
	if (rpm_expect <= 100 && rpm_max <= 99)
		return (rpm_expect << 8) / 100;
	delta = rpm_expect - rpm_now;
	if (((uint32_t)delta << 16) + 0x31ffffu <= 0x63fffeu)
		return fc->duty_to_set;
	step = (int32_t)((uint32_t)delta << 16) > 0 ? 2 : -2;
	if (rpm_expect < t0[1].rpm || rpm_now < t0[1].rpm) {
		step >>= 1;
	} else if (fc->table_items[0] >= 3 && rpm_expect > t0[2].rpm) {
		int32_t d = (int16_t)delta;

		if (d > fc->scaleup_margin || d < -fc->scaleup_margin)
			step *= 10;
	}
	duty = fc->duty_to_set + step;
	duty = duty > DUTY_FULL ? DUTY_FULL : duty < 0 ? 0 : duty;
	return duty < DUTY_KICK && rpm_now < 5 ? DUTY_KICK : duty;
}

/* CIX's adjust_fan_rpm: see above */
static void adjust_fan_rpm(void *arg)
{
	uint32_t fan = (uint32_t)(uintptr_t)arg, rpm_now = 0;
	fan_ctrl_config_t *fc;
	float now, before;
	unsigned t, n, idx;
	int32_t duty;

	if (fan >= FAN_N)
		return;
	fc = &s_fan_config[fan];
	before = fc->temprature;
	if (fc->sensor_id == SENSOR_HOTTEST)
		now = (float)((double)(float)DEV_REG[CUS_TEMPT_REG1][1] / 100.0 +
			      (double)(float)DEV_REG[CUS_TEMPT_REG1][0]);
	else if (fc->sensor_id < PVT_N)
		now = dev_pvt_get_data(fc->sensor_id, PVT_DATA_LATEST);
	else
		now = config_board_sensor_get(fc->sensor_id - PVT_N);
	fc->temprature = now;
	if (fc->policy != fc->policy_new) {
		fc->policy = fc->policy_new;
		fc->using_item = 0;
	}
	if (fc->policy_data != fc->policy_data_new)
		fc->policy_data = fc->policy_data_new;
	t = fc->policy >> 2 ? 0 : fc->policy;
	n = fc->table_items[t];
	idx = fc->using_item;
	if (before <= now) {
		while (idx + 1 < n && now > row(fc, t, idx + 1)->up_temp)
			idx++;
	} else {
		while (idx > 0 && now < row(fc, t, idx)->down_temp)
			idx--;
	}
	fc->using_item = (uint8_t)idx;
	user_mode_policy(fc, now);
	fc->expect_rpm = (int16_t)row(fc, t, idx)->rpm;
	if (!dev_cnts_tach_read_rpm(fc->tach_id, &rpm_now) || rpm_now >= 0x8000)
		return;
	fc->real_rpm = (int16_t)rpm_now;
	if (fc->policy < FAN_CTRL_POLICY_RPM)
		duty = calculate_duty_from_rpm_table((int32_t)rpm_now, fc->expect_rpm, fc);
	else if (fc->policy == FAN_CTRL_POLICY_RPM)
		duty = calculate_duty_from_rpm_table((int32_t)rpm_now, fc->policy_data, fc);
	else if (fc->policy == FAN_CTRL_POLICY_DUTY)
		duty = fc->policy_data;
	else
		return;
	if (duty == fc->duty_to_set)
		return;
	fc->duty_to_set = (int16_t)duty;
	dev_cnts_pwm_update_duty(fc->pwm_id, (uint32_t)(fc->pwm_reversed ? DUTY_FULL - duty : duty));
}

/* CIX's fan_stat_get (PM message 0x801): temperature, speeds, duty in
 * percent, policy */
void fan_stat_get(uint32_t fan_id, struct fan_state *state, bool dump)
{
	const fan_ctrl_config_t *fc;
	int32_t d;

	(void)dump;
	if (fan_id >= FAN_N || !state)
		return;
	fc = &s_fan_config[fan_id];
	d = fc->duty_to_set * 100;
	state->policy = fc->policy;
	state->real_rpm = fc->real_rpm;
	state->temprature = (int32_t)fc->temprature;
	state->expect_rpm = fc->expect_rpm;
	state->duty = (d < 0 ? d + 255 : d) >> 8;
}
