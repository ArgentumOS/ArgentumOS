/*
 * NSArray.m — the CFArray-backed ordered collection.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * ONE ARRAY, AND CF OWNS EVERY SLOT. Every door here is a call into CFArray: no item vector of ours, no count
 * of ours, no retain of ours. The ASYMMETRIC pair is -dealloc, which is why this class never walks its
 * contents on the way out — CFRelease ends the array and, through the callbacks, every item it held.
 *
 * THE SENTINEL IS CF'S, because this tree defines no NSNotFound. CFArrayGetFirstIndexOfValue answers
 * kCFNotFound when the value is absent, so the door returns exactly what CF returned rather than translating
 * one sentinel into another.
 */

#import <Foundation/NSArray.h>

extern unsigned long CFNXBridgeClassToType(Class cls, CFTypeID typeID);

@implementation NSArray

- (instancetype)initWithObjects:(const id _Nonnull * _Nullable)objects count:(NSUInteger)count
{
	self = [super init];
	if (self == nil) {
		return nil;
	}

	/* CF's own callbacks retain each item as CFArrayCreate places it, so no slot is retained here. A count of
	 * zero is legal with a NULL vector, which is what CFArrayCreate expects. */
	_storage = CFArrayCreate(kCFAllocatorDefault, (const void **)objects, (CFIndex)count, &kCFTypeArrayCallBacks);
	if (_storage == NULL) {
		[self release];
		return nil;
	}
	return self;
}

- (void)dealloc
{
	/* CFRelease ends the array AND, through the callbacks, every item it held. */
	if (_storage != NULL) {
		CFRelease(_storage);
		_storage = NULL;
	}
	[super dealloc];
}

- (NSUInteger)count
{
	return (NSUInteger)CFArrayGetCount(_storage);
}

- (id _Nonnull)objectAtIndex:(NSUInteger)index
{
	return (id)CFArrayGetValueAtIndex(_storage, (CFIndex)index);
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
	return CFArrayContainsValue(_storage, CFRangeMake(0, CFArrayGetCount(_storage)), (const void *)anObject);
}

- (NSUInteger)indexOfObject:(id _Nonnull)anObject
{
	if (anObject == nil) {
		return (NSUInteger)kCFNotFound;
	}
	return (NSUInteger)CFArrayGetFirstIndexOfValue(_storage, CFRangeMake(0, CFArrayGetCount(_storage)), (const void *)anObject);
}

/*
 * THE BRIDGE, AND WHY THIS CLASS NEEDS IT. Registering the class with CF's type tells CF's runtime that an
 * object of THIS class is a CFArray: it is what makes CFArrayGetCount and its relatives accept one, and it is
 * the same line NSString carries for CFString. The hook that calls this runs when the class exists, which is
 * why this is a separate entry point rather than a constructor.
 */
void _CFNXBridgeArrayClasses(void)
{
	CFNXBridgeClassToType([NSArray class], CFArrayGetTypeID());
}

@end
