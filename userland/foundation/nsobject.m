/*
 * nsobject.m — the root class.
 *
 * THE ONE MRR FILE. ARC forbids implementing -retain/-release (measured:
 * `error: ARC forbids implementation of 'retain'`), and a root class has to
 * implement them, so this file is compiled with -fno-objc-arc while the rest of
 * the library is ARC. That is the seam the plan describes, not an accident.
 */

#import <foundation/NSObject.h>
#include <stdio.h>
#include <stdlib.h>
#import <foundation/NSString.h>	/* -description has to return one */
#include <objc/objc-arc.h>

/*
 * The runtime's ownership entry points. The reference count is kept in a word
 * before the allocation, so these — and not a count of our own — are the single
 * source of truth for both ARC and MRR callers.
 */
extern id objc_retain(id obj);
extern void objc_release(id obj);
extern id objc_autorelease(id obj);
extern id object_dispose(id obj);

@implementation NSObject

+ (id)alloc
{
	return class_createInstance(self, 0);
}

+ (id)new
{
	return [[self alloc] init];
}

- (id)init
{
	return self;
}

/*
 * THE MARKER, AND WHY IT IS NOT OPTIONAL.
 *
 * The runtime decides whether a class may use its fast (word-based) reference
 * count by looking for exactly this method: a class that implements
 * -retain/-release/-autorelease WITHOUT it has objc_class_flag_fast_arc *cleared*
 * (dtable.c: checkARCAccessorsSlow, which scans cls->methods for the selector
 * `_ARCCompliantRetainRelease` and returns early when it is missing). When the
 * flag is absent the runtime's objc_retain() does not use the count word — it
 * MESSAGES -retain (arc.mm: `return ManualRetainReleaseMessage(obj, retain, …)`)
 * — and since -retain here calls objc_retain(), that is infinite recursion:
 * measured as a SIGSEGV in -[NSObject retain] with a backtrace of nothing but
 * that method, stack overflowing.
 *
 * The body is empty by design: the marker means "my hand-written lifetime
 * methods are ARC-correct", and ours are, because they delegate to the runtime's
 * own count. The runtime's own test suite marks its classes exactly this way
 * (Test/FastARC.m).
 */
- (void)_ARCCompliantRetainRelease
{
}

- (id)retain
{
	return objc_retain(self);
}

- (oneway void)release
{
	objc_release(self);
}

- (id)autorelease
{
	return objc_autorelease(self);
}

- (unsigned long)retainCount
{
	/*
	 * The runtime's count accessor (objc/objc-arc.h). The `_np` suffix is the
	 * runtime's own note that it is non-portable *across runtimes* — fine here,
	 * where the Foundation and the runtime are one thing. Cocoa discourages
	 * reading this in anger; it is part of the contract, and the tests use it.
	 */
	return (unsigned long)object_getRetainCount_np(self);
}

- (void)dealloc
{
	/*
	 * The ROOT class is where the allocation goes away: at reference count 0
	 * the runtime sends -dealloc and does NOT free afterwards (arc.mm:
	 * `[obj dealloc];` in the release path), so this is the free. Subclasses
	 * release their own state and reach here through the super chain — which
	 * an ARC-compiled subclass does automatically, since clang emits
	 * objc_msg_lookup_super(dealloc) even though ARC forbids *writing*
	 * `[super dealloc]`.
	 */
	object_dispose(self);
}

+ (Class)class
{
	return self;
}

+ (Class)superclass
{
	return class_getSuperclass(self);
}

+ (BOOL)isSubclassOfClass:(Class)aClass
{
	Class c;

	for (c = self; c != Nil; c = class_getSuperclass(c)) {
		if (c == aClass) {
			return YES;
		}
	}
	return NO;
}

- (Class)class
{
	return object_getClass(self);
}

- (Class)superclass
{
	return class_getSuperclass(object_getClass(self));
}

- (BOOL)isKindOfClass:(Class)aClass
{
	Class c;

	for (c = object_getClass(self); c != Nil; c = class_getSuperclass(c)) {
		if (c == aClass) {
			return YES;
		}
	}
	return NO;
}

- (BOOL)isMemberOfClass:(Class)aClass
{
	return object_getClass(self) == aClass;
}

- (BOOL)respondsToSelector:(SEL)aSelector
{
	return (aSelector != NULL) &&
	       class_respondsToSelector(object_getClass(self), aSelector);
}

- (BOOL)isEqual:(id)other
{
	return self == other;
}

- (unsigned long)hash
{
	return (unsigned long)(uintptr_t)self;
}

- (NSString *)description
{
	/*
	 * F1: the class name, as a real NSString. Subclasses override it freely; a
	 * class name is what a default description can honestly say about an object
	 * it knows nothing else about.
	 */
	return [NSString stringWithUTF8String:class_getName(object_getClass(self))];
}


/*
 * THE COPYING FAMILY (the public-API audit, A). -copy and -mutableCopy delegate
 * to the ZONE methods, and the zone methods' default here is a LOUD failure.
 *
 * The loudness is the fix, not the boilerplate: before this existed, sending
 * -copy to a class that did not implement it was answered NIL by the runtime's
 * forwarding path, so a dictionary handed such an object as a key filed a
 * phantom entry under a nil key — the count grew and the value was unreachable,
 * silently. Cocoa raises NSInvalidArgumentException here; v1 has no exception
 * objects until F4, so this aborts with the class and selector named.
 */
- (id)copy
{
	return [self copyWithZone:NULL];
}

- (id)mutableCopy
{
	return [self mutableCopyWithZone:NULL];
}

- (id)copyWithZone:(NSZone *)zone
{
	(void)zone;
	[self doesNotRecognizeSelector:_cmd];
	return nil;		/* unreachable: doesNotRecognizeSelector does not return */
}

- (id)mutableCopyWithZone:(NSZone *)zone
{
	(void)zone;
	[self doesNotRecognizeSelector:_cmd];
	return nil;
}

- (void)doesNotRecognizeSelector:(SEL)aSelector
{
	fprintf(stderr, "Foundation: -[%s %s] is not implemented\n",
		class_getName(object_getClass(self)), sel_getName(aSelector));
	abort();
}

/*
 * Protocol conformance, WITH the superclass walk: a class that inherits a
 * protocol from a superclass conforms to it (which is how NSMutableArray
 * answers YES for NSCopying without redeclaring it, as Cocoa's does).
 */
+ (BOOL)conformsToProtocol:(Protocol *)aProtocol
{
	/*
	 * `self` IS the class here. NOT object_getClass(self), which is the
	 * METACLASS: starting the walk there made every conformance query answer NO
	 * — measured, with class_conformsToProtocol on the class saying YES while
	 * this said NO.
	 */
	Class cls;

	for (cls = self; cls != Nil; cls = class_getSuperclass(cls)) {
		if (class_conformsToProtocol(cls, aProtocol)) {
			return YES;
		}
	}
	return NO;
}

- (BOOL)conformsToProtocol:(Protocol *)aProtocol
{
	return [[self class] conformsToProtocol:aProtocol];
}

- (id)self
{
	return self;
}

/* Messaging a selector the caller only knows by name. */
- (id)performSelector:(SEL)aSelector
{
	return ((id (*)(id, SEL))objc_msgSend)(self, aSelector);
}

- (id)performSelector:(SEL)aSelector withObject:(id)object
{
	return ((id (*)(id, SEL, id))objc_msgSend)(self, aSelector, object);
}


/*
 * THE REST OF THE PUBLIC ROOT-CLASS API (the hard rule: a class passes only when
 * its public API is complete).
 *
 *   -zone / +allocWithZone:   NO ZONES: one allocator, so the argument is
 *                             accepted, ignored and documented. -zone answers
 *                             NULL rather than a fake zone, so nothing can be
 *                             handed to an allocator that does not exist.
 *   -isProxy                  the root class is not a proxy.
 *   -debugDescription         the same text as -description.
 *   -methodForSelector:       the runtime's own answer.
 *   +load / +initialize       declared and empty so a subclass's overrides match
 *                             the signatures the runtime calls them with.
 *
 * NOT here: -forwardInvocation: / -methodSignatureForSelector:, which need
 * NSInvocation and NSMethodSignature — classes this Foundation does not ship yet,
 * so they are an ORDERING DEPENDENCY, not a gap. A message nothing implements
 * therefore reaches -doesNotRecognizeSelector: and aborts loudly.
 */
+ (void)load
{
}

+ (void)initialize
{
}

+ (BOOL)respondsToSelector:(SEL)aSelector
{
	/* The receiver IS a class here, so the metaclass carries its class methods. */
	return class_respondsToSelector(object_getClass(self), aSelector);
}

+ (BOOL)instancesRespondToSelector:(SEL)aSelector
{
	return class_respondsToSelector(self, aSelector);
}

+ (id)allocWithZone:(NSZone *)zone
{
	(void)zone;
	return [self alloc];
}

- (BOOL)isProxy
{
	return NO;
}

- (NSZone *)zone
{
	return NULL;
}

- (IMP)methodForSelector:(SEL)aSelector
{
	return class_getMethodImplementation(object_getClass(self), aSelector);
}

- (NSString *)debugDescription
{
	return [self description];
}

@end
