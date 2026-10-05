/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * CGAffineTransform.h — the 3x2 affine transform and its operations (C1).
 *
 * THE FIELD NAMES AND ORDER ARE APPLE'S: a, b, c, d, tx, ty, with a point
 * transformed as
 *
 *     x' = a*x + c*y + tx        y' = b*x + d*y + ty
 *
 * so the matrix is [a b 0; c d 0; tx ty 1] and `CGAffineTransformIdentity` is
 * (1, 0, 0, 1, 0, 0). That mapping is not a choice — it is what makes the four
 * `CG…ApplyAffineTransform` functions agree with the Foundation's own
 * `-[NSAffineTransform transformPoint:]`, whose struct is the same matrix spelled
 * m11/m12/m21/m22/tX/tY. One arithmetic, two spellings.
 *
 * THE PRODUCT ORDER OF THE THREE "MODIFYING" FUNCTIONS IS THIS TREE'S STATEMENT,
 * because Apple's documentation does not state it. `CGAffineTransformTranslate`,
 * `Scale` and `Rotate` **concatenate the new operation on the LEFT of the existing
 * transform** — t' = op · t — which is the same order as the Foundation's
 * `-[NSAffineTransform appendTransform:]` (`fn_concat(arg, self)` in
 * nsaffinetransform.m), whose order was pinned by a check that would pass under
 * the other reading only by coincidence. It matters: a translation and a scale do
 * not commute, so the two readings give different points for the same program.
 *
 * AND `CGAffineTransformInvert` FOLLOWS APPLE'S DOCUMENTED CONTRACT for the case
 * that has no inverse: *"If the affine transform passed in cannot be inverted, the
 * affine transform is returned unchanged."* Not the identity — that is the
 * tempting guess, and the Foundation's `-invert` leaves the receiver alone for the
 * same reason.
 *
 * `CGAffineTransformDecompose`, `CGAffineTransformMakeWithComponents` and the
 * `CGAffineTransformComponents` record are NOT here: their conventions (the sign
 * of the rotation, what "shear" means when the transform is not a rotation-scale)
 * are the kind of contract this tree cannot check against a live reference, so they
 * stay `open` in the C0 ledger rather than being guessed at.
 */
#ifndef CORE_GRAPHICS_CGAFFINETRANSFORM_H
#define CORE_GRAPHICS_CGAFFINETRANSFORM_H

#include <CoreGraphics/CGBase.h>
#include <CoreGraphics/CGGeometry.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CGAffineTransform {
	CGFloat a;
	CGFloat b;
	CGFloat c;
	CGFloat d;
	CGFloat tx;
	CGFloat ty;
} CGAffineTransform;

/* The identity: no scale, no rotation, no translation. */
extern const CGAffineTransform CGAffineTransformIdentity;

/* Constructors. */
CGAffineTransform CGAffineTransformMake(CGFloat a, CGFloat b, CGFloat c, CGFloat d,
					CGFloat tx, CGFloat ty);
CGAffineTransform CGAffineTransformMakeTranslation(CGFloat tx, CGFloat ty);
CGAffineTransform CGAffineTransformMakeScale(CGFloat sx, CGFloat sy);
CGAffineTransform CGAffineTransformMakeRotation(CGFloat angle);

/* Combination and inversion — the two operations the modifiers are built from. */
CGAffineTransform CGAffineTransformConcat(CGAffineTransform t1, CGAffineTransform t2);
CGAffineTransform CGAffineTransformInvert(CGAffineTransform t);

/* The modifiers: the new operation concatenated ON THE LEFT (see the header note). */
CGAffineTransform CGAffineTransformTranslate(CGAffineTransform t, CGFloat tx, CGFloat ty);
CGAffineTransform CGAffineTransformScale(CGAffineTransform t, CGFloat sx, CGFloat sy);
CGAffineTransform CGAffineTransformRotate(CGAffineTransform t, CGFloat angle);

/* Comparison. Field-wise, so a NaN makes a transform unequal to itself — which is
 * what a caller comparing two computed transforms expects from `==`. */
int CGAffineTransformIsIdentity(CGAffineTransform t);
int CGAffineTransformEqualToTransform(CGAffineTransform t1, CGAffineTransform t2);

/* Application. A SIZE is a vector: it scales and rotates, and the translation does
 * not move it. A RECT has no general image under an affine map, so what comes back
 * is Apple's documented answer: the smallest rectangle containing the four
 * transformed corners. */
CGPoint CGPointApplyAffineTransform(CGPoint point, CGAffineTransform t);
CGSize CGSizeApplyAffineTransform(CGSize size, CGAffineTransform t);
CGRect CGRectApplyAffineTransform(CGRect rect, CGAffineTransform t);

#ifdef __cplusplus
}
#endif

#endif /* CORE_GRAPHICS_CGAFFINETRANSFORM_H */
