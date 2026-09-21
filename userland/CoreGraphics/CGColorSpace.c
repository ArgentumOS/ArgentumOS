/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * CGColorSpace.c — the two device colour spaces, and nothing else (see the header).
 *
 * A SINGLETON, NOT A FRESH OBJECT, and that is not an optimization: the device spaces
 * carry no state, so two objects would be two answers to "is this the device RGB
 * space?" — a question `CGColorSpaceGetModel` cannot answer between two instances and
 * a caller has every right to compare. `CGColorSpaceRetain`/`Release` move a
 * refcount; `Release` on a device space with no other holder leaves it alive, which
 * is Apple's documented behaviour for these (they are process-wide).
 */
#include <CoreGraphics/CGColorSpace.h>

#include <stdlib.h>

struct CGColorSpace {
	int refcount;
	CGColorSpaceModel model;
	size_t components;
};

static struct CGColorSpace cg_device_rgb = { 0, kCGColorSpaceModelRGB, 3 };
static struct CGColorSpace cg_device_gray = { 0, kCGColorSpaceModelMonochrome, 1 };
/* FOUR COMPONENTS, ALPHA NOT COUNTED, like the other two: the count belongs to the SPACE, and
 * a colour adds its alpha on top of it. */
static struct CGColorSpace cg_device_cmyk = { 0, kCGColorSpaceModelCMYK, 4 };

CGColorSpaceRef CGColorSpaceCreateDeviceRGB(void)
{
	cg_device_rgb.refcount++;
	return &cg_device_rgb;
}

CGColorSpaceRef CGColorSpaceCreateDeviceGray(void)
{
	cg_device_gray.refcount++;
	return &cg_device_gray;
}

CGColorSpaceRef CGColorSpaceCreateDeviceCMYK(void)
{
	cg_device_cmyk.refcount++;
	return &cg_device_cmyk;
}

CGColorSpaceRef CGColorSpaceRetain(CGColorSpaceRef space)
{
	if (space != NULL) {
		space->refcount++;
	}
	return space;
}

void CGColorSpaceRelease(CGColorSpaceRef space)
{
	if (space != NULL && space->refcount > 0) {
		space->refcount--;
	}
	/* NOT FREED, EVEN AT ZERO: these are the process-wide device spaces, and
	 * `CGColorSpaceCreateDeviceRGB()` may be called again at any time. A caller that
	 * balanced its retain/release has done what the contract asks; the object simply
	 * is not destroyed, which is what "no parameters, one answer" costs. */
}

CGColorSpaceModel CGColorSpaceGetModel(CGColorSpaceRef space)
{
	if (space == NULL) {
		return kCGColorSpaceModelUnknown;
	}
	return space->model;
}

size_t CGColorSpaceGetNumberOfComponents(CGColorSpaceRef space)
{
	if (space == NULL) {
		return 0;
	}
	return space->components;
}
