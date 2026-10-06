/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSCoderCGGeometry.h — the `CG`-spelled KEYED coder doors, in the tier that owns the types (§63.54).
 *
 * ⚠ THEY MOVED HERE FROM FOUNDATION, on the same measurement §63.53 made for `NSValue`: the `CG` SPELLINGS ARE
 * IN NO FOUNDATION HEADER OF EITHER SDK (not macOS 14.5, not iOS 16.5), while `-encodePoint:forKey:` IS (macOS's
 * `Foundation/NSGeometry.h`). The two spellings of one idea therefore belong to two tiers, and a probe cannot
 * tell them apart — `NSPoint` IS `CGPoint` here, so both box the same bytes under the same encoding.
 *
 * AND THE IMPLEMENTATION GOES OVER PUBLIC API ONLY, which is what lets it leave Foundation at all:
 * `-encodeObject:forKey:` / `-decodeObjectForKey:` are the KEYED FAMILY's own published doors, and
 * `+valueWithBytes:objCType:` / `-getValue:` are `NSValue`'s. Nothing here reaches for the private funnel that
 * §63.53 called out.
 *
 * ⚠ AND IT ALSO REPAIRED A REAL DEFECT THE PREVIOUS MOVE LEFT BEHIND: `NSKeyedArchiver` implemented these ten
 * by calling the `CG`-spelled `NSValue` doors, so moving those out of Foundation left **ten call sites in
 * Foundation pointing at selectors that no longer existed** — and an Objective-C message send emits NO
 * UNDEFINED SYMBOL, so the linker could not see it, and the build only passed because the object was STALE.
 * The forced rebuild is what showed it, and the ten implementations and their ten call sites left Foundation
 * TOGETHER.
 */

#ifndef COREGRAPHICS_NSCODERCGGEOMETRY_H
#define COREGRAPHICS_NSCODERCGGEOMETRY_H

#import <Foundation/NSCoder.h>
#import <CoreGraphics/CGGeometry.h>
#import <CoreGraphics/CGAffineTransform.h>

NS_ASSUME_NONNULL_BEGIN

/* A keyed archive carries objects, not C structures, so each door writes its structure WRAPPED IN AN `NSValue`
 * under the key and reads the box back — which is why these are KEYED doors: the value travels BY NAME. */
@interface NSCoder (NSCoderCGGeometryAdditions)

- (void)encodeCGPoint:(CGPoint)point forKey:(NSString *)key;
- (CGPoint)decodeCGPointForKey:(NSString *)key;
- (void)encodeCGSize:(CGSize)size forKey:(NSString *)key;
- (CGSize)decodeCGSizeForKey:(NSString *)key;
- (void)encodeCGRect:(CGRect)rect forKey:(NSString *)key;
- (CGRect)decodeCGRectForKey:(NSString *)key;
/* THE TWO VECTOR DOORS WENT WITH `CGVector` (2026-10-05) — macOS 10.7, and a keyed door names the
 * type it writes. The pairs that remain are CGPoint, CGSize, CGRect and CGAffineTransform. */
- (void)encodeCGAffineTransform:(CGAffineTransform)transform forKey:(NSString *)key;
- (CGAffineTransform)decodeCGAffineTransformForKey:(NSString *)key;

@end

NS_ASSUME_NONNULL_END

#endif /* COREGRAPHICS_NSCODERCGGEOMETRY_H */
