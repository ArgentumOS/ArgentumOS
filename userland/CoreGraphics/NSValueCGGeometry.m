/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSValueCGGeometry.m — the `CG`-spelled NSValue geometry doors (§63.53). Moved here from Foundation.
 *
 * PUBLIC API ONLY, AND THAT IS A RULE RATHER THAN A PREFERENCE: Foundation's box keeps its payload behind
 * `-fnBytes`/`-fnInitWithBytes:`, which are PRIVATE to that library. A category in another tier cannot call
 * them, and reaching around them would put the boundary back — so every door here goes through
 * `+valueWithBytes:objCType:` and `-getValue:`, which are the class's own published doors and are exactly what
 * a caller of these would use.
 *
 * AND THE READERS DO NOT CHECK THE STORED TYPE, which is Apple's contract rather than an omission:
 * `-CGPointValue` on a box holding something else is undefined there too, and the way to ask is `-objCType`.
 */

#import <CoreGraphics/NSValueCGGeometry.h>

@implementation NSValue (NSValueCGGeometryAdditions)

+ (NSValue *)valueWithCGPoint:(CGPoint)point
{
	return [self valueWithBytes:&point objCType:@encode(CGPoint)];
}

+ (NSValue *)valueWithCGSize:(CGSize)size
{
	return [self valueWithBytes:&size objCType:@encode(CGSize)];
}

+ (NSValue *)valueWithCGRect:(CGRect)rect
{
	return [self valueWithBytes:&rect objCType:@encode(CGRect)];
}

+ (NSValue *)valueWithCGAffineTransform:(CGAffineTransform)transform
{
	return [self valueWithBytes:&transform objCType:@encode(CGAffineTransform)];
}

- (CGPoint)CGPointValue
{
	CGPoint value = { 0.0, 0.0 };

	[self getValue:&value];
	return value;
}

- (CGSize)CGSizeValue
{
	CGSize value = { 0.0, 0.0 };

	[self getValue:&value];
	return value;
}

- (CGRect)CGRectValue
{
	CGRect value = { { 0.0, 0.0 }, { 0.0, 0.0 } };

	[self getValue:&value];
	return value;
}

/* `+valueWithCGVector:` AND `-CGVectorValue` STOOD HERE AND WENT WITH `CGVector` (2026-10-05): the
 * type is macOS 10.7, so there is no encoding to box and no reader to spell. */

- (CGAffineTransform)CGAffineTransformValue
{
	/* THE IDENTITY, WRITTEN OUT rather than named: `CGAffineTransformIdentity` is a symbol in this same
	 * library and naming it here would be a data reference across translation units for no gain. It is only
	 * the ZERO FALLBACK for a box that answered nothing. */
	CGAffineTransform value = { 1.0, 0.0, 0.0, 1.0, 0.0, 0.0 };

	[self getValue:&value];
	return value;
}

@end
