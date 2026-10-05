/*
 * CGImage_internal.h — the one question a context asks an image: can this be drawn?
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * NOT INSTALLED, NOT INCLUDED BY ANY PUBLIC HEADER, named the way the other internal headers are.
 * THE QUESTION LIVES HERE, IN THE IMAGE'S OWN FILE, RATHER THAN BEING ASKED FROM THE CONTEXT SIDE:
 * `CGImageCreate` refuses a chart this library cannot draw, so the BLIT must not decide the same
 * thing a second way — two spellings of one rule is how they come to disagree.
 */
#ifndef CORE_GRAPHICS_CGIMAGE_INTERNAL_H
#define CORE_GRAPHICS_CGIMAGE_INTERNAL_H

#include <CoreGraphics/CGImage.h>

int cg_image_is_drawable(CGImageRef image);

/* THE ONE PLACE THAT KNOWS HOW A CHART'S BYTES ARE LAID OUT. A CGImage's `bitmapInfo` describes a
 * matrix — component count, alpha layout, byte order — and BOTH the constructor and the blit have to
 * read the same cell of it: the constructor to check that `bitsPerPixel` agrees with it, the blit to
 * know where each channel actually is. Teaching the pixel loop that matrix, and teaching the
 * constructor a second version of it, is how two spellings of one rule come to disagree — so it is
 * asked here instead, once.
 *
 * `map` receives the byte offset within one pixel of the BLUE, GREEN, RED and ALPHA samples, in that
 * order, or -1 where the chart has no such channel: a grey image has no distinct blue and red, a
 * `NoneSkip*` chart stores a component that is not to be read, and a mask has no colour at all.
 * `stored` is how many components the chart actually occupies (including a padding one), and
 * `straight` says whether the stored alpha is straight rather than premultiplied, so the sampler
 * knows whether it owes the caller a multiply. A `stored` of 0 means the combination is not one this
 * library can read, and the caller must refuse it. */
void cg_image_layout(CGColorSpaceRef space, CGImageAlphaInfo alpha, uint32_t order,
		     int map[4], int *stored, int *straight);

#endif /* CORE_GRAPHICS_CGIMAGE_INTERNAL_H */
