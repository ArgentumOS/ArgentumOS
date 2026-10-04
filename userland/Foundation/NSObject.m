/*
 * NSObject.m — the base object of Foundation: an Objective-C object CoreFoundation accepts as its own.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * The contract is in NSObject.h; this file is its implementation, and there are only three things in it
 * worth reading closely. Each of them is here because NOT doing it is a failure this project has already
 * measured at least once.
 *
 * 1. -retain AND -release ARE objc_retain AND objc_release. Not a private counter, not a CF refcount:
 *    THE runtime's, so that CFRetain(obj) and [obj retain] are literally the same operation on the same
 *    word. The alternative — our own counter, with CF's arm taught to call it — would leave two accounts
 *    of the same object's life, and an object that is alive in one world and dead in the other is the one
 *    failure a "CF-integrated from day one" library exists to make impossible.
 *
 * 2. -dealloc FREES THROUGH THE RUNTIME (object_dispose), because the runtime owns the allocation. A class
 *    that freed with free() would skip the runtime's own bookkeeping — weak references and the
 *    side-table — and would be correct only until something used one of them.
 *
 * 3. -hash AND -isEqual: ARE THE PAIR CF MUST AGREE WITH. A CF container holding one of our objects hashes
 *    and compares it through CF's doors; a Foundation side that hashed it differently would make the same
 *    object findable by one world and not the other, which is a worse bug than a missing feature because
 *    nothing crashes — a lookup just fails.
 */

#import <Foundation/NSObject.h>
#include <stdlib.h>

@implementation NSObject

/* THE RUNTIME ALLOCATES, NOT US: class_createInstance zeroes the storage and sets the isa, which is
 * exactly the first word CF_IS_OBJC is going to compare. */
+ (id)alloc
{
	return class_createInstance(self, 0);
}

- (id)init
{
	return self;
}

/* THE ONE COUNTER (see the header). Both of these return/propagate the receiver, because that is what the
 * callers of the old library's door expect and what makes `obj = [[[X alloc] init] retain]` read normally. */
- (id)retain
{
	return objc_retain(self);
}

- (void)release
{
	objc_release(self);
}

/* SUBCLASSES OVERRIDE THIS TO RELEASE WHAT THEY OWN, AND MUST CALL IT OR THE OBJECT LEAKS rather than
 * dying: object_dispose is what actually returns the memory. */
- (void)dealloc
{
	object_dispose(self);
}

- (Class)class
{
	return object_getClass(self);
}

+ (Class)class
{
	return self;
}

- (BOOL)isKindOfClass:(Class)cls
{
	Class c = object_getClass(self);

	while (c != Nil) {
		if (c == cls) {
			return YES;
		}
		c = class_getSuperclass(c);
	}
	return NO;
}

/* IDENTITY BY DEFAULT, WHICH IS ALSO WHAT CF DOES with kCFTypeDictionary's pointer-equality keys: an
 * object that has not said how to be equal is equal to itself and nothing else. */
- (BOOL)isEqual:(id)other
{
	return (other == self) ? YES : NO;
}

- (NSUInteger)hash
{
	return (NSUInteger)(uintptr_t)self;
}

/* THE DESCRIPTION DOORS, AND THEY ARE ONE DOOR. -description is the Objective-C spelling and
 * -copyDescription is the C one CF calls; both build the same CoreFoundation string, so a description that
 * looks right on one side cannot look wrong on the other. The class name comes from the runtime. */
- (NSString *)description
{
	return (NSString *)CFStringCreateWithFormat(kCFAllocatorDefault, NULL, CFSTR("<%s: %p>"),
		class_getName(object_getClass(self)), (void *)self);
}

+ (NSString *)description
{
	return (NSString *)CFStringCreateWithFormat(kCFAllocatorDefault, NULL, CFSTR("<%s>"),
		class_getName(self));
}

- (CFStringRef)copyDescription
{
	/* CF's own spelling of the same door, and it RETURNS RETAINED because CF's naming says so. */
	return (CFStringRef)[self description];
}

@end
