/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_codecs — the two units' shared surface. docs/design/foundation-plan.md, F12.
 *
 * The support unit imports ONLY <foundation/Foundation.h>, so this probe is also what proves the
 * compression API reached the umbrella. And the bytes are built on the OTHER side, which is the
 * claim worth testing here: a binding is exercised by data that did not come from the same file
 * as the code doing the work.
 */

#ifndef FOUNDATION_CODECS_H
#define FOUNDATION_CODECS_H

#import <foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/* A HIGHLY compressible buffer, and a big one: 100 KB of a repeating phrase compresses to a
 * couple of hundred bytes, so decompression has to grow its buffer many times over. That is the
 * only way the doubling loop gets exercised. */
NSData * _Nullable foundation_codecs_repetitive(void);

/* A buffer with no pattern at all — the case where compression does not help, and the round trip
 * still has to hold. */
NSData * _Nullable foundation_codecs_odd(void);

/* Bytes that are not a zlib stream, for the failure path. */
NSData * _Nullable foundation_codecs_garbage(void);

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_CODECS_H */
