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
#import <Foundation/NSArray.h>
#import <Foundation/NSString.h>
#include <math.h>

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

@end
