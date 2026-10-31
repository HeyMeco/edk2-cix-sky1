/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Firmware in flash: bootloader1 header, firmware table, SE config (hal/storage.c) */
#ifndef STORAGE_H
#define STORAGE_H

#include <stdint.h>
#include "qspi.h"

typedef enum {
	SEC_STOR_TYPE_XIP_FLASH, SEC_STOR_TYPE_FLASH, SEC_STOR_TYPE_EMMC, SEC_STOR_TYPE_UFS,
} sec_storage_type_t;

typedef struct {
	uint32_t offset;
	qspi_param_t *qspi_param;
} sec_storage_param_t;

/* Firmware table at flash 1 MiB (2 MiB with strap bit 0 of SE_CLKCTRL + 0x60) */
typedef enum {
	BOOTLOADER_1_IMG_DATA_TYPE = 1, BOOTLOADER_2_IMG_DATA_TYPE, MEMORY_CONFIG_IMG_DATA_TYPE,
	PM_CONFIG_IMG_DATA_TYPE, UEFI_NVRAM_IMG_DATA_TYPE, SE_CONFIG_IMG_DATA_TYPE,
	BOOTLOADER_3_IMG_DATA_TYPE, SFH_FW_IMG_DATA_TYPE,
} sub_image_type_t;

typedef struct {
	uint32_t img_type;
	uint32_t img_addr;
	uint32_t img_size;
	uint32_t reserved;
} firmware_entry;

/* SE config image ("CIXSECONFIG") */
typedef struct {
	uint8_t header[0x84];
	uint32_t low_power_gate;
	uint32_t ta_enc_config;
	uint32_t dfd_nv_set;
	uint32_t mte_config;
} se_config_t;

uint32_t hal_stor_init(void);
uint32_t hal_stor_load_header(sec_storage_type_t type, sec_storage_param_t *param, uint8_t *header,
			      uint32_t *len);
uint32_t hal_stor_load_fw(sec_storage_type_t type, sec_storage_param_t *param, uint8_t *img,
			  uint32_t len);
uint32_t hal_stor_get_sub_img(sub_image_type_t type, uint8_t *img, uint32_t *len);
uint32_t hal_stor_get_firmware_entry(sub_image_type_t type, firmware_entry *e);
void hal_secfg_init(void);
uint32_t hal_secfg_get_ta_enc_config(void);
uint32_t hal_secfg_get_mte_config(void);
uint32_t hal_secfg_set_mte_config(uint32_t enable);
uint32_t hal_secfg_update(void);

#endif
