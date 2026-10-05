/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_dataoptions.m — THE PROBE FOR §62.97: NSData's four deprecated option spellings and the four
 * compression error codes, which close the last open rows in the binary-data family.
 *
 * WHAT IT ASSERTS, AND WHY EACH ONE IS WORTH THE CALLS IT COSTS:
 *   * THE SPELLINGS ARE THE CANONICAL VALUES, compared one by one: a deprecated name that resolved to a
 *     different bit would compile, read a file a different way, and be invisible to a check that only asked
 *     whether the read succeeded.
 *   * AND THEY REALLY OPEN THE DOORS THEY NAME — a file written with `NSAtomicWrite` is read back with
 *     `NSMappedRead` and `NSUncachedRead`, byte for byte. That is the half a value comparison cannot see: the
 *     options have to reach the reading path.
 *   * THE COMPRESSION CODES ARE APPLE'S NUMBERS, INCLUDING THE RANGE: 5376 for a failed compression, 5377 for
 *     a failed decompression, bracketed by Minimum and Maximum — which is the documented CONTENT of these
 *     constants, so the probe asserts the bracketing as hard as the values.
 *   * AND THE CODES ARE WIRED TO BEHAVIOUR, NOT MERELY DECLARED: asking for an algorithm this library has no
 *     codec for answers `NSCompressionFailedError`, and feeding garbage to the zlib decoder answers
 *     `NSDecompressionFailedError`. Before this unit both answered `code:1`, a number nothing declared — which
 *     is the failure a caller switching on an error (the documented way to use these doors) could not see.
 *   * THE ALGORITHM IS STILL NAMED in the error's description, because the fix must not cost the diagnostic
 *     that was already there.
 */

#import <Foundation/Foundation.h>
#include <stdio.h>
#include <string.h>

static int okc = 0, failc = 0;

static void check(const char *name, BOOL held, NSString *why)
{
	if (held) {
		okc++;
		printf("FOUNDATION-DATAOPTIONS %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-DATAOPTIONS %s FAIL: %s\n", name, [why UTF8String]);
	}
}

/* WHERE A FILE CAN BE WRITTEN IN BOTH WORLDS. `NSTemporaryDirectory()` answers the GUEST's FSH path
 * ("/System/Temporary Files/"), which does not exist on the host - a trap this tree has already recorded once -
 * so the probe ASKS whether that directory is there and falls back to the directory it was started in. A
 * file-backed check must not fail for a reason that is not the library's. */
static NSString *fn_scratch_path(NSString *name)
{
	NSFileManager *manager = [NSFileManager defaultManager];
	NSString *directory = NSTemporaryDirectory();

	if (directory == nil || ![manager fileExistsAtPath:directory]) {
		directory = [manager currentDirectoryPath];
	}
	if (directory == nil) {
		directory = @"/";
	}
	return [directory stringByAppendingPathComponent:name];
}

/* A CALLER'S FAILED CALL, asked the same way every time: the error (or nil) and the object that came back. */
static NSError *fn_compress_failure(NSData *data, BOOL decompress)
{
	NSError *error = nil;

	if (decompress) {
		[data decompressedDataUsingAlgorithm:NSDataCompressionAlgorithmZlib error:&error];
	} else {
		[data compressedDataUsingAlgorithm:NSDataCompressionAlgorithmLZFSE error:&error];
	}
	return error;
}

int main(void)
{
	/* 1. THE SPELLINGS, VALUE BY VALUE. */
	check("the-deprecated-spellings-are-the-canonical-values",
	      NSMappedRead == NSDataReadingMappedIfSafe && NSDataReadingMapped == NSDataReadingMappedIfSafe &&
	      NSUncachedRead == NSDataReadingUncached && NSAtomicWrite == NSDataWritingAtomic,
	      @"three reading names for the two reading options and one writing name for Atomic");

	/* 2. AND THEY REACH THE READING PATH. */
	{
		NSString *path = fn_scratch_path(@"fn_dataoptions.tmp");
		NSData *written = [@"deprecated spellings round trip"
				   dataUsingEncoding:NSUTF8StringEncoding];
		BOOL ok = [written writeToFile:path options:NSAtomicWrite error:NULL];
		NSData *mapped = [NSData dataWithContentsOfFile:path options:NSMappedRead error:NULL];
		NSData *uncached = [NSData dataWithContentsOfFile:path options:NSUncachedRead error:NULL];
		NSData *mappedAlways = [NSData dataWithContentsOfFile:path
							       options:NSDataReadingMapped error:NULL];

		[[NSFileManager defaultManager] removeItemAtPath:path error:NULL];

		check("the-deprecated-spellings-really-open-the-doors-they-name",
		      ok && mapped != nil && [mapped isEqualToData:written] &&
		      uncached != nil && [uncached isEqualToData:written] &&
		      mappedAlways != nil && [mappedAlways isEqualToData:written],
		      @"a file written with NSAtomicWrite reads back byte for byte through every deprecated "
		      @"reading spelling");
	}

	/* 3. THE CODES AND THE RANGE. */
	check("the-compression-error-codes-are-apples-and-the-range-brackets-them",
	      NSCompressionFailedError == 5376 && NSDecompressionFailedError == 5377 &&
	      NSCompressionErrorMinimum == 5376 && NSCompressionErrorMaximum == 5503 &&
	      NSCompressionErrorMinimum <= NSCompressionFailedError &&
	      NSCompressionFailedError <= NSCompressionErrorMaximum &&
	      NSCompressionErrorMinimum <= NSDecompressionFailedError &&
	      NSDecompressionFailedError <= NSCompressionErrorMaximum,
	      @"5376, 5377, and the reserved range they must sit inside");

	/* 4. WIRED TO BEHAVIOUR: a refusal is a COMPRESSION failure, and a bad stream a DECOMPRESSION one. */
	{
		NSError *compressionError = fn_compress_failure([@"anything" dataUsingEncoding:NSUTF8StringEncoding],
								NO);
		NSData *garbage = [NSData dataWithBytes:"not a zlib stream at all" length:24];
		NSError *decompressionError = fn_compress_failure(garbage, YES);

		check("a-refused-compression-and-a-failed-decompression-carry-their-own-codes",
		      compressionError != nil && [compressionError code] == NSCompressionFailedError &&
		      decompressionError != nil && [decompressionError code] == NSDecompressionFailedError,
		      [NSString stringWithFormat:@"compression=%ld decompression=%ld (wanted %ld and %ld)",
						  (long)[compressionError code],
						  (long)[decompressionError code],
						  (long)NSCompressionFailedError,
						  (long)NSDecompressionFailedError]);
	}

	/* 5. AND THE DIAGNOSTIC SURVIVED THE FIX: the algorithm is still named. */
	{
		NSError *error = fn_compress_failure([@"anything" dataUsingEncoding:NSUTF8StringEncoding], NO);
		NSString *description = [error localizedDescription];

		check("the-error-still-names-the-algorithm",
		      description != nil && [description rangeOfString:@"LZFSE"].location != NSNotFound,
		      [NSString stringWithFormat:@"the description must name the algorithm that was refused: %@",
						  description]);
	}

	printf("FOUNDATION-DATAOPTIONS RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-DATAOPTIONS-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-DATAOPTIONS DONE\n");
	return failc ? 1 : 0;
}
