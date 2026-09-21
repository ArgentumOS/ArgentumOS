/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSPointerArray.m — W13a. See the header for what it is and why serialisation is object-only.
 *
 * EVERY INSERT AND EVERY REMOVAL GOES THROUGH THE FUNCTIONS OBJECT, in one place each: `fnAcquire:` is asked
 * for the pointer the array will hold (which may be a copy) and `fnRelinquish:` is handed what was held when
 * a slot goes away. Nothing else in this file knows what a pointer means, which is what keeps the three
 * pointer collections from each re-deciding it.
 *
 * MANUAL OWNERSHIP (MRC: the whole library is). The storage is `malloc`/`free` rather than an NSArray because
 * an NSArray would retain the elements — the one thing this class must not do on its own.
 */

#import <Foundation/NSPointerArray.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSFastEnumeration.h>
#import <Foundation/NSCoder.h>
#import <Foundation/NSException.h>
#import <Foundation/NSString.h>

#include <stdlib.h>
#include <string.h>

#define FN_POINTER_ARRAY_INITIAL_CAPACITY	4

@implementation NSPointerArray

- (instancetype)initWithOptions:(NSPointerFunctionsOptions)options
{
	return [self initWithPointerFunctions:
		[[[NSPointerFunctions alloc] initWithOptions:options] autorelease]];
}

- (instancetype)initWithPointerFunctions:(NSPointerFunctions *)functions
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_functions = [functions retain];
	_capacity = FN_POINTER_ARRAY_INITIAL_CAPACITY;
	_count = 0;
	_items = (void **)calloc(_capacity, sizeof(void *));
	if (_items == NULL) {
		[self release];
		[NSException raise:NSMallocException format:@"NSPointerArray: no room for its slots"];
	}
	return self;
}

+ (instancetype)strongObjectsPointerArray
{
	return [[[self alloc] initWithOptions:NSPointerFunctionsObjectPersonality] autorelease];
}

+ (instancetype)weakObjectsPointerArray
{
	/* IDENTITY AND NO OWNERSHIP, which is exactly what the two options say. */
	return [[[self alloc] initWithOptions:
		 (NSPointerFunctionsObjectPointerPersonality | NSPointerFunctionsWeakMemory)]
		autorelease];
}

- (void)dealloc
{
	NSUInteger i;

	for (i = 0; i < _count; i++) {
		if (_items[i] != NULL) {
			[_functions fnRelinquish:_items[i]];
		}
	}
	free(_items);
	[_functions release];
	[super dealloc];
}

- (NSPointerFunctions *)pointerFunctions
{
	return _functions;
}

/* THE ONE PLACE CAPACITY GROWS. */
- (void)fnEnsureCapacity:(NSUInteger)needed
{
	NSUInteger capacity = _capacity;

	if (needed <= capacity) {
		return;
	}
	while (capacity < needed) {
		capacity *= 2;
	}
	_items = (void **)realloc(_items, capacity * sizeof(void *));
	if (_items == NULL) {
		[NSException raise:NSMallocException format:@"NSPointerArray: no room to grow"];
	}
	memset(_items + _capacity, 0, (capacity - _capacity) * sizeof(void *));
	_capacity = capacity;
}

- (NSUInteger)count
{
	return _count;
}

- (void)setCount:(NSUInteger)count
{
	NSUInteger i;

	if (count == _count) {
		return;
	}
	if (count < _count) {
		/* SHRINKING RELINQUISHES WHAT IT DROPS, or the memory policy would leak the tail. */
		for (i = count; i < _count; i++) {
			if (_items[i] != NULL) {
				[_functions fnRelinquish:_items[i]];
				_items[i] = NULL;
			}
		}
		_count = count;
		return;
	}
	[self fnEnsureCapacity:count];
	/* GROWING FILLS WITH NULLS: a slot that holds nothing is still a slot, which is the header's point. */
	for (i = _count; i < count; i++) {
		_items[i] = NULL;
	}
	_count = count;
}

- (nullable void *)pointerAtIndex:(NSUInteger)index
{
	if (index >= _count) {
		[NSException raise:NSRangeException
			    format:@"NSPointerArray: index %lu of %lu", (unsigned long)index,
				   (unsigned long)_count];
	}
	return _items[index];
}

- (void)addPointer:(nullable void *)pointer
{
	[self insertPointer:pointer atIndex:_count];
}

- (void)insertPointer:(nullable void *)pointer atIndex:(NSUInteger)index
{
	NSUInteger i;

	if (index > _count) {
		[NSException raise:NSRangeException
			    format:@"NSPointerArray: index %lu of %lu", (unsigned long)index,
				   (unsigned long)_count];
	}
	[self fnEnsureCapacity:_count + 1];
	for (i = _count; i > index; i--) {
		_items[i] = _items[i - 1];
	}
	/* A NULL IS STORED AS A NULL WITHOUT ASKING THE FUNCTIONS: there is nothing to acquire. */
	_items[index] = pointer != NULL ? [_functions fnAcquire:pointer] : NULL;
	_count++;
}

- (void)removePointerAtIndex:(NSUInteger)index
{
	NSUInteger i;

	if (index >= _count) {
		[NSException raise:NSRangeException
			    format:@"NSPointerArray: index %lu of %lu", (unsigned long)index,
				   (unsigned long)_count];
	}
	if (_items[index] != NULL) {
		[_functions fnRelinquish:_items[index]];
	}
	for (i = index; i + 1 < _count; i++) {
		_items[i] = _items[i + 1];
	}
	_count--;
	_items[_count] = NULL;
}

- (void)replacePointerAtIndex:(NSUInteger)index withPointer:(nullable void *)item
{
	if (index >= _count) {
		[NSException raise:NSRangeException
			    format:@"NSPointerArray: index %lu of %lu", (unsigned long)index,
				   (unsigned long)_count];
	}
	if (_items[index] != NULL) {
		[_functions fnRelinquish:_items[index]];
	}
	_items[index] = item != NULL ? [_functions fnAcquire:item] : NULL;
}

- (void)compact
{
	NSUInteger read = 0;
	NSUInteger write = 0;

	for (read = 0; read < _count; read++) {
		if (_items[read] != NULL) {
			_items[write++] = _items[read];
		}
	}
	/* THE COMPACTED TAIL IS CLEARED RATHER THAN RELINQUISHED: the entries that vanish here are the NULLs,
	 * and a NULL owns nothing. */
	for (read = write; read < _count; read++) {
		_items[read] = NULL;
	}
	_count = write;
}

- (NSArray *)allObjects
{
	NSMutableArray *objects = [NSMutableArray arrayWithCapacity:_count];
	NSUInteger i;

	for (i = 0; i < _count; i++) {
		if (_items[i] != NULL) {
			[objects addObject:(id)_items[i]];
		}
	}
	return objects;
}

- (NSUInteger)countByEnumeratingWithState:(NSFastEnumerationState *)state
				  objects:(id *)buffer
				    count:(NSUInteger)length
{
	(void)buffer;
	(void)length;
	if (state->state >= _count) {
		return 0;
	}
	/* THE SLOTS ARE HANDED BACK DIRECTLY, which is what makes this cheap and also what makes it only
	 * meaningful for the object personalities: a fast enumeration reads them as OBJECTS. */
	state->itemsPtr = (id *)_items;
	state->mutationsPtr = (unsigned long *)&_count;
	state->state = _count;
	return _count;
}

/* `-copy`, not `-copyWithZone:`: see NSPointerFunctions.m and NSObject.h. */
- (id)copy
{
	NSPointerArray *copy = [[[self class] alloc] initWithPointerFunctions:_functions];
	NSUInteger i;

	[copy setCount:_count];
	for (i = 0; i < _count; i++) {
		if (_items[i] != NULL) {
			/* THE COPY IS TAKEN THROUGH THE FUNCTIONS, so a copying personality copies here too. */
			[copy replacePointerAtIndex:i withPointer:_items[i]];
		}
	}
	return copy;
}

/* ---- serialisation: the POINTEES, in order ---- */

+ (BOOL)supportsSecureCoding
{
	return YES;
}

- (void)encodeWithCoder:(NSCoder *)coder
{
	/* REFUSED BY NAME FOR THE POINTERS THAT ARE NOT OBJECTS, so an archive never holds a number or a byte run
	 * pretending to be one (see the header). The check is on the PERSONALITY — read from the functions object
	 * rather than guessed from the contents, so a mixed array is refused consistently instead of sometimes. */
	NSUInteger personality = (([_functions fnOptions]) >> 8) & 0xFF;

	if (personality != 0 && personality != 2) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSPointerArray: only the object personalities can be archived; this array "
				   "uses personality %lu, whose pointers are not objects",
				   (unsigned long)personality];
	}
	[coder encodeObject:[self allObjects] forKey:@"NS.objects"];
}

- (instancetype)initWithCoder:(NSCoder *)coder
{
	self = [self initWithOptions:NSPointerFunctionsObjectPointerPersonality];
	if (self != nil) {
		NSArray *objects = [coder decodeObjectForKey:@"NS.objects"];
		NSUInteger i;

		for (i = 0; i < [objects count]; i++) {
			[self addPointer:(void *)[objects objectAtIndex:i]];
		}
	}
	return self;
}

@end
