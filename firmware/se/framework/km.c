/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Key manager (CIX framework/fw_km.c).
 *
 * Builds a key block for each later stage and copies it to DRAM when that
 * stage is loaded: BL2 gets the boot configuration, life cycle, serial
 * number and the OEM root key hash; OP-TEE gets its hardware unique key
 * (HKDF-SHA256 of the eFuse device key) and the TA encryption key (of the
 * model key); UEFI gets a version string.  Every block carries 64 random
 * bytes.
 */
#include "se.h"
#include "btcfg.h"
#include "crypto.h"
#include "efuse.h"
#include "km.h"
#include "platform.h"
#include "storage.h"

/* Destinations through the SYSHUB window (SE 0x60000000 = SoC 0x80000000) */
#define KM_BL2_DRAM		0x601ff000u	/* SoC 0x801ff000 */
#define KM_BL33_DRAM		0x63e00000u	/* SoC 0x83e00000, PcdSeHandoffInfoBaseAddr */
#define KM_BL33_VERSION		0x63e01000u
#define KM_TEE_DRAM		0x624fe000u	/* SoC 0x824fe000, OP-TEE's TEE_KEY_META_SHM */

#define KM_CFG_LC		1
#define KM_CFG_SEC_ENABLE	3
#define KM_CFG_ENC_ENABLE	4
#define KM_CFG_SERIAL_NUM	5	/* serial number, then chip ID: 4 words */
#define KM_CFG_TA_ENC_ENABLE	9
#define KM_CFG_COPY		0x24	/* config words handed to every stage */

#define KM_DEVICE_KEY_OFFSET	0	/* in key_cfg */
#define KM_MODEL_KEY_OFFSET	16
#define KM_PK_OFFSET		0
#define KM_KEK_OFFSET		0x400
#define KM_OEM_ROTPK_HASH	0
#define KM_OEM_ROTPK_VALID	0x20

#ifndef SE_FW_VERSION
#define SE_FW_VERSION		"beta2.0-open"
#endif

static csec_km_meta_t g_meta_pbl, g_meta_bl33, g_meta_tee;

/* What a blank chip reports as serial number */
static const uint8_t blank_sn[8] = { 0xa5, 0xb1, 0xee, 0xce, 0x00, 0x20, 0x0c, 0x11 };

uint32_t fw_km_set_key(csec_meta_key_id_t id, const uint8_t *key, uint32_t len)
{
	if (id == CSEC_KEY_ID_HASH_ROOT_PUBKEY)
		memcpy(g_meta_pbl.hpubk, key, MIN(len, sizeof(g_meta_pbl.hpubk)));
	else if (id == CSEC_KEY_ID_PK)
		memcpy(g_meta_bl33.key_cfg + KM_PK_OFFSET, key, MIN(len, 0x400u));
	else if (id == CSEC_KEY_ID_KEK)
		memcpy(g_meta_bl33.key_cfg + KM_KEK_OFFSET, key, MIN(len, 0x400u));
	return 0;
}

/* HKDF-SHA256 (no salt, no info) of the 128-bit eFuse device or model key */
uint32_t fw_km_derive_key(csec_meta_key_id_t src, csec_meta_key_id_t dst, uint8_t *key, uint32_t *len)
{
	uint8_t ikm[16];
	uint32_t err;

	hal_printf_level(LOG_INFO, "[%s:%d]derive_key src id:%d, des id:%d\n", __func__, __LINE__,
			 src, dst);
	if (src == CSEC_KEY_ID_DEVICE_KEY)
		err = fw_efuse_get_device_key(ikm);
	else if (src == CSEC_KEY_ID_MODEL_KEY)
		err = fw_efuse_get_model_key(ikm);
	else
		err = 0;
	if (err) {
		SE_ERR(err);
		return err;
	}
	if (dst == CSEC_KEY_ID_BL32_HUK)
		*len = 16;
	else if (dst == CSEC_KEY_ID_BL32_TA_ENC)
		*len = 32;
	err = sec_hkdf_sha256(ikm, sizeof(ikm), key, *len);
	memset(ikm, 0, sizeof(ikm));
	if (err)
		SE_ERR(err);
	return err;
}

static uint32_t fw_km_set_trng(csec_km_stage_t s)
{
	csec_km_meta_t *m = s == CSEC_KM_BL2 ? &g_meta_pbl : s == CSEC_KM_BL33 ? &g_meta_bl33 :
			    s == CSEC_KM_TEE ? &g_meta_tee : NULL;
	uint32_t err;

	if (m == NULL)
		return 0;
	err = sec_trng_get_data(m->rng, sizeof(m->rng));
	if (err)
		SE_ERR(err);
	return err;
}

uint32_t fw_km_init(void)
{
	uint32_t mode = 0, lc, len, err;

	err = sec_efuse_read(&g_handle_efuse, 0x15e, 4, 3, &mode);
	if (err) {
		SE_ERR(err);
		return err;
	}
	SE_INFO("key mode:%d\n", (int)mode);
	memset(&g_meta_pbl, 0, sizeof(g_meta_pbl));
	memset(&g_meta_bl33, 0, sizeof(g_meta_bl33));
	memset(&g_meta_tee, 0, sizeof(g_meta_tee));
	err = plat_get_info(PLAT_INFO_LIFE_CYCLE, &lc);
	if (err) {
		SE_ERR(err);
		return err;
	}
	g_meta_pbl.config[KM_CFG_LC] = lc;
	err = fw_efuse_get_info(FUSE_ID_SE_BT_SEC, &g_meta_pbl.config[KM_CFG_SEC_ENABLE]);
	if (err) {
		SE_ERR(err);
		return err;
	}
	g_meta_pbl.config[KM_CFG_ENC_ENABLE] = 1;
	if (lc == LIFE_CYCLE_BLANK) {
		memcpy(&g_meta_pbl.config[KM_CFG_SERIAL_NUM], blank_sn, sizeof(blank_sn));
	} else {
		err = fw_efuse_get_pub_ser_num((uint8_t *)&g_meta_pbl.config[KM_CFG_SERIAL_NUM]);
		if (!err)
			err = fw_efuse_get_chip_id((uint8_t *)&g_meta_pbl.config[KM_CFG_SERIAL_NUM + 2]);
		if (err) {
			SE_ERR(err);
			return err;
		}
	}
	err = fw_km_set_trng(CSEC_KM_BL2);
	if (err)
		return err;
	memcpy(g_meta_bl33.config, g_meta_pbl.config, KM_CFG_COPY);
	err = fw_km_set_trng(CSEC_KM_BL33);
	if (err)
		return err;
	memcpy(g_meta_tee.config, g_meta_pbl.config, KM_CFG_COPY);
	err = fw_km_set_trng(CSEC_KM_TEE);
	if (err)
		return err;
	g_meta_tee.config[KM_CFG_TA_ENC_ENABLE] = hal_secfg_get_ta_enc_config();
	err = fw_km_derive_key(CSEC_KEY_ID_DEVICE_KEY, CSEC_KEY_ID_BL32_HUK,
			       g_meta_tee.key_cfg + KM_DEVICE_KEY_OFFSET, &len);
	if (!err)
		err = fw_km_derive_key(CSEC_KEY_ID_MODEL_KEY, CSEC_KEY_ID_BL32_TA_ENC,
				       g_meta_tee.key_cfg + KM_MODEL_KEY_OFFSET, &len);
	if (!err)
		err = fw_efuse_get_oem_rotpk_valid(g_meta_pbl.key_cfg + KM_OEM_ROTPK_VALID);
	if (!err)
		err = fw_efuse_get_oem_rotpk_hash(g_meta_pbl.key_cfg + KM_OEM_ROTPK_HASH, 1);
	if (err)
		SE_ERR(err);
	return err;
}

/* Copy a stage's block to DRAM just before the stage runs */
uint32_t fw_km_sync(csec_km_stage_t s)
{
	if (s == CSEC_KM_BL2) {
		memcpy(g_meta_pbl.boot_cfg, mntr_boot_cfg_item_table, 0x50);
		memcpy((void *)KM_BL2_DRAM, &g_meta_pbl, sizeof(g_meta_pbl));
	} else if (s == CSEC_KM_BL33) {
		memcpy((void *)KM_BL33_DRAM, &g_meta_bl33, sizeof(g_meta_bl33));
		strcpy((char *)KM_BL33_VERSION, SE_FW_VERSION);
		SE_INFO("BL33 sync fw version+hash: %s\n", (char *)KM_BL33_VERSION);
	} else if (s == CSEC_KM_TEE) {
		memcpy((void *)KM_TEE_DRAM, &g_meta_tee, sizeof(g_meta_tee));
	}
	return 0;
}
