/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSGraphicsContext.h — the AppKit bridge: a CoreGraphics context as an object (C8.1).
 *
 * THIS IS THE SEAM, NOT THE WIDGETS. It wraps a `CGContextRef` so AppKit-shaped code can hold one,
 * ask whether its drawing is flipped, save and restore its state, and set the rendering options a
 * view's drawing inherits; everything it draws, it draws by asking for `CGContext` and calling this
 * tree's CoreGraphics. The drawing CLASSES (`NSBezierPath`, `NSColor`, `NSImage`, …) are
 * `cocoa-parity-plan.md`'s and are not here — coregraphics-plan.md §7's C8 bullet says so, and §3
 * row 1 marks where the CG layer ends.
 *
 * THE SURFACE BELOW IS PINNED, NOT RECALLED: `tools/appkit-sweep.py --refresh` read Apple's own
 * navigator tree into docs/reference/appkit-apple-surface.txt, every member here is a live row from
 * it, and every member Apple documents that is NOT here is named below with its reason.
 *
 * WHAT IS NOT HERE, AND WHY — three kinds of reason, not one:
 *
 *   DEPRECATED, so out by §1's fourth and fifth decisions: `graphicsPort`,
 *   `+graphicsContextWithGraphicsPort:flipped:`, `+setGraphicsState:` and
 *   `+graphicsContextWithWindow:`. THIS IS THE FIRST THING THE PIN CHANGED: §7's C8 bullet named
 *   `-graphicsPort` as C8's seam, and it is the one member Apple deprecates — measured, not argued.
 *
 *   NO SUBSTRATE IN THIS TREE: `CIContext` (no Core Image here), and
 *   `+graphicsContextWithBitmapImageRep:` (needs `NSBitmapImageRep` — §3 row 1's class).
 *
 *   AND ONE THAT IS REAL COREGRAPHICS WORK, WITH ITS REASON MEASURED RATHER THAN ASSUMED:
 *   `colorRenderingIntent` needs `CGContextSetRenderingIntent`, which **Apple HAS** (an `open` row
 *   here), and what keeps it deferred is not the missing function but that nothing would CONSUME it -
 *   an intent names how a colour is converted DURING drawing, and this library's drawing does not
 *   convert (fills composite device numbers, images composite their own bytes).
 *
 *   AND THE GAP THAT WAS REAL IS CLOSED. `imageInterpolation` needed
 *   `CGContextSetInterpolationQuality`, `CGContextGetInterpolationQuality` and
 *   `CGInterpolationQuality` - **all three of which Apple has and this tree did not** - AND a sampler
 *   that honours them, because the image blit was nearest-only. That CoreGraphics slice landed
 *   (`17343a35`): a bilinear sampler that premultiplies each texel before weighting, with
 *   `kCGInterpolationLow` and `kCGInterpolationHigh` refused by name because this library has two
 *   filters and not four. **AND THIS IS THE ONE OPTION THAT READS THROUGH TO THE CONTEXT INSTEAD OF
 *   BEING STORED** - see its declaration below. The asymmetry is not an oversight: CoreGraphics has a
 *   getter for THIS one, which is exactly what made it a gap, and it is the same measurement that
 *   made the other three stored options the correct arrangement.
 *
 *     * `imageInterpolation` needs `CGContextSetInterpolationQuality` AND
 *       `CGContextGetInterpolationQuality`, and **Apple HAS both** (both are `open` rows in this
 *       tree's CoreGraphics ledger). So this one IS a genuine gap on OUR side — our `CGContext.h`
 *       has neither — and closing it means the two functions, the `CGInterpolationQuality` enum,
 *       **and bilinear sampling in the image blit**, which is nearest-only today (C5's probe says
 *       so). A setter whose value no drawing reads is a silent no-op, so this is a CoreGraphics
 *       slice rather than a one-line unblock.
 *     * `colorRenderingIntent` needs `CGContextSetRenderingIntent`, which **Apple also HAS** (an
 *       `open` row here). What keeps it deferred is not the missing function but that nothing would
 *       CONSUME it: an intent names how a colour is converted DURING drawing, and this library's
 *       drawing does not convert — fills composite device numbers and images composite their own
 *       bytes.
 *
 *   A CORRECTION, KEPT RATHER THAN QUIETLY DELETED, because it was the kind of mistake that looks
 *   like rigour. The first version of this header deferred FIVE members — the two above plus
 *   `compositingOperation`, `shouldAntialias` and `patternPhase` — reasoning that "an AppKit
 *   property needs both halves" and that `CGContextGetShouldAntialias`,
 *   `CGContextGetPatternPhase` and `CGContextGetBlendMode` do not exist. **Asked of Apple's index
 *   instead of of memory, those three getters do not exist in APPLE EITHER** — the ledger's only
 *   context getter in that family is `CGContextGetInterpolationQuality`. So Apple's own
 *   `NSGraphicsContext` cannot be reading them back from the context: **it stores them, and its
 *   setter applies them**. Storing is not an approximation here, it is the arrangement — and the
 *   objection that "a stored copy would disagree with the context the moment anyone drew through CG
 *   directly" describes Apple's behaviour too. Those three are therefore SHIPPED, below.
 *
 *   AND ONE THAT IS A FOLLOW-ON RATHER THAN A GAP: `attributes` and
 *   `+graphicsContextWithAttributes:` need the attribute KEYS, which the ledger pins
 *   (`NSGraphicsContextDestinationAttributeName`, `NSGraphicsContextPDFFormat`,
 *   `NSGraphicsContextPSFormat`,
 *   `NSGraphicsContextRepresentationFormatAttributeName`) but whose VALUES are format names and a
 *   dictionary this slice does not build.
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

/*
 * THE COMPOSITING OPERATION IS 28 LIVE NAMES HERE, WHERE APPLE'S INDEX LISTS 30 PLUS 30 LEGACY
 * ALIASES. Apple files it under `NSGraphicsContext`, which is why it is declared in this header
 * rather than one of its own. Every case maps one-to-one onto `CGBlendMode` —
 * `NSCompositingOperationSourceOver` IS `kCGBlendModeNormal`, and the names track each other all the
 * way down — WITH ONE EXCEPTION: `NSCompositingOperationPlusDarker` has no pixman operator and
 * therefore no `kCGBlendModePlusDarker` in this tree (`CGContext.h` says why that one is
 * deliberately absent). So the case is DECLARED — a caller's `switch` needs the name — and REFUSED
 * BY NAME by the setter below rather than silently drawing the nearest thing.
 *
 * `NSCompositingOperationHighlight` IS NOT HERE, nor is any of the 30 legacy `NSComposite…` names:
 * the ledger has them all as `struck` rows (Apple deprecates them), which is the same policy that
 * keeps `graphicsPort` out.
 */
typedef enum {
	NSCompositingOperationClear = 0,
	NSCompositingOperationCopy,
	NSCompositingOperationSourceOver,
	NSCompositingOperationSourceIn,
	NSCompositingOperationSourceOut,
	NSCompositingOperationSourceAtop,
	NSCompositingOperationDestinationOver,
	NSCompositingOperationDestinationIn,
	NSCompositingOperationDestinationOut,
	NSCompositingOperationDestinationAtop,
	NSCompositingOperationXOR,
	NSCompositingOperationPlusDarker,
	NSCompositingOperationPlusLighter,
	NSCompositingOperationMultiply,
	NSCompositingOperationScreen,
	NSCompositingOperationOverlay,
	NSCompositingOperationDarken,
	NSCompositingOperationLighten,
	NSCompositingOperationColorDodge,
	NSCompositingOperationColorBurn,
	NSCompositingOperationSoftLight,
	NSCompositingOperationHardLight,
	NSCompositingOperationDifference,
	NSCompositingOperationExclusion,
	NSCompositingOperationHue,
	NSCompositingOperationSaturation,
	NSCompositingOperationColor,
	NSCompositingOperationLuminosity
} NSCompositingOperation;

/*
 * HOW AN IMAGE IS SAMPLED WHEN IT IS SCALED - a one-to-one mapping onto `CGInterpolationQuality`, so
 * the two levels this library cannot honour (`Low` and `High`) inherit THAT enum's refusals rather
 * than getting new ones: the property below forwards to CoreGraphics, which refuses them. All five
 * names are declared, because a caller's `switch` needs them and the refusal is the setter's job.
 */
typedef enum {
	NSImageInterpolationDefault = 0,
	NSImageInterpolationNone,
	NSImageInterpolationLow,
	NSImageInterpolationMedium,
	NSImageInterpolationHigh
} NSImageInterpolation;

@interface NSGraphicsContext : NSObject
{
	CGContextRef _context;      /* NOT owned — see the header note */
	BOOL _flipped;
	BOOL _drawingToScreen;
	/* THE THREE STORED OPTIONS, and storing them is APPLE'S ARRANGEMENT rather than a shortcut:
	 * CoreGraphics exposes no getter for any of the three (measured — see the correction above), so
	 * there is nothing to read back and the object must remember what it was asked for. The SETTERS
	 * are what reach the context. */
	BOOL _shouldAntialias;
	NSPoint _patternPhase;
	NSCompositingOperation _compositingOperation;
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

/*
 * THE RENDERING OPTIONS. Each SETTER reaches the context — `CGContextSetShouldAntialias`,
 * `CGContextSetPatternPhase` (which takes a SIZE, so the point is converted) and
 * `CGContextSetBlendMode` — and each GETTER answers from what this object stored. THE DEFAULTS ARE
 * THE CONTEXT'S: antialiasing on, phase (0, 0), and `SourceOver`, which is `kCGBlendModeNormal`.
 */
@property BOOL shouldAntialias;
/* THE OFFSET APPLIED TO A PATTERN, in the user space of whatever draws next. A POINT here and a
 * SIZE at the CoreGraphics call, which is Apple's own asymmetry and not a slip. */
@property NSPoint patternPhase;
/* `NSCompositingOperationPlusDarker` IS REFUSED BY NAME rather than approximated — see the enum. */
@property NSCompositingOperation compositingOperation;

/*
 * AND THIS ONE IS NOT STORED, WHICH IS ITS WHOLE DIFFERENCE FROM THE THREE ABOVE. They remember what
 * they were asked for because CoreGraphics has no getter for any of them; THIS one has both halves on
 * the CoreGraphics side, so it READS THROUGH: `-imageInterpolation` asks the context, and a refused
 * `Low` or `High` therefore reports the quality ACTUALLY IN FORCE rather than the one that was asked
 * for and rejected. A stored copy here would be the disagreement the other three cannot avoid and
 * this one has no reason to accept.
 *
 * AND THE TWO ENUMS' ORDERS ARE NOT THE SAME — `NSImageInterpolationDefault` is 0 here while
 * `kCGInterpolationNone` is 0 there, because the CoreGraphics ordering is this tree's own decision
 * (`None` first, so a zeroed context means nearest). A CAST BETWEEN THEM WOULD THEREFORE SWAP
 * `Default` AND `None` SILENTLY, which is why the mapping below is an explicit switch.
 */
@property NSImageInterpolation imageInterpolation;

@end

NS_ASSUME_NONNULL_END

#endif /* APPKIT_NSGRAPHICSCONTEXT_H */
