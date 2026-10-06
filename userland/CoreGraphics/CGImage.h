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
 * WHAT MOVED IS UNCHANGED, AND THE BYTE-ORDER SPELLING IS NOW THE 10.6 ONE. This paragraph used to
 * argue the opposite — that the LIVE `CGImageByteOrderInfo` family should be declared instead,
 * because Apple's index carries the `kCGBitmapByteOrder*` names as DEPRECATED and this tree neither
 * ships nor owes a deprecated name. THE SDK GROUND SETTLED IT (2026-10-05) AND AGAINST THAT
 * REASONING: `kCGBitmapByteOrder*` IS in the 10.6 headers and `kCGImageByteOrder*` is NOT, so the
 * era's names — not the modern ones — are what a caller of this surface writes. THE SAME BITS
 * EITHER WAY (`kCGBitmapByteOrder32Little` is 2 << 12, exactly what the modern spelling was), so
 * this is a spelling the surface owes rather than a behaviour change, and it arrives as a FIX for
 * rows the ledger had been carrying as owed. */
/* THE FULL EIGHT, IN APPLE'S CANONICAL VALUES, and the two `NoneSkip*` ones had to MOVE to get
 * there: this tree previously had `NoneSkipFirst = 4` and `NoneSkipLast = 5`, which is BOTH SWAPPED
 * relative to Apple (where NoneSkipLast is 5 and NoneSkipFirst is 6) and a COLLISION with
 * `kCGImageAlphaFirst`, whose value is 4. Nothing depended on the old numbers — the one user names
 * the constant rather than spelling a literal, and the value every context and probe is built with,
 * `kCGImageAlphaPremultipliedFirst`, keeps its value of 2 throughout — so this is a fidelity fix
 * rather than a breaking change. It also fills the four names the ledger has been carrying as OPEN.
 *
 * `kCGImageAlphaOnly` is an alpha channel and NOTHING ELSE — no colour components at all — which is
 * what a mask is, and the sampler in CGContextDrawImage handles it as its own case. */
typedef enum {
	kCGImageAlphaNone = 0,
	kCGImageAlphaPremultipliedLast = 1,
	kCGImageAlphaPremultipliedFirst = 2,
	kCGImageAlphaLast = 3,
	kCGImageAlphaFirst = 4,
	kCGImageAlphaNoneSkipLast = 5,
	kCGImageAlphaNoneSkipFirst = 6,
	kCGImageAlphaOnly = 7
} CGImageAlphaInfo;

/* APPLE'S 10.6 DECLARATION, ALL SIX VALUES AND THE MASK, transcribed from the header itself rather
 * than recalled — `kCGBitmapByteOrderMask` is 0x7000 there, NOT the 0xF000 the modern spelling of
 * the mask uses, and a mask that is too wide is a mask that swallows bits a caller set. THE TWO
 * HOST NAMES ARE MACROS IN APPLE'S HEADER TOO, endian-dependent, and this library builds for
 * x86-64 alone, so they name the Little variants as Apple's own `#else` arm does. */
enum {
	kCGBitmapByteOrderMask = 0x7000,
	kCGBitmapByteOrderDefault = (0 << 12),
	kCGBitmapByteOrder16Little = (1 << 12),
	kCGBitmapByteOrder32Little = (2 << 12),
	kCGBitmapByteOrder16Big = (3 << 12),
	kCGBitmapByteOrder32Big = (4 << 12)
};

/* UNNAMED ON PURPOSE, like Apple's own: 10.6 declares these constants inside `CGBitmapInfo`'s own
 * option set, and there is no such type as `CGImageByteOrderInfo` there to name them with. */

#define kCGBitmapByteOrder16Host kCGBitmapByteOrder16Little
#define kCGBitmapByteOrder32Host kCGBitmapByteOrder32Little

#define kCGBitmapAlphaInfoMask 0x1Fu

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

/* A PNG'S BYTES BECOME AN IMAGE, and what comes back is worth stating here: 8 bits per component,
 * 32 bits per pixel, this library's own premultiplied-first little-endian format, with the samples
 * PREMULTIPLIED — because a PNG's are straight and this library's format promises otherwise — and
 * DEVICE RGB as the colour space whatever the PNG's own colour information says.
 *
 * A 16-BIT PNG IS REFUSED rather than downshifted to 8, and a `decode` array is refused rather than
 * ignored: the same rule `CGImageCreate` follows, for the same reason.
 *
 * THE DECODER IS LIBPNG, WHICH IS ALREADY IN THIS TREE for the X stack (FreeType reads sbix colour
 * glyphs with it) and already staged into the guest, so supporting PNG added a LINK FLAG and no new
 * dependency. The seam is CGImagePNG.c. */
CGImageRef CGImageCreateWithPNGDataProvider(CGDataProviderRef provider, const CGFloat *decode,
					    bool shouldInterpolate,
					    CGColorRenderingIntent intent);

/* A JPEG'S BYTES BECOME AN IMAGE. THE DECODER IS LIBJPEG-TURBO, WHICH THIS TREE VENDORS FOR EXACTLY
 * THIS (`third_party/libjpeg-turbo`, tag 3.2.0, IJG + Modified BSD-3, built by tools/libjpeg-build.sh)
 * — so unlike PNG, which rode the X stack, this one really is a new dependency and carries its own §B
 * entry in the self-hosting manifest.
 *
 * WHAT COMES BACK is the same chart the PNG path produces: 8/32-bit premultiplied-first little-endian
 * in device RGB, with EVERY PIXEL OPAQUE, because a JPEG has no alpha channel.
 *
 * A `decode` array is refused as it is everywhere, and A CORRUPT OR TRUNCATED FILE IS REFUSED RATHER
 * THAN FATAL: libjpeg's own default error handler calls exit(3), which this seam replaces with its own
 * and an escape through setjmp, so a caller of this library gets NULL like they would from any other
 * refusal. The seam is CGImageJPEG.c. */
CGImageRef CGImageCreateWithJPEGDataProvider(CGDataProviderRef provider, const CGFloat *decode,
					     bool shouldInterpolate,
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

/* THE TYPE IDENTITY OF THIS CLASS: a `CFTypeID`, the same for every object of the class and
 * different from every other class's. The value is THIS LIBRARY'S (Apple's are runtime-assigned and
 * published nowhere), which is why the header says so rather than implying a constant someone could
 * port; identity is the whole of what the door promises. See CGTypeID_internal.h. */
CFTypeID CGImageGetTypeID(void);

#endif /* CORE_GRAPHICS_CGIMAGE_H */
