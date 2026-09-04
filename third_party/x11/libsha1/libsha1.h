/*
 * libsha1.h — minimal SHA-1 (RFC 3174) with the sha1_begin/sha1_hash/
 * sha1_end API that the X server's os/xsha1.c 'libsha1' branch expects.
 * Public-domain style; verified against the FIPS 180-1 test vectors.
 */
#ifndef FNX_LIBSHA1_H
#define FNX_LIBSHA1_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
	uint32_t state[5];
	uint64_t count;		/* bytes processed */
	uint8_t buffer[64];
} sha1_ctx;

void sha1_begin(sha1_ctx *ctx);
void sha1_hash(const void *data, size_t size, sha1_ctx *ctx);
void sha1_end(uint8_t digest[20], sha1_ctx *ctx);

#endif /* FNX_LIBSHA1_H */
