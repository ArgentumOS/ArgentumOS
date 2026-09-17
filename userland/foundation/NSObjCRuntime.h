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
 * NO ZONES. `NSZone` is an INCOMPLETE type whose only purpose is to let Cocoa's
 * `-copyWithZone:` / `-mutableCopyWithZone:` SHAPE be reproduced (the plan's §7
 * decision, and the audit's B4). The argument is accepted, ignored and
 * documented: there is one allocator and no zone API, so an NSZone * is never
 * dereferenced, and no function here takes one.
 */

#ifndef FOUNDATION_NSOBJCRUNTIME_H
#define FOUNDATION_NSOBJCRUNTIME_H

#include <limits.h>
#include <objc/objc.h>	/* BOOL, YES, NO */

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

typedef struct _NSZone NSZone;		/* incomplete on purpose — see "NO ZONES" */

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

#endif /* FOUNDATION_NSOBJCRUNTIME_H */
