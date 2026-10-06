/*
 * CGTypeID_internal — the table of type identities, and why the values are ours.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * ONE REGISTRY, ONE FILE, FIXED NUMBERS. Every `CG*GetTypeID` door in this library answers from the table
 * below, so the two properties the API exists for hold by construction rather than by luck: THE SAME CLASS
 * ALWAYS ANSWERS THE SAME ID (a constant, not an allocation), and TWO CLASSES NEVER SHARE ONE (the numbers
 * are distinct and assigned once, here).
 *
 * WHAT IS DUPLICATED AND WHAT IS NOT: the CONTRACT is Apple's — a `CFTypeID` for Core Graphics objects, which
 * is what `CFGetTypeID` would compare against — and the VALUES are not, because Apple's are assigned by its
 * runtime at load time and appear in no header or documentation. A caller can therefore compare ids (ours
 * against ours) exactly as Apple's documentation describes, and cannot port a constant, which no caller
 * does. THE TREE HAS NO COREFOUNDATION, so there is no `CFGetTypeID` to agree with; when one exists, the
 * agreement to make is that it answers THESE numbers for these classes.
 *
 * ONE IS NOT ZERO, AND ZERO IS THE INVALID ID: the numbering starts at 1 so that a zeroed structure, an
 * uninitialised field or a refusal can never be mistaken for a class identity.
 */
#ifndef CORE_GRAPHICS_CGTYPEID_INTERNAL_H
#define CORE_GRAPHICS_CGTYPEID_INTERNAL_H

#include <CoreGraphics/CGBase.h>

/* THE TABLE. Adding a class means adding a line HERE and a door in CGTypeID.c — which is the whole point of
 * one file owning it: no id is ever picked at a call site. */
#define CG_TYPE_ID_CGCOLOR		1u
#define CG_TYPE_ID_CGCOLORSPACE		2u
#define CG_TYPE_ID_CGCONTEXT		3u
#define CG_TYPE_ID_CGDATAPROVIDER	4u
#define CG_TYPE_ID_CGFONT		5u
#define CG_TYPE_ID_CGFUNCTION		6u
#define CG_TYPE_ID_CGGRADIENT		7u
#define CG_TYPE_ID_CGIMAGE		8u
#define CG_TYPE_ID_CGPATH		9u
#define CG_TYPE_ID_CGPATTERN		10u
#define CG_TYPE_ID_CGSHADING		11u

#endif /* CORE_GRAPHICS_CGTYPEID_INTERNAL_H */
