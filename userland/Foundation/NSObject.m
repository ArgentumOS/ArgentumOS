/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSObject.m — the root class.
 *
 * THE ONE MRR FILE. ARC forbids implementing -retain/-release (measured:
 * `error: ARC forbids implementation of 'retain'`), and a root class has to
 * implement them, so this file is compiled with -fno-objc-arc while the rest of
 * the library is ARC. That is the seam the plan describes, not an accident.
 */

#import <Foundation/NSObject.h>
#include <stdio.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#import <Foundation/NSString.h>	/* -description has to return one */
#import <Foundation/NSByteOrder.h>	/* the byte-order family (W2c) */
#import <Foundation/NSException.h>	/* objc_enumerationMutation raises (D6) */
#include <sys/mman.h>		/* the page functions (W2e) */
#include <string.h>		/* memcpy, for NSCopyMemoryPages */
#import <Foundation/NSMethodSignature.h>	/* stage F: the selector's types */
#import <Foundation/NSInvocation.h>	/* stage F: what forwarding is handed */
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
	/* THE PRIMITIVE, AND THE SINGLETON DOOR (2026-09-18). Cocoa makes +allocWithZone: the override
	 * point and +alloc its caller; +allocWithZone: takes an `NSZone` and zones are 32-bit API this
	 * system has none of, so THE DOOR MOVED TO +alloc — override THIS to answer a shared instance.
	 * Measured both ways, which is why the direction matters: when +alloc merely called an override
	 * that no longer exists, a singleton answered a freshly allocated object instead of itself
	 * ([[NSNull alloc] init] was not +null). */
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
 * THE COPYING FAMILY (the public-API audit, A) — AND THE OVERRIDE POINT MOVED
 * HERE (2026-09-18). `-copy` and `-mutableCopy` used to delegate to the ZONE
 * methods, in Cocoa's shape. Those took an `NSZone`, and zones are 32-bit API
 * that this 64-bit system has none of, so the zone-taking methods are gone:
 * **the ENTRY POINT is now the OVERRIDE POINT** — a class that can be copied
 * implements `-copy` (and `-mutableCopy`), and the default here is a LOUD
 * failure. The cost is a deliberate deviation from Cocoa's NSCopying, recorded
 * in docs/design/foundation-plan.md.
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
	[self doesNotRecognizeSelector:_cmd];
	return nil;		/* unreachable: doesNotRecognizeSelector does not return */
}

- (id)mutableCopy
{
	[self doesNotRecognizeSelector:_cmd];
	return nil;
}

- (void)doesNotRecognizeSelector:(SEL)aSelector
{
	/* APPLE RAISES HERE; THIS ABORTED, AND THE DIFFERENCE IS NOT ACADEMIC: a missing selector took the
	 * WHOLE PROCESS DOWN, so no @try/@catch could survive one - which is how a -copy that reached the root
	 * class killed a guest probe instead of reporting itself. Apple's page for this method names the
	 * exception, and this library has an exception class, so the contract is the one to keep. The message
	 * keeps the shape the old fprintf had, so every trace that already quotes it still matches. */
	/* ON write(2) AND NOT fprintf: this library's printf family produces NOTHING (a recorded property), so a
	 * message that only fprintf writes is a message nobody sees - which is exactly how an unimplemented
	 * selector came to abort a probe silently. The raw descriptor is the channel that reaches the console. */
	{
		char line[192];
		int n = snprintf(line, sizeof line, "Foundation: -[%s %s] is not implemented\n",
			class_getName(object_getClass(self)), sel_getName(aSelector));

		if (n > 0) {
			(void)write(2, line, (size_t)n);
		}
	}
	[NSException raise:NSInvalidArgumentException
		    format:@"-[%s %s] is not implemented",
			   class_getName(object_getClass(self)), sel_getName(aSelector)];
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

- (id)performSelector:(SEL)aSelector withObject:(id)object1 withObject:(id)object2
{
	return ((id (*)(id, SEL, id, id))objc_msgSend)(self, aSelector, object1, object2);
}

/*
 * THE SIGNATURE OF ANY SELECTOR (stage F, first half: NSMethodSignature).
 *
 * One helper serves both variants, because `object_getClass(self)` is the right
 * starting point for each: for an instance it is that instance's class, and for a
 * class object it is the metaclass — so one lookup finds an instance method in
 * the first case and a class method in the second, which is exactly what the two
 * variants mean.
 *
 * TWO SOURCES, in order: the METHOD (the runtime's encoding, authoritative when
 * some class implements the selector) and then the SELECTOR's own registered type
 * — which is how a selector nothing has implemented can still answer, the case
 * forwarding will depend on. nil when neither knows.
 */
static NSMethodSignature *fn_signature_for(id receiver, SEL aSelector)
{
	Method method = class_getInstanceMethod(object_getClass(receiver), aSelector);
	const char *types = (method != NULL) ? method_getTypeEncoding(method) : NULL;

	if (types == NULL) {
		types = sel_getType_np(aSelector);
	}
	if (types == NULL) {
		return nil;
	}
	return [NSMethodSignature signatureWithObjCTypes:types];
}

- (NSMethodSignature *)methodSignatureForSelector:(SEL)aSelector
{
	return fn_signature_for(self, aSelector);
}

+ (NSMethodSignature *)methodSignatureForSelector:(SEL)aSelector
{
	/* `self` IS the class here, and the helper starts from the metaclass. */
	return fn_signature_for(self, aSelector);
}

/*
 * THE FORWARDING PAIR (stage F, second half). Both are the DEFAULTS Cocoa
 * documents, and having both is what makes the mechanism terminate:
 *
 *   -forwardingTargetForSelector:  no fast forwarding, so a message nothing
 *                                  implements does not silently retarget;
 *   -forwardInvocation:            the LOUD failure. An unimplemented message
 *                                  arrives here as a real NSInvocation, built by
 *                                  NSInvocation.m's __objc_msg_forward2 hook, and
 *                                  the honest answer is -doesNotRecognizeSelector:.
 *
 * A subclass forwards by overriding either one: the fast path needs no invocation
 * at all, and the slow one has the arguments.
 */
- (id)forwardingTargetForSelector:(SEL)aSelector
{
	(void)aSelector;
	return nil;
}

- (void)forwardInvocation:(NSInvocation *)anInvocation
{
	[self doesNotRecognizeSelector:[anInvocation selector]];
}


/*
 * THE REST OF THE PUBLIC ROOT-CLASS API (the hard rule: a class passes only when
 * its public API is complete).
 *
 *   (no zone API)             NO ZONES AT ALL (2026-09-18). Everything that TOOK an
 *                             `NSZone` is removed, `-zone` (which RETURNED one) is
 *                             removed, and `NSZone` itself is no longer declared:
 *                             zones are 32-bit API and this system is 64-bit only.
 *   -isProxy                  the root class is not a proxy.
 *   -debugDescription         the same text as -description.
 *   -methodForSelector:       the runtime's own answer.
 *   +load / +initialize       declared and empty so a subclass's overrides match
 *                             the signatures the runtime calls them with.
 *
 * NOT here YET: -forwardInvocation: / -forwardingTargetForSelector:, which need
 * NSInvocation — stage F's second half, where the x86-64 argument marshalling
 * lives. They stay an ORDERING DEPENDENCY, not a gap: a message nothing
 * implements still reaches -doesNotRecognizeSelector: and aborts loudly.
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

- (BOOL)isProxy
{
	return NO;
}

- (IMP)methodForSelector:(SEL)aSelector
{
	return class_getMethodImplementation(object_getClass(self), aSelector);
}

- (NSString *)debugDescription
{
	return [self description];
}


/*
 * THE C LEVEL'S ODDS AND ENDS (W2c, W2e and W2g, docs/design/foundation-plan.md §14). They are
 * here because they are the root class's own layer: none of them is an object.
 *
 * TWO GROUPS ARE DELIBERATELY ABSENT OF IMPLEMENTATION, recorded rather than faked:
 *
 *   NSIncrementExtraRefCount / NSDecrementExtraRefCountWasZero / NSExtraRefCount
 *       They read and write an object's EXTRA reference count, and this runtime exposes no such
 *       API (measured: objc/runtime.h declares none). objc_retain/objc_release cannot ANSWER a
 *       count, and a wrong answer is worse than a recorded gap: the dependency is the runtime's.
 *
 *   NSCountFrames / NSFrameAddress
 *       A frame walker, and this tree does not build with a frame-pointer guarantee — walking the
 *       chain would return POINTERS INTO NOTHING rather than fail. Recorded as a dependency on a
 *       stack-walking facility (Apple marks both unavailable anyway).
 */

/* ---------------------------------------------------------------- byte order */

NSSwappedFloat NSConvertHostFloatToSwapped(float x)
{
	NSSwappedFloat out;
	union {
		float f;
		unsigned int u;
	} in;

	in.f = x;
	out.v = (unsigned long)__builtin_bswap32(in.u);
	return out;
}

float NSConvertSwappedFloatToHost(NSSwappedFloat x)
{
	union {
		float f;
		unsigned int u;
	} out;

	out.u = __builtin_bswap32((unsigned int)x.v);
	return out.f;
}

NSSwappedDouble NSConvertHostDoubleToSwapped(double x)
{
	NSSwappedDouble out;
	union {
		double d;
		unsigned long long u;
	} in;

	in.d = x;
	out.v = __builtin_bswap64(in.u);
	return out;
}

double NSConvertSwappedDoubleToHost(NSSwappedDouble x)
{
	union {
		double d;
		unsigned long long u;
	} out;

	out.u = __builtin_bswap64(x.v);
	return out.d;
}

/* THE HOST IS LITTLE-ENDIAN HERE BY CONSTRUCTION: this system is x86-64 and nothing else. */
NSByteOrder NSHostByteOrder(void)
{
	return NS_LittleEndian;
}

/* ===================================================================================================
 * THE BYTE-ORDER CONVERSIONS (§62.51): one helper per width and a direction decided once
 *
 * A byte-order conversion is a REVERSAL of the bytes and nothing else, so the only thing that differs
 * between the forty-two functions above is which direction the caller wrote. THE DEVICE THEY SHARE IS A
 * SWAP OF A FIXED WIDTH; the differences are worked out by asking NSHostByteOrder() rather than by
 * assuming, so that the same source is right on a machine whose order is not this one's.
 * =================================================================================================== */

static unsigned short fn_bo_swap16(unsigned short value)
{
	return (unsigned short)__builtin_bswap16(value);
}

static unsigned int fn_bo_swap32(unsigned int value)
{
	return (unsigned int)__builtin_bswap32(value);
}

static unsigned long long fn_bo_swap64(unsigned long long value)
{
	return (unsigned long long)__builtin_bswap64(value);
}

/* THE PLAIN SWAPS: a reversal, with no host involved. */
unsigned short NSSwapShort(unsigned short value)
{
	return fn_bo_swap16(value);
}

unsigned int NSSwapInt(unsigned int value)
{
	return fn_bo_swap32(value);
}

unsigned long NSSwapLong(unsigned long value)
{
	return fn_bo_swap64(value);
}

unsigned long long NSSwapLongLong(unsigned long long value)
{
	return fn_bo_swap64(value);
}

float NSSwapFloat(float value)
{
	NSSwappedFloat swapped = NSConvertHostFloatToSwapped(value);

	swapped.v = fn_bo_swap32((unsigned int)swapped.v);
	return NSConvertSwappedFloatToHost(swapped);
}

double NSSwapDouble(double value)
{
	NSSwappedDouble swapped = NSConvertHostDoubleToSwapped(value);

	swapped.v = fn_bo_swap64((unsigned long long)swapped.v);
	return NSConvertSwappedDoubleToHost(swapped);
}

/* WHERE THE HOST STANDS. `NSSwapHostXToLittle` changes nothing on a little-endian host and reverses on a
 * big-endian one; the rule is ASKED of the host rather than assumed, so these stay right if the system is
 * ever built for a machine whose order is the other one. */
unsigned short NSSwapHostShortToBig(unsigned short value)
{
	return (NSHostByteOrder() == NS_BigEndian) ? value : NSSwapShort(value);
}

unsigned short NSSwapHostShortToLittle(unsigned short value)
{
	return (NSHostByteOrder() == NS_LittleEndian) ? value : NSSwapShort(value);
}

unsigned int NSSwapHostIntToBig(unsigned int value)
{
	return (NSHostByteOrder() == NS_BigEndian) ? value : NSSwapInt(value);
}

unsigned int NSSwapHostIntToLittle(unsigned int value)
{
	return (NSHostByteOrder() == NS_LittleEndian) ? value : NSSwapInt(value);
}

unsigned long NSSwapHostLongToBig(unsigned long value)
{
	return (NSHostByteOrder() == NS_BigEndian) ? value : NSSwapLong(value);
}

unsigned long NSSwapHostLongToLittle(unsigned long value)
{
	return (NSHostByteOrder() == NS_LittleEndian) ? value : NSSwapLong(value);
}

unsigned long long NSSwapHostLongLongToBig(unsigned long long value)
{
	return (NSHostByteOrder() == NS_BigEndian) ? value : NSSwapLongLong(value);
}

unsigned long long NSSwapHostLongLongToLittle(unsigned long long value)
{
	return (NSHostByteOrder() == NS_LittleEndian) ? value : NSSwapLongLong(value);
}

float NSSwapHostFloatToBig(float value)
{
	if (NSHostByteOrder() == NS_BigEndian) {
		return value;
	}
	return NSSwapFloat(value);
}

float NSSwapHostFloatToLittle(float value)
{
	if (NSHostByteOrder() == NS_LittleEndian) {
		return value;
	}
	return NSSwapFloat(value);
}

double NSSwapHostDoubleToBig(double value)
{
	if (NSHostByteOrder() == NS_BigEndian) {
		return value;
	}
	return NSSwapDouble(value);
}

double NSSwapHostDoubleToLittle(double value)
{
	if (NSHostByteOrder() == NS_LittleEndian) {
		return value;
	}
	return NSSwapDouble(value);
}

/* AND THE DOOR BACK IS THE SAME DOOR: a conversion out of an order and into the host is the mirror of the
 * host conversion, so it CALLS it rather than repeating the rule. Two implementations of one rule is two
 * places for it to be wrong. */
unsigned short NSSwapBigShortToHost(unsigned short value)
{
	return NSSwapHostShortToBig(value);
}

unsigned short NSSwapLittleShortToHost(unsigned short value)
{
	return NSSwapHostShortToLittle(value);
}

unsigned int NSSwapBigIntToHost(unsigned int value)
{
	return NSSwapHostIntToBig(value);
}

unsigned int NSSwapLittleIntToHost(unsigned int value)
{
	return NSSwapHostIntToLittle(value);
}

unsigned long NSSwapBigLongToHost(unsigned long value)
{
	return NSSwapHostLongToBig(value);
}

unsigned long NSSwapLittleLongToHost(unsigned long value)
{
	return NSSwapHostLongToLittle(value);
}

unsigned long long NSSwapBigLongLongToHost(unsigned long long value)
{
	return NSSwapHostLongLongToBig(value);
}

unsigned long long NSSwapLittleLongLongToHost(unsigned long long value)
{
	return NSSwapHostLongLongToLittle(value);
}

float NSSwapBigFloatToHost(float value)
{
	return NSSwapHostFloatToBig(value);
}

float NSSwapLittleFloatToHost(float value)
{
	return NSSwapHostFloatToLittle(value);
}

double NSSwapBigDoubleToHost(double value)
{
	return NSSwapHostDoubleToBig(value);
}

double NSSwapLittleDoubleToHost(double value)
{
	return NSSwapHostDoubleToLittle(value);
}

/* AND BETWEEN TWO ORDERS THAT ARE NOT THE HOST'S THERE IS NO HOST TO CONSULT: big-endian bytes written for
 * little-endian bytes are a reversal on every machine there is, so this is the plain swap whatever the
 * host's order happens to be — which is the one place a host-independent answer exists at all. */
unsigned short NSSwapBigShortToLittle(unsigned short value)
{
	return NSSwapShort(value);
}

unsigned short NSSwapLittleShortToBig(unsigned short value)
{
	return NSSwapShort(value);
}

unsigned int NSSwapBigIntToLittle(unsigned int value)
{
	return NSSwapInt(value);
}

unsigned int NSSwapLittleIntToBig(unsigned int value)
{
	return NSSwapInt(value);
}

unsigned long NSSwapBigLongToLittle(unsigned long value)
{
	return NSSwapLong(value);
}

unsigned long NSSwapLittleLongToBig(unsigned long value)
{
	return NSSwapLong(value);
}

unsigned long long NSSwapBigLongLongToLittle(unsigned long long value)
{
	return NSSwapLongLong(value);
}

unsigned long long NSSwapLittleLongLongToBig(unsigned long long value)
{
	return NSSwapLongLong(value);
}

float NSSwapBigFloatToLittle(float value)
{
	return NSSwapFloat(value);
}

float NSSwapLittleFloatToBig(float value)
{
	return NSSwapFloat(value);
}

double NSSwapBigDoubleToLittle(double value)
{
	return NSSwapDouble(value);
}

double NSSwapLittleDoubleToBig(double value)
{
	return NSSwapDouble(value);
}


/* ------------------------------------------------------ the page functions */

void *NSAllocateMemoryPages(NSUInteger numberOfBytes)
{
	void *p;

	if (numberOfBytes == 0) {
		return NULL;
	}
	p = mmap(NULL, numberOfBytes, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	return (p == MAP_FAILED) ? NULL : p;
}

void NSCopyMemoryPages(const void *source, void *dest, NSUInteger numberOfBytes)
{
	if (source == NULL || dest == NULL || numberOfBytes == 0) {
		return;
	}
	memcpy(dest, source, numberOfBytes);
}

void NSDeallocateMemoryPages(void *ptr, NSUInteger numberOfBytes)
{
	if (ptr == NULL) {
		return;
	}
	munmap(ptr, numberOfBytes);
}

/* ----------------------------------------------------------- debug switches */

/*
 * THE SWITCHES ARE ADJUSTABLE, which is their whole contract: a program sets one and reads it
 * back. Their defaults are all off, and this library does not ACT on any of them yet — the
 * allocation machinery that would consult them is not built.
 */
BOOL NSDebugEnabled = NO;
BOOL NSZombieEnabled = NO;
BOOL NSDeallocateZombies = NO;
BOOL NSKeepAllocationStatistics = NO;

/* THE VERSION NUMBER IS THIS LIBRARY'S OWN. Apple's value names a released Foundation, this
 * library is not that release, and the number is readable — so it is stated once, here. */
double NSFoundationVersionNumber = 0.0;

/*
 * THE FAST-ENUMERATION MUTATION HOOK (D6 of §11.6.1). clang's `for (x in coll)` loop compares
 * `state->mutationsPtr` after every iteration and calls this when the counter moved — and the
 * RUNTIME'S OWN DEFAULT DOES NOT RAISE: libobjc2's `mutation.m` prints to stderr and calls
 * `abort()`, which is a process death where Cocoa raises a CATCHABLE `NSGenericException`
 * ("collection was mutated while being enumerated"). The runtime's comment says why this symbol is
 * weak: "to enable GNUstep or some other framework to replace it trivially". This library is that
 * framework here, so it replaces it — and the collections' counters have been live all along
 * (`_mutations` is bumped by every mutator of NSMutableArray/NSMutableDictionary/NSMutableSet/
 * NSMutableOrderedSet), so detection only needed the handler to do what Cocoa does.
 *
 * MEASURED, and the probe DEMANDS it in two checks: `mutation-handler-direct` calls this function
 * directly inside @try and catches NSGenericException, and `fast-enum-mutation-raises` mutates an
 * array inside a `for (x in …)` loop and catches the same thing. So this definition is the one
 * that runs (the runtime's aborting default is NOT), and the loop path raises rather than dying —
 * which it did NOT do before NSMutableArray got its own copying enumeration (D6, §11.6.1).
 *
 * ONE ANOMALY IS RECORDED RATHER THAN EXPLAINED AWAY: a build in between crashed with STATUS=139
 * with this same code in place, and it has not reproduced in four runs since. The register carries
 * both facts.
 */
void objc_enumerationMutation(id object)
{
	/*
	 * THE REASON IS A LITERAL AND THE HANDLER TAKES NO ARGUMENTS, deliberately. This runs at the
	 * moment an enumeration has just been invalidated, and the one thing it must not do is fail on
	 * its own behalf: a `%@` here would send -description to the collection's CLASS with the
	 * enumeration state half-torn-down, and a crash at this point REPLACES a catchable exception
	 * with a process death — the exact failure the runtime's aborting default has and this
	 * override exists to remove. A format string with no specifiers reads no varargs at all.
	 */
	(void)object;
	[NSException raise:NSGenericException
		    format:@"*** Collection was mutated while being enumerated."];
}
@end
