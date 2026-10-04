/*
 * FNX's bridge declarations for the vendored CoreFoundation.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * WHY THIS FILE EXISTS. With the four dispatch macros real (see modification 5 in CFInternal.h) the
 * Objective-C call sites stop being swallowed as macro arguments and are actually EMITTED — and they name
 * Foundation types: `(NSArray *)array`, `(NSAttributedString *)attrStr`, `(NSUInteger)loc`,
 * `(NSRange *)effectiveRange`, `NSMakeRange(...)`. None of those names exists in this tree's CF build,
 * because while the macros were stubs no compiler ever saw them (measured: the first ObjC compile died on
 * "use of undeclared identifier 'NSArray'"). So this header supplies exactly the names the dispatch sites
 * need, and nothing else.
 *
 * A FORWARD DECLARATION, NOT AN INCLUDE, AND THAT IS THE POINT. Including <Foundation/NSString.h> here
 * would make CoreFoundation depend on Foundation — the exact inversion the plan forbids and upstream
 * avoided with its `__CFSwiftBridge` function-pointer table (§6). `@class` states what the compiler needs
 * (these are ObjC classes) and creates no dependency at all.
 *
 * NSMakeRange IS DEFINED HERE RATHER THAN DECLARED, for the same reason: it appears as an ARGUMENT inside
 * dispatch sites (CFAttributedString.c), so an `extern` would make libcorefoundation reference a symbol
 * that lives in libfoundation — a link-time cycle for a two-field struct constructor. A `static inline`
 * costs no symbol and produces the same bytes, since NSRange and CFRange are the same two CFIndex fields
 * laid out the same way. FOUNDATION NEVER INCLUDES THIS HEADER: it is force-included into CF's own
 * translation units only, which is what keeps this definition from colliding with Foundation's own
 * NSMakeRange.
 */
#ifndef __FNX_CF_BRIDGE_H__
#define __FNX_CF_BRIDGE_H__ 1

#if !defined(INCLUDE_OBJC) || !INCLUDE_OBJC
#error "fnx-cf-bridge.h is for the Objective-C build of CoreFoundation (INCLUDE_OBJC=1)"
#endif

/* Its own two includes: this file is force-included, so nothing else has run yet. */
#include <CoreFoundation/CFBase.h>   /* CFRange and CFIndex live here, not in a CFRange.h */
#include <CoreFoundation/CFStream.h>  /* CFStreamError, named by the two _cfStreamError declarations */
#include <objc/runtime.h>
#include <objc/objc-arc.h>	/* objc_retain/objc_release: the ownership arm (modification 8) */             /* objc_getClass, for modification 7 in CFArray.c */

/* The classes CF's dispatch sites cast to, as measured by scanning every cast in the subtree:
 *   grep -rhoE '\((NS|CF)[A-Za-z]+ ?\*\)' Sources/CoreFoundation/*.c
 */
@class NSArray;
@class NSAttributedString;
@class NSCalendar;
@class NSCharacterSet;
@class NSData;
@class NSDate;
@class NSDictionary;
@class NSError;
@class NSInputStream;
@class NSLocale;
@class NSMutableArray;
@class NSMutableAttributedString;
@class NSMutableCharacterSet;
@class NSMutableData;
@class NSMutableDictionary;
@class NSMutableSet;
@class NSMutableString;
@class NSNumber;
@class NSOutputStream;
@class NSSet;
@class NSString;
@class NSTimer;
@class NSTimeZone;
@class NSObject;
@class NSURL;

/* The two Foundation VALUE names the dispatch sites use. Both are spelled to match Foundation's own
 * definitions on LP64 — NSUInteger is the type of a length, and NSRange is CFRange's two fields under
 * another name (which is why the typedef is CFRange rather than a second struct: one layout, one truth). */
typedef unsigned long NSUInteger;
typedef signed long NSInteger;
typedef CFRange NSRange;

/* THE THREE ENUM-SHAPED NAMES, MIRRORED BY REPRESENTATION RATHER THAN BY APPLE'S SPELLING, and the
 * difference is measurable: Apple declares these as NS_OPTIONS(NSUInteger, ...), which is 8 bytes on
 * LP64, while THIS TREE's Foundation declares all three as plain C enums (userland/Foundation's
 * NSCalendar.h, NSString.h, NSTimeZone.h) whose representation clang makes 4 bytes. CF is the CALLER,
 * so what has to match is the CALLEE's idea of the parameter - mirroring Apple here would put eight
 * bytes on the stack for a four-byte parameter. The values are the tree's either way; only the width
 * is taken from Foundation. M2's gate is what proves this out.
 */
typedef unsigned int NSCalendarUnit;
typedef unsigned int NSStringCompareOptions;
typedef unsigned int NSTimeZoneNameStyle;
/* unichar, the UTF-16 unit Foundation's string API speaks in - and the reason it is here rather than
 * assumed: CFString.c casts to it inside dispatch sites ((unichar)...), so it must exist by name. The
 * spelling is Foundation's own. */
typedef unsigned short unichar;

/* A MINIMAL ROOT CLASS, BECAUSE @class CANNOT BE A SUPERCLASS. The declarations below need a superclass
 * and `@class NSObject;` is not one (measured: "attempting to use the forward class 'NSObject' as
 * superclass"). Declaring it here rather than including <Foundation/NSObject.h> is the same layering
 * rule as the @class list above - CF must not depend on Foundation - and it is a DECLARATION only:
 * NO IVARS, so no layout is implied and CF's view of these classes cannot disagree with the real ones
 * about anything but method names. The class OBJECT at runtime is Foundation's/libraries, which is the
 * whole point of a bridge. -Wno-objc-root-class travels with this in the build script, because a root
 * class is exactly what this declares.
 */
__attribute__((objc_root_class))
@interface NSObject
- (id)retain;
- (void)release;
@end

/* THE FIVE STRUCT-RETURNING DISPATCH SITES, AND WHY THEY ALONE NEED DECLARATIONS. Casting an UNTYPED
 * message send to a STRUCT is illegal - clang says "used type 'CFRange' where arithmetic or pointer type
 * is required" - so for these the compiler must know the selector's return type. This is exactly what
 * upstream's `__CFSwiftBridge` carries for EVERY call (its table entries are typed function pointers)
 * and what message syntax carries only when an interface is visible. MEASURED, the affected set is five
 * sites out of 217: CFRange x3 on NSCalendar (CFCalendar.c) and CFStreamError x2 on the stream classes
 * (CFStream.c). The other 212 return void, a scalar or a pointer, where the cast is fine.
 *
 * ONLY THE SELECTORS THOSE SITES NAME ARE DECLARED, not a Foundation-sized interface: the less this file
 * claims to know about Foundation, the less it can disagree with it.
 */
@interface NSCalendar : NSObject
- (CFRange)_minimumRangeOfUnit:(NSCalendarUnit)unit;
- (CFRange)_maximumRangeOfUnit:(NSCalendarUnit)unit;
- (CFRange)_rangeOfUnit:(NSCalendarUnit)smallerUnit inUnit:(NSCalendarUnit)biggerUnit forAT:(CFAbsoluteTime)at;
@end

/* AND THE SIX DOUBLE-RETURNING SITES, FOR A DIFFERENT ILLEGALITY WITH THE SAME CAUSE: casting an untyped
 * message send to a FLOATING type is rejected too ("pointer cannot be cast to type 'CFTimeInterval'"),
 * where a cast to an integer or pointer type is fine. Same rule as the structs above: message syntax
 * carries a signature only when it can see one. */
@interface NSDate : NSObject
- (CFTimeInterval)timeIntervalSinceReferenceDate;
- (CFTimeInterval)timeIntervalSinceDate:(NSDate *)otherDate;
@end

@interface NSTimer : NSObject
- (CFTimeInterval)timeInterval;
- (CFTimeInterval)tolerance;
- (CFAbsoluteTime)_cffireTime;
@end

@interface NSTimeZone : NSObject
- (CFTimeInterval)_daylightSavingTimeOffsetForAbsoluteTime:(CFAbsoluteTime)at;
- (CFTimeInterval)_nextDaylightSavingTimeTransitionAfterAbsoluteTime:(CFAbsoluteTime)at;
@end

@interface NSInputStream : NSObject
- (CFStreamError)_cfStreamError;
@end

@interface NSOutputStream : NSObject
- (CFStreamError)_cfStreamError;
@end

/* THE SMALL-OBJECT TEST, AND IT IS THE BRIDGE'S MOST IMPORTANT LINE. A `@"..."` literal of fewer than
 * nine characters is not a pointer to anything: this tree's Foundation packs the characters INTO the
 * pointer (NSTinyString.m documents the encoding: 7 bits per character from bit 57 down, a 4-bit length in
 * bits 3-6, and the tag in bits 0-2), and libobjc2 dispatches it through a SMALL-OBJECT CLASS registered
 * at that tag (NSTinyString.h: objc_registerSmallObjectClass_np). Measured, this is exactly what broke
 * the bridge: CF's dispatch read `*(void**)str` - the isa - from a packed literal and faulted on an
 * unmapped address, BEFORE any message was sent.
 *
 * THE RULE IS THE RUNTIME'S OWN, NOT A NUMBER INVENTED HERE: "the runtime dispatches any pointer whose
 * low three bits are non-zero" (NSTinyString.h), so `& 7 != 0` is the test for "this is an object, and
 * dereferencing it is WRONG". A small object answers its class's methods like any other, so CF must treat
 * it as Objective-C AND must not touch its first word. The ordering in CF_IS_OBJC matters for exactly
 * that reason: the || short-circuits, so the isa read never happens for these.
 *
 * AND IT MUST AGREE WITH FOUNDATION, which is what the bridge probe is for: if this test and
 * NSTinyString's tag ever drift apart, `corefoundation_bridge` goes red rather than a literal faulting.
 */
#define FNX_CF_IS_SMALL_OBJECT(obj) (((uintptr_t)(obj) & 0x7u) != 0)

static inline NSRange NSMakeRange(NSUInteger location, NSUInteger length) {
	NSRange range;
	range.location = (CFIndex)location;
	range.length = (CFIndex)length;
	return range;
}

#endif /* __FNX_CF_BRIDGE_H__ */

/* THE BRIDGING DOOR (modification 9): register an Objective-C class as the class of a CF type, after which
 * that type's objects are messageable objects and CF's own doors take their C path on them. */
extern uintptr_t CFNXBridgeClassToType(Class cls, CFTypeID typeID);
