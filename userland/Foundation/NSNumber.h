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

#import <Foundation/NSObject.h>
#import <Foundation/NSDecimal.h>

/* NULLABILITY (F6, slice 3): NONNULL by default, and the ONE exception is a
 * locale parameter — the same rule as the strings: a nil locale means no
 * locale-sensitive rules. Every constructor here is total: the file has no
 * `return nil` and no `return NULL` at all (measured, not assumed). */
/* FOR `NSCoding`: ABOVE THE ASSUME-NONNULL REGION, which is where NSArray.h puts it too — a `#include` inside a
 * nullability region is refused by the compiler, because the imported header's declarations would be dragged
 * into somebody else's region. */
#import <Foundation/NSCoding.h>

NS_ASSUME_NONNULL_BEGIN

/* ===================================================================================================
 * NSNUMBER IS A CLASS CLUSTER (2026-09-29; docs/design/foundation-clusters-plan.md §C.3, M4).
 *
 * THE PAYLOAD LIVES IN THE CONCRETE CLASSES, which is the whole of this milestone: the front is
 * PAYLOAD-FREE, and each concrete class holds one field plus the @encode char it was built with —
 * AGNumberSigned (long long), AGNumberUnsigned (unsigned long long), AGNumberFloating (double),
 * AGNumberBoolean (BOOL), and AGNumberItems, the general instance `+alloc` answers with before a
 * constructor chooses. The CONSTRUCTOR chooses, because the class follows the TYPE: a 44-byte decimal
 * does not fit any of them and is served by NSDecimalNumber (which is why that subclass exists).
 *
 * THE PRIMITIVES ARE THESE FOUR, and everything else here is written over them:
 *
 *     - (unsigned char)agKind                  the @encode char of the type it was built as
 *     - (long long)longLongValue               the payload, as the widest signed form
 *     - (unsigned long long)unsignedLongLongValue
 *     - (double)doubleValue
 *
 * -objCType, the fifteen conversion accessors, -isFloating, -isUnsigned, -boolValue, -isEqualToNumber:,
 * -hash, -description and the decimal bridge are all derived from those, so a subclass that answers the
 * four is correct through every door — which the probe proves with a class of its own.
 * =================================================================================================== */

@interface NSNumber : NSObject <NSCopying, NSCoding>

/* THE NSCoding DOORS (§63.22). THE WIRE IS OURS (Apple's spelling for this class's coded form is not
 * published in a source this project may read). THE TYPE TRAVELS WITH THE VALUE, because this header says in
 * its own words that `-objCType` "answers with the @encode of the type the number was CREATED as" — a box
 * that dropped it would decode the integer 1 as a double. */
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;

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

/* THE DECIMAL CASE (W3). NSDecimal is 44 bytes and this class's store is an 8-byte scalar union, so the
 * CREATION side is served by NSDecimalNumber — which is what Cocoa does, and what -initWithDecimal: says
 * in the implementation. -decimalValue converts from whatever scalar was stored; the rule is documented
 * there too. +numberWithDecimal: STOOD HERE and is GONE (§63.59): it was a second spelling of
 * NSDecimalNumber's +decimalNumberWithDecimal:, answered the same class, and Apple declares no decimal
 * factory on NSNumber at all. */
- (id)initWithDecimal:(NSDecimal)decimal;
- (NSDecimal)decimalValue;

- (BOOL)isEqualToNumber:(NSNumber *)other;
- (NSComparisonResult)compare:(NSNumber *)other;

/* Immutable, so every one of these returns self — Cocoa's behaviour. */
- (id)copy;
- (id)mutableCopy;
- (id)copy;
- (id)mutableCopy;

NS_ASSUME_NONNULL_END

@end

#endif /* FOUNDATION_NSNUMBER_H */
