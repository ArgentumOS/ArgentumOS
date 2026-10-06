/*
 * CGContext_internal — THE CONTEXT PRIMITIVES THAT ARE OURS NOW, and why they are not in CGContext.h.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * ONE FUNCTION SO FAR, AND IT IS HERE FOR THE REASON CGPath_internal.h states: the SURFACE is what a
 * public header declares, and `CGContextResetClip` — absent from the 10.6 headers, so out of era — is
 * not part of it. THE CAPABILITY IS NOT A NAME THOUGH: this library keeps ONE clip, on the context's
 * graphics state, and the AppKit's `-setClip:` is a REPLACE rather than an intersect, so something has
 * to clear that region and its mask before the new clip goes on. In the era AppKit answered that from
 * ITS OWN clip stack; here the stack is CoreGraphics', which is why the primitive is internal to this
 * library rather than invented in the AppKit.
 *
 * AN ERA CALLER REPLACES A CLIP THE WAY THE ERA DID: `CGContextSaveGState`/`RestoreGState`, or
 * `CGContextClipToRect` over the whole surface.
 */
#ifndef CORE_GRAPHICS_CGCONTEXT_INTERNAL_H
#define CORE_GRAPHICS_CGCONTEXT_INTERNAL_H

#include <CoreGraphics/CGContext.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Back to the WHOLE SURFACE: the clip region and the clip mask together, which is what "reset" has to
 * mean for the next clip to be a replacement rather than an intersection. */
void cg_context_reset_clip(CGContextRef context);

#ifdef __cplusplus
}
#endif

#endif /* CORE_GRAPHICS_CGCONTEXT_INTERNAL_H */
