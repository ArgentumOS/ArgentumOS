/*
 * sha1.c — SHA-1 (RFC 3174 / FIPS 180-1), compact C89.
 *
 * API: sha1_begin / sha1_hash / sha1_end — the surface the X server's
 * os/xsha1.c 'libsha1' backend requires. Self-contained, no allocation.
 * Verified: SHA1("abc") == a9993e364706816aba3e25717850c26c9cd0d89d and
 * the two-block FIPS vector.
 */
#include "libsha1.h"

static uint32_t rol32(uint32_t v, int n)
{
	return (v << n) | (v >> (32 - n));
}

static void block(sha1_ctx *ctx, const uint8_t *p)
{
	uint32_t w[80];
	uint32_t a, b, c, d, e;
	int t;

	for(t = 0; t < 16; t++) {
		w[t] = ((uint32_t)p[t * 4] << 24) | ((uint32_t)p[t * 4 + 1] << 16) |
		       ((uint32_t)p[t * 4 + 2] << 8) | (uint32_t)p[t * 4 + 3];
	}
	for(t = 16; t < 80; t++) {
		w[t] = rol32(w[t - 3] ^ w[t - 8] ^ w[t - 14] ^ w[t - 16], 1);
	}
	a = ctx->state[0];
	b = ctx->state[1];
	c = ctx->state[2];
	d = ctx->state[3];
	e = ctx->state[4];
	for(t = 0; t < 80; t++) {
		uint32_t f, k;

		if(t < 20) {
			f = (b & c) | (~b & d);
			k = 0x5a827999;
		} else if(t < 40) {
			f = b ^ c ^ d;
			k = 0x6ed9eba1;
		} else if(t < 60) {
			f = (b & c) | (b & d) | (c & d);
			k = 0x8f1bbcdc;
		} else {
			f = b ^ c ^ d;
			k = 0xca62c1d6;
		}
		{
			uint32_t tmp = rol32(a, 5) + f + e + k + w[t];

			e = d;
			d = c;
			c = rol32(b, 30);
			b = a;
			a = tmp;
		}
	}
	ctx->state[0] += a;
	ctx->state[1] += b;
	ctx->state[2] += c;
	ctx->state[3] += d;
	ctx->state[4] += e;
}

void sha1_begin(sha1_ctx *ctx)
{
	ctx->state[0] = 0x67452301;
	ctx->state[1] = 0xefcdab89;
	ctx->state[2] = 0x98badcfe;
	ctx->state[3] = 0x10325476;
	ctx->state[4] = 0xc3d2e1f0;
	ctx->count = 0;
}

void sha1_hash(const void *data, size_t size, sha1_ctx *ctx)
{
	const uint8_t *p = data;
	size_t fill = (size_t)((ctx->count >> 3) & 63);

	ctx->count += (uint64_t)size * 8;
	if(fill) {
		size_t need = 64 - fill;

		if(size < need) {
			/* keep partial block buffered */
			__builtin_memcpy(ctx->buffer + fill, p, size);
			return;
		}
		__builtin_memcpy(ctx->buffer + fill, p, need);
		block(ctx, ctx->buffer);
		p += need;
		size -= need;
	}
	while(size >= 64) {
		block(ctx, p);
		p += 64;
		size -= 64;
	}
	if(size) {
		__builtin_memcpy(ctx->buffer, p, size);
	}
}

void sha1_end(uint8_t digest[20], sha1_ctx *ctx)
{
	uint64_t bits = ctx->count;	/* message length in bits so far */
	uint8_t byte, lenb[8];
	int t;

	/* 0x80 terminator */
	byte = 0x80;
	sha1_hash(&byte, 1, ctx);
	/* pad with zeros to 56 mod 64 */
	byte = 0;
	while(((ctx->count >> 3) & 63) != 56) {
		sha1_hash(&byte, 1, ctx);
	}
	/* 64-bit big-endian bit count */
	for(t = 7; t >= 0; t--) {
		lenb[t] = (uint8_t)(bits >> ((7 - t) * 8));
	}
	sha1_hash(lenb, 8, ctx);
	/* emit digest */
	for(t = 0; t < 5; t++) {
		digest[t * 4] = (uint8_t)(ctx->state[t] >> 24);
		digest[t * 4 + 1] = (uint8_t)(ctx->state[t] >> 16);
		digest[t * 4 + 2] = (uint8_t)(ctx->state[t] >> 8);
		digest[t * 4 + 3] = (uint8_t)(ctx->state[t]);
	}
}
