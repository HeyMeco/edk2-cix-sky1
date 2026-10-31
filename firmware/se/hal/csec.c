/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Crypto services for the firmware (CIX hal/common/csec, hal_crypto.c):
 * digests, RSA and SM2 signature checks, random numbers, CRCs, key
 * derivation, and the crypto engine's start-up.
 */
#include "se.h"
#include "crypto.h"
#include "efuse.h"
#include "mbedtls/hkdf.h"
#include "mbedtls/md.h"
#include "mbedtls/rsa.h"
#include "mbedtls/sha256.h"
#include "mbedtls/sha512.h"

#define SHA_ERR		0xa401
#define CRC_ERR		0xa901

typedef union {
	mbedtls_sha256_context sha256;
	mbedtls_sha512_context sha512;
	sm3_context sm3;
} sha_ctx;

uint32_t sec_sha_init(sec_sha_t *sha, uint32_t idx)
{
	if (sha == NULL)
		return SHA_ERR;
	memset(sha, 0, sizeof(*sha));
	sha->sha_ctx = calloc(1, sizeof(sha_ctx));
	return sha->sha_ctx ? 0 : SHA_ERR;
}

void sec_sha_uninit(sec_sha_t *sha)
{
	free(sha->sha_ctx);
	memset(sha, 0, sizeof(*sha));
}

uint32_t sec_sha_start(sec_sha_t *sha, sec_sha_context_t *ctx, sec_sha_mode_t mode)
{
	sha_ctx *c = sha->sha_ctx;

	if (ctx == NULL)
		return SHA_ERR;
	ctx->mode = mode;
	switch (mode) {
	case SEC_SHA_MODE_256:
		mbedtls_sha256_init(&c->sha256);
		return mbedtls_sha256_starts_ret(&c->sha256, 0) ? SHA_ERR : 0;
	case SEC_SHA_MODE_384:
		mbedtls_sha512_init(&c->sha512);
		return mbedtls_sha512_starts_ret(&c->sha512, 1) ? SHA_ERR : 0;
	case SEC_SM3_MODE:
		sm3_starts(&c->sm3);
		return 0;
	default:
		return SHA_ERR;
	}
}

uint32_t sec_sha_update(sec_sha_t *sha, sec_sha_context_t *ctx, const void *input, uint32_t size)
{
	sha_ctx *c = sha->sha_ctx;

	if (ctx == NULL || input == NULL)
		return SHA_ERR;
	switch (ctx->mode) {
	case SEC_SHA_MODE_256:
		return mbedtls_sha256_update_ret(&c->sha256, input, size) ? SHA_ERR : 0;
	case SEC_SHA_MODE_384:
		return mbedtls_sha512_update_ret(&c->sha512, input, size) ? SHA_ERR : 0;
	case SEC_SM3_MODE:
		sm3_update(&c->sm3, input, size);
		return 0;
	default:
		return 0;
	}
}

uint32_t sec_sha_finish(sec_sha_t *sha, sec_sha_context_t *ctx, void *output, uint32_t *size)
{
	sha_ctx *c = sha->sha_ctx;

	if (ctx == NULL || output == NULL || size == NULL)
		return SHA_ERR;
	switch (ctx->mode) {
	case SEC_SHA_MODE_256:
		if (mbedtls_sha256_finish_ret(&c->sha256, output))
			return SHA_ERR;
		mbedtls_sha256_free(&c->sha256);
		*size = 32;
		return 0;
	case SEC_SHA_MODE_384:
		if (mbedtls_sha512_finish_ret(&c->sha512, output))
			return SHA_ERR;
		mbedtls_sha512_free(&c->sha512);
		*size = 48;
		return 0;
	case SEC_SM3_MODE:
		sm3_finish(&c->sm3, output);
		*size = 32;
		return 0;
	default:
		return 0;
	}
}

static const uint32_t rsa_len[] = {
	[SEC_RSA_KEY_BITS_1024] = 128, [SEC_RSA_KEY_BITS_2048] = 256,
	[SEC_RSA_KEY_BITS_3072] = 384, [SEC_RSA_KEY_BITS_4096] = 512,
};

static mbedtls_md_type_t md_type(uint32_t hash_type)
{
	return hash_type == SEC_RSA_HASH_TYPE_SHA256 ? MBEDTLS_MD_SHA256 :
	       hash_type == SEC_RSA_HASH_TYPE_SHA384 ? MBEDTLS_MD_SHA384 : MBEDTLS_MD_NONE;
}

/* digest: the message digest the signature covers; PKCS#1 v1.5 or PSS (any salt length) */
bool sec_rsa_verify(sec_rsa_key_bits_t bits, sec_rsa_context_t *ctx, const void *digest,
		    uint32_t digest_size, const void *sig, uint32_t sig_size,
		    sec_rsa_hash_type_t hash_type)
{
	mbedtls_rsa_context rsa;
	uint32_t len = bits < ARRAY_SIZE(rsa_len) ? rsa_len[bits] : 0;
	int padding, ret;

	if (ctx == NULL || digest == NULL || sig == NULL || len == 0)
		return false;
	hal_printf_level(LOG_INFO, "padding_type:%d\n", ctx->padding_type);
	if (ctx->padding_type == SEC_RSA_PADDING_MODE_PKCS1)
		padding = MBEDTLS_RSA_PKCS_V15;
	else if (ctx->padding_type == SEC_RSA_PADDING_MODE_PSS)
		padding = MBEDTLS_RSA_PKCS_V21;
	else
		return false;
	hal_printf_level(LOG_INFO, "rsa bits:%d\n", bits);
	ctx->key_bits = bits;
	mbedtls_rsa_init(&rsa, padding, md_type(ctx->hash_type));
	ret = mbedtls_mpi_read_binary(&rsa.N, ctx->n, len);
	if (!ret)
		ret = mbedtls_mpi_read_binary(&rsa.E, ctx->e, len);
	rsa.len = len;
	if (!ret)
		ret = mbedtls_rsa_check_pubkey(&rsa);
	if (!ret)
		ret = mbedtls_rsa_pkcs1_verify(&rsa, NULL, NULL, MBEDTLS_RSA_PUBLIC, md_type(hash_type),
					       digest_size, digest, sig);
	mbedtls_rsa_free(&rsa);
	if (ret)
		hal_printf_level(LOG_ERR, "ERR %s, %d: -0x%x\n", __func__, __LINE__, (unsigned)-ret);
	return ret == 0;
}

bool sec_sm2_verify(sec_sm2_context_t *ctx, const uint8_t *msg, uint32_t msg_size, const char *id,
		    uint32_t id_size, const uint8_t *sig, uint32_t sig_size)
{
	uint32_t err = sm2_verify(ctx->qx, ctx->qy, id, id_size, msg, msg_size, sig, sig_size);

	if (err == 0)
		hal_printf_level(LOG_INFO, "SM2DSA Read Signature PASS!\n");
	else
		hal_printf_level(LOG_INFO, "SM2DSA Read Signature FAILED!\n");
	return err == 0;
}

uint32_t sec_trng_get_data(uint8_t *buf, uint32_t size)
{
	uint32_t err = ce_trng_read(buf, size);

	if (err)
		SE_ERR(err);
	return err;
}

/* CRC-16/CCITT-FALSE */
uint32_t hal_crc(const uint8_t *input, uint32_t len, uint16_t *crc)
{
	uint16_t c = 0xffff;
	int k;

	*crc = 0xffff;
	if (len && input == NULL)
		return CRC_ERR;
	while (len--) {
		c ^= (uint16_t)*input++ << 8;
		for (k = 0; k < 8; k++)
			c = c & 0x8000 ? (uint16_t)(c << 1) ^ 0x1021 : (uint16_t)(c << 1);
	}
	*crc = c;
	return 0;
}

/* HKDF-SHA256 with no salt and no info (the key manager's derivation) */
uint32_t sec_hkdf_sha256(const uint8_t *ikm, uint32_t ikm_len, uint8_t *okm, uint32_t okm_len)
{
	return mbedtls_hkdf(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), NULL, 0, ikm, ikm_len,
			    NULL, 0, okm, okm_len) ? 0xffffffff : 0;
}

/* c_mbedtls_platform_setup: the engine's start-up */
uint32_t crypto_platform_setup(void)
{
	uint32_t err = ce_drv_init();

	if (err == 0)
		return 0;
	return err == 0x9c05 || err == 0x9c06 ? 0x9704 : 0x9702;
}

/*
 * Load the engine's OTP shadow from the eFuse: model key, device root key,
 * and life cycle state 0, for the engine's key ladder (the "dummy OTP").
 */
uint32_t hal_crypto_init(void)
{
	uint8_t model_key[16], root_key[16], lcs[4] = { 0 }, exist;
	uint16_t tst, s, ns;
	uint32_t err;

	err = ce_otp_get_cfg_info(&exist, &tst, &s, &ns);
	if (err) {
		hal_printf_level(LOG_INFO, "CRYPTO_LITE  c_mbedtls_otp_get_conf failed!\n");
		return err;
	}
	err = sec_efuse_read(&g_handle_efuse, 0x54, 0, 128, model_key);
	if (!err)
		err = sec_efuse_read(&g_handle_efuse, 0x64, 0, 128, root_key);
	if (!err)
		err = ce_otp_write(0x04, model_key, 16);
	if (!err)
		err = ce_otp_write(0x18, root_key, 16);
	if (!err)
		err = ce_otp_write(0x48, lcs, 4);
	memset(model_key, 0, sizeof(model_key));
	memset(root_key, 0, sizeof(root_key));
	if (err) {
		hal_printf_level(LOG_ERR, "ERR %s, %d\n", __func__, __LINE__);
		return err;
	}
	hal_printf_level(LOG_INFO, "CRYPTO_LITE  Dummy OTP initial ok!\n");
	return 0;
}
