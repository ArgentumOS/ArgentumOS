/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * CGGeometry.h — the three geometry value types, with Apple's field names, order and
 * layout. Nothing else: see CGBase.h for why the value types are here and CG's function
 * surface is not.
 */
#ifndef CORE_GRAPHICS_CGGEOMETRY_H
#define CORE_GRAPHICS_CGGEOMETRY_H

#include <CoreGraphics/CGBase.h>

typedef struct CGPoint {
	CGFloat x;
	CGFloat y;
} CGPoint;

typedef struct CGSize {
	CGFloat width;
	CGFloat height;
} CGSize;

typedef struct CGRect {
	CGPoint origin;
	CGSize size;
} CGRect;

#endif /* CORE_GRAPHICS_CGGEOMETRY_H */
