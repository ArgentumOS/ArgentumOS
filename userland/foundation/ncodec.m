/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * ncodec.m — the compression binding. docs/design/foundation-plan.md, F12.
 *
 * MANUAL OWNERSHIP. It is the ONLY file in the library that includes <zlib.h>.
 *
 * WHY ZLIB AND NOT A DEFLATE OF OUR OWN: DEFLATE is a table of Huffman codes and a matching
 * window, so writing it here would be exactly the table this plan keeps refusing. `libz` already
 * ships in this system for the X11 stack and is already staged into the guest, so the honest move
 * is a BINDING — and the honest cost, recorded in the design, is that the library gains a
 * dependency on it.
 *
 * THE OTHER THREE ALGORITHMS ARE REFUSED BY NAME. `NSDataCompressionAlgorithm` carries Cocoa's
 * four names; exactly one of them has a codec here, and a caller who asks for LZFSE gets nil and
 * an error that says so, rather than a stream in some other format.
 *
 * DECOMPRESSION BOUNDS ITSELF. The output size is not in a zlib stream, so the buffer doubles on
 * Z_BUF_ERROR — and stops at a ceiling, because a small input claiming a huge output is the shape
 * of a bomb, and an unbounded loop is how one wins.
 */

#import <foundation/NSData.h>
#import <foundation/NSString.h>
#import <foundation/NSError.h>
#import <foundation/NSDictionary.h>	/* the userInfo literal below needs the class */
#import "fncodec.h"

#include <zlib.h>
#include <stdlib.h>
#include <string.h>

/* The ceiling for a decompressed result: 256 MiB. A stream that wants more is refused rather
 * than chased. */
#define FN_CODEC_MAX_OUTPUT ((uLongf)256 * 1024 * 1024)

static NSString *fn_codec_name(NSDataCompressionAlgorithm algorithm)
{
	if (algorithm == NSDataCompressionAlgorithmLZFSE) {
		return @"LZFSE";
	}
	if (algorithm == NSDataCompressionAlgorithmLZ4) {
		return @"LZ4";
	}
	if (algorithm == NSDataCompressionAlgorithmLZMA) {
		return @"LZMA";
	}
	return @"Zlib";
}

/* The reason NAMES the algorithm: a caller has to be able to see WHICH one was refused. */
static NSError *fn_codec_error(NSDataCompressionAlgorithm algorithm, NSString *why)
{
	NSString *message = [NSString stringWithFormat:@"%@: %@", fn_codec_name(algorithm), why];

	return [NSError errorWithDomain:@"NSCocoaErrorDomain"
				   code:1
			       userInfo:@{ NSLocalizedDescriptionKey: message }];
}

NSData *fn_compressed_data(NSData *data, NSDataCompressionAlgorithm algorithm,
			   NSError * _Nullable * _Nullable errorPtr)
{
	uLongf bound;
	unsigned char *out;
	int status;
	NSData *result;

	if (algorithm != NSDataCompressionAlgorithmZlib) {
		if (errorPtr != NULL) {
			*errorPtr = fn_codec_error(algorithm,
				@"no codec for this algorithm exists in this system");
		}
		return nil;
	}
	bound = compressBound((uLong)[data length]);
	out = (unsigned char *)malloc(bound == 0 ? 1 : (size_t)bound);
	if (out == NULL) {
		if (errorPtr != NULL) {
			*errorPtr = fn_codec_error(algorithm,
				@"the output buffer could not be allocated");
		}
		return nil;
	}
	status = compress2(out, &bound, (const Bytef *)[data bytes], (uLong)[data length],
			   Z_DEFAULT_COMPRESSION);
	if (status != Z_OK) {
		free(out);
		if (errorPtr != NULL) {
			*errorPtr = fn_codec_error(algorithm, @"the codec failed");
		}
		return nil;
	}
	result = [NSData dataWithBytes:out length:(size_t)bound];
	free(out);
	if (result == nil && errorPtr != NULL) {
		*errorPtr = fn_codec_error(algorithm, @"the result could not be built");
	}
	return result;
}

NSData *fn_decompressed_data(NSData *data, NSDataCompressionAlgorithm algorithm,
			     NSError * _Nullable * _Nullable errorPtr)
{
	uLongf capacity;
	unsigned char *out;
	int status;
	NSData *result;

	if (algorithm != NSDataCompressionAlgorithmZlib) {
		if (errorPtr != NULL) {
			*errorPtr = fn_codec_error(algorithm,
				@"no codec for this algorithm exists in this system");
		}
		return nil;
	}
	if ([data length] == 0) {
		/* An empty buffer is not a zlib stream. The codec would answer Z_DATA_ERROR; saying so
		 * here gives the same answer with a better message. */
		if (errorPtr != NULL) {
			*errorPtr = fn_codec_error(algorithm, @"there is nothing to decompress");
		}
		return nil;
	}
	capacity = (uLongf)([data length] * 2 + 64);
	for (;;) {
		out = (unsigned char *)malloc((size_t)capacity);
		if (out == NULL) {
			if (errorPtr != NULL) {
				*errorPtr = fn_codec_error(algorithm,
					@"the output buffer could not be allocated");
			}
			return nil;
		}
		status = uncompress(out, &capacity, (const Bytef *)[data bytes],
				    (uLong)[data length]);
		if (status == Z_OK) {
			break;			/* and `capacity` is the real output size */
		}
		free(out);
		if (status != Z_BUF_ERROR) {
			if (errorPtr != NULL) {
				*errorPtr = fn_codec_error(algorithm,
					@"this is not a zlib stream, or it is damaged");
			}
			return nil;
		}
		if (capacity >= FN_CODEC_MAX_OUTPUT) {
			if (errorPtr != NULL) {
				*errorPtr = fn_codec_error(algorithm,
					@"the stream wants more than this library will produce");
			}
			return nil;
		}
		capacity *= 2;
	}
	result = [NSData dataWithBytes:out length:(size_t)capacity];
	free(out);
	if (result == nil && errorPtr != NULL) {
		*errorPtr = fn_codec_error(algorithm, @"the result could not be built");
	}
	return result;
}
