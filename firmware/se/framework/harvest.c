/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Tell the SoC which cores and blocks the eFuse harvests (CIX
 * framework/fw_harvest.c): CPU frequency grade and harvested cores to the
 * S5 power controller's software registers (for the PM and TF-A), GPU
 * shader cores and VPU cores to their blocks.
 */
#include "se.h"
#include "efuse.h"
#include "harvest.h"
#include "platform.h"

#define S5_SW_HARVEST	0x56000508u	/* [11:0] cores off, [15:12] boot core, [25:16] blocks off */
#define S5_SW_CPU_FREQ	0x5600050cu
#define GPU_HARVEST	0x55000304u
#define VPU_HARVEST	0x54230308u

static const uint32_t gpu_core_bits[10] = {
	0x1, 0x4, 0x10, 0x40, 0x100, 0x400, 0x10000, 0x40000, 0x100000, 0x400000,
};

static const struct {
	const char *name;
	uint16_t addr;
	uint8_t offset, size;
} hw_hvst_table[10] = {
	{ "npu0", 1, 7, 1 }, { "npu1", 3, 0, 1 }, { "dpu0", 2, 1, 1 }, { "dpu1", 2, 2, 1 },
	{ "dpu2", 2, 3, 1 }, { "dpu3", 2, 4, 1 }, { "dpu4", 2, 5, 1 }, { "pciex8", 2, 6, 1 },
	{ "audio", 2, 7, 1 }, { "isp", 2, 0, 1 },
};

uint32_t fw_harvest(hvst_t t)
{
	uint32_t v = 0, bits = 0, core, i, err;

	switch (t) {
	case HVST_TYPE_CPU_FREQ:
		err = sec_efuse_read(NULL, 0x4d4, 8, 4, &v);
		if (err)
			break;
		REG32(S5_SW_CPU_FREQ) &= ~0xfu;
		REG32(S5_SW_CPU_FREQ) |= v;
		hal_printf_level(LOG_INFO, "cpu_freq_harvest: 0x%x\n", (unsigned)v);
		return 0;
	case HVST_TYPE_GPU:
		err = sec_efuse_read(NULL, 0x4d1, 2, 10, &v);
		if (err)
			break;
		hal_printf_level(LOG_INFO, "gpu_harvest: 0x%x\n", (unsigned)v);
		for (i = 0; i < ARRAY_SIZE(gpu_core_bits); i++)
			if (v & BIT(i))
				bits |= gpu_core_bits[i] << 4;
		REG32(GPU_HARVEST) &= 0xf000000f;
		REG32(GPU_HARVEST) |= bits;
		hal_printf_level(LOG_INFO, "gpu_harvest_reg_data: 0x%x\n", (unsigned)bits);
		return 0;
	case HVST_TYPE_VPU:
		err = sec_efuse_read(NULL, 0x4cf, 6, 4, &v);
		if (err)
			break;
		v = (v << 4) & 0xff;
		REG32(VPU_HARVEST) &= 0xffffff0f;
		REG32(VPU_HARVEST) |= v;
		hal_printf_level(LOG_INFO, "vpu_harvest: 0x%x\n", (unsigned)v);
		return 0;
	case HVST_TYPE_CPU_HW:
		err = sec_efuse_read(&g_handle_efuse, 0x46, 4, 12, &v);
		if (err)
			break;
		core = plat_get_ap_bootcore_index();
		hal_printf_level(LOG_INFO, "boot core_id:%d\n", (int)core);
		hal_printf_level(LOG_INFO, "cpu hardware harvest:0x%x\n", (unsigned)v);
		REG32(S5_SW_HARVEST) = (core & 0xf) << 12 | REG32(S5_SW_HARVEST) | v;
		return 0;
	case HVST_TYPE_REMAIN_IP_HW:
		REG32(S5_SW_HARVEST) &= 0xfc00ffff;
		for (i = 0; i < ARRAY_SIZE(hw_hvst_table); i++) {
			v = 0;
			err = sec_efuse_read(&g_handle_efuse, hw_hvst_table[i].addr,
					     hw_hvst_table[i].offset, hw_hvst_table[i].size, &v);
			if (err)
				return err;
			hal_printf_level(LOG_INFO, "%s hardware harvest:0x%x\n", hw_hvst_table[i].name,
					 (unsigned)v);
			REG32(S5_SW_HARVEST) |= (v & 1) << (16 + i);
		}
		return 0;
	default:
		return 0xffffffff;
	}
	SE_ERR(err);
	return err;
}

uint32_t fw_get_cpu_mask_by_harvest(void)
{
	uint32_t harvest = 0;

	if (sec_efuse_read(&g_handle_efuse, 0x46, 4, 12, &harvest))
		return 0xfff;
	return ~harvest & 0xfff;
}
