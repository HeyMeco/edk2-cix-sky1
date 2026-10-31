/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * CSU_PM firmware: the power-on sequence (CIX's ss_por_enter), what CIX's
 * ss_init_enter and ss_run_enter do as far as it is here, SCP-firmware's
 * modules, the SE handshake, then SCP-firmware's event loop.
 */
#include "arch_scp.h"
#include "i2c.h"
#include "lcru.h"
#include "mbox.h"
#include "pmic.h"
#include "pmu.h"
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

static void se_handshake_ready(void)
{
	REG32(PM_MAGIC) = 0;
	REG32(PM_MAGIC) = PM_MAGIC_READY;
}

/* CIX's ss_init_enter: the SoC is up, nothing runs on it yet */
static void ss_init(void)
{
	pmu_reset_all();			/* perf_stream_init */
}

/* CIX's ss_run_enter: the services start */
static void ss_run(void)
{
	pmu_reset_all();			/* perf_stream_restore */
	mbox_init();				/* app_msgInit */
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
	lcru_axprot();
	wdt_off();
	lcru_mbox_reset(0x3ff);
	uart_init();
	uart_puts("CSU_PM: open firmware\n");
	i2c_setup(I2C_HZ, I2C_MASTERS, I2C_SLAVES);
	pmic_init();
	pmic_check();
	se_handshake_up();
	ss_init();
	ss_run();
	scp_init();				/* SCMI on the mailboxes */
	se_handshake_ready();
	scp_run();
}
