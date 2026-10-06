/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSCoderCGGeometry.m — the `CG`-spelled keyed coder doors (§63.54). Moved here from Foundation.
 *
 * ONE RULE FOR ALL TEN, and it is the rule the keyed family already had: BOX THE STRUCTURE IN AN `NSValue` AND
 * SEND IT THROUGH THE KEYED OBJECT DOOR. Both halves are public — `-encodeObject:forKey:` and
 * `-decodeObjectForKey:` are the keyed family's own published doors, and the box is built with
 * `+valueWithBytes:objCType:` rather than with any `CG`-spelled `NSValue` door, so this file does NOT depend on
 * `NSValueCGGeometry` even though both live in this tier. That is deliberate: a tier's files depending on each
 * other for no reason is how a boundary becomes a habit.
 *
 * AND A MISSING KEY ANSWERS ZERO, which is the reading of an absent box rather than a raise: the decode side of
 * a keyed archive already separates "no such key" (nil) from "corrupt value" (a raise inside the reader), and a
 * geometry door has nothing better to say than the zero struct.
 */

#import <CoreGraphics/NSCoderCGGeometry.h>
#import <Foundation/NSValue.h>

@implementation NSCoder (NSCoderCGGeometryAdditions)

- (void)encodeCGPoint:(CGPoint)point forKey:(NSString *)key
{
	[self encodeObject:[NSValue valueWithBytes:&point objCType:@encode(CGPoint)] forKey:key];
}

- (CGPoint)decodeCGPointForKey:(NSString *)key
{
	NSValue *value = [self decodeObjectForKey:key];
	CGPoint point = { 0.0, 0.0 };

	if (value != nil) {
		[value getValue:&point];
	}
	return point;
}

- (void)encodeCGSize:(CGSize)size forKey:(NSString *)key
{
	[self encodeObject:[NSValue valueWithBytes:&size objCType:@encode(CGSize)] forKey:key];
}

- (CGSize)decodeCGSizeForKey:(NSString *)key
{
	NSValue *value = [self decodeObjectForKey:key];
	CGSize size = { 0.0, 0.0 };

	if (value != nil) {
		[value getValue:&size];
	}
	return size;
}

- (void)encodeCGRect:(CGRect)rect forKey:(NSString *)key
{
	[self encodeObject:[NSValue valueWithBytes:&rect objCType:@encode(CGRect)] forKey:key];
}

- (CGRect)decodeCGRectForKey:(NSString *)key
{
	NSValue *value = [self decodeObjectForKey:key];
	CGRect rect = { { 0.0, 0.0 }, { 0.0, 0.0 } };

	if (value != nil) {
		[value getValue:&rect];
	}
	return rect;
}

/* THE TWO VECTOR DOORS STOOD HERE AND WENT WITH `CGVector` (2026-10-05): macOS 10.7, and a keyed
 * door names the type it writes — which is also why the archived-geometry probe's round trip
 * measures four pairs instead of five now. */

- (void)encodeCGAffineTransform:(CGAffineTransform)transform forKey:(NSString *)key
{
	[self encodeObject:[NSValue valueWithBytes:&transform objCType:@encode(CGAffineTransform)] forKey:key];
}

- (CGAffineTransform)decodeCGAffineTransformForKey:(NSString *)key
{
	NSValue *value = [self decodeObjectForKey:key];
	CGAffineTransform transform = { 1.0, 0.0, 0.0, 1.0, 0.0, 0.0 };

	if (value != nil) {
		[value getValue:&transform];
	}
	return transform;
}

@end
