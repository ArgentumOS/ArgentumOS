/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSValue — a box for anything that is not an object. docs/design/foundation-plan.md §10 (the
 * un-refusal program), F13.8c.
 *
 * WHY A LIBRARY THAT ALREADY BOXES SCALARS INTO NSNumber NEEDS THIS: NSNumber is for NUMBERS —
 * it answers -intValue, -doubleValue, -compare:, and it is what arithmetic and sorting want. A
 * value is for a STRUCTURE: bytes with a type encoding, which no number can carry. That is also
 * what makes it load-bearing for the coders (`NSKeyedArchiver` writes structs as values) and for
 * the geometry types a UI layer passes around.
 *
 * THE TWO DOORS THAT MATTER, and why the signature looks like C:
 *   +valueWithBytes:objCType:  copies sizeof(type) bytes, so the caller keeps its own storage;
 *   -getValue:                 copies them back, and the caller is responsible for the buffer
 *                              being large enough — exactly as in Cocoa.
 *
 * THE SIZE COMES FROM THE ENCODING, which is the one piece of real work here: there is no
 * NSGetSizeAndAlignment in this library, so `fn_value_size` walks the encoding — scalars, pointers,
 * objects, C arrays and structs/unions, recursively — and RAISES on an encoding it cannot size
 * rather than guessing a length. Named limits: no bitfields, and no Objective-C++ encodings.
 *
 * THE GEOMETRY EXTENSIONS ARE ABSENT BY NAME: +valueWithPoint:/+valueWithSize:/+valueWithRect: are
 * declared in Cocoa's NSValue.h, and this library has no NSPoint/NSSize/NSRect to build them from
 * (the UI layer's Point/Size/Rect are its own types). NSRange, which IS here, is supported.
 */

#ifndef FOUNDATION_NSVALUE_H
#define FOUNDATION_NSVALUE_H

#import <Foundation/NSObject.h>
#import <Foundation/NSObjCRuntime.h>

NS_ASSUME_NONNULL_BEGIN

@interface NSValue : NSObject <NSCopying>
{
	const char *_objCType;		/* OWNED: a copy of the caller's string */
	void *_bytes;			/* OWNED: _size bytes, the caller's storage copied in */
	NSUInteger _size;
}

+ (NSValue *)valueWithBytes:(const void *)value objCType:(const char *)type;
/* The older spelling, still present in Cocoa's header, so it is here too. */
+ (NSValue *)value:(const void *)value withObjCType:(const char *)type;
+ (NSValue *)valueWithPointer:(nullable const void *)pointer;
+ (NSValue *)valueWithRange:(NSRange)range;

- (void)getValue:(void *)value;
/* Copies at most `size` bytes, and the encoding must fit: Cocoa's own door, for a caller with a
 * buffer of a size it computed itself. */
- (void)getValue:(void *)value size:(NSUInteger)size;

- (const char *)objCType;
- (nullable void *)pointerValue;
- (NSRange)rangeValue;

- (BOOL)isEqualToValue:(NSValue *)value;
- (BOOL)isEqual:(nullable id)other;
- (NSUInteger)hash;
- (NSString *)description;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSVALUE_H */
