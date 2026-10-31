/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Key manager blocks for BL2, OP-TEE and UEFI (framework/km.c) */
#ifndef KM_H
#define KM_H

#include <stdint.h>

typedef struct {
	uint8_t hpubk[32];		/* hash of the root public key */
	uint8_t rng[64];
	uint32_t config[16];
	uint8_t boot_cfg[256];
	uint8_t key_cfg[2048];
} csec_km_meta_t;

typedef enum {
	CSEC_KM_BL2, CSEC_KM_BL31, CSEC_KM_BL32, CSEC_KM_BL33, CSEC_KM_REE, CSEC_KM_TEE,
} csec_km_stage_t;

typedef enum {
	CSEC_KEY_ID_MODEL_KEY, CSEC_KEY_ID_DEVICE_KEY, CSEC_KEY_ID_HASH_ROOT_PUBKEY,
	CSEC_KEY_ID_BL32_HUK, CSEC_KEY_ID_BL32_TA_ENC, CSEC_KEY_ID_PK, CSEC_KEY_ID_KEK,
} csec_meta_key_id_t;

uint32_t fw_km_init(void);
uint32_t fw_km_sync(csec_km_stage_t s);
uint32_t fw_km_set_key(csec_meta_key_id_t id, const uint8_t *key, uint32_t len);
uint32_t fw_km_derive_key(csec_meta_key_id_t src, csec_meta_key_id_t dst, uint8_t *key, uint32_t *len);

#endif
