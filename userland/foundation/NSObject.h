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

/*
 * The copying protocols, in COCOA'S SHAPE (the public-API audit, item B4): the
 * zone method is the protocol's member, and `-copy` / `-mutableCopy` are
 * declared on NSObject below, because every object can be *sent* them. The zone
 * argument is accepted, ignored and documented — there is one allocator here,
 * so `NSZone` is an incomplete type that nothing ever dereferences.
 */
@protocol NSCopying
- (id)copyWithZone:(NSZone *)zone;
@end

@protocol NSMutableCopying
- (id)mutableCopyWithZone:(NSZone *)zone;
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
#if __has_attribute(objc_root_class)
__attribute__((objc_root_class))
#endif
@interface NSObject
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
+ (Class)superclass;
+ (BOOL)isSubclassOfClass:(Class)aClass;
+ (BOOL)conformsToProtocol:(Protocol *)aProtocol;
- (Class)class;
- (Class)superclass;
- (BOOL)isKindOfClass:(Class)aClass;
- (BOOL)isMemberOfClass:(Class)aClass;
- (BOOL)respondsToSelector:(SEL)aSelector;
- (BOOL)conformsToProtocol:(Protocol *)aProtocol;
- (id)self;

/*
 * THE COPYING FAMILY, declared here because every object can be sent it (Cocoa
 * declares them on NSObject too). `-copy` and `-mutableCopy` delegate to the
 * zone methods, and the zone methods' DEFAULT IS A LOUD FAILURE — which is the
 * whole point of the audit's fix A: before this existed, sending `-copy` to a
 * class that did not implement it did not raise, it returned NIL (the runtime's
 * forwarding path), so a dictionary handed an NSNumber key filed a phantom entry
 * under a nil key and the value became unreachable — silently, with a count that
 * said otherwise.
 */
- (id)copy;			/* [self copyWithZone:NULL] */
- (id)mutableCopy;		/* [self mutableCopyWithZone:NULL] */
- (id)copyWithZone:(NSZone *)zone;	/* default: doesNotRecognizeSelector: */
- (id)mutableCopyWithZone:(NSZone *)zone;
- (void)doesNotRecognizeSelector:(SEL)aSelector;

/* Messaging, which is how Cocoa code calls a selector it only knows by name. */
- (id)performSelector:(SEL)aSelector;
- (id)performSelector:(SEL)aSelector withObject:(id)object;

/* Messaging and introspection, the rest of Cocoa's root-class surface. */
- (IMP)methodForSelector:(SEL)aSelector;
+ (BOOL)respondsToSelector:(SEL)aSelector;
+ (BOOL)instancesRespondToSelector:(SEL)aSelector;
+ (void)load;			/* the runtime calls these; declared so overrides match */
+ (void)initialize;
+ (id)allocWithZone:(NSZone *)zone;	/* NO ZONES: the argument is ignored */

- (BOOL)isProxy;
- (NSZone *)zone;		/* NULL — one allocator, and nothing dereferences it */
- (NSString *)debugDescription;

/*
 * Equality and hashing. The defaults are identity and the pointer, which is
 * exactly what a Dictionary key has to override.
 */
- (BOOL)isEqual:(id)other;
- (NSUInteger)hash;

/* Every object describes itself: this names the class (docs, F1). */
- (NSString *)description;

@end

#endif /* FOUNDATION_NSOBJECT_H */
