/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The PMICs (CIX's dev_pmic.c and its drivers: dev_mp5475.c,
 * dev_paged_pmic.c with dev_mp2845b.c and dev_au4683.c), the power-on
 * setup (pmic_profile_setup, ss_probe_pmic) and the MP2845s' PSI setting
 * (set_mp2845b_psi7).
 *
 * A PMIC is registered (dev_pmic_register) when the first rail on it is:
 * one device per I2C port and address, with the driver of its regulator
 * type and, per buck, the EDP domains it feeds.  Two domains on one buck
 * are bound (src/board/pwr_rail.c), so the higher voltage wins.
 *
 * The MP5475 has a VOUT register per buck (0x13 + 8 * buck, 2 mV a step,
 * high byte first) and fault status in 07, 08 and 09.  The MP2845 and the
 * AU4683 are PMBus-like: register 0 selects the page (the buck), then
 * VOUT_COMMAND and READ_VOUT in the regulator's own format; CIX reads
 * no status from them.
 */
#include "i2c.h"
#include "lcru.h"
#include "pmic.h"
#include "rtos.h"
#include "clk/clk.h"
#include "dpm/dvfs_cfg.h"

/* --- the PMICs and their drivers (CIX's dev_pmic.c) ----------------------- */

/* CIX's PMIC_REGISTER_INFO */
struct pmic_reg_info {
	uint8_t page, vout_command, read_vout, rev;
	void (*volt_to_setting)(uint16_t *mv, uint8_t *setting);
	void (*setting_to_volt)(uint16_t *mv, const uint8_t *setting);
};

struct pmic_dev;

/* CIX's PMIC_HW_DRV */
struct pmic_drv {
	bool (*set_voltage)(const struct pmic_dev *dev, uint8_t buck, uint16_t *mv);
	uint32_t (*wait_ready_ns)(uint16_t from, uint16_t to);
	bool (*read_voltage)(const struct pmic_dev *dev, uint8_t buck, uint16_t *mv);
	uint16_t (*read_revision)(const struct pmic_dev *dev);
	bool (*read_status)(enum pmic_status_time idx);
	uint32_t (*build_report)(enum pmic_status_time idx, uint32_t *buf, uint32_t len);
};

/* CIX's PMIC_HW_DEV */
struct pmic_dev {
	const struct pmic_drv *drv;
	uint8_t i2c_port, i2c_addr;
	uint16_t pmic_rev;
	const struct pmic_reg_info *reg_info;
	uint16_t buck_info[4];		/* per buck, the EDP domains it feeds */
};

#define PMIC_DEV_MAX		4

static const struct pmic_drv *s_pmic_drv[PMIC_DEV_MAX];	/* per regulator type */
static struct pmic_dev s_pmic_dev[PMIC_DEV_MAX];
static uint8_t s_pmic_dev_nr;

/* --- MP5475 (CIX's dev_mp5475.c) -------------------------------------------- */

#define MP5475_VOUT(b)		(0x13 + 8 * (b))
#define MP5475_REVISION		0x51
#define MP5475_STATUS(i)	(0x07 + (i))	/* 07, 08, 09 */
#define MP5475_MV_MIN		300
#define MP5475_MV_MAX		2046

/* CIX's u8_get */
static bool u8_get(uint8_t port, uint8_t addr, uint8_t reg, uint8_t *v)
{
	*v = 0;
	return !i2c_write_read(port, addr, &reg, 1, v, 1);
}

static bool dev_pmic_mp5475_readVoltage(const struct pmic_dev *dev, uint8_t buck, uint16_t *mv)
{
	uint8_t reg = (uint8_t)MP5475_VOUT(buck), v[2] = { 0, 0 };
	bool ok = false;

	for (int tries = 0; tries < 4 && !ok; tries++)
		ok = !i2c_write_read(dev->i2c_port, dev->i2c_addr, &reg, 1, v, 2);
	if (ok && mv)
		*mv = (uint16_t)(((v[0] << 9) + v[1] * 2) & 0xfffe);
	return ok;
}

/*
 * The write, tried four times 80 us apart; after a retry the voltage is
 * read back and written again until it is within 4 mV (CIX's loop: once
 * the tries are spent a failed write ends a round, and a read-back that
 * agrees then still counts as a failure).  CIX's loop has no end: its
 * tries counter only stops the writes when it equals -1, so on a bus that
 * stays dead it writes for ever; here a failed write ends every round
 * after the fourth try and there are SET_ROUNDS rounds at most.  The
 * voltage is then what the regulator has: an even number of mV.
 */
#define SET_ROUNDS		3

static bool dev_pmic_mp5475_setVoltage(const struct pmic_dev *dev, uint8_t buck, uint16_t *mv)
{
	uint8_t w[3];
	int left = 3, rounds = 0;
	bool ok;

	if ((uint16_t)(*mv - MP5475_MV_MIN) > MP5475_MV_MAX - MP5475_MV_MIN)
		return false;
	w[0] = (uint8_t)MP5475_VOUT(buck);
	w[1] = (uint8_t)(*mv >> 9);		/* mV / 2, high byte first */
	w[2] = (uint8_t)(*mv >> 1);
	for (;;) {
		bool retried = false;
		uint16_t now;

		ok = true;
		while (i2c_write(dev->i2c_port, dev->i2c_addr, w, 3)) {
			left--;
			retried = true;
			us_delay(80);
			if (left < 0) {
				ok = false;
				break;
			}
		}
		if (!retried)
			break;
		if (dev_pmic_mp5475_readVoltage(dev, buck, &now) &&
		    (now > *mv ? now - *mv : *mv - now) < 4)
			break;
		if (++rounds == SET_ROUNDS) {
			ok = false;
			break;
		}
	}
	*mv &= (uint16_t)~1u;
	return ok;
}

/* 1 us a mV */
static uint32_t dev_pmic_mp5475_wait_4_pmic_ready(uint16_t from, uint16_t to)
{
	return 1000u * (uint32_t)(from > to ? from - to : to - from);
}

static uint16_t dev_pmic_mp5475_readRevision(const struct pmic_dev *dev)
{
	uint8_t rev;

	return u8_get(dev->i2c_port, dev->i2c_addr, MP5475_REVISION, &rev) ? rev : 0;
}

/*
 * CIX's dev_pmic_mp5475_read_status: the fault status registers 07, 08,
 * 09 of the PMICs on ports 0 and 1 (all three when get_pmic_scheme says
 * EVB, which it does whenever the BIOS configuration is there and does
 * not give a layout) read and kept for idx (with the time), then cleared
 * (09 written 0xff); what they said is also gathered since boot
 * (PMIC_STATUS_ALL).  A read that fails stops it all.
 */
static struct {
	uint64_t ts;
	uint8_t s07[3], s08[3], s09[3];
} err_flags[PMIC_STATUS_ALL + 1];

static unsigned mp5475_count(void)
{
	return get_pmic_scheme() == PMIC_EVB ? 3 : 2;
}

static bool dev_pmic_mp5475_read_status(enum pmic_status_time idx)
{
	static const uint8_t clear[2] = { MP5475_STATUS(2), 0xff };
	uint8_t addr[3] = { 0xff, 0xff, 0xff }, s[3][3] = { { 0 } };
	unsigned n;

	for (unsigned i = 0; i < mp5475_count(); i++)
		addr[i] = dpm_edp_get_pmic_i2c_addr((uint8_t)i);
	n = mp5475_count();
	for (unsigned i = 0; i < n; i++) {
		for (unsigned k = 0; k < 3; k++) {
			if (!u8_get((uint8_t)i, addr[i], MP5475_STATUS(k), &s[k][i]))
				return false;
		}
	}
	err_flags[idx].ts = arch_getTimeNowUs() | 1;
	for (unsigned i = 0; i < n; i++) {
		err_flags[idx].s07[i] = s[0][i];
		err_flags[idx].s08[i] = s[1][i];
		err_flags[idx].s09[i] = s[2][i];
	}
	for (unsigned i = 0; i < n; i++)
		i2c_write(i, addr[i], clear, 2);
	err_flags[PMIC_STATUS_ALL].ts = err_flags[idx].ts;
	for (unsigned i = 0; i < n; i++) {
		err_flags[PMIC_STATUS_ALL].s07[i] |= s[0][i];
		err_flags[PMIC_STATUS_ALL].s08[i] |= s[1][i];
		err_flags[PMIC_STATUS_ALL].s09[i] |= s[2][i];
	}
	return true;
}

bool pmic_status_get(enum pmic_status_time idx, uint8_t s07[3], uint8_t s08[3], uint8_t s09[3])
{
	if (!err_flags[idx].ts)
		return false;
	memcpy(s07, err_flags[idx].s07, 3);
	memcpy(s08, err_flags[idx].s08, 3);
	memcpy(s09, err_flags[idx].s09, 3);
	return true;
}

/*
 * CIX's dev_pmic_mp5475_build_msg_report_buffer (20 bytes): word 0 a
 * byte of fault classes per PMIC (the last first), word 1 0x3f per PMIC,
 * words 2-4 the 07, 08 and 09 registers, a byte per PMIC.  Nothing when
 * the status was never read.
 */
static uint32_t dev_pmic_mp5475_build_msg_report_buffer(enum pmic_status_time idx, uint32_t *buf,
							uint32_t len)
{
	const unsigned n = mp5475_count();
	uint32_t w0 = 0, w1 = 0;

	if (len < 20 || !err_flags[idx].ts)
		return 0;
	for (unsigned k = n; k--;) {
		uint8_t s07 = err_flags[idx].s07[k], s08 = err_flags[idx].s08[k];
		uint8_t s09 = err_flags[idx].s09[k];
		uint32_t f = w0 << 8;

		w1 = (w1 << 8) + 0x3f;
		if (s07 & 15)
			f += 16;
		if (s07 >> 4)
			f += 4;
		if (s08)
			f += 8;
		f |= ((s09 >> 4) & 1) * 2 + ((s09 & 0x60) != 0);
		if ((s09 >> 1) & 3)
			f |= 16;
		if (s09 & 3)
			f |= 8;
		w0 = f;
	}
	buf[0] |= w0;
	buf[1] |= w1;
	for (unsigned i = 0; i < 3; i++) {
		buf[2] |= (uint32_t)err_flags[idx].s07[i] << (8 * i);
		buf[3] |= (uint32_t)err_flags[idx].s08[i] << (8 * i);
		buf[4] |= (uint32_t)err_flags[idx].s09[i] << (8 * i);
	}
	return 20;
}

static const struct pmic_drv mp5475_drv = {
	.set_voltage = dev_pmic_mp5475_setVoltage,
	.wait_ready_ns = dev_pmic_mp5475_wait_4_pmic_ready,
	.read_voltage = dev_pmic_mp5475_readVoltage,
	.read_revision = dev_pmic_mp5475_readRevision,
	.read_status = dev_pmic_mp5475_read_status,
	.build_report = dev_pmic_mp5475_build_msg_report_buffer,
};

/* --- paged PMICs (CIX's dev_paged_pmic.c) ----------------------------------- */

/* CIX's dev_pmic_generic_paged_read: the page, then the register; four
 * tries */
static bool dev_pmic_generic_paged_read(const struct pmic_dev *dev, uint8_t page_id, uint8_t reg,
					uint8_t rbuf[2])
{
	uint8_t page[2] = { dev->reg_info->page, page_id };
	bool ok = false;

	for (int tries = 0; tries < 4 && !ok; tries++) {
		if (!i2c_write(dev->i2c_port, dev->i2c_addr, page, 2))
			ok = !i2c_write_read(dev->i2c_port, dev->i2c_addr, &reg, 1, rbuf, 2);
	}
	return ok;
}

static bool dev_pmic_generic_readVoltage(const struct pmic_dev *dev, uint8_t buck, uint16_t *mv)
{
	uint8_t v[2] = { 0, 0 };

	if (!dev_pmic_generic_paged_read(dev, buck, dev->reg_info->read_vout, v))
		return false;
	dev->reg_info->setting_to_volt(mv, v);
	return true;
}

/*
 * The page and VOUT_COMMAND (one transfer each, a repeated start between),
 * tried four times 80 us apart; after a retry the voltage is read back and
 * written again until it is within 10 mV (as the MP5475's, rounds bounded
 * as there).
 */
static bool dev_pmic_generic_setVoltage(const struct pmic_dev *dev, uint8_t buck, uint16_t *mv)
{
	uint8_t page[2] = { dev->reg_info->page, buck };
	uint8_t vout[3] = { dev->reg_info->vout_command, 0, 0 };
	int left = 3, rounds = 0;

	if ((uint16_t)(*mv - 300) > 1746)
		return false;
	dev->reg_info->volt_to_setting(mv, &vout[1]);
	for (;;) {
		bool retried = false, ok = true;
		uint16_t now;

		while (i2c_write_write(dev->i2c_port, dev->i2c_addr, page, 2, vout, 3)) {
			left--;
			retried = true;
			us_delay(80);
			if (left < 0) {
				ok = false;
				break;
			}
		}
		if (!retried)
			return ok;
		if (dev_pmic_generic_readVoltage(dev, buck, &now) &&
		    (now > *mv ? now - *mv : *mv - now) < 10)
			return ok;
		if (++rounds == SET_ROUNDS)
			return false;
	}
}

/* 400 ns a mV */
static uint32_t dev_pmic_generic_wait_4_pmic_ready(uint16_t from, uint16_t to)
{
	return 400u * (uint32_t)(from > to ? from - to : to - from);
}

static uint16_t dev_pmic_generic_readRevision(const struct pmic_dev *dev)
{
	uint8_t v[2];

	if (!dev_pmic_generic_paged_read(dev, 0, dev->reg_info->rev, v))
		return 0;
	return (uint16_t)(v[0] | v[1] << 8);
}

static bool dev_pmic_generic_read_status(enum pmic_status_time idx)
{
	(void)idx;
	return true;
}

/* no status: 20 bytes of zeros */
static uint32_t dev_pmic_generic_build_msg_report_buffer(enum pmic_status_time idx, uint32_t *buf,
							 uint32_t len)
{
	(void)idx;
	(void)len;
	memset(buf, 0, 20);
	return 20;
}

static const struct pmic_drv pmic_generic_drv = {
	.set_voltage = dev_pmic_generic_setVoltage,
	.wait_ready_ns = dev_pmic_generic_wait_4_pmic_ready,
	.read_voltage = dev_pmic_generic_readVoltage,
	.read_revision = dev_pmic_generic_readRevision,
	.read_status = dev_pmic_generic_read_status,
	.build_report = dev_pmic_generic_build_msg_report_buffer,
};

/* CIX's dev_mp2845b.c: VOUT in 5 mV steps, bits 9:8 with 0x44 above */
static void mp2845_volt_to_setting(uint16_t *mv, uint8_t *s)
{
	uint16_t code = *mv / 5;

	s[0] = (uint8_t)code;
	s[1] = (uint8_t)(((code >> 8) & 3) + 0x44);
	*mv = (uint16_t)(*mv - *mv % 5);
}

static void mp2845_setting_to_volt(uint16_t *mv, const uint8_t *s)
{
	*mv = (uint16_t)((((s[1] & 3) << 8) + s[0]) * 5);
}

static const struct pmic_reg_info mp2845b_reg_info = {
	.page = 0, .vout_command = 0x62, .read_vout = 0xa7, .rev = 0x83,
	.volt_to_setting = mp2845_volt_to_setting,
	.setting_to_volt = mp2845_setting_to_volt,
};

/*
 * CIX's dev_au4683.c: VOUT_COMMAND in 5 mV steps above 245 mV (12 bits);
 * READ_VOUT in PMBus LINEAR11 (a 5-bit exponent, an 11-bit mantissa), which
 * CIX turns into mV as mantissa * (977 << (exponent + 10)) / 1000, the
 * exponent being at most -1 (it asserts when the high byte is above 175).
 */
static void au4683_volt_to_setting(uint16_t *mv, uint8_t *s)
{
	int32_t code = ((int32_t)*mv - 245) / 5;

	s[0] = (uint8_t)code;
	s[1] = (uint8_t)((code >> 8) & 0xf);
	*mv = (uint16_t)(*mv - *mv % 5);
}

static void au4683_setting_to_volt(uint16_t *mv, const uint8_t *s)
{
	int32_t exp = (int16_t)(s[1] << 8) >> 11;
	uint32_t mant = ((uint32_t)(s[1] << 8) & 0x700) + s[0];

	*mv = (uint16_t)(mant * (977u << (exp + 10)) / 1000);
}

static const struct pmic_reg_info au4683_reg_info = {
	.page = 0, .vout_command = 0x21, .read_vout = 0x8b, .rev = 0x9b,
	.volt_to_setting = au4683_volt_to_setting,
	.setting_to_volt = au4683_setting_to_volt,
};

/* per regulator type 1-3: the driver, the registers */
static const struct pmic_drv *const type_drv[PMIC_TYPES - 1] = {
	&mp5475_drv, &pmic_generic_drv, &pmic_generic_drv,
};
static const struct pmic_reg_info *const type_reg_info[PMIC_TYPES - 1] = {
	NULL, &mp2845b_reg_info, &au4683_reg_info,
};

/* --- dev_pmic.c -------------------------------------------------------------- */

/* CIX's dev_pmic_get */
static const struct pmic_dev *dev_pmic_get(uint8_t idx)
{
	return idx < PMIC_DEV_MAX && s_pmic_dev[idx].drv ? &s_pmic_dev[idx] : NULL;
}

/* CIX's dev_pmic_find */
static struct pmic_dev *dev_pmic_find(uint8_t port, uint8_t addr)
{
	for (unsigned i = 0; i < PMIC_DEV_MAX; i++) {
		if (s_pmic_dev[i].i2c_port == port && s_pmic_dev[i].i2c_addr == addr &&
		    s_pmic_dev[i].drv)
			return &s_pmic_dev[i];
	}
	return NULL;
}

/* CIX's dev_pmic_register: a PMIC's revision is read when its first rail is */
void dev_pmic_register(uint8_t i2c_ch, uint8_t i2c_addr, uint8_t vr_type, uint8_t buck_id,
		       uint8_t bit_idx)
{
	struct pmic_dev *dev;

	if (bit_idx >= 16 || (i2c_ch | vr_type | buck_id | s_pmic_dev_nr) >= 4 || !vr_type)
		return;
	dev = dev_pmic_find(i2c_ch, i2c_addr);
	if (dev) {
		dev->buck_info[buck_id] |= (uint16_t)BIT(bit_idx);
		return;
	}
	if (!s_pmic_drv[vr_type])
		s_pmic_drv[vr_type] = type_drv[vr_type - 1];
	dev = &s_pmic_dev[s_pmic_dev_nr++];
	dev->i2c_addr = i2c_addr;
	dev->drv = type_drv[vr_type - 1];
	dev->i2c_port = i2c_ch;
	dev->reg_info = type_reg_info[vr_type - 1];
	dev->buck_info[buck_id] |= (uint16_t)BIT(bit_idx);
	dev->pmic_rev = dev->drv->read_revision(dev);
}

/* CIX's dev_pmic_get for each slot: the PMICs' revisions, in the order
 * they were registered */
unsigned pmic_get_versions(uint16_t ver[4])
{
	unsigned n = 0;

	for (uint8_t i = 0; i < PMIC_DEV_MAX; i++) {
		const struct pmic_dev *dev = dev_pmic_get(i);

		if (dev)
			ver[n++] = dev->pmic_rev;
	}
	return n;
}

/*
 * CIX's dev_pmic_setVoltage: the write (the driver may round the voltage),
 * then the time the regulator takes, in core cycles as CIX's cc_delay
 * counts them; the change goes to the PMIC event stream.
 */
bool dev_pmic_setVoltage(const struct rail *r, uint16_t old_mv, uint16_t new_mv, bool debug)
{
	struct pmic_dev *dev;
	uint32_t ns;

	(void)debug;
	if ((r->bus | r->buck) & 4)
		return false;
	dev = dev_pmic_find(r->bus, r->addr);
	if (!dev || !dev->drv->set_voltage(dev, r->buck, &new_mv))
		return false;
	ns = dev->drv->wait_ready_ns(old_mv, new_mv);
	if (ns)
		udelay_cycles(ns * 100 / 125 / (800000000u / core_hz));
	pmic_event_put(r->bus, r->buck, new_mv);
	return true;
}

/* CIX's dev_pmic_readVoltage (on the PMIC worker) */
struct read_voltage_call {
	const struct rail *r;
	uint16_t *mv;
	bool ok;
};

static void read_voltage_job(void *arg)
{
	struct read_voltage_call *c = arg;
	struct pmic_dev *dev = dev_pmic_find(c->r->bus, c->r->addr);

	c->ok = dev && dev->drv->read_voltage(dev, c->r->buck, c->mv);
}

bool dev_pmic_readVoltage(const struct rail *r, uint16_t *mv)
{
	struct read_voltage_call c = { .r = r, .mv = mv };

	rtos_call(RTOS_PMIC, read_voltage_job, &c);
	return c.ok;
}

/* CIX's dev_pmic_read_status: each regulator type's driver once (on the
 * PMIC worker, src/rtos.c) */
static void read_status_job(void *arg)
{
	enum pmic_status_time idx = (enum pmic_status_time)(uintptr_t)arg;

	for (unsigned i = 0; i < PMIC_DEV_MAX; i++) {
		if (s_pmic_drv[i])
			(void)s_pmic_drv[i]->read_status(idx);
	}
}

bool dev_pmic_read_status(enum pmic_status_time idx)
{
	rtos_call(RTOS_PMIC, read_status_job, (void *)(uintptr_t)idx);
	return true;
}

/* CIX's dev_pmic_build_msg_report_buffer: each driver's report in turn */
uint32_t dev_pmic_build_msg_report_buffer(enum pmic_status_time idx, uint32_t *buf, uint32_t len)
{
	uint32_t n = 0;

	for (unsigned i = 0; i < PMIC_DEV_MAX; i++) {
		if (n < len && s_pmic_drv[i])
			n += s_pmic_drv[i]->build_report(idx, buf + n / 4, len - n);
	}
	return n;
}

/* --- power-on (CIX's pmic_profile_setup, ss_probe_pmic) ---------------------- */

/* CIX's PMIC_PROBE_DATA: what answers on the bus picks the layout */
static const struct {
	uint8_t i2c_ch, i2c_addr, reg_addr, data_len, preset;
} pmic_probe_data[] = {
	{ 2, 0x60, 0x12, 1, PMIC_EVB },
	{ 1, 0x45, 0x83, 2, PMIC_PHECDA },
	{ 0, 0x21, 0x99, 2, PMIC_ALCOR },
};

/* CIX's pmic_mp2845_data: Phecda's MP2845s were found */
static bool pmic_mp2845_found;

/* CIX's ss_probe_pmic: each three times; Phecda's regulators then get
 * their power-saving setting */
static int ss_probe_pmic(void)
{
	uint8_t buf[2];

	for (unsigned i = 0; i < ARRAY_SIZE(pmic_probe_data); i++) {
		for (int tries = 0; tries < 3; tries++) {
			if (i2c_write_read(pmic_probe_data[i].i2c_ch, pmic_probe_data[i].i2c_addr,
					   &pmic_probe_data[i].reg_addr, 1, buf,
					   pmic_probe_data[i].data_len))
				continue;
			if (i == 1) {
				pmic_mp2845_found = true;
				set_mp2845b_psi7(true);
			}
			return pmic_probe_data[i].preset;
		}
	}
	return -1;
}

/* CIX's u_misc_u32lowestOneBit: the lowest set bit at or above start */
static uint8_t lowest_one_bit(uint32_t v, uint32_t start)
{
	v >>= start;
	return v ? (uint8_t)(__builtin_ctz(v) + start) : 0x80;
}

/*
 * CIX's pmic_profile_setup: the layout (when the BIOS gave none, found by
 * probing; nothing found is the CRB), its PMICs registered, the domains
 * sharing a buck bound, the rails with a regulator at their boot voltage,
 * the PMICs' status read.
 */
void pmic_init(void)
{
	if (dpm_edp_cfg_get_type() >= PMIC_PRESETS) {
		int preset = ss_probe_pmic();

		dpm_edp_cfg_set_type(preset >= 0 ? (enum pmic_preset)preset : PMIC_CRB);
	}
	for (uint8_t d = 0; d < EDP_N; d++) {
		const struct rail *r = dpm_edp_get_pwr_rail_config(d);

		if (r && r->type)
			dev_pmic_register(r->bus, r->addr, r->type, r->buck, d);
	}
	for (uint8_t i = 0; i < PMIC_DEV_MAX; i++) {
		const struct pmic_dev *dev = dev_pmic_get(i);

		if (!dev)
			break;
		for (unsigned b = 0; b < 4; b++) {
			uint8_t first = lowest_one_bit((uint8_t)dev->buck_info[b], 0), second;

			if (first >= EDP_N)
				continue;
			second = lowest_one_bit((uint8_t)dev->buck_info[b], first + 1u);
			if (second < EDP_N)
				pwr_rail_bind(first, second, true);
		}
	}
	for (uint8_t d = 0; d < EDP_N; d++) {
		const struct rail *r = dpm_edp_get_pwr_rail_config(d);

		if (r && r->type)
			dpm_edp_set_voltage(d, r->boot_mv);
	}
	(void)dev_pmic_read_status(PMIC_STATUS_POR);
}

/* --- PSI (CIX's set_mp2845b_psi7, subsys_hsm.c) ------------------------------ */

bool config_enable_psi7 = true;

/*
 * Phecda's MP2845s (0x45 on ports 0 and 1), each page: two vendor registers
 * (0x64 and, on page 1, 0x73), then 0x5f.  CIX's function takes an enable
 * it never looks at: the same values are written at power-on and after
 * suspend, unless the BIOS configuration turned it off.
 */
void set_mp2845b_psi7(bool enable)
{
	static const uint8_t r73[3] = { 0x73, 0x66, 0x58 };

	(void)enable;
	if (!config_enable_psi7)
		return;
	for (uint8_t port = 0; port < 2; port++) {
		for (uint8_t page = 0; page < 4; page++) {
			uint8_t sel[2] = { 0x00, page };
			uint8_t r64[3] = { 0x64, 0x9a, page == 0 ? 0x1c : 0x00 };
			uint8_t r5f[3] = { 0x5f, page != 2 ? 0xef : 0xbf, page != 2 ? 0x6e : 0x9b };

			(void)i2c_write_write(port, 0x45, sel, 2, r64, 3);
			if (page == 1)
				(void)i2c_write_write(port, 0x45, sel, 2, r73, 3);
			(void)i2c_write_write(port, 0x45, sel, 2, r5f, 3);
		}
	}
}

/* CIX's ss_s3_exit: Phecda's MP2845s get it again after suspend */
void pmic_resume(void)
{
	if (pmic_mp2845_found)
		set_mp2845b_psi7(true);
}
