/*
 * CGPaint_internal.h — what a paint is, when it is not one colour.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * NOT INSTALLED, NOT INCLUDED BY ANY PUBLIC HEADER, named the way the other internal headers are.
 *
 * C2 DREW WITH ONE COLOUR AND THAT WAS ENOUGH FOR IT, because a fill's colour is one number per
 * channel and pixman composites a 1×1 source repeated everywhere. A gradient, a shading and a
 * pattern are all the same problem solved once: THE SOURCE IS NOT CONSTANT, so it cannot be a 1×1
 * image — but it can still be an IMAGE, and every compositing call in CGContext.c already takes one.
 * So the whole of C6's substrate is the function below, which turns a question — "what colour is
 * USER-space (x, y)?" — into a device-space image pixman can composite through the mask the path
 * already produces.
 *
 * WHY USER SPACE AND NOT DEVICE: the caller's coordinates mean user space, and everything C6 draws
 * is described in it — a gradient's two points, a pattern's tile size and phase, a shading's
 * circles. The mapping from a pixel centre back into user space is the CTM's inverse, and doing it
 * HERE means none of the three paints has to know the CTM exists. It also means a rotated or scaled
 * CTM does the right thing for free, which is the property a per-paint implementation would each
 * have to remember.
 *
 * THE PIXEL FORMAT IS THE LIBRARY'S ONE FORMAT, and the premultiply that packs it lives here rather
 * than being written a second time for paints: `cg_premultiplied_pixel` was CGContext.c's, and C6
 * moved it to CGPaint.c so the constant colour and the sampled one cannot be packed differently.
 */
#ifndef CORE_GRAPHICS_CGPAINT_INTERNAL_H
#define CORE_GRAPHICS_CGPAINT_INTERNAL_H

#include <CoreGraphics/CGAffineTransform.h>
#include <CoreGraphics/CGGeometry.h>

#include <pixman.h>
#include <stdint.h>

/* A paint answers one question: the colour at USER-space (x, y), in STRAIGHT (unpremultiplied)
 * components 0…1, alpha last. Four zeros mean "nothing here", which composites as a no-op under
 * every operator pixman has. */
typedef void (*cg_paint_eval_fn)(void *info, CGFloat x, CGFloat y, CGFloat rgba[4]);

/* PACKS ONE COLOUR INTO THIS LIBRARY'S OWN PIXEL WORD — the format `CGBitmapContextCreate` pins,
 * `kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little`, which is memory B, G, R, A and
 * therefore pixman's `PIXMAN_a8r8g8b8` when read as a 32-bit word. ONE PLACE, so an image's bytes
 * and a context's pixels cannot disagree about what "premultiplied first" means. */
uint32_t cg_premultiplied_pixel(CGFloat r, CGFloat g, CGFloat b, CGFloat a);

/* Builds the paint as a DEVICE-space image covering the device rectangle (`px`, `py`, `w`, `h`), by
 * asking `eval` at the user-space position of each pixel's CENTRE. `ctm` is the user-to-device
 * transform; `alpha` multiplies every sample, which is the context's alpha doing what
 * `CGContextSetAlpha` promises. Returns NULL for an empty rectangle or an allocation failure; the
 * caller owns the image and releases it with `pixman_image_unref`.
 *
 * THE BUFFER BELONGS TO PIXMAN — the image is created with no bits and filled through
 * `pixman_image_get_data`, so there is no second free and no way to release the image while the
 * buffer it points at is still the caller's. */
pixman_image_t *cg_paint_image(int px, int py, int w, int h, CGAffineTransform ctm, CGFloat alpha,
			       cg_paint_eval_fn eval, void *info);

#endif /* CORE_GRAPHICS_CGPAINT_INTERNAL_H */
