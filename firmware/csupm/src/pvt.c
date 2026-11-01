/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * PVT sensors (CIX's dev_pvt.c, app_pvt.c, pvt_event.c; the trims are
 * cfg_fuse.c's pvt_trim_fuse_init).
 *
 * Block i (0-12) at PVT_BASE + 0x1000 (i + 1):
 *   +0x00 control: bit 0 enable, [3:1] mode (0 temperature, bit 3 set:
 *         voltage), [8:4] and [14:9] trims from eFuse, [20:15] a filter
 *   +0x04 data: three 10-bit readings at bits 0, 10 and 20 (CIX's types 0,
 *         1, 2: maximum, minimum and the latest)
 *   +0x08 weight (averaging)
 *   +0x0c thresholds: [9:0] high-temperature warning, [19:10] safe
 *   +0x10 [9:0], [19:10] the low-temperature ones
 * The common block at PVT_BASE: +0x00 the sensors' enables and the average
 * reading, +0x08 [9:0] a low-temperature threshold and bit 16 its enable.
 *
 * A sensor above its warning threshold raises aggregator line 29 (bit per
 * sensor); CIX's handler then waits for it to fall below the safe one (line
 * 28) and back.  The thresholds are the BIOS's thermal trip (100 C without
 * one).  The SoC's hottest sensor is read every 100 ms (CIX's PVT polling)
 * and put where the EC reads it (CUS_TEMPT_REG1, src/board/i2c_slave.c).
 * CIX's sky1_pvt_init also sets up an event whose callback (on_pvt_event_cb)
 * would send SCMI sensor notifications; nothing in its image signals it,
 * so it is left out.
 *
 * A reading d converts to degrees C as CIX's convert2temperature does
 * (a quadratic fitted to the sensor), a temperature back to a code with
 * its inverse.  CIX computes in double precision on the core's DFP; GCC's
 * description of the core has single precision only, so the doubles here
 * are software (src/dpm/dfloat.c), which gives CIX's bits.
 */
#include <mod_pm_svc.h>

#include "board/board.h"
#include "irq.h"
#include "lcru.h"
#include "pvt.h"

#define PVT_BASE		0xa0070000u
#define PVT_COMMON_EN		(PVT_BASE + 0x00)
#define PVT_COMMON_LOW		(PVT_BASE + 0x08)
#define PVT_ALL_EN		0x800ffc00u
#define PVT_LOW_EN		BIT(16)
#define PVT(i)			(PVT_BASE + 0x1000 * ((i) + 1))
#define PVT_CTRL		0x00
#define PVT_DATA		0x04
#define PVT_WEIGHT		0x08
#define PVT_THRESH		0x0c
#define PVT_THRESH_LOW		0x10
#define CTRL_EN			BIT(0)
#define CTRL_MODE(c)		(((c) >> 1) & 7)
#define CTRL_VOLTAGE		BIT(3)
#define RESET_PVT		BIT(7)
#define PVT_SAFE_LINE		28
#define PVT_WARN_LINE		29
#define PVT_POLL_US		100000u

/* CIX's g_pvt_deadline: the high-temperature threshold, the BIOS's trip */
static uint8_t g_pvt_deadline = 100;
/* CIX's _s_pvt_weight_ptr: the BIOS's weights, when it gives them */
static const uint8_t *s_pvt_weight;

/* The eFuse copy the SE leaves above DRAM1 (pm.ld); while it holds the
 * marker the parts' PVT fuses were not distributed */
#define EFUSE_COPY		0x200be700u
#define EFUSE_UNSET		0xa8763271u
static const uint32_t efuse_default[4] __attribute__((section(".dram1.efuse"), used)) = {
	EFUSE_UNSET, EFUSE_UNSET, EFUSE_UNSET, EFUSE_UNSET,
};

/* CIX's coefficients (doubles in its image) */
#define PVT_C2			(-1.4147e-05)
#define PVT_C1			0.28532
#define PVT_C0			(-63.018)
#define PVT_K2			6.8504e-04
#define PVT_K1			3.584
#define PVT_K0			223.3
#define PVT_V1			0.00071388	/* voltage mode: V = d * V1 + V0 */
#define PVT_V0			0.45598

/* CIX's pvt_data_offset: where each data type's 10 bits are */
static const uint8_t pvt_data_offset[3] = { 0, 10, 20 };

/* in CIX's order of operations: (k2 * x) * x + x * k1 + k0 */
static float convert2temperature(uint32_t d)
{
	double x = (double)(float)d;

	return (float)(PVT_C2 * x * x + x * PVT_C1 + PVT_C0);
}

static int32_t convert_temperature_to_data(int32_t celsius)
{
	double t = (double)(float)celsius;

	return (int32_t)(float)(PVT_K2 * t * t + t * PVT_K1 + PVT_K0);
}

static float convert2voltage(uint32_t d)
{
	return (float)((double)(float)d * PVT_V1 + PVT_V0);
}

static const uint8_t *fuse_base(void)
{
	for (unsigned int i = 0; i < 4; i++) {
		if (REG32(EFUSE_COPY + 4 * i) == EFUSE_UNSET)
			return NULL;
	}
	return (const uint8_t *)EFUSE_COPY;
}

/* n bits at bit position pos, least significant first */
static uint32_t extract(const uint8_t *p, unsigned int pos, unsigned int n)
{
	uint32_t v = 0;

	for (unsigned int i = 0; i < n; i++)
		v |= (uint32_t)((p[(pos + i) / 8] >> ((pos + i) % 8)) & 1) << i;
	return v;
}

/* --- dev_pvt.c ------------------------------------------------------------------- */

/* CIX's dev_pvt_trim_set: a sensor's two trims, from the eFuses */
void dev_pvt_trim_set(uint32_t inst, uint32_t trimg, uint32_t trimo)
{
	if (inst < PVT_N)
		REG32(PVT(inst) + PVT_CTRL) = (REG32(PVT(inst) + PVT_CTRL) & 0xffff800fu) +
					      ((trimg & 0x1f) << 4) + ((trimo & 0x3f) << 9);
}

/* CIX's dev_pvt_set_control_mode */
bool dev_pvt_set_control_mode(uint32_t inst, enum dev_pvt_control_mode mode, int32_t data)
{
	uint32_t a = PVT(inst), code;

	if (inst >= PVT_N)
		return false;
	switch (mode) {
	case PVT_CTRL_HT_WARNING:
	case PVT_CTRL_HT_SAFE:
	case PVT_CTRL_LT_WARNING:
	case PVT_CTRL_LT_SAFE: {
		uint32_t reg = mode < PVT_CTRL_LT_WARNING ? PVT_THRESH : PVT_THRESH_LOW;
		uint32_t shift = (mode == PVT_CTRL_HT_SAFE || mode == PVT_CTRL_LT_SAFE) ? 10 : 0;

		code = (uint32_t)convert_temperature_to_data(data) & 0x3ff;
		REG32(a + reg) = (REG32(a + reg) & ~(0x3ffu << shift)) | code << shift;
		break;
	}
	case PVT_CTRL_FILTER:
		REG32(a + PVT_CTRL) = (REG32(a + PVT_CTRL) & 0xffe07fffu) | ((uint32_t)data & 0x3f) << 15;
		break;
	case PVT_CTRL_WEIGHT:
		REG32(a + PVT_WEIGHT) = (uint32_t)data & 0x3ff;
		REG32(PVT_COMMON_EN) |= PVT_ALL_EN;
		break;
	case PVT_CTRL_LT_ALL:
		code = (uint32_t)convert_temperature_to_data(data) & 0x3ff;
		REG32(PVT_COMMON_LOW) = (REG32(PVT_COMMON_LOW) & ~0x3ffu) | code;
		break;
	case PVT_CTRL_LT_ALL_EN:
		REG32(PVT_COMMON_LOW) = (REG32(PVT_COMMON_LOW) & ~PVT_LOW_EN) | (data != 0 ? PVT_LOW_EN : 0);
		break;
	default:
		return false;
	}
	return true;
}

void dev_pvt_enable_sensor(uint32_t inst)
{
	if (inst < PVT_N)
		REG32(PVT(inst) + PVT_CTRL) |= CTRL_EN;
}

/* CIX's dev_pvt_set_mode: temperature, voltage, or the process modes */
void dev_pvt_set_mode(uint32_t inst, enum dev_work_mode mode)
{
	static const uint8_t ctrl[5] = { 0x01, 0x09, 0x03, 0x05, 0x07 };

	if (inst < PVT_N && mode < 5)
		REG32(PVT(inst) + PVT_CTRL) = (REG32(PVT(inst) + PVT_CTRL) & ~0xfu) | ctrl[mode];
}

/* CIX's dev_pvt_get_mode; for PVT_N, the mode most sensors are in */
enum dev_work_mode dev_pvt_get_mode(uint32_t inst)
{
	uint32_t c;

	if (inst >= PVT_N) {
		unsigned t = 0, v = 0, p = 0;

		for (uint32_t i = 0; i < PVT_N; i++) {
			enum dev_work_mode m = dev_pvt_get_mode(i);

			if (m >= PVT_MODE_P0 && m <= PVT_MODE_P2)
				p++;
			else if (m == PVT_MODE_TEMP)
				t++;
			else if (m == PVT_MODE_VOLTAGE)
				v++;
		}
		if (t >= v && t >= p)
			return PVT_MODE_TEMP;
		if (v >= t)
			return v < p ? PVT_MODE_P0 : PVT_MODE_VOLTAGE;
		return PVT_MODE_P0;
	}
	c = REG32(PVT(inst) + PVT_CTRL);
	if (!(c & CTRL_EN))
		return PVT_MODE_OFF;
	if (CTRL_MODE(c) >= 4)
		return PVT_MODE_VOLTAGE;
	return (enum dev_work_mode)((0x04030200u >> (8 * CTRL_MODE(c))) & 0xff);
}

/* CIX's dev_pvt_reset_max_min_threshold: the max and min readings reset */
void dev_pvt_reset_max_min_threshold(uint32_t inst)
{
	if (inst < PVT_N)
		REG32(PVT(inst) + PVT_DATA) = (REG32(PVT(inst) + PVT_DATA) & 0xff000000u) | 0x3ff << 10;
	else
		REG32(PVT_COMMON_EN) = (REG32(PVT_COMMON_EN) & 0xc0000000u) | 0x3ff << 10;
}

uint32_t dev_pvt_get_raw_data(uint32_t inst, enum dev_pvt_data_type type)
{
	if (inst >= PVT_N || type > PVT_DATA_LATEST)
		return 0;
	return (REG32(PVT(inst) + PVT_DATA) >> pvt_data_offset[type]) & 0x3ff;
}

/* CIX's dev_pvt_get_data: degrees C, volts, or the raw code, as the
 * sensor's mode says; 0 for a sensor that is off */
float dev_pvt_get_data(uint32_t inst, enum dev_pvt_data_type type)
{
	uint32_t c, d;

	if (inst >= PVT_N || type > PVT_DATA_LATEST)
		return 0.0f;
	c = REG32(PVT(inst) + PVT_CTRL);
	d = (REG32(PVT(inst) + PVT_DATA) >> pvt_data_offset[type]) & 0x3ff;
	if (!(c & CTRL_EN))
		return 0.0f;
	if (c & CTRL_VOLTAGE)
		return convert2voltage(d);
	if (c & 6)
		return (float)d;
	return convert2temperature(d);
}

/* CIX's dev_pvt_get_average_data: the common block's reading */
float dev_pvt_get_average_data(enum dev_pvt_data_type type)
{
	uint32_t d;

	if (type > PVT_DATA_LATEST)
		return 0.0f;
	d = (REG32(PVT_COMMON_EN) >> pvt_data_offset[type]) & 0x3ff;
	switch (dev_pvt_get_mode(PVT_N)) {
	case PVT_MODE_TEMP:
		return convert2temperature(d);
	case PVT_MODE_VOLTAGE:
		return convert2voltage(d);
	default:
		return (float)d;
	}
}

/*
 * CIX's dev_pvt_ht_warning_isr (line 29, a source per sensor): the sensor
 * passed its warning threshold; its warning off, its safe one on, the
 * common low-temperature alarm on at 0 C.  dev_pvt_ht_safe_isr (line 28):
 * back below the safe threshold, the warning on again.
 */
static void dev_pvt_ht_warning_isr(uint32_t inst)
{
	if (inst >= PVT_N)
		return;
	REG32(AGG_ENABLE(PVT_WARN_LINE)) &= ~BIT(inst);
	REG32(AGG_ENABLE(PVT_SAFE_LINE)) |= BIT(inst);
	(void)dev_pvt_get_raw_data(inst, PVT_DATA_LATEST);	/* CIX logs it, with the mode */
	(void)dev_pvt_get_mode(inst);
	dev_pvt_set_control_mode(0, PVT_CTRL_LT_ALL_EN, 1);
	dev_pvt_set_control_mode(0, PVT_CTRL_LT_ALL, 0);
}

static void dev_pvt_ht_safe_isr(uint32_t inst)
{
	if (inst >= PVT_N)
		return;
	REG32(AGG_ENABLE(PVT_SAFE_LINE)) &= ~BIT(inst);
	REG32(AGG_ENABLE(PVT_WARN_LINE)) |= BIT(inst);
}

/* CIX's _intr_lv2_dispatcher for lines 28 and 29: each pending sensor, until
 * none is */
void pvt_safe_irq(void)
{
	uint32_t p;

	while ((p = REG32(AGG_STATUS(PVT_SAFE_LINE)) & REG32(AGG_ENABLE(PVT_SAFE_LINE))) != 0)
		dev_pvt_ht_safe_isr((uint32_t)__builtin_ctz(p));
}

void pvt_warning_irq(void)
{
	uint32_t p;

	while ((p = REG32(AGG_STATUS(PVT_WARN_LINE)) & REG32(AGG_ENABLE(PVT_WARN_LINE))) != 0)
		dev_pvt_ht_warning_isr((uint32_t)__builtin_ctz(p));
}

/* --- app_pvt.c ------------------------------------------------------------------- */

/*
 * CIX's sky1_pvt_data_config (do_pm_config): pm_config_pvt_t's thermal
 * trip (valid when bit 0 is clear) and weights (when weight_valid is 0),
 * then the board's sensors.
 */
void sky1_pvt_data_config(const uint8_t *cfg)
{
	uint32_t trip = (uint32_t)cfg[0] | (uint32_t)cfg[1] << 8 | (uint32_t)cfg[2] << 16 |
			(uint32_t)cfg[3] << 24;

	if (!(trip & 1))
		g_pvt_deadline = (uint8_t)(trip >> 1);
	if (cfg[4] == 0)
		s_pvt_weight = cfg + 5;
	config_board_sensor(cfg);
}

/*
 * CIX's sky1_pvt_init (ss_run_enter): the block out of reset, the trims,
 * each sensor's weight (79 for 0-9, 78 for the others, or the BIOS's) and
 * warning threshold, the sensors on; the common low-temperature alarm off.
 */
void pvt_init(void)
{
	const uint8_t *fuse;

	lcru_reset(RESET_PVT, false);		/* dev_pvt_init */
	lcru_reset(RESET_PVT, true);		/* dev_pvt_set_base */
	fuse = fuse_base();			/* pvt_trim_fuse_init */
	if (fuse) {
		for (unsigned int i = 0; i < PVT_N; i++)
			dev_pvt_trim_set(i, extract(fuse, 70 + 31 * i, 5), extract(fuse, 75 + 31 * i, 6));
	}
	for (unsigned int i = 0; i < PVT_N; i++) {
		dev_pvt_set_control_mode(i, PVT_CTRL_WEIGHT, s_pvt_weight ? s_pvt_weight[i] :
							     i < 10 ? 79 : 78);
		dev_pvt_set_control_mode(i, PVT_CTRL_HT_WARNING, g_pvt_deadline);
		dev_pvt_enable_sensor(i);
	}
	dev_pvt_set_control_mode(0, PVT_CTRL_LT_ALL_EN, 0);
	dev_pvt_set_control_mode(0, PVT_CTRL_LT_ALL, 0);
}

/* CIX's _s_pvt_data: the latest readings, then the GPU's mean */
static float s_pvt_data[PVT_N + 1];

/*
 * CIX's pvt_polling_tick_handler, every 100 ms: each sensor in temperature
 * mode read, the hottest put where the EC reads it (whole degrees, then
 * hundredths), unless none is above 0 C.
 */
static void pvt_polling_tick_handler(void *arg)
{
	float max = 0.0f;

	(void)arg;
	for (uint32_t i = 0; i < PVT_N; i++) {
		if (dev_pvt_get_mode(i) != PVT_MODE_TEMP)
			continue;
		s_pvt_data[i] = dev_pvt_get_data(i, PVT_DATA_LATEST);
		if (!(s_pvt_data[i] <= max))
			max = s_pvt_data[i];
	}
	s_pvt_data[PVT_N] = (s_pvt_data[1] + s_pvt_data[2]) * 0.5f;
	if (max != 0.0f) {
		int32_t whole = (int32_t)max;

		DEV_REG[CUS_TEMPT_REG1][0] = (uint8_t)whole;
		DEV_REG[CUS_TEMPT_REG1][1] = (uint8_t)(int32_t)((max - (float)(uint8_t)whole) * 100.0f);
	}
}

static uint8_t s_pvt_polling_tmrid = PM_SVC_TIMER_NONE;

/* CIX's sky1_pvt_fixed_polling_enable (pm_ready) */
void sky1_pvt_fixed_polling_enable(void)
{
	if (s_pvt_polling_tmrid == PM_SVC_TIMER_NONE)
		s_pvt_polling_tmrid = pm_svc_timer_alloc(PVT_POLL_US, pvt_polling_tick_handler, NULL,
							 true, true);
	else if (!pm_svc_timer_is_enabled(s_pvt_polling_tmrid))
		pm_svc_timer_enable(s_pvt_polling_tmrid);
}

void sky1_pvt_fixed_polling_disable(void)
{
	if (s_pvt_polling_tmrid != PM_SVC_TIMER_NONE)
		pm_svc_timer_disable(s_pvt_polling_tmrid);
}

/* CIX's sky1_pvt_get_value: the last reading polled */
float sky1_pvt_get_value(uint32_t inst)
{
	return inst <= PVT_N ? s_pvt_data[inst] : 0.0f;
}

/* SCMI's sensor numbering: 1-13 the sensors, 14 the mean of 2 and 3 (GPU) */
static int read_one(unsigned int i, float *celsius)
{
	uint32_t ctrl = REG32(PVT(i) + PVT_CTRL);
	uint32_t d = (REG32(PVT(i) + PVT_DATA) >> 20) & 0x3ff;

	if (!(ctrl & CTRL_EN) || CTRL_MODE(ctrl) != 0)
		return -1;
	*celsius = convert2temperature(d);
	return 0;
}

int pvt_temperature(unsigned int id, float *celsius)
{
	float a, b;

	if (id >= 1 && id <= PVT_N)
		return read_one(id - 1, celsius);
	if (id == PVT_N + 1) {
		if (read_one(1, &a) || read_one(2, &b))
			return -1;
		*celsius = (a + b) * 0.5f;
		return 0;
	}
	return -1;
}

/* --- pvt_event.c: the PVT readings for the AP's performance stream ------------ */

static uint16_t pvt_idx_bitmask;

void pvt_event_stream_setup(uint16_t pvt_sensor_mask)
{
	pvt_idx_bitmask |= pvt_sensor_mask & 0x7fff;
}

void pvt_event_stream_clear(void)
{
	pvt_idx_bitmask = 0;
}

static uint32_t float_bits(float f)
{
	uint32_t v;

	memcpy(&v, &f, sizeof(v));
	return v;
}

/*
 * CIX's pvt_event_fetch: the next sensor of the stream from prev_idx on
 * (0xff: none), its maximum, minimum and latest codes; 13 is the common
 * block's (in degrees, as floats), 14 the first two board sensors.
 */
uint8_t pvt_event_fetch(struct pvt_event *event, uint8_t prev_idx)
{
	uint32_t mask = (uint32_t)pvt_idx_bitmask >> prev_idx;
	uint8_t i;

	if (!mask)
		return 0xff;
	i = (uint8_t)(__builtin_ctz(mask) + prev_idx);
	if (i == PVT_N) {
		event->data_max = float_bits(dev_pvt_get_average_data(PVT_DATA_MAX));
		event->data_min = float_bits(dev_pvt_get_average_data(PVT_DATA_MIN));
		event->data_avg = float_bits(dev_pvt_get_average_data(PVT_DATA_LATEST));
	} else if (i == PVT_N + 1) {
		event->data_min = float_bits(config_board_sensor_get(0));
		event->data_max = float_bits(config_board_sensor_get(1));
		event->data_avg = 0;
	} else if (i < PVT_N) {
		event->data_max = dev_pvt_get_raw_data(i, PVT_DATA_MAX);	/* pvt_event_fill_data */
		event->data_min = dev_pvt_get_raw_data(i, PVT_DATA_MIN);
		event->data_avg = dev_pvt_get_raw_data(i, PVT_DATA_LATEST);
	}
	return i;
}
