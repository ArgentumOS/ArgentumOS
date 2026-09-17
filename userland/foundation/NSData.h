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

+ (NSData *)data;
+ (NSData *)dataWithBytes:(const void *)bytes length:(size_t)length;
+ (NSData *)dataWithBytesNoCopy:(void *)bytes length:(size_t)length;
+ (NSData *)dataWithBytesNoCopy:(void *)bytes length:(size_t)length freeWhenDone:(BOOL)freeWhenDone;
+ (NSData *)dataWithData:(NSData *)other;
+ (NSData *)dataWithContentsOfFile:(NSString *)path;
+ (NSData *)dataWithBase64EncodedString:(NSString *)string;

- (id)initWithBytes:(const void *)bytes length:(size_t)length;
- (id)initWithBytesNoCopy:(void *)bytes length:(size_t)length;
- (id)initWithBytesNoCopy:(void *)bytes length:(size_t)length freeWhenDone:(BOOL)freeWhenDone;
- (id)initWithData:(NSData *)other;
- (id)initWithContentsOfFile:(NSString *)path;
- (id)initWithBase64EncodedString:(NSString *)string
			  options:(NSDataBase64EncodingOptions)options;

- (size_t)length;
- (const void *)bytes;
- (void)getBytes:(void *)buffer length:(size_t)length;
- (void)getBytes:(void *)buffer range:(NSRange)range;
- (NSData *)subdataWithRange:(NSRange)range;
- (NSRange)rangeOfData:(NSData *)other
	       options:(NSDataSearchOptions)options
		 range:(NSRange)range;

- (NSString *)base64EncodedStringWithOptions:(NSDataBase64EncodingOptions)options;
- (BOOL)writeToFile:(NSString *)path atomically:(BOOL)useAuxiliaryFile;

- (BOOL)isEqualToData:(NSData *)other;

@end

@interface NSMutableData : NSData <NSMutableCopying>
{
	size_t _capacity;
}

+ (NSMutableData *)dataWithCapacity:(size_t)capacity;
+ (NSMutableData *)dataWithLength:(size_t)length;
- (id)initWithCapacity:(size_t)capacity;
- (id)initWithLength:(size_t)length;

- (void)appendBytes:(const void *)bytes length:(size_t)length;
- (void)appendData:(NSData *)other;
- (void)setLength:(size_t)length;
- (void)increaseLengthBy:(size_t)extraLength;
- (void *)mutableBytes;
- (void)replaceBytesInRange:(NSRange)range withBytes:(const void *)bytes;
- (void)replaceBytesInRange:(NSRange)range
		  withBytes:(const void *)bytes
		     length:(size_t)replacementLength;
- (void)resetBytesInRange:(NSRange)range;

@end

#endif /* FOUNDATION_NSDATA_H */
