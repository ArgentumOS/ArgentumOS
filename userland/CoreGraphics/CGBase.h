/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * CGBase.h — the base of this tree's CoreGraphics, and it is FIRST-PARTY.
 *
 * WHY THIS EXISTS AT ALL (user's decision, 2026-09-18): Apple's Foundation declares six
 * conversions between its geometry and CoreGraphics' — NSPointFromCGPoint and friends — plus
 * the macro that says the two type sets are identical. Those seven ledger rows cannot ship
 * without CG's VALUE TYPES, and the types are published interface: structs and a typedef,
 * no implementation to take. So this tree defines them, under Apple's spelling, in
 * `userland/CoreGraphics/` — which `-Iuserland` makes visible to every userland compile and
 * the kernel (which uses `-Iinclude` only) never sees.
 *
 * WHAT IS *NOT* HERE: this header is the VALUE TYPES only, and that was the right first step —
 * those seven ledger rows needed the types and nothing else, and CG's structs and a typedef are
 * published interface with no implementation to take.
 *
 * SUPERSEDED (2026-09, user's decision): CG's function surface and its DRAWING HALF are now to
 * be built, as a duplication of Apple's drawing API — docs/design/coregraphics-plan.md. The
 * earlier reasoning that "the drawing half is already answered elsewhere in this tree by
 * X11/Xfb" is what the plan KEEPS and what it changes: the display stays X11/Xfb, and CG's
 * drawing sits on top of it instead of replacing it. Do not read the paragraph below as the
 * current decision; it is this file's history.
 *
 * WHAT THE FIRST VERSION SAID, kept because its reasoning still binds the substrate: CG's
 * function surface (CGPointMake, CGRectGetMinX, the affine transforms, CGColor, the drawing
 * contexts and everything they imply) is CG's own API and stays out of THIS HEADER — §12.6's
 * rule that a dependency is added rather than refused applies to it separately, in the plan
 * named above.
 */
#ifndef CORE_GRAPHICS_CGBASE_H
#define CORE_GRAPHICS_CGBASE_H

/*
 * `<stddef.h>` FOR `size_t`, WHICH SEVERAL CG HEADERS TAKE — `CGBitmapContextCreate`'s
 * width, height and strides, `CGColorSpaceGetNumberOfComponents`. Apple's headers get
 * it transitively from CoreFoundation's; this tree has no CoreFoundation (the plan
 * retracted that, §1), so the header that IS the base states it instead of leaving
 * every other header to rediscover it — which is exactly what happened when C2's first
 * build failed on `unknown type name 'size_t'` in two headers at once, and then again on
 * `uint32_t` in `CGBitmapContextCreate`'s `bitmapInfo`. Both are stated here.
 *
 * AND `<stdbool.h>` FOR `bool`, ADDED WHEN THE THIRD HEADER NEEDED IT RATHER THAN THE FIRST.
 * `bool` is what a comparison returns — `CGPathIsEmpty`, `CGColorEqualToColor` — so CGPath.h
 * included it, and then CGContext.h did too, and then CGColor.h did not: the third instance
 * of a lesson the paragraph above already records twice. The base states it now, so a fourth
 * header cannot repeat it; the two that include it for themselves keep doing so, because a
 * header that may be read alone should say what it uses.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * AND THE FOUNDATION OBJECT TYPES THE SIGNATURES NAME, FOR THE SAME REASON `bool` IS HERE: more
 * than one header takes one. The binding is decided — CoreGraphics here declares its signatures
 * with FOUNDATION types rather than inventing a CF layer (the retracted CoreFoundation plan) — so
 * `CGColorSpaceCreateWithName` takes an `NSString *` and the `…WithCFData`/`…ICCData` forms take
 * an `NSData *`.
 *
 * A C COMPILER CANNOT SEE `@interface NSString`, so under `__OBJC__` these are forward
 * declarations of the classes and otherwise the opaque structs a class is at the ABI level. A C
 * caller passes and receives those pointers and never looks inside them; the Objective-C
 * translation units that do look import Foundation. THE SPELLING WAS MEASURED BEFORE IT WAS
 * WRITTEN — both directions compile, link and run, and the linker agreeing is what "the same
 * type" means across two languages — and stating it ONCE here is what keeps a third header from
 * rediscovering it.
 *
 * APPLE'S HEADERS SOLVE THE SAME PROBLEM UNDER ANOTHER NAME: their `CFStringRef` is a bridged
 * declaration in a C header. There is no CF here, so the spelling is the Foundation class itself.
 *
 * `NSArray` JOINED THIS LIST WHEN THE FIRST SIGNATURE NEEDED ONE — `CGGradientCreateWithColors`
 * (C6.1), whose Apple form is a `CFArrayRef` of `CGColorRef`. The class is named here for the same
 * reason as the other two and NOT because an array takes an arbitrary place in these signatures: it
 * is the Foundation counterpart of Apple's `CFArrayRef`, and what may live INSIDE it is that
 * function's business, not this header's.
 */
#ifdef __OBJC__
@class NSArray;
@class NSData;
@class NSMutableData;
@class NSString;
@class NSURL;
#else
typedef struct objc_object NSArray;
typedef struct objc_object NSData;
typedef struct objc_object NSMutableData;
typedef struct objc_object NSString;
typedef struct objc_object NSURL;
#endif

/*
 * AND THE `ns_returns_retained` ATTRIBUTE, WHICH IS THE OTHER THING A C COMPILER CANNOT USE. A
 * `Copy`-named function returns +1, and an Objective-C caller under ARC has to be told so or it
 * will treat the result as +0 and leak. To a C compiler these functions return a `struct
 * objc_object *`, so the attribute is ignored there — and clang says as much under `-Wextra`,
 * which is how this macro came to exist: the attribute must be ON for an Objective-C translation
 * unit and ABSENT for a C one, and a macro is the only way to say that once. Apple's headers spell
 * the same problem `CF_RETURNS_RETAINED`; there is no CF here, so this is it.
 */
#ifdef __OBJC__
#define CG_RETURNS_RETAINED __attribute__((ns_returns_retained))
#else
#define CG_RETURNS_RETAINED
#endif

/*
 * CGFloat IS DOUBLE ON 64-BIT, which is what Apple's is under __LP64__, and this tree is
 * 64-bit only (no 32-bit compatibility exists here at all).
 */
typedef double CGFloat;

#define CG_INLINE static inline
#ifndef CG_EXTERN
#define CG_EXTERN extern
#endif

/* THE TYPE IDENTITY, AND IT IS COREGRAPHICS-NATIVE HERE. Apple's is `CFTypeID`, declared in
 * `<CoreFoundation/CFBase.h>`, and every `CG*GetTypeID` door returns one. THIS TREE HAS NO COREFOUNDATION to
 * assign ids, so the type is this library's own and named for the framework that mints it: `CGTypeID` IS THE
 * TYPE and Apple's spelling is `typedef CGTypeID CFTypeID` — ONE LINE, AND IT STAYS, because the acceptance
 * this duplication is held to is that source written against Apple's headers compiles unmodified, and
 * `CFTypeID t = CGImageGetTypeID();` is ordinary Apple source. The two are THE SAME TYPE, exactly as they are
 * in Apple's own headers, where `CFTypeID` is CoreFoundation's name for an identity Core Graphics minted.
 *
 * WHAT IT MEANS IS APPLE'S: AN OPAQUE IDENTITY, THE SAME FOR EVERY OBJECT OF ONE CLASS AND DIFFERENT FOR
 * TWO. WHAT CANNOT BE COPIED IS THE VALUE: Apple's ids are assigned by its runtime at load and published
 * nowhere, so ours are this library's own — stable across runs, unique per class — and IDENTITY IS THE WHOLE
 * OF WHAT THE CONTRACT PROMISES. See CGTypeID_internal.h for the registry and CGTypeID.c for the doors. */
typedef unsigned long CGTypeID;
typedef CGTypeID CFTypeID;

/* ------------------------------------------------------------------------- */
/* The macros: the float type, and the three annotations                       */
/* ------------------------------------------------------------------------- */

/* APPLE'S MACROS, WITH THE BRANCH ALREADY TAKEN. Their `CGBase.h` decides between `float` and
 * `double` per architecture and defines `CGFLOAT_TYPE`, `CGFLOAT_IS_DOUBLE`, `CGFLOAT_MIN` and
 * `CGFLOAT_MAX` accordingly; THIS TREE IS x86-64 ONLY, so the 64-bit arm is the only one there is and
 * the values are Apple's own for it — `double`, `DBL_MIN`, `DBL_MAX`.
 *
 * `CGFLOAT_DEFINED` IS THE GUARD APPLE'S OWN HEADERS USE to tell whether `CGFloat` has already been
 * declared (their `CGGeometry.h` wraps the typedef in it), so it means "CGFloat IS DEFINED HERE" and
 * is 1, as theirs is. `CGFLOAT_MIN` and `CGFLOAT_MAX` are the largest and smallest MAGNITUDES of the
 * type rather than its most negative number — about 2.2e-308 and 1.8e308 for `double` — which is
 * what `DBL_MIN`/`DBL_MAX` mean and why Apple names the macros that way. */
#include <float.h>

#define CGFLOAT_TYPE double
#define CGFLOAT_IS_DOUBLE 1
#define CGFLOAT_MIN DBL_MIN
#define CGFLOAT_MAX DBL_MAX
#define CGFLOAT_DEFINED 1

/* `CG_LOCAL` IS "VISIBLE ACROSS THE FRAMEWORK, NOT OUTSIDE IT", and `CG_PRIVATE_EXTERN` is its older
 * name for the same idea — Apple's header defines the second AS the first, and this one does the same
 * rather than inventing a third meaning. Apple's own expansion is `__private_extern__` where the
 * compiler has it and `CG_EXTERN` otherwise, and THIS LIBRARY TAKES THE SECOND ARM: it is built as
 * one library with no cross-file hidden symbols to protect, so what the macro has to mean here is
 * what a caller sees, and `CG_EXTERN` is that. */
#define CG_LOCAL CG_EXTERN
#define CG_PRIVATE_EXTERN CG_LOCAL

/* `CG_OBSOLETE` MARKS A DECLARATION THAT TIME HAS PASSED, AND THIS LIBRARY DEFINES IT AS NOTHING.
 * That is a decision and not an omission: this duplication exists so that OLDER SOURCE COMPILES, and
 * a header that raised a deprecation warning at the caller would punish exactly the use it is for.
 * Apple's macro expands to `__attribute__((deprecated))` where the compiler has it; here the name
 * exists, spells nothing, and the historical fact it would carry belongs in a ledger row's `why`. */
#define CG_OBSOLETE

#endif /* CORE_GRAPHICS_CGBASE_H */
