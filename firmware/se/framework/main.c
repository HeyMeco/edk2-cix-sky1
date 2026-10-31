/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Firmware set-up before the services (CIX framework/fw_main.c, fw_log.c) */
#include "se.h"
#include "crypto.h"
#include "efuse.h"
#include "km.h"
#include "mailbox.h"
#include "memcfg.h"
#include "platform.h"
#include "services.h"
#include "storage.h"

#ifndef SE_FW_BUILD
#define SE_FW_BUILD		"open"
#endif

static uint32_t log_mode = 0xf;		/* 0xf: from the memory configuration */

void print_bannar(void)
{
	static const char rule[] =
		"[SE_FW]#################################################################################\n";

	hal_printf_level(LOG_INFO, rule);
	hal_printf_level(LOG_INFO, "[SE_FW]# CiX SE firmware Sky1 %s\n", SE_FW_VERSION);
	hal_printf_level(LOG_INFO, "[SE_FW]# Weekly Version: %s\n", "2.0.2026W01.01-open");
	hal_printf_level(LOG_ERR, "[SE_FW]# Compile Commit Id: %s\n", SE_FW_BUILD);
	hal_printf_level(LOG_INFO, rule);
}

/* Unfused parts log everything; others as the memory configuration says (default 3) */
uint32_t fw_log_init(void)
{
	uint32_t lc = 0, err;

	err = plat_get_info(PLAT_INFO_LIFE_CYCLE, &lc);
	if (err) {
		SE_ERR(err);
		return err;
	}
	if (lc == LIFE_CYCLE_BLANK) {
		hal_log_set_level(LOG_INFO);
		return 0;
	}
	if (log_mode == 0xf) {
		err = drv_memcfg_get_se_log_lv(&log_mode);
		if (err) {
			SE_ERR(err);
			return err;
		}
		if (log_mode == 0xf)
			log_mode = LOG_INFO;
	}
	hal_log_set_level(log_mode);
	hal_printf_level(LOG_WARN, "Log mode is: 0x%x\n", (unsigned)log_mode);
	return 0;
}

uint32_t fw_init(void)
{
	uint32_t err;

	system_heap_init();
	hal_pc_trace(0x9006);
	REG32(SE_SYSCTRL_IDLE) = 0;
	fw_efuse_int();
	hal_pc_trace(0x9007);
	drv_memcfg_base_init();
	fw_log_init();
	hal_pc_trace(0x9008);
	err = hal_stor_init();
	if (err)
		goto fail;
	hal_secfg_init();
	hal_pc_trace(0x9009);
	err = crypto_platform_setup();
	if (err)
		goto fail;
	hal_printf_level(LOG_INFO, "Mbedtls setup successful\n");
	hal_pc_trace(0x9012);
	err = hal_crypto_init();
	if (err)
		goto fail;
	hal_printf_level(LOG_INFO, "Crypto init successful\n");
	hal_pc_trace(0x9013);
	err = fw_km_init();
	if (err)
		goto fail;
	err = hal_ipc_init();
	if (err)
		goto fail;
	hal_printf_level(LOG_INFO, "IPC init successful\n");
	err = fw_dispatcher_init();
	if (err)
		goto fail;
	hal_printf_level(LOG_INFO, "Dispatcher init successful\n");
	hal_pc_trace(0x9018);
	return 0;
fail:
	SE_ERR(err);
	return err;
}
