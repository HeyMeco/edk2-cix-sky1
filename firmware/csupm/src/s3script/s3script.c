/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The S3 boot script (CIX's Source/common/s3script/s3script.c).
 *
 * The table (BootScriptInternalFormat.h): a header (0xaa, its length 12,
 * version, the table's length), then entries: 0x00 write (width 8/16/32/64
 * bits as 0-3, count, 64-bit address, the data), 0x01 read-modify-write
 * (width, address, data, mask: data | (value & mask)), 0xff the end (2
 * bytes, the table's last).  Addresses below 0x20000000 are SoC registers,
 * through the PM's window; others are DRAM, through TLB window 6.  The
 * table itself is read through TLB window 5.  CIX keeps the checksum UEFI
 * gives but does not check it.
 */
#include "s3script/s3script.h"
#include "rtt/rtt.h"
#include "soc.h"

#define TLB_TABLE		5
#define TLB_DATA		6
#define OP_WRITE		0x00
#define OP_READ_WRITE		0x01
#define OP_TABLE		0xaa
#define OP_END			0xff

static uint64_t s3script_mem_base, s3script_checksum;

bool s3script_register_table(uint32_t base_lo, uint32_t base_hi, uint32_t crc_lo, uint32_t crc_hi)
{
	s3script_mem_base = (uint64_t)base_hi << 32 | base_lo;
	s3script_checksum = (uint64_t)crc_hi << 32 | crc_lo;
	return true;
}

void s3script_unregister_table(void)
{
	s3script_mem_base = 0;
	s3script_checksum = 0;
}

bool s3script_has_table(void)
{
	return s3script_mem_base != 0;
}

static uint32_t le32(const volatile uint8_t *p)
{
	return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t le64(const volatile uint8_t *p)
{
	return le32(p) | (uint64_t)le32(p + 4) << 32;
}

/* the header, and the end entry where the length says */
bool s3script_check_table(void)
{
	const volatile uint8_t *p;
	bool ok = false;

	if (!s3script_mem_base)
		return false;
	p = (const volatile uint8_t *)(uintptr_t)tlb_ctrl_dram_to_syshub_by_specific_entry(
		s3script_mem_base, TLB_TABLE, false);
	if (p && p[0] == OP_TABLE && p[1] == 12) {
		uint32_t len = le32(p + 4);

		ok = p[len - 2] == OP_END && p[len - 1] == 2;
	}
	tlb_ctrl_free_entry(TLB_TABLE);
	return ok;
}

static bool is_soc(uint64_t addr)
{
	return addr <= 0x1fffffff;
}

/* CIX's MmioRead* and MmioWrite* */
static uint64_t mmio_read(uint64_t addr, unsigned width)
{
	uint64_t v = 0;
	volatile uint8_t *p;

	if (is_soc(addr)) {
		uint32_t a = (uint32_t)addr;

		if (width == 3)
			return soc_read64(a & ~3u);
		v = soc_read32(a & ~3u) >> (8 * (a & 3));
		return width == 0 ? v & 0xff : width == 1 ? v & 0xffff : v;
	}
	p = (volatile uint8_t *)(uintptr_t)tlb_ctrl_dram_to_syshub_by_specific_entry(addr, TLB_DATA, false);
	if (!p)
		return 0;
	switch (width) {
	case 0:
		v = *p;
		break;
	case 1:
		v = *(volatile uint16_t *)p;
		break;
	case 2:
		v = *(volatile uint32_t *)p;
		break;
	default:
		v = (uint64_t)((volatile uint32_t *)p)[1] << 32 | ((volatile uint32_t *)p)[0];
		break;
	}
	tlb_ctrl_free_entry(TLB_DATA);
	return v;
}

static void mmio_write(uint64_t addr, unsigned width, uint64_t v)
{
	volatile uint8_t *p;

	if (is_soc(addr)) {
		uint32_t a = (uint32_t)addr, sh = 8 * (a & 3), m;

		if (width == 3) {
			soc_write64(a & ~3u, v);
			return;
		}
		if (width == 2) {
			soc_write32(a & ~3u, (uint32_t)v);
			return;
		}
		m = (width == 0 ? 0xffu : 0xffffu) << sh;
		soc_write32(a & ~3u, (soc_read32(a & ~3u) & ~m) | (((uint32_t)v << sh) & m));
		return;
	}
	p = (volatile uint8_t *)(uintptr_t)tlb_ctrl_dram_to_syshub_by_specific_entry(addr, TLB_DATA, false);
	if (!p)
		return;
	switch (width) {
	case 0:
		*p = (uint8_t)v;
		break;
	case 1:
		*(volatile uint16_t *)p = (uint16_t)v;
		break;
	case 2:
		*(volatile uint32_t *)p = (uint32_t)v;
		break;
	default:
		((volatile uint32_t *)p)[0] = (uint32_t)v;
		((volatile uint32_t *)p)[1] = (uint32_t)(v >> 32);
		break;
	}
	tlb_ctrl_free_entry(TLB_DATA);
}

/* CIX's do_mem_write: count values from address on; the entry's length */
static uint32_t do_mem_write(const volatile uint8_t *op)
{
	unsigned width = op[2], size = 1u << width;
	uint64_t addr = le64(op + 4);
	const volatile uint8_t *d = op + 12;

	if (width > 3)
		return 0;
	for (unsigned n = op[3]; n; n--, d += size, addr += size) {
		uint64_t v = 0;

		for (unsigned b = 0; b < size; b++)
			v |= (uint64_t)d[b] << (8 * b);
		mmio_write(addr, width, v);
	}
	return op[1];
}

/* CIX's do_mem_read_write */
static uint32_t do_mem_read_write(const volatile uint8_t *op)
{
	unsigned width = op[2], size = 1u << width;
	uint64_t addr = le64(op + 3), data = 0, mask = 0;

	if (width > 3)
		return 0;
	for (unsigned b = 0; b < size; b++) {
		data |= (uint64_t)op[11 + b] << (8 * b);
		mask |= (uint64_t)op[11 + size + b] << (8 * b);
	}
	mmio_write(addr, width, (mmio_read(addr, width) & mask) | data);
	return op[1];
}

bool s3script_execute_table(void)
{
	const volatile uint8_t *p;
	uint32_t len, i;

	if (!s3script_has_table())
		return false;
	p = (const volatile uint8_t *)(uintptr_t)tlb_ctrl_dram_to_syshub_by_specific_entry(
		s3script_mem_base, TLB_TABLE, false);
	if (!p)
		return false;
	len = le32(p + 4);
	for (i = p[1]; i < len;) {
		uint32_t step;

		if (p[i] == OP_END) {
			i += 2;
			break;
		}
		if (p[i] == OP_READ_WRITE)
			step = do_mem_read_write(p + i);
		else if (p[i] == OP_WRITE)
			step = do_mem_write(p + i);
		else
			break;
		if (!step)
			break;
		i += step;
	}
	tlb_ctrl_free_entry(TLB_TABLE);
	return true;
}
