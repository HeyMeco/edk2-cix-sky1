/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * SM2 signature verification (GB/T 32918.2-2016) on Mbed TLS's prime-field
 * arithmetic: e = SM3(ZA || M), ZA = SM3(ENTL || ID || a || b || G || Q);
 * the signature (r, s) is valid when R = (e + x1) mod n == r, where
 * (x1, y1) = s*G + (r + s)*Q.
 */
#include "se.h"
#include "crypto.h"
#include "mbedtls/asn1.h"
#include "mbedtls/ecp.h"

#define SM2_ERR_VERIFY		0x950f
#define SM2_ERR_PARAM		0x950c

static const uint8_t sm2_p[32] = {
	0xff, 0xff, 0xff, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};
static const uint8_t sm2_a[32] = {
	0xff, 0xff, 0xff, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfc,
};
static const uint8_t sm2_b[32] = {
	0x28, 0xe9, 0xfa, 0x9e, 0x9d, 0x9f, 0x5e, 0x34, 0x4d, 0x5a, 0x9e, 0x4b, 0xcf, 0x65, 0x09, 0xa7,
	0xf3, 0x97, 0x89, 0xf5, 0x15, 0xab, 0x8f, 0x92, 0xdd, 0xbc, 0xbd, 0x41, 0x4d, 0x94, 0x0e, 0x93,
};
static const uint8_t sm2_gx[32] = {
	0x32, 0xc4, 0xae, 0x2c, 0x1f, 0x19, 0x81, 0x19, 0x5f, 0x99, 0x04, 0x46, 0x6a, 0x39, 0xc9, 0x94,
	0x8f, 0xe3, 0x0b, 0xbf, 0xf2, 0x66, 0x0b, 0xe1, 0x71, 0x5a, 0x45, 0x89, 0x33, 0x4c, 0x74, 0xc7,
};
static const uint8_t sm2_gy[32] = {
	0xbc, 0x37, 0x36, 0xa2, 0xf4, 0xf6, 0x77, 0x9c, 0x59, 0xbd, 0xce, 0xe3, 0x6b, 0x69, 0x21, 0x53,
	0xd0, 0xa9, 0x87, 0x7c, 0xc6, 0x2a, 0x47, 0x40, 0x02, 0xdf, 0x32, 0xe5, 0x21, 0x39, 0xf0, 0xa0,
};
static const uint8_t sm2_n[32] = {
	0xff, 0xff, 0xff, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0x72, 0x03, 0xdf, 0x6b, 0x21, 0xc6, 0x05, 0x2b, 0x53, 0xbb, 0xf4, 0x09, 0x39, 0xd5, 0x41, 0x23,
};

/* a = p - 3: Mbed TLS takes an unset A as -3 */
static int sm2_group(mbedtls_ecp_group *grp)
{
	int ret;

	mbedtls_ecp_group_init(grp);
	if ((ret = mbedtls_mpi_read_binary(&grp->P, sm2_p, 32)) ||
	    (ret = mbedtls_mpi_read_binary(&grp->B, sm2_b, 32)) ||
	    (ret = mbedtls_mpi_read_binary(&grp->N, sm2_n, 32)) ||
	    (ret = mbedtls_mpi_read_binary(&grp->G.X, sm2_gx, 32)) ||
	    (ret = mbedtls_mpi_read_binary(&grp->G.Y, sm2_gy, 32)) ||
	    (ret = mbedtls_mpi_lset(&grp->G.Z, 1)))
		return ret;
	grp->pbits = 256;
	grp->nbits = 256;
	grp->h = 1;
	return 0;
}

static uint32_t za_digest(const uint8_t *qx, const uint8_t *qy, const char *id, uint32_t id_len,
			  uint8_t za[32])
{
	sm3_context c;
	uint8_t entl[2] = { (uint8_t)(id_len * 8 >> 8), (uint8_t)(id_len * 8) };

	if (id_len > 0x1fff)
		return SM2_ERR_PARAM;
	sm3_starts(&c);
	sm3_update(&c, entl, 2);
	sm3_update(&c, (const uint8_t *)id, id_len);
	sm3_update(&c, sm2_a, 32);
	sm3_update(&c, sm2_b, 32);
	sm3_update(&c, sm2_gx, 32);
	sm3_update(&c, sm2_gy, 32);
	sm3_update(&c, qx, 32);
	sm3_update(&c, qy, 32);
	sm3_finish(&c, za);
	return 0;
}

static int read_sig(const uint8_t *sig, uint32_t len, mbedtls_mpi *r, mbedtls_mpi *s)
{
	unsigned char *p = (unsigned char *)sig;
	const unsigned char *end = sig + len;
	size_t n;

	if (mbedtls_asn1_get_tag(&p, end, &n, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE) ||
	    p + n != end || mbedtls_asn1_get_mpi(&p, end, r) || mbedtls_asn1_get_mpi(&p, end, s) ||
	    p != end)
		return -1;
	return 0;
}

uint32_t sm2_verify(const uint8_t *qx, const uint8_t *qy, const char *id, uint32_t id_len,
		    const uint8_t *msg, uint32_t msg_len, const uint8_t *sig, uint32_t sig_len)
{
	mbedtls_ecp_group grp;
	mbedtls_ecp_point q, pt;
	mbedtls_mpi r, s, t, e;
	uint8_t za[32], h[32];
	sm3_context c;
	uint32_t ret = SM2_ERR_VERIFY;

	if (za_digest(qx, qy, id, id_len, za))
		return SM2_ERR_PARAM;
	sm3_starts(&c);
	sm3_update(&c, za, 32);
	sm3_update(&c, msg, msg_len);
	sm3_finish(&c, h);

	mbedtls_ecp_point_init(&q);
	mbedtls_ecp_point_init(&pt);
	mbedtls_mpi_init(&r);
	mbedtls_mpi_init(&s);
	mbedtls_mpi_init(&t);
	mbedtls_mpi_init(&e);
	if (sm2_group(&grp) ||
	    mbedtls_mpi_read_binary(&q.X, qx, 32) || mbedtls_mpi_read_binary(&q.Y, qy, 32) ||
	    mbedtls_mpi_lset(&q.Z, 1) || mbedtls_ecp_check_pubkey(&grp, &q) ||
	    read_sig(sig, sig_len, &r, &s))
		goto out;
	/* 1 <= r, s < n; t = (r + s) mod n != 0 */
	if (mbedtls_mpi_cmp_int(&r, 1) < 0 || mbedtls_mpi_cmp_mpi(&r, &grp.N) >= 0 ||
	    mbedtls_mpi_cmp_int(&s, 1) < 0 || mbedtls_mpi_cmp_mpi(&s, &grp.N) >= 0 ||
	    mbedtls_mpi_add_mpi(&t, &r, &s) || mbedtls_mpi_mod_mpi(&t, &t, &grp.N) ||
	    mbedtls_mpi_cmp_int(&t, 0) == 0)
		goto out;
	if (mbedtls_ecp_muladd(&grp, &pt, &s, &grp.G, &t, &q) ||
	    mbedtls_mpi_read_binary(&e, h, 32) || mbedtls_mpi_add_mpi(&e, &e, &pt.X) ||
	    mbedtls_mpi_mod_mpi(&e, &e, &grp.N))
		goto out;
	if (mbedtls_mpi_cmp_mpi(&e, &r) == 0)
		ret = 0;
out:
	mbedtls_ecp_group_free(&grp);
	mbedtls_ecp_point_free(&q);
	mbedtls_ecp_point_free(&pt);
	mbedtls_mpi_free(&r);
	mbedtls_mpi_free(&s);
	mbedtls_mpi_free(&t);
	mbedtls_mpi_free(&e);
	return ret;
}
