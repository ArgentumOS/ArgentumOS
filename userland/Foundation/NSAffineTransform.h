/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSAffineTransform.h — a 3×2 matrix as a value (W2h, §14).
 *
 * THE STRUCT IS THE API, so its FIELDS AND THEIR ORDER are theirs, not ours: `{m11, m12, m21, m22,
 * tX, tY}`, the third column of the 3×3 matrix being the fixed `[0 0 1]`. A point maps as
 * `x' = m11·x + m21·y + tX` and `y' = m12·x + m22·y + tY` — note which index multiplies which
 * coordinate, because getting that backwards is a transform that looks plausible and is wrong.
 *
 * APPEND AND PREPEND ARE NOT SYNONYMS, and the difference is the whole reason both exist:
 * `-appendTransform:` makes the argument the FIRST transform applied to a point, `-prependTransform:`
 * makes it the LAST. The check distinguishes them with a translation and a scale, which commute
 * only when nobody is looking.
 *
 * WHAT IS NOT, named: `-set`, `-concat` and `-transformBezierPath:` — THE APPKIT ADDITIONS. They act on a
 * graphics context or on an NSBezierPath, so they are declared and implemented in AppKit, as
 * `NSAffineTransformAdditions.{h,m}` (the tree's NSBundleAdditions is the same shape), NOT here: this
 * library does not import the drawing API, and a Foundation that reached into AppKit would be a cycle.
 * Also named: any raise from `-invert` on a singular matrix — the matrix is left unchanged there.
 */
#ifndef FOUNDATION_NSAFFINETRANSFORM_H
#define FOUNDATION_NSAFFINETRANSFORM_H

#import <Foundation/NSGeometry.h>
#import <Foundation/NSObject.h>

NS_ASSUME_NONNULL_BEGIN

typedef struct {
	CGFloat m11, m12, m21, m22;
	CGFloat tX, tY;
} NSAffineTransformStruct;

@interface NSAffineTransform : NSObject <NSCopying>
{
	NSAffineTransformStruct _matrix;
}

+ (NSAffineTransform *)transform;
- (instancetype)initWithTransform:(NSAffineTransform *)transform;

- (void)translateXBy:(CGFloat)deltaX yBy:(CGFloat)deltaY;
- (void)rotateByDegrees:(CGFloat)angle;
- (void)rotateByRadians:(CGFloat)angle;
- (void)scaleBy:(CGFloat)scale;
- (void)scaleXBy:(CGFloat)scaleX yBy:(CGFloat)scaleY;

- (void)appendTransform:(NSAffineTransform *)transform;
- (void)prependTransform:(NSAffineTransform *)transform;
- (void)invert;

- (NSPoint)transformPoint:(NSPoint)point;
- (NSSize)transformSize:(NSSize)size;

@property NSAffineTransformStruct transformStruct;

/* THE MATRIX, FIELD BY FIELD, WHICH IS THE POINT OF THEM: Apple exposes the six values directly, and the
 * names are the matrix positions (m_row_column) — `m21` multiplies y into x, which is the one people
 * expect to be `m12`. They read and write the SAME storage `transformStruct` moves, so a caller may mix
 * the two doors freely. */
@property CGFloat m11;
@property CGFloat m12;
@property CGFloat m21;
@property CGFloat m22;
@property CGFloat tX;
@property CGFloat tY;

@end

NS_ASSUME_NONNULL_END
#endif /* FOUNDATION_NSAFFINETRANSFORM_H */
