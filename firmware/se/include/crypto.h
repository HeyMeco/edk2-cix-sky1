/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Crypto engine and the crypto services (library/crypto, hal/csec.c) */
#ifndef CRYPTO_H
#define CRYPTO_H

#include <stdbool.h>
#include <stdint.h>

/* CE-Lite engine (library/crypto/ce.c) */
uint32_t ce_drv_init(void);
uint32_t ce_trng_read(uint8_t *buf, uint32_t len);
uint32_t ce_otp_read(uint32_t off, uint8_t *buf, uint32_t len);
uint32_t ce_otp_write(uint32_t off, uint8_t *buf, uint32_t len);
uint32_t ce_otp_get_cfg_info(uint8_t *exist, uint16_t *tst, uint16_t *s, uint16_t *ns);

/* SM3 (library/crypto/sm3.c) */
typedef struct {
	uint32_t total[2];
	uint32_t state[8];
	uint8_t buffer[64];
} sm3_context;

void sm3_starts(sm3_context *ctx);
void sm3_update(sm3_context *ctx, const uint8_t *input, uint32_t len);
void sm3_finish(sm3_context *ctx, uint8_t out[32]);
void sm3(const uint8_t *input, uint32_t len, uint8_t out[32]);

/* SM2 signature check (library/crypto/sm2.c): qx, qy 32 bytes; sig DER (r, s) */
uint32_t sm2_verify(const uint8_t *qx, const uint8_t *qy, const char *id, uint32_t id_len,
		    const uint8_t *msg, uint32_t msg_len, const uint8_t *sig, uint32_t sig_len);

uint32_t crc32(const uint8_t *buf, uint32_t len);

/* hal/csec.c */
typedef enum { SEC_SHA_MODE_256, SEC_SHA_MODE_384, SEC_SHA_MODE_512, SEC_SM3_MODE } sec_sha_mode_t;

typedef struct {
	void *sha_ctx;
	uint32_t idx;
} sec_sha_t;

typedef struct {
	sec_sha_mode_t mode;
} sec_sha_context_t;

uint32_t sec_sha_init(sec_sha_t *sha, uint32_t idx);
void sec_sha_uninit(sec_sha_t *sha);
uint32_t sec_sha_start(sec_sha_t *sha, sec_sha_context_t *ctx, sec_sha_mode_t mode);
uint32_t sec_sha_update(sec_sha_t *sha, sec_sha_context_t *ctx, const void *input, uint32_t size);
uint32_t sec_sha_finish(sec_sha_t *sha, sec_sha_context_t *ctx, void *output, uint32_t *size);

typedef enum {
	SEC_RSA_KEY_BITS_192, SEC_RSA_KEY_BITS_256, SEC_RSA_KEY_BITS_512, SEC_RSA_KEY_BITS_1024,
	SEC_RSA_KEY_BITS_2048, SEC_RSA_KEY_BITS_3072, SEC_RSA_KEY_BITS_4096,
} sec_rsa_key_bits_t;
typedef enum { SEC_RSA_PADDING_MODE_PKCS1, SEC_RSA_PADDING_MODE_PSS } sec_rsa_padding_type_t;
typedef enum {
	SEC_RSA_HASH_TYPE_MD5, SEC_RSA_HASH_TYPE_SHA1, SEC_RSA_HASH_TYPE_SHA224,
	SEC_RSA_HASH_TYPE_SHA256, SEC_RSA_HASH_TYPE_SHA384, SEC_RSA_HASH_TYPE_SHA512,
} sec_rsa_hash_type_t;

typedef struct {
	const uint8_t *n;	/* big-endian, key length */
	const uint8_t *e;	/* big-endian, key length */
	const uint8_t *d;
	uint8_t key_bits;	/* sec_rsa_key_bits_t */
	uint8_t padding_type;	/* sec_rsa_padding_type_t */
	uint8_t hash_type;	/* sec_rsa_hash_type_t, of the digest signed */
} sec_rsa_context_t;

bool sec_rsa_verify(sec_rsa_key_bits_t bits, sec_rsa_context_t *ctx, const void *digest,
		    uint32_t digest_size, const void *sig, uint32_t sig_size,
		    sec_rsa_hash_type_t hash_type);

typedef struct {
	const uint8_t *d, *qx, *qy;
} sec_sm2_context_t;

bool sec_sm2_verify(sec_sm2_context_t *ctx, const uint8_t *msg, uint32_t msg_size, const char *id,
		    uint32_t id_size, const uint8_t *sig, uint32_t sig_size);

uint32_t sec_trng_get_data(uint8_t *buf, uint32_t size);
uint32_t hal_crc(const uint8_t *input, uint32_t len, uint16_t *crc);
uint32_t hal_crypto_init(void);
uint32_t crypto_platform_setup(void);
uint32_t sec_hkdf_sha256(const uint8_t *ikm, uint32_t ikm_len, uint8_t *okm, uint32_t okm_len);

#endif
