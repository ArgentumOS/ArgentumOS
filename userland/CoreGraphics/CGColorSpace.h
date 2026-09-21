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
 * THE REST IS C4 (docs/design/coregraphics-plan.md §7), and it is arriving in pieces rather
 * than as stubs: `CGColorSpaceCreateLab` is here, because a Lab space is what gives the engine
 * something to CONVERT and therefore something to check. Still absent, and still deliberate:
 * the ICC-based and indexed spaces (an ICC space needs a data provider, an indexed one a
 * table), the calibrated spaces, the extended-range and HDR predicates, the colour table and
 * the property-list serialization. A stub that returned a plausible object for
 * `CGColorSpaceCreateICCBased` would be worse than an absent symbol, because the pixels would
 * look right and be wrong.
 *
 * AND THE ENGINE IS BOUND NOW (lcms2, vendored at 2.19.1 by tools/lcms2-build.sh, linked by
 * this library): a space built from PARAMETERS carries the engine's profile, and
 * `cg_colorspace_engine_profile` is the private seam the colour half asks through. THE FIELD
 * IS ALSO THE KIND — a profile means the components have to be INTERPRETED, no profile means
 * they are what you blend — which is what closes the gap recorded against C4.1, where the
 * guard dispatched on the colour model alone and so would have copied a calibrated-RGB colour
 * as if it were a device one.
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
 * CMYK IS HERE FOR THE COLOUR IT MAKES POSSIBLE RATHER THAN FOR A DRAWING PATH: a CMYK colour
 * can be created and inspected — its four components and its alpha are just numbers, and the
 * model says what they mean — while the CONTEXT still REFUSES to draw with it, because four
 * inks are not three lights and THERE IS NO DEVICE-CMYK PROFILE for the engine to convert
 * through. It is now the ONLY refused colour that can be built, which is exactly the role it
 * was added for: without it, the only refusal a probe could reach was a NULL.
 *
 * AND Lab IS THE OPPOSITE CASE, which is why it arrives with the engine rather than after it.
 * A Lab space HAS a profile — lcms2 builds one from the white point alone — so a Lab colour is
 * drawn by CONVERTING it, not by refusing it. The two spaces together are what let the colour
 * probe show both sides of the line this design draws. */
CGColorSpaceRef CGColorSpaceCreateDeviceRGB(void);
CGColorSpaceRef CGColorSpaceCreateDeviceGray(void);
CGColorSpaceRef CGColorSpaceCreateDeviceCMYK(void);
/* A NULL `whitePoint` is D50, which is Apple's documented default and exactly what the engine's
 * own Lab4 profile uses — so the default is not an approximation of Apple's, it is the same
 * thing. The black point and the range are accepted and have no effect, which is Apple's
 * documented behaviour for a Lab space as well. */
CGColorSpaceRef CGColorSpaceCreateLab(const CGFloat *whitePoint, const CGFloat *blackPoint,
				      const CGFloat *range);

CGColorSpaceRef CGColorSpaceRetain(CGColorSpaceRef space);
void CGColorSpaceRelease(CGColorSpaceRef space);

CGColorSpaceModel CGColorSpaceGetModel(CGColorSpaceRef space);
size_t CGColorSpaceGetNumberOfComponents(CGColorSpaceRef space);

#ifdef __cplusplus
}
#endif

#endif /* CORE_GRAPHICS_CGCOLORSPACE_H */
