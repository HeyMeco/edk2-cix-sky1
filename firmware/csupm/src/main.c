/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * CSU_PM firmware: the power-on sequence (CIX's ss_por_enter), the SE
 * handshake, then idle.
 */
#include "i2c.h"
#include "lcru.h"
#include "uart.h"

#define I2C_HZ			400000u
/* I2C 0-2 are masters (PMICs), 3 a slave, on the boards CIX supports */
#define I2C_MASTERS		0x7u
#define I2C_SLAVES		0x8u

/* The SE waits up to 5 s for each step (firmware/se/service/main.c) */
static void se_handshake_up(void)
{
	REG32(PM_MAGIC) = PM_MAGIC_UP;
}

static void se_handshake_ready(void)
{
	while (REG32(PM_MAGIC) != PM_MAGIC_SE_DONE)
		;
	REG32(PM_MAGIC) = 0;
	REG32(PM_MAGIC) = PM_MAGIC_READY;
}

__attribute__((noinline)) void arch_idle(void)
{
	/* nothing to wait for yet: no interrupt is enabled */
	__asm__ volatile("" ::: "memory");
}

int main(void)
{
	lcru_core_clock(1);			/* 400 MHz */
	scratch_clear();
	lcru_axprot();
	wdt_off();
	lcru_mbox_reset(0x3ff);
	uart_init();
	uart_puts("CSU_PM: open firmware\n");
	i2c_setup(I2C_HZ, I2C_MASTERS, I2C_SLAVES);
	se_handshake_up();
	se_handshake_ready();
	for (;;)
		arch_idle();
}
