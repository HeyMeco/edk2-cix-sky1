/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * What the power sequence needs of CIX's drivers: polling a register
 * (Source/common/soc/soc_reg_access.c's soc_pull_register), the warm reset
 * (Source/common/soc/sys/sys_ctrl.c), the S5 power controller's interrupt
 * to the PM (Source/projects/sky1/intr/intr_mgmt.c), the mailboxes' resets
 * (dev_mailbox.c's dev_mbox_toggle_all_rst) and the AP's system counter
 * across suspend (dev_counters.c's handle_counter_before_sleep and
 * recover_counter_after_sleep).
 */
#include "hsm/hsm.h"
#include "irq.h"
#include "lcru.h"
#include "mbox.h"

#define PULL_ADDR		(PM_SCRATCH + 0x64)	/* the register soc_pull_register waits on */

/*
 * CIX's soc_pull_register: wait until (*addr & mask) == (value & mask),
 * checking every 20 cycles (200 for timeouts above 1000), at most
 * timeout_cycles core cycles; 0 waits for ever (CIX logs every 300 ms of
 * it).  The register being polled is in a scratch word meanwhile.
 */
bool soc_pull_register(uint32_t addr, uint32_t mask, uint32_t value, uint32_t timeout_cycles)
{
	uint32_t want = value & mask, start, step;

	REG32(PULL_ADDR) = addr;
	if ((REG32(addr) & mask) != want) {
		start = ccount();
		step = timeout_cycles > 1000 ? 200 : 20;
		for (;;) {
			cc_delay(step);
			if ((REG32(addr) & mask) == want)
				break;
			if (timeout_cycles && ccount() - start >= timeout_cycles) {
				REG32(PULL_ADDR) = 0;
				return false;
			}
		}
	}
	REG32(PULL_ADDR) = 0;
	return true;
}

/*
 * CIX's soc_warm_reset: with a reason, it goes to the S5 software register
 * (reason << 8 | 0x46) first; then the S5 controller's warm reset.
 */
void soc_warm_reset(uint32_t reason)
{
	if (reason)
		REG32(S5_SW_REASON) = (reason << 8) + 0x46;
	REG32(S5_WARM_RST) = 1;
	for (;;)
		;
}

/*
 * CIX's pm_ctrls5_intr_enable: the S5 controller's interrupt to the PM
 * cleared, routed through aggregator line 18 and enabled (CIX asserts it
 * is not pending already).  CIX's intr_platform_init enables core line 18
 * at boot; here it is enabled from now until the PTS state is left.
 */
void pm_ctrls5_intr_enable(void)
{
	uint32_t v;

	REG32(S5_CSU_PM_INTR) = 0;
	REG32(AGG_ENABLE(S5_PM_IRQ_LINE)) = 1;
	REG32(S5_CSU_PM_INTR_EN) = 1;
	__asm__ volatile("rsr.intenable %0" : "=a"(v));
	v |= BIT(S5_PM_IRQ_LINE);
	__asm__ volatile("wsr.intenable %0; rsync" : : "a"(v) : "memory");
}

/* CIX's pm_ctrls5_intr_is_fired: the aggregator's raw status */
bool pm_ctrls5_intr_is_fired(void)
{
	return REG32(AGG_STATUS(S5_PM_IRQ_LINE)) & 1;
}

/* CIX's pm_ctrls5_intr_mask_off (ss_pts_exit) */
void pm_ctrls5_intr_mask_off(void)
{
	uint32_t v;

	REG32(AGG_ENABLE(S5_PM_IRQ_LINE)) = 0;
	__asm__ volatile("rsr.intenable %0" : "=a"(v));
	v &= ~BIT(S5_PM_IRQ_LINE);
	__asm__ volatile("wsr.intenable %0; rsync" : : "a"(v) : "memory");
}

/*
 * CIX's dev_mbox_toggle_all_rst: the mailboxes in mask (bit i: mailbox i)
 * into reset and out again, the others left as they are.
 */
void dev_mbox_toggle_all_rst(uint32_t mask)
{
	uint32_t bits = (mask << RESET_MBOX_SHIFT) & RESET_MBOX_ALL;
	uint32_t v = REG32(LCRU_RESET) & ~bits;

	REG32(LCRU_RESET) = v;
	cc_delay(20);
	REG32(LCRU_RESET) = v | bits;
}

/*
 * The AP's system counter (SoC 0x16002000: control, count at +8) runs
 * from the S5 divider at 0x56000600 [9:0]; while the SoC sleeps it counts
 * 100 times slower (CIX's handle_counter_before_sleep).  On wake-up the
 * count is corrected for the time asleep (recover_counter_after_sleep):
 * now + (now - before) * 100.
 */
#define AP_CNT_DIV		0x56000600u
#define AP_CNT_DIV_MASK		0x3ffu
#define AP_CNT_CTRL		0x56002000u

static uint64_t cnts_before_sleep;

void handle_counter_before_sleep(void)
{
	uint32_t div = div_ctrl_get_divisor(AP_CNT_DIV, AP_CNT_DIV_MASK) * 100;

	cnts_before_sleep = dev_cnts_get_ap_free_counter();
	div_ctrl_set_divisor(AP_CNT_DIV, AP_CNT_DIV_MASK, div);
}

void recover_counter_after_sleep(void)
{
	uint32_t div = div_ctrl_get_divisor(AP_CNT_DIV, AP_CNT_DIV_MASK);
	uint64_t now, count;

	div_ctrl_set_divisor(AP_CNT_DIV, AP_CNT_DIV_MASK, div / 100);
	now = dev_cnts_get_ap_free_counter();
	/* CIX logs when the counter went backwards */
	count = now + (now - cnts_before_sleep) * 100;
	REG32(AP_CNT_CTRL) &= ~1u;
	REG32(AP_CNT_CTRL + 8) = (uint32_t)count;
	REG32(AP_CNT_CTRL + 12) = (uint32_t)(count >> 32);
	REG32(AP_CNT_CTRL) |= 1;
	cnts_before_sleep = 0;
}
