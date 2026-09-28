/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSProxy.m — the implementation (W2h). A ROOT CLASS: it inherits nothing, so what a first-class
 * object owes is either here or in the runtime, and everything else is forwarded.
 */
#import <Foundation/NSProxy.h>
#import <Foundation/NSString.h>	/* -description builds one */
#import <Foundation/NSException.h>
#import <Foundation/NSMethodSignature.h>
#import <Foundation/NSInvocation.h>
#import <objc/runtime.h>
#include <objc/objc-arc.h>	/* objc_retain/objc_release/objc_autorelease */

/* THE THREE PERFORMERS' ONE IMPLEMENTATION: a file-static function rather than a method, because the argument
 * vector is a multi-level pointer and the nullability region has nothing sensible to say about one ("nullable
 * cannot be applied to multi-level pointer"). */
static id fn_perform_selector(id proxy, SEL aSelector, id *arguments, NSUInteger count);

@implementation NSProxy

/* A ROOT CLASS ALLOCATES ITSELF: there is no superclass to ask, which is the whole reason this is
 * written here rather than inherited. */
+ (instancetype)alloc
{
	return class_createInstance(self, 0);
}

+ (Class)class
{
	return self;
}

+ (BOOL)respondsToSelector:(SEL)aSelector
{
	return class_respondsToSelector(object_getClass(self), aSelector);
}

/*
 * THE MEMBERS A ROOT CLASS OWES FROM THE NSObject PROTOCOL, and they are HERE rather than forwarded
 * because forwarding them is a MEMORY BUG: a proxy that forwards -retain and -release to the object
 * it stands for keeps releasing the TARGET, and the runtime's own ARC entry points would take that
 * path on every message. Apple's NSProxy implements the same set for the same reason. -isKindOfClass:,
 * -isMemberOfClass: and the rest of the introspection surface are deliberately NOT here: forwarding
 * those to the real object is the documented behaviour, which is exactly why -isProxy exists.
 */
/*
 * THE MARKER, AND IT WAS MISSING HERE - measured, not reasoned: a proxy that is RELEASED recursed until
 * the stack blew (a SIGSEGV in -[NSProxy release] with a backtrace of nothing but that method). The
 * runtime decides whether a class may use its fast, word-based reference count by looking for exactly
 * this selector (dtable.c, checkARCAccessorsSlow); without it objc_release() does not touch the count
 * word but MESSAGES -release, and -release here calls objc_release(), which is the loop. The root class
 * has carried this marker since it was measured there (NSObject.m says so at length); NSProxy did not,
 * and nothing released a proxy in this tree until -prepareWithInvocationTarget:'s proxy was, so the two
 * cases had never met. The body is empty for the reason NSObject's is: the marker means "my hand-written
 * lifetime methods are ARC-correct", and this one delegates to the runtime's own count.
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

- (NSUInteger)retainCount
{
	/* MIRRORED FROM NSObject, including the runtime's `_np` accessor: the suffix is the
	 * runtime's note that it is non-portable across runtimes, which is fine where the
	 * Foundation and the runtime are one thing. */
	return (NSUInteger)object_getRetainCount_np(self);
}

- (id)self
{
	return self;
}

- (BOOL)isProxy
{
	return YES;
}

- (BOOL)isEqual:(id)other
{
	return self == other;
}

- (NSUInteger)hash
{
	return (NSUInteger)(uintptr_t)self >> 3;
}

- (Class)class
{
	return object_getClass(self);
}

- (Class)superclass
{
	return class_getSuperclass(object_getClass(self));
}

- (BOOL)respondsToSelector:(SEL)aSelector
{
	return class_respondsToSelector(object_getClass(self), aSelector);
}

- (BOOL)conformsToProtocol:(Protocol *)aProtocol
{
	return class_conformsToProtocol(object_getClass(self), aProtocol);
}


/* THE FIVE NSObject PROTOCOL DOORS THIS PROXY WAS MISSING, IN THE STYLE THE CLASS ALREADY SET: -class,
 * -superclass, -respondsToSelector: and -conformsToProtocol: above all answer from the DYNAMIC class
 * (object_getClass(self)), so the two "is it one of these" questions read the same place - a proxy here is not
 * "the object it stands for" but the object it IS, and the runtime is what knows. The three performers go through
 * -methodSignatureForSelector: and -forwardInvocation:, which is the flow Apple documents and the one a concrete
 * subclass fills in; the default raises, exactly as it does below, rather than inventing an answer. */
- (BOOL)isKindOfClass:(Class)aClass
{
	Class walking = object_getClass(self);

	while (walking != Nil) {
		if (walking == aClass) {
			return YES;
		}
		walking = class_getSuperclass(walking);
	}
	return NO;
}

- (BOOL)isMemberOfClass:(Class)aClass
{
	return object_getClass(self) == aClass;
}

- (id)performSelector:(SEL)aSelector
{
	return fn_perform_selector(self, aSelector, NULL, 0);
}

- (id)performSelector:(SEL)aSelector withObject:(id)object
{
	id arguments[1];

	arguments[0] = object;
	return fn_perform_selector(self, aSelector, arguments, 1);
}

- (id)performSelector:(SEL)aSelector withObject:(id)object1 withObject:(id)object2
{
	id arguments[2];

	arguments[0] = object1;
	arguments[1] = object2;
	return fn_perform_selector(self, aSelector, arguments, 2);
}

/*
 * THE DEFAULT IS TO RAISE, in both directions, and that is Apple's contract rather than a shortcut:
 * a proxy that has not been told what it stands for cannot invent a method signature, and answering
 * nil or a fabricated one would turn a programming error into a wrong result.
 */
/* ONE ARITHMETIC FOR THE THREE PERFORMERS (the arguments start at index 2, past target and selector), and the
 * result is read ONLY when the selector returns an object: -performSelector: is documented for object-returning
 * selectors, and reading a pointer out of a scalar return would be the kind of wrong answer worth refusing. */
static id fn_perform_selector(id proxy, SEL aSelector, id *arguments, NSUInteger count)
{
	NSMethodSignature *signature = [proxy methodSignatureForSelector:aSelector];
	NSInvocation *invocation = [NSInvocation invocationWithMethodSignature:signature];
	const char *returnType = [signature methodReturnType];
	id result = nil;
	NSUInteger i;

	[invocation setTarget:proxy];
	[invocation setSelector:aSelector];
	for (i = 0; i < count; i++) {
		[invocation setArgument:&arguments[i] atIndex:(NSInteger)(i + 2)];
	}
	[invocation invoke];
	if (returnType != NULL && returnType[0] == '@') {
		[invocation getReturnValue:&result];
	}
	return result;
}

- (void)forwardInvocation:(NSInvocation *)invocation
{
	[NSException raise:NSInvalidArgumentException
		    format:@"-[%@ %@] must be overridden: a proxy forwards what it does not implement",
			   [self class], @"forwardInvocation:"];
}

- (NSMethodSignature *)methodSignatureForSelector:(SEL)selector
{
	[NSException raise:NSInvalidArgumentException
		    format:@"-[%@ %@] must be overridden: a proxy has no signature to offer by default",
			   [self class], NSStringFromSelector(selector)];
	return nil;	/* -raise: does not return */
}

- (void)dealloc
{
	/* THE INSTANCE IS THE ROOT CLASS'S OWN TO FREE: nothing above this will do it. */
	object_dispose((id)self);
}

- (NSString *)description
{
	return [NSString stringWithFormat:@"<%@: %p>", [self class], (void *)self];
}

- (NSString *)debugDescription
{
	return [self description];
}

@end
