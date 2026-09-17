/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSString — the immutable string, with its concrete and constant subclasses.
 * docs/design/foundation-plan.md, F1 + the hard rule.
 *
 * UTF-8 IS THE STORAGE (the user's decision, 2026-09-17) and `-length` counts
 * BYTES. `-characterCount` counts Unicode characters and `-characterAtIndex:`
 * indexes THOSE — the honest reading of the name, and a DOCUMENTED deviation
 * from Cocoa, whose `-length` counts UTF-16 code units. Everything that follows
 * from the UTF-16 boundary (-initWithCharacters:length:, -getCharacters:range:)
 * is therefore absent, deliberately, and recorded as such.
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

@interface NSString : NSObject <NSCopying>

/* Creation. The `init` family returns +1, as ARC decides BY NAME. */
+ (id)string;
+ (id)stringWithString:(NSString *)other;
+ (id)stringWithUTF8String:(const char *)utf8;
+ (id)stringWithFormat:(NSString *)format, ...;
+ (id)stringWithContentsOfFile:(NSString *)path
			  encoding:(NSStringEncoding)encoding
			     error:(NSError **)errorPtr;
+ (id)stringWithContentsOfFile:(NSString *)path
		  usedEncoding:(NSStringEncoding *)encoding
			 error:(NSError **)errorPtr;
+ (id)stringWithFormat:(NSString *)format arguments:(va_list)arguments;

- (id)init;
- (id)initWithString:(NSString *)other;
- (id)initWithUTF8String:(const char *)utf8;
- (id)initWithFormat:(NSString *)format, ...;
- (id)initWithFormat:(NSString *)format arguments:(va_list)arguments;
- (id)initWithData:(NSData *)data encoding:(NSStringEncoding)encoding;

/* The primitives every concrete subclass implements. */
- (const char *)UTF8String;
- (size_t)length;		/* BYTES — the documented deviation */
- (size_t)characterCount;	/* Unicode characters */
- (unsigned char)byteAtIndex:(size_t)index;
- (unsigned short)characterAtIndex:(size_t)index;	/* by CHARACTER */
- (size_t)lengthOfBytesUsingEncoding:(NSStringEncoding)encoding;
- (NSData *)dataUsingEncoding:(NSStringEncoding)encoding;
- (const char *)cStringUsingEncoding:(NSStringEncoding)encoding;
- (BOOL)writeToFile:(NSString *)path
	 atomically:(BOOL)useAuxiliaryFile
	   encoding:(NSStringEncoding)encoding
	      error:(NSError **)errorPtr;

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
			locale:(id)locale;
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
		  locale:(id)locale;

/* Case, substrings, appending and replacing. Case mapping is ASCII: this
 * Foundation is UTF-8 and makes no Unicode case claims. */
- (NSString *)uppercaseString;
- (NSString *)lowercaseString;
- (NSString *)uppercaseStringWithLocale:(id)locale;
- (NSString *)lowercaseStringWithLocale:(id)locale;
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
- (NSArray *)pathComponents;
- (NSString *)stringByStandardizingPath;
- (BOOL)isAbsolutePath;

@end

@interface NSOwnedString : NSString
{
	char *_bytes;		/* owned, NUL-terminated, UTF-8 */
	size_t _length;		/* bytes, excluding the terminating NUL */
}
+ (id)stringWithUTF8String:(const char *)utf8;
- (id)initWithUTF8String:(const char *)utf8;
- (id)initWithBytes:(const char *)bytes length:(size_t)length;
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

#endif /* FOUNDATION_NSSTRING_H */
