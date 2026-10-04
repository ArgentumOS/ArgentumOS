/*
 * CGColorSpace_internal.h — the seam between the colour space and the colour.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * NOT INSTALLED, NOT INCLUDED BY ANY PUBLIC HEADER, and named the way libconfig_internal.h is
 * for the same reason: one translation unit knows how a colour space is built, and one other
 * needs to ask it for the engine's handle. The alternative was to publish the struct, which
 * would make every field part of the ABI.
 *
 * THE HANDLE IS `void *` AND NOT `cmsHPROFILE`, DELIBERATELY: so that this header — and
 * therefore any file that includes it only for the accessor — does not have to pull in
 * `<lcms2.h>`. lcms2's own `cmsHPROFILE` is a `void *`, so the value passes through both ways
 * without a cast at the seam.
 */
#ifndef CORE_GRAPHICS_CGCOLORSPACE_INTERNAL_H
#define CORE_GRAPHICS_CGCOLORSPACE_INTERNAL_H

#include <CoreGraphics/CGColorSpace.h>

/* THE ENGINE'S PROFILE FOR A SPACE, OR NULL. NULL IS THE ANSWER FOR EVERY DEVICE SPACE, and
 * that is not a gap: a device space means "these numbers are what you blend", so there is
 * nothing to interpret and nothing to convert FROM. It is also the answer for a space with no
 * colour model the engine can build a profile for — device CMYK, today. A caller that gets
 * NULL must either copy the components as they stand (which is right for device RGB and device
 * grayscale) or refuse. */
void *cg_colorspace_engine_profile(CGColorSpaceRef space);

/* A SPACE FROM AN ENGINE PROFILE THAT IS ALREADY OPEN, WHICH IS THE HALF OF THE CONSTRUCTION
 * THAT LIVES WITH THE STRUCT. The named spaces are built from an Objective-C translation unit —
 * the only place a name can be compared — but a `CGColorSpace` is private to CGColorSpace.c, so
 * the profile comes here to be wrapped. ON SUCCESS THE SPACE OWNS THE PROFILE and closes it when
 * it is released; on failure the caller still owns it and must close it, which is why the two
 * sides of this seam both mention the same rule. */
CGColorSpaceRef cg_colorspace_from_profile(void *profile, CGColorSpaceModel model,
					   size_t components);

#endif /* CORE_GRAPHICS_CGCOLORSPACE_INTERNAL_H */
