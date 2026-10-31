/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Top level (CIX service/service_main.c, service_fw.c, service/boot,
 * service/debug).
 *
 * Cold boot: clocks and power, SRAM repair, fuse distribution, harvest;
 * then the runtime services; then CSU_PM is loaded and started, DRAM is
 * trained once the PM is up, and BL2 is loaded and the AP released.  The
 * SE then stays in its idle loop serving the AP and the PM.
 */
#include "se.h"
#include "arch.h"
#include "btcfg.h"
#include "drivers.h"
#include "efuse.h"
#include "harvest.h"
#include "memcfg.h"
#include "memory.h"
#include "platform.h"
#include "rlog.h"
#include "services.h"
#include "soc.h"
#include "xloader.h"
#include "bootperf.h"

#define PM_MAGIC		0x46610090u
#define RLOG_BUF		0x63da0000u	/* SoC 0x83da0000, 16 KiB */

/* The DFD block's view of the AP debug registers */
void drv_dfd_init(void);

static const char *const boot_reason_txt[10] = {
	"cold rsmrst", "str", "cold boot", "warm wdt (ap)", "warm reset (software)",
	"warm exbutton reset", "unknown", "cold power btn", "warm wdt (se)", "unknown",
};

static void fw_flush_cache_by_addr(void)
{
}

static const ramlog_ops g_r_ops = { .flush = fw_flush_cache_by_addr };

static uint32_t soc_bring_up(bool warm)
{
	static const hvst_t harvest[] = {
		HVST_TYPE_CPU_FREQ, HVST_TYPE_GPU, HVST_TYPE_VPU, HVST_TYPE_CPU_HW, HVST_TYPE_REMAIN_IP_HW,
	};
	uint32_t err, i;

	if (plat_is_se_only())
		return 0;
	err = hal_soc_init_pre(0);
	if (err)
		goto fail;
	hal_pc_trace(0x900d);
	err = fw_memr_init();
	if (!err)
		err = fw_memr_dbs2();
	if (err)
		goto fail;
	if (!warm)
		hal_pc_trace(0x9023);
	err = fw_fuse_dist(FUSED_TYPE_GLITCH);
	if (!err)
		err = fw_fuse_dist(FUSED_TYPE_BOOT);
	for (i = 0; !err && i < ARRAY_SIZE(harvest); i++)
		err = fw_harvest(harvest[i]);
	if (err)
		goto fail;
	return 0;
fail:
	SE_ERR(err);
	return err;
}

uint32_t service_boot_entry(void)
{
	uint32_t reason = plat_get_boot_reason();

	SE_INFO("%s: boot_cause[%d]: %s \n", __func__, (int)reason,
		reason < 10 ? boot_reason_txt[reason] : "unexpected boot-cause");
	if ((reason & ~2u) == 0 || reason == 7) {
		SE_INFO("Service cold boot Start...\n");
		return soc_bring_up(false);
	}
	if (reason - 3 < 3 || reason == 8) {
		SE_INFO("Service warm boot Start...\n");
		return soc_bring_up(true);
	}
	return 0xffffffff;
}

/*
 * Wait for the PM's handshake in PM_MAGIC.  While waiting for 0x5c5a the
 * PM may instead ask for the sensor fusion hub (0xaacc): the system is
 * then in suspend and the SE stays in its suspend loop.
 */
static uint32_t service_check_boot_magic_ready(uint32_t magic, uint32_t ms)
{
	uint32_t err;

	for (;;) {
		if (REG32(PM_MAGIC) == magic) {
			REG32(PM_MAGIC) = 0;
			return 0;
		}
		if (magic == 0x5c5a && REG32(PM_MAGIC) == 0xaacc)
			break;
		hal_mdelay(1);
		if (ms-- == 0) {
			hal_printf_level(LOG_ERR, "[SE_FW]# Wait for boot magic  0x%x failed \n", (unsigned)magic);
			return 0xffffffff;
		}
	}
	hal_crash_deinit();
	err = hal_sfh_init();
	if (err)
		return err;
	REG32(PM_MAGIC) = 0xccaa;
	for (;;) {
		hal_mdelay(1);
		service_str_entry();
	}
}

#define TRY(x)	do { err = (x); if (err) return err; } while (0)

uint32_t service_secure_boot_entry(void)
{
	uint32_t err, bde = 0;
	bool spinor = plat_get_fm_load() == PLAT_FW_LOAD_SPINOR;

	if (plat_is_se_only()) {
		SE_INFO("skip load AP & pm, only run csu_se\n");
		return 0;
	}
	if (spinor)
		SE_INFO("no skip load AP & pm\n");
	else
		SE_INFO("skip load AP & pm\n");
	hal_pc_trace(0x9101);
	if (spinor) {
		hal_printf_level(LOG_ERR, "[SE_FW]# PM boot start\n");
		TRY(fw_jumper_init(SEC_FW_TYPE_PM));
		hal_pc_trace(0x9102);
		TRY(fw_xloader_load_img(SEC_FW_TYPE_PM));
		hal_pc_trace(0x9103);
		hal_pc_trace(0x9105);
		TRY(fw_jumper_prejump(SEC_FW_TYPE_PM));
		TRY(drv_memcfg_set_pm_strap_info());
		fw_jumper_run(SEC_FW_TYPE_PM);
		hal_pc_trace(0x9106);
	} else {
		hal_printf_level(LOG_ERR, "[SE_FW]!!!! please backdoor CSU_PM\n");
		TRY(fw_jumper_init(SEC_FW_TYPE_PM));
		hal_pc_trace(0x9102);
		TRY(fw_jumper_prejump(SEC_FW_TYPE_PM));
		fw_jumper_run(SEC_FW_TYPE_PM);
	}
	TRY(service_check_boot_magic_ready(0x5c5a, 5000));
	TRY(hal_soc_init_post(0));
	REG32(PM_MAGIC) = 0x55ca;
	SE_INFO("\n%s:##### init ram log \n", __func__);
	rlog_init((char *)RLOG_BUF, 0x4000, &g_r_ops);
	if (spinor && plat_get_syshub_model()) {
		TRY(service_check_boot_magic_ready(0x5a5c, 5000));
		hal_printf_level(LOG_ERR, "[SE_FW]# PM boot end\n");
		hal_printf_level(LOG_ERR, "[SE_FW]# PBL boot start\n");
		TRY(fw_jumper_init(SEC_FW_TYPE_PBL));
		hal_pc_trace(0x9107);
		TRY(btcfg_get_value((const char *)mntr_boot_cfg_item_table, "se.pblbde", &bde));
		if (bde == 0) {
			TRY(fw_xloader_load_img(SEC_FW_TYPE_PBL));
			hal_pc_trace(0x9108);
		}
	} else {
		hal_printf_level(LOG_ERR, "[SE_FW]!!!! please backdoor PBL  !!!\n");
		TRY(fw_jumper_init(SEC_FW_TYPE_PBL));
	}
	TRY(fw_jumper_prejump(SEC_FW_TYPE_PBL));
	fw_jumper_run(SEC_FW_TYPE_PBL);
	hal_printf_level(LOG_ERR, "[SE_FW]# PBL boot end\n");
	hal_pc_trace(0x9109);
	return 0;
}

uint32_t service_debug_entry(void)
{
	SE_INFO("Service debug Start...\n");
	drv_dfd_init();
	hal_printf_level(LOG_ERR, "[SE_FW]# All service were inited successful....");
	return 0;
}

uint32_t services_init(void)
{
	uint32_t err;

	err = service_boot_entry();
	if (err)
		goto fail;
	hal_pc_trace(0x9020);
	err = service_runtime_entry();
	if (err)
		goto fail;
	hal_pc_trace(0x9021);
	err = service_secure_boot_entry();
	if (err)
		goto fail;
	err = service_debug_entry();
	if (err)
		goto fail;
	hal_pc_trace(0x9022);
	return 0;
fail:
	SE_ERR(err);
	return err;
}

void main(void)
{
	uint8_t chip_id[8] = { 0 };
	uint32_t err;

	drv_uart_init(plat_get_uart_baudrate(), plat_get_uart_clk_speed() * 1000);
	cix_boot_perf_init(SE_PHASE);
	cix_set_boot_phase(SE_PHASE, RECORD_START);
	hal_pc_trace(0x9003);
	sys_clk_init();
	hal_pc_trace(0x9004);
	drv_rom_lock();
	hal_pc_trace(0x9005);
	err = fw_init();
	if (err)
		goto fail;
	hal_pc_trace(0x9019);
	print_bannar();
	err = sysh_io_init(0x80000000);
	if (err)
		goto fail;
	hal_printf_level(LOG_INFO, "SYSHUB window mapped\n");
	hal_pc_trace(0x901a);
	err = fw_efuse_get_chip_id(chip_id);
	if (err)
		goto fail;
	hal_printf_level(LOG_ERR, "[SE_FW]# Chip ID: 0x%08x%08x\n", (unsigned)*(uint32_t *)(chip_id + 4),
			 (unsigned)*(uint32_t *)chip_id);
	if (fw_sec_debug_check())
		hal_printf_level(LOG_INFO, "No secure debug certificate\n");
	plat_wakeup_source_init();
	err = services_init();
	if (err)
		goto fail;
	hal_pc_trace(0x90ff);
	service_idle_entry();
	return;
fail:
	SE_ERR(err);
	hal_pc_trace(0x90ff);
	fw_err_handler(err);
	hal_pc_trace(0x90ff);
	service_idle_entry();
}
