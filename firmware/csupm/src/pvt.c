/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * PVT sensors (CIX's dev_pvt.c, app_pvt.c, cfg_fuse.c).
 *
 * Block i (0-12) at PVT_BASE + 0x1000 (i + 1):
 *   +0x00 control: bit 0 enable, [3:1] mode (0 temperature, bit 3 set:
 *         voltage), [8:4] and [14:9] trims from eFuse
 *   +0x04 data: three 10-bit readings at bits 0, 10 and 20
 *   +0x08 weight (averaging)
 *   +0x0c thresholds: [9:0] the high-temperature one
 * The common block at PVT_BASE: +0x00 the sensors' enables, +0x08 [9:0] a
 * low-temperature threshold and bit 16 its enable.
 *
 * A reading d converts to degrees C as CIX's convert2temperature does
 * (a quadratic fitted to the sensor), a temperature back to a code with
 * its inverse.  CIX computes in double precision on the core's DFP; GCC's
 * description of the core has single precision only, which is plenty for
 * 10-bit readings.
 */
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
#define CTRL_EN			BIT(0)
#define CTRL_MODE(c)		(((c) >> 1) & 7)
#define CTRL_VOLTAGE		BIT(3)
#define RESET_PVT		BIT(7)

#define PVT_DEADLINE_C		100	/* high-temperature threshold */

/* The eFuse copy the SE leaves above DRAM1 (pm.ld); while it holds the
 * marker the parts' PVT fuses were not distributed */
#define EFUSE_COPY		0x200be700u
#define EFUSE_UNSET		0xa8763271u
static const uint32_t efuse_default[4] __attribute__((section(".dram1.efuse"), used)) = {
	EFUSE_UNSET, EFUSE_UNSET, EFUSE_UNSET, EFUSE_UNSET,
};

/* CIX's coefficients (doubles in its image) */
#define PVT_C2			(-1.4147e-05f)
#define PVT_C1			0.28532f
#define PVT_C0			(-63.018f)
#define PVT_K2			6.8504e-04f
#define PVT_K1			3.584f
#define PVT_K0			223.3f

static float to_celsius(uint32_t d)
{
	float x = (float)(int32_t)d;

	return x * x * PVT_C2 + x * PVT_C1 + PVT_C0;
}

static uint32_t to_code(int celsius)
{
	float t = (float)celsius;

	return (uint32_t)(int32_t)(t * t * PVT_K2 + t * PVT_K1 + PVT_K0);
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

void pvt_init(void)
{
	const uint8_t *fuse;

	lcru_reset(RESET_PVT, false);
	lcru_reset(RESET_PVT, true);
	fuse = fuse_base();
	if (fuse) {
		for (unsigned int i = 0; i < PVT_N; i++) {
			uint32_t a = extract(fuse, 70 + 31 * i, 5), b = extract(fuse, 75 + 31 * i, 6);

			REG32(PVT(i) + PVT_CTRL) = (REG32(PVT(i) + PVT_CTRL) & 0xffff800fu) |
						   a << 4 | b << 9;
		}
	}
	for (unsigned int i = 0; i < PVT_N; i++) {
		REG32(PVT(i) + PVT_WEIGHT) = i < 10 ? 79 : 78;
		REG32(PVT_COMMON_EN) |= PVT_ALL_EN;
		REG32(PVT(i) + PVT_THRESH) = (REG32(PVT(i) + PVT_THRESH) & ~0x3ffu) |
					     (to_code(PVT_DEADLINE_C) & 0x3ff);
		REG32(PVT(i) + PVT_CTRL) |= CTRL_EN;
	}
	REG32(PVT_COMMON_LOW) &= ~PVT_LOW_EN;
	REG32(PVT_COMMON_LOW) = (REG32(PVT_COMMON_LOW) & ~0x3ffu) | (to_code(0) & 0x3ff);
}

static int read_one(unsigned int i, float *celsius)
{
	uint32_t ctrl = REG32(PVT(i) + PVT_CTRL);
	uint32_t d = (REG32(PVT(i) + PVT_DATA) >> 20) & 0x3ff;

	if (!(ctrl & CTRL_EN) || CTRL_MODE(ctrl) != 0)
		return -1;
	*celsius = to_celsius(d);
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
