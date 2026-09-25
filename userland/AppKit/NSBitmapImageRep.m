/*
 * NSBitmapImageRep — a byte buffer with a layout, or a CGImage with a layout, and the bridge.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
#import <AppKit/NSBitmapImageRep.h>
#import <CoreGraphics/CGColorSpace.h>
#import <CoreGraphics/CGDataProvider.h>
#import <CoreGraphics/CGImage.h>
#import <Foundation/NSString.h>
#include <stdio.h>
#include <stdlib.h>

/* THE COLOUR SPACE A CGImage IS IN, AS ONE OF THIS LAYER'S TWO NAMES. Read off the space rather than
 * guessed from the channel count, because a 1-channel 8-bit image is gray and a 1-channel 32-bit one
 * is not. */
static NSString *fn_name_for(CGColorSpaceRef cs)
{
	if (cs != NULL && CGColorSpaceGetModel(cs) == kCGColorSpaceModelMonochrome) {
		return NSDeviceGrayColorSpace;
	}
	return NSDeviceRGBColorSpace;
}

/* AND THE SPACE ITSELF, FROM THE NAME. The caller owns the +1 result. */
static CGColorSpaceRef fn_space_for(NSString *name)
{
	if (name != nil && [name isEqualToString:NSDeviceGrayColorSpace]) {
		return CGColorSpaceCreateDeviceGray();
	}
	return CGColorSpaceCreateDeviceRGB();
}

@implementation NSBitmapImageRep

- (nullable instancetype)initWithBitmapDataPlanes:(unsigned char * _Nullable * _Nullable)planes
				       pixelsWide:(NSInteger)pixelsWide
				       pixelsHigh:(NSInteger)pixelsHigh
				    bitsPerSample:(NSInteger)bitsPerSample
				  samplesPerPixel:(NSInteger)samplesPerPixel
					 hasAlpha:(BOOL)hasAlpha
					 isPlanar:(BOOL)isPlanar
				   colorSpaceName:(nullable NSString *)colorSpaceName
				      bytesPerRow:(NSInteger)bytesPerRow
				     bitsPerPixel:(NSInteger)bitsPerPixel
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	/* A LAYOUT THIS CLASS CANNOT HOLD IS REFUSED RATHER THAN HALF-ACCEPTED. Planar is the one that is a
	 * real feature and not a bad argument — it needs several planes and this class holds one — so it is
	 * named as absent rather than silently treated as non-planar. */
	if (isPlanar) {
		fprintf(stderr, "APPKIT-REFUSE: NSBitmapImageRep isPlanar:YES needs several planes, and this "
				"class holds ONE (non-planar only)\\n");
		[self release];
		return nil;
	}
	if (pixelsWide <= 0 || pixelsHigh <= 0 || bitsPerSample <= 0 || samplesPerPixel <= 0
	    || bytesPerRow <= 0 || bitsPerPixel <= 0) {
		fprintf(stderr, "APPKIT-REFUSE: NSBitmapImageRep needs positive dimensions, a bit depth, a "
				"channel count and a stride (got %ldx%ld, %ld bits, %ld channels, "
				"%ld per row, %ld per pixel)\\n", (long)pixelsWide, (long)pixelsHigh,
			(long)bitsPerSample, (long)samplesPerPixel, (long)bytesPerRow, (long)bitsPerPixel);
		[self release];
		return nil;
	}
	_pixelsWide = pixelsWide;
	_pixelsHigh = pixelsHigh;
	_size = NSMakeSize((CGFloat)pixelsWide, (CGFloat)pixelsHigh);
	_bitsPerSample = bitsPerSample;
	_samplesPerPixel = samplesPerPixel;
	_hasAlpha = hasAlpha;
	_isPlanar = NO;
	_bytesPerRow = bytesPerRow;
	_bitsPerPixel = bitsPerPixel;
	[self setColorSpaceName:colorSpaceName];

	if (planes == NULL || planes[0] == NULL) {
		/* ALLOCATE, ZEROED — an uninitialised buffer would make the first draw of a fresh rep depend on
		 * whatever the allocator had, which is exactly the kind of silent difference this tree refuses. */
		_bits = calloc(1, (size_t)bytesPerRow * (size_t)pixelsHigh);
		if (_bits == NULL) {
			[self release];
			return nil;
		}
		_ownsData = YES;
	} else {
		_bits = planes[0];
		_ownsData = NO;
	}
	_cgImage = NULL;
	return self;
}

- (nullable instancetype)initWithCGImage:(CGImageRef)cgImage
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	if (cgImage == NULL) {
		[self release];
		return nil;
	}
	/* RETAINED RATHER THAN COPIED: the pixels are the image's and this rep is the second owner, which is
	 * what makes `-bitmapData` answering NULL honest instead of lossy. */
	_cgImage = CGImageRetain(cgImage);
	_pixelsWide = (NSInteger)CGImageGetWidth(cgImage);
	_pixelsHigh = (NSInteger)CGImageGetHeight(cgImage);
	_size = NSMakeSize((CGFloat)_pixelsWide, (CGFloat)_pixelsHigh);
	_bitsPerSample = (NSInteger)CGImageGetBitsPerComponent(cgImage);
	_bitsPerPixel = (NSInteger)CGImageGetBitsPerPixel(cgImage);
	_samplesPerPixel = _bitsPerSample > 0 ? _bitsPerPixel / _bitsPerSample : _bitsPerPixel / 8;
	_bytesPerRow = (_bitsPerPixel / 8) * _pixelsWide;
	_hasAlpha = CGImageGetAlphaInfo(cgImage) != kCGImageAlphaNone;
	_isPlanar = NO;
	_bits = NULL;
	_ownsData = NO;
	[self setColorSpaceName:fn_name_for(CGImageGetColorSpace(cgImage))];
	return self;
}

+ (nullable instancetype)imageRepWithCGImage:(CGImageRef)cgImage
{
	return [[[self alloc] initWithCGImage:cgImage] autorelease];
}

+ (nullable instancetype)imageRepWithData:(NSData *)data
{
	const unsigned char *b = (const unsigned char *)[data bytes];
	NSUInteger n = [data length];
	CGDataProviderRef prov;
	CGImageRef img = NULL;
	NSBitmapImageRep *rep;

	if (b == NULL || n < 8) {
		fprintf(stderr, "APPKIT-REFUSE: +imageRepWithData: needs at least 8 bytes to identify a "
				"format (got %lu)\\n", (unsigned long)n);
		return nil;
	}
	/* THE FORMAT COMES FROM THE DATA, because the two formats this library decodes each begin with an
	 * unambiguous signature — PNG's eight bytes and JPEG's SOI marker. Anything else is refused by
	 * name: this is not "unsupported by accident", it is the whole of what the decoder set here does. */
	if (b[0] == 0xff && b[1] == 0xd8) {
		prov = CGDataProviderCreateWithData(NULL, b, (size_t)n, NULL);
		if (prov == NULL) {
			return nil;
		}
		img = CGImageCreateWithJPEGDataProvider(prov, NULL, false, kCGRenderingIntentDefault);
		CGDataProviderRelease(prov);
	} else if (b[0] == 0x89 && b[1] == 'P' && b[2] == 'N' && b[3] == 'G') {
		prov = CGDataProviderCreateWithData(NULL, b, (size_t)n, NULL);
		if (prov == NULL) {
			return nil;
		}
		img = CGImageCreateWithPNGDataProvider(prov, NULL, false, kCGRenderingIntentDefault);
		CGDataProviderRelease(prov);
	} else {
		fprintf(stderr, "APPKIT-REFUSE: +imageRepWithData: decodes PNG and JPEG only, and these "
				"bytes are neither (CoreGraphics in this tree has the two DECODERS and no "
				"encoder)\\n");
		return nil;
	}
	if (img == NULL) {
		return nil;
	}
	rep = [[self alloc] initWithCGImage:img];
	CGImageRelease(img);
	return [rep autorelease];
}

- (unsigned char *)bitmapData
{
	return _bits;
}

- (NSInteger)bitsPerSample
{
	return _bitsPerSample;
}

- (NSInteger)samplesPerPixel
{
	return _samplesPerPixel;
}

- (BOOL)hasAlpha
{
	return _hasAlpha;
}

- (BOOL)isPlanar
{
	return _isPlanar;
}

- (NSInteger)bytesPerRow
{
	return _bytesPerRow;
}

- (NSInteger)bitsPerPixel
{
	return _bitsPerPixel;
}

- (CGImageRef)CGImage
{
	/* A CGIMAGE-BACKED REP ANSWERS ITS OWN IMAGE, which is the one it was built from. */
	if (_cgImage != NULL) {
		return _cgImage;
	}
	if (_bits == NULL) {
		return NULL;
	}
	/* AND A BITMAP-BACKED ONE BUILDS THE IMAGE OVER ITS OWN BYTES, ONCE. The provider is the same
	 * borrow the initialiser made — it does not own the buffer and must not free it — and the result is
	 * cached, so the rep and the image share one buffer rather than two that could drift apart. */
	{
		CGDataProviderRef prov = CGDataProviderCreateWithData(NULL, _bits,
								     (size_t)_bytesPerRow * (size_t)_pixelsHigh,
								     NULL);
		CGColorSpaceRef cs = fn_space_for(_colorSpaceName);
		/* THE PARAMETER IS A PLAIN `uint32_t` IN THIS TREE, not Apple's `CGBitmapInfo` — which
		 * does not exist here (measured at the declaration), and reaching for it was the same
		 * kind of mistake as assuming an Apple CGPath function existed. */
		uint32_t info = _hasAlpha ? (uint32_t)kCGImageAlphaPremultipliedLast
					  : (uint32_t)kCGImageAlphaNone;

		if (prov != NULL && cs != NULL) {
			_cgImage = CGImageCreate((size_t)_pixelsWide, (size_t)_pixelsHigh,
						 (size_t)_bitsPerSample, (size_t)_bitsPerPixel,
						 (size_t)_bytesPerRow, cs, info, prov, NULL, false,
						 kCGRenderingIntentDefault);
		}
		if (prov != NULL) {
			CGDataProviderRelease(prov);
		}
		if (cs != NULL) {
			CGColorSpaceRelease(cs);
		}
	}
	return _cgImage;
}

- (void)dealloc
{
	if (_cgImage != NULL) {
		CGImageRelease(_cgImage);
	}
	if (_ownsData && _bits != NULL) {
		free(_bits);
	}
	[super dealloc];
}

@end
