/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* SM3 hash (GB/T 32905-2016) */
#include "se.h"
#include "crypto.h"

#define ROL(x, n)	(((x) << ((n) & 31)) | ((x) >> ((32 - ((n) & 31)) & 31)))
#define P0(x)		((x) ^ ROL((x), 9) ^ ROL((x), 17))
#define P1(x)		((x) ^ ROL((x), 15) ^ ROL((x), 23))

static uint32_t be32(const uint8_t *p)
{
	return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

static void put_be32(uint8_t *p, uint32_t v)
{
	p[0] = v >> 24;
	p[1] = v >> 16;
	p[2] = v >> 8;
	p[3] = v;
}

static void sm3_process(sm3_context *ctx, const uint8_t data[64])
{
	uint32_t w[68], w1[64], a, b, c, d, e, f, g, h, ss1, ss2, tt1, tt2, t;
	int j;

	for (j = 0; j < 16; j++)
		w[j] = be32(data + 4 * j);
	for (j = 16; j < 68; j++)
		w[j] = P1(w[j - 16] ^ w[j - 9] ^ ROL(w[j - 3], 15)) ^ ROL(w[j - 13], 7) ^ w[j - 6];
	for (j = 0; j < 64; j++)
		w1[j] = w[j] ^ w[j + 4];
	a = ctx->state[0], b = ctx->state[1], c = ctx->state[2], d = ctx->state[3];
	e = ctx->state[4], f = ctx->state[5], g = ctx->state[6], h = ctx->state[7];
	for (j = 0; j < 64; j++) {
		t = j < 16 ? 0x79cc4519 : 0x7a879d8a;
		ss1 = ROL(ROL(a, 12) + e + ROL(t, j), 7);
		ss2 = ss1 ^ ROL(a, 12);
		if (j < 16) {
			tt1 = (a ^ b ^ c) + d + ss2 + w1[j];
			tt2 = (e ^ f ^ g) + h + ss1 + w[j];
		} else {
			tt1 = ((a & b) | (a & c) | (b & c)) + d + ss2 + w1[j];
			tt2 = ((e & f) | (~e & g)) + h + ss1 + w[j];
		}
		d = c;
		c = ROL(b, 9);
		b = a;
		a = tt1;
		h = g;
		g = ROL(f, 19);
		f = e;
		e = P0(tt2);
	}
	ctx->state[0] ^= a, ctx->state[1] ^= b, ctx->state[2] ^= c, ctx->state[3] ^= d;
	ctx->state[4] ^= e, ctx->state[5] ^= f, ctx->state[6] ^= g, ctx->state[7] ^= h;
}

void sm3_starts(sm3_context *ctx)
{
	static const uint32_t iv[8] = {
		0x7380166f, 0x4914b2b9, 0x172442d7, 0xda8a0600,
		0xa96f30bc, 0x163138aa, 0xe38dee4d, 0xb0fb0e4e,
	};

	memset(ctx, 0, sizeof(*ctx));
	memcpy(ctx->state, iv, sizeof(iv));
}

void sm3_update(sm3_context *ctx, const uint8_t *input, uint32_t len)
{
	uint32_t fill = ctx->total[0] & 63, n;

	ctx->total[0] += len;
	if (ctx->total[0] < len)
		ctx->total[1]++;
	while (len) {
		n = MIN(len, 64 - fill);
		memcpy(ctx->buffer + fill, input, n);
		fill += n;
		input += n;
		len -= n;
		if (fill == 64) {
			sm3_process(ctx, ctx->buffer);
			fill = 0;
		}
	}
}

void sm3_finish(sm3_context *ctx, uint8_t out[32])
{
	static const uint8_t pad[64] = { 0x80 };
	uint32_t fill = ctx->total[0] & 63, hi, lo, i;
	uint8_t len[8];

	hi = (ctx->total[0] >> 29) | (ctx->total[1] << 3);
	lo = ctx->total[0] << 3;
	put_be32(len, hi);
	put_be32(len + 4, lo);
	sm3_update(ctx, pad, fill < 56 ? 56 - fill : 120 - fill);
	sm3_update(ctx, len, 8);
	for (i = 0; i < 8; i++)
		put_be32(out + 4 * i, ctx->state[i]);
}

void sm3(const uint8_t *input, uint32_t len, uint8_t out[32])
{
	sm3_context ctx;

	sm3_starts(&ctx);
	sm3_update(&ctx, input, len);
	sm3_finish(&ctx, out);
}
