/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Loading and verifying CSU_PM and BL2 (CIX framework/xloader/fw_xloader.c,
 * framework/image_verify, framework/fw_jumper.c).
 *
 * Both come from the bootloader1 package ("CIXBTFF!" header, 4 KiB, at
 * flash 0x188000 or 0x288000 by strap; the boot ROM leaves a copy at
 * 0x2007e800).  With secure boot fused on:
 *   - the header carries the OEM key; its hash must match the eFuse, and
 *     it signs the header (signature at 0xe00);
 *   - the header carries CIX's image key; its hash must match the eFuse,
 *     and it signs the images (signature in the last 512 bytes of each
 *     4 KiB-padded image); the header also holds each image's digest.
 * CSU_PM's image ("XBIN") lists sections for the PM's SRAM; BL2 is copied
 * to its run address in DRAM.
 */
#include "se.h"
#include "crypto.h"
#include "drivers.h"
#include "efuse.h"
#include "km.h"
#include "memcfg.h"
#include "platform.h"
#include "soc.h"
#include "storage.h"
#include "xloader.h"

#define ROM_HDR			((cbff_img_header *)0x2007e800)
#define BL1_FLASH(strap)	((strap) & 1 ? 0x288000u : 0x188000u)
#define FLASH_STRAP		(SE_CLKCTRL_BASE + 0x60)
#define CBFF_MAGIC		"CIXBTFF!"
#define XBIN_MAGIC		0x4e494258u	/* "XBIN" */
#define PM_SRAM_SE		0x26a10000u	/* PM address 0x20000000 + this: SE address (SoC 0x06a10000) */
#define PM_SRAM_END		0x20500000u
#define PM_CONFIG_DST		0x600bf000u	/* through the SYSHUB window mapped for the PM */
#define SIG_SIZE		0x200

#define ERR_HDR_READ		0x9301
#define ERR_HDR_VERIFY		0x9302
#define ERR_PARAM		0x9303
#define ERR_NO_IMAGE		0x9304
#define ERR_NOMEM		0x9305
#define ERR_HDR_LOAD		0x9307
#define ERR_IMAGE		0x9308
#define ERR_LOAD		0x930c
#define ERR_PM_SECTION		0xa501
#define ERR_DIGEST_PARAM	0x9206

typedef struct {
	uint32_t addr, size;
} section_info;

typedef struct {
	uint8_t *pm_header;
	uint8_t *padding_data;
	image_signature *sig_dat;
	uint32_t base_flash_offset;
	uint32_t binary_size;
	uint32_t padding_size;
	uint32_t section_num;
	section_info sections[30];
} pm_data_info;

static pm_data_info g_PM_data_info;
static key_group cix_img_key;		/* CIX's image key, from a verified header */

static uint32_t round_4k(uint32_t v)
{
	return (v + 0xfff) & ~0xfffu;
}

/* Digest by fused signature algorithm, else by the image's signature type */
static uint32_t fw_get_digest_mode(uint32_t sign_type, sec_sha_mode_t *mode)
{
	uint32_t alg = 0, err = fw_efuse_get_info(FUSE_ID_SE_BT_SIGN_ALG, &alg);

	if (err)
		return err;
	if (alg == 1 || (alg != 2 && (sign_type == IMG_SIGN_RSA3072 || sign_type == IMG_SIGN_RSA2048)))
		*mode = SEC_SHA_MODE_256;
	else if (alg == 2 || sign_type == IMG_SIGN_SM2)
		*mode = SEC_SM3_MODE;
	else
		return EFUSE_ERR_SIGN_TYPE;
	return 0;
}

static uint32_t fw_cal_digest(const void *in, uint32_t len, uint8_t *digest, uint32_t *dig_len,
			      sec_sha_mode_t mode)
{
	sec_sha_context_t ctx;
	sec_sha_t sha;
	uint32_t err;

	if (in == NULL || digest == NULL || dig_len == NULL)
		return ERR_DIGEST_PARAM;
	err = sec_sha_init(&sha, 0);
	if (err)
		return err;
	err = sec_sha_start(&sha, &ctx, mode);
	if (!err)
		err = sec_sha_update(&sha, &ctx, in, len);
	if (!err)
		err = sec_sha_finish(&sha, &ctx, digest, dig_len);
	sec_sha_uninit(&sha);
	if (err)
		hal_printf_level(LOG_ERR, "ERR %s, %d\n", __func__, __LINE__);
	return err;
}

/* PM digest: header and section table, the sections as loaded, (the padding) */
static uint32_t fw_cal_pm_digest(const pm_data_info *pm, uint8_t *digest, uint32_t *dig_len,
				 sec_sha_mode_t mode, bool padding)
{
	sec_sha_context_t ctx;
	sec_sha_t sha;
	uint32_t err, i;

	if (pm == NULL || digest == NULL || dig_len == NULL)
		return ERR_DIGEST_PARAM;
	err = sec_sha_init(&sha, 0);
	if (err)
		return err;
	err = sec_sha_start(&sha, &ctx, mode);
	if (!err)
		err = sec_sha_update(&sha, &ctx, pm->pm_header, pm->section_num * 0x14 + 0x10);
	for (i = 0; !err && i < pm->section_num; i++)
		if (pm->sections[i].size)
			err = sec_sha_update(&sha, &ctx, (const void *)(uintptr_t)
					     (((pm->sections[i].addr + 0xe0000000) & 0x3fffffff) + 0x60000000),
					     pm->sections[i].size);
	if (!err && padding)
		err = sec_sha_update(&sha, &ctx, pm->padding_data, pm->padding_size);
	if (!err)
		err = sec_sha_finish(&sha, &ctx, digest, dig_len);
	sec_sha_uninit(&sha);
	if (err)
		hal_printf_level(LOG_ERR, "ERR %s, %d\n", __func__, __LINE__);
	return err;
}

/* key: modulus then exponent (RSA, key_len bytes) or x then y (SM2) */
static bool sig_verify(uint32_t sign_type, const uint8_t *key, uint32_t key_len,
		       const uint8_t *digest, uint32_t dig_len, const uint8_t *sig, uint32_t sig_len)
{
	uint8_t e[384] = { 0 };
	uint32_t n = sign_type == IMG_SIGN_RSA3072 ? 384 : 256;
	sec_rsa_context_t rsa = {
		.n = key, .e = e, .padding_type = SEC_RSA_PADDING_MODE_PSS,
		.hash_type = SEC_RSA_HASH_TYPE_SHA256,
	};
	sec_sm2_context_t sm2 = { .qx = key, .qy = key + 32 };
	static const char sm2_id[] = "cixtech.com";

	if (sign_type == IMG_SIGN_SM2)
		return sec_sm2_verify(&sm2, digest, dig_len, sm2_id, strlen(sm2_id), sig, sig_len);
	if (sign_type != IMG_SIGN_RSA3072 && sign_type != IMG_SIGN_RSA2048)
		return false;
	if (key_len <= n || key_len > 2 * n)
		return false;
	memcpy(e + 2 * n - key_len, key + n, key_len - n);	/* exponent right-aligned */
	rsa.key_bits = sign_type == IMG_SIGN_RSA3072 ? SEC_RSA_KEY_BITS_3072 : SEC_RSA_KEY_BITS_2048;
	return sec_rsa_verify(rsa.key_bits, &rsa, digest, dig_len, sig, sig_len, SEC_RSA_HASH_TYPE_SHA256);
}

static const char *sign_name(uint32_t t)
{
	return t == IMG_SIGN_RSA2048 ? "RSA2048" : t == IMG_SIGN_RSA3072 ? "RSA3072" : "SM2";
}

/* OEM key (hash in eFuse) checks the header; keep CIX's image key from it */
bool fw_verify_header(const cbff_img_header *h, uint32_t len)
{
	uint8_t rotpk[32], digest[32];
	uint32_t dig_len = 0, mode;
	sec_sha_mode_t m;
	const key_group *oem = &h->key_groups[0];

	if (h == NULL || len != 0x1000)
		return false;
	if (fw_get_digest_mode(h->sign_type, &m) || fw_efuse_get_cix_user_rotpk_hash(rotpk, h->sign_type) ||
	    fw_cal_digest(oem->key_value, oem->key_length, digest, &dig_len, m) ||
	    memcmp(rotpk, digest, 32))
		return false;
	hal_printf_level(LOG_INFO, "Verify OEM ROTPK successful\n");
	mode = h->sign_type == IMG_SIGN_SM2 ? SEC_SM3_MODE : SEC_SHA_MODE_256;
	if (fw_cal_digest(h, 0xe00, digest, &dig_len, mode))
		return false;
	if (!sig_verify(h->sign_type, oem->key_value, oem->key_length, digest, dig_len,
			h->signature.content, h->signature.len))
		return false;
	hal_printf_level(LOG_INFO, "Verify Image header with OEM %s key successful\n", sign_name(h->sign_type));
	memcpy(&cix_img_key, &h->key_groups[1], sizeof(cix_img_key));
	return true;
}

/* CIX's image key against the eFuse; sign_type picks the algorithm */
static bool check_cix_key(uint32_t sign_type, sec_sha_mode_t *mode, uint8_t rotpk[32])
{
	uint8_t digest[32];
	uint32_t dig_len = 0;

	if (fw_get_digest_mode(sign_type, mode) || fw_efuse_get_cix_rotpk_hash(rotpk, sign_type) ||
	    cix_img_key.key_length > sizeof(cix_img_key.key_value) ||
	    fw_cal_digest(cix_img_key.key_value, cix_img_key.key_length, digest, &dig_len, *mode) ||
	    memcmp(rotpk, digest, 32))
		return false;
	hal_printf_level(LOG_INFO, "Verify Cix ROTPK successful\n");
	return true;
}

static bool fw_verify_PBL_imgs(const uint8_t *img, uint32_t len, uint32_t sign_type)
{
	uint8_t rotpk[32], digest[32];
	uint32_t dig_len = 0;
	image_signature sig;
	sec_sha_mode_t mode;

	if (img == NULL || len < SIG_SIZE)
		return false;
	memcpy(&sig, img + len - SIG_SIZE, SIG_SIZE);
	if (!check_cix_key(sign_type, &mode, rotpk))
		return false;
	if (fw_km_set_key(CSEC_KEY_ID_HASH_ROOT_PUBKEY, rotpk, 32) ||
	    fw_km_set_key(CSEC_KEY_ID_PK, cix_img_key.key_value, cix_img_key.key_length) ||
	    fw_km_set_key(CSEC_KEY_ID_KEK, cix_img_key.key_value, cix_img_key.key_length))
		return false;
	if (fw_cal_digest(img, len - SIG_SIZE, digest, &dig_len,
			  sign_type == IMG_SIGN_SM2 ? SEC_SM3_MODE : SEC_SHA_MODE_256))
		return false;
	if (!sig_verify(sign_type, cix_img_key.key_value, cix_img_key.key_length, digest, dig_len,
			sig.content, sig.len))
		return false;
	hal_printf_level(LOG_INFO, "Verify Image by cix Cix %s key successful\n", sign_name(sign_type));
	return true;
}

static bool fw_verify_PM_imgs(const pm_data_info *pm, uint32_t sign_type)
{
	uint8_t rotpk[32], digest[32];
	uint32_t dig_len = 0;
	sec_sha_mode_t mode;

	if (pm == NULL || !check_cix_key(sign_type, &mode, rotpk))
		return false;
	if (fw_cal_pm_digest(pm, digest, &dig_len,
			     sign_type == IMG_SIGN_SM2 ? SEC_SM3_MODE : SEC_SHA_MODE_256, true))
		return false;
	if (!sig_verify(sign_type, cix_img_key.key_value, cix_img_key.key_length, digest, dig_len,
			pm->sig_dat->content, pm->sig_dat->len))
		return false;
	hal_printf_level(LOG_INFO, "Verify Image by cix Cix %s key successful\n", sign_name(sign_type));
	return true;
}

/* The header's digest table entry for an image, against what was loaded */
static bool check_hash(const cbff_img_header *h, sec_fw_type_t id, const uint8_t *digest, uint32_t len)
{
	uint32_t i;

	for (i = 0; i < h->hash_count && i < ARRAY_SIZE(h->hash_tables); i++) {
		if (h->hash_tables[i].image_id != id)
			continue;
		if (h->hash_tables[i].hash_len == len && memcmp(digest, h->hash_tables[i].hash_data, len) == 0)
			return true;
		break;
	}
	hal_printf_level(LOG_ERR, "[SE_FW]# Check hash data fail\n");
	return false;
}

static bool hash_mode(const cbff_img_header *h, sec_sha_mode_t *mode)
{
	uint32_t alg = 0;

	if (fw_efuse_get_info(FUSE_ID_SE_BT_HASH_ALG, &alg))
		return false;
	if (alg == 0) {
		alg = h->hash_type;
		hal_printf_level(LOG_INFO, "Use hasl type from header: 0x%x\n", (unsigned)alg);
	}
	*mode = alg == 2 ? SEC_SM3_MODE : SEC_SHA_MODE_256;
	return alg == 1 || alg == 2;
}

static bool fw_verify_PBL_img_hash(const cbff_img_header *h, const uint8_t *img, uint32_t len)
{
	uint8_t digest[64] = { 0 };
	uint32_t dig_len = 0;
	sec_sha_mode_t mode;

	if (hash_mode(h, &mode) && fw_cal_digest(img, len, digest, &dig_len, mode))
		return false;
	return check_hash(h, SEC_FW_TYPE_PBL, digest, dig_len);
}

static bool fw_verify_PM_img_hash(const cbff_img_header *h, const pm_data_info *pm)
{
	uint8_t digest[64] = { 0 };
	uint32_t dig_len = 0;
	sec_sha_mode_t mode;

	if (hash_mode(h, &mode) && fw_cal_pm_digest(pm, digest, &dig_len, mode, false))
		return false;
	return check_hash(h, SEC_FW_TYPE_PM, digest, dig_len);
}

/* Secure debug certificate: signed with the OEM key over its first 3 KiB */
bool fw_verify_sec_debug_img(const uint8_t *data)
{
	uint8_t digest[32];
	uint32_t dig_len = 0, sign_type;
	sec_sha_mode_t mode;
	const key_group *oem = &ROM_HDR->key_groups[0];

	if (data == NULL)
		return false;
	if (memcmp(CBFF_MAGIC, ROM_HDR, 8) && fw_xloader_get_header(ROM_HDR))
		return false;
	memcpy(&sign_type, data + 0x38, 4);
	if (fw_get_digest_mode(sign_type, &mode) || fw_cal_digest(data, 0xc00, digest, &dig_len, mode))
		return false;
	if (!sig_verify(sign_type, oem->key_value, oem->key_length, digest, dig_len, data + 0xc04,
			*(const uint32_t *)(data + 0xc00)))
		return false;
	hal_printf_level(LOG_INFO, "Verify secure debug binary with OEM %s key successful\n",
			 sign_name(sign_type));
	return true;
}

uint32_t fw_xloader_get_header(cbff_img_header *h)
{
	sec_storage_param_t p;
	uint32_t len = 0x1000, strap;

	if (h == NULL)
		return ERR_PARAM;
	strap = REG32(FLASH_STRAP);
	hal_printf_level(LOG_INFO, "Flash Offset flag from strappng pin is: 0x%x\n", (unsigned)strap);
	p.offset = BL1_FLASH(strap);
	hal_printf_level(LOG_INFO, "Offset of Bootloader1 image in flash is: 0x%x\n", (unsigned)p.offset);
	if (hal_stor_load_header(SEC_STOR_TYPE_FLASH, &p, (uint8_t *)h, &len))
		return ERR_HDR_LOAD;
	hal_printf_level(LOG_INFO, "boot_key_info: key solution:%d, rootkey id:%d\n",
			 (int)h->boot_key.key_sln, (int)h->boot_key.root_key);
	return 0;
}

static uint32_t fw_xloader_get_img_info(sec_fw_type_t id, const cbff_img_header *h,
					image_header_group *info)
{
	uint32_t i;

	if (h == NULL || info == NULL)
		return ERR_PARAM;
	for (i = 0; i < h->image_count && i < ARRAY_SIZE(h->image_header_groups); i++) {
		if (h->image_header_groups[i].image_id == id) {
			SE_INFO("Find Image ID: 0x%x in image header groups\n", id);
			memcpy(info, &h->image_header_groups[i], sizeof(*info));
			return 0;
		}
	}
	return ERR_NO_IMAGE;
}

static bool secure_boot(void)
{
	uint32_t sec = 0;

	return fw_efuse_get_info(FUSE_ID_SE_BT_SEC, &sec) == 0 && sec == 1;
}

static uint32_t load(uint32_t offset, void *dst, uint32_t len)
{
	sec_storage_param_t p = { .offset = offset };

	return hal_stor_load_fw(SEC_STOR_TYPE_FLASH, &p, dst, len);
}

/* CSU_PM: header, padding and signature to buffers, sections into the PM's SRAM */
static uint32_t load_pm(void)
{
	pm_data_info *pm = &g_PM_data_info;
	image_header_group ih;
	uint32_t err, i, *e, end, size;

	SE_INFO("Start to load PM firmware\n");
	memset(pm, 0, sizeof(*pm));
	pm->pm_header = malloc(0x268);
	pm->padding_data = malloc(0x1000);
	pm->sig_dat = malloc(SIG_SIZE);
	if (!pm->pm_header || !pm->padding_data || !pm->sig_dat)
		return ERR_NOMEM;
	err = fw_xloader_get_img_info(SEC_FW_TYPE_PM, ROM_HDR, &ih);
	if (err)
		return ERR_NOMEM;
	hal_printf_level(LOG_INFO, "Flash Offset flag from strappng pin is: 0x%x\n",
			 (unsigned)REG32(FLASH_STRAP));
	pm->base_flash_offset = ih.binary_offset + BL1_FLASH(REG32(FLASH_STRAP));
	pm->binary_size = ih.binary_length;
	end = round_4k(pm->binary_size + SIG_SIZE);
	pm->padding_size = end - SIG_SIZE - pm->binary_size;
	if (load(pm->base_flash_offset, pm->pm_header, 0x268))
		return ERR_IMAGE;
	if (pm->padding_size &&
	    load(pm->base_flash_offset + pm->binary_size, pm->padding_data, pm->padding_size))
		return ERR_IMAGE;
	if (load(pm->base_flash_offset + end - SIG_SIZE, pm->sig_dat, SIG_SIZE))
		return ERR_IMAGE;

	/* XBIN: magic, then from 0x10 sections of offset, address, size, memory size, flags */
	if (*(uint32_t *)pm->pm_header != XBIN_MAGIC)
		return ERR_PM_SECTION;
	hal_printf_level(LOG_INFO, "idx,    offset,      size,       mem_addr,    mem_size,     flags\n");
	for (i = 0; i < 30; i++) {
		e = (uint32_t *)(pm->pm_header + 0x10 + i * 0x14);
		if (e[0] + e[2] > pm->binary_size) {
			hal_printf_level(LOG_ERR, " %d - this section exceeds buffer size !!\n", (int)i);
			return ERR_PM_SECTION;
		}
		if (e[1] < 0x20000000 || e[1] + e[2] > PM_SRAM_END) {
			hal_printf_level(LOG_ERR, "There are illegal section in PM header\n");
			return ERR_PM_SECTION;
		}
		pm->sections[i].addr = e[1];
		pm->sections[i].size = e[2];
		hal_printf_level(LOG_INFO, " %02d,  0x%08x,  0x%08x,  0x%08x,  0x%08x,  0x%08x\n", (int)i,
				 (unsigned)e[0], (unsigned)e[2], (unsigned)e[1], (unsigned)e[3], (unsigned)e[4]);
		if (e[4] & BIT(31))
			break;
	}
	pm->section_num = MIN(i + 1, 30u);
	for (i = 0; i < pm->section_num; i++) {
		e = (uint32_t *)(pm->pm_header + 0x10 + i * 0x14);
		size = (e[2] + 3) & ~3u;
		if (size && load(pm->base_flash_offset + e[0], (void *)(uintptr_t)(e[1] + PM_SRAM_SE), size))
			return ERR_IMAGE;
	}
	if (secure_boot()) {
		if (!fw_verify_PM_img_hash(ROM_HDR, pm))
			return ERR_IMAGE;
		hal_printf_level(LOG_INFO, "Verify image hash successful\n");
		if (!fw_verify_PM_imgs(pm, ROM_HDR->sign_type))
			return ERR_IMAGE;
		hal_printf_level(LOG_INFO, "Verify image signature successful\n");
	}
	return 0;
}

/* BL2: image and signature to its run address in DRAM (SYSHUB window at 0x80000000) */
static uint32_t load_pbl(void)
{
	image_header_group ih;
	uint32_t size, offset, err;
	uint8_t *dst;

	err = fw_xloader_get_img_info(SEC_FW_TYPE_PBL, ROM_HDR, &ih);
	if (err)
		return ERR_NOMEM;
	size = round_4k(ih.binary_length + SIG_SIZE);
	dst = (uint8_t *)(uintptr_t)((ih.run_address & 0x3fffffff) + 0x60000000);
	hal_printf_level(LOG_INFO, "Flash Offset flag from strappng pin is: 0x%x\n",
			 (unsigned)REG32(FLASH_STRAP));
	offset = ih.binary_offset + BL1_FLASH(REG32(FLASH_STRAP));
	hal_printf_level(LOG_INFO, "Offset of image in flash is: 0x%x, 0x%x\n", (unsigned)offset,
			 (unsigned)(uintptr_t)dst);
	if (load(offset, dst, size))
		return ERR_IMAGE;
	if (secure_boot()) {
		if (!fw_verify_PBL_img_hash(ROM_HDR, dst, ih.binary_length))
			return ERR_IMAGE;
		hal_printf_level(LOG_INFO, "Verify image hash successful\n");
		if (!fw_verify_PBL_imgs(dst, size, ROM_HDR->sign_type))
			return ERR_IMAGE;
		hal_printf_level(LOG_INFO, "Verify image signature successful\n");
	}
	return 0;
}

uint32_t fw_xloader_load_img(sec_fw_type_t id)
{
	cbff_img_header *h = ROM_HDR;
	uint32_t err, i, cfg_size = 0;

	if (memcmp(CBFF_MAGIC, h, 8) && fw_xloader_get_header(h))
		return ERR_HDR_READ;
	for (i = 0; i < h->image_count && i < ARRAY_SIZE(h->image_header_groups); i++) {
		const image_header_group *g = &h->image_header_groups[i];

		hal_printf_level(LOG_INFO, "Sub-image[%d] in target files:\n", (int)i);
		hal_printf_level(LOG_INFO, "image_id: %d\n", (int)g->image_id);
		hal_printf_level(LOG_INFO, "image_version: %d.%d\n", (int)g->version.major_ver,
				 (int)g->version.minor_ver);
		hal_printf_level(LOG_INFO, "binary_offset: 0x%x\n", (unsigned)g->binary_offset);
		hal_printf_level(LOG_INFO, "binary_length: 0x%x\n", (unsigned)g->binary_length);
		hal_printf_level(LOG_INFO, "run_address: 0x%x\n", (unsigned)g->run_address);
	}
	if (secure_boot()) {
		hal_printf_level(LOG_INFO, "Secure boot enable\n");
		if (!fw_verify_header(h, 0x1000)) {
			hal_printf_level(LOG_ERR, "[SE_FW]# Verify header in sram fail, so re-read header from flash & try it again\n");
			if (fw_xloader_get_header(h))
				return ERR_HDR_READ;
			if (!fw_verify_header(h, 0x1000))
				return ERR_HDR_VERIFY;
		}
	} else {
		hal_printf_level(LOG_ERR, "[SE_FW]# Secure boot disable\n");
	}
	if (id == SEC_FW_TYPE_PBL) {
		err = load_pbl();
		if (err)
			SE_ERR(err);
		return err;
	}
	if (id != SEC_FW_TYPE_PM)
		return ERR_LOAD;
	err = load_pm();
	free(g_PM_data_info.pm_header);
	free(g_PM_data_info.padding_data);
	free(g_PM_data_info.sig_dat);
	if (err) {
		hal_printf_level(LOG_ERR, "ERR %s, %d\n", "l_xloader_get_PM_image", (int)err);
		return ERR_LOAD;
	}
	hal_printf_level(LOG_INFO, "Start to load PM config binary\n");
	return hal_stor_get_sub_img(PM_CONFIG_IMG_DATA_TYPE, (uint8_t *)PM_CONFIG_DST, &cfg_size);
}

/* Jumper: what each stage needs before and when it starts */
uint32_t fw_jumper_init(sec_fw_type_t type)
{
	uint32_t err = 0;

	if (type == SEC_FW_TYPE_PBL) {
		if (!plat_get_syshub_model()) {
			hal_printf_level(LOG_ERR, "!!! there is no syshub in soc, access ddr failed \n");
			return 0;
		}
		err = sysh_io_init(0x80000000);
	} else if (type == SEC_FW_TYPE_PM) {
		drv_pm_init();
		err = sysh_io_init(0x1c90000000ull);
	} else if (type == SEC_FW_TYPE_SFH)
		err = sysh_io_init(0x1ca0000000ull);
	if (err)
		SE_ERR(err);
	return err;
}

uint32_t fw_jumper_prejump(sec_fw_type_t type)
{
	uint32_t err = 0;

	if (type == SEC_FW_TYPE_PBL) {
		if (!plat_get_syshub_model()) {
			hal_printf_level(LOG_ERR, "!!! there is no syshub in soc, access ddr failed \n");
			return 0;
		}
		err = fw_km_sync(CSEC_KM_BL2);
		if (!err)
			err = fw_km_sync(CSEC_KM_BL33);
		if (!err)
			err = fw_km_sync(CSEC_KM_TEE);
	} else if (type == SEC_FW_TYPE_PM) {
		err = fw_fuse_dist(FUSED_TYPE_PVT);
	}
	if (err)
		SE_ERR(err);
	return err;
}

void fw_jumper_run(sec_fw_type_t type)
{
	if (type == SEC_FW_TYPE_PBL)
		drv_soc_release_ap();
	else if (type == SEC_FW_TYPE_PM)
		drv_soc_release_pm();
	sysh_io_init(0x80000000);
}
