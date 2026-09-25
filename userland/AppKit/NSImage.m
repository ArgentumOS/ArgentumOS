/*
 * NSImage — a container of representations that answers drawing requests by choosing one.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE OWNERSHIP IS THE WHOLE OF THIS FILE'S DIFFICULTY, and it is small: this class owns `_reps` and
 * nothing else. `-addRepresentation:` RETAINS its argument (an image outlives the local that made the
 * rep), and the array itself is REPLACED rather than mutated, so `-representations` can hand out a
 * snapshot without a caller changing this image's contents through it.
 */
#import <AppKit/NSImage.h>
#import <AppKit/NSBitmapImageRep.h>
#import <AppKit/NSGraphicsContext.h>
#import <CoreGraphics/CGBitmapContext.h>
#import <CoreGraphics/CGColorSpace.h>
#import <CoreGraphics/CGContext.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSString.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* THE PRIVATE SEAM, DECLARED HERE AND NOT INSIDE THE IMPLEMENTATION. */
@interface NSImage ()
- (void)fnAdoptFocusBuffer:(unsigned char *)buf;
@end

@implementation NSImage

+ (instancetype)image
{
	return [[[self alloc] initWithSize:NSMakeSize(0.0, 0.0)] autorelease];
}

- (instancetype)init
{
	return [self initWithSize:NSMakeSize(0.0, 0.0)];
}

- (instancetype)initWithSize:(NSSize)size
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_reps = nil;
	_size = size;
	/* AN EXPLICIT SIZE EVEN WHEN IT IS ZERO: `-initWithSize:` means "this is the size", which is what
	 * `+image`'s empty canvas needs — otherwise it would answer a rep's size the moment one was added. */
	_sizeIsExplicit = YES;
	return self;
}

- (nullable instancetype)initWithData:(NSData *)data
{
	NSBitmapImageRep *rep = [NSBitmapImageRep imageRepWithData:data];

	if (rep == nil) {
		[self release];
		return nil;
	}
	self = [self initWithSize:NSMakeSize(0.0, 0.0)];
	if (self == nil) {
		return nil;
	}
	/* A DECODED IMAGE'S SIZE IS ITS PIXELS UNTIL SOMEONE SAYS OTHERWISE, so this is NOT explicit: the
	 * size follows the representation, which is what `-size`'s precedence is for. */
	_sizeIsExplicit = NO;
	[self addRepresentation:rep];
	return self;
}

- (nullable instancetype)initWithContentsOfFile:(NSString *)path
{
	NSData *data = [NSData dataWithContentsOfFile:path];

	if (data == nil) {
		[self release];
		return nil;
	}
	return [self initWithData:data];
}

- (nullable instancetype)initWithCGImage:(CGImageRef)cgImage size:(NSSize)size
{
	NSBitmapImageRep *rep;

	if (cgImage == NULL) {
		[self release];
		return nil;
	}
	rep = [NSBitmapImageRep imageRepWithCGImage:cgImage];
	if (rep == nil) {
		[self release];
		return nil;
	}
	self = [self initWithSize:size];
	if (self == nil) {
		return nil;
	}
	[self addRepresentation:rep];
	return self;
}

/* ADOPTING THE CANVAS FROM A CLASS METHOD, which cannot touch this instance's ivars: the class method
 * makes the buffer and this instance owns it, and the seam is a private method rather than a
 * friend-style ivar poke. The DECLARATION is in the extension ABOVE `@implementation`, because an
 * `@interface` that opens inside an implementation ENDS it — which the compiler said plainly the first
 * time this was written the other way round. */
- (void)fnAdoptFocusBuffer:(unsigned char *)buf
{
	/* THE OLD ONE GOES FIRST — it was this object's, and the rep that borrowed it has already been
	 * dropped by the caller path that replaces it. */
	if (_focusBuffer != NULL) {
		free(_focusBuffer);
	}
	_focusBuffer = buf;
}

- (void)dealloc
{
	[_reps release];
	/* THE CANVAS IS OURS, AND MUST OUTLIVE THE REP THAT BORROWS IT — which is why it lives here rather
	 * than on the class method's stack. */
	if (_focusBuffer != NULL) {
		free(_focusBuffer);
	}
	[super dealloc];
}

- (void)addRepresentation:(NSImageRep *)imageRep
{
	NSMutableArray *next;

	if (imageRep == nil) {
		return;
	}
	/* A NEW ARRAY EACH TIME, so the snapshot handed out by `-representations` is never the one being
	 * mutated. The copy is `+arrayWithArray:` rather than a hand-rolled move for the ordinary reason:
	 * a hand-rolled one is where the retain/autorelease bug lives. */
	next = [NSMutableArray arrayWithArray:_reps != nil ? _reps : [NSArray array]];
	[next addObject:imageRep];
	[_reps release];
	_reps = [next copy];
}

- (void)removeRepresentation:(NSImageRep *)imageRep
{
	NSMutableArray *next;

	if (imageRep == nil || _reps == nil) {
		return;
	}
	next = [NSMutableArray arrayWithArray:_reps];
	[next removeObjectIdenticalTo:imageRep];
	[_reps release];
	_reps = [next copy];
}

- (NSArray *)representations
{
	return _reps != nil ? _reps : [NSArray array];
}

- (NSSize)size
{
	if (_sizeIsExplicit) {
		return _size;
	}
	if (_reps != nil && [_reps count] > 0) {
		return [[_reps objectAtIndex:0] size];
	}
	return _size;
}

- (void)setSize:(NSSize)s
{
	_size = s;
	_sizeIsExplicit = YES;
}

- (BOOL)isValid
{
	return _reps != nil && [_reps count] > 0;
}

- (nullable NSImageRep *)bestRepresentationForRect:(NSRect)rect
					   context:(nullable NSGraphicsContext *)referenceContext
					     hints:(nullable NSDictionary *)hints
{
	/* THE CLOSEST IN POINTS, ties to the FIRST — the decision this class is allowed to make. A caller
	 * who wants a different rule can walk `-representations` itself, which is why this stays simple
	 * rather than growing a scoring model. */
	NSImageRep *best = nil;
	double bestDist = 0.0;
	NSUInteger i;

	(void)referenceContext;
	(void)hints;
	if (_reps == nil) {
		return nil;
	}
	for (i = 0; i < [_reps count]; i++) {
		NSImageRep *rep = [_reps objectAtIndex:i];
		NSSize s = [rep size];
		double d = fabs(s.width - rect.size.width) + fabs(s.height - rect.size.height);

		if (best == nil || d < bestDist) {
			best = rep;
			bestDist = d;
		}
	}
	return best;
}

- (BOOL)drawInRect:(NSRect)rect
{
	NSImageRep *rep = [self bestRepresentationForRect:rect context:nil hints:nil];

	if (rep == nil) {
		return NO;
	}
	return [rep drawInRect:rect];
}

- (BOOL)drawAtPoint:(NSPoint)point
{
	NSSize s = [self size];

	return [self drawInRect:NSMakeRect(point.x, point.y, s.width, s.height)];
}

/* BGRA -> RGBA, PREMULTIPLIED ON BOTH SIDES SO NO UN-PREMULTIPLY IS OWED. The source is this tree's
 * bitmap-context format (alpha first in a little-endian word, which in MEMORY is B, G, R, A) and the
 * destination is what a rep's `-CGImage` is built with (alpha last, so R, G, B, A). Getting this
 * backwards is not subtle for long: red comes back blue, which is exactly what the probe checks. */
static void fn_swizzle_bgra_to_rgba(const unsigned char *src, unsigned char *dst, size_t pixels)
{
	size_t i;

	for (i = 0; i < pixels; i++) {
		dst[0] = src[2];
		dst[1] = src[1];
		dst[2] = src[0];
		dst[3] = src[3];
		src += 4;
		dst += 4;
	}
}

+ (nullable instancetype)imageWithSize:(NSSize)size
			       flipped:(BOOL)flipped
			drawingHandler:(nullable BOOL (^)(NSRect dstRect))drawingHandler
{
	NSImage *im = nil;
	unsigned char *canvas;
	unsigned char *buf;
	CGColorSpaceRef cs;
	CGContextRef ctx = NULL;
	NSGraphicsContext *gstate = nil;
	NSGraphicsContext *prev = nil;
	NSInteger w = (NSInteger)ceil(size.width);
	NSInteger h = (NSInteger)ceil(size.height);
	size_t n;

	if (drawingHandler == nil) {
		fprintf(stderr, "APPKIT-REFUSE: +imageWithSize:flipped:drawingHandler: needs a handler\n");
		return nil;
	}
	if (w <= 0 || h <= 0) {
		fprintf(stderr, "APPKIT-REFUSE: +imageWithSize:flipped:drawingHandler: with a size of "
				"%gx%g has no pixels to draw into\n", (double)size.width, (double)size.height);
		return nil;
	}
	n = (size_t)w * (size_t)h * 4u;
	canvas = calloc(1, n);
	buf = malloc(n);
	if (canvas == NULL || buf == NULL) {
		free(canvas);
		free(buf);
		return nil;
	}
	cs = CGColorSpaceCreateDeviceRGB();
	ctx = CGBitmapContextCreate(canvas, (size_t)w, (size_t)h, 8, (size_t)w * 4u, cs,
				    kCGImageAlphaPremultipliedFirst | kCGImageByteOrder32Little);
	CGColorSpaceRelease(cs);
	if (ctx == NULL) {
		free(canvas);
		free(buf);
		return nil;
	}
	/* A FRESH CANVAS IS EMPTY, not whatever the allocator had. */
	CGContextClearRect(ctx, CGRectMake(0.0, 0.0, (CGFloat)w, (CGFloat)h));
	/* THE FLAG IS HONOURED BY FLIPPING THE CTM, because NSGraphicsContext's `flipped` is a STATEMENT
	 * about the context rather than a transform it applies to it — AND THE CONDITION IS THE OPPOSITE WAY
	 * ROUND FROM THE FIRST VERSION, WHICH IS WHAT MEASURING FOUND:
	 *
	 * a CGBitmapContext's DEFAULT CTM is (1, 0, 0, -1, 0, h) — its origin is the TOP-LEFT and y grows
	 * DOWNWARD — which is exactly what AppKit calls flipped:YES. So YES needs NO transform, and NO is
	 * the case that needs the flip. The first version tested `if (flipped)`, applied the flip for YES,
	 * and pushed the handler's rect to y-16 on an 8-tall canvas: an EMPTY canvas, silently. The
	 * measurement that proved it printed this from inside the handler:
	 *     flipped=0  CTM a=1 b=0 c=0 d=-1 tx=0 ty=8
	 *     flipped=1  CTM a=1 b=0 c=0 d=1  tx=0 ty=-16
	 */
	if (!flipped) {
		/* SCALE FIRST, THEN TRANSLATE — AND THE ORDER IS THE SECOND THING MEASURING CAUGHT, not a
		 * style choice. In this tree the two calls compose as
		 *     ScaleCTM(1, s):    d *= s;  ty *= s
		 *     TranslateCTM(0, t): ty += t
		 * (measured from the printed CTM, and it is why the first version was wrong twice). From the
		 * bitmap default (d=-1, ty=h):
		 *     Scale(1,-1)         -> d=1,  ty=-h
		 *     then Translate(0,h) -> d=1,  ty=0    == IDENTITY, which is the y-up space this needs
		 * The other order gives (d=1, ty=-2h) — the drawing lands at y-h, off an h-tall canvas. */
		CGContextScaleCTM(ctx, 1.0, -1.0);
		CGContextTranslateCTM(ctx, 0.0, (CGFloat)h);
	}
	gstate = [NSGraphicsContext graphicsContextWithCGContext:ctx flipped:flipped];
	if (gstate == nil) {
		CGContextRelease(ctx);
		free(canvas);
		free(buf);
		return nil;
	}
	prev = [[NSGraphicsContext currentContext] retain];
	[NSGraphicsContext setCurrentContext:gstate];
	/* THE HANDLER DRAWS; ITS ANSWER IS DISCARDED, as Apple's is. */
	(void)drawingHandler(NSMakeRect(0.0, 0.0, (CGFloat)w, (CGFloat)h));
	[NSGraphicsContext setCurrentContext:prev];
	[prev release];

	im = [[self alloc] initWithSize:size];
	if (im != nil) {
		unsigned char *planes[1];
		NSBitmapImageRep *rep;

		fn_swizzle_bgra_to_rgba(canvas, buf, (size_t)w * (size_t)h);
		planes[0] = buf;
		rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:planes pixelsWide:w
					pixelsHigh:h bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES
					isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace
					bytesPerRow:w * 4 bitsPerPixel:32];
		if (rep == nil) {
			[im release];
			im = nil;
			free(buf);
		} else {
			[im addRepresentation:rep];
			[im fnAdoptFocusBuffer:buf];
			[rep release];
		}
	} else {
		free(buf);
	}
	CGContextRelease(ctx);
	free(canvas);
	return [im autorelease];
}

@end
