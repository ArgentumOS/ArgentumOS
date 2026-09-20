/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSString — the immutable string, with its concrete and constant subclasses.
 * docs/design/foundation-plan.md, F1 + the hard rule.
 *
 * UTF-16 IS THE STORAGE (the user's decision, 2026-09-18, later): "If Apple is
 * using UTF-16, so should we." NSOwnedString holds UTF-16 code units, the
 * runtime's constant strings already ARE UTF-16 (the compiler emits every
 * non-ASCII literal that way, and `_rlength` is a unit count), and `-UTF8String`
 * is now a CONVERSION with a materialised cache rather than the storage door.
 * docs/design/foundation-plan.md §13 is the design, its measured cost (340
 * `-UTF8String` sites), and its slices.
 *
 * THE CONTRACT IS STILL THE OLD ONE FOR ONE MORE SLICE — `-length` answers BYTES
 * and `-characterAtIndex:` indexes Unicode scalars — because flipping it moves
 * ~600 call sites and the storage had to land first. §13.6 slice 2 flips it to
 * UTF-16 code units, which is Apple's contract and where this ends up.
 *
 * NSString HAS NO INSTANCE VARIABLES, AND THAT IS NOT AN OVERSIGHT. The
 * compiler emits `@"..."` as an object whose fields sit at FIXED offsets from
 * the object pointer — measured, and pinned by our runtime's own Test/Test.h,
 * whose NSConstantString subclasses a BARE root class rather than a
 * storage-carrying one. A subclass therefore cannot inherit storage: its own
 * ivars would be pushed past the offsets the runtime fills, and every constant
 * string would read foreign words. That is exactly how F1's first two attempts
 * failed (a SEGV on a message send to a literal), and why the storage lives in
 * the concrete subclasses here, as it does in Cocoa:
 *
 *   NSString            the contract and the value semantics (no ivars)
 *   NSOwnedString       a string that owns a UTF-8 buffer
 *   NSMutableString     NSOwnedString plus mutation
 *   NSConstantString    the compiler's layout, filled in by the runtime
 *
 * EVERY METHOD IS BUILT ON THE ACCESSORS (-length, -byteAtIndex:,
 * -characterCount, -characterAtIndex:, -UTF8String), never on a field: an
 * NSConstantString's storage is the runtime's, so a method that read
 * NSOwnedString's offsets on one would read foreign memory.
 */

#ifndef FOUNDATION_NSSTRING_H
#define FOUNDATION_NSSTRING_H

#import <foundation/NSObject.h>
#include <stddef.h>
#include <stdarg.h>		/* va_list, for -initWithFormat:arguments: */

/* The families that take or return other Foundation types: forward declarations
 * keep this header from dragging half the library in (and would deadlock the
 * imports if NSData or NSArray ever needed to come back here). */
@class NSData;
@class NSArray;
@class NSError;
@class NSCharacterSet;

/* NULLABILITY (F6, slice 2): NONNULL by default, and the exceptions are MEASURED —
 * every one below is a method nstring.m actually answers nil or NULL from (awk
 * over its `return nil;`/`return NULL;` sites), not a guess from the name:
 *   -stringWithUTF8String: and -initWithUTF8String: (a NULL or invalid argument),
 *   -initWithData:encoding: (data that is not valid in that encoding),
 *   +stringWithContentsOfFile:... (a file that did not read),
 *   -dataUsingEncoding: and -cStringUsingEncoding: (unconvertible), and
 *   -pathComponents (measured: an empty path answers nil here, where Cocoa
 *   answers an empty array).
 * The rest of the file RAISES rather than answering an absent thing. The locale
 * parameters are nullable because the header already says what a nil locale
 * means ("a nil locale means 'no locale-sensitive rules'"), and an NSError**
 * out-parameter is nullable at BOTH levels: the caller may pass NULL for
 * "no error report". */
NS_ASSUME_NONNULL_BEGIN


/* Cocoa's option set, reduced to the ones a UTF-8 byte comparison can honour. */
typedef enum {
	NSLiteralSearch = 0,
	NSCaseInsensitiveSearch = 1
} NSStringCompareOptions;

/*
 * Cocoa's encodings, and the raw values are Cocoa's so a numeric comparison in
 * existing code still means the same thing. Only the UTF-8 one is SUPPORTED here
 * (it is the storage); the others are named so the calls compile and can be
 * rejected honestly rather than silently mis-encoded.
 */
typedef enum {
	NSASCIIStringEncoding = 1,
	NSUTF8StringEncoding = 4,
	NSUnicodeStringEncoding = 10
} NSStringEncoding;

/* THE TWO OPTION SETS THE WINDOWS TAKE (2026-09-20). The doors that use them — a
 * lossy encoding conversion, and -enumerateSubstringsInRange:options:usingBlock: —
 * are not in this header, so the types ship ahead of their users, as in Cocoa's own
 * header. Names from Apple's documentation index; values are ours (§11.6.1 D2, see
 * NSFileManager.h), and both are bit sets as their names say. */
typedef enum {
	NSStringEncodingConversionAllowLossy = 1 << 0,
	NSStringEncodingConversionExternalRepresentation = 1 << 1
} NSStringEncodingConversionOptions;

typedef enum {
	NSStringEnumerationByLines = 1 << 0,
	NSStringEnumerationByParagraphs = 1 << 1,
	NSStringEnumerationByComposedCharacterSequences = 1 << 2,
	NSStringEnumerationByWords = 1 << 3,
	NSStringEnumerationBySentences = 1 << 4,
	NSStringEnumerationReverse = 1 << 5,
	NSStringEnumerationSubstringNotRequired = 1 << 6,
	NSStringEnumerationLocalized = 1 << 7,
	NSStringEnumerationByCaretPositions = 1 << 8,
	NSStringEnumerationByDeletionClusters = 1 << 9
} NSStringEnumerationOptions;

@interface NSString : NSObject <NSCopying>

/* Creation. The `init` family returns +1, as ARC decides BY NAME. */
+ (id)string;
+ (id)stringWithString:(NSString *)other;
+ (nullable id)stringWithUTF8String:(const char * _Nullable)utf8;
+ (id)stringWithFormat:(NSString *)format, ...;
+ (nullable id)stringWithContentsOfFile:(NSString *)path
			  encoding:(NSStringEncoding)encoding
			     error:(NSError * _Nullable * _Nullable)errorPtr;
+ (nullable id)stringWithContentsOfFile:(NSString *)path
		  usedEncoding:(NSStringEncoding *)encoding
			 error:(NSError * _Nullable * _Nullable)errorPtr;
+ (id)stringWithFormat:(NSString *)format arguments:(va_list)arguments;

- (id)init;
- (id)initWithString:(NSString *)other;
- (nullable id)initWithUTF8String:(const char * _Nullable)utf8;
- (id)initWithFormat:(NSString *)format, ...;
- (id)initWithFormat:(NSString *)format arguments:(va_list)arguments;
- (nullable id)initWithData:(NSData *)data encoding:(NSStringEncoding)encoding;

/* The primitives every concrete subclass implements. */
- (const char *)UTF8String;
- (size_t)length;		/* UTF-16 CODE UNITS — Apple's contract (W1 slice 3) */
- (size_t)characterCount;	/* Unicode characters — an ADDITION: Cocoa has no such method */
- (unsigned char)byteAtIndex:(size_t)index;		/* a UTF-8 BYTE — the house door */
- (unsigned short)characterAtIndex:(size_t)index;	/* the UNIT at a unit index */

/*
 * THE four `…Characters:` FORMS (W1 slice 4) — the rows the unit CLOSES. They take
 * and give exactly the units the storage holds, so none of them converts anything:
 * a copy in, a copy out, and (in the no-copy form) ownership handed over.
 *
 * `-initWithCharactersNoCopy:length:freeWhenDone:` is Apple's ownership contract:
 * with YES the receiver owns the buffer and frees it; with NO it borrows it, never
 * writes it, and never frees it — and a MUTATION on such a string takes a copy
 * first, so a borrowed buffer is never written even through NSMutableString.
 */
+ (id)stringWithCharacters:(const unichar * _Nullable)characters length:(NSUInteger)length;
- (id)initWithCharacters:(const unichar * _Nullable)characters length:(NSUInteger)length;
- (id)initWithCharactersNoCopy:(unichar * _Nullable)characters length:(NSUInteger)length
		  freeWhenDone:(BOOL)freeBuffer;
- (void)getCharacters:(unichar *)buffer range:(NSRange)range;
- (size_t)lengthOfBytesUsingEncoding:(NSStringEncoding)encoding;
- (nullable NSData *)dataUsingEncoding:(NSStringEncoding)encoding;
- (nullable const char *)cStringUsingEncoding:(NSStringEncoding)encoding;
- (BOOL)writeToFile:(NSString *)path
	 atomically:(BOOL)useAuxiliaryFile
	   encoding:(NSStringEncoding)encoding
	      error:(NSError * _Nullable * _Nullable)errorPtr;

/* Value semantics. */
- (BOOL)isEqualToString:(NSString *)other;
- (NSComparisonResult)compare:(NSString *)other;
- (NSComparisonResult)caseInsensitiveCompare:(NSString *)other;
- (NSComparisonResult)compare:(NSString *)other
		      options:(NSStringCompareOptions)options;
- (NSComparisonResult)compare:(NSString *)other
		      options:(NSStringCompareOptions)options
			range:(NSRange)range;
- (NSComparisonResult)compare:(NSString *)other
		      options:(NSStringCompareOptions)options
			range:(NSRange)range
			locale:(nullable id)locale;
/* The localised forms. A locale is HONOURED FOR CASE and for nothing else here.
 * The Turkic languages (tr, az) pair i with İ and I with ı — Unicode's
 * SpecialCasing makes their mapping conditional — so a Turkic locale changes the
 * case-insensitive result through the locale-taking form, and
 * -localizedCaseInsensitiveCompare: asks +[NSLocale currentLocale]. ORDERING is
 * NOT localised (no collation tables ship: the order is byte order), a nil locale
 * means "no locale-sensitive rules", and -rangeOfString:…locale: ignores it
 * because a search answers a RANGE and the Turkic fold changes lengths. */
- (NSComparisonResult)localizedCompare:(NSString *)other;
- (NSComparisonResult)localizedCaseInsensitiveCompare:(NSString *)other;
- (BOOL)hasPrefix:(NSString *)prefix;
- (BOOL)hasSuffix:(NSString *)suffix;
- (BOOL)containsString:(NSString *)substring;
- (NSRange)rangeOfString:(NSString *)substring;
- (NSRange)rangeOfString:(NSString *)substring options:(NSStringCompareOptions)options;
- (NSRange)rangeOfString:(NSString *)substring
		 options:(NSStringCompareOptions)options
		   range:(NSRange)range;
- (NSRange)rangeOfString:(NSString *)substring
		 options:(NSStringCompareOptions)options
		   range:(NSRange)range
		  locale:(nullable id)locale;

/* Case, substrings, appending and replacing. The BASE mapping is ASCII — this
 * Foundation is UTF-8 and makes no general Unicode case claims — and the two
 * locale-taking forms below add the one locale rule that does ship: the Turkic
 * pairing of i with İ and I with ı. */
- (NSString *)uppercaseString;
- (NSString *)lowercaseString;
- (NSString *)uppercaseStringWithLocale:(nullable id)locale;
- (NSString *)lowercaseStringWithLocale:(nullable id)locale;
- (NSString *)capitalizedString;
- (NSString *)substringFromIndex:(NSUInteger)index;
- (NSString *)substringToIndex:(NSUInteger)index;
- (NSString *)substringWithRange:(NSRange)range;
- (NSString *)stringByAppendingString:(NSString *)other;
- (NSString *)stringByAppendingPathExtension:(NSString *)extension;
- (NSString *)stringByPaddingToLength:(NSUInteger)newLength
			 withString:(NSString *)pad
		    startingAtIndex:(NSUInteger)index;
- (NSString *)stringByAppendingFormat:(NSString *)format, ...;
- (NSString *)stringByReplacingOccurrencesOfString:(NSString *)target
					 withString:(NSString *)replacement;
- (NSString *)stringByReplacingOccurrencesOfString:(NSString *)target
					 withString:(NSString *)replacement
					    options:(NSStringCompareOptions)options
					      range:(NSRange)range;
- (NSArray *)componentsSeparatedByString:(NSString *)separator;
- (NSRange)rangeOfCharacterFromSet:(NSCharacterSet *)set;
- (NSArray *)componentsSeparatedByCharactersInSet:(NSCharacterSet *)set;
- (NSString *)stringByTrimmingCharactersInSet:(NSCharacterSet *)set;
- (NSString *)stringByReplacingCharactersInRange:(NSRange)range withString:(NSString *)replacement;

/* Conversions. */
- (int)intValue;
- (NSInteger)integerValue;
- (long long)longLongValue;
- (float)floatValue;
- (double)doubleValue;
- (BOOL)boolValue;

/* Paths — pure string operations, and FSH-shaped paths are slash-separated. */
- (NSString *)lastPathComponent;
- (NSString *)pathExtension;
- (NSString *)stringByDeletingLastPathComponent;
- (NSString *)stringByDeletingPathExtension;
- (NSString *)stringByAppendingPathComponent:(NSString *)component;
- (nullable NSArray *)pathComponents;
- (NSString *)stringByStandardizingPath;
- (BOOL)isAbsolutePath;

@end

@interface NSOwnedString : NSString
{
	unsigned short *_units;	/* NOT NUL-terminated: UTF-16 CODE UNITS */
	size_t _length;		/* UNITS — the storage's own count */
	char *_utf8;		/* LAZY: the materialised UTF-8 form, owned, NUL-terminated */
	size_t _utf8size;	/* its byte count, excluding the terminating NUL */
	unsigned char _ownsUnits;	/* 0 only for -initWithCharactersNoCopy:…:freeWhenDone:NO */
}
+ (nullable id)stringWithUTF8String:(const char * _Nullable)utf8;
- (nullable id)initWithUTF8String:(const char * _Nullable)utf8;
- (nullable id)initWithBytes:(const char *)bytes length:(size_t)length;
- (id)initWithCharacters:(const unichar * _Nullable)characters length:(NSUInteger)length;
- (id)initWithCharactersNoCopy:(unichar * _Nullable)characters length:(NSUInteger)length
		  freeWhenDone:(BOOL)freeBuffer;
@end

@interface NSMutableString : NSOwnedString <NSMutableCopying>
+ (id)string;
+ (id)stringWithCapacity:(NSUInteger)capacity;
- (id)initWithCapacity:(NSUInteger)capacity;
- (void)setString:(NSString *)other;
- (void)appendString:(NSString *)other;
- (void)appendUTF8String:(const char *)utf8;
- (void)appendFormat:(NSString *)format, ...;
- (void)insertString:(NSString *)string atIndex:(NSUInteger)index;
- (void)deleteCharactersInRange:(NSRange)range;
- (void)replaceCharactersInRange:(NSRange)range withString:(NSString *)string;
- (NSUInteger)replaceOccurrencesOfString:(NSString *)target
			      withString:(NSString *)replacement
				 options:(NSStringCompareOptions)options
				   range:(NSRange)range;
@end

/*
 * NSConstantString — the class `@"..."` names, pinned by
 * `-fconstant-string-class=NSConstantString` in tools/musl-clang-objc64.sh.
 *
 * THE IVARS ARE THE RUNTIME'S. The compiler emits these objects and the runtime
 * fills them in, so the layout is fixed by our runtime's own Test/Test.h
 * (NEW_ABI): flags, length (UTF-16 code units), size (BYTES), hash, then a
 * POINTER to the characters — not trailing character data, which is what
 * loader.c's older `struct nsstr` comment suggests.
 *
 * `flags`' LOW TWO BITS ARE THE ENCODING (loader.c: 0 ASCII, 1 UTF-8, 2 UTF-16,
 * 3 UTF-32), and clang uses UTF-16 for EVERY non-ASCII literal — its
 * GenerateConstantString says "all non-ASCII strings are represented as UTF-16".
 * So the character data is not always UTF-8, and nstring.m CONVERTS a UTF-16
 * constant rather than reinterpreting it. An all-ASCII literal cannot notice the
 * difference (there length == size), which is why this went unseen until a
 * non-ASCII literal was asserted byte by byte.
 *
 * Instances are marked permanent by the runtime (class_table.c special-cases
 * this name), so they take no part in reference counting.
 */
@interface NSConstantString : NSString
{
	unsigned int _rflags;
	unsigned int _rlength;	/* UTF-16 code units */
	unsigned int _rsize;	/* bytes */
	unsigned int _rhash;
	const char * const _rstr;
}
@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSSTRING_H */
