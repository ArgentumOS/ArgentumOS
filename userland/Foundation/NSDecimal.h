/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSDecimal — base-10 arithmetic a program can trust (W3).
 *
 * WHAT APPLE PUBLISHES, AND WHAT IS THEREFORE THE SPECIFICATION: an NSDecimal is
 * `mantissa × 10^exponent`, the mantissa holding up to 38 significant digits and the exponent running
 * −128 through 127; the functions below take a result pointer, one or two operand pointers and a
 * rounding mode, and RETURN AN `NSCalculationError` rather than raising; NSDecimalAdd and friends
 * normalize their operands first (equal exponents); NSDecimalRound rounds to a SCALE, the number of
 * digits after the point; NSDecimalCompact makes the representation as small as it can be, and every
 * function accepts a compacted decimal.
 *
 * WHAT APPLE DOES NOT PUBLISH, AND IS THEREFORE OURS (§11.6.1 D2): THE STRUCT'S LAYOUT. Apple's header
 * packs it into bitfields and an array of shorts; the layout is not in the documentation, it is only in
 * the header, and Apple's headers are off-limits here. So this is OUR layout, chosen to make the
 * arithmetic readable: the digits LEAST-SIGNIFICANT FIRST, one decimal digit per byte, which is what
 * long addition, schoolbook multiplication and trial-division long division all want. A program that
 * uses NSDecimal through these functions — the documented usage — observes nothing of it; a program that
 * reads `_digits` directly would, and no such program can exist, because the field names are ours too.
 *
 * THE TWO FUNCTIONS THAT ALIAS ARE SUPPORTED ON PURPOSE: Apple's own documentation shows
 * `NSDecimalAdd(&value1, &value1, &value2, NSRoundPlain)` — the result pointer may BE an operand — so
 * every function here writes through a local and copies at the end.
 */

#ifndef FOUNDATION_NSDECIMAL_H
#define FOUNDATION_NSDECIMAL_H

#import <Foundation/NSObjCRuntime.h>

@class NSString;

/* THE LIMITS, from Apple's prose: 38 significant digits, an exponent of −128 through 127. */
#define NSDecimalMaxDigits	38
#define NSDecimalMinExponent	(-128)
#define NSDecimalMaxExponent	127

/* `NSDecimalNoScale` IS NOT DEFINED HERE: NSObjCRuntime.h already declares it, in the block of names
 * Apple publishes without a number (its value is ours and one place only — NSObjCRuntime.h — says so).
 *
 * THE MANTISSA HAS ONE SPARE DIGIT, which is not decoration: a rounding carry out of the 38th place has
 * to go SOMEWHERE before the exponent can absorb it, and writing it one past the end of the array was a
 * measured out-of-bounds write. */
typedef struct {
	signed char _exponent;			/* −128 through 127 */
	unsigned char _length;			/* significant digits, 1..38; 0 means the value is ZERO */
	unsigned char _isNegative;
	unsigned char _isCompact;		/* what NSDecimalCompact sets; arithmetic never reads it */
	unsigned char _isNaN;			/* the flag `_length == 0` cannot carry: NaN is not zero */
	unsigned char _digits[NSDecimalMaxDigits + 1];	/* LEAST significant first, one digit per byte */
} NSDecimal;

/* HOW A RESULT IS ROUNDED WHEN IT CANNOT HOLD WHAT THE ARITHMETIC PRODUCED. The four names are Apple's
 * and so are their VALUES here (0, 1, 2, 3), which is unusual for this library and worth saying: they
 * are published by Apple's own rounding table, which the probe asserts. */
typedef enum {
	NSRoundPlain = 0,		/* to nearest; a tie goes away from zero */
	NSRoundDown = 1,		/* toward zero */
	NSRoundUp = 2,			/* away from zero */
	NSRoundBankers = 3		/* to nearest; a tie goes to the EVEN digit */
} NSRoundingMode;

/* THE ERROR A CALCULATION REPORTS, and its values are published too (0..4 in this order). */
typedef enum {
	NSCalculationNoError = 0,
	NSCalculationLossOfPrecision = 1,
	NSCalculationUnderflow = 2,
	NSCalculationOverflow = 3,
	NSCalculationDivideByZero = 4
} NSCalculationError;

/* TWO RULES OF OURS THAT A CALLER CAN SEE, and so are part of the contract rather than an accident:
 *
 * 1. A VALUE OUTSIDE THE EXPONENT'S RANGE IS REPORTED AND SATURATED: overflow gives NSDecimalMax (or
 *    NSDecimalMin when negative), underflow gives a signed zero, and the returned code says which. The
 *    exponent lives in a signed char, so a caller that ignores the code would otherwise read a WRAPPED
 *    exponent as a plausible number.
 * 2. THE EXPONENT BOUNDS APPLY TO THE LOWEST DIGIT. `1 × 10^127` is at the top and `1 × 10^-128` at the
 *    bottom, so a 38-digit number may hold significant digits ABOVE 10^127 — that is what makes
 *    NSDecimalMax the 38 nines it is. Overflow and underflow are about the exponent, not the magnitude.
 * 3. AN ARITHMETIC RESULT IS COMPACTED: it carries no trailing zeros, so 2.5 + 2.5 prints "5" and 2/4
 *    prints "0.5" rather than a mantissa padded out with zeros. (Every function accepts a compacted
 *    decimal, which Apple's documentation says explicitly.) */

NS_ASSUME_NONNULL_BEGIN

/* THE ARITHMETIC. `result` may alias either operand. */
NSCalculationError NSDecimalAdd(NSDecimal *result, const NSDecimal *left, const NSDecimal *right, NSRoundingMode mode);
NSCalculationError NSDecimalSubtract(NSDecimal *result, const NSDecimal *left, const NSDecimal *right, NSRoundingMode mode);
NSCalculationError NSDecimalMultiply(NSDecimal *result, const NSDecimal *left, const NSDecimal *right, NSRoundingMode mode);
NSCalculationError NSDecimalDivide(NSDecimal *result, const NSDecimal *left, const NSDecimal *right, NSRoundingMode mode);
NSCalculationError NSDecimalPower(NSDecimal *result, const NSDecimal *number, NSUInteger power, NSRoundingMode mode);
NSCalculationError NSDecimalMultiplyByPowerOf10(NSDecimal *result, const NSDecimal *number, short power, NSRoundingMode mode);

/* THE REPRESENTATION'S OWN OPERATIONS. */
void NSDecimalCompact(NSDecimal *number);
NSCalculationError NSDecimalNormalize(NSDecimal *number1, NSDecimal *number2, NSRoundingMode mode);
void NSDecimalCopy(NSDecimal *destination, const NSDecimal *source);

/* THE REST OF THE SURFACE. */
void NSDecimalRound(NSDecimal *result, const NSDecimal *number, NSInteger scale, NSRoundingMode mode);
NSComparisonResult NSDecimalCompare(const NSDecimal *left, const NSDecimal *right);
BOOL NSDecimalIsNotANumber(const NSDecimal *number);
NSString *NSDecimalString(const NSDecimal *number, id _Nullable locale);

/* APPLE'S TWO CONSTANTS OF THE TYPE, both "the largest/smallest representable". */
extern const NSDecimal NSDecimalMax;
extern const NSDecimal NSDecimalMin;

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSDECIMAL_H */
