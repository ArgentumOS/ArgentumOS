/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * CGBitmapContext.h — the offscreen destination: a context whose surface is a block
 * of memory the caller can read.
 *
 * THE IMPLEMENTATION IS IN CGContext.c, DELIBERATELY. A bitmap context is a context
 * with a particular surface, so the constructor, the surface accessors and the
 * rasterizer that fills it are one translation unit; a second file would exist only to
 * hold a forward declaration. What matters here is the API, and the API is Apple's.
 *
 * THE PIXEL FORMAT IS THE ONE COMBINATION C2 SUPPORTS, AND IT IS NAMED EXACTLY:
 * 8 bits per component, 32 bits per pixel, `kCGImageAlphaPremultipliedFirst` with
 * `kCGBitmapByteOrder32Little`, which is this implementation's `pixman_format_code_t`
 * `PIXMAN_a8r8g8b8` — 32-bit words whose most significant byte is alpha, stored
 * little-endian, so the BYTES IN MEMORY ARE B, G, R, A in that order.
 *
 * THAT LAST SENTENCE IS A MEASUREMENT, NOT A TRANSCRIPTION, and it is the honest form
 * of an answer this tree cannot check: Apple's headers would say which of the two
 * readings of "premultiplied first" they mean, and they are not readable here. Rather
 * than guess in the dark, the probe asserts the four bytes of a known red pixel, so
 * the binding between the Apple constants and these bytes is PINNED AND TESTABLE. If
 * Apple's reading is the other one, the fix is one alias in CGContext.c and the probe
 * is what will say so.
 *
 * A NULL `data` asks the context to allocate and own the buffer, which is freed with
 * the context; a caller-supplied buffer belongs to the caller, who must keep it alive
 * for the context's lifetime.
 */
#ifndef CORE_GRAPHICS_CGBITMAPCONTEXT_H
#define CORE_GRAPHICS_CGBITMAPCONTEXT_H

#include <CoreGraphics/CGBase.h>
#include <CoreGraphics/CGColorSpace.h>
#include <CoreGraphics/CGContext.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The alpha/byte-order half of `CGBitmapContextCreate`'s `bitmapInfo`. The four values
 * that matter to a 32-bit context are here; the rest of Apple's set (`…SkippedLast`,
 * the 16- and 64-bit orders, the float components) is not, and a caller who needs one
 * gets a compile error rather than a context that silently reinterprets the bytes. */
typedef enum {
	kCGImageAlphaNoneSkipFirst = 4,
	kCGImageAlphaNoneSkipLast = 5,
	kCGImageAlphaPremultipliedFirst = 2,
	kCGImageAlphaPremultipliedLast = 1
} CGImageAlphaInfo;

/* THE BYTE ORDER LIVES IN THE `CGImageByteOrderInfo` FAMILY, NOT THE
 * `kCGBitmapByteOrder*` ONE, AND THAT IS THE LEDGER'S DOING: Apple's index carries all
 * four `kCGBitmapByteOrder*` value cases as DEPRECATED, and this tree's standing policy
 * is that a deprecated name is neither shipped nor owed — the sweep flags it as a policy
 * finding and `--strict` fails on it. The live spelling is the same bits under the
 * modern name (`kCGImageByteOrder32Little` is 2 << 12, exactly as
 * `kCGBitmapByteOrder32Little` was), so nothing about the pixels changed; what changed
 * is which namespace this tree declares. The two masks are here because the
 * `bitmapInfo` word is a packed pair and masking it with a named constant beats masking
 * it with a literal. */
typedef enum {
	kCGImageByteOrderDefault = 0,
	kCGImageByteOrder32Little = 2 << 12,
	kCGImageByteOrder32Big = 4 << 12
} CGImageByteOrderInfo;

#define kCGBitmapAlphaInfoMask 0x1Fu
#define kCGBitmapByteOrderInfoMask 0xF000u

CGContextRef CGBitmapContextCreate(void *data, size_t width, size_t height,
				   size_t bitsPerComponent, size_t bytesPerRow,
				   CGColorSpaceRef space, uint32_t bitmapInfo);

/* The surface accessors. `GetData` is the pointer the drawing has already landed in
 * — a bitmap context needs no flush for its bytes to be current. */
void *CGBitmapContextGetData(CGContextRef context);
size_t CGBitmapContextGetWidth(CGContextRef context);
size_t CGBitmapContextGetHeight(CGContextRef context);
size_t CGBitmapContextGetBytesPerRow(CGContextRef context);
size_t CGBitmapContextGetBitsPerComponent(CGContextRef context);
size_t CGBitmapContextGetBitsPerPixel(CGContextRef context);
CGImageAlphaInfo CGBitmapContextGetAlphaInfo(CGContextRef context);
uint32_t CGBitmapContextGetBitmapInfo(CGContextRef context);
CGColorSpaceRef CGBitmapContextGetColorSpace(CGContextRef context);

#ifdef __cplusplus
}
#endif

#endif /* CORE_GRAPHICS_CGBITMAPCONTEXT_H */
