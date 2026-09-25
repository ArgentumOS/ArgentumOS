/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSImageRep.h — ONE REPRESENTATION OF AN IMAGE, and the layer that owns the drawing seam (C8.9).
 *
 * THE HIERARCHY IS APPLE'S: a rep is a piece of image data with a known pixel layout, and `NSImage`
 * above it is a container of reps. This file is the ABSTRACT half — the metadata every rep has (how
 * many pixels, how big that is in points, which colour space) and the ONE drawing rule they share.
 *
 * AND THE DRAWING RULE IS WHY THIS IS A CLASS RATHER THAN A STRUCT: `-drawInRect:` needs the current
 * graphics context and an image to put in it, so the base asks itself for `-CGImage` — a member the
 * SUBCLASS answers — and draws that once. Every rep type therefore gets drawing for free and has only
 * one question to answer, which is the same shape as `NSGraphicsContext`'s read-through properties.
 *
 * WHAT IS NOT HERE, AND WHY, so the ledger's open rows are a decision rather than an omission:
 * `-representationUsingType:properties:` and `-TIFFRepresentation` need an ENCODER, and CoreGraphics
 * in this tree has only the two DECODERS (`CGImageCreateWithPNGDataProvider`,
 * `CGImageCreateWithJPEGDataProvider` — measured, and there is no `CGImageDestination` here); the
 * `NSImageRep` registry, `-canInitWithData:` and `+imageRepsWithData:` need the class registry that
 * does not exist; the NSCoding members need an archive format; `-colorSpace` needs `NSColorSpace`;
 * and the `NSBitmapImageFileType`/`NSImageRepLoadStatus`/`NSImageRepHintKey` enumerations ride with
 * the encoder and the registry they belong to. Each is absent rather than stubbed, and the reason is
 * written next to it.
 *
 * THE COLOUR-SPACE NAMES ARE STRINGS, as Apple's are, and they live here because a rep's designated
 * initialiser takes one. `NSColorSpaceName` is a typed extensible enum in Apple and a plain
 * `NSString *` here: the extra type is a spelling, and the constant is the substance.
 */

#import <CoreGraphics/CGImage.h>
#import <Foundation/NSGeometry.h>
#import <Foundation/NSObject.h>
#import <Foundation/NSString.h>

NS_ASSUME_NONNULL_BEGIN

/* THE TWO NAMES THIS LAYER CAN ANSWER FOR, which are the two colour spaces the pixels here can be
 * in — the same two CoreGraphics creates device spaces for. The calibrated and generic names are
 * absent because nothing distinguishes them at this layer. */
extern NSString *const NSDeviceRGBColorSpace;
extern NSString *const NSDeviceGrayColorSpace;

@interface NSImageRep : NSObject
{
	/* THE PIXELS ARE THE SUBCLASS'S; THESE ARE THE METADATA. `_size` is in POINTS and is deliberately
	 * independent of the pixel count — a rep can be 4 pixels wide and 100 points wide, which is
	 * resolution independence and the reason `setSize:` is not a synonym for the pixel dims. */
	NSSize _size;
	NSInteger _pixelsWide;
	NSInteger _pixelsHigh;
	NSString *_colorSpaceName;
}

@property NSInteger pixelsWide;
@property NSInteger pixelsHigh;
/* THE SIZE IN POINTS. A newly initialised rep answers its PIXEL dimensions, which is what Apple's do
 * — so a 4x4 rep is 4x4 points until someone says otherwise. */
@property NSSize size;
@property (copy) NSString *colorSpaceName;

/* THE DRAWING SEAM. `-drawInRect:` is implemented once here and every rep inherits it: it asks the
 * current context and `-CGImage`, which is the SUBCLASS's answer. `-draw` and `-drawAtPoint:` are the
 * two Apple calls that sit either side of it. All three answer NO when there is nothing to draw or no
 * context to draw into, rather than raising — the same choice `NSGraphicsContext` makes. */
- (BOOL)draw;
- (BOOL)drawAtPoint:(NSPoint)point;
- (BOOL)drawInRect:(NSRect)rect;

/* THE QUESTION A SUBCLASS ANSWERS, and the only one. The base answers NULL because an abstract rep has
 * no pixels; every concrete rep here overrides it, so `-drawInRect:` works for all of them at once. */
@property (nullable, readonly) CGImageRef CGImage;

@end

NS_ASSUME_NONNULL_END
