/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * CSU_PM firmware: the power-on sequence (CIX's ss_por_enter), what CIX's
 * ss_init_enter and ss_run_enter do as far as it is here, SCP-firmware's
 * modules, the SE handshake, then SCP-firmware's event loop.
 */
#include "arch_scp.h"
#include "clk/clk.h"
#include "dpm/dvfs_cfg.h"
#include "hsm/hsm.h"
#include "i2c.h"
#include "irq.h"
#include "lcru.h"
#include "log/log.h"
#include "mbox.h"
#include "mclp/mclp.h"
#include "msg/msg.h"
#include "pcc/pcc.h"
#include "pmic.h"
#include "plat.h"
#include "pmu.h"
#include "pvt.h"
#include "rtt/rtt.h"
#include "uart.h"

#define I2C_HZ			400000u
/* I2C 0-2 are masters (PMICs), 3 a slave, on the boards CIX supports */
#define I2C_MASTERS		0x7u
#define I2C_SLAVES		0x8u

/*
 * The SE waits up to 5 s for each step (firmware/se/service/main.c): the
 * PM says it runs, waits until the SE has brought up the SoC, initialises
 * what needs the SoC, then says it is ready for the AP.
 */
static void se_handshake_up(void)
{
	REG32(PM_MAGIC) = PM_MAGIC_UP;
	while (REG32(PM_MAGIC) != PM_MAGIC_SE_DONE)
		;
}

/*
 * CIX's pm_ready: scratch words cleared, the audio block's workaround
 * (a warm reset when it fails, on silicon), then ready for the AP (the
 * handshake word was cleared by app_msgInit).  CIX then starts its DPM,
 * performance streams, PVT polling and fans (fan_ctrl_start: no fan is
 * configured; only the pads would be set, which src/hsm does after
 * resume).
 */
static void se_handshake_ready(void)
{
	REG32(PM_SCRATCH + 0x18) = 0;
	REG32(PM_SCRATCH + 0x40) = 0;
	REG32(PM_SCRATCH + 0x48) = 0;
	if (!plat_id_iss && !audio_div_workaround())
		soc_warm_reset(1);
	REG32(PM_MAGIC) = PM_MAGIC_READY;
}

/*
 * The end of CIX's ss_por_enter, once the SE has brought the SoC (and
 * DRAM) up: the RTT block in DRAM, the RAM log (0xaa55 in mailbox 8's
 * fast channel 0 tells the SE it is there), the DRAM block of both locked
 * in its hub window, the version for the AP.
 */
#define SE_RLOG_READY		(PM_MBOX(8) + 0x8c)
#define SE_RLOG_READY_MAGIC	0xaa55u
#define DRAM_LOG_BLOCK		0x80000000u

static void por_shared_memory(void)
{
	sky1_rtt_init();
	fwlog_enable_dram(false, true);
	REG32(SE_RLOG_READY) = SE_RLOG_READY_MAGIC;
	rlog_init(RLOG_DRAM_BASE);
	tlb_ctrl_alloc_entry(DRAM_LOG_BLOCK, false);
	tlb_ctrl_lock_entry(tlb_ctrl_get_entry_idx_by_dram_base(DRAM_LOG_BLOCK, false));
	md_version_broadcast();
}

/* CIX's intr_platform_init: aggregator line 28's sources off (the
 * handlers are registered as SCP-firmware's drivers start), the core's
 * interrupt registers to the log */
#define AGG_LINE_28		28

static void intr_platform_init(void)
{
	uint32_t v;

	REG32(AGG_ENABLE(AGG_LINE_28)) = 0;
	fwlog_write(FWLOG_INTR, "  Intr registers:");
	__asm__ volatile("rsr.intenable %0" : "=a"(v));
	fwlog_write(FWLOG_INTR, "    XT_RSR_INTENABLE %08X", (unsigned int)v);
	__asm__ volatile("rsr.interrupt %0" : "=a"(v));
	fwlog_write(FWLOG_INTR, "    XT_RSR_INTERRUPT %08X", (unsigned int)v);
}

/* CIX's ss_init_enter: the SoC is up, nothing runs on it yet */
void ss_init(void)
{
	bool policy = g_enable_pll_policy;

	pm_ss_state = HSM_ST_INIT;
	/* the power-on clock settings go to the PLLs directly */
	g_enable_pll_policy = false;
	clk_nodes_init();
	g_enable_pll_policy = policy;
	pmu_reset_all();			/* perf_stream_init */
	apply_config_dvfs_sustained_level();	/* DVFS domains at their boot OPP */
	mclp_hwif_init();
	for (uint32_t ch = 0; ch < MCLP_CHANNELS; ch++) {
		if (g_chEnMask & BIT(ch))
			mclp_hwif_power_on_reset(ch);
	}
}

/* CIX's ss_run_enter: the services start */
void ss_run(void)
{
	pm_ss_state = HSM_ST_RUN;
	pmu_reset_all();			/* perf_stream_restore */
	pvt_init();				/* sky1_pvt_init */
	dev_cnts_init();
	dev_cnts_set_instance(0xa0040000, 0, 25000000);
	dev_cnts_set_instance(0xa0048000, 1, 25000000);
	dev_cnts_set_instance(0x44110000, 2, 25000000);
	intr_platform_init();
	app_msgInit();
	pcc_init();				/* at the end of app_msgInit */
	app_msgRestore();			/* after suspend */
	dev_gpio_init();
	ss_run_entered();			/* CIX's resumeEvt, pm_ready's event */
}

static void run_constructors(void)
{
	extern void (*__init_array_start[])(void), (*__init_array_end[])(void);

	for (void (**f)(void) = __init_array_start; f < __init_array_end; f++)
		(*f)();
}

int main(void)
{
	run_constructors();
	lcru_core_clock(1);			/* 400 MHz */
	scratch_clear();
	u_rrq_init();
	fwlog_init();
	fwlog_setLogLevel(4);
	lcru_axprot();
	wdt_off();
	lcru_mbox_reset(0x3ff);
	uart_init();
	uart_puts("CSU_PM: open firmware\n");
	dpm_por_fuses();			/* BIOS configuration, OPN, V/F fuses */
	plat_init();
	i2c_setup(I2C_HZ, I2C_MASTERS, I2C_SLAVES);
	pmic_init();
	pmic_check();
	/* CIX tells the SE it runs (0x5c5a) just before this */
	dpm_por_vf_tables();			/* builtin_perf_profile_setup, cpu_harvest_setup */
	hsm_init();
	se_handshake_up();
	por_shared_memory();
	ss_init();
	ss_run();
	scp_init();				/* SCMI on the mailboxes */
	se_handshake_ready();
	scp_run();
}
