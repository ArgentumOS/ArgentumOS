/*
 * NSArray.m — the ordered collection that IS a CFArray.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * SELF IS THE ARRAY. There is no second storage to fall out of step with CF's: every door below casts the
 * RECEIVER — not an ivar — to a CFArrayRef and calls CF's own function on it. That is what makes the free
 * cast in both directions true rather than merely compiled, and it is the same shape this library's NSString
 * takes toward CFString.
 *
 * WHICH MEANS -retain AND -release ARE CF'S, NOT NSObject's. An object whose first three words are CF's header
 * cannot keep a private count in them: NSObject's -release decrements `_refcount`, which on a CF-shaped object
 * is CF's own storage. So the pair is spelled with CFRetain/CFRelease, exactly as NSString spells it, and the
 * one count is CF's, reached from both sides.
 *
 * THE SENTINEL IS CF'S, because this tree defines no NSNotFound. CFArrayGetFirstIndexOfValue answers
 * kCFNotFound when the value is absent, so the door returns exactly what CF returned rather than translating
 * one sentinel into another.
 */

#import <Foundation/NSArray.h>
#import <objc/runtime.h>

extern unsigned long CFNXBridgeClassToType(Class cls, CFTypeID typeID);

/*
 * THE COMPARISON IS OURS; EVERYTHING ELSE IN THE PAIR IS CF'S. This was decided by measurement.
 *
 * kCFTypeArrayCallBacks' equal is CFEqual (CFArray.c:23), and CFEqual's ObjC dispatch only fires for a class
 * CF's runtime has been told about (CFRuntime.c:1078) -- for any other class it falls through to
 * __CFGenericAssertIsCF (CFRuntime.c:1082) and TRAPS. So -containsObject: died with SIGILL on the first
 * object of an unregistered class, while -count and -objectAtIndex: were fine: they never compare.
 *
 * The shim below is the missing half of CF's own delegation rather than a replacement for it: CFEqual MEANT to
 * send -isEqual:, and that is exactly what this sends, for any class. Apple's NSArray compares with -isEqual:
 * too, so the two worlds agree here rather than one being worked around.
 *
 * RETAIN AND RELEASE STAY CF'S OWN. They are the half the object probe measured as working -- a CF array holds
 * an object of this library and is what ends it -- so they are copied verbatim from kCFTypeArrayCallBacks
 * rather than rewritten. Nothing about ownership changes here; only where the comparison comes from.
 */
static Boolean fnx_array_equal(const void *value1, const void *value2)
{
	if (value1 == value2) {
		return true;
	}
	if (value1 == NULL || value2 == NULL) {
		return false;
	}
	return [(id)value1 isEqual:(id)value2] ? true : false;
}

/* Built once, on first use, from CF's own pair. A plain flag is enough: the two threads that raced here would
 * write the same values, and a HALF-built pair is the one failure that would matter, so the ready flag is set
 * only after every field is in place. */
static CFArrayCallBacks fnx_array_callbacks;
static Boolean fnx_array_callbacks_ready = false;

static const CFArrayCallBacks *fnx_array_callbacks_get(void)
{
	if (!fnx_array_callbacks_ready) {
		fnx_array_callbacks = kCFTypeArrayCallBacks;
		fnx_array_callbacks.equal = fnx_array_equal;
		fnx_array_callbacks_ready = true;
	}
	return &fnx_array_callbacks;
}

@implementation NSArray

/* -init CANNOT BE NSObject's, for the same reason -retain cannot: NSObject's leaves the receiver as itself,
 * and `self` here is the +alloc'd shell — an object with this class as its isa and no CF storage. Routing it
 * through the real initialiser means `[[NSArray alloc] init]` answers an empty CFArray rather than an object
 * that would read its own retain count as a count. */
- (instancetype)init
{
	return [self initWithObjects:NULL count:0];
}

- (instancetype)initWithObjects:(const id _Nonnull * _Nullable)objects count:(NSUInteger)count
{
	/* CF's own retain/release callbacks hold each item as CFArrayCreate places it, so no slot is retained
	 * here; only the comparison is ours (see fnx_array_equal). A count of zero is legal with a NULL vector. */
	CFArrayRef array = CFArrayCreate(kCFAllocatorDefault, (const void **)objects, (CFIndex)count, fnx_array_callbacks_get());

	/* THE SHELL IS DISPOSED, NOT RELEASED, AND THAT DISTINCTION IS LOAD-BEARING. `self` is the +alloc'd
	 * receiver: an ordinary Objective-C allocation with this class as its isa and NO CF storage, so sending
	 * it -release would route into the CF-shaped pair below and hand CFRelease an object that is not one.
	 * object_dispose is what NSObject's own -dealloc calls to return the memory, and it is the honest way to
	 * say "this allocation is not the object"; nothing of ours needs unwinding because this class has no
	 * fields (an NSArray IS a CFArray). */
	object_dispose(self);

	/* A NULL CFArrayCreate is a failed allocation, and Apple's contract for a failed -init is nil. */
	return (NSArray *)array;
}

/* THE ONE COUNT, AND IT IS CF'S — see the file header for why this pair cannot be NSObject's. */
- (id)retain
{
	return (id)CFRetain((CFTypeRef)self);
}

- (void)release
{
	CFRelease((CFTypeRef)self);
}

- (NSUInteger)count
{
	return (NSUInteger)CFArrayGetCount((CFArrayRef)self);
}

- (id _Nonnull)objectAtIndex:(NSUInteger)index
{
	return (id)CFArrayGetValueAtIndex((CFArrayRef)self, (CFIndex)index);
}

- (id _Nonnull)objectAtIndexedSubscript:(NSUInteger)index
{
	return [self objectAtIndex:index];
}

- (id _Nullable)firstObject
{
	return ([self count] == 0) ? nil : [self objectAtIndex:0];
}

- (id _Nullable)lastObject
{
	NSUInteger n = [self count];

	return (n == 0) ? nil : [self objectAtIndex:n - 1];
}

- (BOOL)containsObject:(id _Nonnull)anObject
{
	/* An array cannot hold nil under Apple's contract, so a nil query is false rather than a search. */
	if (anObject == nil) {
		return NO;
	}
	return CFArrayContainsValue((CFArrayRef)self, CFRangeMake(0, CFArrayGetCount((CFArrayRef)self)), (const void *)anObject);
}

- (NSUInteger)indexOfObject:(id _Nonnull)anObject
{
	if (anObject == nil) {
		return (NSUInteger)kCFNotFound;
	}
	return (NSUInteger)CFArrayGetFirstIndexOfValue((CFArrayRef)self, CFRangeMake(0, CFArrayGetCount((CFArrayRef)self)), (const void *)anObject);
}

/*
 * THE BRIDGE, AND WHY THIS CLASS NEEDS IT. Registering the class with CF's type tells CF's runtime that an
 * object of THIS class is a CFArray: it is what makes CFArrayGetCount and its relatives accept one, and it is
 * the same line NSString carries for CFString. The hook that calls this runs when the class exists, which is
 * why this is a separate entry point rather than a constructor.
 *
 * AND THE REGISTRATION IS WHAT MAKES THE DOORS ABOVE NON-RECURSIVE. CF_IS_OBJC(typeID, obj) is an ISA
 * COMPARISON — true when the object's isa is NOT the class registered for that type — so it is FALSE for an
 * object of this class, and CFArrayGetCount called from -count takes CF's own C path instead of dispatching
 * back into -count. An object whose isa differed from the registered class would dispatch, and this file's
 * doors would call themselves.
 */
void _CFNXBridgeArrayClasses(void)
{
	/* Warm the callback pair too, so a caller that somehow reaches -initWithObjects:count: first still gets
	 * a complete pair rather than the all-zero one a static starts as. */
	(void)fnx_array_callbacks_get();
	extern void _FNXBridgeClass(Class cls, unsigned long typeID);

	/* THE REGISTRATION ITSELF. A pattern-based edit removed this line while stripping a diagnostic that sat
	 * beside it -- the second time in one session that a regex took a load-bearing line with it. If this call
	 * disappears, NSArray silently stops being a CFArray to CF and nothing else looks wrong. */
	_FNXBridgeClass([NSArray class], (unsigned long)CFArrayGetTypeID());
}

@end
