/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* eFuse array access and programming (CIX driver/efuse/drv_efuse.c) */
#include "se.h"
#include "efuse.h"

#define EFUSE_SIZE		0x800
#define EFUSE_PROG		(SE_EFUSE_CTRL_BASE + 0x4)
#define EFUSE_PROG_START	0x80000101u
#define EFUSE_PROG_DONE		BIT(1)
#define EFUSE_SENSE_START	0x40000010u
#define EFUSE_SENSE_DONE	BIT(5)

uint32_t drv_fuse_read(uint8_t *buf, uint32_t offset, uint32_t len)
{
	if (len > EFUSE_SIZE) {
		SE_ERR(EFUSE_ERR_PARAM);
		return EFUSE_ERR_PARAM;
	}
	memcpy(buf, (const void *)(SE_EFUSE_BASE + offset), len);
	return 0;
}

/* bits [start_bit, start_bit + bits) of data, to out from bit 0 */
void drv_fuse_align_for_read(uint32_t start_bit, const uint8_t *data, uint32_t bits, uint8_t *out)
{
	uint32_t i, b;

	memset(out, 0, (bits + 7) / 8);
	for (i = 0; i < bits; i++) {
		b = start_bit + i;
		out[i / 8] |= ((data[b / 8] >> (b % 8)) & 1) << (i % 8);
	}
}

/* bits [0, bits) of data, to out from start_bit */
void drv_fuse_align_for_write(uint32_t start_bit, const uint8_t *data, uint32_t bits, uint8_t *out)
{
	uint32_t i, b;

	memset(out, 0, (bits + 7 + start_bit) / 8);
	for (i = 0; i < bits; i++) {
		b = start_bit + i;
		out[b / 8] |= ((data[i / 8] >> (i % 8)) & 1) << (b % 8);
	}
}

/* Blow every set bit of buf into the fuses from byte_addr */
uint32_t drv_fuse_write_onerow(const uint8_t *buf, uint32_t byte_addr, uint32_t len)
{
	uint32_t i, bit, addr, cmd, rd;

	for (i = 0; i < len; i++) {
		for (bit = 0; bit < 8; bit++) {
			if (!(buf[i] & BIT(bit)))
				continue;
			addr = (byte_addr + i) * 8 + bit;
			/* row = addr[12:5], column = addr[4:0], bank = addr[13] */
			addr = (addr & 0x1f) << 8 | (addr & 0x1fff) >> 5 | (addr > 0x1fff) << 13;
			cmd = EFUSE_PROG_START | addr << 12;
			REG32(EFUSE_PROG) = cmd;
			rd = REG32(EFUSE_PROG);
			if ((cmd ^ rd) & 0x3fff000) {
				hal_printf_level(LOG_ERR, "fuse macro write bit addr:0x%08x\n",
						 (unsigned)(addr << 12));
				hal_printf_level(LOG_ERR, "fuse macro read bit addr:0x%08x\n",
						 (unsigned)(rd & 0x3fff000));
				return 0xffffffff;
			}
			while (!(REG8(EFUSE_PROG) & EFUSE_PROG_DONE))
				;
			REG32(EFUSE_PROG) = EFUSE_PROG_DONE;
			REG32(EFUSE_PROG) = 0;
		}
	}
	return 0;
}

/* Reload the fuse SRAM from the array, one 32-bit row at a time */
void drv_fuse_re_sense_one_row(uint32_t addr, uint32_t size)
{
	uint32_t bit = (addr & ~3u) * 8, end = (addr + size) * 8 & ~31u;

	for (; bit < end; bit += 32) {
		REG32(EFUSE_PROG) = 0x20;
		REG32(EFUSE_PROG) = (bit & 0x1fe0) << 7 | (bit > 0x1fff) << 25 | EFUSE_SENSE_START;
		while (!(REG32(EFUSE_PROG) & EFUSE_SENSE_DONE))
			;
		REG32(EFUSE_PROG) = 0;
	}
}
