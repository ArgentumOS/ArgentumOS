/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSDecimalNumber — the object form of NSDecimal (W3's second half).
 *
 * WHAT APPLE PUBLISHES, and therefore what is implemented here: NSDecimalNumber is an IMMUTABLE SUBCLASS
 * OF NSNumber wrapping `mantissa × 10^exponent` (38 digits, exponent −128..127); it is the type to use
 * where binary floating point is not acceptable, because 0.1 + 0.2 is 0.3 and not 0.30000000000000004.
 * `+decimalNumberWithString:` accepts an optional leading sign, ONE decimal separator and ONE `E`/`e`
 * exponent, and **the separator is the DEFAULT LOCALE'S** — a period in the US, a comma in France — which
 * is why the `locale:` variants exist and why this implementation reads the separator from ICU rather than
 * hardcoding '.'. Arithmetic has TWO forms: the plain one uses `+defaultBehavior`, and the `withBehavior:`
 * one takes anything conforming to NSDecimalNumberBehaviors.
 *
 * `+defaultBehavior` and `+defaultDecimalNumberHandler` ARE THE SAME BEHAVIOUR, and the documentation
 * says what it is in words rather than numbers: NO ROUNDING (scale is NSDecimalNoScale and the mode is
 * NSRoundPlain, which with no scale is never consulted), 38 significant digits assumed, and a RAISE on
 * overflow, underflow and divide-by-zero — but NOT on loss of precision, which is why an inexact sum
 * returns a rounded number instead of throwing. The four exception NAMES are the constants below; the
 * `raiseOn...` flags are what select which of them a handler throws.
 *
 * THE ONE DEVIATION, DECLARED (policy: deviations only as far as necessary, and documented): the default
 * behavior is a PROCESS-WIDE setting in Apple's design, and it is one here too — but this library is
 * built for a single-user session and does not synchronise it against a second thread.
 */

#ifndef FOUNDATION_NSDECIMALNUMBER_H
#define FOUNDATION_NSDECIMALNUMBER_H

#import <Foundation/NSDecimal.h>
#import <Foundation/NSNumber.h>
#import <Foundation/NSObjCRuntime.h>
#import <Foundation/NSCoding.h>

@class NSString, NSLocale, NSDecimalNumber, NSDecimalNumberHandler;

NS_ASSUME_NONNULL_BEGIN

/* THE FOUR NAMES A HANDLER RAISES, exactly as Apple declares them. */

extern NSString * const NSDecimalNumberExactnessException;
extern NSString * const NSDecimalNumberOverflowException;
extern NSString * const NSDecimalNumberUnderflowException;
extern NSString * const NSDecimalNumberDivideByZeroException;

/* THE PROTOCOL: three methods, and the third is the one that decides what an error DOES. */
@protocol NSDecimalNumberBehaviors <NSObject>
@required
- (NSRoundingMode)roundingMode;
- (short)scale;
/* Called when an operation reports `error`; return the number to use instead, or nil to accept the
 * rounded result. A handler that raises never returns. */
- (nullable NSDecimalNumber *)exceptionDuringOperation:(SEL)method error:(NSCalculationError)error
					  leftOperand:(nullable NSDecimalNumber *)leftOperand
					 rightOperand:(nullable NSDecimalNumber *)rightOperand;
@end

@interface NSDecimalNumber : NSNumber
{
	NSDecimal _decimal;
}

/* CREATION. The string forms are locale-aware for the decimal separator (see the file comment). */
+ (NSDecimalNumber *)decimalNumberWithString:(NSString *)numberValue;
+ (NSDecimalNumber *)decimalNumberWithString:(NSString *)numberValue locale:(nullable id)locale;
+ (NSDecimalNumber *)decimalNumberWithMantissa:(unsigned long long)mantissa exponent:(short)exponent
				    isNegative:(BOOL)flag;
+ (NSDecimalNumber *)decimalNumberWithDecimal:(NSDecimal)decimal;

- (id)initWithString:(NSString *)numberValue;
- (id)initWithString:(NSString *)numberValue locale:(nullable id)locale;
- (id)initWithMantissa:(unsigned long long)mantissa exponent:(short)exponent isNegative:(BOOL)flag;
- (id)initWithDecimal:(NSDecimal)decimal;

/* THE CONSTANTS Apple defines, as class methods. */
+ (NSDecimalNumber *)zero;
+ (NSDecimalNumber *)one;
+ (NSDecimalNumber *)notANumber;
+ (NSDecimalNumber *)maximumDecimalNumber;
+ (NSDecimalNumber *)minimumDecimalNumber;

/* THE DEFAULT BEHAVIOUR, settable process-wide. */
+ (id <NSDecimalNumberBehaviors>)defaultBehavior;
+ (void)setDefaultBehavior:(id <NSDecimalNumberBehaviors>)behavior;

/* ACCESSORS. The scalar ones convert, and the conversion rule is documented in the implementation. */
- (NSDecimal)decimalValue;
- (const char *)objCType;
- (NSString *)stringValue;
- (NSString *)descriptionWithLocale:(nullable id)locale;

/* COMPARISON AND ARITHMETIC. */
- (NSComparisonResult)compare:(NSNumber *)otherNumber;
- (BOOL)isEqualToNumber:(NSNumber *)number;

- (NSDecimalNumber *)decimalNumberByAdding:(NSDecimalNumber *)decimalNumber;
- (NSDecimalNumber *)decimalNumberByAdding:(NSDecimalNumber *)decimalNumber
			      withBehavior:(nullable id <NSDecimalNumberBehaviors>)behavior;
- (NSDecimalNumber *)decimalNumberBySubtracting:(NSDecimalNumber *)decimalNumber;
- (NSDecimalNumber *)decimalNumberBySubtracting:(NSDecimalNumber *)decimalNumber
				   withBehavior:(nullable id <NSDecimalNumberBehaviors>)behavior;
- (NSDecimalNumber *)decimalNumberByMultiplyingBy:(NSDecimalNumber *)decimalNumber;
- (NSDecimalNumber *)decimalNumberByMultiplyingBy:(NSDecimalNumber *)decimalNumber
				     withBehavior:(nullable id <NSDecimalNumberBehaviors>)behavior;
- (NSDecimalNumber *)decimalNumberByDividingBy:(NSDecimalNumber *)decimalNumber;
- (NSDecimalNumber *)decimalNumberByDividingBy:(NSDecimalNumber *)decimalNumber
				  withBehavior:(nullable id <NSDecimalNumberBehaviors>)behavior;
- (NSDecimalNumber *)decimalNumberByRaisingToPower:(NSUInteger)power;
- (NSDecimalNumber *)decimalNumberByRaisingToPower:(NSUInteger)power
				      withBehavior:(nullable id <NSDecimalNumberBehaviors>)behavior;
- (NSDecimalNumber *)decimalNumberByMultiplyingByPowerOf10:(short)power;
- (NSDecimalNumber *)decimalNumberByMultiplyingByPowerOf10:(short)power
					      withBehavior:(nullable id <NSDecimalNumberBehaviors>)behavior;
- (NSDecimalNumber *)decimalNumberByRoundingAccordingToBehavior:(nullable id <NSDecimalNumberBehaviors>)behavior;

@end

/* THE CONCRETE BEHAVIOUR, which is what most callers pass. */
@interface NSDecimalNumberHandler : NSObject <NSDecimalNumberBehaviors, NSCoding>
{
	NSRoundingMode _roundingMode;
	short _scale;
	BOOL _raiseOnExactness;
	BOOL _raiseOnOverflow;
	BOOL _raiseOnUnderflow;
	BOOL _raiseOnDivideByZero;
}

+ (NSDecimalNumberHandler *)defaultDecimalNumberHandler;
+ (NSDecimalNumberHandler *)decimalNumberHandlerWithRoundingMode:(NSRoundingMode)roundingMode
							   scale:(short)scale
						raiseOnExactness:(BOOL)raiseOnExactness
						 raiseOnOverflow:(BOOL)raiseOnOverflow
						raiseOnUnderflow:(BOOL)raiseOnUnderflow
					       raiseOnDivideByZero:(BOOL)raiseOnDivideByZero;

- (id)initWithRoundingMode:(NSRoundingMode)roundingMode scale:(short)scale
	  raiseOnExactness:(BOOL)raiseOnExactness
	   raiseOnOverflow:(BOOL)raiseOnOverflow
	  raiseOnUnderflow:(BOOL)raiseOnUnderflow
	 raiseOnDivideByZero:(BOOL)raiseOnDivideByZero;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSDECIMALNUMBER_H */
