/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * SRAM repair (CIX driver/memory/drv_mem_repair.c, hal/common/hal_memory.c,
 * framework/fw_memory.c).
 *
 * Production test leaves repair data for each block's SRAMs in the eFuse:
 * a directory of (group, rows) entries at 0x564 and the rows from 0x58c.
 * Each block's repair controller takes its data in 16-byte chunks and
 * either repairs right away (with the repair groups enabled) or keeps the
 * data until the block powers up ("distribute").
 */
#include "se.h"
#include "btcfg.h"
#include "efuse.h"
#include "memory.h"
#include "platform.h"

#define FUSE_MEMR_DIR		(SE_EFUSE_BASE + 0x564)
#define FUSE_MEMR_DIR_END	(SE_EFUSE_BASE + 0x58c)
#define FUSE_MEMR_ROW(r)	(SE_EFUSE_BASE + 0x588 + (r) * 4)
#define S5_SW_MEMR_DONE		0x56000504u	/* bit per group: data distributed */
#define S5_SW_REG6		0x56000518u	/* bit 0: S5 domain kept its repair (suspend) */

#define MEMR_DATA(b, i)		((b) + 0x40000000 + (i) * 4)
#define MEMR_CTRL(b)		((b) + 0x40000010)	/* [7:4] chunk, 3 received, 2 failed, 1 repaired, 0 start */
#define MEMR_ENABLE(b)		((b) + 0x40000014)
#define MEMR_STATUS(b)		((b) + 0x40000018)
#define MEMR_POLL		20000000

#define MEMR_ERR_ORDER		0xa601
#define MEMR_ERR_ID		0xa603
#define MEMR_ERR_INDEX		0xa604
#define MEMR_ERR_LEN		0x9806

typedef struct {
	uint32_t id;
	uint32_t domain;
	uint32_t base;		/* SoC address of the repair controller */
	uint32_t max_rows;
	const uint8_t *data;
	uint32_t len;
	uint32_t index;		/* repair groups present */
	uint32_t enable;
	uint32_t done;
} memr_group_t;

static memr_group_t memr_table[21] = {
	{ MEMR_GROUP_ID_CPU, 0x22, 0x0fc50000, 32, NULL, 0, 0x300, 1 },
	{ MEMR_GROUP_ID_CI700, 0x23, 0x10000000, 16, NULL, 0, 0x1, 1 },
	{ MEMR_GROUP_ID_DPU, 0x24, 0x14000000, 16, NULL, 0, 0x1f, 1 },
	{ MEMR_GROUP_ID_GPU, 0x34, 0x15000000, 16, NULL, 0, 0x1, 1 },
	{ MEMR_GROUP_ID_NPU, 0x2f, 0x14250000, 16, NULL, 0, 0xf, 1 },
	{ MEMR_GROUP_ID_AUDIO, 0x03, 0x07000000, 4, NULL, 0, 0x1, 1 },
	{ MEMR_GROUP_ID_ISP, 0x32, 0x14330000, 8, NULL, 0, 0x1, 1 },
	{ MEMR_GROUP_ID_VPU, 0x2e, 0x14230000, 8, NULL, 0, 0x1f, 1 },
	{ MEMR_GROUP_ID_CSU_PM, 0x02, 0x06000000, 4, NULL, 0, 0x3, 1 },
	{ MEMR_GROUP_ID_CSU_SE, 0x01, 0xb0030200, 4, NULL, 0, 0x3, 1 },
	{ MEMR_GROUP_ID_MMHUB, 0x1e, 0x0d030000, 4, NULL, 0, 0x1, 1 },
	{ MEMR_GROUP_ID_PCIE, 0x0f, 0x0a000000, 4, NULL, 0, 0x3, 1 },
	{ MEMR_GROUP_ID_SENSOR, 0x04, 0x08000000, 2, NULL, 0, 0x1, 1 },
	{ MEMR_GROUP_ID_DP, 0x25, 0x14050000, 2, NULL, 0, 0x1, 1 },
	{ MEMR_GROUP_ID_GMAC, 0x0e, 0x09310000, 2, NULL, 0, 0x1, 1 },
	{ MEMR_GROUP_ID_MIPI, 0x30, 0x14270000, 2, NULL, 0, 0x1, 1 },
	{ MEMR_GROUP_ID_SMMU_MM, 0x13, 0x0b1a0000, 2, NULL, 0, 0x1, 1 },
	{ MEMR_GROUP_ID_SMMU_PCIE, 0x11, 0x0b000000, 2, NULL, 0, 0x1, 1 },
	{ MEMR_GROUP_ID_SMMU_SYS, 0x12, 0x0b0d0000, 2, NULL, 0, 0x1, 1 },
	{ MEMR_GROUP_ID_USB, 0x0a, 0x09250000, 2, NULL, 0, 0x1, 1 },
	{ MEMR_GROUP_ID_FCH, 0x00, 0x00000000, 1, NULL, 0, 0x1, 1 },
};

#define G(id)	(&memr_table[(id) - 1])

static void memr_wait_chunk(uint32_t base)
{
	uint32_t n = MEMR_POLL;

	while (--n && (REG32(MEMR_CTRL(base)) & 0xf0))
		;
}

/*
 * Send len bytes in max_len / 16 chunks (at least one), the data's whole
 * words then zeros; the controller takes a chunk when CTRL[7:4] is set.
 */
static void drv_memr_recv_start(uint32_t base, const uint8_t *data, uint32_t len, uint32_t max_len)
{
	uint32_t chunks = max_len > 16 ? max_len / 16 : 1, words = len / 4, k, i, w;

	for (k = 0; k < chunks; k++) {
		if (k)
			memr_wait_chunk(base);
		for (i = 0; i < 4; i++) {
			w = 0;
			if (k * 4 + i < words)
				memcpy(&w, data + (k * 4 + i) * 4, 4);
			REG32(MEMR_DATA(base, i)) = w;
		}
		REG32(MEMR_CTRL(base)) |= 0xf0;
	}
}

static uint32_t memr_wait(uint32_t base, uint32_t bit)
{
	uint32_t n = MEMR_POLL;

	while (--n)
		if (REG32(MEMR_CTRL(base)) & bit)
			return 0;
	return 0xffffffff;
}

static uint32_t memr_send(memr_group_t *g)
{
	uint32_t err;

	if (g->len == 0)
		return 0;
	if (g->len > 0x80)
		return MEMR_ERR_LEN;
	hexdump("MEMR data", g->data, g->len);
	if (!g->enable) {
		SE_INFO("group_id:%d disabled!\n", (int)g->id);
		return 0;
	}
	REG32(MEMR_CTRL(g->base)) = 1;
	drv_memr_recv_start(g->base, g->data, g->len, g->max_rows * 4);
	err = memr_wait(g->base, BIT(3));
	if (err)
		SE_ERR(err);
	return err;
}

/* Repair now, with the given repair groups (0: all of the block's) */
uint32_t hal_memr_by_id(MEMR_GROUP_ID id, uint32_t index)
{
	memr_group_t *g;
	uint32_t err;

	if (id > MEMR_GROUP_ID_MAX) {
		hal_printf_level(LOG_ERR, "# Invalid group id: %d\n", id);
		return MEMR_ERR_ID;
	}
	if (id == MEMR_GROUP_ID_CSU_SE) {
		SE_INFO("CSU_SE SRAM already repaired in bootrom!\n");
		return 0;
	}
	g = G(id);
	SE_INFO("group_id:%d\n", (int)g->id);
	if (g->len == 0 || g->len > 0x80 || !g->enable)
		return memr_send(g);
	err = memr_send(g);
	if (err)
		return err;
	SE_INFO("Fetch done\n");
	if (index == 0) {
		SE_INFO("available index:0x%x\n", (unsigned)g->index);
		index = g->index;
	} else {
		SE_INFO("input index:0x%x, available index:0x%x\n", (unsigned)index, (unsigned)g->index);
		if (!(index & g->index))
			return MEMR_ERR_INDEX;
	}
	REG32(MEMR_ENABLE(g->base)) = (uint16_t)index;
	g->done = 1;
	SE_INFO("wait done, group id:%d\n", id);
	if (memr_wait(g->base, BIT(1)) || memr_wait(g->base, BIT(2))) {
		hal_printf_level(LOG_ERR, "Err: 0x%x\n", (unsigned)REG32(MEMR_STATUS(g->base)));
		if (id == MEMR_GROUP_ID_GPU)
			REG32(0x5500030c) |= 0xf;
		return 0xffffffff;
	}
	if (id == MEMR_GROUP_ID_GPU)
		REG32(0x5500030c) &= 0xfffffff0;
	return 0;
}

/* Hand the data to the block's controller; it repairs when the block powers up */
uint32_t hal_memr_distribute_by_id(MEMR_GROUP_ID id, uint32_t *valid)
{
	memr_group_t *g;
	uint32_t err;

	*valid = 0;
	if (id > MEMR_GROUP_ID_MAX) {
		hal_printf_level(LOG_ERR, "# Invalid group id: %d\n", id);
		return MEMR_ERR_ID;
	}
	if (id == MEMR_GROUP_ID_CSU_SE) {
		SE_INFO("CSU_SE SRAM already repaired in bootrom!\n");
		return 0;
	}
	g = G(id);
	SE_INFO("group_id:%d\n", (int)g->id);
	if (g->len == 0) {
		SE_INFO("No data in fuse!\n");
		return 0;
	}
	if (g->len > 0x80 || !g->enable)
		return memr_send(g);
	err = memr_send(g);
	if (err)
		return err;
	SE_INFO("memrepair id:%d distribute data done.\n", id);
	*valid = 1;
	REG32(S5_SW_MEMR_DONE) |= BIT(id);
	return 0;
}

static void disable_unused(void)
{
	uint32_t i0, i1, i2, i3, i4;

	if (fw_efuse_get_info(FUSE_ID_NPU0_HARVEST, &i0) || fw_efuse_get_info(FUSE_ID_NPU1_HARVEST, &i1))
		return;
	if (i0 == 1)
		G(MEMR_GROUP_ID_NPU)->index &= 0xfffffff1;
	else if (i1 == 1)
		G(MEMR_GROUP_ID_NPU)->index &= 0xfffffff3;
	if (fw_efuse_get_info(FUSE_ID_ISP_HARVEST, &i0))
		return;
	if (i0 == 1)
		G(MEMR_GROUP_ID_ISP)->enable = 0;
	if (fw_efuse_get_info(FUSE_ID_DPU0_HARVEST, &i0) || fw_efuse_get_info(FUSE_ID_DPU1_HARVEST, &i1) ||
	    fw_efuse_get_info(FUSE_ID_DPU2_HARVEST, &i2) || fw_efuse_get_info(FUSE_ID_DPU3_HARVEST, &i3) ||
	    fw_efuse_get_info(FUSE_ID_DPU4_HARVEST, &i4))
		return;
	if (i0 == 1 && i1 == 1 && i2 == 1 && i3 == 1 && i4 == 1) {
		G(MEMR_GROUP_ID_DPU)->enable = 0;
	} else {
		G(MEMR_GROUP_ID_DPU)->index &= ~((i0 == 1) | (i1 == 1) << 1 | (i2 == 1) << 2 |
						 (i3 == 1) << 3 | (i4 == 1) << 4);
	}
	if (fw_efuse_get_info(FUSE_ID_PCIE_X8_HARVEST, &i0)) {
		SE_ERR(0);
		return;
	}
	if (i0 == 1)
		G(MEMR_GROUP_ID_PCIE)->index &= ~1u;
	if (fw_efuse_get_info(FUSE_ID_AUDIO_SS_HARVEST, &i0)) {
		SE_ERR(0);
		return;
	}
	if (i0 == 1)
		G(MEMR_GROUP_ID_AUDIO)->enable = 0;
	if (!plat_get_dpu_model())
		G(MEMR_GROUP_ID_DPU)->enable = 0;
	if (!plat_get_gpu_model())
		G(MEMR_GROUP_ID_GPU)->enable = 0;
	if (!plat_get_npu_model())
		G(MEMR_GROUP_ID_NPU)->enable = 0;
	if (!plat_get_audio_model())
		G(MEMR_GROUP_ID_AUDIO)->enable = 0;
	if (!plat_get_isp_model())
		G(MEMR_GROUP_ID_ISP)->enable = 0;
	if (!plat_get_vpu_model())
		G(MEMR_GROUP_ID_VPU)->enable = 0;
	if (!plat_get_mmhub_model())
		G(MEMR_GROUP_ID_MMHUB)->enable = 0;
	if (!plat_get_pcie_model())
		G(MEMR_GROUP_ID_PCIE)->enable = 0;
	if (!plat_get_sensor_model())
		G(MEMR_GROUP_ID_SENSOR)->enable = 0;
	if (!plat_get_dp_model())
		G(MEMR_GROUP_ID_DP)->enable = 0;
	if (!plat_get_gmac_model())
		G(MEMR_GROUP_ID_GMAC)->enable = 0;
	if (!plat_get_isp_model())
		G(MEMR_GROUP_ID_MIPI)->enable = 0;
	if (!plat_get_mmhub_model())
		G(MEMR_GROUP_ID_SMMU_MM)->enable = 0;
	if (!plat_get_pcie_model())
		G(MEMR_GROUP_ID_SMMU_PCIE)->enable = 0;
	if (!plat_get_usb_model())
		G(MEMR_GROUP_ID_USB)->enable = 0;
	if (!plat_get_fch_model())
		G(MEMR_GROUP_ID_FCH)->enable = 0;
}

/* Walk the eFuse directory: three 10-bit entries (group [4:0], rows - 1 [9:5]) per word */
uint32_t hal_mem_init(void)
{
	uint32_t a, w, e, id, rows, row = 1;
	memr_group_t *g;

	for (a = FUSE_MEMR_DIR; a < FUSE_MEMR_DIR_END; a += 4) {
		w = REG32(a);
		for (e = 0; e < 3; e++) {
			id = (w >> (e * 10)) & 0x1f;
			if (id > MEMR_GROUP_ID_MAX) {
				hal_printf_level(LOG_ERR, "# Invalid group id: %d\n", (int)id);
				return MEMR_ERR_ID;
			}
			if (id == 0)
				goto done;
			g = G(id);
			if (g->id != id) {
				hal_printf_level(LOG_ERR, "# Group id disorder: %d\n", (int)id);
				return MEMR_ERR_ORDER;
			}
			rows = ((w >> (e * 10 + 5)) & 0x1f) + 1;
			g->data = (const uint8_t *)FUSE_MEMR_ROW(row);
			row += rows;
			if (rows > g->max_rows) {
				hal_printf_level(LOG_ERR, "# Invalid group length row: %d(id:%d,max len row:%d)\n",
						 (int)rows, (int)id, (int)g->max_rows);
				g->enable = 0;
			}
			if (id == MEMR_GROUP_ID_DPU && rows > G(MEMR_GROUP_ID_DPU)->max_rows - 2) {
				hal_printf_level(LOG_ERR, "# Invalid group length row: %d(id:%d,max len row:%d)\n",
						 (int)rows, 3, (int)G(MEMR_GROUP_ID_DPU)->max_rows - 2);
				g->enable = 0;
			}
			g->len = rows * 4;
		}
	}
done:
	disable_unused();
	return 0;
}

uint32_t fw_memr_init(void)
{
	return hal_mem_init();
}

#define MEMR(id, idx)	do { err = hal_memr_by_id(id, idx); if (err) { SE_ERR(err); return err; } } while (0)
#define DIST(id)	do { err = hal_memr_distribute_by_id(id, &valid); if (err) { SE_ERR(err); return err; } } while (0)

static uint32_t fch_boot(uint32_t *fch)
{
	uint32_t err = sec_efuse_read(NULL, 0x155, 0, 1, fch);

	if (err)
		SE_ERR(err);
	return err;
}

/* Cold and warm boot */
uint32_t fw_memr_dbs2(void)
{
	uint32_t bypass = 0, valid = 0, fch = 0, reason, err;

	err = btcfg_get_value((const char *)mntr_boot_cfg_item_table, "se.memrbp", &bypass);
	if (err) {
		SE_ERR(err);
		return err;
	}
	if (bypass == 1) {
		SE_INFO("Bypass memrepair!\n\n");
		return 0;
	}
	if (REG32(S5_SW_REG6) & 1) {
		reason = plat_get_boot_reason();
		SE_INFO("boot_cause: %d \n", (int)reason);
		if (reason - 3 <= 2 || reason == 8) {
			SE_INFO("Skip S5 domain memrepair!\n\n");
		} else if ((reason & ~2u) == 0 || reason == 7) {
			REG32(S5_SW_REG6) &= ~1u;
			SE_INFO("Do S5 domain memrepair!\n");
		}
	}
	if (!(REG32(S5_SW_REG6) & 1)) {
		MEMR(MEMR_GROUP_ID_CSU_PM, 3);
		MEMR(MEMR_GROUP_ID_SENSOR, 0);
		MEMR(MEMR_GROUP_ID_USB, 0);
	}
	MEMR(MEMR_GROUP_ID_CPU, 0x300);
	MEMR(MEMR_GROUP_ID_PCIE, 2);
	MEMR(MEMR_GROUP_ID_GMAC, 0);
	MEMR(MEMR_GROUP_ID_MMHUB, 0);
	MEMR(MEMR_GROUP_ID_SMMU_MM, 0);
	MEMR(MEMR_GROUP_ID_CI700, 0);
	MEMR(MEMR_GROUP_ID_SMMU_PCIE, 0);
	MEMR(MEMR_GROUP_ID_SMMU_SYS, 0);
	err = fch_boot(&fch);
	if (err)
		return err;
	if (fch == 0)
		MEMR(MEMR_GROUP_ID_FCH, 0);
	MEMR(MEMR_GROUP_ID_DP, 0);
	MEMR(MEMR_GROUP_ID_MIPI, 0);
	DIST(MEMR_GROUP_ID_DPU);
	DIST(MEMR_GROUP_ID_GPU);
	DIST(MEMR_GROUP_ID_NPU);
	DIST(MEMR_GROUP_ID_AUDIO);
	DIST(MEMR_GROUP_ID_ISP);
	DIST(MEMR_GROUP_ID_VPU);
	DIST(MEMR_GROUP_ID_PCIE);
	return 0;
}

/* Resume from suspend: the S5 domain kept its SRAMs */
uint32_t fw_memr_str(void)
{
	uint32_t bypass = 0, valid = 0, fch = 0, err;

	err = btcfg_get_value((const char *)mntr_boot_cfg_item_table, "se.memrbp", &bypass);
	if (err) {
		SE_ERR(err);
		return err;
	}
	if (bypass == 1) {
		SE_INFO("Bypass memrepair!\n\n");
		return 0;
	}
	MEMR(MEMR_GROUP_ID_PCIE, 2);
	MEMR(MEMR_GROUP_ID_GMAC, 0);
	MEMR(MEMR_GROUP_ID_MMHUB, 0);
	MEMR(MEMR_GROUP_ID_SMMU_MM, 0);
	MEMR(MEMR_GROUP_ID_CI700, 0);
	MEMR(MEMR_GROUP_ID_SMMU_SYS, 0);
	err = fch_boot(&fch);
	if (err)
		return err;
	if (fch == 0)
		MEMR(MEMR_GROUP_ID_FCH, 0);
	MEMR(MEMR_GROUP_ID_DP, 0);
	MEMR(MEMR_GROUP_ID_MIPI, 0);
	DIST(MEMR_GROUP_ID_CPU);
	DIST(MEMR_GROUP_ID_DPU);
	DIST(MEMR_GROUP_ID_GPU);
	DIST(MEMR_GROUP_ID_NPU);
	DIST(MEMR_GROUP_ID_AUDIO);
	DIST(MEMR_GROUP_ID_ISP);
	DIST(MEMR_GROUP_ID_VPU);
	DIST(MEMR_GROUP_ID_PCIE);
	return 0;
}
