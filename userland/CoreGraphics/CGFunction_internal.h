/*
 * CGFunction_internal.h — how a shading asks a caller's function for a colour.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * NOT INSTALLED, NOT INCLUDED BY ANY PUBLIC HEADER, named the way the other internal headers are.
 *
 * ONE CALL, AND THE SIZES ARE WHY IT IS A FUNCTION RATHER THAN A DIRECT CALL TO THE CALLBACK. A
 * shading must hand the caller's `evaluate` an array of exactly `domainDimension` inputs and an array
 * of exactly `rangeDimension` outputs, and it must then know whether the result was clamped — that is
 * the whole of what this does. A shading that called the callback itself would have to make those
 * three checks itself, and a second caller would make them a second way.
 */
#ifndef CORE_GRAPHICS_CGFUNCTION_INTERNAL_H
#define CORE_GRAPHICS_CGFUNCTION_INTERNAL_H

#include <CoreGraphics/CGFunction.h>

size_t cg_function_domain_dimension(CGFunctionRef function);
size_t cg_function_range_dimension(CGFunctionRef function);

/* Calls the caller's `evaluate` with `in` and writes `range_dimension` values into `out`, CLAMPED
 * into the function's range when it declared one. Does nothing when the function or the callback is
 * absent, so a caller with a half-built function cannot crash on it. */
void cg_function_evaluate(CGFunctionRef function, const CGFloat *in, CGFloat *out);

#endif /* CORE_GRAPHICS_CGFUNCTION_INTERNAL_H */
