/*
 * CGImage — a bitmap and the chart that describes it.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * AN IMAGE IS PIXELS PLUS THEIR MEANING, and the meaning is not decoration: bits per component,
 * bits per pixel, bytes per row, the colour space, and WHICH BYTE IS WHICH CHANNEL are what turn a
 * buffer into something that can be drawn. Apple's `CGImageCreate` therefore takes all of them,
 * and this one does too — the signature is the API.
 *
 * THE BYTES ARE THE PROVIDER'S, AND THE IMAGE RETAINS THE PROVIDER rather than copying it: an
 * image is a view of memory that already exists, which is what makes it cheap to make one from a
 * decoded file or a mapped block. That is why the provider's own contract — the bytes stay valid
 * until it is released — is the image's contract too.
 *
 * ONE FORMAT IS SUPPORTED, THE ONE THIS LIBRARY'S CONTEXTS USE: 8 bits per component, 32 bits per
 * pixel, `kCGImageAlphaPremultipliedFirst` with `kCGBitmapByteOrder32Little`. A different one is
 * REFUSED rather than reinterpreted — the pixels would look plausible and be wrong — and the
 * refusal names the format, so the next person can see what the matrix would have to cover. THE
 * GENERAL FORMAT MATRIX IS A LATER SLICE, and this is where it will land.
 *
 * AND `decode` IS REFUSED IF NON-NULL rather than ignored: it maps input ranges onto output ones,
 * so accepting it and doing nothing would silently draw an image nobody asked for. The same rule
 * the colour-space constructors follow.
 */
#ifndef CORE_GRAPHICS_CGIMAGE_H
#define CORE_GRAPHICS_CGIMAGE_H

#include <CoreGraphics/CGBase.h>
/* THE ALPHA AND BYTE-ORDER VOCABULARY LIVES BESIDE THE BITMAP CONTEXT, because that is the
 * milestone that first needed it — `kCGImageAlphaPremultipliedFirst`, `kCGBitmapByteOrder32Little`
 * and the `CGImageAlphaInfo` enum.
 *
 * AND THE VOCABULARY ITSELF IS HERE NOW, MOVED FROM `CGBitmapContext.h`, WHICH IS WHERE IT WAS
 * DECLARED UNTIL THIS SLICE — and the move is not tidiness: `CGImageAlphaInfo` and the byte-order
 * constants are APPLE'S IMAGE TYPES, one vocabulary for images and contexts alike, and keeping them
 * in the bitmap header made the two CIRCULAR, because that header needs `CGContext.h` and
 * `CGContext.h` needs this one for the type it draws. A cycle is not a style problem: with include
 * guards in place, whichever header is read first leaves the other's types undefined — which is
 * exactly how it failed, with `unknown type name 'CGImageAlphaInfo'`.
 *
 * WHAT MOVED IS UNCHANGED. The four alpha values a 32-bit context can be built with; the LIVE
 * `CGImageByteOrderInfo` family rather than the `kCGBitmapByteOrder*` one — Apple's index carries
 * all four of those as DEPRECATED and this tree neither ships nor owes a deprecated name, so the
 * same bits are declared under the modern spelling (`kCGImageByteOrder32Little` is 2 << 12, exactly
 * as `kCGBitmapByteOrder32Little` was); and the two masks, because a `bitmapInfo` word is a packed
 * pair and a named mask beats a literal. */
typedef enum {
	kCGImageAlphaNoneSkipFirst = 4,
	kCGImageAlphaNoneSkipLast = 5,
	kCGImageAlphaPremultipliedFirst = 2,
	kCGImageAlphaPremultipliedLast = 1
} CGImageAlphaInfo;

typedef enum {
	kCGImageByteOrderDefault = 0,
	kCGImageByteOrder32Little = 2 << 12,
	kCGImageByteOrder32Big = 4 << 12
} CGImageByteOrderInfo;

#define kCGBitmapAlphaInfoMask 0x1Fu
#define kCGBitmapByteOrderInfoMask 0xF000u

#include <CoreGraphics/CGColor.h>
#include <CoreGraphics/CGColorSpace.h>
#include <CoreGraphics/CGDataProvider.h>
#include <CoreGraphics/CGGeometry.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CGImage *CGImageRef;

/* `bytesPerRow` may be 0, which asks for a tightly packed image (width * bitsPerPixel / 8).
 *
 * THE THREE PROPERTIES ARE CHECKED SEPARATELY BECAUSE THEY ARE SEPARATE FACTS: `bitsPerComponent`
 * says how deep each channel is, `bitsPerPixel` how many bits one pixel occupies, and `bitmapInfo`
 * which channels those are and in what order. A provider that holds fewer bytes than the chart
 * describes is refused too — an image whose last rows read past its own data is a crash waiting for
 * whoever draws it, and refusing here is one line where the alternative is a segfault in a caller.
 *
 * `shouldInterpolate` and `intent` are ACCEPTED AND KEPT, and they are the reason the getters for
 * them exist. What this library does with them when drawing is stated at `CGContextDrawImage`. */
CGImageRef CGImageCreate(size_t width, size_t height, size_t bitsPerComponent, size_t bitsPerPixel,
			 size_t bytesPerRow, CGColorSpaceRef space, uint32_t bitmapInfo,
			 CGDataProviderRef provider, const CGFloat *decode, bool shouldInterpolate,
			 CGColorRenderingIntent intent);

CGImageRef CGImageRetain(CGImageRef image);
void CGImageRelease(CGImageRef image);

/* The chart, read back. `GetDataProvider` hands the provider back BORROWED — the image owns it,
 * and the caller may retain it if they need it to outlive the image. */
size_t CGImageGetWidth(CGImageRef image);
size_t CGImageGetHeight(CGImageRef image);
size_t CGImageGetBitsPerComponent(CGImageRef image);
size_t CGImageGetBitsPerPixel(CGImageRef image);
size_t CGImageGetBytesPerRow(CGImageRef image);
CGColorSpaceRef CGImageGetColorSpace(CGImageRef image);
CGImageAlphaInfo CGImageGetAlphaInfo(CGImageRef image);
uint32_t CGImageGetBitmapInfo(CGImageRef image);
CGDataProviderRef CGImageGetDataProvider(CGImageRef image);
bool CGImageGetShouldInterpolate(CGImageRef image);
CGColorRenderingIntent CGImageGetRenderingIntent(CGImageRef image);

#ifdef __cplusplus
}
#endif

#endif /* CORE_GRAPHICS_CGIMAGE_H */
