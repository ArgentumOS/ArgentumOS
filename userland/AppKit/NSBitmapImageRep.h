/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSBitmapImageRep.h — pixels you can hold, and the door between AppKit and Core Graphics (C8.9).
 *
 * THIS IS THE REPRESENTATION THAT OWNS PIXELS, so it is also the one that has to say WHAT the pixels
 * are: a bit depth, a channel count, whether there is an alpha channel, a colour space, a row stride.
 * Every one of those is a caller's choice at the designated initialiser and a read-only fact after.
 *
 * AND IT IS ONE PIECE OF STATE, NOT TWO REPRESENTATIONS IN A COAT: a rep is either
 *   * BITMAP-BACKED — it holds a byte buffer (`-bitmapData` answers it), or
 *   * CGIMAGE-BACKED — its pixels are a `CGImage`'s (the case `+imageRepWithData:` produces),
 * and `-bitmapData` answers NULL in the second case rather than unpacking a second copy. THAT IS A
 * STATED DEVIATION and it is the honest one: the decoded image's pixels live in the CGImage, and
 * copying them into a plane would be a second owner of the same bytes with no way to tell which is
 * authoritative. Apple always exposes a bitmap because its decoder always produces one; this layer
 * decodes straight into a CGImage (that is the only decoder it has), so it says so.
 *
 * THE READ SIDE IS REAL AND THE WRITE SIDE IS ABSENT, for a measured reason: CoreGraphics here has
 * `CGImageCreateWithPNGDataProvider` and `CGImageCreateWithJPEGDataProvider` and NO ENCODER — there is
 * no `CGImageDestination` in this tree, and no `libpng`/`libjpeg` write path behind one. So
 * `+imageRepWithData:` sniffs the two formats it can decode and REFUSES anything else by name, and
 * `-representationUsingType:properties:`, `-TIFFRepresentation` and the `NSBitmapImageFileType`
 * enumeration that belongs to them are ABSENT rather than declared-and-refused, because there is
 * nothing for a caller to reach: the honest name for that half is "no encoder", and it is written
 * here rather than left for someone to discover from a link error.
 *
 * AND WHAT THE PIXEL LAYOUT IS, SINCE A BUFFER NEEDS ONE: non-planar only (`isPlanar:YES` is refused
 * by name), one plane (`planes[0]`), and for a rep built FROM a CGImage the layout is READ OFF that
 * image (`CGImageGetBitsPerComponent` and friends) rather than assumed.
 *
 * **THE COLOUR ORDER OF A BITMAP-BACKED REP IS `R, G, B, A`, ALPHA LAST AND PREMULTIPLIED** — stated
 * here because a caller writing bytes needs it, and because getting it wrong is SILENT rather than
 * loud: the bytes are declared to Core Graphics with an explicit `kCGImageByteOrder32Big`, which stores
 * the 32-bit word in the order named. `kCGImageAlphaPremultipliedLast` ALONE would mean the DEFAULT
 * byte order, which is little-endian, and that reads the same bytes as `A, B, G, R` — so an opaque RED
 * pixel, whose byte 0 is 0, came out fully TRANSPARENT. That is not a hypothetical: it is what this
 * class shipped first, and the probe could not see it. An 8-bit grey rep (`samplesPerPixel:1`) has one
 * channel and no alpha byte.
 */

#import <AppKit/NSImageRep.h>
#import <Foundation/NSData.h>

NS_ASSUME_NONNULL_BEGIN

@interface NSBitmapImageRep : NSImageRep
{
	unsigned char *_bits;       /* the pixels of a BITMAP-BACKED rep, or NULL */
	BOOL _ownsData;             /* NO when the caller's plane was adopted rather than allocated */
	NSInteger _bitsPerSample;
	NSInteger _samplesPerPixel;
	BOOL _hasAlpha;
	BOOL _isPlanar;
	NSInteger _bytesPerRow;
	NSInteger _bitsPerPixel;
	CGImageRef _cgImage;        /* RETAINED: a CGIMAGE-BACKED rep's pixels ARE this image */
}

/* THE DESIGNATED INITIALISER, APPLE'S AND IN APPLE'S ORDER. A NULL `planes` (or a NULL `planes[0]`)
 * means ALLOCATE — that is Apple's contract and it is the useful default, so it is kept — and a
 * non-NULL one is ADOPTED: the buffer must outlive the rep, which is the same borrow the context
 * makes of a bitmap context's data. Returns nil for a layout this class cannot hold, and says why. */
- (nullable instancetype)initWithBitmapDataPlanes:(unsigned char * _Nullable * _Nullable)planes
				       pixelsWide:(NSInteger)pixelsWide
				       pixelsHigh:(NSInteger)pixelsHigh
				    bitsPerSample:(NSInteger)bitsPerSample
				  samplesPerPixel:(NSInteger)samplesPerPixel
					 hasAlpha:(BOOL)hasAlpha
					 isPlanar:(BOOL)isPlanar
				   colorSpaceName:(nullable NSString *)colorSpaceName
				      bytesPerRow:(NSInteger)bytesPerRow
				     bitsPerPixel:(NSInteger)bitsPerPixel;

/* THE CORE GRAPHICS DOOR, BOTH WAYS: `-initWithCGImage:` takes an image's pixels (retaining the image
 * rather than copying them), and `-CGImage` hands one back — the same image for a CGIMAGE-BACKED rep,
 * and for a BITMAP-BACKED one an image built over the rep's own bytes, CACHED so that two calls do not
 * produce two images. */
- (nullable instancetype)initWithCGImage:(CGImageRef)cgImage;
+ (nullable instancetype)imageRepWithCGImage:(CGImageRef)cgImage;

/* THE DECODE. The format is taken from the data's own first bytes rather than from a parameter — a
 * caller with PNG bytes should not have to say "these are PNG bytes" — and anything this library
 * cannot decode answers NIL, with the reason printed, rather than a rep with no pixels in it. */
+ (nullable instancetype)imageRepWithData:(NSData *)data;

/* THE LAYOUT, AS FACTS. `-bitmapData` is NULL for a CGIMAGE-BACKED rep (see the note above). */
@property (nullable, readonly) unsigned char *bitmapData;
@property (readonly) NSInteger bitsPerSample;
@property (readonly) NSInteger samplesPerPixel;
@property (readonly) BOOL hasAlpha;
@property (readonly) BOOL isPlanar;
@property (readonly) NSInteger bytesPerRow;
@property (readonly) NSInteger bitsPerPixel;

@end

NS_ASSUME_NONNULL_END
