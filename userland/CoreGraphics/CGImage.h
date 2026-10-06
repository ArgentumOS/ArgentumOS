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

/* `CGBitmapInfo` IS THE TYPE THOSE CONSTANTS BELONG TO, and this library named it nowhere until now:
 * Apple's 10.6 declares the flags inside an UNNAMED enumeration and then does `typedef uint32_t
 * CGBitmapInfo;` — so the type carries the mask values and the enumeration has no name of its own,
 * which is why a reader looking for `enum CGBitmapInfo` in their header will not find one. The
 * constants above keep this file's own spelling; what was missing was the name that gathers them. */
typedef uint32_t CGBitmapInfo;

#define kCGBitmapAlphaInfoMask 0x1Fu

/* THE FLOAT-COMPONENTS FLAG, Apple's second bit in this family and the one this file was missing:
 * `1 << 8` sits ABOVE the alpha mask (0x1F) and BELOW the byte-order bits (0x7000), so a caller ORs it
 * with either without either noticing. It says the samples are floating point, which is a fact about
 * the DATA a caller hands over rather than about the color space. */
#define kCGBitmapFloatComponents (1 << 8)

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
			 size_t bytesPerRow, CGColorSpaceRef space, CGBitmapInfo bitmapInfo,
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
CGBitmapInfo CGImageGetBitmapInfo(CGImageRef image);
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
CGTypeID CGImageGetTypeID(void);

/* ------------------------------------------------------------------------- */
/* Masks, and the three ways an image is derived from another                  */
/* ------------------------------------------------------------------------- */

/* THE DECODE ARRAY, WHICH MAPS THE IMAGE'S SAMPLES INTO THE RANGES ITS COLOR SPACE EXPECTS. Apple's rule,
 * read from its documentation rather than recalled: THE ARRAY HOLDS 2N VALUES, {min[1], max[1], ... min[N],
 * max[N]}, WHERE N IS THE NUMBER OF COMPONENTS OF THE IMAGE'S COLOR SPACE (one for gray, three for RGB — the
 * same N, and the same wording, as CGImageCreateWithMaskingColors uses above), each value being a VALID IMAGE
 * SAMPLE VALUE (0 to 255 for this library's 8-bit images), and the mapping is LINEAR: a sample s of component
 * i becomes `min[i] + (s / 255) * (max[i] - min[i])`.
 *
 * THE ALPHA COMPONENT IS NOT DECODED, because N counts the COLOR SPACE's components and a color space has no
 * alpha: the modern documentation's parenthetical "(including the alpha component)" contradicts its own
 * worked example, which gives six entries for RGB. THE SPECIFICATION CLAUSE WINS HERE and the divergence is
 * stated rather than guessed at.
 *
 * `CGImageGetDecode` returns the array the image was made with, or NULL when it has none — "if remapping of
 * the image's color values is not allowed, the returned value will be NULL", which for this library means
 * exactly "no decode array was given". */
const CGFloat *CGImageGetDecode(CGImageRef image);

/* A MASK IS AN IMAGE WITH NO COLOR SPACE WHOSE SAMPLES ARE THE MASK ITSELF, and drawing one directly is an
 * error: it is made to be CLIPPED WITH or to mask another image. This library therefore marks it as not
 * drawable, which `cg_image_is_drawable` already refuses in one place rather than in every drawing door.
 *
 * ITS ALPHA LAYOUT IS `kCGImageAlphaOnly` — "no color data, alpha data only" — which is this library's own
 * existing reading of a mask (see CGImage.c's format note). `CGImageGetColorSpace` answers NULL for one,
 * as Apple's does, because a mask has no colors to be in a space.
 *
 * `decode` IS REFUSED HERE FOR THE SAME REASON `CGImageCreate` REFUSES IT: it maps input ranges onto output
 * ones, and ignoring it would draw something the caller did not describe. */
CGImageRef CGImageMaskCreate(size_t width, size_t height, size_t bitsPerComponent, size_t bitsPerPixel,
			     size_t bytesPerRow, CGDataProviderRef provider, const CGFloat *decode,
			     bool shouldInterpolate);

/* True for an image made by `CGImageMaskCreate`, false for every other image, and false for NULL. */
bool CGImageIsMask(CGImageRef image);

/* THE STRUCTURE AND NOT THE BYTES: Apple's own comment says so — "Only the image structure itself is copied;
 * the underlying data is not." The copy shares the provider and the color space with the original and holds
 * a reference to each, which is what keeps the bytes alive for as long as either image needs them. */
CGImageRef CGImageCreateCopy(CGImageRef image);

/* A copy with a different color space, and THE SPACE MUST HOLD THE SAME NUMBER OF COMPONENTS or this returns
 * NULL (Apple's rule, and the one that keeps a caller from reinterpreting the same bytes as a different
 * shape). A MASK HAS NO SPACE TO REPLACE, so a mask returns NULL too. */
CGImageRef CGImageCreateCopyWithColorSpace(CGImageRef image, CGColorSpaceRef space);

/* THE MASK DOORS, AND APPLE'S TWO RULES ARE DIFFERENT RULES. `CGImageCreateWithMask` takes either an
 * IMAGE MASK — whose sample S is an INVERSE alpha, so S=1 paints nothing and S=0 paints fully — or a
 * PICTURE, which serves as an alpha mask whose sample S is the alpha directly. An image used as a mask must
 * be DeviceGray, must have no alpha, and must not itself be masked; and the picture being masked must be a
 * picture that is not already masked, by either door. All four are REFUSED BY NAME here rather than assumed.
 *
 * THE RESULT DRAWS THROUGH THE MASK: the picture keeps its own bytes and the mask is applied where the
 * picture is painted, which is why a derived image still shares the original's provider.
 *
 * THE MASK IS SAMPLED BY NORMALIZED POSITION — (0,0) to (1,1) across each — because the header says nothing
 * about the two being the same size, and this is the reading that lets them differ: a mask stretched over
 * the rectangle the picture is drawn into. */
CGImageRef CGImageCreateWithMask(CGImageRef image, CGImageRef mask);

/* MASKING COLORS: `components` holds 2N values { min[1], max[1], ... min[N], max[N] } where N IS THE NUMBER
 * OF COMPONENTS OF THE IMAGE'S COLOR SPACE (one for a gray image, three for RGB), and ANY SAMPLE WHOSE
 * COMPONENTS ALL FALL INSIDE THEIR RANGES IS MASKED OUT — left unpainted. Every value must be a valid sample
 * value, which for this library's 8-bit images means 0 to 255, and anything else is refused rather than
 * clamped: a caller who wrote 300 meant something this door cannot do. */
CGImageRef CGImageCreateWithMaskingColors(CGImageRef image, const CGFloat *components);

/* THE SUBRECTANGLE, IN APPLE'S OWN THREE STEPS, which the header documents in that order: the rectangle is
 * made integral, intersected with the image's bounds, and the pixels inside the result are referenced —
 * THE BYTES ARE NOT COPIED. The new image retains the original, so a caller may release the original the
 * moment this returns. A null or empty result returns NULL. */
CGImageRef CGImageCreateWithImageInRect(CGImageRef image, CGRect rect);

#endif /* CORE_GRAPHICS_CGIMAGE_H */
