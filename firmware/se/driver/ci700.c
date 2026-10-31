/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Arm CoreLink CI-700 coherent interconnect (CIX driver/ci700/v1/drv_ci700.c).
 *
 * Node configuration registers sit at node base + 0x10000 (SoC address;
 * SE address + 0x40000000).  Requesters' system address maps (RN-SAM)
 * route the address space; the home nodes (HN-F) own DRAM, the SLC and
 * MTE tag storage, which is carved from the top of DRAM.
 */
#include "se.h"
#include "ci700.h"
#include "ddr.h"
#include "platform.h"
#include "storage.h"

#define NODE(base, reg)		((base) + 0x40010000u + (reg))
#define RTL_REV_B		0x5bd5

/* 64-bit registers written low word first, as CIX does */
static void wr64(uint32_t base, uint32_t reg, uint64_t v)
{
	REG32(NODE(base, reg)) = (uint32_t)v;
	REG32(NODE(base, reg + 4)) = (uint32_t)(v >> 32);
}

/* Read-modify-write of a 64-bit register's low word (high word read first) */
static void rmw64_lo(uint32_t base, uint32_t reg, uint32_t clr, uint32_t set)
{
	uint32_t hi = REG32(NODE(base, reg + 4));

	REG32(NODE(base, reg)) = (REG32(NODE(base, reg)) & ~clr) | set;
	REG32(NODE(base, reg + 4)) = hi;
}

/* Clean and invalidate [start, start + size) from one HN-F's SLC */
static void __drv_ci700_slc_flush(uint32_t hnf, uint64_t start, uint64_t size)
{
	uint32_t n = 1000000, st;

	if (REG32(NODE(hnf, 0xa08)) & 0x2800)
		return;
	wr64(hnf, 0xf50, start);
	wr64(hnf, 0xf58, start + size);
	rmw64_lo(hnf, 0xf60, 0, 1);
	do {
		st = REG32(NODE(hnf, 0xf68));
		hal_udelay(1);
		if (st & 1)
			return;
	} while (n--);
	hal_printf_level(LOG_ERR, "slc timeout!!!\n");
}

/* DRAM above 8 GiB of address space is reached through its alias at 512 GiB */
static void _drv_ci700_slc_flush(uint32_t hnf, uint64_t start, uint64_t size)
{
	const uint64_t split = 0x200000000ull, alias = 0x8000000000ull;

	if (start + size <= split) {
		__drv_ci700_slc_flush(hnf, start, size);
		return;
	}
	__drv_ci700_slc_flush(hnf, start, split - start);
	__drv_ci700_slc_flush(hnf, alias, start + size - split);
}

/* HN-F nodes by number of DRAM channels in use */
static unsigned int hnf_nodes(uint32_t ch, uint32_t base_lo, uint32_t base_hi, uint32_t *nodes)
{
	if (ch == 2) {
		if (plat_get_plat_id() == PLAT_FPGA) {
			nodes[0] = base_lo, nodes[1] = base_lo + 0x400000;
		} else {
			nodes[0] = base_hi, nodes[1] = base_hi + 0x400000;
		}
		return 2;
	}
	if (ch == 1) {
		nodes[0] = base_hi + 0x400000;
		return 1;
	}
	nodes[0] = base_lo, nodes[1] = base_lo + 0x400000;
	nodes[2] = base_hi, nodes[3] = base_hi + 0x400000;
	return 4;
}

void drv_ci700_slc_flush(uint64_t start, uint64_t size)
{
	uint32_t nodes[4];
	unsigned int i, n = hnf_nodes(drv_ddr_get_ch_num(), 0x10380000, 0x11380000, nodes);

	for (i = 0; i < n; i++)
		_drv_ci700_slc_flush(nodes[i], start, size);
}

static void _drv_ci700_mte_tag_flush(uint32_t mtu)
{
	uint32_t n;

	wr64(mtu, 0xa30, 3);
	if (REG32(NODE(mtu, 0xa38)) & 1)
		return;
	hal_udelay(1);
	for (n = 500; n; n--) {
		if (REG32(NODE(mtu, 0xa38)) & 1)
			return;
		hal_udelay(1);
	}
	hal_printf_level(LOG_ERR, "tag cache flush timeout!!!");
}

void drv_ci700_flush_mte_tagcache(void)
{
	uint32_t nodes[4];
	unsigned int i, n = hnf_nodes(drv_ddr_get_ch_num(), 0x10210000, 0x11210000, nodes);

	for (i = 0; i < n; i++)
		_drv_ci700_mte_tag_flush(nodes[i]);
}

/* MTE tag unit: tags for DRAM (capped at 30 GiB) below its top 64 MiB */
static void mtu_init(uint32_t node, uint32_t hnf_cnt)
{
	uint64_t bytes, cap = 0x780000000ull;

	if (!hal_secfg_get_mte_config())
		return;
	rmw64_lo(node, 0xa08, 3, 0);
	wr64(node, 0xa40, 0);
	bytes = (uint64_t)drv_ddr_get_mem_size() << 20;
	if (bytes > cap)
		bytes = cap;
	wr64(node, 0xa48, bytes + 0x7c000000 - (bytes >> 5));
	if (hnf_cnt == 2 || hnf_cnt == 4) {
		uint32_t v = hnf_cnt == 2 ? 0x11111111 : 0x22222222;

		REG32(NODE(node, 0xa50)) = v & ~0xfu;
		REG32(NODE(node, 0xa54)) = v;
		REG32(NODE(node, 0xa58)) = v;
		REG32(NODE(node, 0xa5c)) = v;
		REG32(NODE(node, 0xa60)) = v;
		REG32(NODE(node, 0xa64)) = v & 0xffffff;
	}
}

static void rnsam_init(uint32_t node)
{
	static const uint32_t regions[] = {
		5, 0x3000000, 0x20000005, 0x2000000, 0x30000005, 0x2000000, 0x40000005, 0x4000000,
		5, 0x9000008, 5, 0x9000010, 5, 0x8000018, 0x80000005, 0x300001c, 0xa0000005,
		0x200001c, 0xb0000005, 0x200001c,
	};
	uint32_t rev = plat_get_rtl_revision(), ch = drv_ddr_get_ch_num(), sn, sn_hi, cnt, i;

	REG32(NODE(node, 0xe00)) = 1;
	REG32(NODE(node, 0xe04)) = 0xe000000;
	if (ch == 1) {
		cnt = 1, sn = 0x2a, sn_hi = 0;
	} else if (ch == 2) {
		cnt = 2, sn = plat_get_plat_id() == PLAT_FPGA ? 0xa002 : 0x2a022, sn_hi = 0;
	} else {
		cnt = 4, sn = 0x2200a002, sn_hi = 0x2a0;
	}
	REG32(NODE(node, 0xf00)) = sn;
	REG32(NODE(node, 0xf04)) = sn_hi;
	REG32(NODE(node, 0xea0)) = cnt;
	REG32(NODE(node, 0xea4)) = 0;
	for (i = 0; i < ARRAY_SIZE(regions); i++)
		REG32(NODE(node, 0xc00 + i * 4)) = regions[i];
	REG32(NODE(node, 0xd80)) = 0x68068006;
	REG32(NODE(node, 0xd84)) = 0x680;
	REG32(NODE(node, 0xd88)) = 0x68068068;
	REG32(NODE(node, 0xd8c)) = 0x440;
	if (rev < RTL_REV_B) {
		REG32(NODE(node, 0xd90)) = 0x6e044;
		REG32(NODE(node, 0xd94)) = 0;
	} else {
		REG32(NODE(node, 0xd90)) = 0x64044;
		REG32(NODE(node, 0xd94)) = 0;
		REG32(NODE(node, 0xe80)) = 0xffffff80;
		REG32(NODE(node, 0xe84)) = 0xffffffff;
	}
	REG32(NODE(node, 0x1100)) = 2;
	REG32(NODE(node, 0x1104)) = 0x440000;
}

static void hnfsam_init(uint32_t node, uint16_t target_sn)
{
	if (plat_get_rtl_revision() >= RTL_REV_B)
		wr64(node, 0xcf0, 0xffffffffffffff80ull);
	wr64(node, 0xd00, target_sn);
	if (hal_secfg_get_mte_config())
		rmw64_lo(node, 0xa00, BIT(24), 0);
	else
		rmw64_lo(node, 0xa00, 0, BIT(24));
	{
		uint32_t hi = REG32(NODE(node, 0xa0c));

		REG32(NODE(node, 0xa08)) &= 0xffffd7ff;
		REG32(NODE(node, 0xa0c)) = hi & 0xfffffbff;
	}
	if (MemGetBootEnv() == 1) {
		hal_printf_level(LOG_ERR, "[SE_FW]%s-enable ocm mode\n", __func__);
		rmw64_lo(node, 0xa00, 0, 0x600);
	}
}

static void _drv_ci700_qos_init(uint32_t rni, uint32_t qos)
{
	uint32_t v = qos << 20 | qos << 16 | 0xc;

	REG32(NODE(rni, 0xa80)) = v;
	REG32(NODE(rni, 0xa84)) = 0;
	REG32(NODE(rni, 0xaa0)) = v;
	REG32(NODE(rni, 0xaa4)) = 0;
}

/* Address-based DRAM interleave: bits dropped from the channel address */
static void drv_ddr_bit_drop_cfg(uint32_t drop)
{
	uint32_t v = drop << 9 | drop << 3, i;

	for (i = 0; i < 4; i++)
		REG32(0x4c000300 + i * 0x20000) = (REG32(0x4c000300 + i * 0x20000) & 0xfffff9e7) | v;
}

uint32_t drv_ci700_update_mem_size(void)
{
	uint32_t mb = drv_ddr_get_mem_size();

	if (hal_secfg_get_mte_config()) {
		if (mb >= 0x7800)
			mb = 0x7800;
		mb = mb - 0x40 - mb / 32;
	}
	drv_ddr_set_avail_mem_size(mb);
	return 0;
}

/* Port and system cache settings both passes program */
static void ci700_common(uint32_t rev)
{
	uint32_t b = 0x501a0000;

	REG32(b + 0xc00) = 0x11;
	REG32(b + 0xc04) = 0x60000000;
	REG32(b + 0x3380a00) |= 0x20;
	REG32(b + 0x3380a04) = REG32(b + 0x3380a04);
	REG32(b + 0x33f0a00) |= 0x20;
	REG32(b + 0x33f0a04) = REG32(b + 0x33f0a04);
	REG32(b + 0x3370c00) = 0x16;
	REG32(b + 0x3370c04) = 0x70000000;
	REG32(b + 0x3370c08) = 0x1110;
	REG32(b + 0x3370c0c) = 0xf0000002;
	REG32(b + 0x3370c10) = 0x13;
	REG32(b + 0x3370c14) = 0xf0000004;
	REG32(0x52160c00) = 0x10;
	REG32(0x52160c04) = 0x60000000;
	if (rev < RTL_REV_B) {
		REG32(0x535a0c00) = 0x10;
		REG32(0x535a0c04) = 0x60000000;
	} else {
		REG32(0x53120c00) = 0x10;
		REG32(0x53120c04) = 0x60000000;
	}
}

static void rnsam_all(uint32_t rev)
{
	uint32_t i;

	rnsam_init(0x100a0000);
	rnsam_init(0x104a0000);
	rnsam_init(0x110a0000);
	rnsam_init(0x114a0000);
	_drv_ci700_qos_init(0x12040000, 0xc);
	_drv_ci700_qos_init(0x12440000, 0xc);
	for (i = 0; i < 4; i++) {
		uint32_t b = 0x50030000 + (i & 1) * 0x400000 + (i >> 1) * 0x1000000;

		REG32(b + 0xa80) = 0xd0004;
		REG32(b + 0xa84) = 0;
	}
	rnsam_init(0x12080000);
	rnsam_init(0x12480000);
	rnsam_init(0x12490000);
	rnsam_init(0x13200000);
	rnsam_init(0x13280000);
	if (rev < RTL_REV_B)
		rnsam_init(0x13210000);
	rnsam_init(0x13680000);
	rnsam_init(0x13610000);
}

void drv_ci700_init_pre(void)
{
	uint32_t rev = plat_get_rtl_revision();

	hal_printf_level(LOG_INFO, "drv_ci700_pre_start ...\n");
	ci700_common(rev);
	rnsam_all(rev);
	hal_printf_level(LOG_INFO, "drv_ci700_pre_end  ...\n");
}

void drv_ci700_init(void)
{
	static const uint16_t hnf_sn[4] = { 4, 0xc, 0x24, 0x2c };
	uint32_t rev = plat_get_rtl_revision(), ch = drv_ddr_get_ch_num(), nodes[4], i, n;

	hal_printf_level(LOG_INFO, "drv_ci700_start ...\n");
	n = hnf_nodes(ch, 0x10210000, 0x11210000, nodes);
	for (i = 0; i < n; i++)
		mtu_init(nodes[i], ch == 1 ? 1 : n);
	ci700_common(rev);
	n = hnf_nodes(ch, 0x10380000, 0x11380000, nodes);
	for (i = 0; i < n; i++) {
		/* target: by node, 4 0xc 0x24 0x2c for the four HN-Fs */
		uint32_t k = (nodes[i] >= 0x11380000 ? 2 : 0) + ((nodes[i] >> 22) & 1);

		hnfsam_init(nodes[i], hnf_sn[k]);
	}
	rnsam_all(rev);
	if (plat_get_ddr_model()) {
		if (ch == 2)
			drv_ddr_bit_drop_cfg(DDR_BIT_DROP_1_BIT_ON_BIT7);
		else if (ch == 1)
			drv_ddr_bit_drop_cfg(DDR_BIT_DROP_NONE);
		else
			drv_ddr_bit_drop_cfg(DDR_BIT_DROP_2_BITS);
	}
	hal_printf_level(LOG_INFO, "drv_ci700_end  ...\n");
}
