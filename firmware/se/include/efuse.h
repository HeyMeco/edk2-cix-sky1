/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * eFuse (CIX driver/efuse, hal/common/hal_efuse*.c, framework/efuse).
 *
 * The 2 KiB fuse array is mirrored at SE_EFUSE_BASE (the "fuse SRAM", which
 * the boot ROM loads and the firmware may override); fields are addressed
 * by byte address, bit offset and size in bits.
 */
#ifndef EFUSE_H
#define EFUSE_H

#include <stdint.h>

typedef struct { void *priv; } sec_efuse_t;
extern sec_efuse_t g_handle_efuse;

typedef enum {
	FUSE_ID_CIX_MODEL_KEY, FUSE_ID_CIX_DEVICE_KEY, FUSE_ID_CIX_ROTPK_RSA_HASH,
	FUSE_ID_CIX_ROTPK_SM2_HASH, FUSE_ID_CIX_TEST_MODEL_KEY, FUSE_ID_CIX_TEST_DEVICE_KEY,
	FUSE_ID_CIX_TEST_ROTPK_RSA_HASH, FUSE_ID_CIX_TEST_ROTPK_SM2_HASH, FUSE_ID_SE_AVB_PUB_HASH,
	FUSE_ID_HDCP22_LC128, FUSE_ID_HDCP23_LC128, FUSE_ID_SE_BT_SEC, FUSE_ID_SE_BT_SIGN_ALG,
	FUSE_ID_SE_BT_HASH_ALG, FUSE_ID_SE_BT_FL_QSPI_NAME, FUSE_ID_SE_BT_FL_BUS_CLK,
	FUSE_ID_SE_BT_FL_SPI_MODE, FUSE_ID_SE_BT_FL_RX_DELAY, FUSE_ID_SE_BT_FL_ADDR_MODE,
	FUSE_ID_SE_BT_FL_READ_ADV, FUSE_ID_SE_BT_WD_SWITCH, FUSE_ID_SE_BT_RUN_ADDR,
	FUSE_ID_SE_BT_ERR_POLICY, FUSE_ID_SE_BT_FAULT_POLICY, FUSE_ID_SE_BT_SMN_FREQ_DIV,
	FUSE_ID_SE_BT_EXCEPT_DETECT, FUSE_ID_SE_JTAG_MODE, FUSE_ID_SE_LOG_MODE,
	FUSE_ID_SE_POSTC_MODE, FUSE_ID_SE_FUSE_OVERR_MODE, FUSE_ID_NPU0_HARVEST,
	FUSE_ID_ISP_HARVEST, FUSE_ID_DPU0_HARVEST, FUSE_ID_DPU1_HARVEST, FUSE_ID_DPU2_HARVEST,
	FUSE_ID_DPU3_HARVEST, FUSE_ID_DPU4_HARVEST, FUSE_ID_PCIE_X8_HARVEST,
	FUSE_ID_AUDIO_SS_HARVEST, FUSE_ID_NPU1_HARVEST, FUSE_ID_CPU_CORE_HARVEST,
} fuse_id_t;

typedef enum { FUSED_TYPE_PVT, FUSED_TYPE_GLITCH, FUSED_TYPE_BOOT } fused_t;

/* Image signature types in the firmware header */
#define IMG_SIGN_RSA3072	0x5a
#define IMG_SIGN_SM2		0x5b
#define IMG_SIGN_RSA2048	0x5c

#define EFUSE_ERR_PARAM		0x9802
#define EFUSE_ERR_SIGN_TYPE	0x9803
#define EFUSE_ERR_ID		0x9804

/* driver */
uint32_t drv_fuse_read(uint8_t *buf, uint32_t offset, uint32_t len);
void drv_fuse_align_for_read(uint32_t start_bit, const uint8_t *data, uint32_t bits, uint8_t *out);
void drv_fuse_align_for_write(uint32_t start_bit, const uint8_t *data, uint32_t bits, uint8_t *out);
uint32_t drv_fuse_write_onerow(const uint8_t *buf, uint32_t byte_addr, uint32_t len);
void drv_fuse_re_sense_one_row(uint32_t addr, uint32_t size);

/* hal */
uint32_t sec_efuse_init(sec_efuse_t *efuse);
uint32_t sec_efuse_read(sec_efuse_t *efuse, uint32_t addr, uint32_t offset, uint32_t size, void *data);
uint32_t sec_efuse_program(sec_efuse_t *efuse, uint32_t addr, uint32_t offset, uint32_t size,
			   const void *data);
uint32_t sec_efuse_program_sram(sec_efuse_t *efuse, uint32_t addr, uint32_t offset, uint32_t size,
				const void *data);
uint32_t sec_efuse_program_sram_by_word(sec_efuse_t *efuse, uint32_t addr, uint32_t offset,
					uint32_t size, const void *data);
uint32_t sec_efuse_program_sram_dist(sec_efuse_t *efuse, uint32_t addr, uint32_t offset,
				     uint32_t size, const void *data);

/* framework */
uint32_t fw_efuse_int(void);
uint32_t fw_efuse_get_info(fuse_id_t id, uint32_t *info);
uint32_t fw_efuse_get_model_key(uint8_t *key);
uint32_t fw_efuse_get_device_key(uint8_t *key);
uint32_t fw_efuse_get_cix_rotpk_hash(uint8_t *hash, uint32_t img_sign_type);
uint32_t fw_efuse_get_cix_user_rotpk_hash(uint8_t *hash, uint32_t img_sign_type);
uint32_t fw_efuse_get_oem_model_key(uint8_t *key);
uint32_t fw_efuse_get_oem_rotpk_valid(uint8_t *valid);
uint32_t fw_efuse_get_oem_rotpk_hash(uint8_t *hash, uint32_t img_sign_type);
uint32_t fw_efuse_get_chip_id(uint8_t *id);
uint32_t fw_efuse_get_pub_ser_num(uint8_t *sn);
uint32_t fw_efuse_req_from_non_sec(uint8_t *rsp, uint32_t addr, uint32_t len);
uint32_t fw_efuse_program(uint32_t addr, uint32_t offset, uint32_t size, const void *data);
uint32_t fw_efuse_get_ddr_config_info(uint8_t *info, uint32_t bits);
uint32_t fw_fuse_dist(fused_t t);

#endif
