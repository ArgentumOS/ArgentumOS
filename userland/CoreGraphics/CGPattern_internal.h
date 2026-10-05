/*
 * CGPattern_internal.h — how a context turns a pattern into a paint.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * NOT INSTALLED, NOT INCLUDED BY ANY PUBLIC HEADER, named the way the other internal headers are.
 *
 * THE CELL IS RENDERED ONCE PER PAINT, NOT ONCE PER PATTERN, AND THAT IS A DECISION RATHER THAN A
 * LACK OF ONE. The cell is whatever the caller's `drawPattern` produces, and only the caller knows
 * whether that depends on something that changes between one fill and the next — their `info`. A
 * cache inside the pattern object would be faster and would one day draw a stale cell, which is the
 * kind of failure nobody attributes to the cache. So the context asks for a fresh cell when it needs
 * one and lets it go when the fill is done: two calls, and no invalidation rule to get wrong. The
 * cost is bounded by the cell's own size, which is what `bounds` is.
 *
 * WHY THE CELL IS A `cg_pattern_cell` AND NOT A CONTEXT: the caller's callback needs a context to
 * draw INTO, and this library needs the finished BYTES to sample from. Handing both around as one
 * object keeps the two halves of that in step — the cell owns the context it drew into and the
 * bitmap that context wrote to, and `cg_pattern_cell_free` releases both in the right order.
 */
#ifndef CORE_GRAPHICS_CGPATTERN_INTERNAL_H
#define CORE_GRAPHICS_CGPATTERN_INTERNAL_H

#include <CoreGraphics/CGGeometry.h>
#include <CoreGraphics/CGPattern.h>

typedef struct cg_pattern_cell cg_pattern_cell;

/* Renders the pattern's cell by calling the caller's `drawPattern` once, into a bitmap of the cell's
 * own size, with pattern space's `bounds.origin` at the bitmap's lower-left. Returns NULL if there is
 * no callback or no memory — and the caller then paints nothing, which is visible. */
cg_pattern_cell *cg_pattern_render_cell(CGPatternRef pattern);
void cg_pattern_cell_free(cg_pattern_cell *cell);

/* THE TILE SAMPLE. `x` and `y` are in the DRAWING context's USER space, `phase` is the offset
 * `CGContextSetPatternPhase` last set for that context, and `rgba` receives STRAIGHT components with
 * the alpha last — four zeros where the point falls in a GAP between cells or outside the cell's own
 * bounds inside its tile, which composites as a no-op under every operator pixman has. */
void cg_pattern_sample(CGPatternRef pattern, cg_pattern_cell *cell, CGSize phase, CGFloat x,
		       CGFloat y, CGFloat rgba[4]);

#endif /* CORE_GRAPHICS_CGPATTERN_INTERNAL_H */
