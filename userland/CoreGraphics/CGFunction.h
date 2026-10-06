/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * CGFunction.h — a C function as an object, which is what a shading's colours come from.
 *
 * WHY A FUNCTION IS AN OBJECT AT ALL, AND WHY THAT IS THE POINT OF THIS FILE. A gradient's colours
 * are a LIST someone hands over; a shading's colours are COMPUTED, and the computation is the
 * caller's — `CGShadingCreateAxial` takes a `CGFunctionRef` and this library calls it once per
 * sample. That is the "callout" the plan says lands with C6, and it is the first place in this
 * library where the API is a C ABI rather than a set of values: the caller supplies a function
 * pointer, an `info` pointer it owns, and the two have to be kept together and let go of together.
 *
 * WHAT THE OBJECT ACTUALLY HOLDS IS THE CONTRACT, NOT THE COMPUTATION. It stores the caller's
 * `info`, their `evaluate` and `releaseInfo`, and the DOMAIN and RANGE the function is stated to be
 * valid over. The evaluate callback is never wrapped or adapted; what this library does around it is
 * CLAMP the output into the range, which is Apple's reading of the `range` parameter and the reason
 * it is not decoration — a function whose output is unbounded would let a caller paint channel values
 * this library cannot represent, and clamping says so rather than wrapping.
 */
#ifndef CORE_GRAPHICS_CGFUNCTION_H
#define CORE_GRAPHICS_CGFUNCTION_H

#include <CoreGraphics/CGBase.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CGFunction *CGFunctionRef;

/* THE CALLER'S TWO CALLBACKS. `evaluate` receives the `info` the caller passed to `CGFunctionCreate`,
 * `domainDimension` input values and must write `rangeDimension` output values; `releaseInfo` is
 * called ONCE, when the function is released, and is where an `info` the caller allocated is freed.
 *
 * `in` AND `out` ARE THE CALLER'S TO READ AND WRITE, AND `out` ARRIVES AS SCRATCH. Apple does not
 * promise its contents and neither does this library — a callback that reads `out` before writing it
 * has read whatever was in the buffer. The arrays are the function's own sizes and never longer, so a
 * callback that writes exactly `rangeDimension` values cannot run off the end. */
typedef void (*CGFunctionEvaluateCallback)(void *info, const CGFloat *in, CGFloat *out);
typedef void (*CGFunctionReleaseInfoCallback)(void *info);

/*
 * `version` IS CHECKED AND MUST BE ZERO, which is Apple's rule and the reason it exists: it is how a
 * struct like this grows a field without an old binary reading the new layout. A version this library
 * does not know is REFUSED rather than read as if it were zero — the alternative is reading a field
 * that is not where this code thinks it is.
 */
typedef struct CGFunctionCallbacks {
	unsigned int version;
	CGFunctionEvaluateCallback evaluate;
	CGFunctionReleaseInfoCallback releaseInfo;
} CGFunctionCallbacks;

/*
 * `domain` is `2 * domainDimension` values — a low and a high for each input — and `NULL` means every
 * input ranges over 0…1, which is what a shading's single parameter always is. `range` is
 * `2 * rangeDimension` values and `NULL` means unbounded, in which case the output is NOT clamped.
 *
 * REFUSED RATHER THAN ACCEPTED: a NULL `callbacks`, a `version` that is not zero, a NULL `evaluate`,
 * a zero dimension, or a domain whose low exceeds its high. Each of those leaves a function this
 * library could only call wrongly.
 */
CGFunctionRef CGFunctionCreate(void *info, size_t domainDimension, const CGFloat *domain,
			       size_t rangeDimension, const CGFloat *range,
			       const CGFunctionCallbacks *callbacks);

/* Lifetime. Releasing the last reference calls the caller's `releaseInfo` exactly once. */
CGFunctionRef CGFunctionRetain(CGFunctionRef function);
void CGFunctionRelease(CGFunctionRef function);

#ifdef __cplusplus
}
#endif

/* THE TYPE IDENTITY OF THIS CLASS: a `CFTypeID`, the same for every object of the class and
 * different from every other class's. The value is THIS LIBRARY'S (Apple's are runtime-assigned and
 * published nowhere), which is why the header says so rather than implying a constant someone could
 * port; identity is the whole of what the door promises. See CGTypeID_internal.h. */
CGTypeID CGFunctionGetTypeID(void);

#endif /* CORE_GRAPHICS_CGFUNCTION_H */
