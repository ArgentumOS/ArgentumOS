/*
 * NSNumber — a boxed scalar, so the collections can hold numbers.
 * docs/design/foundation-plan.md, F2.
 *
 * A PLAIN OBJECT, NOT A TAGGED ONE, in v1. The runtime does have the machinery
 * for tagging — `SmallObjectClasses[0]` exists and class.h says it is "typically
 * used for storing 31-bit signed integers" — and boxing a number in the pointer
 * is a real optimisation. It is also an optimisation, and v1's rule is the
 * obvious implementation first; recorded here so the option is not lost.
 *
 * VALUE SEMANTICS. `-isEqual:` compares values, ACROSS TYPES where it can: an
 * integer and a double holding the same number are equal. That is what makes a
 * number usable as a collection key no matter how it was built — and it is why
 * `-hash` canonicalises an integral value to its integer form, since 1 and 1.0
 * must hash alike if they are equal.
 */

#ifndef FOUNDATION_NSNUMBER_H
#define FOUNDATION_NSNUMBER_H

#import <foundation/NSObject.h>

@interface NSNumber : NSObject
{
	union {
		long long _signedValue;
		unsigned long long _unsignedValue;
		double _doubleValue;
	} _value;
	unsigned char _kind;		/* 'i' signed, 'u' unsigned, 'd' double */
}

+ (NSNumber *)numberWithInt:(int)value;
+ (NSNumber *)numberWithLongLong:(long long)value;
+ (NSNumber *)numberWithUnsignedLongLong:(unsigned long long)value;
+ (NSNumber *)numberWithDouble:(double)value;
+ (NSNumber *)numberWithBool:(BOOL)value;

- (id)initWithInt:(int)value;
- (id)initWithLongLong:(long long)value;
- (id)initWithUnsignedLongLong:(unsigned long long)value;
- (id)initWithDouble:(double)value;
- (id)initWithBool:(BOOL)value;

- (int)intValue;
- (long long)longLongValue;
- (unsigned long long)unsignedLongLongValue;
- (double)doubleValue;
- (BOOL)boolValue;

- (BOOL)isEqualToNumber:(NSNumber *)other;
- (int)compare:(NSNumber *)other;		/* -1, 0, or 1 */

@end

#endif /* FOUNDATION_NSNUMBER_H */
