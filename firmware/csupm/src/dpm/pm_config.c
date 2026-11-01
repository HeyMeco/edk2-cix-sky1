/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The PM configuration the BIOS leaves above DRAM1 (CIX's
 * Source/common/drivers/pm_config.c): do_pm_config's check of the block
 * and what it configures, in CIX's order: the log, the board's rail
 * layout (pmic_scheme_config), the fans, the OPP tables, the PVT sensors
 * (thermal trip, weights, board sensors), whether the fused minimum
 * voltages are used, the skin temperature's controllers, the watchdog, the GPIOs' functions, the MP2845s' PSI
 * setting; and what pm_ready configures from it once the AP may run
 * (noc_idle_config, op_mode_config).
 *
 * The block at 0x200bf000: a 16-bit version (3) at +0, its length at +8,
 * two sums at +0x10 and +0x14 (of the words, and of the running sums,
 * computed with both fields zero), then CIX's pm_config_t at +0x18, packed.
 * A config_data_t is valid when its bit 0 is clear (erased flash is all
 * ones), its value in bits 1-31.
 */
#include "dpm.h"
#include "dpm_rt.h"
#include "fan/fan.h"
#include "lcru.h"
#include "log/log.h"
#include "pvt.h"
#include "board/board.h"

#define BIOS_CFG		0x200bf000u
#define BIOS_CFG_VERSION	3
#define BIOS_CFG_MAX		0x1000u
#define PM_CONFIG		(BIOS_CFG + 0x18)	/* pm_config_t */

/* offsets in pm_config_t */
#define CFG_PMIC_SCHEME		0x000	/* pm_config_pmic_t: scheme, opp_max[13], edp_cfg[8] */
#define CFG_PMIC_OPP_MAX	0x004
#define CFG_PMIC_EDP		0x01e
#define CFG_PVT			0x05e	/* pm_config_pvt_t */
#define CFG_OPP_VALID		0x080	/* pm_config_opp_t opp_config */
#define CFG_OPPS		0x081	/* domain_opp_config_t opps[13], 212 bytes each */
#define CFG_FAN			0xb45	/* pm_config_fan_t fan_config[3] */
#define CFG_LOG			0xce3	/* the log's: DRAM log magic, UART rate */
#define CFG_VMIN_DISABLE	0xceb
#define CFG_VMIN_PROFILE	0xcef
#define CFG_NOC_IDLE		0xcf3	/* enable, two AMU thresholds, DDR power, hysteresis */
#define CFG_SPT_MASK		0xd07	/* pm_config_spt_t: enables, set points[4], margin, */
#define CFG_SPT_SETPOINT	0xd0b	/* the skin model's kh, ka, c, alpha */
#define CFG_SPT_MARGIN		0xd1b
#define CFG_SPT_KH		0xd1f
#define CFG_SPT_KA		0xd23
#define CFG_SPT_C		0xd27
#define CFG_SPT_ALPHA		0xd2b
#define SPT_COEFF_UNIT		1.0e-5f
#define CFG_WDT_TIMEOUT		0xd2f
#define CFG_OPP_100M_ENABLE	0xd33
#define CFG_GPIO		0xd37	/* three */
#define CFG_OP_MODE		0xd43
#define CFG_PSI7_MODE		0xd47
#define VMIN_ENABLE_MAGIC	0x67997cdau

bool bios_cfg_valid;

static uint8_t cfg8(uint32_t offset)
{
	return REG8(PM_CONFIG + offset);
}

/* CIX's pm_config_get()->field for a 32-bit field (packed, byte reads) */
uint32_t pm_config_get_word(uint32_t offset)
{
	return (uint32_t)cfg8(offset) | (uint32_t)cfg8(offset + 1) << 8 |
	       (uint32_t)cfg8(offset + 2) << 16 | (uint32_t)cfg8(offset + 3) << 24;
}

/* a config_data_t: its value when valid */
static bool cfg_data(uint32_t offset, uint32_t *value)
{
	uint32_t v = pm_config_get_word(offset);

	*value = v >> 1;
	return !(v & 1);
}

/* CIX's pm_config_get */
const uint8_t *pm_config_get(void)
{
	return (const uint8_t *)(uintptr_t)PM_CONFIG;
}

/* The BIOS's OPPs for DVFS element idx (domain_opp_config_t: size,
 * sustained index, 13 OPPs) */
const uint8_t *pm_config_get_opp(uint32_t idx)
{
	return (const uint8_t *)(uintptr_t)(PM_CONFIG + CFG_OPPS + 212 * idx);
}

/*
 * CIX's get_pmic_scheme: the layout word as it is, whether or not the
 * block is valid: a valid preset (0-7), else EVB (as for erased flash; the
 * MP5475s' status then covers all three PMICs)
 */
enum pmic_preset get_pmic_scheme(void)
{
	uint32_t v = REG32(PM_CONFIG + CFG_PMIC_SCHEME);

	return (v & 0xfffffff1u) ? PMIC_EVB : (enum pmic_preset)(v >> 1);
}

/* CIX's do_pm_config_crc_check */
static bool do_pm_config_crc_check(void)
{
	volatile uint32_t *sum1 = (volatile uint32_t *)(BIOS_CFG + 0x10);
	volatile uint32_t *sum2 = (volatile uint32_t *)(BIOS_CFG + 0x14);
	uint32_t len = REG32(BIOS_CFG + 8), s1, s2, a = 0, b = 0;

	if (len > BIOS_CFG_MAX || REG16(BIOS_CFG) != BIOS_CFG_VERSION)
		return false;
	s1 = *sum1;
	s2 = *sum2;
	*sum2 = 0;
	*sum1 = 0;
	for (uint32_t i = 0; i < len; i += 4) {
		a += REG32(BIOS_CFG + i);
		b += a;
	}
	*sum2 = s2;
	*sum1 = s1;
	return s1 == a && s2 == b;
}

/*
 * CIX's pmic_scheme_config: a valid layout word picks the layout (the
 * board found by probing otherwise, src/pmic.c); CUSTOM (0) is the BIOS's
 * own, OPP maxima and rails
 */
static void pmic_scheme_config(void)
{
	uint32_t v;

	if (!cfg_data(CFG_PMIC_SCHEME, &v))
		return;
	if (!(v & 0xff))
		pmic_set_custom_scheme(pm_config_get() + CFG_PMIC_OPP_MAX,
				       pm_config_get() + CFG_PMIC_EDP);
	if ((v & 0xff) < PMIC_PRESETS)
		pmic_config_preset((enum pmic_preset)(v & 0xff));
}

/* The OPP tables the BIOS gives: those whose size is set; bit 7 of
 * opp_valid limits the override to them, else all domains count as given */
static void opp_table_config(void)
{
	uint8_t opp_valid = cfg8(CFG_OPP_VALID);
	uint32_t mask = 0;

	if (opp_valid & 0x7f)
		return;
	for (uint32_t i = 0; i < 12; i++) {
		const uint8_t *o = pm_config_get_opp(i);

		if (((uint32_t)o[0] | (uint32_t)o[1] << 8) == 0xffff)
			continue;
		override_config_dvfs_opp_source((uint8_t)i);
		mask |= 1u << i;
	}
	set_config_dvfs_source(opp_valid & 0x80 ? mask | 1u << 30 : 0x80000000u);
}

/* The fused minimum voltages are used when the BIOS says so (a magic) */
static void vmin_config(void)
{
	uint32_t v = pm_config_get_word(CFG_VMIN_DISABLE);
	uint32_t p;

	if (!(v & 1)) {
		if ((v & ~1u) == VMIN_ENABLE_MAGIC)
			cfg_vf_fuse_enable();
		return;
	}
	p = pm_config_get_word(CFG_VMIN_PROFILE);
	if (!(p & 1))
		cfg_vf_fuse_set_profile(p >> 1);
}

/*
 * CIX's spt_config: the skin temperature's (SPT) controllers of CIX's DPM
 * runtime: which of the four are on, their set points (degrees), the
 * margin, and the skin model's coefficients in 1e-5 units (kh, ka and c
 * only together; alpha alone).
 */
static void spt_config(void)
{
	uint32_t mask, v, kh, ka, c;
	float fkh, fka, fc, falpha;
	bool update = false;

	if (!cfg_data(CFG_SPT_MASK, &mask))
		return;
	for (uint32_t i = 0; i < 4; i++) {
		spt_ctrls[i].is_enabled = (mask & BIT(i)) != 0;
		if ((mask & BIT(i)) && cfg_data(CFG_SPT_SETPOINT + 4 * i, &v))
			pid_setSetPoint(spt_ctrls[i].pid, (float)(int32_t)v);
	}
	if (cfg_data(CFG_SPT_MARGIN, &v))
		spt_set_skin_margin((float)(int32_t)v);
	config_skin_temp_params_get(&fkh, &fka, &fc, &falpha);
	if (cfg_data(CFG_SPT_KH, &kh) && cfg_data(CFG_SPT_KA, &ka) &&
	    !(pm_config_get_word(CFG_SPT_C) & 1)) {
		c = pm_config_get_word(CFG_SPT_C);
		fkh = (float)(int32_t)kh * SPT_COEFF_UNIT;
		fka = (float)(int32_t)ka * SPT_COEFF_UNIT;
		fc = (float)((int32_t)c >> 1) * SPT_COEFF_UNIT;
		update = true;
	}
	if (cfg_data(CFG_SPT_ALPHA, &v)) {
		falpha = (float)(int32_t)v * SPT_COEFF_UNIT;
		update = true;
	}
	if (update)
		config_skin_temp_params_set(fkh, fka, fc, falpha);
}

/* --- the local watchdog (CIX's wdt_set, wdt_config, wdt_restore; dev_wdt.c) -- */

/*
 * An Arm SP805 at 0xa0050000 counting at 25 MHz; its first time-out
 * interrupts (aggregator line 21, source 0: src/board's wdt_intr_handler
 * reloads it), a second one resets the PM.  Its registers are locked but
 * between 0x1acce551 and 0x55aa in the lock register.
 */
#define WDT_BASE		0xa0050000u
#define WDT_LOAD		0x000
#define WDT_CONTROL		0x008
#define WDT_INTCLR		0x00c
#define WDT_LOCK		0xc00
#define WDT_UNLOCK_KEY		0x1acce551u
#define WDT_LOCK_KEY		0x55aau
#define WDT_HZ_PER_US		25
#define WDT_INTEN_RESEN		3
#define LOCAL_WDT_ON		0x10

static uint32_t wdt_timeouts;

/* CIX's _dev_wdg_enable_reg_access / _dev_wdg_disable_reg_access */
static void wdt_access(bool on)
{
	REG32(WDT_BASE + WDT_LOCK) = on ? WDT_UNLOCK_KEY : WDT_LOCK_KEY;
	(void)REG32(WDT_BASE + WDT_LOCK);
}

/* CIX's dev_wdt_refresh */
void dev_wdt_refresh(void)
{
	wdt_access(true);
	REG32(WDT_BASE + WDT_INTCLR) = 1;
	wdt_access(false);
}

/* CIX's wdt_set: the watchdog reset, then off or counting timeout_us */
static void wdt_set(uint32_t timeout_us)
{
	lcru_reset(RESET_WDT, false);		/* dev_wdt_init */
	lcru_reset(RESET_WDT, true);		/* dev_wdt_set_base */
	if (timeout_us) {
		uint32_t load = timeout_us * WDT_HZ_PER_US;

		wdt_access(true);		/* dev_wdt_set_timeout */
		REG32(WDT_BASE + WDT_LOAD) = load;
		REG32(WDT_BASE + WDT_INTCLR) = load;
		(void)REG32(WDT_BASE + WDT_LOAD);
		REG32(WDT_BASE + WDT_CONTROL) = WDT_INTEN_RESEN;
		wdt_access(false);
		REG32(LOCAL_WDT_CTRL) = LOCAL_WDT_ON;	/* dev_wdt_enable */
	} else {
		REG32(LOCAL_WDT_CTRL) = 0;	/* dev_wdt_disable */
	}
	wdt_timeouts = timeout_us;
}

/* CIX's wdt_restore: after suspend, as the configuration had it */
void wdt_restore(void)
{
	wdt_set(wdt_timeouts);
}

static void wdt_config(void)
{
	uint32_t us;

	if (cfg_data(CFG_WDT_TIMEOUT, &us))
		wdt_set(us);
}

/* CIX's gpio_config: which local GPIO tells the EC about fan changes, which
 * one blinks (src/board's dev_gpio.c) */
static void gpio_config(void)
{
	for (uint32_t i = 0; i < 3; i++) {
		uint32_t v;

		if (!cfg_data(CFG_GPIO + 4 * i, &v))
			continue;
		if ((v & 0xff) == PM_GPIO_FUNC_HEARTBEAT)
			gpio_heartbeat = i;
		else if ((v & 0xff) == PM_GPIO_FUNC_EC)
			gpio_to_ec = i;
	}
}

/* CIX's psi7_mode_config: only a valid 0 turns the MP2845s' PSI setting
 * off */
static void psi7_mode_config(void)
{
	if (pm_config_get_word(CFG_PSI7_MODE) == 0)
		config_enable_psi7 = false;
}

/* CIX's do_pm_config (power-on, before the fuses) */
void do_pm_config(void)
{
	bios_cfg_valid = do_pm_config_crc_check();
	if (!bios_cfg_valid)
		return;
	pm_log_config((const uint8_t *)(uintptr_t)(PM_CONFIG + CFG_LOG));
	pmic_scheme_config();
	fan_data_config(pm_config_get() + CFG_FAN);
	opp_table_config();
	sky1_pvt_data_config(pm_config_get() + CFG_PVT);
	vmin_config();
	spt_config();
	wdt_config();
	gpio_config();
	psi7_mode_config();
}

/* Without the BIOS's say-so the CPU clusters and DSU start at 200 MHz and
 * the GPU at 60, not 100 */
void override_100M_opp(void)
{
	if (bios_cfg_valid && !(cfg8(CFG_OPP_100M_ENABLE) & 1))
		return;
	for (uint32_t i = DPM_DxS_CORE_LIT; i <= DPM_DxS_DSU; i++)
		override_config_dvfs_minimum_level(i, 200, true);
	override_config_dvfs_minimum_level(DPM_DxS_GPU_CORE, 60, true);
	override_config_dvfs_minimum_level(DPM_DxS_GPU_TOP, 60, true);
}

/*
 * CIX's noc_idle_config (pm_ready): bit 0 of the value turns the SoC idle
 * switch on (with the thresholds the BIOS gives, each when valid: two AMU
 * ones, the DDR's dynamic power, a hysteresis), bit 1 steps the DSU with
 * the CPU clusters.  Read whether or not the block is valid, as CIX's.
 */
void noc_idle_config(void)
{
	uint32_t v = pm_config_get_word(CFG_NOC_IDLE);
	uint32_t thro[5] = { 0 }, x;
	bool set = false;

	if (v & 1)
		return;
	if (v & 2) {
		perf_soc_idle_switch_get_thro(&thro[0], &thro[1], &thro[2], &thro[3], &thro[4]);
		for (uint32_t i = 1; i < 4; i++) {
			if (cfg_data(CFG_NOC_IDLE + 4 * i, &x)) {
				thro[i - 1] = x;
				set = true;
			}
		}
		if (cfg_data(CFG_NOC_IDLE + 16, &x)) {
			thro[3] = x;
			set = true;
		}
		if (set)
			perf_soc_idle_switch_set_thro(thro[0], thro[1], thro[2], thro[3], thro[4]);
		perf_soc_idle_switch_set_enable(true);
		v = pm_config_get_word(CFG_NOC_IDLE);
	}
	if (v & 4)
		sky1soc_set_dsu_dvfs(true);
}

/* CIX's op_mode_config (pm_ready): the SoC idle switch, 1 on, 0 off */
void op_mode_config(void)
{
	uint32_t v;

	if (!cfg_data(CFG_OP_MODE, &v) || v > 1)
		return;
	perf_soc_idle_switch_set_enable(v == 1);
}
