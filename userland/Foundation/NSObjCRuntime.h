/*
 * NSObjCRuntime.h — Foundation's value and runtime types: aliased to CF's where they ARE the same type, and
 * defined from APPLE'S shape where the two disagree.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * WHY THE FIRST QUESTION IS "DOES CF ALREADY HAVE IT", AND WHY THE ANSWER IS SOMETIMES NO. CoreFoundation
 * declares the same family — `CFRange {CFIndex location; CFIndex length;}`, `CFIndex`, `CFOptionFlags`,
 * `CFRangeMake`, and `static const CFIndex kCFNotFound = -1` — and this library's rule is that CF is the
 * structural core, so a value type should be TAKEN from CF rather than invented. Two of them genuinely are
 * the same type and are ALIASES here:
 *
 *     CFIndex        == signed long   == Apple's NSInteger
 *     CFOptionFlags  == unsigned long == Apple's NSUInteger
 *
 * AND TWO ARE NOT, WHICH IS THE WHOLE REASON THIS HEADER IS NOT A PAGE OF TYPEDEFS TO CF:
 *
 *   * `CFRange.location` is a SIGNED `CFIndex`; Apple's `NSRange.location` is `NSUInteger`. CF's own
 *     functions assert that a range is non-negative, so aliasing the two would turn a negative location that
 *     Cocoa wrapping-accepts into a CF assertion — a behaviour change dressed as a convenience.
 *   * CF's missing-index sentinel is `kCFNotFound == -1`; Apple's `NSNotFound` is `NSIntegerMax`. Those are
 *     DIFFERENT NUMBERS — `(NSUInteger)-1 != NSIntegerMax` — and the archived library's own note records
 *     exactly what that costs: "Code written the Cocoa way — `if ([array indexOfObject:x] == NSNotFound)` —
 *     silently never [matched]." NSArray returned `(NSUInteger)kCFNotFound` until this unit, and the ledger
 *     marks `NSNotFound` as OWED, so the Cocoa value is the one that ships and the doors translate at CF's
 *     boundary.
 *
 * SO THE RULE THIS HEADER SETS FOR THE REST OF THE FAMILY: alias where the types are identical, define
 * Apple's where the SEMANTICS differ, and never let a sentinel's spelling decide a branch by accident.
 *
 * AND IT IS APPLE'S HOME FOR THE NULLABILITY MACROS, which is where they now live (they were defined in
 * NSObject.h before). `tools/foundation-gate.py` has always exempted the name `NSObjCRuntime.h` with the
 * reason "it DEFINES the two macros; a region there would be circular" — and until this file existed that
 * reason described a header that was not in the tree.
 */

#ifndef FNX_FOUNDATION_NSOBJCRUNTIME_H
#define FNX_FOUNDATION_NSOBJCRUNTIME_H

#include <objc/runtime.h>
#include <CoreFoundation/CFBase.h>
#include <limits.h>

/*
 * APPLE'S NULLABILITY SPELLING. The classes in this library annotate with the `_Nullable`/`_Nonnull` KEYWORDS;
 * these two macros are the same note in Apple's spelling, and they are defined so a header written to APPLE'S
 * convention compiles — NSException.h is the first such. DEFINING THEM OPENS NO REGION: the pragma is emitted
 * where a header WRITES the macro, so this pair is a vocabulary rather than a switch.
 */
#ifndef NS_ASSUME_NONNULL_BEGIN
#define NS_ASSUME_NONNULL_BEGIN	_Pragma("clang assume_nonnull begin")
#define NS_ASSUME_NONNULL_END		_Pragma("clang assume_nonnull end")
#endif

/* THE ALIASES — same type by construction, so there is one definition rather than two that can drift. */
typedef CFIndex NSInteger;
typedef CFOptionFlags NSUInteger;

/* Apple's value for the largest NSInteger, spelled the way Apple spells it (NSObjCRuntime.h: LONG_MAX). */
#ifndef NSIntegerMax
#define NSIntegerMax	LONG_MAX
#endif

/* NOT an alias. See the header: the two sentinels are different numbers, and this is the one Cocoa code tests. */
#define NSNotFound	NSIntegerMax

/* AND NOT an alias either: unsigned here, signed in CFRange. */
typedef struct _NSRange {
	NSUInteger location;
	NSUInteger length;
} NSRange;

typedef NSRange *NSRangePointer;

static inline NSRange NSMakeRange(NSUInteger location, NSUInteger length)
{
	NSRange range;

	range.location = location;
	range.length = length;
	return range;
}

#endif	/* FNX_FOUNDATION_NSOBJCRUNTIME_H */
