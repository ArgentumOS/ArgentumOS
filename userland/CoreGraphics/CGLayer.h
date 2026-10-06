/*
 * CGLayer — a surface you draw into and then draw somewhere else.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * A LAYER IS AN OFFSCREEN SURFACE WITH A SIZE: `CGLayerGetContext` hands back a context whose drawing goes into
 * the layer, and `CGContextDrawLayerInRect` puts the layer's contents into another context's user space. THAT
 * ROUND TRIP IS THE WHOLE TYPE, which is why the four doors that make it up ship together — a layer that could
 * be drawn into but never drawn out would be a declaration rather than a feature.
 *
 * THREE THINGS COME STRAIGHT FROM APPLE'S HEADER AND ARE QUOTED RATHER THAN PARAPHRASED: the size is "specified
 * in default user space (base space) units"; `auxiliaryInfo` "should be NULL; it is reserved for future
 * expansion"; and drawing at a point "is equivalent to calling `CGContextDrawLayerInRect' with a rectangle
 * having origin at `point' and size equal to the size of `layer'". The last one is implemented by CALLING that
 * function, so the equivalence cannot drift.
 *
 * AND TWO ARE THIS LIBRARY'S, STATED: a layer's surface is a 32-bit RGB bitmap, because that is the only chart
 * this library's contexts are built for — a layer made from a gray context still has an RGB surface, so a
 * caller moving between them gets the same colours and not the same color space. A size is ROUNDED UP to whole
 * pixels, because a surface is made of pixels and rounding down would lose a row or column of what the caller
 * asked for.
 *
 * `CGLayerRetain` and `CGLayerRelease` are NULL-SAFE, which the header states as a difference from CFRetain and
 * CFRelease — so a NULL layer is not a refusal here, it is a no-op.
 */
#ifndef CORE_GRAPHICS_CGLAYER_H
#define CORE_GRAPHICS_CGLAYER_H

#include <CoreGraphics/CGBase.h>
#include <CoreGraphics/CGContext.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CGLayer *CGLayerRef;

/* THE LAYER'S SURFACE IS THIS LIBRARY'S ONLY BITMAP CHART, whatever the creating context was; see the note
 * above. The SIZE is in the creating context's user space and is rounded up to whole pixels. */
CGLayerRef CGLayerCreateWithContext(CGContextRef context, CGSize size, NSDictionary *auxiliaryInfo);

/* THE CONTEXT OWNS ITS SURFACE AND THE LAYER OWNS THE CONTEXT, so a caller may draw into the returned context
 * but must not release it: `CGLayerRelease` does that. */
CGContextRef CGLayerGetContext(CGLayerRef layer);

/* THE SIZE ASKED FOR, in the units it was asked for in — NOT the rounded-up pixel size of the surface, which
 * is an implementation detail of this library rather than part of Apple's answer. */
CGSize CGLayerGetSize(CGLayerRef layer);

/* THE ROUND TRIP. InRect SCALES the layer to fit the rectangle, as Apple's header says it does; AtPoint is the
 * same call with the layer's own size, as Apple's header says it is. */
void CGContextDrawLayerInRect(CGContextRef context, CGRect rect, CGLayerRef layer);
void CGContextDrawLayerAtPoint(CGContextRef context, CGPoint point, CGLayerRef layer);

/* NULL-SAFE, unlike the CF calls they are equivalent to. */
CGLayerRef CGLayerRetain(CGLayerRef layer);
void CGLayerRelease(CGLayerRef layer);

/* The type identity, as every class in this library publishes it. */
CGTypeID CGLayerGetTypeID(void);

#ifdef __cplusplus
}
#endif

#endif /* CORE_GRAPHICS_CGLAYER_H */
