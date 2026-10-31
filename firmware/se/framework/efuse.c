/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * eFuse fields the firmware uses (CIX framework/efuse/fw_efuse.c,
 * fw_fuse_dist.c).
 *
 * Keys and root-of-trust hashes exist twice: CIX's production values,
 * used once the life cycle is SECURE, and test values for every other
 * life cycle.
 */
#include "se.h"
#include "efuse.h"
#include "platform.h"

static uint32_t read_field(uint32_t addr, uint32_t offset, uint32_t size, void *data)
{
	uint32_t err = sec_efuse_read(&g_handle_efuse, addr, offset, size, data);

	if (err)
		SE_ERR(err);
	return err;
}

static uint32_t life_cycle(uint32_t *lc)
{
	uint32_t err = plat_get_info(PLAT_INFO_LIFE_CYCLE, lc);

	if (err)
		SE_ERR(err);
	return err;
}

uint32_t fw_efuse_int(void)
{
	uint32_t err = sec_efuse_init(&g_handle_efuse);

	if (err)
		SE_ERR(err);
	return err;
}

static const struct {
	uint16_t addr;
	uint8_t offset, size;
	uint8_t zero_if_blank;		/* reads as 0 on a blank (unfused) chip */
} fuse_fields[] = {
	[FUSE_ID_SE_BT_SEC]		= { 0x154, 3, 1 },
	[FUSE_ID_SE_BT_SIGN_ALG]	= { 0x154, 4, 2 },
	[FUSE_ID_SE_BT_HASH_ALG]	= { 0x154, 6, 2 },
	[FUSE_ID_SE_BT_FL_BUS_CLK]	= { 0x155, 1, 3 },
	[FUSE_ID_SE_BT_FL_SPI_MODE]	= { 0x155, 4, 1 },
	[FUSE_ID_SE_BT_FL_RX_DELAY]	= { 0x155, 5, 2 },
	[FUSE_ID_SE_BT_FL_ADDR_MODE]	= { 0x155, 7, 1 },
	[FUSE_ID_SE_BT_FL_READ_ADV]	= { 0x15e, 1, 1, 1 },
	[FUSE_ID_SE_BT_WD_SWITCH]	= { 0x156, 0, 1 },
	[FUSE_ID_SE_BT_RUN_ADDR]	= { 0x156, 1, 8 },
	[FUSE_ID_SE_BT_ERR_POLICY]	= { 0x157, 1, 2 },
	[FUSE_ID_SE_BT_FAULT_POLICY]	= { 0x15c, 7, 2 },
	[FUSE_ID_SE_BT_SMN_FREQ_DIV]	= { 0x157, 3, 2 },
	[FUSE_ID_SE_LOG_MODE]		= { 0x15c, 2, 2 },
	[FUSE_ID_SE_POSTC_MODE]		= { 0x15c, 4, 2 },
	[FUSE_ID_NPU0_HARVEST]		= { 0x001, 7, 1, 1 },
	[FUSE_ID_ISP_HARVEST]		= { 0x002, 0, 1, 1 },
	[FUSE_ID_DPU0_HARVEST]		= { 0x002, 1, 1, 1 },
	[FUSE_ID_DPU1_HARVEST]		= { 0x002, 2, 1, 1 },
	[FUSE_ID_DPU2_HARVEST]		= { 0x002, 3, 1, 1 },
	[FUSE_ID_DPU3_HARVEST]		= { 0x002, 4, 1, 1 },
	[FUSE_ID_DPU4_HARVEST]		= { 0x002, 5, 1, 1 },
	[FUSE_ID_PCIE_X8_HARVEST]	= { 0x002, 6, 1, 1 },
	[FUSE_ID_AUDIO_SS_HARVEST]	= { 0x002, 7, 1, 1 },
	[FUSE_ID_NPU1_HARVEST]		= { 0x003, 0, 1, 1 },
	[FUSE_ID_CPU_CORE_HARVEST]	= { 0x046, 4, 12, 1 },
};

uint32_t fw_efuse_get_info(fuse_id_t id, uint32_t *info)
{
	uint32_t lc = 0, err;

	err = life_cycle(&lc);
	if (err)
		return err;
	*info = 0;
	if (id >= ARRAY_SIZE(fuse_fields) || fuse_fields[id].size == 0) {
		SE_ERR(EFUSE_ERR_ID);
		return EFUSE_ERR_ID;
	}
	if (fuse_fields[id].zero_if_blank && lc == LIFE_CYCLE_BLANK)
		return 0;
	err = sec_efuse_read(&g_handle_efuse, fuse_fields[id].addr, fuse_fields[id].offset,
			     fuse_fields[id].size, info);
	if (err)
		SE_ERR(err);
	return err;
}

/* 128-bit key: CIX's when SECURE, the test key otherwise */
static uint32_t get_key(uint8_t *key, uint32_t secure_addr, uint32_t test_addr)
{
	uint32_t lc, err;

	err = life_cycle(&lc);
	if (err)
		return err;
	return read_field(lc == LIFE_CYCLE_SECURE ? secure_addr : test_addr, 0, 128, key);
}

uint32_t fw_efuse_get_model_key(uint8_t *key)
{
	return get_key(key, 0x54, 0xb4);
}

uint32_t fw_efuse_get_device_key(uint8_t *key)
{
	return get_key(key, 0x64, 0xc4);
}

/*
 * SHA-256 of a root public key.  The fused signature algorithm (1 RSA,
 * 2 SM2) decides; unfused, the image's signature type does.
 */
static uint32_t rotpk_hash(uint8_t *hash, uint32_t img_sign_type, const char *who,
			   uint32_t rsa, uint32_t sm2, uint32_t test_rsa, uint32_t test_sm2)
{
	uint32_t lc, sign_type, err;
	int use_sm2;

	err = life_cycle(&lc);
	if (err)
		return err;
	err = fw_efuse_get_info(FUSE_ID_SE_BT_SIGN_ALG, &sign_type);
	if (err) {
		SE_ERR(err);
		return err;
	}
	if (sign_type == 1 || sign_type == 2)
		use_sm2 = sign_type == 2;
	else if (img_sign_type == IMG_SIGN_RSA3072 || img_sign_type == IMG_SIGN_RSA2048)
		use_sm2 = 0;
	else if (img_sign_type == IMG_SIGN_SM2)
		use_sm2 = 1;
	else
		return EFUSE_ERR_SIGN_TYPE;
	if (lc != LIFE_CYCLE_SECURE)
		who = "TEST";
	hal_printf_level(LOG_INFO, "# System will use %s %s rotpk\n", who, use_sm2 ? "SM2" : "RSA");
	if (lc == LIFE_CYCLE_SECURE)
		return read_field(use_sm2 ? sm2 : rsa, 0, 256, hash);
	return read_field(use_sm2 ? test_sm2 : test_rsa, 0, 256, hash);
}

uint32_t fw_efuse_get_cix_rotpk_hash(uint8_t *hash, uint32_t img_sign_type)
{
	return rotpk_hash(hash, img_sign_type, "CIX", 0x74, 0x94, 0xd4, 0xf4);
}

uint32_t fw_efuse_get_cix_user_rotpk_hash(uint8_t *hash, uint32_t img_sign_type)
{
	return rotpk_hash(hash, img_sign_type, "OEM", 0x188, 0x1a8, 0xd4, 0xf4);
}

uint32_t fw_efuse_get_oem_model_key(uint8_t *key)
{
	return read_field(0x168, 0, 128, key);
}

uint32_t fw_efuse_get_oem_rotpk_valid(uint8_t *valid)
{
	return read_field(0x15e, 3, 1, valid);
}

uint32_t fw_efuse_get_oem_rotpk_hash(uint8_t *hash, uint32_t img_sign_type)
{
	return read_field(0x4e4, 0, 256, hash);
}

uint32_t fw_efuse_get_chip_id(uint8_t *id)
{
	return read_field(0x1c8, 0, 64, id);
}

uint32_t fw_efuse_get_pub_ser_num(uint8_t *sn)
{
	return read_field(0x48, 0, 64, sn);
}

uint32_t fw_efuse_req_from_non_sec(uint8_t *rsp, uint32_t addr, uint32_t len)
{
	return read_field(addr, 0, len * 8, rsp);
}

uint32_t fw_efuse_program(uint32_t addr, uint32_t offset, uint32_t size, const void *data)
{
	uint32_t err = sec_efuse_program(NULL, addr, offset, size, data);

	if (err)
		SE_ERR(err);
	return err;
}

uint32_t fw_efuse_get_ddr_config_info(uint8_t *info, uint32_t bits)
{
	return read_field(0x4ac, 0, bits, info);
}

/*
 * Hand fuse data to the rest of the chip:
 *  PVT	   the PVT sensor trim (fuses 0x228..0x4e4) into the PM's fuse copy,
 *	   with its unused range cleared, if ATE marked it for distribution;
 *  GLITCH the glitch detector trim into the PLL blocks, likewise;
 *  BOOT   three bytes of chip identification for the boot ROM's successor.
 */
#define PM_PVT_SENSOR_DATA	0x46ace700u
#define CRU_PLL0		0x46570000u
#define CRU_PLL1		0x46575000u

uint32_t fw_fuse_dist(fused_t t)
{
	uint8_t buf[128] = { 0 };
	uint32_t ate = 0, v, err;

	switch (t) {
	case FUSED_TYPE_PVT:
		err = sec_efuse_read(NULL, 0x4a3, 6, 2, &ate);
		if (err) {
			SE_ERR(err);
			return err;
		}
		hal_printf_level(LOG_INFO, "ate_distribute:%d\n", (int)ate);
		if (!(ate & 1))
			return 0;
		memcpy((void *)PM_PVT_SENSOR_DATA, (const void *)(SE_EFUSE_BASE + 0x228), 700);
		err = sec_efuse_program_sram_dist(NULL, 0x4a4, 0, 0x1c0, buf);
		if (err)
			SE_ERR(err);
		return err;
	case FUSED_TYPE_GLITCH:
		err = sec_efuse_read(NULL, 0x4a3, 6, 2, &ate);
		if (err) {
			SE_ERR(err);
			return err;
		}
		if (!(ate & 2))
			return 0;
		err = sec_efuse_read(NULL, 0x4a0, 0, 30, buf);
		if (err) {
			SE_ERR(err);
			return err;
		}
		memcpy(&v, buf, 4);
		REG32(CRU_PLL0 + 0x02c) |= v & 0x1f;
		REG32(CRU_PLL0 + 0x12c) |= (v >> 5) & 0x1f;
		REG32(CRU_PLL0 + 0x22c) |= (v >> 10) & 0x1f;
		REG32(CRU_PLL0 + 0x32c) |= (v >> 15) & 0x1f;
		REG32(CRU_PLL0 + 0x42c) |= (v >> 20) & 0x1f;
		REG32(CRU_PLL1 + 0x12c) |= (v >> 25) & 0x1f;
		return 0;
	case FUSED_TYPE_BOOT:
		err = sec_efuse_read(NULL, 0x4dc, 0, 24, buf);
		if (err) {
			SE_ERR(err);
			return err;
		}
		hexdump("chip id", buf, 3);
		memcpy((void *)(SE_CLKCTRL_BASE + 0x12c), buf, 3);
		return 0;
	}
	return 0xffffffff;
}
