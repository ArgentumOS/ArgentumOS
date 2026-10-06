/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSValueCGGeometry.h — the `CG`-spelled NSValue geometry doors, in the tier that owns the types (§63.53).
 *
 * ⚠ THEY MOVED HERE FROM FOUNDATION, AND THE MEASUREMENT IS WHAT PUT THEM HERE RATHER THAN IN FOUNDATION OR
 * NOWHERE. Apple's headers were read for both spellings of the same idea (the user's directive of 2026-10-01:
 * *"removing from our Foundation implementation anything which does not belong in Foundation … CoreGraphics or
 * the like are not"*):
 *
 *   * **THE `NS`-SPELLED DOORS ARE FOUNDATION'S AND STAY THERE** — macOS's `Foundation/NSGeometry.h` declares
 *     `+valueWithPoint:`, `-pointValue` and `-decodePointForKey:`.
 *   * **THE `CG`-SPELLED ONES ARE IN NO FOUNDATION HEADER OF EITHER SDK.** `+valueWithCGPoint:`,
 *     `-CGPointValue` and their siblings appear in neither the macOS 14.5 Foundation headers nor the iOS 16.5
 *     ones; on iOS they are **UIKit's** (`UIGeometry.h`). This tree has no UIKit, and its CoreGraphics tier is
 *     where the `CG` types live — so that is where the doors go.
 *
 * AND THEY ARE IMPLEMENTED OVER **PUBLIC** `NSValue` API ONLY (`+valueWithBytes:objCType:` and `-getValue:`),
 * which is the whole point of the move: Foundation's private funnel (`-fnInitWithBytes:`) is not reachable from
 * another tier, and reaching for it would put the boundary back where it was.
 */

#ifndef COREGRAPHICS_NSVALUECGGEOMETRY_H
#define COREGRAPHICS_NSVALUECGGEOMETRY_H

#import <Foundation/NSValue.h>
#import <CoreGraphics/CGGeometry.h>
#import <CoreGraphics/CGAffineTransform.h>

NS_ASSUME_NONNULL_BEGIN

@interface NSValue (NSValueCGGeometryAdditions)

/* The type IS the encoding: `@encode(CGPoint)` is `"{CGPoint=dd}"`, so each door is `+valueWithBytes:objCType:`
 * over its struct and each reader is `-getValue:`'s shape over the same encoding. */
+ (NSValue *)valueWithCGPoint:(CGPoint)point;
+ (NSValue *)valueWithCGSize:(CGSize)size;
+ (NSValue *)valueWithCGRect:(CGRect)rect;
+ (NSValue *)valueWithCGAffineTransform:(CGAffineTransform)transform;

- (CGPoint)CGPointValue;
- (CGSize)CGSizeValue;
- (CGRect)CGRectValue;
/* `+valueWithCGVector:` AND `-CGVectorValue` WENT WITH `CGVector` (2026-10-05): the type is macOS
 * 10.7 and this category cannot box a type this surface does not have. The four that remain are
 * the era's own — CGPoint, CGSize, CGRect and CGAffineTransform — which is why the header's note
 * above counts the doors it has rather than the ones Apple's index lists. */
- (CGAffineTransform)CGAffineTransformValue;

@end

NS_ASSUME_NONNULL_END

#endif /* COREGRAPHICS_NSVALUECGGEOMETRY_H */
