/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSAffineTransformAdditions.m — the three AppKit doors' IMPLEMENTATION (§63.187). Their DECLARATIONS are
 * Foundation's, on the class, as Apple's are; what they need (a graphics context, an NSBezierPath) is here.
 */
#import <AppKit/NSGraphicsContext.h>
#import <CoreGraphics/CGContext.h>
#import <Foundation/NSAffineTransform.h>

static CGAffineTransform fn_cg_affine(NSAffineTransform *transform)
{
	NSAffineTransformStruct s = [transform transformStruct];

	return CGAffineTransformMake(s.m11, s.m12, s.m21, s.m22, s.tX, s.tY);
}

static CGContextRef fn_current_cg_context(void)
{
	NSGraphicsContext *context = [NSGraphicsContext currentContext];

	return context != nil ? context.CGContext : NULL;
}

@implementation NSAffineTransform (NSAffineTransformAppKitAdditions)

- (void)set
{
	CGContextRef cg = fn_current_cg_context();

	if (cg == NULL) {
		return;
	}
	CGContextConcatCTM(cg, CGAffineTransformInvert(CGContextGetCTM(cg)));
	CGContextConcatCTM(cg, fn_cg_affine(self));
}

- (void)concat
{
	CGContextRef cg = fn_current_cg_context();

	if (cg == NULL) {
		return;
	}
	CGContextConcatCTM(cg, fn_cg_affine(self));
}

- (NSBezierPath *)transformBezierPath:(NSBezierPath *)path
{
	NSBezierPath *copy = [path copy];

	[copy transformUsingAffineTransform:self];
	return [copy autorelease];
}

@end
