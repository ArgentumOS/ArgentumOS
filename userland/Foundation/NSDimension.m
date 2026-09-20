/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSDimension.m — a unit plus its converter. docs/design/foundation-plan.md §12.3 W12.
 *
 * MANUAL OWNERSHIP: one owned converter, copied in the designated initializer. A converter is IMMUTABLE
 * (coefficient and constant are set once and have no setters), so the copy is a retain in practice and
 * the ownership question is settled by the type rather than by a convention here.
 */

#import <Foundation/NSDimension.h>
#import <Foundation/NSUnitConverter.h>
#import <Foundation/NSString.h>
#import <Foundation/NSCoder.h>

@implementation NSDimension

- (instancetype)initWithSymbol:(NSString *)symbol converter:(NSUnitConverter *)converter
{
	self = [super initWithSymbol:symbol];
	if (self == nil) {
		return nil;
	}
	_converter = [converter retain];
	return self;
}

- (void)dealloc
{
	[_converter release];
	[super dealloc];
}

- (nullable NSUnitConverter *)converter
{
	return _converter;
}

+ (nullable NSUnit *)baseUnit
{
	/* There is no base unit of dimensions in general — see the header. A concrete family answers its
	 * own; this is the honest abstract answer. */
	return nil;
}

/* NSCoding, on top of NSUnit's symbol: the dimension adds the converter, and the pair is enough to
 * rebuild the unit because a dimension's behaviour IS its symbol plus its converter. */
- (void)encodeWithCoder:(NSCoder *)coder
{
	[super encodeWithCoder:coder];
	[coder encodeObject:_converter forKey:@"NS.dimensionConverter"];
}

- (instancetype)initWithCoder:(NSCoder *)coder
{
	self = [super initWithCoder:coder];
	if (self == nil) {
		return nil;
	}
	_converter = [[coder decodeObjectForKey:@"NS.dimensionConverter"] retain];
	return self;
}

@end
