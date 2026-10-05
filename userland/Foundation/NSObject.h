/*
 * NSObject.h — the base object of Foundation, designed into CoreFoundation from its first line.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE CONTRACT, IN ONE PARAGRAPH, AND WHY IT IS THIS AND NOT SOMETHING ELSE.
 *
 * A Foundation object here is an OBJECTIVE-C OBJECT that CoreFoundation accepts as one of its own. Both
 * halves of that are load-bearing, and this session measured why:
 *
 *   * CF's TYPE-SPECIFIC doors (CFStringGetLength, CFArrayGetCount, CFStringGetCString, CFEqual…) decide
 *     whether to use their C implementation or to send a message by asking CF_IS_OBJC — an ISA COMPARISON
 *     against the class CF has registered for the type. For an object of this library that comparison is
 *     TRUE, so CF runs THIS LIBRARY'S code rather than its own C code on our storage. That is the bridge,
 *     and it works in the old tree from the moment the classes exist.
 *
 *   * CF's TYPE-AGNOSTIC doors (CFRetain, CFRelease) have no type in hand — they read a CF header
 *     (CFRuntimeBase) that an Objective-C object does not have. So by default they do NOT reach us: an
 *     object of ours placed in a CFArray built with kCFTypeArrayCallBacks was DROPPED (measured, not
 *     reasoned — the CFArray let it die). A CF container silently losing its contents is not a tolerable
 *     way to be "CF-integrated", so the ownership half has to be solved rather than documented.
 *
 * AND WHERE THE ONE COUNT LIVES IS THE PART THE FIRST IMPLEMENTATION GOT WRONG, IN A WAY WORTH RECORDING
 * BECAUSE IT IS THE RULE THIS LIBRARY'S OWN HEADER STATES. The first version of -retain read
 * `return objc_retain(self);` on the theory that the runtime's counter and CF's would then be one. IT
 * RECURSED TO A STACK OVERFLOW: in libobjc2, objc_retain on a class that implements -retain (an MRC class,
 * which this is) is spelled `[obj retain]` — so -retain called objc_retain which called -retain. The probe
 * showed it as a repeating triad of addresses on the stack, which is what unbounded recursion looks like
 * from the outside.
 *
 * SO THE COUNT IS THIS CLASS'S OWN FIELD, and the runtime's entry points are the DOORWAY INTO IT rather
 * than the thing it delegates to: the arm calls objc_retain, objc_retain messages -retain, and -retain
 * increments _refcount. ONE HOP — and therefore ONE COUNT, which is what the design wanted, without the
 * loop. The same holds for -release and objc_release.
 *
 * (The count is a plain field, not an atomic, because nothing in this library is threaded yet. When it is,
 * this is the line that changes, and the note is here so it is found rather than rediscovered.)
 *
 * AND THE SOLUTION IS THE ONE UPSTREAM'S OWN COMMENT DESCRIBES: CFRetain/CFRelease are MEANT to ask
 * CF_IS_OBJC first (see internalInclude/CFInternal.h's note about "a race between CFRetain / CFRelease
 * (which call CF_IS_OBJC) and _CFRuntimeBridgeClasses"). In upstream's Swift deployment mode that arm is
 * spelled swift_retain; on this tree it is spelled objc_retain/objc_release and lives in the CF package
 * we own. WITH THAT ARM, ONE COUNTER SERVES BOTH WORLDS, and this class's -retain/-release are the same
 * operation as CFRetain/CFRelease.
 *
 * SO THE DESIGN RULE FOR EVERY CLASS IN THIS LIBRARY IS:
 *
 *   1. The object's LIFETIME is ONE COUNT, held in this class, reachable from both worlds: CF's arm calls
 *      objc_retain/objc_release, the runtime spells those as -retain/-release for a class like this one,
 *      and the methods increment this class's own field. So an object cannot be alive in one world and dead
 *      in the other. That failure mode is why this file exists at all.
 *   2. The object's IDENTITY is its Objective-C class, which is what makes CF_IS_OBJC true and sends CF's
 *      type-specific doors into this library's methods.
 *   3. Whatever the object's BEHAVIOUR looks like from the C side, CF's own type for it must be the type
 *      whose doors it will be handed to. CFStringGetLength on a CFArray is a programmer error in CF; the
 *      same is true here, and the classes say which CF type they stand in for.
 *
 * WHAT IS DELIBERATELY ABSENT: no zones, no -copyWithZone:, no autorelease pools yet. The first was
 * removed by recorded decision (the old tree struck the zone API, and a class made to answer it again
 * would be re-adding what was removed); autorelease is deferred until there is a pool to belong to.
 * Absent-and-stated is the rule here, not absent-by-accident.
 */

#ifndef FNX_FOUNDATION_NSOBJECT_H
#define FNX_FOUNDATION_NSOBJECT_H

#include <objc/runtime.h>
#include <objc/objc-arc.h>
#include <CoreFoundation/CFBase.h>
#include <CoreFoundation/CFString.h>
#include <stddef.h>

/*
 * APPLE'S NULLABILITY SPELLING AND THE VALUE TYPES NOW LIVE IN THEIR OWN HEADER — which is where Apple keeps
 * them, and where this file's own note said NSUInteger would move "the moment a second class needs it, and
 * only then". That moment arrived: NSArray's range-taking doors make it four classes plus a struct. So
 * Foundation/NSObjCRuntime.h is the home of NSUInteger/NSInteger (ALIASED to CFIndex/CFOptionFlags, which are
 * the same types), of NSRange/NSRangePointer/NSMakeRange, of NSNotFound, and of these two macros. Imported
 * rather than re-declared so there is one definition of each and nothing to drift.
 */
#import <Foundation/NSObjCRuntime.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * THE RULE FOR EVERY EQUIVALENT PAIR, WHICH THIS FILE ALREADY OBEYS AND THE NEXT CLASSES INHERIT: an
 * Objective-C method whose behaviour is a CoreFoundation function's should CALL that function — ON ITS
 * STORAGE, NEVER ON ITSELF. The distinction is not pedantry, it is the difference between one behaviour
 * and a hang: CF's doors are implemented by DISPATCHING BACK to the class (CF_IS_OBJC is an ISA
 * comparison, and this library's classes pass it by construction). So -hash calling CFHash(self) is an
 * infinite loop through the same door, while -hash over a CFDictionaryRef the object owns is CF's real C
 * path. DELEGATION MUST SAY WHAT IT DELEGATES ON.
 *
 *   * WHERE THE STORAGE IS A CF OBJECT, every storage-level door delegates and there is ONE implementation
 *     of the behaviour. That is the point of the design: with two implementations the two worlds can
 *     disagree, and with one they cannot. This file is the example — -retain and -release ARE
 *     objc_retain/objc_release, which is what CF's ownership arm calls.
 *   * WHERE THE CONTRACTS DIFFER, delegation is not available and the difference is STATED, not silently
 *     reimplemented. The old library's -getCharacters:range: zero-fills past the end while CF's requires
 *     an in-bounds range: a real divergence, and the standing policy is that a deviation is tolerated only
 *     as far as it is necessary and must be documented.
 *   * WHERE THE PAIR IS NOT SYMMETRIC, delegate the OPERATION and own the SHAPE. CF's uppercase is the
 *     in-place CFStringUppercase with no immutable-returning twin, so -uppercaseString runs CF's operation
 *     over a copy the method made.
 *   * AND THE COMPATIBILITY SURFACE CANNOT DELEGATE AT ALL: the doors CF itself implements by dispatching
 *     to us (-getCString:maxLength:encoding:, -length, -characterAtIndex: …) ARE the CF side of the pair.
 */

/* THE TWO NAMES THIS HEADER CANNOT AVOID, and both are forward rather than owned: NSUInteger is the
 * library's own integer spelling (it moves to its own header the moment a second class needs it, and only
 * then), and NSString is named because -description returns one — here that is CoreFoundation's string
 * seen through the bridge, which is what makes the two description doors the same door. */
/* MOVED, AND THE PARAGRAPH ABOVE SAID WHEN: NSUInteger was "this library's own integer spelling" until a
 * second class needed it — and Foundation/NSObjCRuntime.h is now that home, where it is an ALIAS of CF's
 * CFOptionFlags (the same type by construction). NSObject.h imports it, so every consumer is unchanged. */
@class NSString;

/*
 * THE PROTOCOL, AND IN THIS DESIGN IT IS NOT DECORATION BUT THE ONLY BINDER THERE CAN BE.
 *
 * On Apple the protocol exists for the EXCEPTION: ordinary classes inherit the class, and NSProxy -- the other
 * root class -- CONFORMS instead, which is what makes a proxy interchangeable with an NSObject-derived object.
 * Here every class that stands for a CoreFoundation type is a root class BY NECESSITY, because its memory is
 * CF's and it cannot inherit this class's field (an NSObject base would expect one at offset 8, where CF keeps
 * the object's info word). So the protocol is what binds them, and until it exists what binds them is only
 * the CF runtime and the runtime's count.
 *
 * IT DECLARES ONLY WHAT EVERY ROOT CLASS HERE CAN ACTUALLY IMPLEMENT -- eight doors, no stubs. -retainCount is
 * deliberately NOT among them: CF's count is not readable through a portable door, and declaring one this
 * library cannot answer would be a promise it does not keep.
 */
@protocol NSObject
- (BOOL)isEqual:(id)other;
- (NSUInteger)hash;
- (Class)class;
+ (Class)class;
- (BOOL)isKindOfClass:(Class)cls;
- (NSString *)description;
- (id)retain;
- (void)release;
@end

/* WHETHER ONE CLASS IS A KIND OF ANOTHER, as a plain C function so BOTH root classes answer -isKindOfClass:
 * with the same walk rather than two copies of it. */
static inline BOOL FNXClassIsKindOfClass(Class cls, Class wanted)
{
	while (cls != Nil) {
		if (cls == wanted) {
			return YES;
		}
		cls = class_getSuperclass(cls);
	}
	return NO;
}

/* THE ROOT CLASS, and it is a ROOT: it inherits from nothing, which is why libobjc2 needs to be told so
 * (objc_root_class) rather than being handed a superclass that does not exist in this library. */
__attribute__((objc_root_class))
@interface NSObject <NSObject>
{
	Class isa;			/* THE FIRST WORD — and it IS CF's _cfisa: CF_IS_OBJC compares it to its own table */
	unsigned long long _cfinfoa;	/* THE SECOND WORD, WHICH IS CF'S. CF's own C doors read a TYPE ID out of this
					 * word, so an object of this library is acceptable to them only when the word is
					 * CF's. The first word already agreed -- which is why class registration worked --
					 * and this is the word that did not: CFGetTypeID answered 0 for an NSArray because
					 * it was reading a retain count. Eight bytes, the width of CFRuntimeBase's field. */
	unsigned int _refcount;		/* THE ONE COUNT — kept AFTER the CF header, so CF's doors can read the
					 * object's front without walking into the count. */
}

/* LIFETIME — the runtime's counter, shared with CF (see the contract above). */
+ (id)alloc;
- (id)init;
- (id)retain;
- (void)release;
- (void)dealloc;

/* IDENTITY AND EQUALITY — and -hash must agree with CFHash's expectations, because a CF container will
 * call whichever one it is holding: an object whose -hash disagreed with CFHash would be findable by one
 * world and not the other. */
/* -retainCount IS APPLE'S DOOR AND IT IS HERE BECAUSE TWO REASONS AGREED: the surface rule (match Apple
 * class-for-class) says NSObject has it, and the first probe needed a public way to ask whether CF's arm
 * moved the count, which reading the ivar from outside the class cannot do. It is not for reasoning about
 * lifetime — Apple says so too — it is for asking a question about the machinery. */
- (NSUInteger)retainCount;

- (Class)class;
+ (Class)class;
- (BOOL)isKindOfClass:(Class)cls;
- (BOOL)isEqual:(id)other;
- (NSUInteger)hash;

/* DESCRIPTION — CF's CFCopyDescription and -description must not disagree either, for the same reason. */
- (NSString *)description;
+ (NSString *)description;
- (CFStringRef)copyDescription;		/* CF's spelling of the same door, for CFCopyDescription's benefit */

@end

#ifdef __cplusplus
}
#endif

#endif /* FNX_FOUNDATION_NSOBJECT_H */
