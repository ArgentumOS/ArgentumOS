/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSGraphicsContext.h — the AppKit bridge: a CoreGraphics context as an object (C8.1).
 *
 * THIS IS THE SEAM, NOT THE WIDGETS. It wraps a `CGContextRef` so AppKit-shaped code can hold
 * one, ask whether its drawing is flipped, and save and restore its state; everything it draws,
 * it draws by asking for `CGContext` and calling this tree's CoreGraphics. The drawing CLASSES
 * (`NSBezierPath`, `NSColor`, `NSImage`, …) are `cocoa-parity-plan.md`'s and are not here —
 * coregraphics-plan.md §7's C8 bullet says so, and §3 row 1 marks where the CG layer ends.
 *
 * THE SURFACE BELOW IS PINNED, NOT RECALLED, and that is why this header is shorter than
 * Apple's. `tools/appkit-sweep.py --refresh` read Apple's own navigator tree into
 * docs/reference/appkit-apple-surface.txt; every member here is a live row from it, and every
 * member Apple documents that is NOT here is named below with its reason — so a reader never has
 * to guess whether something was forgotten or refused.
 *
 * WHAT IS NOT HERE, AND WHY — four kinds of reason, not one:
 *
 *   DEPRECATED, so out by §1's fourth and fifth decisions: `graphicsPort`,
 *   `+graphicsContextWithGraphicsPort:flipped:`, `+setGraphicsState:` and
 *   `+graphicsContextWithWindow:`. **THIS IS THE FIRST THING THE PIN CHANGED**: §7's C8 bullet
 *   named `-graphicsPort` as C8's seam, and it is the one member Apple deprecates — a measured
 *   answer rather than an argument.
 *
 *   NO SUBSTRATE IN THIS TREE: `CIContext` (there is no Core Image here), and
 *   `+graphicsContextWithBitmapImageRep:` (which needs `NSBitmapImageRep` — §3 row 1's class).
 *
 *   AND FIVE THAT ARE C8 ACTING AS CG's ORACLE, which §8 says is the AppKit's job to be.
 *   `compositingOperation`/`NSCompositingOperation`, `imageInterpolation`/`NSImageInterpolation`
 *   and `colorRenderingIntent`/`NSColorRenderingIntent` are each a one-to-one match for a
 *   CoreGraphics enum this library has no CONTEXT SETTER for — `CGContextSetInterpolationQuality`
 *   and `CGContextSetRenderingIntent` are absent from CGContext.h. `shouldAntialias` and
 *   `patternPhase` are worse off: their SETTERS exist but the state is WRITE-ONLY, because
 *   `CGContextGetShouldAntialias` and `CGContextGetPatternPhase` do not, and an AppKit property
 *   needs both halves. Storing a copy here would disagree with the context the moment anyone drew
 *   through CG directly, so all five are DEFERRED to a CoreGraphics follow-on rather than
 *   approximated. Five gaps, found before this file had a line of implementation.
 *
 *   AND ONE THAT IS A FOLLOW-ON RATHER THAN A GAP: `attributes` and
 *   `+graphicsContextWithAttributes:` need the attribute KEYS, which the ledger now pins
 *   (`NSGraphicsContextDestinationAttributeName`, `NSGraphicsContextPDFFormat`,
 *   `NSGraphicsContextPSFormat`, `NSGraphicsContextRepresentationFormatAttributeName`) but whose
 *   VALUES are format names and a dictionary this slice does not build.
 *
 * THE CONTEXT IS NOT RETAINED, which is Apple's arrangement: the graphics context belongs to
 * whoever made it, and this object only points at it, so a caller keeps their own reference alive
 * for as long as they draw. Said out loud because the alternative is a silent over-release on one
 * side or a leak on the other.
 */
#ifndef APPKIT_NSGRAPHICSCONTEXT_H
#define APPKIT_NSGRAPHICSCONTEXT_H

#import <CoreGraphics/CGContext.h>
#import <Foundation/NSGeometry.h>
#import <Foundation/NSObject.h>

NS_ASSUME_NONNULL_BEGIN

@interface NSGraphicsContext : NSObject
{
	CGContextRef _context;      /* NOT owned — see the header note */
	BOOL _flipped;
	BOOL _drawingToScreen;
}

/* THE DOOR, and the only way to get one of these. Apple spells it
 * `+graphicsContextWithCGContext:flipped:` in Objective-C and `init(cgContext:flipped:)` in Swift;
 * the ledger holds the Objective-C spelling, and so does this header. */
+ (nullable NSGraphicsContext *)graphicsContextWithCGContext:(CGContextRef)cgContext
                                                    flipped:(BOOL)initialFlippedState;

/* THE CG CONTEXT, and the whole point of the object. NULL for an object that was never given one
 * (see the implementation's `-init` note). */
@property (nullable, readonly) CGContextRef CGContext;

/* THE CURRENT CONTEXT IS PER-THREAD, which is Apple's contract ("the current graphics context of
 * the current thread") and not a simplification: a current context is a property of the drawing
 * thread, and one global would let two threads' draws collide. `+setCurrentContext:` REPLACES it
 * rather than pushing — the stack is the save/restore pair below. */
+ (nullable NSGraphicsContext *)currentContext;
+ (void)setCurrentContext:(nullable NSGraphicsContext *)context;

/* THE COORDINATE SYSTEM, and the reason a caller asks: this library's bitmap contexts have y
 * increasing UPWARD while a view's own space usually does not, so the flip is carried here rather
 * than inferred later. */
@property (readonly, getter=isFlipped) BOOL flipped;

/* WHETHER THE DESTINATION IS THE SCREEN — and for every context THIS LIBRARY CAN MAKE it is NO,
 * because its CoreGraphics contexts are all bitmaps and there is no window- or screen-backed
 * `CGContextRef` here at all (the display is Xfb's, a separate path). Answering NO is therefore a
 * fact about our contexts rather than a guess about someone else's. */
@property (readonly, getter=isDrawingToScreen) BOOL drawingToScreen;
+ (BOOL)currentContextDrawingToScreen;

/* THE SAVE/RESTORE PAIR IN BOTH SPELLINGS, AND THEY ARE NOT THE SAME OPERATION.
 *
 *   -saveGraphicsState / -restoreGraphicsState act on the RECEIVER: the instance pair is
 *   `CGContextSaveGState`/`CGContextRestoreGState` on its own context — the C2 state stack (CTM,
 *   clip, colours, line state) that already existed.
 *
 *   +saveGraphicsState / +restoreGraphicsState act on the CURRENT CONTEXT *and* on a per-thread
 *   stack OF CONTEXTS: save pushes the current one and sends it a save; restore pops, makes the
 *   popped one current, and sends it a restore. Apple's own abstracts say exactly that ("Pops a
 *   graphics context from the per-thread stack, makes it current, and sends the context a restore
 *   graphics state message"), and the difference is observable: a caller who sets a current
 *   context, saves, sets another and restores gets the FIRST one back, which the instance pair
 *   cannot do.
 *
 * The stack is BOUNDED, and an overflow is REFUSED with a message rather than silently
 * overwriting — the rule the graphics state's dash array already follows. */
- (void)saveGraphicsState;
- (void)restoreGraphicsState;
+ (void)saveGraphicsState;
+ (void)restoreGraphicsState;

/* "Make sure the drawing has arrived." For a bitmap context it already has when this returns, so
 * this forwards to `CGContextFlush` and inherits its honest no-op-ness (CGContext.h says why).
 * It exists because a caller writing code against a window-backed context will call it. */
- (void)flushGraphics;

@end

NS_ASSUME_NONNULL_END

#endif /* APPKIT_NSGRAPHICSCONTEXT_H */
