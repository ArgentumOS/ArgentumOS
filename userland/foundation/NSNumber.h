/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSNumber — a boxed scalar of the whole C-type matrix.
 * docs/design/foundation-plan.md, F2 + the public-API audit + the hard rule.
 *
 * A PLAIN OBJECT, NOT A TAGGED ONE, in v1: the runtime HAS the machinery
 * (SmallObjectClasses[0] is documented in class.h as "typically used for storing
 * 31-bit signed integers"), and boxing a number in the pointer is a real
 * optimisation — but it is an optimisation, and v1's rule is the obvious
 * implementation first. Recorded here so the option is not lost.
 *
 * VALUE SEMANTICS ACROSS TYPES: -isEqual: compares values, so an integer and a
 * double holding the same number are equal and hash alike. That is what makes a
 * number usable as a collection key however it was built — and what makes the
 * COPYING family below load-bearing rather than decorative, since a dictionary
 * copies its keys (a number is immutable, so copying returns self).
 *
 * -objCType answers with the @encode of the type the number was CREATED as, which
 * is Cocoa's behaviour: [numberWithFloat:1.5f objCType] is "f" even though the
 * value is stored as a double.
 */

#ifndef FOUNDATION_NSNUMBER_H
#define FOUNDATION_NSNUMBER_H

#import <foundation/NSObject.h>

/* NULLABILITY (F6, slice 3): NONNULL by default, and the ONE exception is a
 * locale parameter — the same rule as the strings: a nil locale means no
 * locale-sensitive rules. Every constructor here is total: the file has no
 * `return nil` and no `return NULL` at all (measured, not assumed). */
NS_ASSUME_NONNULL_BEGIN

@interface NSNumber : NSObject <NSCopying>
{
	union {
		long long _signedValue;
		unsigned long long _unsignedValue;
		double _doubleValue;
	} _value;
	unsigned char _kind;		/* the @encode char of the type it was built as */
}

/* Creation and initialisation: the full 15-type matrix, Cocoa's names. */
+ (NSNumber *)numberWithBool:(BOOL)value;
+ (NSNumber *)numberWithChar:(char)value;
+ (NSNumber *)numberWithShort:(short)value;
+ (NSNumber *)numberWithInt:(int)value;
+ (NSNumber *)numberWithLong:(long)value;
+ (NSNumber *)numberWithLongLong:(long long)value;
+ (NSNumber *)numberWithInteger:(NSInteger)value;
+ (NSNumber *)numberWithUnsignedChar:(unsigned char)value;
+ (NSNumber *)numberWithUnsignedShort:(unsigned short)value;
+ (NSNumber *)numberWithUnsignedInt:(unsigned int)value;
+ (NSNumber *)numberWithUnsignedLong:(unsigned long)value;
+ (NSNumber *)numberWithUnsignedLongLong:(unsigned long long)value;
+ (NSNumber *)numberWithUnsignedInteger:(NSUInteger)value;
+ (NSNumber *)numberWithFloat:(float)value;
+ (NSNumber *)numberWithDouble:(double)value;

- (id)initWithBool:(BOOL)value;
- (id)initWithChar:(char)value;
- (id)initWithShort:(short)value;
- (id)initWithInt:(int)value;
- (id)initWithLong:(long)value;
- (id)initWithLongLong:(long long)value;
- (id)initWithInteger:(NSInteger)value;
- (id)initWithUnsignedChar:(unsigned char)value;
- (id)initWithUnsignedShort:(unsigned short)value;
- (id)initWithUnsignedInt:(unsigned int)value;
- (id)initWithUnsignedLong:(unsigned long)value;
- (id)initWithUnsignedLongLong:(unsigned long long)value;
- (id)initWithUnsignedInteger:(NSUInteger)value;
- (id)initWithFloat:(float)value;
- (id)initWithDouble:(double)value;

/* The same matrix again as accessors, plus the two textual forms. */
- (BOOL)boolValue;
- (char)charValue;
- (short)shortValue;
- (int)intValue;
- (long)longValue;
- (long long)longLongValue;
- (NSInteger)integerValue;
- (unsigned char)unsignedCharValue;
- (unsigned short)unsignedShortValue;
- (unsigned int)unsignedIntValue;
- (unsigned long)unsignedLongValue;
- (unsigned long long)unsignedLongLongValue;
- (NSUInteger)unsignedIntegerValue;
- (float)floatValue;
- (double)doubleValue;

- (NSString *)stringValue;			/* == -description */
- (NSString *)descriptionWithLocale:(nullable id)locale;
- (const char *)objCType;

- (BOOL)isEqualToNumber:(NSNumber *)other;
- (NSComparisonResult)compare:(NSNumber *)other;

/* Immutable, so every one of these returns self — Cocoa's behaviour. */
- (id)copy;
- (id)mutableCopy;
- (id)copyWithZone:(NSZone *)zone;
- (id)mutableCopyWithZone:(NSZone *)zone;

NS_ASSUME_NONNULL_END

@end

#endif /* FOUNDATION_NSNUMBER_H */
