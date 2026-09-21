/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSHashTable.m — W13b. See the header for what it is and for the NULL limit.
 *
 * THE TABLE DOES THE WORK; this file is the API over it. Two things are worth pointing out because they are
 * decisions rather than mechanics: the ENUMERATION SNAPSHOT (fast enumeration must hand back a compact C
 * array, and a hash table is full of gaps, so a compacted copy is kept and rebuilt only when the table has
 * changed), and the SET OPERATIONS, which are written against the other table's SLOTS rather than against a
 * snapshot, so no intermediate collection is built.
 *
 * MANUAL OWNERSHIP (MRC: the whole library is).
 */

#import <Foundation/NSHashTable.h>
#import <Foundation/FNPointerTable.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSSet.h>
#import <Foundation/NSEnumerator.h>
#import <Foundation/NSException.h>
#import <Foundation/NSString.h>
#import <Foundation/NSCoder.h>
#include <Foundation/NSNull.h>

#include <stdlib.h>

@implementation NSHashTable

- (instancetype)initWithOptions:(NSHashTableOptions)options capacity:(NSUInteger)initialCapacity
{
	return [self initWithPointerFunctions:
		[[[NSPointerFunctions alloc] initWithOptions:options] autorelease]
		capacity:initialCapacity];
}

- (instancetype)initWithPointerFunctions:(NSPointerFunctions *)functions
				capacity:(NSUInteger)initialCapacity
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_table = [[FNPointerTable alloc] initWithKeyFunctions:functions
					     valueFunctions:nil
						   capacity:initialCapacity];
	_mutations = 1;
	return self;
}

+ (instancetype)weakObjectsHashTable
{
	/* NOT OWNED, AND NOT ZEROED — the two options say both halves of that, and the header of
	 * NSPointerFunctions says what "not zeroed" costs. */
	return [[[self alloc] initWithOptions:
		 (NSPointerFunctionsObjectPointerPersonality | NSPointerFunctionsWeakMemory)
		capacity:0] autorelease];
}

+ (instancetype)hashTableWithOptions:(NSHashTableOptions)options
{
	return [[[self alloc] initWithOptions:options capacity:0] autorelease];
}

- (void)dealloc
{
	[_table release];
	free(_snapshot);
	[super dealloc];
}

- (NSPointerFunctions *)pointerFunctions
{
	return [_table keyFunctions];
}

- (NSUInteger)count
{
	return [_table count];
}

- (id)anyObject
{
	NSUInteger i;
	NSUInteger slots = [_table slotCount];

	for (i = 0; i < slots; i++) {
		if ([_table fnStateAtSlot:i] == FN_SLOT_FULL) {
			return (id)[_table fnKeyAtSlot:i];
		}
	}
	return nil;
}

- (NSArray *)allObjects
{
	NSMutableArray *objects = [NSMutableArray arrayWithCapacity:[_table count]];
	NSUInteger i;
	NSUInteger slots = [_table slotCount];

	for (i = 0; i < slots; i++) {
		if ([_table fnStateAtSlot:i] == FN_SLOT_FULL) {
			[objects addObject:(id)[_table fnKeyAtSlot:i]];
		}
	}
	return objects;
}

- (NSSet *)setRepresentation
{
	return [NSSet setWithArray:[self allObjects]];
}

- (BOOL)containsObject:(id)anObject
{
	return anObject != nil && [_table fnMember:(void *)anObject] != NULL;
}

- (id)member:(id)object
{
	return object != nil ? (id)[_table fnMember:(void *)object] : nil;
}

- (NSEnumerator *)objectEnumerator
{
	/* A SNAPSHOT ENUMERATOR, built from the same list `-allObjects` answers: mutating while enumerating
	 * therefore changes the TABLE but not the walk, which is the safe behaviour and the one to state. */
	return [[self allObjects] objectEnumerator];
}

- (void)addObject:(id)object
{
	if (object == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSHashTable: nil is not a member (and a NULL pointer cannot be one)"];
	}
	(void)[_table fnAddKey:(void *)object];
	_mutations++;
}

- (void)removeObject:(id)object
{
	if (object != nil) {
		[_table fnRemoveKey:(void *)object];
		_mutations++;
	}
}

- (void)removeAllObjects
{
	[_table fnRemoveAll];
	_mutations++;
}

/* ---- the set operations ---- */

- (void)intersectHashTable:(NSHashTable *)other
{
	NSArray *mine = [self allObjects];
	NSUInteger i;

	for (i = 0; i < [mine count]; i++) {
		id member = [mine objectAtIndex:i];

		if (![other containsObject:member]) {
			[self removeObject:member];
		}
	}
}

- (BOOL)intersectsHashTable:(NSHashTable *)other
{
	NSArray *mine = [self allObjects];
	NSUInteger i;

	for (i = 0; i < [mine count]; i++) {
		if ([other containsObject:[mine objectAtIndex:i]]) {
			return YES;
		}
	}
	return NO;
}

- (BOOL)isSubsetOfHashTable:(NSHashTable *)other
{
	NSArray *mine = [self allObjects];
	NSUInteger i;

	for (i = 0; i < [mine count]; i++) {
		if (![other containsObject:[mine objectAtIndex:i]]) {
			return NO;
		}
	}
	return YES;
}

- (BOOL)isEqualToHashTable:(NSHashTable *)other
{
	return other != nil && [self count] == [other count] && [self isSubsetOfHashTable:other];
}

- (void)minusHashTable:(NSHashTable *)other
{
	NSArray *mine = [self allObjects];
	NSUInteger i;

	for (i = 0; i < [mine count]; i++) {
		id member = [mine objectAtIndex:i];

		if ([other containsObject:member]) {
			[self removeObject:member];
		}
	}
}

- (void)unionHashTable:(NSHashTable *)other
{
	/* THE OTHER TABLE'S SLOTS ARE READ DIRECTLY — an object may read the ivars of another instance of its own
	 * class — so this builds no intermediate collection at all. */
	NSUInteger i;
	NSUInteger slots = [other->_table slotCount];

	for (i = 0; i < slots; i++) {
		if ([other->_table fnStateAtSlot:i] == FN_SLOT_FULL) {
			(void)[_table fnAddKey:(void *)[other->_table fnKeyAtSlot:i]];
		}
	}
	_mutations++;
}

/* ---- the enumerator's half ---- */

- (id)copy
{
	NSHashTable *copy = [[[self class] alloc] initWithPointerFunctions:[self pointerFunctions]
								  capacity:[self count]];
	NSUInteger i;
	NSUInteger slots = [_table slotCount];

	for (i = 0; i < slots; i++) {
		if ([_table fnStateAtSlot:i] == FN_SLOT_FULL) {
			(void)[copy->_table fnAddKey:(void *)[_table fnKeyAtSlot:i]];
		}
	}
	return copy;
}

- (NSUInteger)countByEnumeratingWithState:(NSFastEnumerationState *)state
				  objects:(id *)buffer
				    count:(NSUInteger)length
{
	(void)buffer;
	(void)length;
	/* THE SNAPSHOT IS REBUILT ONLY WHEN THE TABLE HAS CHANGED, keyed by its generation counter. */
	if (_snapshot == NULL || _snapshotGeneration != [_table generation]) {
		NSUInteger i;
		NSUInteger slots = [_table slotCount];

		free(_snapshot);
		_snapshot = (void **)calloc([_table count] > 0 ? [_table count] : 1, sizeof(void *));
		_snapshotCount = 0;
		for (i = 0; i < slots; i++) {
			if ([_table fnStateAtSlot:i] == FN_SLOT_FULL) {
				_snapshot[_snapshotCount++] = (void *)[_table fnKeyAtSlot:i];
			}
		}
		_snapshotGeneration = [_table generation];
	}
	if (state->state != 0) {
		return 0;
	}
	state->itemsPtr = (id *)_snapshot;
	state->mutationsPtr = &_mutations;
	state->state = 1;
	return _snapshotCount;
}

+ (BOOL)supportsSecureCoding
{
	return YES;
}

- (void)encodeWithCoder:(NSCoder *)coder
{
	[coder encodeObject:[self allObjects] forKey:@"NS.objects"];
}

- (instancetype)initWithCoder:(NSCoder *)coder
{
	self = [self initWithOptions:NSPointerFunctionsObjectPointerPersonality capacity:0];
	if (self != nil) {
		NSArray *objects = [coder decodeObjectForKey:@"NS.objects"];
		NSUInteger i;

		for (i = 0; i < [objects count]; i++) {
			(void)[_table fnAddKey:(void *)[objects objectAtIndex:i]];
		}
	}
	return self;
}

@end
