/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSData / NSMutableData — an owned byte buffer.
 * docs/design/foundation-plan.md, F2.
 *
 * The buffer is the object's own: `-initWithBytes:length:` copies, so a caller
 * can hand over a stack array and walk away. `-copy` of a MUTABLE data is a
 * snapshot, the same rule the strings follow.
 */

#ifndef FOUNDATION_NSDATA_H
#define FOUNDATION_NSDATA_H

#import <Foundation/NSObject.h>
#include <stddef.h>

/* Forward-declared for the :options:error: forms; a pointer is all they need. */
@class NSError;

/* NULLABILITY (F6, slice 3): NONNULL by default, and the exceptions are MEASURED
 * at the writer, in three classes:
 *   1. ndata.m's `return nil;` sites — SEVEN constructors, after D4 of §11.6.1 removed the
 *      eighth (the byte/no-copy forms, the file and base64 forms, and the mutable
 *      capacity/length ones) — and every one of those seven is Apple's OWN nullability
 *      (a missing file, an undecodable base64 string, an exhaustive no-copy form);
 *   2. PROPAGATION — every factory is `return [[self alloc] initWith...]` for one
 *      of those, so it inherits the same nullability. +data: and +dataWithBytes:length:
 *      are NOT nullable: the first mallocs nothing, and the second now RAISES on an
 *      unsatisfiable allocation rather than answering nil, because Apple declares both
 *      NONNULL and a nullable here is a difference a consumer sees;
 *   3. the two buffer accessors, -bytes and -mutableBytes: the ivar's own comment
 *      is "NULL only while empty" — an empty buffer is not a usable pointer.
 * The :options:error: forms take the error out-parameter at BOTH levels, because a
 * caller may pass NULL for "no error report".
 *
 * THE BYTE PARAMETERS OF THE CONSTRUCTORS ARE NULLABLE — a truth the 2026-09-18 sweep made
 * explicit. `-initWithBytes:NULL length:0` is exactly how an EMPTY data is built, and -bytes
 * answers NULL for that same object; the declaration said nonnull, so the library's own empty
 * constructors were warnings against their own header. The MUTATORS (-appendBytes:length: and
 * the -replaceBytesInRange: forms) stay NONNULL, because a NULL buffer there is meaningless. */
@class NSURL;

#import <Foundation/NSCoding.h>

NS_ASSUME_NONNULL_BEGIN

/* NSSecureCoding IS HERE BECAUSE APPLE'S NSData CONFORMS TO IT (§62.23): NSItemProvider's completion handler is
 * typed `__kindof id<NSSecureCoding>` in Apple's own declaration, and a class that conforms on Apple's platform and
 * not here is a difference — a caller passing NSData got a type warning about this library's gap. The ENFORCEMENT
 * is the coder's and is still absent; NSCoding.h names that debt, and the other collections' conformances are the
 * same sweep, owed rather than done. */
@interface NSData : NSObject <NSCopying, NSCoding, NSSecureCoding>
{
	unsigned char *_bytes;		/* owned; NULL only while empty */
	size_t _length;
}

/* Cocoa's option set for the ENCODING methods. It is one of TWO types, and that is
 * a correction (2026-09-20): the DECODING flag used to live here, which meant this
 * header had one option set where Cocoa has two, and the two decoding methods
 * below took the encoding type. Both are Apple's; the split is the fidelity. */
typedef enum {
	NSDataBase64EncodingDefault = 0,
	NSDataBase64Encoding64CharacterLineLength = 1,
	NSDataBase64EncodingEndLineWithLineFeed = 2,
	/* THE NEXT TWO TAKE FREE BITS ON PURPOSE, AND THE REASON IS WORTH READING: this set already assigned 1 and
	 * 2 to its own members, so copying Apple's numbers here would COLLIDE with them - a collision that would
	 * make one of the four options unrequestable in silence. Values are ours (§11.6.1 D2) and the check asserts
	 * the four are distinct. */
	NSDataBase64Encoding76CharacterLineLength = 4,
	NSDataBase64EncodingEndLineWithCarriageReturn = 8
} NSDataBase64EncodingOptions;

/* The DECODING option set, which is where NSDataBase64DecodingIgnoreUnknownCharacters
 * belongs. The VALUE is ours (§11.6.1 D2 — Apple publishes the name and not the
 * number) and it keeps the 4 this library already used, so nothing behavioural
 * moves: only the type the flag is declared under. */
typedef enum {
	NSDataBase64DecodingDefault = 0,
	NSDataBase64DecodingIgnoreUnknownCharacters = 4
} NSDataBase64DecodingOptions;

/* A DIFFERENT option set: -rangeOfData:options:range: searches, it does not
 * decode. Only the plain forward, unanchored search is implemented, and the
 * header says so rather than pretending otherwise. */
typedef enum {
	NSDataSearchDefault = 0,
	NSDataSearchBackwards = 1,
	NSDataSearchAnchored = 2
} NSDataSearchOptions;

/* Reading and writing options. Only the flags we honour are defined; the rest of
 * Cocoa's set is absent rather than silently accepted. */
typedef enum {
	NSDataReadingDefault = 0,
	NSDataReadingMappedIfSafe = 1,
	NSDataReadingUncached = 2,
	NSDataReadingMappedAlways = 4,

	/* THE THREE DEPRECATED SPELLINGS, which are names for the values above rather than new options — so a
	 * program written against the old ones reads exactly what a modern one reads (§62.97). Apple declares them
	 * as deprecated aliases; this library's availability macros are inert (foundation-plan.md row D12), so the
	 * record is this comment, and the deprecation ground itself was retired on 2026-09-26 (§62.24, row D7). */
	NSMappedRead = NSDataReadingMappedIfSafe,
	NSDataReadingMapped = NSDataReadingMappedIfSafe,
	NSUncachedRead = NSDataReadingUncached
} NSDataReadingOptions;

typedef enum {
	NSDataWritingDefault = 0,
	NSDataWritingAtomic = 1,
	/* THE FILE-PROTECTION VALUES ARE A MASK-VALUED SET, WHICH IS WHY Mask IS A REAL MASK: AND-ing it with a
	 * value has to extract the protection, so the values are consecutive and Mask covers exactly them. Nothing
	 * in this system acts on file protection - it is named so a caller compiles and the header says the rest. */
	NSDataWritingFileProtectionNone = 0,
	NSDataWritingFileProtectionComplete = 1,
	NSDataWritingFileProtectionCompleteUnlessOpen = 2,
	NSDataWritingFileProtectionCompleteUntilFirstUserAuthentication = 3,
	NSDataWritingFileProtectionCompleteWhenUserInactive = 4,
	NSDataWritingFileProtectionMask = 0xff,
	NSDataWritingWithoutOverwriting = 1 << 12,

	/* THE DEPRECATED SPELLING OF Atomic, for the same reason the reading enum gives (§62.97). */
	NSAtomicWrite = NSDataWritingAtomic
} NSDataWritingOptions;

/*
 * THE COMPRESSION CODECS (F12). Cocoa's four names, and ONE of them is implemented — an
 * algorithm this library has no codec for is refused with an NSError that names it, rather than
 * quietly doing something else.
 */
typedef enum {
	NSDataCompressionAlgorithmLZFSE = 0,
	NSDataCompressionAlgorithmLZ4,
	NSDataCompressionAlgorithmLZMA,
	NSDataCompressionAlgorithmZlib
} NSDataCompressionAlgorithm;

/* THE ERROR CODES THE COMPRESSION DOORS ANSWER WITH (§62.97) — Apple's numbers, from its own header, which is
 * the same source the option bits above came from, and the reason they are declared rather than chosen: a
 * caller that switches on an error's code is reading a number, not a name. THE RANGE IS THE DOCUMENTED
 * CONTENT: the two failures sit inside [Minimum, Maximum], which is the window Apple reserves so that a
 * compression failure can never be confused with another Cocoa error, and the probe asserts the bracketing
 * as hard as the values. `NSDataCodec`'s error path uses them, so the codes are wired to behaviour rather
 * than merely declared. */
enum {
	NSCompressionFailedError = 5376,	/* compressing failed - and also what an algorithm with no codec answers */
	NSDecompressionFailedError = 5377,	/* decompressing failed */
	NSCompressionErrorMinimum = 5376,
	NSCompressionErrorMaximum = 5503
};

+ (NSData *)data;
+ (NSData *)dataWithBytes:(const void * _Nullable)bytes length:(size_t)length;
+ (nullable NSData *)dataWithBytesNoCopy:(void * _Nullable)bytes length:(size_t)length;
+ (nullable NSData *)dataWithBytesNoCopy:(void * _Nullable)bytes length:(size_t)length freeWhenDone:(BOOL)freeWhenDone;
+ (NSData *)dataWithData:(NSData *)other;
+ (nullable NSData *)dataWithContentsOfFile:(NSString *)path;
+ (nullable NSData *)dataWithContentsOfFile:(NSString *)path options:(NSDataReadingOptions)options error:(NSError * _Nullable * _Nullable)errorPtr;
+ (nullable NSData *)dataWithBase64EncodedString:(NSString *)string;
+ (nullable NSData *)dataWithBase64EncodedString:(NSString *)string options:(NSDataBase64DecodingOptions)options;

/* THE URL FORMS (D7's kind (D)). Apple's nullability: the READING forms are nullable (a URL can
 * fail), the WRITING forms answer BOOL with an error out-parameter. */
+ (nullable NSData *)dataWithContentsOfURL:(NSURL *)url;
+ (nullable NSData *)dataWithContentsOfURL:(NSURL *)url options:(NSDataReadingOptions)options error:(NSError * _Nullable * _Nullable)errorPtr;
- (nullable id)initWithContentsOfURL:(NSURL *)url;
- (nullable id)initWithContentsOfURL:(NSURL *)url options:(NSDataReadingOptions)options error:(NSError * _Nullable * _Nullable)errorPtr;
- (BOOL)writeToURL:(NSURL *)url atomically:(BOOL)useAuxiliaryFile;
- (BOOL)writeToURL:(NSURL *)url options:(NSDataWritingOptions)options error:(NSError * _Nullable * _Nullable)errorPtr;

- (id)initWithBytes:(const void * _Nullable)bytes length:(size_t)length;
- (nullable id)initWithBytesNoCopy:(void * _Nullable)bytes length:(size_t)length;
- (nullable id)initWithBytesNoCopy:(void * _Nullable)bytes length:(size_t)length freeWhenDone:(BOOL)freeWhenDone;
- (id)initWithData:(NSData *)other;
- (nullable id)initWithContentsOfFile:(NSString *)path;
- (nullable id)initWithContentsOfFile:(NSString *)path options:(NSDataReadingOptions)options error:(NSError * _Nullable * _Nullable)errorPtr;
- (nullable id)initWithBase64EncodedString:(NSString *)string
			  options:(NSDataBase64DecodingOptions)options;
- (nullable id)initWithBase64EncodedData:(NSData *)base64Data options:(NSDataBase64DecodingOptions)options;

- (size_t)length;
- (nullable const void *)bytes;
- (void)getBytes:(void *)buffer length:(size_t)length;
- (void)getBytes:(void *)buffer range:(NSRange)range;
- (NSData *)subdataWithRange:(NSRange)range;
- (NSRange)rangeOfData:(NSData *)other
	       options:(NSDataSearchOptions)options
		 range:(NSRange)range;

- (NSString *)base64EncodedStringWithOptions:(NSDataBase64EncodingOptions)options;
- (NSData *)base64EncodedDataWithOptions:(NSDataBase64EncodingOptions)options;
- (BOOL)writeToFile:(NSString *)path atomically:(BOOL)useAuxiliaryFile;
- (BOOL)writeToFile:(NSString *)path options:(NSDataWritingOptions)options error:(NSError * _Nullable * _Nullable)errorPtr;
- (void)enumerateByteRangesUsingBlock:(void (^)(const void *bytes, NSRange byteRange, BOOL *stop))block;

/*
 * THE COMPRESSION FORMS (F12). This is a BINDING and not a rule: DEFLATE is a table of Huffman
 * codes, so it comes from `libz` — which this system already ships for the X11 stack — instead
 * of being written out here. `NSDataCompressionAlgorithmZlib` produces a ZLIB-WRAPPED stream
 * (the one whose header byte is `0x78`), which is what Cocoa's `.zlib` means.
 *
 * A REFUSED algorithm answers nil and fills the error, naming the algorithm: that is the API's
 * own way of saying no. The three refusals (`LZFSE`, `LZ4`, `LZMA`) each say which they are and
 * that no codec for them exists in this system.
 */
- (nullable NSData *)compressedDataUsingAlgorithm:(NSDataCompressionAlgorithm)algorithm
					    error:(NSError * _Nullable * _Nullable)errorPtr;
- (nullable NSData *)decompressedDataUsingAlgorithm:(NSDataCompressionAlgorithm)algorithm
					      error:(NSError * _Nullable * _Nullable)errorPtr;

- (BOOL)isEqualToData:(NSData *)other;

@end

@interface NSMutableData : NSData <NSMutableCopying>
{
	size_t _capacity;
}

+ (nullable NSMutableData *)dataWithCapacity:(size_t)capacity;
+ (nullable NSMutableData *)dataWithLength:(size_t)length;
- (nullable id)initWithCapacity:(size_t)capacity;
- (nullable id)initWithLength:(size_t)length;

- (void)appendBytes:(const void *)bytes length:(size_t)length;
- (void)appendData:(NSData *)other;
- (void)setLength:(size_t)length;
- (void)increaseLengthBy:(size_t)extraLength;
- (nullable void *)mutableBytes;
- (void)replaceBytesInRange:(NSRange)range withBytes:(const void *)bytes;
- (void)replaceBytesInRange:(NSRange)range
		  withBytes:(const void *)bytes
		     length:(size_t)replacementLength;
- (void)resetBytesInRange:(NSRange)range;
- (void)setData:(NSData *)other;

/* In place, which is what MUTABLE means here: the receiver BECOMES the result. NO on a refusal
 * or a codec failure, with the reason in the error — and the receiver is left ALONE either way,
 * because a half-replaced buffer is worse than none. */
- (BOOL)compressUsingAlgorithm:(NSDataCompressionAlgorithm)algorithm
			 error:(NSError * _Nullable * _Nullable)errorPtr;
- (BOOL)decompressUsingAlgorithm:(NSDataCompressionAlgorithm)algorithm
			   error:(NSError * _Nullable * _Nullable)errorPtr;

NS_ASSUME_NONNULL_END

@end

/*
 * THE PRIVATE HALF, folded in from fncodec.h: the declarations this library shares internally.
 * They are HERE because the public headers are now the only headers - what used to be a
 * private file two units imported is a section of the class's own header. The region is its
 * own one only when this point in the header is outside the header's own (a nested region
 * does not compile, and neither does an unclosed one).
 */
NS_ASSUME_NONNULL_BEGIN
NSData * _Nullable fn_compressed_data(NSData *data,
				      NSDataCompressionAlgorithm algorithm,
				      NSError * _Nullable * _Nullable errorPtr);
NSData * _Nullable fn_decompressed_data(NSData *data,
					NSDataCompressionAlgorithm algorithm,
					NSError * _Nullable * _Nullable errorPtr);
NS_ASSUME_NONNULL_END


#endif /* FOUNDATION_NSDATA_H */
