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

#import <foundation/NSObject.h>
#include <stddef.h>

/* Forward-declared for the :options:error: forms; a pointer is all they need. */
@class NSError;

/* NULLABILITY (F6, slice 3): NONNULL by default, and the exceptions are MEASURED
 * at the writer, in three classes:
 *   1. ndata.m's `return nil;` sites — eight constructors (the byte/no-copy forms,
 *      the file and base64 forms, and the mutable capacity/length ones);
 *   2. PROPAGATION — every factory is `return [[self alloc] initWith...]` for one
 *      of those, so it inherits the same nullability (that is why +data: is
 *      nullable too: it goes through -initWithBytes:NULL length:0);
 *   3. the two buffer accessors, -bytes and -mutableBytes: the ivar's own comment
 *      is "NULL only while empty" — an empty buffer is not a usable pointer.
 * The :options:error: forms take the error out-parameter at BOTH levels, because a
 * caller may pass NULL for "no error report". */
NS_ASSUME_NONNULL_BEGIN

@interface NSData : NSObject <NSCopying>
{
	unsigned char *_bytes;		/* owned; NULL only while empty */
	size_t _length;
}

/* Cocoa's option set for the base64 methods; both flags are honoured. */
typedef enum {
	NSDataBase64EncodingDefault = 0,
	NSDataBase64Encoding64CharacterLineLength = 1,
	NSDataBase64EncodingEndLineWithLineFeed = 2,
	NSDataBase64DecodingIgnoreUnknownCharacters = 4
} NSDataBase64EncodingOptions;

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
	NSDataReadingUncached = 2
} NSDataReadingOptions;

typedef enum {
	NSDataWritingDefault = 0,
	NSDataWritingAtomic = 1
} NSDataWritingOptions;

+ (nullable NSData *)data;
+ (nullable NSData *)dataWithBytes:(const void *)bytes length:(size_t)length;
+ (nullable NSData *)dataWithBytesNoCopy:(void *)bytes length:(size_t)length;
+ (nullable NSData *)dataWithBytesNoCopy:(void *)bytes length:(size_t)length freeWhenDone:(BOOL)freeWhenDone;
+ (NSData *)dataWithData:(NSData *)other;
+ (nullable NSData *)dataWithContentsOfFile:(NSString *)path;
+ (nullable NSData *)dataWithContentsOfFile:(NSString *)path options:(NSDataReadingOptions)options error:(NSError * _Nullable * _Nullable)errorPtr;
+ (nullable NSData *)dataWithBase64EncodedString:(NSString *)string;
+ (nullable NSData *)dataWithBase64EncodedString:(NSString *)string options:(NSDataBase64EncodingOptions)options;

- (nullable id)initWithBytes:(const void *)bytes length:(size_t)length;
- (nullable id)initWithBytesNoCopy:(void *)bytes length:(size_t)length;
- (nullable id)initWithBytesNoCopy:(void *)bytes length:(size_t)length freeWhenDone:(BOOL)freeWhenDone;
- (id)initWithData:(NSData *)other;
- (nullable id)initWithContentsOfFile:(NSString *)path;
- (nullable id)initWithContentsOfFile:(NSString *)path options:(NSDataReadingOptions)options error:(NSError * _Nullable * _Nullable)errorPtr;
- (nullable id)initWithBase64EncodedString:(NSString *)string
			  options:(NSDataBase64EncodingOptions)options;
- (nullable id)initWithBase64EncodedData:(NSData *)base64Data options:(NSDataBase64EncodingOptions)options;

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

NS_ASSUME_NONNULL_END

@end

#endif /* FOUNDATION_NSDATA_H */
