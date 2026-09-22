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

#endif /* CORE_GRAPHICS_CGIMAGE_INTERNAL_H */
