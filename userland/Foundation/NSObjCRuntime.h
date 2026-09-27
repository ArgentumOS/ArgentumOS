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

/* APPLE'S POINTER SPELLING FOR A RANGE, declared beside the range itself (it was a ledger row on its
 * own: §23's NSIndexSet unit is what needed it). */
typedef NSRange *NSRangePointer;


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

/*
 * HOW MUCH A THREAD OR OPERATION MATTERS TO THE USER (2026-09-20), and it is declared
 * HERE because Apple declares it here - the type is not NSOperation's or NSThread's,
 * both of which name it. The ORDER carries the meaning (Background < Utility <
 * UserInitiated < UserInteractive) and Default is 0 because it says "unspecified"
 * rather than "lowest".
 *
 * THE VALUES ARE OURS (§11.6.1 D2). Apple's happen to be 0x21/0x19/0x11/0x09/-1, and
 * the names are the API there rather than the numbers: a program compares against the
 * constants, not the literals. Third-party BINDING documentation publishes those
 * numbers (Microsoft's .NET binding, the Dart objective_c package), which is how we
 * know they exist at all - and they are still not Apple's to copy, so ours stand.
 */
typedef enum {
	NSQualityOfServiceDefault = 0,
	NSQualityOfServiceBackground = 1,
	NSQualityOfServiceUtility = 2,
	NSQualityOfServiceUserInitiated = 3,
	NSQualityOfServiceUserInteractive = 4
} NSQualityOfService;

/*
 * THE MACRO SURFACE (2026-09-19). Apple's documentation declares this family and
 * WE DID NOT, so ~80 names a Cocoa-shaped program spells were simply absent —
 * `NS_ENUM`, `NS_OPTIONS`, `FOUNDATION_EXPORT`, the availability spellings, and
 * the handful of real constants.
 *
 * WHAT APPLE PUBLISHES, MEASURED RATHER THAN ASSUMED: each page gives the
 * macro's SIGNATURE (`#define NS_OPTIONS(_type, _name)`, `#define
 * FOUNDATION_EXPORT`, `#define NS_AVAILABLE(_mac, _ios)`) and its description —
 * and NOT the expansion body. So the signatures below are Apple's, transcribed,
 * while the bodies are ours and chosen to do what the description says. By
 * §11.5's own test that is not a difference: a body is not observable to a
 * program that writes the macro's name.
 *
 * THE AVAILABILITY SPELLINGS ARE INERT HERE, which is a documented deviation
 * (§11.6.1 D12): no compiler this system ships has a notion of macOS or iOS
 * availability, so a body using `__attribute__((availability(...)))` would warn
 * on every use — and the house build is warning-free. The parameter lists are
 * Apple's; the bodies do nothing.
 */

/* --- declaring an enum (Apple's signatures) --- */
#define NS_ENUM(_type, _name)		enum _name : _type _name; enum _name : _type
#define NS_OPTIONS(_type, _name)	enum _name : _type _name; enum _name : _type
#define NS_CLOSED_ENUM(_type, _name)	enum _name : _type _name; enum _name : _type
#define NS_ERROR_ENUM(_type, _name)	enum _name : _type _name; enum _name : _type
/* typed/extensible string enums: documented bare names */
#define NS_TYPED_ENUM
#define NS_STRING_ENUM
#define NS_EXTENSIBLE_STRING_ENUM
#define NS_TYPED_EXTENSIBLE_ENUM

/* --- the symbols a shared library exports --- */
#define FOUNDATION_EXPORT		extern
#define FOUNDATION_IMPORT		extern
#define FOUNDATION_EXTERN		extern
#define FOUNDATION_STATIC_INLINE	static inline
#define FOUNDATION_EXTERN_INLINE	extern inline

/* --- how a declaration behaves (Apple's signatures; bodies per their descriptions) --- */
#define NS_INLINE			static inline
#define NS_NOESCAPE			__attribute__((noescape))
#define NS_ROOT_CLASS			__attribute__((objc_root_class))
#define NS_RETURNS_RETAINED		__attribute__((ns_returns_retained))
#define NS_RETURNS_NOT_RETAINED		__attribute__((ns_returns_not_retained))
#define NS_RETURNS_INNER_POINTER	__attribute__((objc_returns_inner_pointer))
#define NS_REQUIRES_SUPER		__attribute__((objc_requires_super))
#define NS_REQUIRES_PROPERTY_DEFINITIONS
/* MEASURED 2026-09-20: clang 19 accepts the attribute in NEITHER documented position -
 * on the @protocol it swallows the member list ("expected identifier or '('") and on a
 * protocol method it says "only applies to Objective-C protocols". A body that breaks the
 * build the moment it is used is worse than no body, so this stays empty until the correct
 * spelling/position is found. The documented SEMANTICS (a conforming class must implement
 * such a method explicitly) are therefore NOT enforced here: an open item, not a claim. */
#define NS_PROTOCOL_REQUIRES_EXPLICIT_IMPLEMENTATION
#define NS_RELEASES_ARGUMENT		__attribute__((ns_consumed))
#define NS_REPLACES_RECEIVER		__attribute__((ns_consumes_self))
#define NS_VOIDRETURN
#define NS_NO_TAIL_CALL			__attribute__((disable_tail_calls))
/* MEASURED 2026-09-20: clang 19 rejects __attribute__((objc_arc_unavailable)) with
 * "unknown attribute ... ignored", so this one keeps an empty body like its weak-ref
 * neighbour rather than shipping an attribute the compiler throws away. */
#define NS_AUTOMATED_REFCOUNT_UNAVAILABLE
/* no counterpart in this toolchain: weak-refs are unavailable under manual retain/release, which is what this marks */
#define NS_AUTOMATED_REFCOUNT_WEAK_UNAVAILABLE
#define NS_UNICHAR_IS_EIGHT_BIT	0	/* this runtime's unichar is UTF-16, never 8-bit */
#define NSEDGEINSETS_DEFINED	1
#define NS_FALLTHROUGH			__attribute__((fallthrough))
#define NS_WARN_UNUSED_RESULT		__attribute__((warn_unused_result))
#define NS_VALID_UNTIL_END_OF_SCOPE	__attribute__((objc_precise_lifetime))
#define NS_FORMAT_FUNCTION(F, A)	__attribute__((format(__NSString__, F, A)))
#define NS_FORMAT_ARGUMENT(A)	__attribute__((format_arg(A)))
#define NS_VALUERETURN(v, t)	return (v)
#define NS_HEADER_AUDIT_BEGIN(...)
#define NS_HEADER_AUDIT_END(...)

/* --- the availability and deprecation spellings (INERT here: D12) --- */
#define NS_AVAILABLE(_mac, _ios)
#define NS_AVAILABLE_MAC(_mac)
#define NS_AVAILABLE_IOS(_ios)
#define NS_AVAILABLE_IPHONE(_ios)
#define NS_CLASS_AVAILABLE(_mac, _ios)
#define NS_CLASS_AVAILABLE_MAC(_mac)
#define NS_CLASS_AVAILABLE_IOS(_ios)
#define NS_ENUM_AVAILABLE(_mac, _ios)
#define NS_ENUM_AVAILABLE_MAC(_mac)
#define NS_ENUM_AVAILABLE_IOS(_ios)
#define NS_BLOCKS_AVAILABLE
#define NS_NONATOMIC_IOSONLY		nonatomic
#define NS_NONATOMIC_IPHONEONLY		nonatomic
#define NSURLSESSION_AVAILABLE
#define NS_UNAVAILABLE
#define NS_DEPRECATED(_macIntro, _macDep, _iosIntro, _iosDep, ...)
#define NS_DEPRECATED_MAC(_macIntro, _macDep, ...)
#define NS_DEPRECATED_IOS(_iosIntro, _iosDep, ...)
#define NS_DEPRECATED_IPHONE(_iosIntro, _iosDep)
#define NS_DEPRECATED_WITH_REPLACEMENT_MAC(_rep, _macIntroduced, _macDeprecated)
#define NS_CLASS_DEPRECATED(_mac, _macDep, _ios, _iosDep, ...)
#define NS_CLASS_DEPRECATED_MAC(_macIntro, _macDep, ...)
#define NS_CLASS_DEPRECATED_IOS(_iosIntro, _iosDep, ...)
#define NS_ENUM_DEPRECATED(_macIntro, _macDep, _iosIntro, _iosDep, ...)
#define NS_ENUM_DEPRECATED_MAC(_macIntro, _macDep, ...)
#define NS_ENUM_DEPRECATED_IOS(_iosIntro, _iosDep, ...)
#define NS_EXTENSION_UNAVAILABLE(_msg)
#define NS_EXTENSION_UNAVAILABLE_MAC(_msg)
#define NS_EXTENSION_UNAVAILABLE_IOS(_msg)
#define NS_CALENDAR_DEPRECATED(A, B, C, D, ...)
#define NS_CALENDAR_DEPRECATED_MAC(A, B, ...)
#define NS_CALENDAR_ENUM_DEPRECATED(A, B, C, D, ...)

/*
 * --- THE LEGACY EXCEPTION MACROS (2026-09-20) ---
 *
 * THE ONE PLACE A BODY CAME FROM SOMETHING OTHER THAN APPLE'S SIGNATURE. Apple renders a
 * parameterless macro as a bare `#define NAME`, publishing no body at all, which is why these
 * were left undeclared rather than guessed. GNUstep's documentation supplies the contract in
 * PROSE - its NSException page describes the macros as enclosing "a sensitive section of
 * code", states that "the local variable localException is the name of the exception object",
 * and shows it re-raised with `[localException raise]`. What they expand to below is that
 * contract and nothing more. That the handler binds that name is API, because code written
 * against the documentation refers to it.
 *
 * A PROGRAM THAT SPELLS THESE MUST HAVE NSException DECLARED, which is why such a program
 * includes <Foundation/NSException.h>: a macro is not expanded until it is used, so this
 * header needs no import for it.
 */
#define NS_DURING		@try {
#define NS_HANDLER		} @catch (NSException *localException) {
#define NS_ENDHANDLER		}

/*
 * --- the real constants ---
 *
 * ONE HAS A VALUE APPLE DOCUMENTS AS A DATE, so it is arithmetic rather than a
 * lookup: NSTimeIntervalSince1970 is "the interval between 1 January 1970 and
 * 1 January 2001" — 31 years, 8 of them leap (1972..2000), is 11,323 days.
 * NSDecimalMaxSize and NSDecimalNoScale publish a name and no number (their
 * pages show a getter and prose), so those values are ours, as §11.6.1 D2 is
 * that register row by construction.
 */
/* MIN and MAX are GUARDED, and that is a measured necessity rather than taste:
 * this system's own headers already define both (include/fnx/string.h and musl's
 * sys/param.h), so Apple's unguarded spelling would warn on every translation
 * unit that includes a system header. The macro is Apple's; the guard is ours. */
#ifndef MIN
#define MIN(A, B)	((A) < (B) ? (A) : (B))
#endif
#ifndef MAX
#define MAX(A, B)	((A) > (B) ? (A) : (B))
#endif
#define ABS(A)		((A) < 0 ? -(A) : (A))

#define NSDecimalMaxSize	8
#define NSDecimalNoScale	((short)0x7FFF)
#define NSTimeIntervalSince1970	978307200.0	/* 11,323 days x 86,400 s */
#define NSURLResponseUnknownLength	(-1)

/*
 * THE OBJECT-LIFETIME DEBUG EVENTS (2026-09-20). These are the identifiers the debug
 * machinery reports when an object's reference state changes, and they belong with the
 * switches above: NSDebugEnabled, NSKeepAllocationStatistics and these are one facility.
 * Apple documents each name's EXISTENCE - there is a page per name, rendered as a bare
 * `#define`, which is that renderer's way of showing a macro with no published body -
 * and the shared group is what identifies their meaning: autorelease, an extra retain
 * recorded and undone, and the same for the internal count.
 *
 * THE VALUES ARE OURS (§11.6.1 D2). These are codes a caller compares against rather
 * than numbers a contract fixes, so only their distinctness and order matter.
 */
#define NSObjectAutoreleasedEvent		1
#define NSObjectExtraRefIncrementedEvent	2
#define NSObjectExtraRefDecrementedEvent	3
#define NSObjectInternalRefIncrementedEvent	4
#define NSObjectInternalRefDecrementedEvent	5

/* THE LAST VERSION CONSTANT (2026-09-20), and it is NOT one the sixth exclusion struck:
 * that rule matches the per-release NSFoundationVersionNumber* names, while this one asks
 * which release first had NSFileManager resource-fork support. On this system that release
 * is this one, so it answers this library's own version rather than borrowing a number
 * from a release it is not (§11.6.1 D2, the reasoning NSFoundationVersionNumber carries). */
#define NSFoundationVersionWithFileManagerResourceForkSupport	NSFoundationVersionNumber

/* The localized-string family: Apple's signatures, bodies that read the string
 * from a bundle. They are macros, so they are not compiled until used. */
#define NSLocalizedString(key, comment) \
	[[NSBundle mainBundle] localizedStringForKey:(key) value:@"" table:nil]
#define NSLocalizedStringFromTable(key, tbl, comment) \
	[[NSBundle mainBundle] localizedStringForKey:(key) value:@"" table:(tbl)]
#define NSLocalizedStringFromTableInBundle(key, tbl, bundle, comment) \
	[(bundle) localizedStringForKey:(key) value:@"" table:(tbl)]
#define NSLocalizedStringWithDefaultValue(key, tbl, bundle, val, comment) \
	[(bundle) localizedStringForKey:(key) value:(val) table:(tbl)]


/* ===================================================================================================
 * THE FREE FUNCTIONS THAT WERE STILL MISSING (§62.52)
 *
 * Apple's `NSObjCRuntime.h` declares a handful of C functions beside the classes, and these are the ones this
 * library did not have. Each carries its own rule where the rule is a choice rather than a fact.
 *
 * TWO SIGNATURES ARE NOT APPLE'S, AND THE REASON IS A TYPE THIS LIBRARY DOES NOT HAVE RATHER THAN A PREFERENCE:
 * there is no `NSZone`, so `NSCopyObject`'s third argument is ACCEPTED AND IGNORED (a caller who passes the zone
 * Apple's header asks for still compiles, and there is nothing for the value to mean); and there is no
 * `CFTypeRef`, so `NSMakeCollectable` takes an object of this library's own.
 * =================================================================================================== */
/* THESE CARRY NO NULLABILITY SPECIFIERS, WHICH IS A DELIBERATE CHOICE ABOUT SCOPE. A file with ONE specifier is
 * checked THROUGHOUT by clang, and this header has unannotated pointers in its older declarations — so annotating
 * this block would either cascade through the whole header or leave it half-annotated. ANNOTATING `NSObjCRuntime.h`
 * IS ITS OWN UNIT and not a footnote to this one; what this block needed was the two things below. */
#include <stdarg.h>	/* va_list, for NSLogv */
@class NSString;

/* LOGGING. Apple's `NSLog` writes to STANDARD ERROR with a prefix; so does this one, and the prefix is the process
 * id, which is what a reader of a log needs and what needs no date formatter to produce. `NSLogv` is the doer. */
void NSLog(NSString *format, ...);
void NSLogv(NSString *format, va_list args);

/* PAGES. The size comes from the system that will honour it (`sysconf`) rather than from a constant written here,
 * and the rounding is the arithmetic the names promise. */
NSUInteger NSPageSize(void);
NSUInteger NSLogPageSize(void);			/* the base-two logarithm of the page size, which is what a shift needs */
NSUInteger NSRoundDownToMultipleOfPageSize(NSUInteger bytes);
NSUInteger NSRoundUpToMultipleOfPageSize(NSUInteger bytes);

/* THE WHOLE MACHINE'S RAM as the kernel reports it, NOT the memory that happens to be free: a caller who wants
 * that is asking the kernel rather than this library, and the header says which question this answers. */
NSUInteger NSRealMemoryAvailable(void);

/* A RANGE FROM ITS OWN DESCRIPTION: the format `NSStringFromRange` writes, and {0, 0} for anything else, which is
 * a stated answer rather than a refusal because a string that does not describe a range describes no range. */
NSRange NSRangeFromString(NSString *aString);

/* `NSCopyObject` IS NOT HERE, AND THE REASON IS MEASURABLE RATHER THAN A JUDGEMENT: a faithful raw byte copy must
 * give the copy a RETAIN COUNT OF ONE, and this library's runtime publishes no way to SET a count — only to read
 * one and to retain and release. A copy carrying the original's count would be a wrong count in an object nobody
 * would suspect, so the door is absent and `-copy` is the one that honours a class's own rules. */

/* THE EXTRA RETAIN COUNT. Apple's runtime keeps a count beyond the basic one and these are its doors. THIS
 * LIBRARY'S RUNTIME HAS NO SEPARATE COUNTER, so the equivalence is stated where they are defined: the extra count
 * is the retain count minus the basic one, which is the only reading of the two that agrees with `-retainCount`. */
NSUInteger NSExtraRefCount(id object);
id NSIncrementExtraRefCount(id object);
BOOL NSDecrementExtraRefCountWasZero(id object);

/* THE STACK. Level zero is what a compiler is obliged to answer for; a higher level needs a CHAINED FRAME POINTER,
 * which this build does not promise, so these answer NULL beyond the levels they can honour rather than reading a
 * frame that may not be there. `NSCountFrames` IS ABSENT for the same reason and not by oversight: counting frames
 * means walking them, and a walk that reads a bad frame pointer faults — the door is absent rather than dangerous. */
void *NSFrameAddress(NSUInteger level);
void *NSReturnAddress(NSUInteger level);

/* THE GARBAGE-COLLECTOR DOORS, WHICH THIS LIBRARY HAS BY NAME AND NOT BY COLLECTOR. Each is given the behaviour
 * Apple documents for a program that is not collected: an object IS collectable while it is valid, NOTHING is
 * reported as freed, and recording an allocation event is a no-op because there is nothing to record it for. */
id NSMakeCollectable(id anObject);
void *NSReallocateCollectable(void *pointer, NSUInteger size, NSUInteger options);
void NSRecordAllocationEvent(NSInteger event, id object);
BOOL NSIsFreedObject(id anObject);


#endif /* FOUNDATION_NSOBJCRUNTIME_H */
