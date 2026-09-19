/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * nsproxy.m — the implementation (W2h). A ROOT CLASS: it inherits nothing, so what a first-class
 * object owes is either here or in the runtime, and everything else is forwarded.
 */
#import <foundation/NSProxy.h>
#import <foundation/NSException.h>
#import <foundation/NSMethodSignature.h>
#import <foundation/NSInvocation.h>
#import <objc/runtime.h>

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
	return objc_retainCount(self);
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


/*
 * THE DEFAULT IS TO RAISE, in both directions, and that is Apple's contract rather than a shortcut:
 * a proxy that has not been told what it stands for cannot invent a method signature, and answering
 * nil or a fabricated one would turn a programming error into a wrong result.
 */
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
