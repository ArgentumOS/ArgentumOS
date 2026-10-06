/*
 * CGTypeID — every `CG*GetTypeID` door in this library, in one file.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * WHY ONE FILE: the answer is a CONSTANT, and the two properties the API promises — the same class always
 * answers the same id, and two classes never share one — are properties of the TABLE, not of any call site.
 * A door defined beside its class would put the numbers in eleven places and make them agree by review.
 *
 * WHICH DOORS ARE MISSING, AND WHY THAT IS NOT AN OVERSIGHT: `CGDataConsumerGetTypeID` and
 * `CGLayerGetTypeID` are owed because their CLASSES are — this library has no `CGDataConsumer` and no
 * `CGLayer` — and a type id for a type that does not exist would be a number with nothing to identify.
 */
#include <CoreGraphics/CGTypeID_internal.h>

CFTypeID CGColorGetTypeID(void)
{
	return (CFTypeID)CG_TYPE_ID_CGCOLOR;
}

CFTypeID CGColorSpaceGetTypeID(void)
{
	return (CFTypeID)CG_TYPE_ID_CGCOLORSPACE;
}

CFTypeID CGContextGetTypeID(void)
{
	return (CFTypeID)CG_TYPE_ID_CGCONTEXT;
}

CFTypeID CGDataProviderGetTypeID(void)
{
	return (CFTypeID)CG_TYPE_ID_CGDATAPROVIDER;
}

CFTypeID CGFontGetTypeID(void)
{
	return (CFTypeID)CG_TYPE_ID_CGFONT;
}

CFTypeID CGFunctionGetTypeID(void)
{
	return (CFTypeID)CG_TYPE_ID_CGFUNCTION;
}

CFTypeID CGGradientGetTypeID(void)
{
	return (CFTypeID)CG_TYPE_ID_CGGRADIENT;
}

CFTypeID CGImageGetTypeID(void)
{
	return (CFTypeID)CG_TYPE_ID_CGIMAGE;
}

CFTypeID CGPathGetTypeID(void)
{
	return (CFTypeID)CG_TYPE_ID_CGPATH;
}

CFTypeID CGPatternGetTypeID(void)
{
	return (CFTypeID)CG_TYPE_ID_CGPATTERN;
}

CFTypeID CGShadingGetTypeID(void)
{
	return (CFTypeID)CG_TYPE_ID_CGSHADING;
}

