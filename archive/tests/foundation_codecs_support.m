/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_codecs, unit 1 of 2 — the fixtures (MRR). docs/design/foundation-plan.md, F12.
 *
 * The buffers are built HERE so the check unit works on bytes it did not create. Note what is
 * MISSING: no block literals at all, capturing or otherwise — this unit is `-fno-objc-arc`, and
 * the plan's §6 records what a capturing block built in a support unit does.
 */

#import "foundation_codecs.h"

#include <stdlib.h>

#define FN_REPETITIVE_LENGTH (100 * 1024)

NSData * _Nullable foundation_codecs_repetitive(void)
{
	char *bytes = (char *)malloc(FN_REPETITIVE_LENGTH);
	NSUInteger i;
	NSData *result;

	if (bytes == NULL) {
		return nil;
	}
	for (i = 0; i < FN_REPETITIVE_LENGTH; i++) {
		bytes[i] = (char)('a' + (i % 8));
	}
	result = [NSData dataWithBytes:bytes length:FN_REPETITIVE_LENGTH];
	free(bytes);
	return result;
}

NSData * _Nullable foundation_codecs_odd(void)
{
	unsigned char bytes[512];
	unsigned int state = 0x12345678u;
	NSUInteger i;

	for (i = 0; i < sizeof bytes; i++) {
		state = state * 1103515245u + 12345u;
		bytes[i] = (unsigned char)(state >> 16);
	}
	return [NSData dataWithBytes:bytes length:sizeof bytes];
}

NSData * _Nullable foundation_codecs_garbage(void)
{
	static const char text[] = "not a zlib stream at all, not even close";

	return [NSData dataWithBytes:text length:sizeof text - 1];
}
