/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUnitConverter.m — the abstract pair, and the linear one. docs/design/foundation-plan.md §12.3 W12.
 *
 * THE BASE RAISES. A converter is a dimension's arithmetic, and a converter that answered without one
 * would be inventing a conversion — the same shape NSFormatter's abstract doors have, and for the same
 * reason. The messages say which subclass answers them.
 *
 * THE LINEAR ARITHMETIC IS TWO LINES AND IT IS THE WHOLE CLASS: base = value * coefficient + constant,
 * and the inverse divides — with the DIVIDE guarded, because a coefficient of zero is a caller error
 * rather than a value: a converter that maps every input to the constant is not a conversion, and
 * answering infinity would push that onto every reader.
 */

#import <Foundation/NSUnitConverter.h>
#import <Foundation/NSException.h>
#import <Foundation/NSString.h>
#import <Foundation/NSCoder.h>

@implementation NSUnitConverter

- (double)baseUnitValueFromValue:(double)value
{
	(void)value;
	[NSException raise:NSInvalidArgumentException
		    format:@"[%@ %@]: the base converter has no arithmetic — a concrete converter "
			   "(NSUnitConverterLinear, say) must implement it",
			   [self class], @"baseUnitValueFromValue:"];
	return 0.0;
}

- (double)valueFromBaseUnitValue:(double)baseUnitValue
{
	(void)baseUnitValue;
	[NSException raise:NSInvalidArgumentException
		    format:@"[%@ %@]: the base converter has no arithmetic — a concrete converter "
			   "(NSUnitConverterLinear, say) must implement it",
			   [self class], @"valueFromBaseUnitValue:"];
	return 0.0;
}

@end

@implementation NSUnitConverterLinear

- (instancetype)initWithCoefficient:(double)coefficient
{
	return [self initWithCoefficient:coefficient constant:0.0];
}

- (instancetype)initWithCoefficient:(double)coefficient constant:(double)constant
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_coefficient = coefficient;
	_constant = constant;
	return self;
}

- (double)coefficient { return _coefficient; }
- (double)constant { return _constant; }

- (double)baseUnitValueFromValue:(double)value
{
	return value * _coefficient + _constant;
}

- (double)valueFromBaseUnitValue:(double)baseUnitValue
{
	/* A ZERO COEFFICIENT IS A CALLER ERROR AND IT RAISES rather than answering an infinity: the inverse
	 * of a converter that ignored its input is not a conversion. */
	if (_coefficient == 0.0) {
		[NSException raise:NSInvalidArgumentException
			    format:@"[%@ %@]: a zero coefficient has no inverse",
				   [self class], @"valueFromBaseUnitValue:"];
	}
	return (baseUnitValue - _constant) / _coefficient;
}

- (BOOL)isEqual:(id)other
{
	if (other == self) {
		return YES;
	}
	if (![other isKindOfClass:[NSUnitConverterLinear class]]) {
		return NO;
	}
	return ([other coefficient] == _coefficient && [other constant] == _constant);
}

- (NSUInteger)hash
{
	return (NSUInteger)(_coefficient * 31.0 + _constant);
}

- (NSString *)description
{
	return [NSString stringWithFormat:@"<%@: coefficient=%g constant=%g>",
		[self class], _coefficient, _constant];
}

+ (BOOL)supportsSecureCoding
{
	return YES;
}

- (void)encodeWithCoder:(NSCoder *)coder
{
	[coder encodeDouble:_coefficient forKey:@"NS.converterCoefficient"];
	[coder encodeDouble:_constant forKey:@"NS.converterConstant"];
}

- (instancetype)initWithCoder:(NSCoder *)coder
{
	return [self initWithCoefficient:[coder decodeDoubleForKey:@"NS.converterCoefficient"]
				constant:[coder decodeDoubleForKey:@"NS.converterConstant"]];
}

@end
