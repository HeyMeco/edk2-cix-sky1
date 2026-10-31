/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Suspend to RAM and the idle loop (CIX service/service_str.c,
 * service/boot/service_str_boot.c, service/service_idle.c,
 * platform/sky1/plat_wakeup_source.c).
 *
 * The AP asks for suspend over the mailbox; the idle loop then flushes the
 * system cache, tells the PM and waits for an interrupt.  On wake-up the
 * SE brings the SoC back without retraining DRAM, restores the cores'
 * reset vectors and tells the PM, which may ask for the sensor fusion hub
 * to be started or stopped.
 */
#include "se.h"
#include "arch.h"
#include "ci700.h"
#include "crypto.h"
#include "ddr.h"
#include "drivers.h"
#include "efuse.h"
#include "mailbox.h"
#include "memory.h"
#include "platform.h"
#include "qspi.h"
#include "rlog.h"
#include "services.h"
#include "soc.h"
#include "storage.h"

#define IRQn_SE_INTR		231
#define PM_FAST_CHANNEL		0x4661008cu	/* SE -> PM: 1 resume, 2 suspend; 0xaa55: RAM log on */
#define PM_MAGIC		0x46610090u
#define S5_SW_SUSPEND		0x5600020cu
#define S5_SW_STR		0x56000228u
#define S5_SW_REG6		0x56000518u
#define S5_RESET_VECTOR		0x5600051cu
#define S5_PM_RUN		0x56000704u
#define CPU_RESET_VECTOR(n)	(0x4fc50308u + (n) * 8)

static uint32_t rc_wdt_wait;

static void str_suspend_handler(void *param, uint32_t plen, void *resp, uint32_t *rlen)
{
	hal_printf_level(LOG_INFO, "%s, suspend\n", __func__);
	REG32(SE_SYSCTRL_IDLE) = 1;
}

/* The PM's wake-up interrupt: release the SE's sleep request */
static void str_resume_handler(void *arg)
{
	uint32_t irq = core_irq_save(), n = 200000000;

	core_irq_disable(IRQn_SE_INTR);
	hal_udelay(100);
	while (--n && !(REG32(SE_CLKCTRL_BASE + 0x2c) & 1))
		;
	REG32(SE_CLKCTRL_BASE + 0x20) = 1;
	hal_udelay(100);
	REG32(0x5600021c) = 0;
	core_irq_restore(irq);
	core_irq_enable(IRQn_SE_INTR);
}

uint32_t service_str_boot_entry(void)
{
	uint32_t err;

	SE_INFO("Service regist suspend/resumue handler\n");
	REG32(SE_SYSCTRL_IDLE) = 0;
	err = fw_dispatcher_reg_handler(0x82000007, str_suspend_handler);
	if (err) {
		SE_ERR(err);
		return err;
	}
	core_irq_register(IRQn_SE_INTR, str_resume_handler, NULL);
	core_irq_enable(IRQn_SE_INTR);
	return 0;
}

static void flush_dram(void)
{
	drv_ci700_slc_flush(0x80000000ull, (uint64_t)drv_ddr_get_mem_size() << 20);
	if (hal_secfg_get_mte_config())
		drv_ci700_flush_mte_tagcache();
}

static void suspend(void)
{
	SE_INFO("suspend in loop\n");
	if (!plat_is_se_only()) {
		flush_dram();
		REG32(S5_SW_SUSPEND) |= 1;
		REG32(S5_SW_STR) = 1;
		hal_ipc_suspend();
		REG32(S5_SW_REG6) |= 1;
		SE_INFO("%s: SW_USED_REG6: %d \n", __func__, (int)REG32(S5_SW_REG6));
		hal_printf_level(LOG_ERR, "[SE_FW]# se save down, request to pm (suspend)\n");
		REG32(PM_FAST_CHANNEL) = 2;
	}
	hal_crash_deinit();
	rlog_set_enable_disable(0);
	hal_printf_level(LOG_ERR, "[SE_FW]# se entry wfi!\n");
	__asm__ volatile("wfi");
}

static void resume(void)
{
	uint32_t vec, i;

	drv_uart_init(plat_get_uart_baudrate(), plat_get_uart_clk_speed() * 1000);
	fw_log_init();
	plat_save_wakeup_source();
	hal_printf_level(LOG_ERR, "[SE_FW]# power reason: %0x\n", (unsigned)plat_get_boot_reason());
	if (crypto_platform_setup()) {
		SE_ERR(0xffffffff);
		return;
	}
	hal_soc_init_pre(1);
	hal_soc_init_post(1);
	SE_INFO("restore cpu reset vector!\n");
	vec = REG32(S5_RESET_VECTOR);
	SE_INFO("reset vector val:0x%x!\n", (unsigned)vec);
	for (i = 0; i < 12; i++) {
		REG32(CPU_RESET_VECTOR(i)) = vec;
		SE_INFO("cpu%d, reset_vector:0x%x!\n", (int)i, (unsigned)REG32(CPU_RESET_VECTOR(i)));
	}
	fw_memr_str();
	if (fw_fuse_dist(FUSED_TYPE_BOOT)) {
		SE_ERR(0xffffffff);
		return;
	}
	hal_ipc_resume();
	REG32(S5_PM_RUN) = 0;
	hal_printf_level(LOG_ERR, "[SE_FW]# se restore over, request to pm (resume)\n");
	REG32(SE_SYSCTRL_IDLE) = 0;
	while (drv_pl011_getchar_poll(plat_get_core_freq() * 5) >= 0)
		;
}

/* After resume: the PM asks for the SFH (0xaacc) or for it to stop (0xaabb) */
static void str_magic(void)
{
	uint32_t n, err;

	REG32(PM_FAST_CHANNEL) = 1;
	for (n = 5001; n; n--) {
		if (REG32(PM_MAGIC) == 0xaacc) {
			hal_crash_deinit();
			err = hal_stor_qspi_user_switch(QSPI_USER_SE);
			if (!err)
				err = hal_sfh_init();
			if (!err)
				err = hal_stor_qspi_user_switch(QSPI_USER_AP);
			if (err)
				SE_ERR(err);
			else
				REG32(PM_MAGIC) = 0xccaa;
			return;
		}
		if (REG32(PM_MAGIC) == 0xaabb) {
			if (hal_is_se_load_sfh() == 1) {
				err = hal_sfh_deinit();
				if (err) {
					SE_ERR(err);
					return;
				}
			}
			REG32(PM_MAGIC) = 0;
			return;
		}
		hal_mdelay(1);
	}
	hal_printf_level(LOG_ERR, "Wait for STR magic failed \n");
}

/* One pass of the idle loop */
void service_str_entry(void)
{
	if (REG32(SE_SYSCTRL_IDLE)) {
		suspend();
		resume();
		str_magic();
	}
	if (!is_rlog_enable()) {
		hal_mdelay(1);
		if (REG32(PM_FAST_CHANNEL) == 0xaa55)
			rlog_set_enable_disable(1);
	}
}

static void idle_exit_handler(void *param, uint32_t plen, void *resp, uint32_t *rlen)
{
}

static void idle_enter_handler(void *param, uint32_t plen, void *resp, uint32_t *rlen)
{
	SE_INFO("suspend in loop\n");
	if (plat_is_se_only())
		return;
	flush_dram();
	REG32(S5_SW_SUSPEND) |= 1;
	REG32(S5_SW_STR) = 1;
	hal_crash_deinit();
	hal_ipc_suspend();
	REG32(S5_SW_REG6) |= 1;
	SE_INFO("%s: SW_USED_REG6: %d \n", __func__, (int)REG32(S5_SW_REG6));
	SE_INFO("se send fastchannel to pm(suspend)\n");
	REG32(PM_FAST_CHANNEL) = 2;
}

/* Never returns: serve the AP, keep the crash watchdog fed */
uint32_t service_idle_entry(void)
{
	uint32_t err;

	hal_printf_level(LOG_ERR, "idle\n");
	err = fw_dispatcher_reg_handler(0x82000013, idle_enter_handler);
	if (!err)
		err = fw_dispatcher_reg_handler(0x82000014, idle_exit_handler);
	if (err) {
		SE_ERR(err);
		return err;
	}
	for (;;) {
		if (rc_wdt_wait == 0) {
			hal_crash_feed_dog();
			rc_wdt_wait = plat_get_core_freq() * 2000;
		}
		rc_wdt_wait--;
		service_str_entry();
	}
}

/* Wake-up sources, for the AP to ask after resume */
static struct {
	uint32_t count;
	uint32_t source;
	uint32_t data[6];
} g_plat_wakeup_src;

void plat_save_wakeup_source(void)
{
	uint32_t src, i;

	memset(&g_plat_wakeup_src, 0, sizeof(g_plat_wakeup_src));
	g_plat_wakeup_src.count = 4;
	src = REG32(S5_WAKEUP_SOURCE);
	SE_INFO("wakup source: 0x%0x\n", (unsigned)src);
	g_plat_wakeup_src.source = src;
	if (src & 2) {
		for (i = 0; i < 3; i++) {
			uint32_t mask = REG32(S5_GPIO_BASE + 0x14 + i * 0x1000);
			uint32_t st = REG32(S5_GPIO_BASE + 0x20 + i * 0x1000);

			g_plat_wakeup_src.data[i] = st & ~mask;
			SE_INFO("s5 gpio%d int mask: 0x%x int state: 0x%x val: 0x%x\n", (int)i,
				(unsigned)mask, (unsigned)st, (unsigned)(st & ~mask));
		}
	} else if (REG32(S5_WAKEUP_SOURCE) & 4) {
		g_plat_wakeup_src.data[0] = REG32(S5_WAKEUP_TYPE);
		SE_INFO("usb wakeup type: 0x%x\n", (unsigned)g_plat_wakeup_src.data[0]);
	}
}

void plat_send_wakeup_src(void)
{
	mbox_msg_t msg = { 0 };
	uint32_t err;

	msg.cmd_id = 0x50000001;
	memcpy(msg.data, &g_plat_wakeup_src, sizeof(g_plat_wakeup_src));
	msg.size = 0x28;
	err = hal_ipc_send_data(MBOX_CHN_SE2AP_REE, &msg);
	if (err)
		hal_printf_level(LOG_WARN, "SE send wakeup source msg failed, ret = %d \n", (int)err);
}

static void hc_wakeup_source_handler(void *param, uint32_t plen, void *resp, uint32_t *rlen)
{
	hal_printf_level(LOG_INFO, "receive wakeup source req form AP \n");
	plat_send_wakeup_src();
}

int plat_wakeup_source_init(void)
{
	uint32_t err = fw_dispatcher_reg_handler(0x82000016, hc_wakeup_source_handler);

	if (err)
		SE_ERR(err);
	return err;
}
