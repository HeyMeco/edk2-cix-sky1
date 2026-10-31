/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * PMICs and the rails of the eight EDP domains (CIX's dev_pmic, dev_mp5475,
 * pmic_profile_setup).
 *
 * Which buck of which PMIC feeds which domain depends on the board; the
 * firmware tells the boards apart by probing (pmic_probe below) and
 * falls back to the CRB layout.  The layouts are those of CIX's
 * dpm_edp_scheme table.
 */
#include "i2c.h"
#include "lcru.h"
#include "pmic.h"

/* Register layout of an MP5475 buck: VOUT at 0x13 + 8 * buck, 2 mV a step,
 * written high byte first */
#define MP5475_VOUT(b)		(0x13 + 8 * (b))
#define MP5475_REVISION		0x51
#define MP5475_STATUS(i)	(0x07 + (i))	/* 07, 08, 09 */
#define MP5475_MV_MIN		300
#define MP5475_MV_MAX		2046

/*
 * Board layouts, indexed by pmic_preset and EDP domain: I2C bus and
 * address, buck, regulator type (0 none, 1 MP5475, 2 and 3 others), the
 * domain's current limit, its boot voltage and the offset added at the
 * PMIC (IR drop).
 */
#define R(b, a, k, t, lim, mv, off)	{ b, a, k, t, lim, mv, off }
static const struct rail schemes[PMIC_PRESETS][EDP_N] = {
	[PMIC_EVB] = {
		R(1, 0x60, 2, 1, 2400, 790, 25), R(0, 0x60, 1, 1, 6700, 790, 30),
		R(1, 0x60, 3, 1, 6500, 790, 30), R(1, 0x60, 0, 1, 8000, 790, 30),
		R(0, 0x60, 2, 1, 8000, 790, 30), R(0, 0x60, 0, 1, 5500, 790, 15),
		R(2, 0x60, 2, 1, 12000, 790, 20), R(2, 0x60, 0, 1, 9000, 790, 15),
	},
	[PMIC_PHECDA] = {
		R(0, 0x45, 1, 2, 2500, 790, 25), R(1, 0x45, 2, 2, 6500, 790, 30),
		R(0, 0x45, 0, 2, 6500, 790, 30), R(0, 0x45, 2, 2, 8000, 790, 30),
		R(0, 0x45, 3, 2, 8000, 790, 30), R(1, 0x45, 3, 2, 5500, 790, 15),
		R(1, 0x45, 1, 2, 12000, 790, 20), R(1, 0x45, 0, 2, 9000, 790, 15),
	},
	[PMIC_CRB] = {
		R(1, 0x60, 1, 1, 2400, 790, 25), R(0, 0x60, 2, 1, 6000, 790, 30),
		R(1, 0x60, 0, 1, 6000, 790, 30), R(1, 0x60, 3, 1, 8000, 790, 30),
		R(1, 0x60, 2, 1, 8000, 790, 30), R(0, 0x60, 3, 1, 5500, 790, 15),
		R(0, 0x60, 0, 1, 12000, 790, 20), R(0, 0x00, 0, 0, 9000, 790, 15),
	},
	[PMIC_NIO] = {
		R(0, 0x60, 1, 1, 2000, 790, 25), R(1, 0x60, 2, 1, 4000, 790, 30),
		R(0, 0x60, 0, 1, 4000, 790, 30), R(1, 0x60, 0, 1, 5000, 790, 30),
		R(1, 0x60, 1, 1, 5000, 790, 30), R(1, 0x60, 3, 1, 3500, 790, 15),
		R(0, 0x60, 3, 1, 6000, 790, 20), R(0, 0x60, 2, 1, 6000, 790, 15),
	},
	[PMIC_CLDBK_POR] = {
		R(0, 0x00, 0, 0, 0, 790, 25), R(0, 0x45, 1, 2, 6000, 790, 30),
		R(0, 0x00, 0, 0, 0, 790, 30), R(0, 0x00, 0, 0, 0, 790, 30),
		R(0, 0x45, 0, 2, 6000, 790, 30), R(0, 0x45, 2, 2, 5500, 790, 15),
		R(0, 0x45, 3, 2, 6000, 790, 20), R(0, 0x00, 0, 0, 8000, 790, 15),
	},
	[PMIC_CLDBK2024] = {
		R(1, 0x60, 1, 1, 0, 790, 25), R(0, 0x60, 3, 1, 6000, 790, 30),
		R(1, 0x60, 0, 1, 0, 790, 30), R(1, 0x60, 3, 1, 0, 790, 30),
		R(1, 0x60, 2, 1, 6000, 790, 30), R(0, 0x60, 2, 1, 5500, 790, 15),
		R(0, 0x60, 0, 1, 6000, 790, 20), R(0, 0x00, 0, 0, 8000, 790, 15),
	},
	[PMIC_ALCOR] = {
		R(0, 0x00, 0, 0, 2500, 825, 25), R(1, 0x21, 1, 3, 6500, 790, 30),
		R(0, 0x21, 1, 3, 6500, 790, 30), R(0, 0x21, 2, 3, 8000, 790, 30),
		R(1, 0x21, 2, 3, 8000, 790, 30), R(0, 0x00, 0, 0, 5500, 815, 15),
		R(1, 0x21, 0, 3, 12000, 790, 20), R(0, 0x21, 0, 3, 9000, 790, 15),
	},
};

/* What answers on the bus picks the layout: an MP5475 at 0x60 on bus 2
 * (EVB), an MP2845 at 0x45 on bus 1 (Phecda); nothing, the CRB. */
static const struct {
	uint8_t bus, addr, reg, len, preset;
} probes[] = {
	{ 2, 0x60, 0x12, 1, PMIC_EVB },
	{ 1, 0x45, 0x83, 2, PMIC_PHECDA },
};

static const struct rail *rails;
static uint8_t preset;
static uint16_t revision[I2C_N];
static uint8_t registered;			/* bit per bus */

static int pmic_probe(void)
{
	uint8_t buf[2];

	for (unsigned i = 0; i < ARRAY_SIZE(probes); i++) {
		for (int tries = 0; tries < 3; tries++) {
			if (!i2c_write_read(probes[i].bus, probes[i].addr, &probes[i].reg, 1,
					    buf, probes[i].len))
				return probes[i].preset;
		}
	}
	return PMIC_CRB;
}

static int read_u8(unsigned bus, uint8_t addr, uint8_t reg, uint8_t *v)
{
	return i2c_write_read(bus, addr, &reg, 1, v, 1);
}

/* A PMIC's revision is read when its first rail is registered */
static void rail_register(const struct rail *r)
{
	uint8_t rev = 0;

	if (registered & BIT(r->bus))
		return;
	registered |= (uint8_t)BIT(r->bus);
	if (r->type == PMIC_MP5475 && !read_u8(r->bus, r->addr, MP5475_REVISION, &rev))
		revision[r->bus] = rev;
}

static bool mp5475_set(const struct rail *r, uint32_t mv)
{
	uint8_t w[3];

	if (mv < MP5475_MV_MIN || mv > MP5475_MV_MAX)
		return false;
	w[0] = (uint8_t)MP5475_VOUT(r->buck);
	w[1] = (uint8_t)(mv >> 9);		/* mV / 2, high byte first */
	w[2] = (uint8_t)(mv >> 1);
	for (int tries = 0; tries < 4; tries++) {
		if (!i2c_write(r->bus, r->addr, w, 3))
			return true;
		udelay_cycles(80 * (core_hz / 1000000));
	}
	return false;
}

bool pmic_set_mv(unsigned domain, uint32_t mv)
{
	const struct rail *r = &rails[domain];

	if (r->type != PMIC_MP5475)
		return false;			/* other regulators: not yet */
	return mp5475_set(r, mv + r->offset_mv);
}

/* The address of the PMIC on a bus, from the layout */
static uint8_t pmic_addr(unsigned bus)
{
	for (unsigned d = 0; d < EDP_N; d++) {
		if (rails[d].type && rails[d].bus == bus)
			return rails[d].addr;
	}
	return 0;
}

/* Read and clear the fault status of the PMICs on buses 0 and 1 (CIX: all
 * three when the PM configuration says EVB) */
static void pmic_status(void)
{
	static const uint8_t clear[2] = { MP5475_STATUS(2), 0xff };
	unsigned n = 2;
	uint8_t v;

	for (unsigned i = 0; i < n; i++) {
		for (unsigned k = 0; k < 3; k++) {
			if (read_u8(i, pmic_addr(i), MP5475_STATUS(k), &v))
				break;
		}
	}
	for (unsigned i = 0; i < n; i++)
		i2c_write(i, pmic_addr(i), clear, 2);
}

void pmic_init(void)
{
	preset = (uint8_t)pmic_probe();
	rails = schemes[preset];
	for (unsigned d = 0; d < EDP_N; d++) {
		if (rails[d].type)
			rail_register(&rails[d]);
	}
	for (unsigned d = 0; d < EDP_N; d++)
		pmic_set_mv(d, rails[d].boot_mv);
}

void pmic_check(void)
{
	pmic_status();
}
