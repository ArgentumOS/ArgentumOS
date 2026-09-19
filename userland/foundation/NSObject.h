/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSObject — the root class of the Foundation (docs/design/foundation-plan.md).
 *
 * WHY THE NAME HAS A PREFIX. The runtime we ship declares a class called
 * `Object` in <objc/Object.h> AND registers it at startup (builtin_classes.c);
 * both facts were measured, and each breaks a first-party root class: importing
 * that header here is `error: duplicate interface definition for class 'Object'`,
 * and a class *named* Object makes the loader print "Loading two versions of
 * Object. The class that will be used is undefined" and die. The runtime also
 * owns Protocol/ProtocolGCC/ProtocolGSv1/__IncompleteProtocol, and the blocks
 * runtime owns the _NSConcrete*Block names. The `NS` prefix (the user's
 * amendment, 2026-09-17) is what keeps the whole family out of the way; that
 * header stays off-limits, and tools/foundation-gate.py enforces it.
 */

#ifndef FOUNDATION_NSOBJECT_H
#define FOUNDATION_NSOBJECT_H

#include <objc/runtime.h>
#include <stdint.h>
#include <foundation/NSObjCRuntime.h>

@class NSString;
@class NSMethodSignature;
@class NSInvocation;

/*
 * NULLABILITY (F6). The sweep's rules, in one place: everything below is NONNULL
 * by default, and the handful of declarations that can legitimately answer nil say
 * so — a class with no superclass, a selector the runtime cannot find, a
 * -performSelector: whose method answers nil, and a -forwardingTargetForSelector:
 * meaning "no fast forwarding". (The list used to include -zone, which answered
 * NULL; it is removed with the rest of the zone API.)
 */
NS_ASSUME_NONNULL_BEGIN

/*
 * The copying protocols — AND THIS IS A DELIBERATE DEVIATION FROM COCOA
 * (2026-09-18). Cocoa's members are `-copyWithZone:` / `-mutableCopyWithZone:`,
 * whose argument is an `NSZone`; Apple states on the `NSZone` page that "Zones
 * are ignored on iOS and 64-bit runtime in macOS. You should not use zones in
 * current development", and this system is 64-bit only, so every method that
 * TAKES an NSZone has been REMOVED — the protocols' members are `-copy` and
 * `-mutableCopy`, and the override point is the entry point. The consequence,
 * stated where the change is: a Cocoa class that implements `-copyWithZone:`
 * will not conform to these protocols, and `[obj copyWithZone:nil]` will not
 * compile. docs/design/foundation-plan.md carries the decision and its cost. */
@protocol NSCopying
- (id)copy;
@end

@protocol NSMutableCopying
- (id)mutableCopy;
@end

/*
 * THE ROOT-CLASS ATTRIBUTE, and it is LOAD-BEARING — not decoration to silence a
 * warning. Without it, clang does not treat this as a root class of the modern
 * ABI, and in a translation unit that includes this header a `@"..."` literal is
 * NOT emitted as an object: it is folded into a bogus immediate constant
 * (measured: `movabs $0xc790000000000014`, with no relocation and no
 * `__objc_constant_string` entry to fill it), so every constant string is
 * garbage and a message send to it faults inside the runtime.
 *
 * Our runtime's own tests mark their root class exactly this way (Test/Test.h),
 * which is how the difference was found.
 */
/*
 * THE NSObject PROTOCOL (W2h) - the group of methods that make an object a first-class object. It
 * is the dependency NSProgressReporting needs and the reason it is here first (§12: a missing
 * dependency is ADDED, not refused).
 *
 * THE MEMBER LIST IS APPLE'S PUBLISHED ONE, checked against the documentation rather than recalled
 * (this library's ledger is symbol-level, so it could not supply it): isEqual:, hash, superclass,
 * class, self, isProxy, isKindOfClass:, isMemberOfClass:, conformsToProtocol:, respondsToSelector:,
 * description, debugDescription, retain, release, autorelease, retainCount, and the three
 * performSelector: forms. `-methodForSelector:` and `-doesNotRecognizeSelector:` are NOT in it -
 * they belong to the NSObject CLASS - which is a distinction the documentation settled.
 *
 * ONE MEMBER IS DELIBERATELY ABSENT, and it is the only one: `-zone`. Apple's protocol declares it;
 * this library REMOVED the entire zone API as 32-bit-only (§11.5), and a protocol cannot promise
 * what the system does not have. That is ground (i) of §11.6.1's necessity test, and it is
 * registered there rather than left for a reader to notice. The parameter nullability follows THIS library's
 * class declaration rather than Apple's published spelling, so the header does not argue with itself; where the
 * class and the protocol could differ, the class is the one the compiler has to accept.
 */
@protocol NSObject

@property (readonly) NSUInteger hash;
@property (readonly, copy) NSString *description;
@property (readonly, copy) NSString *debugDescription;

- (BOOL)isEqual:(id)object;
- (nullable Class)superclass;	/* a ROOT class's superclass IS nil */
- (Class)class;
- (instancetype)self;
- (BOOL)isProxy;
- (BOOL)isKindOfClass:(Class)aClass;
- (BOOL)isMemberOfClass:(Class)aClass;
- (BOOL)conformsToProtocol:(Protocol *)aProtocol;
- (BOOL)respondsToSelector:(SEL)aSelector;
- (instancetype)retain;
- (oneway void)release;
- (instancetype)autorelease;
- (NSUInteger)retainCount;
- (nullable id)performSelector:(SEL)aSelector;
- (nullable id)performSelector:(SEL)aSelector withObject:(id)object;
- (nullable id)performSelector:(SEL)aSelector withObject:(id)object1 withObject:(id)object2;

@end

#if __has_attribute(objc_root_class)
__attribute__((objc_root_class))
#endif

@interface NSObject <NSObject>
{
	/*
	 * THE STORAGE RULE. class_createInstance() returns nil when
	 * instance_size < sizeof(Class) (runtime.c:360), so a root class with no
	 * instance variables silently produces no instances at all. The isa is
	 * the runtime's to manage; this declaration is what makes it exist.
	 */
	Class isa;
}

/*
 * Creation. These names are Cocoa's ON PURPOSE: ARC decides ownership from the
 * METHOD NAME (+alloc/+new/-init/-copy/-mutableCopy return +1, everything else
 * +0), so they keep those names whatever the class is called.
 */
+ (id)alloc;
+ (id)new;
- (id)init;

/*
 * The three lifetimes. They delegate to the runtime rather than keeping a count
 * of their own: the count lives in a word *before* the allocation
 * (gc_none.c:allocate_class), so an ARC caller (objc_retain/objc_release) and an
 * MRR caller ([obj retain]) share ONE count.
 *
 * This class is marked ARC-compatible to the runtime — see
 * -_ARCCompliantRetainRelease in nsobject.m, without which those delegations
 * recurse.
 */
- (id)retain;
- (oneway void)release;
- (id)autorelease;
- (NSUInteger)retainCount;
- (void)dealloc;		/* the root class frees the allocation */

/* Identity, class membership and introspection. */
+ (Class)class;
+ (nullable Class)superclass;
+ (BOOL)isSubclassOfClass:(Class)aClass;
+ (BOOL)conformsToProtocol:(Protocol *)aProtocol;
- (Class)class;
- (nullable Class)superclass;
- (BOOL)isKindOfClass:(Class)aClass;
- (BOOL)isMemberOfClass:(Class)aClass;
- (BOOL)respondsToSelector:(SEL)aSelector;
- (BOOL)conformsToProtocol:(Protocol *)aProtocol;
- (id)self;

/*
 * THE COPYING FAMILY, declared here because every object can be sent it (Cocoa
 * declares them on NSObject too) — and THE ENTRY POINT IS THE OVERRIDE POINT
 * (2026-09-18): a class that can be copied implements `-copy` (and
 * `-mutableCopy`), and the default on NSObject is A LOUD FAILURE. In Cocoa the
 * default lives on `-copyWithZone:` instead and `-copy` merely calls it; that
 * method took an `NSZone` and is removed with the rest of the zone API.
 *
 * The loudness is the whole point of the audit's fix A: before it existed,
 * sending `-copy` to a class that did not implement it did not raise, it
 * returned NIL (the runtime's forwarding path), so a dictionary handed an
 * NSNumber key filed a phantom entry under a nil key and the value became
 * unreachable — silently, with a count that said otherwise.
 */
- (id)copy;		/* default: doesNotRecognizeSelector: */
- (id)mutableCopy;	/* default: doesNotRecognizeSelector: */
- (void)doesNotRecognizeSelector:(SEL)aSelector;

/* Messaging, which is how Cocoa code calls a selector it only knows by name. */
- (nullable id)performSelector:(SEL)aSelector;
- (nullable id)performSelector:(SEL)aSelector withObject:(nullable id)object;
- (nullable id)performSelector:(SEL)aSelector withObject:(nullable id)object1 withObject:(nullable id)object2;

/* Messaging and introspection, the rest of Cocoa's root-class surface. */
/* THE SIGNATURE OF A SELECTOR, and the FORWARDING that uses it (stage F). The
 * lookup needs no invocation; -forwardInvocation: IS the invocation — built by the
 * runtime's __objc_msg_forward2 hook (ninvocation.m) and delivered here. */
- (nullable NSMethodSignature *)methodSignatureForSelector:(SEL)aSelector;
+ (nullable NSMethodSignature *)methodSignatureForSelector:(SEL)aSelector;
- (nullable id)forwardingTargetForSelector:(SEL)aSelector;	/* default: nil */
- (void)forwardInvocation:(NSInvocation *)anInvocation;	/* default: doesNotRecognizeSelector: */

- (nullable IMP)methodForSelector:(SEL)aSelector;
+ (BOOL)respondsToSelector:(SEL)aSelector;
+ (BOOL)instancesRespondToSelector:(SEL)aSelector;
+ (void)load;			/* the runtime calls these; declared so overrides match */
+ (void)initialize;
/* NO +allocWithZone: (2026-09-18): it takes an `NSZone`, and every zone-taking
 * method is removed. THE SINGLETON DOOR IS +alloc — override THAT to answer a
 * shared instance (see nsobject.m for the measurement that makes the direction
 * matter). */

- (BOOL)isProxy;
/* NO -zone EITHER (2026-09-18, the user closing the last gap: "any method which
 * returns an NSZone is removed"). With it gone, NOTHING in this library names
 * `NSZone`, so the TYPE is gone from NSObjCRuntime.h as well — there is no
 * allocator to ask about and no zone to hand anyone. */
- (NSString *)debugDescription;

/*
 * Equality and hashing. The defaults are identity and the pointer, which is
 * exactly what a Dictionary key has to override.
 */
- (BOOL)isEqual:(id)other;
- (NSUInteger)hash;

/* Every object describes itself: this names the class (docs, F1). */
- (NSString *)description;

/*
 * THE C ACCESSORS (W2a, docs/design/foundation-plan.md §12.3): the boundary between the
 * runtime's names and this library's strings. Apple declares them in NSObjCRuntime.h, and
 * they CANNOT live there in this tree: that header is the lowest level and has no
 * `NSString` in scope, and opening a nullability region in it would subject its C block
 * typedef to the completeness check the gate exempts it from. So they are declared here,
 * where the class they answer with is already forward-declared.
 *
 * The contracts worth stating: `NSClassFromString` answers Nil for a name nothing
 * registers and `NSStringFromClass(nil)` answers nil; `NSSelectorFromString` REGISTERS a
 * name that was never compiled (the runtime's behaviour, not a lookup failure).
 */
NSString * _Nullable NSStringFromClass(Class _Nullable aClass);
Class _Nullable NSClassFromString(NSString *aClassName);
NSString *NSStringFromSelector(SEL aSelector);
/* APPLE'S HEADER IS THE SPECIFICATION, AND APPLE'S DOCUMENTATION IS THE BEHAVIOUR, and here the
 * two disagree: the declaration is nonnull, while the documentation says "if aSelectorName is nil,
 * or cannot be converted to UTF-8 ... it returns (SEL)0". D8 of §11.6.1 asked which one wins, and
 * the answer is the API surface — so this is declared nonnull exactly as Apple's is, the writer
 * keeps returning the null selector exactly as Apple documents, and the single building site
 * silences -Wnonnull (which Apple's own build must do as well). A nullable here was NOT necessary,
 * which is the only test the policy allows. */
SEL NSSelectorFromString(NSString *aSelectorName);
NSString *NSStringFromRange(NSRange range);

/* THE PAGE FUNCTIONS (W2e): a region of the process's address space, named for pages. They live
 * here because Apple declares them with the object-allocation functions — a region to allocate
 * an object in is the same layer. The pointers are NONNULL by contract: a caller checks the
 * answer before using it, which is what the probe does. */
void * _Nullable NSAllocateMemoryPages(NSUInteger numberOfBytes);
void NSCopyMemoryPages(const void *source, void *dest, NSUInteger numberOfBytes);
void NSDeallocateMemoryPages(void *ptr, NSUInteger numberOfBytes);

NS_ASSUME_NONNULL_END

@end

#endif /* FOUNDATION_NSOBJECT_H */
