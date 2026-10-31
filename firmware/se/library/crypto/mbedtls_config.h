/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Mbed TLS 2.28 for the SE: hashes, HKDF, RSA verification, prime-field ECC (SM2) */
#ifndef SE_MBEDTLS_CONFIG_H
#define SE_MBEDTLS_CONFIG_H

#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY

#define MBEDTLS_MD_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA256_SMALLER
#define MBEDTLS_SHA512_C
#define MBEDTLS_HKDF_C

#define MBEDTLS_BIGNUM_C
#define MBEDTLS_RSA_C
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_PKCS1_V21
#define MBEDTLS_OID_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C

#define MBEDTLS_ECP_C
#define MBEDTLS_ECP_NO_INTERNAL_RNG		/* only verification: no secret scalars */
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED	/* Mbed TLS needs one; SM2's curve is set up by hand */

#include "mbedtls/check_config.h"

#endif
