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

- (void)dealloc
{
	[_reps release];
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

- (void)lockFocus
{
	NSSize s = [self size];
	CGColorSpaceRef cs;
	NSGraphicsContext *gstate;
	NSInteger w = (NSInteger)ceil(s.width);
	NSInteger h = (NSInteger)ceil(s.height);

	if (_focusContext != NULL) {
		return;
	}
	/* A ZERO-PIXEL CANVAS IS REFUSED BY NAME: it would make a context nothing can be drawn into and a
	 * rep with no pixels, which is a silent nothing rather than an answer. */
	if (w <= 0 || h <= 0) {
		fprintf(stderr, "APPKIT-REFUSE: -lockFocus on an image with no SIZE (got %gx%g); set the "
				"size first\n", (double)s.width, (double)s.height);
		return;
	}
	_canvas = calloc(1, (size_t)w * (size_t)h * 4u);
	if (_canvas == NULL) {
		return;
	}
	cs = CGColorSpaceCreateDeviceRGB();
	_focusContext = CGBitmapContextCreate(_canvas, (size_t)w, (size_t)h, 8, (size_t)w * 4u, cs,
					      kCGImageAlphaPremultipliedFirst | kCGImageByteOrder32Little);
	CGColorSpaceRelease(cs);
	if (_focusContext == NULL) {
		free(_canvas);
		_canvas = NULL;
		return;
	}
	/* A FRESH CANVAS IS EMPTY, not whatever the allocator had — the same rule the rep's own allocated
	 * buffer follows. */
	CGContextClearRect(_focusContext, CGRectMake(0.0, 0.0, (CGFloat)w, (CGFloat)h));
	gstate = [NSGraphicsContext graphicsContextWithCGContext:_focusContext flipped:NO];
	if (gstate == nil) {
		CGContextRelease(_focusContext);
		_focusContext = NULL;
		free(_canvas);
		_canvas = NULL;
		return;
	}
	_focusW = w;
	_focusH = h;
	_focusGState = [gstate retain];
	/* RETAINED, AND IT MAY BE NIL — retaining nil is nil, so an image focused with NO context current
	 * is a legitimate state and `-unlockFocus` puts nil back. */
	_gstatePrevious = [[NSGraphicsContext currentContext] retain];
	[NSGraphicsContext setCurrentContext:gstate];
}

- (void)unlockFocus
{
	unsigned char *buf;
	size_t n;

	if (_focusContext == NULL) {
		return;
	}
	[NSGraphicsContext setCurrentContext:_gstatePrevious];
	[_focusGState release];
	_focusGState = nil;
	[_gstatePrevious release];
	_gstatePrevious = nil;
	CGContextRelease(_focusContext);
	_focusContext = NULL;

	n = (size_t)_focusW * (size_t)_focusH * 4u;
	buf = malloc(n);
	if (buf != NULL) {
		unsigned char *planes[1];
		NSBitmapImageRep *rep;

		fn_swizzle_bgra_to_rgba(_canvas, buf, (size_t)_focusW * (size_t)_focusH);
		/* THE PREVIOUS FOCUS REP GOES, AND ITS BUFFER WITH IT: the rep BORROWS the buffer, so keeping
		 * one and dropping the other is a dangling pointer rather than a leak. */
		if (_focusRep != nil) {
			[self removeRepresentation:_focusRep];
			_focusRep = nil;
		}
		free(_focusBuffer);
		_focusBuffer = buf;
		planes[0] = buf;
		rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:planes pixelsWide:_focusW
					pixelsHigh:_focusH bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES
					isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace
					bytesPerRow:_focusW * 4 bitsPerPixel:32];
		if (rep != nil) {
			[self addRepresentation:rep];
			_focusRep = rep;
			[rep release];
		}
	}
	free(_canvas);
	_canvas = NULL;
}

@end
