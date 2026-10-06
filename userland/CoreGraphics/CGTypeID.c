/*
 * CGTypeID — every `CG*GetTypeID` door in this library, in one file.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE TYPE IS `CGTypeID`, THIS LIBRARY'S OWN NAME (CGBase.h says why, and keeps Apple's spelling as an
 * alias), and THE ANSWER IS A CONSTANT, and the two properties the API promises — the same class always
 * answers the same id, and two classes never share one — are properties of the TABLE, not of any call site.
 * A door defined beside its class would put the numbers in eleven places and make them agree by review.
 *
 * WHICH DOORS ARE MISSING, AND WHY THAT IS NOT AN OVERSIGHT: `CGDataConsumerGetTypeID` and
 * `CGLayerGetTypeID` are owed because their CLASSES are — this library has no `CGDataConsumer` and no
 * `CGLayer` — and a type id for a type that does not exist would be a number with nothing to identify.
 */
#include <CoreGraphics/CGTypeID_internal.h>

CGTypeID CGColorGetTypeID(void)
{
	return (CGTypeID)CG_TYPE_ID_CGCOLOR;
}

CGTypeID CGColorSpaceGetTypeID(void)
{
	return (CGTypeID)CG_TYPE_ID_CGCOLORSPACE;
}

CGTypeID CGContextGetTypeID(void)
{
	return (CGTypeID)CG_TYPE_ID_CGCONTEXT;
}

CGTypeID CGDataProviderGetTypeID(void)
{
	return (CGTypeID)CG_TYPE_ID_CGDATAPROVIDER;
}

CGTypeID CGFontGetTypeID(void)
{
	return (CGTypeID)CG_TYPE_ID_CGFONT;
}

CGTypeID CGFunctionGetTypeID(void)
{
	return (CGTypeID)CG_TYPE_ID_CGFUNCTION;
}

CGTypeID CGGradientGetTypeID(void)
{
	return (CGTypeID)CG_TYPE_ID_CGGRADIENT;
}

CGTypeID CGImageGetTypeID(void)
{
	return (CGTypeID)CG_TYPE_ID_CGIMAGE;
}

CGTypeID CGPathGetTypeID(void)
{
	return (CGTypeID)CG_TYPE_ID_CGPATH;
}

CGTypeID CGPatternGetTypeID(void)
{
	return (CGTypeID)CG_TYPE_ID_CGPATTERN;
}

CGTypeID CGShadingGetTypeID(void)
{
	return (CGTypeID)CG_TYPE_ID_CGSHADING;
}

