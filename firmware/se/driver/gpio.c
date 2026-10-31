/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * FCH GPIO inputs (CIX driver/gpio/drv_gpio.c), for the DDR driver's board
 * straps.  GPIO n (1..156): bank, bit, pin-mux register and its GPIO
 * function, from CIX's table.
 */
#include "se.h"

static const struct {
	uint8_t bank;		/* 0..2 S5 GPIO, 3..6 FCH GPIO */
	uint8_t bit;
	uint16_t mux;		/* register offset in the bank group's pin mux */
	uint8_t func;
} fch_gpio_iomuxmap_table[156] = {
	{ 0, 0, 0x0, 0 }, { 0, 1, 0x4, 0 }, { 0, 2, 0x8, 0 }, { 0, 3, 0xc, 0 },
	{ 0, 4, 0x10, 0 }, { 0, 5, 0x14, 0 }, { 0, 6, 0x18, 0 }, { 0, 7, 0x1c, 0 },
	{ 0, 8, 0x20, 0 }, { 0, 9, 0x24, 0 }, { 0, 10, 0x28, 0 }, { 0, 11, 0x2c, 0 },
	{ 0, 12, 0x30, 0 }, { 0, 13, 0x34, 0 }, { 1, 0, 0x80, 0 }, { 1, 1, 0x84, 0 },
	{ 1, 2, 0x88, 0 }, { 1, 3, 0x8c, 1 }, { 1, 4, 0x90, 1 }, { 1, 5, 0x94, 1 },
	{ 1, 6, 0x98, 1 }, { 1, 7, 0x9c, 1 }, { 1, 8, 0xa0, 1 }, { 1, 9, 0xa4, 1 },
	{ 0, 14, 0xa8, 1 }, { 0, 15, 0xac, 1 }, { 0, 16, 0xb0, 1 }, { 0, 17, 0xb4, 1 },
	{ 0, 18, 0xb8, 1 }, { 0, 19, 0xbc, 0 }, { 0, 20, 0xc0, 0 }, { 0, 21, 0xc4, 0 },
	{ 0, 22, 0xc8, 0 }, { 0, 23, 0xcc, 0 }, { 0, 24, 0xd0, 0 }, { 0, 25, 0xd4, 0 },
	{ 0, 26, 0xd8, 0 }, { 0, 27, 0xdc, 0 }, { 0, 28, 0xe0, 0 }, { 0, 29, 0xe4, 0 },
	{ 0, 30, 0xe8, 0 }, { 0, 31, 0xec, 0 }, { 3, 0, 0x0, 0 }, { 3, 1, 0x4, 0 },
	{ 3, 2, 0x8, 0 }, { 3, 3, 0xc, 0 }, { 3, 4, 0x68, 1 }, { 3, 5, 0x6c, 1 },
	{ 3, 6, 0x70, 1 }, { 3, 7, 0x74, 1 }, { 3, 8, 0x78, 1 }, { 3, 9, 0x7c, 1 },
	{ 3, 10, 0x80, 1 }, { 3, 11, 0x84, 1 }, { 3, 12, 0x88, 2 }, { 3, 13, 0x8c, 2 },
	{ 3, 14, 0x90, 0 }, { 3, 15, 0x94, 2 }, { 3, 16, 0x98, 2 }, { 3, 17, 0x9c, 0 },
	{ 3, 18, 0xa0, 1 }, { 3, 19, 0xa4, 1 }, { 3, 20, 0xbc, 1 }, { 3, 21, 0xc0, 1 },
	{ 3, 22, 0xc4, 1 }, { 3, 23, 0xc8, 1 }, { 3, 24, 0xcc, 1 }, { 3, 25, 0xd0, 1 },
	{ 3, 26, 0xd4, 1 }, { 3, 27, 0xd8, 1 }, { 3, 28, 0xdc, 1 }, { 3, 29, 0xe0, 1 },
	{ 3, 30, 0xe4, 1 }, { 3, 31, 0xe8, 1 }, { 4, 0, 0xec, 1 }, { 4, 1, 0xf0, 1 },
	{ 4, 2, 0xf4, 1 }, { 4, 3, 0xf8, 1 }, { 4, 4, 0xfc, 1 }, { 4, 5, 0x100, 1 },
	{ 4, 6, 0x104, 1 }, { 4, 7, 0x108, 1 }, { 4, 8, 0x10c, 1 }, { 4, 9, 0x110, 1 },
	{ 4, 10, 0x114, 1 }, { 4, 11, 0x118, 1 }, { 4, 12, 0x11c, 1 }, { 4, 13, 0x120, 1 },
	{ 4, 14, 0x124, 1 }, { 4, 15, 0x128, 0 }, { 4, 16, 0x12c, 0 }, { 4, 17, 0x130, 0 },
	{ 4, 18, 0x134, 0 }, { 4, 19, 0x138, 0 }, { 4, 20, 0x13c, 2 }, { 4, 21, 0x140, 2 },
	{ 4, 22, 0x144, 2 }, { 4, 23, 0x148, 2 }, { 4, 24, 0x14c, 2 }, { 4, 25, 0x150, 2 },
	{ 4, 26, 0x154, 2 }, { 4, 27, 0x158, 2 }, { 4, 28, 0x15c, 1 }, { 4, 29, 0x160, 1 },
	{ 4, 30, 0x164, 1 }, { 4, 31, 0x168, 1 }, { 5, 0, 0x16c, 1 }, { 5, 1, 0x170, 1 },
	{ 5, 2, 0x174, 1 }, { 5, 3, 0x178, 1 }, { 5, 4, 0x17c, 1 }, { 5, 5, 0x180, 1 },
	{ 5, 6, 0x184, 1 }, { 5, 7, 0x188, 1 }, { 5, 8, 0x18c, 1 }, { 5, 9, 0x190, 1 },
	{ 5, 10, 0x194, 1 }, { 5, 11, 0x198, 1 }, { 5, 12, 0x19c, 1 }, { 5, 13, 0x1a0, 1 },
	{ 5, 14, 0x1a4, 0 }, { 5, 15, 0x1a8, 0 }, { 5, 16, 0x1ac, 0 }, { 5, 17, 0x1b0, 0 },
	{ 5, 18, 0x1b4, 0 }, { 5, 19, 0x1b8, 0 }, { 5, 20, 0x1bc, 0 }, { 5, 21, 0x1c0, 0 },
	{ 5, 22, 0x1c4, 0 }, { 5, 23, 0x1c8, 0 }, { 5, 24, 0x1cc, 0 }, { 5, 25, 0x1d0, 0 },
	{ 5, 26, 0x1d4, 0 }, { 5, 27, 0x1d8, 0 }, { 5, 28, 0x1dc, 0 }, { 5, 29, 0x1e0, 0 },
	{ 5, 30, 0x1e4, 0 }, { 5, 31, 0x1e8, 0 }, { 6, 0, 0x1ec, 0 }, { 6, 1, 0x1f0, 0 },
	{ 6, 2, 0x1f4, 0 }, { 6, 3, 0x1f8, 0 }, { 6, 4, 0x1fc, 0 }, { 6, 5, 0x200, 0 },
	{ 6, 6, 0x204, 0 }, { 6, 7, 0x208, 0 }, { 6, 8, 0x20c, 0 }, { 6, 9, 0x210, 0 },
	{ 6, 10, 0x214, 0 }, { 6, 11, 0x218, 0 }, { 6, 12, 0x21c, 0 }, { 6, 13, 0x220, 0 },
	{ 6, 14, 0x224, 0 }, { 6, 15, 0x48, 0 }, { 6, 16, 0x4c, 0 }, { 255, 255, 0xffff, 255 },
};

static const uint32_t gpio_bank_base[7] = {
	0x16004000, 0x16005000, 0x16004000, 0x04120000, 0x04130000, 0x04140000, 0x04150000,
};

#define GPIO_DIR_CLR	0x0
#define GPIO_IN_EN	0x4
#define GPIO_DATA_IN	0x10

uint32_t drv_gpio_get_value(uint32_t index, uint32_t *value)
{
	uint32_t mux_base, base, bit, a;

	if (value == NULL) {
		hal_printf_level(LOG_ERR, "[%s:%d]ERR: %s", __func__, __LINE__, "illegal input param. \n");
		return 0xffffffff;
	}
	if (index - 1 >= ARRAY_SIZE(fch_gpio_iomuxmap_table)) {
		hal_printf_level(LOG_ERR, "[%s:%d]ERR: %s", __func__, __LINE__, "illegal index number. \n");
		return 0xffffffff;
	}
	if (fch_gpio_iomuxmap_table[index - 1].bank < 3)
		mux_base = 0x16007000;
	else if (fch_gpio_iomuxmap_table[index - 1].bank <= 6)
		mux_base = 0x04170000;
	else
		goto bad;
	a = mux_base + fch_gpio_iomuxmap_table[index - 1].mux + 0x40000000;
	REG32(a) = (REG32(a) & 0xfffffe7f) | (uint32_t)fch_gpio_iomuxmap_table[index - 1].func << 7;
	hal_printf_level(LOG_INFO, "mux_addr is 0x%x, value = 0x%x\n", (unsigned)a, (unsigned)REG32(a));
	base = gpio_bank_base[fch_gpio_iomuxmap_table[index - 1].bank] + 0x40000000;
	bit = fch_gpio_iomuxmap_table[index - 1].bit;
	REG32(base + GPIO_DIR_CLR) &= ~BIT(bit);
	REG32(base + GPIO_IN_EN) |= BIT(bit);
	*value = (REG32(base + GPIO_DATA_IN) & BIT(bit)) >> bit;
	return 0;
bad:
	hal_printf_level(LOG_ERR, "[%s:%d]ERR: %s", __func__, __LINE__, "Set gpio mux mode fail.\n");
	return 0xffffffff;
}
