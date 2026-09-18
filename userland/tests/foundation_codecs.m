/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_codecs, unit 2 of 2 — the checks (ARC). docs/design/foundation-plan.md, F12.
 *
 *   codec-round-trip  compress then decompress gives the bytes back
 *   codec-large       a 100 KB buffer whose compressed form is tiny, so decompression has to
 *                     grow its buffer many times — the doubling loop
 *   codec-zlib-stream the output IS a zlib stream: the 0x78 header byte is MEASURED, not assumed
 *   codec-empty       an empty buffer round-trips
 *   codec-odd         a buffer with no pattern round-trips (compression need not shrink it)
 *   codec-refusals    LZFSE/LZ4/LZMA answer nil AND an error that NAMES the algorithm
 *   codec-bad-input   bytes that are not a stream are refused, with an error and not a crash
 *   codec-truncated   a PREFIX of a valid stream is refused
 *   codec-mutable     the in-place forms — and a refusal leaves the receiver ALONE
 *   codec-null-error  a NULL error out-parameter still answers nil
 *   cross-tu          bytes built in the other unit compress here
 */

#import "foundation_codecs.h"
#include <stdio.h>
#include <string.h>

static int okc, failc;

static void check(const char *name, int ok, const char *detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-CODECS %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-CODECS %s FAIL %s\n", name, detail ? detail : "");
	}
}

/* THE DETAIL CARRIES THE MEASUREMENT: the length, and the FIRST TWO BYTES — because for a zlib
 * stream those bytes ARE the claim being tested (0x78, then the DEFLATE method in the low nibble). */
static const char *fn_why(NSData *data)
{
	static char why[160];
	const unsigned char *bytes = data != nil ? (const unsigned char *)[data bytes] : NULL;

	if (data == nil) {
		return "(nil)";
	}
	if (bytes == NULL || [data length] == 0) {
		snprintf(why, sizeof why, "length=%lu (no bytes)", (unsigned long)[data length]);
		return why;
	}
	snprintf(why, sizeof why, "length=%lu first=0x%02x%02x",
		 (unsigned long)[data length], bytes[0],
		 [data length] > 1 ? bytes[1] : 0);
	return why;
}

/* The message is asked for THE API'S WAY — -localizedDescription — instead of poking the
 * userInfo with a literal key: the key's VALUE is NSError's business, not the probe's. That is
 * not theoretical. This probe first looked up the literal "NSLocalizedDescriptionKey" and found
 * nothing, because the constant's value was "NSLocalizedDescription" — a fidelity bug in
 * nerror.m, found by this check and fixed there. */
static int fn_names(NSError *error, const char *what)
{
	NSString *message = error != nil ? [error localizedDescription] : nil;

	if (message == nil) {
		return 0;
	}
	return strstr([message UTF8String], what) != NULL;
}

static const char *fn_message(NSError *error)
{
	NSString *message = error != nil ? [error localizedDescription] : nil;

	return message != nil ? [message UTF8String] : "(no message)";
}

int main(void)
{
	{
		NSData *original = foundation_codecs_odd();
		NSError *error = nil;
		NSData *packed = nil;
		NSData *back = nil;

		if (original != nil) {
			packed = [original compressedDataUsingAlgorithm:NSDataCompressionAlgorithmZlib
								  error:&error];
			if (packed != nil) {
				back = [packed decompressedDataUsingAlgorithm:NSDataCompressionAlgorithmZlib
									error:&error];
			}
		}
		check("codec-round-trip",
		      original != nil && packed != nil && back != nil &&
		      [back isEqualToData:original] && [back length] == [original length],
		      fn_message(error));
	}

	{
		/* THE BIG ONE, AND THE REASON IT IS HERE: 100 KB of repetition compresses to a couple of
		 * hundred bytes, so decompressing it has to grow the output buffer MANY times. A codec
		 * that guessed one size and gave up would pass every small check and fail this one. */
		NSData *original = foundation_codecs_repetitive();
		NSError *error = nil;
		NSData *packed = nil;
		NSData *back = nil;

		if (original != nil) {
			packed = [original compressedDataUsingAlgorithm:NSDataCompressionAlgorithmZlib
								  error:&error];
			if (packed != nil) {
				back = [packed decompressedDataUsingAlgorithm:NSDataCompressionAlgorithmZlib
									error:&error];
			}
		}
		check("codec-large",
		      original != nil && packed != nil && back != nil &&
		      [back isEqualToData:original] &&
		      /* It must actually SHRINK: a "codec" that stored the bytes unchanged would pass the
		       * round trip and fail here. */
		      [packed length] < [original length] / 4,
		      fn_why(packed));
	}

	{
		/* THE STREAM ITSELF: `Zlib` in Cocoa means the zlib-WRAPPED format, whose first byte is
		 * 0x78 and whose second byte's low nibble is 8 (DEFLATE). Measured, not assumed. */
		NSData *original = foundation_codecs_repetitive();
		NSData *packed = original != nil
			? [original compressedDataUsingAlgorithm:NSDataCompressionAlgorithmZlib error:NULL]
			: nil;
		const unsigned char *bytes = packed != nil ? (const unsigned char *)[packed bytes] : NULL;
		int isZlib = 0;

		if (bytes != NULL && [packed length] >= 2) {
			/* THE METHOD LIVES IN THE FIRST BYTE, WHICH IS THE ONE I GOT WRONG FIRST: CMF packs
			 * the compression METHOD in its low nibble (8 = DEFLATE) and the window size in its
			 * high nibble, so 0x78 is exactly "DEFLATE, 32 KiB window". The SECOND byte is FLG
			 * — the level and the check bits, which is where the 0x9c after it comes from. The
			 * first version tested the low nibble of the WRONG byte and failed a stream that
			 * was perfect, which the detail (first=0x789c) said outright. */
			isZlib = (bytes[0] == 0x78 && (bytes[0] & 0x0f) == 8) ? 1 : 0;
		}
		check("codec-zlib-stream", isZlib == 1, fn_why(packed));
	}

	{
		NSData *empty = [NSData data];
		NSError *error = nil;
		NSData *packed = [empty compressedDataUsingAlgorithm:NSDataCompressionAlgorithmZlib
							       error:&error];
		NSData *back = packed != nil
			? [packed decompressedDataUsingAlgorithm:NSDataCompressionAlgorithmZlib error:&error]
			: nil;

		check("codec-empty",
		      empty != nil && packed != nil && back != nil && [back length] == 0,
		      fn_message(error));
	}

	{
		NSData *odd = foundation_codecs_odd();
		NSError *error = nil;
		NSData *packed = odd != nil
			? [odd compressedDataUsingAlgorithm:NSDataCompressionAlgorithmZlib error:&error] : nil;
		NSData *back = packed != nil
			? [packed decompressedDataUsingAlgorithm:NSDataCompressionAlgorithmZlib error:&error]
			: nil;

		/* NO CLAIM ABOUT SIZE: a buffer with no pattern may grow, and the round trip is the whole
		 * of what is promised. */
		check("codec-odd",
		      odd != nil && packed != nil && back != nil && [back isEqualToData:odd],
		      fn_why(packed));
	}

	{
		NSData *original = foundation_codecs_odd();
		NSError *lzfseError = nil;
		NSError *lz4Error = nil;
		NSError *lzmaError = nil;
		NSData *lzfse = nil;
		NSData *lz4 = nil;
		NSData *lzma = nil;

		if (original != nil) {
			lzfse = [original compressedDataUsingAlgorithm:NSDataCompressionAlgorithmLZFSE
								 error:&lzfseError];
			lz4 = [original compressedDataUsingAlgorithm:NSDataCompressionAlgorithmLZ4
							       error:&lz4Error];
			lzma = [original compressedDataUsingAlgorithm:NSDataCompressionAlgorithmLZMA
								error:&lzmaError];
		}
		/* THE REFUSALS, each answering nil AND an error that NAMES IT — because a caller has to
		 * be able to tell WHICH algorithm was refused, not merely that one was. */
		check("codec-refusals",
		      original != nil && lzfse == nil && lz4 == nil && lzma == nil &&
		      fn_names(lzfseError, "LZFSE") && fn_names(lz4Error, "LZ4") &&
		      fn_names(lzmaError, "LZMA"),
		      fn_message(lzfseError));
	}

	{
		NSData *garbage = foundation_codecs_garbage();
		NSError *error = nil;
		NSData *back = garbage != nil
			? [garbage decompressedDataUsingAlgorithm:NSDataCompressionAlgorithmZlib error:&error]
			: nil;

		check("codec-bad-input",
		      garbage != nil && back == nil && error != nil,
		      back == nil ? fn_message(error) : fn_why(back));
	}

	{
		/* A TRUNCATED STREAM: the first half of a valid one is not a valid one, and the codec has
		 * to say so rather than answering half the bytes. */
		NSData *original = foundation_codecs_repetitive();
		NSData *packed = original != nil
			? [original compressedDataUsingAlgorithm:NSDataCompressionAlgorithmZlib error:NULL]
			: nil;
		NSData *prefix = (packed != nil && [packed length] > 4)
			? [packed subdataWithRange:NSMakeRange(0, [packed length] / 2)] : nil;
		NSError *error = nil;
		NSData *back = prefix != nil
			? [prefix decompressedDataUsingAlgorithm:NSDataCompressionAlgorithmZlib error:&error]
			: nil;

		check("codec-truncated",
		      prefix != nil && back == nil && error != nil,
		      back == nil ? fn_message(error) : fn_why(back));
	}

	{
		NSData *original = foundation_codecs_repetitive();
		NSMutableData *mutable = [[NSMutableData alloc] init];
		NSError *error = nil;
		BOOL compressed = NO;
		BOOL decompressed = NO;
		BOOL refused = NO;
		NSData *kept = nil;

		if (original != nil) {
			[mutable setData:original];
			compressed = [mutable compressUsingAlgorithm:NSDataCompressionAlgorithmZlib
							       error:&error];
			if (compressed) {
				decompressed = [mutable decompressUsingAlgorithm:NSDataCompressionAlgorithmZlib
									   error:&error];
			}
			/* A REFUSED algorithm leaves the receiver ALONE: the header promises it, and
			 * "answered NO" on its own would pass for a receiver that had been emptied. */
			kept = [NSData dataWithData:mutable];
			refused = ![mutable compressUsingAlgorithm:NSDataCompressionAlgorithmLZMA
							     error:&error];
		}
		check("codec-mutable",
		      mutable != nil && compressed && decompressed &&
		      [mutable isEqualToData:original] &&
		      refused && [mutable isEqualToData:kept],
		      fn_why(mutable));
	}

	{
		NSData *original = foundation_codecs_odd();
		NSError *error = nil;
		NSData *packed = original != nil
			? [original compressedDataUsingAlgorithm:NSDataCompressionAlgorithmZlib error:NULL]
			: nil;
		NSData *back = packed != nil
			? [packed decompressedDataUsingAlgorithm:NSDataCompressionAlgorithmZlib error:NULL]
			: nil;

		/* NULL FOR "I DO NOT WANT AN ERROR", which the header documents: the answer is still
		 * nil, and still not a crash. */
		check("codec-null-error",
		      original != nil && packed != nil && back != nil && [back isEqualToData:original],
		      fn_why(packed));
		(void)error;
	}

	{
		NSData *theirs = foundation_codecs_repetitive();
		NSError *error = nil;
		NSData *packed = theirs != nil
			? [theirs compressedDataUsingAlgorithm:NSDataCompressionAlgorithmZlib error:&error]
			: nil;
		NSData *back = packed != nil
			? [packed decompressedDataUsingAlgorithm:NSDataCompressionAlgorithmZlib error:&error]
			: nil;

		check("cross-tu",
		      theirs != nil && packed != nil && back != nil && [back isEqualToData:theirs] &&
		      [packed length] < [theirs length] / 4,
		      fn_why(packed));
	}

	printf("FOUNDATION-CODECS RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-CODECS DONE\n");
	return failc ? 1 : 0;
}
