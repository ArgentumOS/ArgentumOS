/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSImage.h — a CONTAINER OF REPRESENTATIONS, and the class callers actually pass around (C8.10).
 *
 * THE SHAPE IS APPLE'S AND THE SPLIT IS WHY THERE ARE TWO CLASSES: an `NSImage` is not pixels, it is a
 * NAMED THING with a SIZE that answers drawing requests by choosing one of its representations
 * (`NSImageRep`) to hand the work to. That is what makes an image resolution-independent and what makes
 * `-setSize:` mean something different from a rep's pixels: the SIZE is the image's, and the pixels
 * belong to whichever rep draws it.
 *
 * SO `-size` HAS TWO SOURCES AND THE PRECEDENCE IS THE POINT: an EXPLICIT size (from `-initWithSize:`
 * or `-setSize:`) wins; otherwise the image answers the FIRST representation's size, which is what a
 * caller who loaded a PNG expects. `-isValid` asks a different question — whether there is anything to
 * draw at all — and it is NO for an image with a size and no representations, which is exactly the
 * empty canvas `+image` produces.
 *
 * WHAT IS NOT HERE, AND WHY:
 *   `-lockFocus`/`-unlockFocus` are the next slice rather than this one, and the reason is a FORMAT
 *   mismatch that deserves its own turn: this tree's bitmap context is premultiplied-FIRST
 *   little-endian (a measured refusal in `CGBitmapContextCreate`) and a rep's pixels are
 *   premultiplied-LAST, so an offscreen canvas needs a channel swizzle between the two rather than a
 *   wrap.
 *   THE ENCODING HALF IS ABSENT FOR C8.9's ALREADY-MEASURED REASON: `-TIFFRepresentation` and
 *   `-representationUsingType` need an ENCODER, and this tree's CoreGraphics has only the two
 *   decoders.
 *   `+imageNamed:`, `-initWithContentsOfURL:`, the name/alias members and the `NSImageDelegate` surface
 *   each need a bundle, a search path or an URL-loading subsystem this layer does not have; the
 *   `NSImageSymbolConfiguration` and `NSImageSymbol*` system-image family needs the SF Symbols catalogue
 *   and an appearance system; `-drawInRect:fromRect:operation:fraction:`, the composite members and
 *   `-recache`/`-cacheMode` need the compositing-operation surface and a cache model. Each is absent
 *   rather than stubbed, and the reason is written next to it.
 */

#import <AppKit/NSImageRep.h>
#import <CoreGraphics/CGImage.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSData.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSGeometry.h>

NS_ASSUME_NONNULL_BEGIN

@class NSGraphicsContext;
@class NSBitmapImageRep;

@interface NSImage : NSObject
{
	/* THE REPS, AS AN IMMUTABLE SNAPSHOT: `-representations` hands a caller an array it cannot use to
	 * reach in and change this image behind its back, which is the same rule Apple's accessor keeps. */
	NSArray *_reps;
	NSSize _size;
	BOOL _sizeIsExplicit;
	/* THE OFFSCREEN CANVAS, AND THERE ARE TWO BUFFERS ON PURPOSE. `_canvas` is what the bitmap context
	 * was created OVER, in this tree's premultiplied-FIRST little-endian format; `_focusBuffer` is the
	 * CHANNEL-SWIZZLED copy that a representative BORROWS, in the premultiplied-LAST order a rep is
	 * built with. The two layers disagree about channel order and neither one can un-disagree, so the
	 * swizzle happens once, at `-unlockFocus`, rather than on a per-draw path. */
	unsigned char *_canvas;
	unsigned char *_focusBuffer;
	NSInteger _focusW;
	NSInteger _focusH;
	CGContextRef _focusContext;
	NSGraphicsContext *_focusGState;       /* RETAINED while locked */
	NSGraphicsContext *_gstatePrevious;    /* RETAINED, to put back at -unlockFocus */
	NSBitmapImageRep *_focusRep;           /* NOT RETAINED HERE: the representation list owns it */
}

/* AN EMPTY CANVAS — no representations, no size, `-isValid` NO. */
+ (instancetype)image;

- (instancetype)initWithSize:(NSSize)size;
- (nullable instancetype)initWithData:(NSData *)data;
- (nullable instancetype)initWithContentsOfFile:(NSString *)path;
- (nullable instancetype)initWithCGImage:(CGImageRef)cgImage size:(NSSize)size;

/* THE REPRESENTATIONS. `-addRepresentation:` RETAINS what it is given, and the FIRST one added is the
 * one `-size` answers from when no explicit size was set. */
- (void)addRepresentation:(NSImageRep *)imageRep;
- (void)removeRepresentation:(NSImageRep *)imageRep;
/* A PLAIN `NSArray`, NOT `NSArray<NSImageRep *> *`: this tree's Foundation has no lightweight generics,
 * so the element type is a comment rather than a type argument. */
@property (readonly, copy) NSArray *representations;

/* THE SIZE IN POINTS, with the precedence described above. */
@property NSSize size;

/* WHETHER THERE IS ANYTHING TO DRAW. An image with a size and NO representations is a valid canvas and
 * an INVALID image, which is the distinction that makes this worth asking. */
@property (readonly, getter=isValid) BOOL valid;

/* WHICH REPRESENTATION WOULD DRAW A RECT. The rule here is the CLOSEST in points, with the first
 * winning ties — stated because Apple leaves it to the class to decide and this is the decision. */
- (nullable NSImageRep *)bestRepresentationForRect:(NSRect)rect
					   context:(nullable NSGraphicsContext *)referenceContext
					     hints:(nullable NSDictionary *)hints;

/* AND DRAWING, WHICH IS THE WHOLE POINT: the chosen representation draws, SCALED to the rectangle.
 * Both answer NO when there is nothing to draw rather than raising. */
- (BOOL)drawAtPoint:(NSPoint)point;
- (BOOL)drawInRect:(NSRect)rect;

/* THE OFFSCREEN CANVAS, WHICH IS THE CLASSIC APPKIT IDIOM AND THE REASON THIS CLASS DOES NOT NEED A
 * WINDOW TO BE USEFUL: `-lockFocus` makes a canvas of the image's SIZE, makes a context over it, and
 * makes that context CURRENT; `-unlockFocus` puts the previous context back and turns the canvas into a
 * representation. The pixels drawn in between are the image.
 *
 * A CANVAS IS ONE PIXEL PER POINT HERE (1:1), which is a stated simplification: Apple scales by the
 * destination screen's scale factor, and this layer has no screen to ask. `-lockFocus` on an image with
 * no size is REFUSED by name rather than making a zero-pixel canvas, and `-unlockFocus` without a lock
 * is a no-op rather than a raise. */
- (void)lockFocus;
- (void)unlockFocus;

@end

NS_ASSUME_NONNULL_END
