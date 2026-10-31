/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * eFuse fields (CIX hal/common/hal_efuse.c, hal_efuse_dist.c): read a field,
 * blow it, or override it in the fuse SRAM (the boot-time copy every block
 * reads) or in the copy the PM keeps.
 */
#include "se.h"
#include "efuse.h"

/* PM's copy of the fuse array (SoC 0x06ace4d8) */
#define PM_FUSE_RAM	0x46ace4d8u

sec_efuse_t g_handle_efuse;

uint32_t sec_efuse_init(sec_efuse_t *efuse)
{
	return 0;
}

uint32_t sec_efuse_read(sec_efuse_t *efuse, uint32_t addr, uint32_t offset, uint32_t size, void *data)
{
	uint8_t buf[128] = { 0 };
	uint32_t err;

	err = drv_fuse_read(buf, addr, (offset + size + 7) / 8);
	if (err) {
		SE_ERR(err);
		return err;
	}
	drv_fuse_align_for_read(offset, buf, size, data);
	return 0;
}

uint32_t sec_efuse_program(sec_efuse_t *efuse, uint32_t addr, uint32_t offset, uint32_t size,
			   const void *data)
{
	uint8_t buf[128] = { 0 }, first;
	uint32_t err;

	err = drv_fuse_read(&first, addr, 1);
	if (err) {
		SE_ERR(err);
		return err;
	}
	drv_fuse_align_for_write(offset, data, size, buf);
	buf[0] |= first;
	err = drv_fuse_write_onerow(buf, addr, (size + 7 + offset) / 8);
	if (err)
		SE_ERR(err);
	return err;
}

/*
 * The bytes of the field, from addr, with the bits around it (below offset
 * in the first byte, above the end in the last) as the fuses hold them.
 */
static uint32_t field_bytes(uint32_t (*rd)(uint8_t *, uint32_t, uint32_t), uint32_t addr,
			    uint32_t offset, uint32_t size, const void *data, uint8_t *buf)
{
	uint32_t count = (size + offset + 7) / 8, err, tail = (size + offset) & 7;
	uint8_t first = 0, last = 0;

	if (data == NULL || size == 0) {
		SE_ERR(EFUSE_ERR_PARAM);
		return EFUSE_ERR_PARAM;
	}
	err = rd(&first, addr, 1);
	if (!err)
		err = rd(&last, addr + count - 1, 1);
	if (err) {
		SE_ERR(err);
		return err;
	}
	drv_fuse_align_for_write(offset, data, size, buf);
	buf[0] |= first & (offset >= 8 ? 0xff : BIT(offset) - 1);
	buf[count - 1] |= tail ? last & ~(BIT(tail) - 1) : 0;
	return 0;
}

uint32_t sec_efuse_program_sram(sec_efuse_t *efuse, uint32_t addr, uint32_t offset, uint32_t size,
				const void *data)
{
	uint8_t buf[128] = { 0 };
	uint32_t err = field_bytes(drv_fuse_read, addr, offset, size, data, buf);

	if (!err)
		memcpy((void *)(SE_EFUSE_BASE + addr), buf, (size + offset + 7) / 8);
	return err;
}

/* The same, in whole 32-bit words (for fuse SRAM ranges that take no byte writes) */
uint32_t sec_efuse_program_sram_by_word(sec_efuse_t *efuse, uint32_t addr, uint32_t offset,
					uint32_t size, const void *data)
{
	uint8_t buf[128] = { 0 }, words[136];
	uint32_t count = (size + offset + 7) / 8, start = addr & ~3u, i;
	uint32_t len = ((addr + count - 1) & ~3u) + 4 - start;
	uint32_t err = field_bytes(drv_fuse_read, addr, offset, size, data, buf);

	if (err)
		return err;
	err = drv_fuse_read(words, start, len);
	if (err) {
		SE_ERR(err);
		return err;
	}
	memcpy(words + (addr & 3), buf, count);
	for (i = 0; i < len; i += 4) {
		uint32_t w;

		memcpy(&w, words + i, 4);
		REG32(SE_EFUSE_BASE + start + i) = w;
	}
	return 0;
}

static uint32_t drv_fuse_read_dist(uint8_t *buf, uint32_t offset, uint32_t len)
{
	if (len > 0x800) {
		SE_ERR(EFUSE_ERR_PARAM);
		return EFUSE_ERR_PARAM;
	}
	memcpy(buf, (const void *)(PM_FUSE_RAM + offset), len);
	return 0;
}

uint32_t sec_efuse_program_sram_dist(sec_efuse_t *efuse, uint32_t addr, uint32_t offset,
				     uint32_t size, const void *data)
{
	uint8_t buf[128] = { 0 };
	uint32_t err = field_bytes(drv_fuse_read_dist, addr, offset, size, data, buf);

	if (!err)
		memcpy((void *)(PM_FUSE_RAM + addr), buf, (size + offset + 7) / 8);
	return err;
}
