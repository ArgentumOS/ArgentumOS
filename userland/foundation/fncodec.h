/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * fncodec.h — the PRIVATE compression binding. docs/design/foundation-plan.md, F12.
 *
 * It is a header of its own for the same reason fnpredicate.h is: the CODEC is one concern and
 * the COLLECTION it is offered on is another. `ndata.m` owns NSData; this owns zlib, and it is
 * the only file in the library that includes <zlib.h> at all.
 *
 * BOTH FUNCTIONS ANSWER THE API'S OWN SHAPE: nil on a refusal or a codec failure, with the
 * reason in the error out-parameter when the caller asked for one. A caller that passes NULL
 * still gets the nil, which is the part that matters.
 */

#ifndef FOUNDATION_FNCODEC_H
#define FOUNDATION_FNCODEC_H

#import <foundation/NSData.h>

NS_ASSUME_NONNULL_BEGIN

NSData * _Nullable fn_compressed_data(NSData *data,
				      NSDataCompressionAlgorithm algorithm,
				      NSError * _Nullable * _Nullable errorPtr);
NSData * _Nullable fn_decompressed_data(NSData *data,
					NSDataCompressionAlgorithm algorithm,
					NSError * _Nullable * _Nullable errorPtr);

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_FNCODEC_H */
