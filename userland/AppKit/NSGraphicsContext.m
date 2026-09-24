/*
 * NSGraphicsContext — the seam: a CGContextRef behind an object, and the two state stacks.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THERE ARE TWO STACKS HERE AND CONFLATING THEM IS THE ONLY REAL MISTAKE THIS FILE CAN MAKE.
 * `-saveGraphicsState`/`-restoreGraphicsState` are the RECEIVER's, and they are pure forwards to
 * `CGContextSaveGState`/`CGContextRestoreGState` — the C2 state stack inside CoreGraphics, which
 * this file neither owns nor knows the depth of. `+saveGraphicsState`/`+restoreGraphicsState` are
 * the CLASS's, and they push and pop a per-thread stack OF CONTEXTS *as well as* sending the
 * context its own save or restore. A caller can therefore nest either without touching the other,
 * and the probe checks the pair separately for exactly that reason.
 *
 * THE PER-THREAD STORAGE IS `__thread` AND THAT HAS ONE COST, STATED RATHER THAN DISCOVERED: a
 * thread that exits while it holds a current context never releases it, because nothing runs then.
 * The alternative — one global — would make two threads' draws collide, so the leak is the cheaper
 * of the two mistakes, and the caller who set the context owns it anyway (this object does not
 * retain it; see the header).
 */
#import <AppKit/NSGraphicsContext.h>
#import <CoreGraphics/CGContext.h>

#include <stdio.h>

/* THE PER-THREAD CURRENT CONTEXT, and the per-thread stack of saved ones. The depth bound is the
 * same kind of bound the graphics state's dash array has, and it is refused rather than wrapped for
 * the same reason: a stack that silently dropped its bottom entry would restore the wrong context
 * and look deliberate doing it. */
#define APPKIT_GSTATE_STACK_MAX 64

static __thread NSGraphicsContext *fn_current;
static __thread NSGraphicsContext *fn_stack[APPKIT_GSTATE_STACK_MAX];
static __thread int fn_depth;

/* THE ENUM TO THE OPERATOR, AND THE ONE CASE THAT HAS NO OPERATOR. `CGBlendMode`'s names track
 * `NSCompositingOperation`'s one-for-one from `Clear` down to `Luminosity`, so this is a switch and
 * not a table — and `NSCompositingOperationPlusDarker` has no case because `kCGBlendModePlusDarker`
 * does not exist in this tree (no pixman operator computes it; CGContext.h says so). `*ok` reports
 * that refusal and the setter turns it into a message, because drawing the nearest available thing
 * instead would be a picture that looks deliberate. */
static CGBlendMode fn_blend_mode(NSCompositingOperation op, int *ok)
{
	*ok = 1;
	switch (op) {
	case NSCompositingOperationClear: return kCGBlendModeClear;
	case NSCompositingOperationCopy: return kCGBlendModeCopy;
	case NSCompositingOperationSourceOver: return kCGBlendModeNormal;
	case NSCompositingOperationSourceIn: return kCGBlendModeSourceIn;
	case NSCompositingOperationSourceOut: return kCGBlendModeSourceOut;
	case NSCompositingOperationSourceAtop: return kCGBlendModeSourceAtop;
	case NSCompositingOperationDestinationOver: return kCGBlendModeDestinationOver;
	case NSCompositingOperationDestinationIn: return kCGBlendModeDestinationIn;
	case NSCompositingOperationDestinationOut: return kCGBlendModeDestinationOut;
	case NSCompositingOperationDestinationAtop: return kCGBlendModeDestinationAtop;
	case NSCompositingOperationXOR: return kCGBlendModeXOR;
	case NSCompositingOperationPlusLighter: return kCGBlendModePlusLighter;
	case NSCompositingOperationMultiply: return kCGBlendModeMultiply;
	case NSCompositingOperationScreen: return kCGBlendModeScreen;
	case NSCompositingOperationOverlay: return kCGBlendModeOverlay;
	case NSCompositingOperationDarken: return kCGBlendModeDarken;
	case NSCompositingOperationLighten: return kCGBlendModeLighten;
	case NSCompositingOperationColorDodge: return kCGBlendModeColorDodge;
	case NSCompositingOperationColorBurn: return kCGBlendModeColorBurn;
	case NSCompositingOperationSoftLight: return kCGBlendModeSoftLight;
	case NSCompositingOperationHardLight: return kCGBlendModeHardLight;
	case NSCompositingOperationDifference: return kCGBlendModeDifference;
	case NSCompositingOperationExclusion: return kCGBlendModeExclusion;
	case NSCompositingOperationHue: return kCGBlendModeHue;
	case NSCompositingOperationSaturation: return kCGBlendModeSaturation;
	case NSCompositingOperationColor: return kCGBlendModeColor;
	case NSCompositingOperationLuminosity: return kCGBlendModeLuminosity;
	case NSCompositingOperationPlusDarker:
	default:
		*ok = 0;
		return kCGBlendModeNormal;
	}
}

@implementation NSGraphicsContext

/* THE DEFAULTS LIVE IN `-init` AND NOT IN THE FACTORY, so that `alloc`/`init` — the path that makes
 * a CONTEXT-LESS object, which the probe covers — starts from the same state as the factory does.
 * AND THEY ARE NOT OPTIONAL, because `alloc` ZEROES THE IVARS: without this a fresh object would
 * report antialiasing OFF (not the context's default of on) and a compositing operation of `Clear`,
 * which is what enum value 0 happens to be and is not `SourceOver`. Three quiet wrong answers, all
 * of them wrong in the direction of "draws nothing". */
- (instancetype)init
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_shouldAntialias = YES;
	_patternPhase = NSMakePoint(0.0, 0.0);
	_compositingOperation = NSCompositingOperationSourceOver;
	return self;
}

+ (nullable NSGraphicsContext *)graphicsContextWithCGContext:(CGContextRef)cgContext
                                                    flipped:(BOOL)initialFlippedState
{
	NSGraphicsContext *result = [[NSGraphicsContext alloc] init];

	if (result == nil) {
		return nil;
	}
	/* A NULL CONTEXT IS NOT REFUSED, and that is a deliberate difference from this tree's usual
	 * habit of refusing by name: Apple's factory takes a `CGContextRef` and has no documented
	 * refusal, and an object that reports NULL from `-CGContext` is visibly unusable rather than
	 * silently wrong. Every accessor below tolerates it. */
	result->_context = cgContext;
	result->_flipped = initialFlippedState ? YES : NO;
	return result;
}

- (CGContextRef)CGContext
{
	return _context;
}

- (BOOL)isFlipped
{
	return _flipped;
}

- (BOOL)isDrawingToScreen
{
	/* NO FOR EVERY CONTEXT THIS LIBRARY CAN MAKE — see the header note. It is stored rather than
	 * computed so that the day a screen-backed context exists there is one place to change. */
	return _drawingToScreen;
}

- (void)saveGraphicsState
{
	if (_context != NULL) {
		CGContextSaveGState(_context);
	}
}

- (void)restoreGraphicsState
{
	if (_context != NULL) {
		CGContextRestoreGState(_context);
	}
}

- (void)flushGraphics
{
	if (_context != NULL) {
		CGContextFlush(_context);
	}
}

/* THE RENDERING OPTIONS: EACH SETTER REACHES THE CONTEXT, EACH GETTER ANSWERS FROM STORAGE — which
 * is Apple's arrangement rather than a shortcut, because CoreGraphics has no getter for any of the
 * three (measured; see the header's correction). The `!= NULL` guards are what make a context-less
 * object harmless instead of a crash. */

- (BOOL)shouldAntialias
{
	return _shouldAntialias;
}

- (void)setShouldAntialias:(BOOL)flag
{
	_shouldAntialias = flag ? YES : NO;
	if (_context != NULL) {
		CGContextSetShouldAntialias(_context, _shouldAntialias);
	}
}

- (NSPoint)patternPhase
{
	return _patternPhase;
}

- (void)setPatternPhase:(NSPoint)phase
{
	_patternPhase = phase;
	if (_context != NULL) {
		/* A POINT IN, A SIZE OUT: that is Apple's own asymmetry between this property and
		 * `CGContextSetPatternPhase`, and the conversion is the whole of the difference. */
		CGContextSetPatternPhase(_context, CGSizeMake(phase.x, phase.y));
	}
}

- (NSCompositingOperation)compositingOperation
{
	return _compositingOperation;
}

- (void)setCompositingOperation:(NSCompositingOperation)op
{
	CGBlendMode mode;
	int ok;

	mode = fn_blend_mode(op, &ok);
	if (!ok) {
		fprintf(stderr, "APPKIT-REFUSE: NSCompositingOperationPlusDarker has no operator in this "
				"library — pixman computes no plus-darker, which is why "
				"kCGBlendModePlusDarker is absent from CGContext.h — so the blend mode is "
				"left as it was rather than set to the nearest available thing\n");
		return;
	}
	_compositingOperation = op;
	if (_context != NULL) {
		CGContextSetBlendMode(_context, mode);
	}
}

+ (nullable NSGraphicsContext *)currentContext
{
	return fn_current;
}

+ (void)setCurrentContext:(nullable NSGraphicsContext *)context
{
	/* REPLACE, RETAIN THE NEW, RELEASE THE OLD — and in that order, so a caller who sets the
	 * context that is already current does not free it under itself. */
	NSGraphicsContext *old = fn_current;

	fn_current = [context retain];
	[old release];
}

+ (void)saveGraphicsState
{
	if (fn_depth == APPKIT_GSTATE_STACK_MAX) {
		fprintf(stderr, "APPKIT-REFUSE: +saveGraphicsState at depth %d does not fit this "
				"library's stack of %d, and dropping the oldest would restore the wrong "
				"context\n", fn_depth, APPKIT_GSTATE_STACK_MAX);
		return;
	}
	/* THE CURRENT CONTEXT IS PUSHED EVEN WHEN IT IS NIL. A caller who saved with nothing current
	 * and then set one before restoring gets their own context popped back, which is what a
	 * stack of contexts means; skipping the push would make the two calls asymmetric and the
	 * restore would pop something a different save left. */
	fn_stack[fn_depth++] = fn_current;
	if (fn_current != nil) {
		[fn_current saveGraphicsState];
	}
}

+ (void)restoreGraphicsState
{
	if (fn_depth == 0) {
		/* Apple's contract is that an unbalanced restore is a caller error; the cheapest honest
		 * reading is to leave the current context alone rather than read below the stack. */
		return;
	}
	fn_depth--;
	if (fn_current != fn_stack[fn_depth]) {
		/* THE POPPED CONTEXT BECOMES CURRENT, and the one it displaces is released — the pair to
		 * the retain `+setCurrentContext:` took when it was installed. `fn_current` is not
		 * released here when it IS the popped one, because the caller's own reference is what
		 * kept it alive through the save. */
		NSGraphicsContext *displaced = fn_current;

		fn_current = [fn_stack[fn_depth] retain];
		[displaced release];
	}
	fn_stack[fn_depth] = nil;
	if (fn_current != nil) {
		[fn_current restoreGraphicsState];
	}
}

+ (BOOL)currentContextDrawingToScreen
{
	NSGraphicsContext *current = [self currentContext];

	return current == nil ? NO : [current isDrawingToScreen];
}

@end
