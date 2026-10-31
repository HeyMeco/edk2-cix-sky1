/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Bootloader1 package header ("CIXBTFF!") and the loaders (framework/xloader.c) */
#ifndef XLOADER_H
#define XLOADER_H

#include <stdbool.h>
#include <stdint.h>
#include "qspi.h"

typedef enum {
	SEC_FW_TYPE_SE = 1, SEC_FW_TYPE_PM, SEC_FW_TYPE_PBL, SEC_FW_TYPE_EFUSE, SEC_FW_TYPE_SFH,
} sec_fw_type_t;

typedef struct {
	uint32_t key_id;
	uint32_t key_length;
	uint8_t key_value[768];		/* RSA: modulus, exponent; SM2: x, y */
} key_group;

typedef struct {
	uint32_t image_id;
	struct { uint32_t major_ver, minor_ver; } version;
	uint32_t binary_offset;		/* from the package start */
	uint32_t binary_length;
	uint32_t run_address;
} image_header_group;

typedef struct {
	uint32_t image_id;
	uint32_t hash_len;
	uint8_t hash_data[64];
} hash_table;

typedef struct {
	uint32_t len;
	uint8_t content[508];
} image_signature;

typedef struct {
	uint8_t magic[8];
	uint32_t cbff_version;
	uint32_t encrypt_type;
	uint32_t keys_number;
	key_group key_groups[2];	/* OEM key (signs this header), CIX image key */
	uint8_t store_reserved[476];
	uint32_t qspi_number;
	qspi_param_t qspi_config[30];
	uint32_t sign_type;
	uint32_t image_count;
	image_header_group image_header_groups[16];
	uint32_t hash_type;
	uint32_t hash_count;
	hash_table hash_tables[5];
	struct { uint32_t key_sln, root_key; } boot_key;
	uint8_t padding[284];
	image_signature signature;
} cbff_img_header;

_Static_assert(sizeof(cbff_img_header) == 0x1000, "cbff header");

uint32_t fw_xloader_get_header(cbff_img_header *h);
uint32_t fw_xloader_load_img(sec_fw_type_t id);
bool fw_verify_header(const cbff_img_header *h, uint32_t len);
bool fw_verify_sec_debug_img(const uint8_t *data);
uint32_t fw_jumper_init(sec_fw_type_t type);
uint32_t fw_jumper_prejump(sec_fw_type_t type);
void fw_jumper_run(sec_fw_type_t type);

#endif
