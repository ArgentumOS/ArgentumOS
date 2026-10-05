/*
 * CGShading_internal.h — the one question a context asks a shading.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * NOT INSTALLED, NOT INCLUDED BY ANY PUBLIC HEADER, named the way the other internal headers are.
 *
 * ONE FUNCTION, AND THE GRADIENT HAS THREE BECAUSE THE GRADIENT'S CALLER CHOOSES THE GEOMETRY AT
 * DRAW TIME. A shading does not: `CGShadingCreateAxial` and `CGShadingCreateRadial` are two
 * constructors that fix the geometry inside the object, so the context's draw verb has nothing to
 * select and asks one question — what colour is USER-space (x, y)? — exactly as the paint evaluator
 * wants to be asked. That the geometry is stored rather than passed is the only structural difference
 * between the two paints, and it is why this header is one declaration and CGGradient_internal.h
 * is three.
 */
#ifndef CORE_GRAPHICS_CGSHADING_INTERNAL_H
#define CORE_GRAPHICS_CGSHADING_INTERNAL_H

#include <CoreGraphics/CGGeometry.h>
#include <CoreGraphics/CGShading.h>

/* Writes the colour at USER-space (x, y) into `rgba`, or four zeros when the point lies past an end
 * the shading does not extend — the same "not painted" signal the gradients use, and for the same
 * reason (a transparent sample is a no-op under every operator pixman has). */
void cg_shading_sample(CGShadingRef shading, CGFloat x, CGFloat y, CGFloat rgba[4]);

#endif /* CORE_GRAPHICS_CGSHADING_INTERNAL_H */
