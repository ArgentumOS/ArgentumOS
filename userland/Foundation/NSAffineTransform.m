/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSAffineTransform.m — the implementation (W2h).
 */
#import <Foundation/NSAffineTransform.h>
#include <math.h>

static const CGFloat fn_degrees_to_radians = 0.017453292519943295;

/* THE ONE MULTIPLICATION EVERY ACCUMULATOR GOES THROUGH. `left` is applied AFTER `right` to a
 * point, which is what makes append and prepend differ by argument order and nothing else. */
static NSAffineTransformStruct fn_concat(NSAffineTransformStruct left, NSAffineTransformStruct right)
{
	NSAffineTransformStruct out;

	out.m11 = left.m11 * right.m11 + left.m21 * right.m12;
	out.m12 = left.m12 * right.m11 + left.m22 * right.m12;
	out.m21 = left.m11 * right.m21 + left.m21 * right.m22;
	out.m22 = left.m12 * right.m21 + left.m22 * right.m22;
	out.tX = left.m11 * right.tX + left.m21 * right.tY + left.tX;
	out.tY = left.m12 * right.tX + left.m22 * right.tY + left.tY;
	return out;
}

@implementation NSAffineTransform

+ (NSAffineTransform *)transform
{
	return [[self alloc] init];
}

- (instancetype)init
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_matrix.m11 = 1.0;
	_matrix.m12 = 0.0;
	_matrix.m21 = 0.0;
	_matrix.m22 = 1.0;
	_matrix.tX = 0.0;
	_matrix.tY = 0.0;
	return self;
}

- (instancetype)initWithTransform:(NSAffineTransform *)transform
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_matrix = [transform transformStruct];
	return self;
}

- (void)translateXBy:(CGFloat)deltaX yBy:(CGFloat)deltaY
{
	NSAffineTransformStruct step = _matrix;

	step.tX += deltaX;
	step.tY += deltaY;
	_matrix = step;
}

- (void)rotateByRadians:(CGFloat)angle
{
	NSAffineTransformStruct step;
	CGFloat c = cos(angle);
	CGFloat s = sin(angle);

	step.m11 = c;
	step.m12 = s;
	step.m21 = -s;
	step.m22 = c;
	step.tX = 0.0;
	step.tY = 0.0;
	_matrix = fn_concat(step, _matrix);
}

- (void)rotateByDegrees:(CGFloat)angle
{
	[self rotateByRadians:angle * fn_degrees_to_radians];
}

- (void)scaleXBy:(CGFloat)scaleX yBy:(CGFloat)scaleY
{
	NSAffineTransformStruct step;

	step.m11 = scaleX;
	step.m12 = 0.0;
	step.m21 = 0.0;
	step.m22 = scaleY;
	step.tX = 0.0;
	step.tY = 0.0;
	_matrix = fn_concat(step, _matrix);
}

- (void)scaleBy:(CGFloat)scale
{
	[self scaleXBy:scale yBy:scale];
}

/*
 * APPEND AND PREPEND ARE THE TWO PRODUCT ORDERS, and Apple states which is which: append multiplies
 * the ARGUMENT by the receiver, prepend the receiver by the argument. Because a product is
 * associative but not commutative, a translation and a scale give different points through the two,
 * which is what the check measures - and the wording here is deliberately about the PRODUCT rather
 * than about which is "applied first", because that phrasing is exactly how I got it backwards once.
 */
- (void)appendTransform:(NSAffineTransform *)transform
{
	_matrix = fn_concat([transform transformStruct], _matrix);
}

- (void)prependTransform:(NSAffineTransform *)transform
{
	_matrix = fn_concat(_matrix, [transform transformStruct]);
}

- (void)invert
{
	NSAffineTransformStruct held = _matrix;
	CGFloat determinant = held.m11 * held.m22 - held.m12 * held.m21;
	NSAffineTransformStruct out;

	if (determinant == 0.0) {
		/* A SINGULAR MATRIX HAS NO INVERSE, and this library leaves the receiver UNCHANGED rather
		 * than raising: the documented behaviour is undefined, the header says which way we went,
		 * and a raise here would be a claim about a case Apple does not define. */
		return;
	}
	out.m11 = held.m22 / determinant;
	out.m12 = -held.m12 / determinant;
	out.m21 = -held.m21 / determinant;
	out.m22 = held.m11 / determinant;
	out.tX = -(out.m11 * held.tX + out.m21 * held.tY);
	out.tY = -(out.m12 * held.tX + out.m22 * held.tY);
	_matrix = out;
}

- (NSPoint)transformPoint:(NSPoint)point
{
	NSPoint out;

	out.x = _matrix.m11 * point.x + _matrix.m21 * point.y + _matrix.tX;
	out.y = _matrix.m12 * point.x + _matrix.m22 * point.y + _matrix.tY;
	return out;
}

/* TRANSLATION DOES NOT APPLY TO A SIZE: a size is a vector, and a vector has no position to move.
 * Apple states this, and it is the difference between this method and -transformPoint:. */
- (NSSize)transformSize:(NSSize)size
{
	NSSize out;

	out.width = _matrix.m11 * size.width + _matrix.m21 * size.height;
	out.height = _matrix.m12 * size.width + _matrix.m22 * size.height;
	return out;
}

- (NSAffineTransformStruct)transformStruct
{
	return _matrix;
}

- (void)setTransformStruct:(NSAffineTransformStruct)transformStruct
{
	_matrix = transformStruct;
}

- (id)copy
{
	NSAffineTransform *copy = [[[self class] alloc] init];

	[copy setTransformStruct:_matrix];
	return copy;	/* MUTABLE, so the copy is a new object and the snapshot is real */
}

- (BOOL)isEqual:(id)other
{
	NSAffineTransformStruct theirs;

	if (![other isKindOfClass:[NSAffineTransform class]]) {
		return NO;
	}
	theirs = [other transformStruct];
	return _matrix.m11 == theirs.m11 && _matrix.m12 == theirs.m12 &&
	       _matrix.m21 == theirs.m21 && _matrix.m22 == theirs.m22 &&
	       _matrix.tX == theirs.tX && _matrix.tY == theirs.tY;
}

- (NSString *)description
{
	return [NSString stringWithFormat:@"<%@: {%g %g %g %g %g %g}>", [self class],
		_matrix.m11, _matrix.m12, _matrix.m21, _matrix.m22, _matrix.tX, _matrix.tY];
}

@end
