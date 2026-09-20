/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * CGAffineTransform.c — the affine arithmetic (C1).
 *
 * THE ONE MULTIPLICATION, and the product order it fixes. `fn_concat(left, right)`
 * is left·right, and applying it to a point applies `right` first. That is the same
 * function, in the same shape, as the Foundation's `fn_concat` in
 * nsaffinetransform.m — deliberately, because the two frameworks describe one
 * matrix and a second multiplication would be a second answer.
 *
 * Everything the three modifiers do is `fn_concat(step, t)`: the new operation on
 * the LEFT of the existing transform. See CGAffineTransform.h for why that order is
 * stated here rather than copied from Apple's documentation, which does not state
 * it at all.
 */
#include <CoreGraphics/CGAffineTransform.h>

#include <math.h>

const CGAffineTransform CGAffineTransformIdentity = { 1.0, 0.0, 0.0, 1.0, 0.0, 0.0 };

/* left·right: `left` is applied AFTER `right`. */
static CGAffineTransform fn_concat(CGAffineTransform left, CGAffineTransform right)
{
	CGAffineTransform out;

	out.a = left.a * right.a + left.c * right.b;
	out.b = left.b * right.a + left.d * right.b;
	out.c = left.a * right.c + left.c * right.d;
	out.d = left.b * right.c + left.d * right.d;
	out.tx = left.a * right.tx + left.c * right.ty + left.tx;
	out.ty = left.b * right.tx + left.d * right.ty + left.ty;
	return out;
}

CGAffineTransform CGAffineTransformMake(CGFloat a, CGFloat b, CGFloat c, CGFloat d,
					CGFloat tx, CGFloat ty)
{
	CGAffineTransform t;

	t.a = a;
	t.b = b;
	t.c = c;
	t.d = d;
	t.tx = tx;
	t.ty = ty;
	return t;
}

CGAffineTransform CGAffineTransformMakeTranslation(CGFloat tx, CGFloat ty)
{
	return CGAffineTransformMake(1.0, 0.0, 0.0, 1.0, tx, ty);
}

CGAffineTransform CGAffineTransformMakeScale(CGFloat sx, CGFloat sy)
{
	return CGAffineTransformMake(sx, 0.0, 0.0, sy, 0.0, 0.0);
}

/*
 * The rotation matrix, in the convention the rest of the file (and the Foundation's
 * -rotateByRadians:) uses: c = cos, b = sin, c-column = -sin. A positive angle turns
 * the +x axis toward +y, which is counter-clockwise in a y-up space and clockwise on
 * a screen whose y grows downward — the same transform either way, and a property of
 * the coordinate space rather than of this function.
 */
CGAffineTransform CGAffineTransformMakeRotation(CGFloat angle)
{
	CGFloat c = cos(angle);
	CGFloat s = sin(angle);

	return CGAffineTransformMake(c, s, -s, c, 0.0, 0.0);
}

CGAffineTransform CGAffineTransformConcat(CGAffineTransform t1, CGAffineTransform t2)
{
	return fn_concat(t1, t2);
}

/*
 * A SINGULAR TRANSFORM IS RETURNED UNCHANGED, which is Apple's documented contract
 * and not a fallback: the inverse of a matrix with determinant 0 does not exist, the
 * documented answer for that case is the input, and inventing an identity here would
 * silently move every point that went through the result.
 */
CGAffineTransform CGAffineTransformInvert(CGAffineTransform t)
{
	CGFloat determinant = t.a * t.d - t.b * t.c;
	CGAffineTransform out;

	if (determinant == 0.0) {
		return t;
	}
	out.a = t.d / determinant;
	out.b = -t.b / determinant;
	out.c = -t.c / determinant;
	out.d = t.a / determinant;
	out.tx = -(out.a * t.tx + out.c * t.ty);
	out.ty = -(out.b * t.tx + out.d * t.ty);
	return out;
}

CGAffineTransform CGAffineTransformTranslate(CGAffineTransform t, CGFloat tx, CGFloat ty)
{
	return fn_concat(CGAffineTransformMakeTranslation(tx, ty), t);
}

CGAffineTransform CGAffineTransformScale(CGAffineTransform t, CGFloat sx, CGFloat sy)
{
	return fn_concat(CGAffineTransformMakeScale(sx, sy), t);
}

CGAffineTransform CGAffineTransformRotate(CGAffineTransform t, CGFloat angle)
{
	return fn_concat(CGAffineTransformMakeRotation(angle), t);
}

int CGAffineTransformIsIdentity(CGAffineTransform t)
{
	return t.a == 1.0 && t.b == 0.0 && t.c == 0.0 && t.d == 1.0 && t.tx == 0.0 &&
	       t.ty == 0.0;
}

int CGAffineTransformEqualToTransform(CGAffineTransform t1, CGAffineTransform t2)
{
	return t1.a == t2.a && t1.b == t2.b && t1.c == t2.c && t1.d == t2.d &&
	       t1.tx == t2.tx && t1.ty == t2.ty;
}

CGPoint CGPointApplyAffineTransform(CGPoint point, CGAffineTransform t)
{
	CGPoint out;

	out.x = t.a * point.x + t.c * point.y + t.tx;
	out.y = t.b * point.x + t.d * point.y + t.ty;
	return out;
}

/* A size is a VECTOR: it is scaled and rotated, and the translation does not move it
 * because a vector has no position. Apple states this, and it is the whole
 * difference between this function and the one above. */
CGSize CGSizeApplyAffineTransform(CGSize size, CGAffineTransform t)
{
	CGSize out;

	out.width = t.a * size.width + t.c * size.height;
	out.height = t.b * size.width + t.d * size.height;
	return out;
}

/*
 * APPLE'S DOCUMENTED ANSWER, and the reason a rectangle needs one: an affine map
 * does not preserve rectangles, so the image of one is a quadrilateral. What comes
 * back is *"the smallest rectangle that contains the transformed corner points"* —
 * four transformed corners, then min/max on each axis.
 */
CGRect CGRectApplyAffineTransform(CGRect rect, CGAffineTransform t)
{
	CGPoint corners[4];
	CGRect out;
	int i;

	corners[0] = CGPointApplyAffineTransform(rect.origin, t);
	corners[1] = CGPointApplyAffineTransform(CGPointMake(rect.origin.x + rect.size.width,
							    rect.origin.y), t);
	corners[2] = CGPointApplyAffineTransform(CGPointMake(rect.origin.x,
							    rect.origin.y + rect.size.height), t);
	corners[3] = CGPointApplyAffineTransform(CGPointMake(rect.origin.x + rect.size.width,
							    rect.origin.y + rect.size.height), t);
	out.origin = corners[0];
	out.size.width = 0.0;
	out.size.height = 0.0;
	for (i = 1; i < 4; i++) {
		if (corners[i].x < out.origin.x) {
			out.origin.x = corners[i].x;
		}
		if (corners[i].y < out.origin.y) {
			out.origin.y = corners[i].y;
		}
		if (corners[i].x > out.size.width) {
			out.size.width = corners[i].x;
		}
		if (corners[i].y > out.size.height) {
			out.size.height = corners[i].y;
		}
	}
	out.size.width -= out.origin.x;
	out.size.height -= out.origin.y;
	return out;
}
