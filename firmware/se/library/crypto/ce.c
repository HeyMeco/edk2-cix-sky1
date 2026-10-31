/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Arm China CE-Lite crypto engine, the parts the SE firmware drives itself:
 * bring-up and configuration lock, the TRNG and the OTP shadow registers
 * holding the keys the engine's key ladder uses.  Register use follows
 * CIX's SE firmware (library/ce_lite: ce_common.c, ce_config.c, ce_trng.c,
 * ce_otp.c); hashing and public-key operations run in software (Mbed TLS).
 */
#include "se.h"
#include "platform.h"
#include "crypto.h"

#define CE_TOP_STAT_VER		(SE_CE_BASE + 0x004)
#define CE_TOP_STAT_CFG0	(SE_CE_BASE + 0x008)	/* host counts per engine */
#define CE_TOP_STAT_CFG1	(SE_CE_BASE + 0x00c)	/* SRAM size, queue depths */
#define CE_TOP_STAT_CFG2	(SE_CE_BASE + 0x010)	/* OTP and TRNG presence */
#define CE_TOP_STAT_POOL	(SE_CE_BASE + 0x014)	/* bit 24: context pool ready */
#define CE_TOP_STAT_INTR	(SE_CE_BASE + 0x020)	/* bit 5: context SRAM overflow */
#define CE_TOP_STAT_CTX		(SE_CE_BASE + 0x024)	/* context numbers, security */
#define CE_OTP_CFG0		(SE_CE_BASE + 0x0d8)
#define CE_OTP_CFG1		(SE_CE_BASE + 0x0dc)

#define CE_HOST0		(SE_CE_BASE + 0x3000)
#define CE_ACA_CFG		(CE_HOST0 + 0x144)
#define CE_ACA_INTR_MSK		(CE_HOST0 + 0x164)
#define CE_SCA_CTL		(CE_HOST0 + 0x20c)
#define CE_HASH_CTL		(CE_HOST0 + 0x28c)
#define CE_RNP_CTRL		(CE_HOST0 + 0x300)
#define CE_RNP_STAT		(CE_HOST0 + 0x304)
#define CE_RNP_INTR		(CE_HOST0 + 0x308)
#define CE_RNP_DATA		(CE_HOST0 + 0x310)	/* 8 words */
#define CE_TRNG_KEY_GEN		(CE_HOST0 + 0x380)
#define CE_TRNG_SRC		(CE_HOST0 + 0x384)
#define CE_TRNG_SAMPLE		(CE_HOST0 + 0x388)
#define CE_TRNG_RO		(CE_HOST0 + 0x38c)
#define CE_TRNG_POSTPROC	(CE_HOST0 + 0x390)
#define CE_TRNG_EVAL		(CE_HOST0 + 0x394)
#define CE_TRNG_STAT		(CE_HOST0 + 0x398)	/* bit 0: hardware key generated */
#define CE_TRNG_INTR		(CE_HOST0 + 0x39c)
#define CE_OTP_DEV_ROOT_KEY	(CE_HOST0 + 0x620)	/* OTP offset 0x18 */
#define CE_OTP_MODEL_KEY	(CE_HOST0 + 0x630)	/* OTP offset 0x04 */
#define CE_OTP_LCS		(CE_HOST0 + 0x61c)	/* [3:0] one-hot life cycle, bit 4 lock */
#define CE_TOP_CFG		(CE_HOST0 + 0x708)	/* bit 0: configuration locked, bit 1: SW init done */
#define CE_ARB_CFG0		(CE_HOST0 + 0x710)
#define CE_ARB_CFG1		(CE_HOST0 + 0x714)
#define CE_ARB_CFG2		(CE_HOST0 + 0x718)

#define CE_ERROR_GENERIC	0x9c00
#define CE_ERROR_BAD_PARAMS	0x9c06
#define CE_ERROR_ACCESS_DENIED	0x9c01
#define CE_ERROR_NOT_SUPPORTED	0x9c0a
#define CE_ERROR_NO_OTP		0x9d01

#define LOG_PREFIX		"CRYPTO_LITE "

/* TRNG entropy source, sampling, ring oscillators, post-processing, evaluation */
static const uint32_t g_trng_conf[5] = { 0x0000000f, 0x0003000a, 0x00007654, 0x00004001, 0x00572e4d };

static struct {
	uint8_t otp_exist;
	uint16_t otp_tst_sz, otp_s_sz, otp_ns_sz;
} g_otp_cfg;

static void ce_config_hw(void)
{
	hal_printf_level(LOG_INFO, "%s Configure CE HW...\n", LOG_PREFIX);
	REG32(CE_ARB_CFG0) = 0x0f186186 | (REG32(CE_ARB_CFG0) & 0xf0000000);
	REG32(CE_ARB_CFG1) = 0x0f104104 | (REG32(CE_ARB_CFG1) & 0xf0000000);
	REG32(CE_ARB_CFG2) = 0x00104104 | (REG32(CE_ARB_CFG2) & 0xff000000);
}

static void trng_engine_init(void)
{
	REG32(CE_TRNG_SRC) = g_trng_conf[0];
	REG32(CE_TRNG_SAMPLE) = g_trng_conf[1];
	REG32(CE_TRNG_RO) = g_trng_conf[2];
	REG32(CE_TRNG_POSTPROC) = g_trng_conf[3];
	REG32(CE_TRNG_EVAL) = g_trng_conf[4];
	if (!(REG32(CE_TRNG_STAT) & 1)) {
		REG32(CE_TRNG_KEY_GEN) = 1;
		hal_printf_level(LOG_INFO, "%s Check TRNG HW key generate done flag!\n", LOG_PREFIX);
		while (!(REG32(CE_TRNG_STAT) & 1))
			;
		hal_printf_level(LOG_INFO, "%s Check done!\n", LOG_PREFIX);
	}
}

static uint32_t ce_otp_drv_init(void)
{
	uint32_t cfg0 = REG32(CE_OTP_CFG0);

	g_otp_cfg.otp_exist = (REG32(CE_TOP_STAT_CFG2) >> 1) & 1;
	g_otp_cfg.otp_s_sz = (cfg0 & 0x7ff) << 2;
	g_otp_cfg.otp_ns_sz = ((cfg0 >> 12) & 0x7ff) << 2;
	g_otp_cfg.otp_tst_sz = (REG32(CE_OTP_CFG1) & 0x7ff) << 2;
	return g_otp_cfg.otp_exist ? CE_ERROR_NO_OTP : 0;
}

static const char *sec(uint32_t v, uint32_t bit)
{
	return v & bit ? "Secure" : "Non-Secure";
}

/*
 * Check the engine has host 0 everywhere, set the arbitration and lock the
 * configuration (until the engine is reset), start the sub-engines.
 */
uint32_t ce_drv_init(void)
{
	static const char *const rel[] = { "Beta REL", "LAC REL", "EAC REL", "Full REL" };
	uint32_t cfg = REG32(CE_TOP_STAT_CFG0), v, err;

	hal_printf_level(LOG_INFO, "%s TOP_STAT_CFG0_STAT:0x%x\n", LOG_PREFIX, (unsigned)cfg);
	if (!(cfg & 0xf8000) || !(cfg & 0x7c00) || !(cfg & 0x3e0) || !(cfg & 0x1f)) {
		hal_printf_level(LOG_INFO, "%s Bad host id: %d, no such host for %s!\n", LOG_PREFIX, 0,
				 !(cfg & 0xf8000) ? "TRNG Pool" : !(cfg & 0x7c00) ? "ACA" :
				 !(cfg & 0x3e0) ? "SCA" : "HASH");
		SE_ERR(CE_ERROR_GENERIC);
		return CE_ERROR_GENERIC;
	}
	v = REG32(CE_TOP_CFG);
	hal_printf_level(LOG_INFO, "%s TOP_CTL_TOP_CFG:0x%x\n", LOG_PREFIX, (unsigned)v);
	if (!(v & 1)) {
		ce_config_hw();
		hal_printf_level(LOG_INFO, "%s Warning! This will lock the hardware configurations!\n",
				 LOG_PREFIX);
		REG32(CE_TOP_CFG) |= 1;
		while (!(REG32(CE_TOP_STAT_POOL) & BIT(24))) {
			if (REG32(CE_TOP_STAT_INTR) & BIT(5)) {
				hal_printf_level(LOG_INFO, "%s Context SRAM overflow!\n", LOG_PREFIX);
				SE_ERR(CE_ERROR_GENERIC);
				return CE_ERROR_GENERIC;
			}
		}
		hal_printf_level(LOG_INFO, "%s CE context pool is ready!\n", LOG_PREFIX);
	} else {
		hal_printf_level(LOG_INFO, "%s CE initialization is locked!\n", LOG_PREFIX);
	}

	REG32(CE_ACA_CFG) = 0x401;			/* ACA */
	REG32(CE_ACA_INTR_MSK) = 0x1ff;
	REG32(CE_HASH_CTL) |= 0xc1;			/* hash: clock, interrupts */
	err = ce_otp_drv_init();
	if (err)
		SE_ERR(err);
	if (plat_get_plat_id() == PLAT_EMU_VIRTUAL)
		hal_printf_level(LOG_INFO, "%s EMU not support TRNG!\n", LOG_PREFIX);
	else
		trng_engine_init();
	REG32(CE_SCA_CTL) |= 0xc1;			/* SCA */
	v = REG32(CE_TOP_CFG);
	if (!(v & 2)) {
		hal_printf_level(LOG_INFO, "%s Set SW initialization done flag!\n", LOG_PREFIX);
		REG32(CE_TOP_CFG) = v | 2;
	}

	v = REG32(CE_TOP_STAT_CTX);
	hal_printf_level(LOG_INFO, "%s Current host ID: %d\n", LOG_PREFIX, 0);
	hal_printf_level(LOG_INFO, "%s     RN_POOL is: %s\n", LOG_PREFIX, sec(v, BIT(27)));
	hal_printf_level(LOG_INFO, "%s     ACA is: %s\n", LOG_PREFIX, sec(v, BIT(26)));
	hal_printf_level(LOG_INFO, "%s     HASH is: %s\n", LOG_PREFIX, sec(v, BIT(25)));
	hal_printf_level(LOG_INFO, "%s     SCA is: %s\n", LOG_PREFIX, sec(v, BIT(24)));
	v = REG32(CE_TOP_STAT_VER);
	hal_printf_level(LOG_INFO, "%s CE Version: %s r%dp%d\n", LOG_PREFIX,
			 v >> 24 < 4 ? rel[v >> 24] : "Bad REL", (int)((v >> 16) & 0xff), (int)(v & 0xff));
	return 0;
}

/* Random bytes from the TRNG's pool, 32 at a time */
uint32_t ce_trng_read(uint8_t *buf, uint32_t len)
{
	uint32_t n, i, w, ctrl;

	if (plat_get_plat_id() == PLAT_EMU_VIRTUAL) {
		hal_printf_level(LOG_INFO, "%s EMU not support TRNG!\n", LOG_PREFIX);
		return 0;
	}
	if (len && buf == NULL)
		return CE_ERROR_BAD_PARAMS;
	while (len) {
		while (REG32(CE_RNP_STAT) & 6)
			;
		ctrl = REG32(CE_RNP_CTRL) | 3;
		REG32(CE_RNP_CTRL) = ctrl;
		while (!(REG32(CE_RNP_INTR) & 1))
			;
		REG32(CE_RNP_INTR) = ctrl;
		n = MIN(len, 32u);
		for (i = 0; i < n; i += 4) {
			w = REG32(CE_RNP_DATA + i);
			memcpy(buf + i, &w, MIN(4u, n - i));
		}
		REG32(CE_TRNG_INTR) = REG32(CE_TRNG_INTR);
		buf += n;
		len -= n;
	}
	return 0;
}

uint32_t ce_otp_get_cfg_info(uint8_t *exist, uint16_t *tst, uint16_t *s, uint16_t *ns)
{
	*exist = g_otp_cfg.otp_exist;
	*tst = g_otp_cfg.otp_tst_sz;
	*s = g_otp_cfg.otp_s_sz;
	*ns = g_otp_cfg.otp_ns_sz;
	return 0;
}

static void otp_copy(uint32_t reg, uint8_t *buf, uint32_t len, int write)
{
	uint32_t i, w;

	for (i = 0; i + 4 <= len; i += 4) {
		if (write) {
			memcpy(&w, buf + i, 4);
			REG32(reg + i) = w;
		} else {
			w = REG32(reg + i);
			memcpy(buf + i, &w, 4);
		}
	}
}

/*
 * OTP offsets: 0x04 model key, 0x18 device root key (16 bytes each),
 * 0x48 life cycle state, 0x4c lock.  The LCS register is one-hot:
 * 8 CM, 4 DM, 2 DD, 1 DR (states 0, 1, 3, 7).
 */
uint32_t ce_otp_read(uint32_t off, uint8_t *buf, uint32_t len)
{
	uint32_t v;

	switch (off) {
	case 0x04:
		otp_copy(CE_OTP_MODEL_KEY, buf, len, 0);
		return 0;
	case 0x18:
		otp_copy(CE_OTP_DEV_ROOT_KEY, buf, len, 0);
		return 0;
	case 0x48:
		v = REG32(CE_OTP_LCS) & 0xf;
		memset(buf, 0, len);
		if (v == 1)
			buf[0] = 7;
		else if (v == 2)
			buf[0] = 3;
		else if (v == 4)
			buf[0] = 1;
		else if (v != 0 && v != 8)
			return CE_ERROR_NOT_SUPPORTED;
		return 0;
	case 0x4c:
		memset(buf, 0, len);
		buf[0] = (REG32(CE_OTP_LCS) >> 4) & 1;
		return 0;
	}
	return CE_ERROR_NOT_SUPPORTED;
}

uint32_t ce_otp_write(uint32_t off, uint8_t *buf, uint32_t len)
{
	static const uint8_t lcs_bit[8] = { 8, 4, 0, 2, 0, 0, 0, 1 };
	uint32_t lcs = 0;

	if (buf == NULL || ((off | len) & 3))
		return CE_ERROR_BAD_PARAMS;
	if (off == 0x48 && len) {
		if ((buf[0] & 8) || ((buf[0] & 6) && (buf[0] & 3) != 3))
			return CE_ERROR_BAD_PARAMS;
	}
	if (REG32(CE_OTP_LCS) & BIT(4))
		return CE_ERROR_ACCESS_DENIED;
	switch (off) {
	case 0x04:
		otp_copy(CE_OTP_MODEL_KEY, buf, len, 1);
		return 0;
	case 0x18:
		otp_copy(CE_OTP_DEV_ROOT_KEY, buf, len, 1);
		return 0;
	case 0x4c:
		REG32(CE_OTP_LCS) |= BIT(4);
		return 0;
	case 0x48:
		memcpy(&lcs, buf, MIN(len, 4u));
		if (lcs >= 8 || lcs_bit[lcs] == 0)
			return CE_ERROR_NOT_SUPPORTED;
		REG32(CE_OTP_LCS) = lcs_bit[lcs];
		return 0;
	}
	return CE_ERROR_NOT_SUPPORTED;
}
