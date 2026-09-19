/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSObjCRuntime.h — the scalar types, the comparison enum, ranges, and
 * NSNotFound. docs/design/foundation-plan.md, the public-API audit (item B).
 *
 * THESE ARE THE NAMES COCOA-SHAPED CODE SPELLS. This Foundation had been using
 * `unsigned long`, `int` and `size_t` directly, which is source-compatible in
 * most positions but fails the moment a caller NAMES a type: `for (NSUInteger i
 * = 0; ...)` did not compile at all before this header existed, and neither did
 * `NSComparisonResult`, `NSRange` or `NSNotFound`. Where Cocoa has a type, the
 * type is Cocoa's — there is nothing to gain by inventing one.
 *
 * NO ZONES AT ALL (2026-09-18). `NSZone` IS NO LONGER DECLARED HERE, and that is
 * the end of a sequence the user drove: first every method that TOOK an `NSZone`
 * was removed, then every method that RETURNED one (`-zone`), and with nothing
 * left able to name it the TYPE went too. Zones are 32-bit API and this system is
 * 64-bit only; Apple's own words, on the `NSZone` page: "Zones are ignored on iOS
 * and 64-bit runtime in macOS. You should not use zones in current development."
 * (It was declared here, incomplete, from F0 until 2026-09-18 so that Cocoa's
 * `-copyWithZone:` / `-mutableCopyWithZone:` SHAPE could be reproduced — the
 * plan's §7 decision, and the audit's B4.)
 */

#ifndef FOUNDATION_NSOBJCRUNTIME_H
#define FOUNDATION_NSOBJCRUNTIME_H

#include <limits.h>
#include <objc/objc.h>	/* BOOL, YES, NO */

/*
 * NULLABILITY (F6). These are the STANDARD spellings, and they are defined here
 * because the sweep needs them: a header wraps its declarations in BEGIN/END and
 * then annotates only the exceptions. `_Nonnull` and `_Nullable` are clang
 * keywords, so only the two region macros need defining — and the fallback keeps a
 * non-clang compiler seeing exactly the header it saw before.
 */
#if defined(__clang__)
#	define NS_ASSUME_NONNULL_BEGIN	_Pragma("clang assume_nonnull begin")
#	define NS_ASSUME_NONNULL_END	_Pragma("clang assume_nonnull end")
#else
#	define NS_ASSUME_NONNULL_BEGIN
#	define NS_ASSUME_NONNULL_END
#endif

typedef signed long NSInteger;
typedef unsigned long NSUInteger;

#define NSIntegerMax	LONG_MAX
#define NSIntegerMin	LONG_MIN
#define NSUIntegerMax	ULONG_MAX

/*
 * Cocoa's "no such index / nothing found" sentinel, and it is worth being
 * explicit about why this type exists at all: before the audit, -indexOfObject:
 * answered `(NSUInteger)-1`, and `(NSUInteger)-1 != NSNotFound`. Code written
 * the Cocoa way — `if ([array indexOfObject:x] == NSNotFound)` — silently never
 * matched. The sentinel is NSIntegerMax, NOT -1.
 */
#define NSNotFound	NSIntegerMax

typedef enum {
	NSOrderedAscending = -1,
	NSOrderedSame = 0,
	NSOrderedDescending = 1
} NSComparisonResult;

typedef struct {
	NSUInteger location;
	NSUInteger length;
} NSRange;


/*
 * RANGES ARE FUNCTIONS HERE, NOT COCOA'S MACROS, and the reason is MEASURED: as
 * macros, `NSLocationInRange(2, range)` failed to compile in this toolchain with
 * "expected identifier" pointing INTO the macro name — while its one-parameter
 * neighbour NSMaxRange worked and NSMakeRange worked, and the fully-expanded
 * expression compiled fine on its own. Rather than debug the preprocessor, the
 * three are functions: they cannot be mis-expanded, they are type-checked, they
 * take the Cocoa spelling, and NSMakeRange stops being a compound literal (which
 * C++ mode does not accept either).
 */
static inline NSRange NSMakeRange(NSUInteger location, NSUInteger length)
{
	NSRange range;

	range.location = location;
	range.length = length;
	return range;
}

static inline NSUInteger NSMaxRange(NSRange range)
{
	return range.location + range.length;
}

static inline BOOL NSLocationInRange(NSUInteger location, NSRange range)
{
	return (location >= range.location &&
		location - range.location < range.length) ? YES : NO;
}

typedef unsigned short unichar;

/* The block comparator the sorted/sort ...UsingComparator: forms take, and the
 * options for the binary-search family. Both are Cocoa's spellings. */
typedef NSComparisonResult (^NSComparator)(id object1, id object2);

typedef enum {
	NSBinarySearchingFirstEqual = 1,
	NSBinarySearchingLastEqual = 2,
	NSBinarySearchingInsertionIndex = 4
} NSBinarySearchingOptions;

/*
 * Cocoa spells this on its nil-terminated variadic methods. The attribute only
 * means something for C functions, so as written here it is documentation that
 * keeps the spelling compiling.
 */
#define NS_REQUIRES_NIL_TERMINATION

/*
 * NSGetSizeAndAlignment (W2e): the size and alignment of a type ENCODING, and the pointer past
 * what was consumed. Apple declares it here, and this file can hold it because it involves no
 * Objective-C type — `const char *` in and `NSUInteger *` out, which is also why this header
 * needs no nullability region (it is one of the four the gate exempts by name).
 */
const char *NSGetSizeAndAlignment(const char *typePtr, NSUInteger *sizep, NSUInteger *alignp);

/* THE DEBUG SWITCHES (W2g): globals a program reads and sets. This library does not act on any
 * of them yet — the allocation machinery that would consult them is not built — and
 * NSFoundationVersionNumber is this library's OWN number, stated in its implementation rather
 * than borrowed from a release it is not. */
extern BOOL NSDebugEnabled;
extern BOOL NSZombieEnabled;
extern BOOL NSDeallocateZombies;
extern BOOL NSKeepAllocationStatistics;
extern double NSFoundationVersionNumber;

#endif /* FOUNDATION_NSOBJCRUNTIME_H */
