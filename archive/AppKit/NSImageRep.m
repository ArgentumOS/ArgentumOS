/*
 * NSImageRep — the metadata every representation has, and the one drawing rule they share.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE MRC OWNERSHIP IS SMALL AND EXPLICIT: this class owns exactly `_colorSpaceName` (copied in, so a
 * caller may pass a mutable string and let it go) and nothing else — the pixels belong to the
 * subclass, which knows whether it owns them.
 */
#import <AppKit/NSImageRep.h>
#import <AppKit/NSGraphicsContext.h>
#import <CoreGraphics/CGContext.h>

NSString *const NSDeviceRGBColorSpace = @"NSDeviceRGBColorSpace";
NSString *const NSDeviceGrayColorSpace = @"NSDeviceGrayColorSpace";

@implementation NSImageRep

- (instancetype)init
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_size = NSMakeSize(0.0, 0.0);
	_pixelsWide = 0;
	_pixelsHigh = 0;
	_colorSpaceName = nil;
	return self;
}

- (void)dealloc
{
	[_colorSpaceName release];
	[super dealloc];
}

- (NSInteger)pixelsWide
{
	return _pixelsWide;
}

- (void)setPixelsWide:(NSInteger)n
{
	_pixelsWide = n;
}

- (NSInteger)pixelsHigh
{
	return _pixelsHigh;
}

- (void)setPixelsHigh:(NSInteger)n
{
	_pixelsHigh = n;
}

- (NSSize)size
{
	return _size;
}

- (void)setSize:(NSSize)s
{
	_size = s;
}

- (NSString *)colorSpaceName
{
	return _colorSpaceName;
}

- (void)setColorSpaceName:(NSString *)name
{
	if (name != _colorSpaceName) {
		NSString *copy = [name copy];

		[_colorSpaceName release];
		_colorSpaceName = copy;
	}
}

/* AN ABSTRACT REP HAS NO PIXELS, so there is no image to answer with. This is not a stub to be filled
 * in later: it is the contract, and every concrete rep overrides it. */
- (CGImageRef)CGImage
{
	return NULL;
}

- (BOOL)drawInRect:(NSRect)rect
{
	NSGraphicsContext *ctx = [NSGraphicsContext currentContext];
	CGImageRef img = [self CGImage];
	CGContextRef cg;

	/* NO CONTEXT OR NO PIXELS IS A NO, NOT A RAISE — the same rule the context's own members follow. */
	if (ctx == nil || img == NULL) {
		return NO;
	}
	cg = [ctx CGContext];
	if (cg == NULL) {
		return NO;
	}
	CGContextDrawImage(cg, NSRectToCGRect(rect), img);
	return YES;
}

- (BOOL)drawAtPoint:(NSPoint)point
{
	return [self drawInRect:NSMakeRect(point.x, point.y, _size.width, _size.height)];
}

- (BOOL)draw
{
	return [self drawInRect:NSMakeRect(0.0, 0.0, _size.width, _size.height)];
}

@end
