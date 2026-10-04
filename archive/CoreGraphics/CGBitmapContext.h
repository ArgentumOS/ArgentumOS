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

/* THE ALPHA AND BYTE-ORDER VOCABULARY MOVED TO `CGImage.h`, WHERE APPLE HAS IT: it is one
 * vocabulary for images and contexts alike, and keeping it here made these two headers CIRCULAR,
 * because this one needs `CGContext.h` and that one needs `CGImage.h` for the type it draws. The
 * reasoning behind the byte-order spelling travelled with the types. */
#include <CoreGraphics/CGImage.h>

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
