/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Crash information and watchdogs (CIX hal/common/crash, framework/fw_err_handler.c).
 *
 * The SE runs its own watchdog and takes the PM's and the AP's watchdog
 * interrupts.  On one, it saves what it can about the hang into the
 * "exception" area the AP registered in DRAM (records of name, offset and
 * size, each with a CRC) and resets the system.  DDR controller state from
 * an AP watchdog is kept in SE SRAM across the reset and copied to DRAM on
 * the next boot.
 */
#include "se.h"
#include "arch.h"
#include "ci700.h"
#include "crypto.h"
#include "ddr.h"
#include "drivers.h"
#include "harvest.h"
#include "mailbox.h"
#include "soc.h"
#include "storage.h"

#define IRQn_WDT_LVL1		32
#define IRQn_PM_WDOGRES		230
#define IRQn_AP_WDOG		232

#define EXC_HANDOFF		0x63da3e00u	/* SoC 0x83da3e00: AP's registration */
#define EXC_MAGIC		0xbab1face
#define RC_HDR_SIZE		0x308
#define RC_MAX_RECORDS		0x20

#define DDR_EINFO_SRAM		0x2007d800u
#define DDR_EINFO_SIZE		0x354
#define DDR_EINFO_CRC		0x2007db54u

#define S5_RESET_REASON		0x56000500u
#define S5_RESET		0x56000300u

/* Exception area: state, record count, records { name[16], offset, size } */
typedef struct {
	uint32_t state;		/* 1 dumped, 2 taken by the AP */
	uint32_t count;
	struct {
		char name[16];
		uint32_t offset;
		uint32_t size;
	} rec[RC_MAX_RECORDS];
} rc_hdr_t;

static rc_hdr_t *g_rc;
uint32_t g_sp;			/* stack pointer at the last interrupt (start.c) */

void reboot(uint8_t subreason)
{
	REG32(S5_RESET_REASON) = (uint32_t)subreason << 8 | 0x47;
	REG32(S5_RESET) = 1;
}

static void hc_set_exception_addr(void)
{
	if (REG32(EXC_HANDOFF + 8) == EXC_MAGIC) {
		g_rc = (rc_hdr_t *)(uintptr_t)((REG32(EXC_HANDOFF + 4) & 0x3fffffff) + 0x60000000);
		hal_printf_level(LOG_INFO, "g_rc: 0x%x\n", (unsigned)(uintptr_t)g_rc);
	} else {
		g_rc = NULL;
	}
}

static void hc_set_exception_handler(void *param, uint32_t plen, void *resp, uint32_t *rlen)
{
	const uint32_t *p = param;

	if (plen - 12 < 4) {
		REG32(EXC_HANDOFF) = p[0];
		REG32(EXC_HANDOFF + 4) = p[1];
		REG32(EXC_HANDOFF + 8) = p[2];
		hc_set_exception_addr();
	}
}

/* Find or add the record called name, sized for size bytes and a CRC */
void *hal_crash_get_eaddr(const char *name, uint32_t size)
{
	uint32_t i, n;

	if (g_rc == NULL || g_rc->state == 2 || g_rc->count >= RC_MAX_RECORDS)
		return NULL;
	n = g_rc->count;
	for (i = 0; i < n; i++) {
		if (strcmp(g_rc->rec[i].name, name))
			continue;
		if (g_rc->rec[i].size != size + 4) {
			g_rc->rec[i].size = size + 4;
			for (; i + 1 < n; i++)
				g_rc->rec[i + 1].offset = g_rc->rec[i].offset + g_rc->rec[i].size;
		}
		return (uint8_t *)g_rc + g_rc->rec[i].offset;
	}
	memset(g_rc->rec[n].name, 0, sizeof(g_rc->rec[n].name));
	memcpy(g_rc->rec[n].name, name, MIN(strlen(name), sizeof(g_rc->rec[n].name) - 1));
	g_rc->rec[n].offset = n ? g_rc->rec[n - 1].offset + g_rc->rec[n - 1].size : RC_HDR_SIZE;
	g_rc->rec[n].size = size + 4;
	g_rc->count = n + 1;
	return (uint8_t *)g_rc + g_rc->rec[n].offset;
}

/* CRC every record, push the area out of the system cache */
static void hal_crash_dump_done(void)
{
	uint32_t i, total = 0;
	uint8_t *rec;

	if (g_rc == NULL || g_rc->state == 2)
		return;
	g_rc->state = 1;
	for (i = 0; i < g_rc->count; i++) {
		rec = (uint8_t *)g_rc + g_rc->rec[i].offset;
		*(uint32_t *)(rec + g_rc->rec[i].size - 4) = crc32(rec, g_rc->rec[i].size - 4);
		total += g_rc->rec[i].size;
	}
	drv_ci700_slc_flush((uint32_t)(uintptr_t)g_rc + 0x20000000u, total + RC_HDR_SIZE);
	if (hal_secfg_get_mte_config())
		drv_ci700_flush_mte_tagcache();
}

/* AP cores' activity monitors */
static void amu_save_einfo(uint32_t half)
{
	uint32_t *p = hal_crash_get_eaddr("AMU", 0x380), cpus, cpu, *rec, i;

	if (p == NULL)
		return;
	p = (uint32_t *)((uint8_t *)p + (half & 1) * 0x1c0);
	cpus = fw_get_cpu_mask_by_harvest();
	p[0] = 0;
	p[1] = 0x24;
	p[2] = 0x10;
	p[3] = 0x1b0;
	for (cpu = 0; cpu < 12; cpu++) {
		if (!(cpus & BIT(cpu)))
			continue;
		rec = p + 4 + p[0] * 9;
		rec[0] = cpu;
		for (i = 0; i < 8; i++)
			rec[1 + i] = REG32(0x4f090000 + cpu * 0x100000 + i * 4);
		p[0]++;
	}
}

/* DDR controllers' state, kept in SE SRAM until the next boot */
static const uint32_t ddr_rcsu_regs_bases[4] = { 0x0c000000, 0x0c020000, 0x0c040000, 0x0c060000 };
static const uint16_t ctrl_regs_offsets[16] = {
	0x57c, 0x580, 0x584, 0x58c, 0x590, 0x594, 0x598, 0x59c,
	0x5a0, 0x5a4, 0x5a8, 0x5b0, 0x5b4, 0x5b8, 0x5bc, 0x5c0,
};

static void ddr_einfo_save_to_sram(void)
{
	uint32_t ch = drv_ddr_get_ch_num(), c, k, *e, base;

	REG32(DDR_EINFO_SRAM) = ch;
	for (c = ch == 2 ? 2 : 0; c < 4; c++) {
		base = ddr_rcsu_regs_bases[c];
		e = (uint32_t *)(DDR_EINFO_SRAM + c * 0xd4);
		e[1] = 0x11;
		e[2] = c;
		e[3] = 0x408;
		e[4] = 0;
		e[5] = REG32(base + 0x40000408);
		for (k = 0; k < 16; k++) {
			e[6 + k * 3] = ctrl_regs_offsets[k];
			e[7 + k * 3] = 1;
			e[8 + k * 3] = REG32(base + 0x40010000 + ctrl_regs_offsets[k]);
		}
	}
	REG32(DDR_EINFO_CRC) = crc32((const uint8_t *)DDR_EINFO_SRAM, DDR_EINFO_SIZE);
}

static bool ddr_check(void)
{
	return !((REG8(0x4c000408) | REG8(0x4c020408) | REG8(0x4c040408) | REG8(0x4c060408)) & 1);
}

static void ddr_einfo_copy_to_ddr(void)
{
	uint32_t n = REG32(DDR_EINFO_SRAM), i;
	void *dst;

	if (crc32((const uint8_t *)DDR_EINFO_SRAM, DDR_EINFO_SIZE) != REG32(DDR_EINFO_CRC) || n - 1 >= 4)
		return;
	for (i = 0; i < n; i++)
		if (REG32(DDR_EINFO_SRAM + i * 0xd4 + 4) != 0x11)
			return;
	dst = hal_crash_get_eaddr("DDR", DDR_EINFO_SIZE);
	if (dst) {
		memcpy(dst, (const void *)DDR_EINFO_SRAM, DDR_EINFO_SIZE);
		memset((void *)DDR_EINFO_SRAM, 0, DDR_EINFO_SIZE + 4);
	}
}

static void rc_save_soc_hang_info(void)
{
	amu_save_einfo(0);
	clk_save_einfo();
	pwr_save_einfo();
	amu_save_einfo(1);
	hal_crash_dump_done();
}

static void rc_ap_wdt_assert_policy(void *ctx)
{
	core_irq_disable(IRQn_AP_WDOG);
	ddr_einfo_save_to_sram();
	if (ddr_check())
		rc_save_soc_hang_info();
	hal_printf_level(LOG_INFO, "rc_ap_wdt_assert_policy enter, save regs and will reset later!\n");
	REG32(S5_RESET_REASON) = 0x147;
	REG32(S5_RESET) = 1;
}

static void hc_req_hardlock_handler(void *param, uint32_t plen, void *resp, uint32_t *rlen)
{
	rc_save_soc_hang_info();
}

/* SE watchdog: the SE's stack around the interrupt, its frame and fault registers */
static void rc_se_assert_policy(void *ctx)
{
	extern uint32_t __stack_limit[], __stack_top[];
	uint32_t *p, *sp = (uint32_t *)(uintptr_t)g_sp, i;
	mbox_msg_t msg = { 0 };

	core_irq_disable(IRQn_WDT_LVL1);
	p = hal_crash_get_eaddr("SE_WDT", 0x144);
	if (p) {
		const uint32_t *from = sp < __stack_limit ? __stack_limit :
				       sp + 64 > __stack_top ? __stack_top - 64 : sp;

		memcpy(p + 4, from, 0x100);
		p[0] = 0x10;
		p[1] = 0x100;
		if (sp >= __stack_limit && sp + 8 <= __stack_top) {
			for (i = 0; i < 8; i++)
				p[0x44 + i] = sp[i];
			p[0x4c] = REG32(SCB_CFSR);
			p[0x4d] = REG32(SCB_HFSR);
			p[0x4e] = REG32(0xe000ed30);
			p[0x4f] = REG32(SCB_BFAR);
			p[0x50] = REG32(SCB_MMFAR);
			p[2] = 0x110;
			p[3] = 0x34;
		}
	}
	hal_crash_dump_done();
	msg.cmd_id = 0x40000001;
	msg.size = 8;
	if (hal_ipc_send_data(MBOX_CHN_SE2AP_REE, &msg))
		hal_printf_level(LOG_WARN, "SE crash report failed\n");
}

/* PM watchdog: the PM's debug registers and the last 256 bytes of its RAM log */
static const uint32_t pm_debug_regs[46] = {
	0x6510400, 0x6510404, 0x6510408, 0x6510500, 0x6510504, 0x6510508, 0x651050c, 0x6510510,
	0x6510514, 0x6510518, 0x651051c, 0x6510520, 0x6510524, 0x6510528, 0x651052c, 0x6510530,
	0x6510534, 0x6510538, 0x651053c, 0x6510540, 0x6510544, 0x6510548, 0x6510580, 0x6510584,
	0x6510588, 0x651058c, 0x6510590, 0x6510594, 0x6510598, 0x651059c, 0x65105a0, 0x65105a4,
	0x65105a8, 0x65105ac, 0x65105b0, 0x65105b4, 0x65105b8, 0x65105bc, 0x65105c0, 0x65105c4,
	0x65105c8, 0x6020200, 0x6020300, 0x6020304, 0x6020400, 0x6020404,
};

static void rc_pm_assert_policy(void *ctx)
{
	uint32_t *p, i, a;

	core_irq_disable(IRQn_PM_WDOGRES);
	p = hal_crash_get_eaddr("PM_WDT", 0x374);
	if (p) {
		for (i = 0; i < ARRAY_SIZE(pm_debug_regs); i++) {
			p[1 + i * 2] = pm_debug_regs[i];
			p[2 + i * 2] = REG32(pm_debug_regs[i] + 0x40000000);
		}
		for (a = 0x46acef00, i = ARRAY_SIZE(pm_debug_regs); a < 0x46acf000; a += 4, i++) {
			p[1 + i * 2] = a - 0x40000000;
			p[2 + i * 2] = REG32(a);
		}
		p[0] = 0x6e;
	}
	hal_crash_dump_done();
	reboot(3);
}

void hal_crash_feed_dog(void)
{
	drv_wdt_feed();
}

uint32_t hal_crash_init(uint32_t is_str)
{
	uint32_t err;

	core_irq_register(IRQn_WDT_LVL1, rc_se_assert_policy, NULL);
	core_irq_enable(IRQn_WDT_LVL1);
	err = drv_wdt_init(10000, 0);
	if (!err)
		err = drv_wdt_start();
	if (err) {
		SE_ERR(err);
		return err;
	}
	hal_printf_level(LOG_ERR, "[SE_FW]# SE wdt start...\n");
	core_irq_register(IRQn_PM_WDOGRES, rc_pm_assert_policy, NULL);
	core_irq_enable(IRQn_PM_WDOGRES);
	if (is_str)
		return 0;
	hc_set_exception_addr();
	ddr_einfo_copy_to_ddr();
	hal_crash_dump_done();
	core_irq_register(IRQn_AP_WDOG, rc_ap_wdt_assert_policy, NULL);
	core_irq_enable(IRQn_AP_WDOG);
	err = fw_dispatcher_reg_handler(0x82000015, hc_set_exception_handler);
	if (!err)
		err = fw_dispatcher_reg_handler(0x82000017, hc_req_hardlock_handler);
	if (err)
		SE_ERR(err);
	return err;
}

void hal_crash_deinit(void)
{
	core_irq_unregister(IRQn_WDT_LVL1);
	core_irq_disable(IRQn_WDT_LVL1);
	drv_wdt_deinit();
	core_irq_unregister(IRQn_PM_WDOGRES);
	core_irq_disable(IRQn_PM_WDOGRES);
}

/* Faults: report the stacked frame, then reset */
void hard_fault_policy(uint32_t exc_return, uint32_t *sp)
{
	hal_pc_trace(0x9201);
	hal_printf_level(LOG_ERR, "fault_handler_lr: 0x%08x\n", (unsigned)exc_return);
	hal_printf_level(LOG_ERR, "fault_handler_sp: 0x%08x\n", (unsigned)(uintptr_t)sp);
	if (!(exc_return & BIT(2)) && sp)
		hal_printf_level(LOG_ERR, "r0 %08x r1 %08x r2 %08x r3 %08x r12 %08x lr %08x pc %08x xpsr %08x\n",
				 (unsigned)sp[2], (unsigned)sp[3], (unsigned)sp[4], (unsigned)sp[5],
				 (unsigned)sp[6], (unsigned)sp[7], (unsigned)sp[8], (unsigned)sp[9]);
	hal_printf_level(LOG_ERR, "CFSR %08x HFSR %08x MMFAR %08x BFAR %08x\n", (unsigned)REG32(SCB_CFSR),
			 (unsigned)REG32(SCB_HFSR), (unsigned)REG32(SCB_MMFAR), (unsigned)REG32(SCB_BFAR));
	hal_printf_level(LOG_ERR, "System will reboot\n");
	hal_pc_trace(0x9204);
	reboot(4);
	for (;;)
		;
}

void fw_err_handler(uint32_t err)
{
	hal_printf_level(LOG_ERR, "Error in debug mode\n");
	hal_printf_level(LOG_ERR, "System will reboot...\n");
	reboot(5);
}
