/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * CGColorSpace.h — the colour space, as far as C2 needs one.
 *
 * A BITMAP CONTEXT CANNOT BE CREATED WITHOUT ONE, which is the whole reason this
 * header exists in the C2 milestone: `CGBitmapContextCreate` takes a colour space and
 * the pixels' meaning depends on it. What is here is the device spaces — the two that
 * have no profile and no colour management because device RGB *is* the assumption C2
 * draws under.
 *
 * THE REST IS C4 (docs/design/coregraphics-plan.md §7), deliberately not stubbed:
 * the ICC-based spaces, the calibrated and indexed spaces, the extended-range and
 * HDR predicates, the colour table and the property-list serialization. A stub that
 * returned a plausible object for `CGColorSpaceCreateICCBased` would be worse than
 * an absent symbol, because the pixels would look right and be wrong.
 *
 * AND THE COLOUR MANAGEMENT ENGINE IS DECIDED (lcms2, §5 of the plan) but not yet
 * bound: the device spaces need no transform, and C2's probe asserts exact bytes, so
 * a colour-managed path would only be able to make those assertions harder to state.
 */
#ifndef CORE_GRAPHICS_CGCOLORSPACE_H
#define CORE_GRAPHICS_CGCOLORSPACE_H

#include <CoreGraphics/CGBase.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CGColorSpace *CGColorSpaceRef;

/* THE CASE VALUES ARE THIS TREE'S, as everywhere a `…Model` enum appears: Apple
 * publishes the case names and no numbers, and nothing here depends on which number
 * is which. The ORDER is Apple's documented order, so a reader comparing the two
 * lists finds them in the same sequence. */
typedef enum {
	kCGColorSpaceModelUnknown = -1,
	kCGColorSpaceModelMonochrome = 0,
	kCGColorSpaceModelRGB = 1,
	kCGColorSpaceModelCMYK = 2,
	kCGColorSpaceModelLab = 3,
	kCGColorSpaceModelDeviceN = 4,
	kCGColorSpaceModelIndexed = 5,
	kCGColorSpaceModelPattern = 6,
	kCGColorSpaceModelXYZ = 7
} CGColorSpaceModel;

/* THE DEVICE SPACES. Each is a shared singleton: a device space has no parameters, so two
 * callers asking for device RGB get the same object and the refcount is what keeps it alive.
 *
 * CMYK IS HERE FOR THE COLOUR IT MAKES POSSIBLE RATHER THAN FOR A DRAWING PATH. This library
 * has no conversion engine yet (C4.2), so a CMYK COLOUR can be created and inspected — its
 * four components and its alpha are just numbers — while the CONTEXT REFUSES to draw with it,
 * because four inks are not three lights. That refusal is a design claim of CGColor's, and
 * without a CMYK space there was no way to exercise it with a colour that exists: the only
 * refusal a probe could reach was a NULL. */
CGColorSpaceRef CGColorSpaceCreateDeviceRGB(void);
CGColorSpaceRef CGColorSpaceCreateDeviceGray(void);
CGColorSpaceRef CGColorSpaceCreateDeviceCMYK(void);

CGColorSpaceRef CGColorSpaceRetain(CGColorSpaceRef space);
void CGColorSpaceRelease(CGColorSpaceRef space);

CGColorSpaceModel CGColorSpaceGetModel(CGColorSpaceRef space);
size_t CGColorSpaceGetNumberOfComponents(CGColorSpaceRef space);

#ifdef __cplusplus
}
#endif

#endif /* CORE_GRAPHICS_CGCOLORSPACE_H */
