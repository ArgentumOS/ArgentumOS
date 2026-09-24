/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * CGPattern.h — a CELL the caller draws, tiled.
 *
 * A PATTERN IS A DRAWING CALLBACK PLUS A TILING LATTICE, AND THE ORDER OF THOSE TWO THINGS IS THE
 * WHOLE DESIGN. A gradient and a shading supply their colours as data — a list of stops, or a
 * function — and this library interprets them. A pattern supplies a DRAWING: the caller is handed a
 * context and paints one cell of their choosing into it, and everything this library then does is
 * repeat that cell. Which means the pattern is the FIRST place in this library where a paint is
 * produced by the SAME drawing code a caller uses — the cell is drawn through `CGContextFillPath`
 * and friends, by the caller's callback, into a context this library made.
 *
 * THE CELL IS DRAWN IN PATTERN SPACE, AND THE MATRIX IS HOW PATTERN SPACE RELATES TO THE USER SPACE
 * OF WHOEVER DRAWS IT. `bounds` positions the cell within its tile and gives its size; `matrix` maps
 * pattern space onto the context's user space; `xStep`/`yStep` are the spacing between tiles, which
 * may exceed the cell's size (a lattice with gaps) but not be smaller than it in the direction where
 * the cell would overlap itself. Everything a caller recognises as "where the pattern is" — scale,
 * rotation, position, and `CGContextSetPatternPhase` — is those three things and nothing else.
 *
 * TWO BOUNDARIES ARE STATED HERE RATHER THAN DISCOVERED, AND BOTH ARE REFUSALS BY NAME:
 *
 *   * A COLOURED PATTERN ONLY. Apple's other kind is a STENCIL pattern (`isColored = false`), whose
 *     cell is drawn in the colour the context has at draw time — so the same pattern paints a
 *     different picture under a different fill colour, and honouring it means re-drawing the cell
 *     for every colour and threading the fill state INTO the callback. This library refuses it by
 *     name instead of drawing a cell that ignores the colour it was supposed to use.
 *   * ONE TILING. Of the three `CGPatternTiling` values, `kCGPatternTilingNoDistortion` is what this
 *     library does: the cell is tiled by a rigid lattice in pattern space, so a rotated or scaled
 *     matrix rotates and scales the tiles with it. The two `…ConstantSpacing…` values ask for the
 *     spacing to be held constant in DEVICE space instead, allowing the cell to be distorted or
 *     clipped to fit — a different algorithm producing a different picture, and accepting the
 *     request while drawing the other thing is exactly the silent wrong answer this tree refuses.
 */
#ifndef CORE_GRAPHICS_CGPATTERN_H
#define CORE_GRAPHICS_CGPATTERN_H

#include <CoreGraphics/CGBase.h>
#include <CoreGraphics/CGAffineTransform.h>
#include <CoreGraphics/CGGeometry.h>

/*
 * FORWARD, AND NOT AN INCLUDE, AND THE CYCLE IS WHY: `CGContext.h` includes THIS header, because
 * `CGContextSetFillPattern` takes a `CGPatternRef`. So this header cannot include that one back.
 * The type is a pointer to an incomplete struct, which is all a callback signature needs, and the
 * spelling is the same one `CGContext.h` uses — a repeated typedef of the same type, which C11 and
 * `-std=gnu11` allow and which is what keeps the two headers independent.
 */
typedef struct CGContext *CGContextRef;

typedef struct CGPattern *CGPatternRef;

/*
 * EVERY CASE NAME IS APPLE'S; the values are this tree's ordering of Apple's names, as everywhere
 * Apple publishes names and no numbers. TWO OF THE THREE ARE REFUSED AT `CGPatternCreate` — see the
 * header note above — and they are DECLARED anyway, because a caller's `switch` over the enum, and a
 * caller who passes one of them and reads the refusal, both need the name to exist.
 */
typedef enum {
	kCGPatternTilingNoDistortion,
	kCGPatternTilingConstantSpacingMinimalDistortion,
	kCGPatternTilingConstantSpacing
} CGPatternTiling;

/* THE CALLER'S TWO CALLBACKS, the same pair a `CGFunction` has: `drawPattern` paints one cell into
 * the context this library hands it, and `releaseInfo` is called once when the pattern is released,
 * which is where an `info` the caller allocated is freed. */
typedef void (*CGPatternDrawPatternCallback)(void *info, CGContextRef context);
typedef void (*CGPatternReleaseInfoCallback)(void *info);

/* `version` must be 0, for the reason `CGFunctionCallbacks` states it: it is how a struct like this
 * grows a field without an old binary reading the new layout. */
typedef struct CGPatternCallbacks {
	unsigned int version;
	CGPatternDrawPatternCallback drawPattern;
	CGPatternReleaseInfoCallback releaseInfo;
} CGPatternCallbacks;

/*
 * REFUSED RATHER THAN ACCEPTED: a NULL `callbacks`, a `version` that is not zero, a NULL
 * `drawPattern`, an `isColored` of false, a tiling that is not `kCGPatternTilingNoDistortion`, a
 * `bounds` or a step with no size, and a step smaller than the cell in a direction where the tiles
 * would then overlap themselves. Each would leave a pattern this library could only draw wrongly.
 */
CGPatternRef CGPatternCreate(void *info, CGRect bounds, CGAffineTransform matrix, CGFloat xStep,
			     CGFloat yStep, CGPatternTiling tiling, bool isColored,
			     const CGPatternCallbacks *callbacks);

/* Lifetime. Releasing the last reference calls the caller's `releaseInfo` exactly once. */
CGPatternRef CGPatternRetain(CGPatternRef pattern);
void CGPatternRelease(CGPatternRef pattern);

#ifdef __cplusplus
}
#endif

#endif /* CORE_GRAPHICS_CGPATTERN_H */
