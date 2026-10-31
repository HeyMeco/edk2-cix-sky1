/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Clocks, resets and power of the SoC blocks (CIX driver/cru/drv_cru.c).
 *
 * S5 power controller resets (0x56000000) and the clock generator
 * (0x46570000, SoC 0x06570000) bring each block out of reset in a fixed
 * order; the audio subsystem gets a power self-test on cold boot.
 */
#include "se.h"
#include "drivers.h"
#include "harvest.h"
#include "mailbox.h"
#include "platform.h"
#include "soc.h"

#define CGC		0x46570000u
#define S5_RST		0x56000000u
#define S5_RST_HUBS	(S5_RST + 0x400)	/* CI-700, GIC, hubs, NPU, DPU, DDRC */
#define S5_RST_AUDIO	(S5_RST + 0x404)
#define AUDIO_PWR	0x47000020u
#define AUDIO_CTRL	0x47000034u

#define TRACE()		hal_printf_level(LOG_INFO, "%s\n", __func__)

static void ipc_clk_auto_gating_enable_handler(void *param, uint32_t plen, void *resp, uint32_t *rlen)
{
	REG32(0x4fc50000 + 0x270) |= 0x3ffff;
}

static void ipc_clk_auto_gating_disable_handler(void *param, uint32_t plen, void *resp, uint32_t *rlen)
{
	REG32(0x4fc50000 + 0x270) &= 0xfffc0000;
}

/* Poll a power status register (SoC address) for us microseconds */
uint32_t drv_soc_wait_power_state(uint32_t reg, uint32_t mask, uint32_t state, uint32_t us)
{
	int i;

	for (; us; us--) {
		for (i = 0; i < 2; i++)
			if (!((REG32(reg + 0x40000000) ^ state) & mask))
				return 0;
		hal_udelay(1);
	}
	return 0xffffffff;
}

static void drv_soc_common_init(void)
{
	REG32(CGC + 0x5314) |= 0x10;
}

static void drv_soc_fch_init(void)
{
	REG32(0x40000300) = 1;
	REG32(0x44160000) = 0x1ffffff;
	REG32(0x44160004) = 0x7fffff;
	REG32(0x44160008) = 0x7fffff;
	REG32(0x4416000c) = 0x3fffff;
	REG32(0x44160010) = 0x1f;
}

/* Clusters of two cores: bit 0 and bit 4 enable the cores of each PLL domain */
static void drv_soc_cpu_init(void)
{
	uint32_t m = fw_get_cpu_mask_by_harvest();

	TRACE();
	REG32(CGC + 0x03c) = ((m >> 8) & 1) | ((m >> 9) & 1) << 4;
	REG32(CGC + 0x13c) = ((m >> 10) & 1) | ((m >> 11) & 1) << 4;
	REG32(CGC + 0x23c) = ((m >> 4) & 1) | ((m >> 5) & 1) << 4;
	REG32(CGC + 0x33c) = ((m >> 6) & 1) | ((m >> 7) & 1) << 4;
	REG32(CGC + 0x43c) = (m & 1) | ((m >> 1) & 1) << 4 | ((m >> 2) & 1) << 8 | ((m >> 3) & 1) << 12;
}

static void drv_soc_dsu_init(void)
{
	TRACE();
	REG32(CGC + 0x52c) = 1;
	REG32(CGC + 0x618) |= 0x111;
}

static uint32_t drv_soc_audio_ss_self_test(void)
{
	uint32_t err;

	hal_printf_level(LOG_ERR, "[SE_FW]# audio self-test start\n");
	REG32(CGC + 0x211c) |= 0x10000;
	REG32(S5_RST_AUDIO) |= 2;
	REG32(AUDIO_PWR) = 0xfff;
	err = drv_soc_wait_power_state(0x07000020, 0x7000, 0x3000, 20);
	if (err) {
		hal_printf_level(LOG_ERR, "[SE_FW]%s: power on failed \n", __func__);
		SE_ERR(err);
		return err;
	}
	REG32(AUDIO_CTRL) = 0x20000000;
	err = drv_wdt_init(500, 1);
	if (!err)
		err = drv_wdt_start();
	if (!err)
		err = drv_wdt_stop();
	if (err) {
		SE_ERR(err);
		return err;
	}
	REG32(AUDIO_PWR) = 0xffe;
	hal_udelay(20);
	REG32(CGC + 0x211c) |= 0x10000;
	hal_printf_level(LOG_ERR, "[SE_FW]# audio self-test end\n");
	return 0;
}

static void drv_soc_npu_init(void)
{
	uint32_t npu = 0x54250000;

	TRACE();
	REG32(CGC + 0x2114) = (REG32(CGC + 0x2114) & 0xfffff0ff) | 0x500;
	REG32(npu + 0x300) = 0xf00001e0;
	REG32(npu + 0x308) = 0x1e0;
	REG32(npu + 0x30c) &= 0x9fffffff;
	REG32(S5_RST_HUBS) |= 0x780;
	REG32(npu + 0x300) |= 0x33001e0;
	REG32(npu + 0x304) |= 0xc0000000;
	REG32(npu + 0x308) |= 0x33001e0;
	REG32(npu + 0x30c) |= 0xc0000000;
}

static void drv_soc_dpu_init(void)
{
	static const uint32_t dpu[5] = { 0x5400030c, 0x5407030c, 0x540e030c, 0x5415030c, 0x541c030c };
	unsigned int i;

	TRACE();
	REG32(CGC + 0x211c) |= 1;
	REG32(CGC + 0x2114) = (REG32(CGC + 0x2114) & ~3u) | 2;
	for (i = 0; i < ARRAY_SIZE(dpu); i++)
		REG32(dpu[i]) = (REG32(dpu[i]) & ~7u) | 0x38;
	REG32(S5_RST_HUBS) |= 0x1f0000;
}

static void drv_soc_dp_init(void)
{
	TRACE();
	REG32(0x54050300) = 0xfbb6000;
	REG32(0x54050304) = 0x2001;
	REG32(0x540c0300) = 0xfbb6000;
	REG32(0x540c0304) = 0x2001;
	REG32(0x54130320) |= 0x7000;
	REG32(0x541a0300) = 0xfbb6000;
	REG32(0x541a0304) = 0x2001;
	REG32(0x54210300) = 0xfbb6000;
	REG32(0x54210304) = 0x2001;
}

static void hw_clk_auto_gating_default(void)
{
	static const uint32_t reg[7] = {
		0x4d000240, 0x50000260, 0x54000210, 0x54070210, 0x540e0210, 0x54150210, 0x541c0210,
	};
	unsigned int i;

	hal_printf_level(LOG_INFO, "enable %s\n", __func__);
	for (i = 0; i < ARRAY_SIZE(reg); i++)
		REG32(reg[i]) |= 1;
}

uint32_t drv_soc_cru_init(uint32_t is_str)
{
	uint32_t err;

	hal_printf_level(LOG_INFO, "sys_pll lock_status: %d\n", (int)REG32(S5_RST));
	drv_soc_common_init();
	hal_printf_level(LOG_INFO, "%s\n", "drv_soc_pmctrl_s5_init");
	REG32(0x56100008) = 1;
	hal_printf_level(LOG_INFO, "%s\n", "drv_soc_dfd_init");
	REG32(S5_RST + 0x0c) = 1;
	hal_printf_level(LOG_INFO, "%s\n", "drv_soc_csu_pm_init");
	REG32(S5_RST + 0x14) = 1;
	hal_printf_level(LOG_INFO, "%s\n", "drv_soc_csu_se_init");
	REG32(S5_RST + 0x10) = 1;
	hal_printf_level(LOG_INFO, "%s\n", "drv_soc_tsgen_init");
	REG32(S5_RST + 0x18) = 1;
	drv_soc_fch_init();
	drv_soc_cpu_init();
	drv_soc_dsu_init();
	hal_printf_level(LOG_INFO, "%s\n", "drv_soc_ci700_init");
	REG32(CGC + 0x5214) = 1;
	REG32(S5_RST_HUBS) |= 4;
	hal_printf_level(LOG_INFO, "%s\n", "drv_soc_gic_init");
	REG32(S5_RST_HUBS) |= 2;
	if (plat_get_npu_model())
		drv_soc_npu_init();
	if (plat_get_mmhub_model()) {
		hal_printf_level(LOG_INFO, "%s\n", "drv_soc_mmhub_init");
		REG32(CGC + 0x2014) |= 0x10;
		REG32(S5_RST_HUBS) |= 0x10;
	}
	if (plat_get_syshub_model()) {
		hal_printf_level(LOG_INFO, "%s\n", "drv_soc_syshub_init");
		REG32(CGC + 0x2014) |= 1;
		REG32(S5_RST_HUBS) |= 8;
	}
	if (plat_get_pciehub_model()) {
		hal_printf_level(LOG_INFO, "%s\n", "drv_soc_pcie_hub_init");
		REG32(CGC + 0x6018) |= 1;
		REG32(S5_RST_HUBS) |= 0x20;
	}
	hal_printf_level(LOG_INFO, "%s\n", "drv_soc_ddrc_init");
	REG32(S5_RST_HUBS) |= 1;
	if (plat_get_gpu_model()) {
		hal_printf_level(LOG_INFO, "%s\n", "drv_soc_gpu_init");
		REG32(0x5500030c) = 0xf;
	}
	if (plat_get_dp_model())
		drv_soc_dp_init();
	if (plat_get_dpu_model())
		drv_soc_dpu_init();
	if (!is_str && plat_get_audio_model()) {
		err = drv_soc_audio_ss_self_test();
		if (err)
			return err;
	}
	hw_clk_auto_gating_default();
	if (is_str)
		return 0;
	err = fw_dispatcher_reg_handler(0x82000018, ipc_clk_auto_gating_enable_handler);
	if (!err) {
		hal_pc_trace(0x9016);
		err = fw_dispatcher_reg_handler(0x82000019, ipc_clk_auto_gating_disable_handler);
	}
	if (err) {
		SE_ERR(err);
		return err;
	}
	hal_pc_trace(0x9016);
	return 0;
}
