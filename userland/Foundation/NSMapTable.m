/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSMapTable.m — W13b. See the header for the two independent sides and for the nil-value rule.
 *
 * THE ONLY THING THIS FILE ADDS TO THE SHARED TABLE IS A SECOND SET OF FUNCTIONS: `FNPointerTable` was
 * written with a value array precisely so that this class would not need a table of its own, and the four
 * convenience constructors below are nothing but a pairing of two options words.
 *
 * FAST ENUMERATION HANDS BACK THE VALUES, which is what Apple's map table enumerates (the keys have their own
 * `-keyEnumerator`), and the snapshot is kept in the two parallel arrays the header declares.
 *
 * MANUAL OWNERSHIP (MRC: the whole library is).
 */

#import <Foundation/NSMapTable.h>
#import <Foundation/FNPointerTable.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSEnumerator.h>
#import <Foundation/NSException.h>
#import <Foundation/NSString.h>
#import <Foundation/NSCoder.h>
#import <Foundation/NSNull.h>

#include <stdlib.h>

@implementation NSMapTable

- (instancetype)initWithKeyOptions:(NSMapTableOptions)keyOptions
		       valueOptions:(NSMapTableOptions)valueOptions
			   capacity:(NSUInteger)initialCapacity
{
	return [self initWithKeyPointerFunctions:
			[[[NSPointerFunctions alloc] initWithOptions:keyOptions] autorelease]
		     valuePointerFunctions:
			[[[NSPointerFunctions alloc] initWithOptions:valueOptions] autorelease]
				  capacity:initialCapacity];
}

- (instancetype)initWithKeyPointerFunctions:(NSPointerFunctions *)keyFunctions
		     valuePointerFunctions:(NSPointerFunctions *)valueFunctions
				  capacity:(NSUInteger)initialCapacity
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_table = [[FNPointerTable alloc] initWithKeyFunctions:keyFunctions
					     valueFunctions:valueFunctions
						   capacity:initialCapacity];
	_mutations = 1;
	return self;
}

+ (instancetype)mapTableWithKeyOptions:(NSMapTableOptions)keyOptions
			  valueOptions:(NSMapTableOptions)valueOptions
{
	return [[[self alloc] initWithKeyOptions:keyOptions
				    valueOptions:valueOptions
					capacity:0] autorelease];
}

+ (instancetype)strongToStrongObjectsMapTable
{
	return [self mapTableWithKeyOptions:NSPointerFunctionsObjectPersonality
			       valueOptions:NSPointerFunctionsObjectPersonality];
}

+ (instancetype)weakToStrongObjectsMapTable
{
	/* THE OBSERVER REGISTRY: the keys are not owned (and not zeroed), the values are. */
	return [self mapTableWithKeyOptions:
			(NSPointerFunctionsObjectPointerPersonality | NSPointerFunctionsWeakMemory)
			       valueOptions:NSPointerFunctionsObjectPersonality];
}

+ (instancetype)strongToWeakObjectsMapTable
{
	return [self mapTableWithKeyOptions:NSPointerFunctionsObjectPointerPersonality
			       valueOptions:
			(NSPointerFunctionsObjectPointerPersonality | NSPointerFunctionsWeakMemory)];
}

+ (instancetype)weakToWeakObjectsMapTable
{
	return [self mapTableWithKeyOptions:
			(NSPointerFunctionsObjectPointerPersonality | NSPointerFunctionsWeakMemory)
			       valueOptions:
			(NSPointerFunctionsObjectPointerPersonality | NSPointerFunctionsWeakMemory)];
}

- (void)dealloc
{
	[_table release];
	free(_snapshotKeys);
	free(_snapshotValues);
	[super dealloc];
}

- (NSPointerFunctions *)keyPointerFunctions { return [_table keyFunctions]; }
- (NSPointerFunctions *)valuePointerFunctions { return [_table valueFunctions]; }

- (id)objectForKey:(id)aKey
{
	return aKey != nil ? (id)[_table fnValueForKey:(void *)aKey] : nil;
}

- (void)setObject:(id)anObject forKey:(id)aKey
{
	if (aKey == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSMapTable: nil is not a key (and a NULL pointer cannot be one)"];
	}
	/* A NIL VALUE IS STORED, not a removal: the entry exists with an empty value. See the header. */
	(void)[_table fnSetKey:(void *)aKey value:(void *)anObject];
	_mutations++;
}

- (void)removeObjectForKey:(id)aKey
{
	if (aKey != nil) {
		[_table fnRemoveKey:(void *)aKey];
		_mutations++;
	}
}

- (void)removeAllObjects
{
	[_table fnRemoveAll];
	_mutations++;
}

- (NSUInteger)count
{
	return [_table count];
}

- (NSEnumerator *)keyEnumerator
{
	return [[self fnKeysArray] objectEnumerator];
}

- (NSEnumerator *)objectEnumerator
{
	return [[self fnValuesArray] objectEnumerator];
}

- (NSDictionary *)dictionaryRepresentation
{
	/* BUILT FROM THE PAIRS, so a key whose value is empty is represented by an NSNull rather than being
	 * dropped — a dictionary cannot hold a nil value, and silently losing the entry would be worse. */
	NSMutableDictionary *representation = [NSMutableDictionary dictionary];
	NSUInteger i;
	NSUInteger slots = [_table slotCount];

	for (i = 0; i < slots; i++) {
		if ([_table fnStateAtSlot:i] == FN_SLOT_FULL) {
			id key = (id)[_table fnKeyAtSlot:i];
			id value = (id)[_table fnValueAtSlot:i];

			[representation setObject:(value != nil ? value : (id)[NSNull null]) forKey:key];
		}
	}
	return representation;
}

- (NSArray *)fnKeysArray
{
	NSMutableArray *keys = [NSMutableArray arrayWithCapacity:[_table count]];
	NSUInteger i;
	NSUInteger slots = [_table slotCount];

	for (i = 0; i < slots; i++) {
		if ([_table fnStateAtSlot:i] == FN_SLOT_FULL) {
			[keys addObject:(id)[_table fnKeyAtSlot:i]];
		}
	}
	return keys;
}

- (NSArray *)fnValuesArray
{
	NSMutableArray *values = [NSMutableArray arrayWithCapacity:[_table count]];
	NSUInteger i;
	NSUInteger slots = [_table slotCount];

	for (i = 0; i < slots; i++) {
		if ([_table fnStateAtSlot:i] == FN_SLOT_FULL) {
			id value = (id)[_table fnValueAtSlot:i];

			[values addObject:(value != nil ? value : (id)[NSNull null])];
		}
	}
	return values;
}

- (id)copy
{
	NSMapTable *copy = [[[self class] alloc]
		initWithKeyPointerFunctions:[_table keyFunctions]
		     valuePointerFunctions:[_table valueFunctions]
				  capacity:[_table count]];
	NSUInteger i;
	NSUInteger slots = [_table slotCount];

	for (i = 0; i < slots; i++) {
		if ([_table fnStateAtSlot:i] == FN_SLOT_FULL) {
			(void)[copy->_table fnSetKey:(void *)[_table fnKeyAtSlot:i]
					       value:(void *)[_table fnValueAtSlot:i]];
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
	if (_snapshotKeys == NULL || _snapshotGeneration != [_table generation]) {
		NSUInteger i;
		NSUInteger slots = [_table slotCount];
		NSUInteger size = [_table count] > 0 ? [_table count] : 1;

		free(_snapshotKeys);
		free(_snapshotValues);
		_snapshotKeys = (void **)calloc(size, sizeof(void *));
		_snapshotValues = (void **)calloc(size, sizeof(void *));
		_snapshotCount = 0;
		for (i = 0; i < slots; i++) {
			if ([_table fnStateAtSlot:i] == FN_SLOT_FULL) {
				id value = (id)[_table fnValueAtSlot:i];

				_snapshotKeys[_snapshotCount] = (void *)[_table fnKeyAtSlot:i];
				_snapshotValues[_snapshotCount] = value != nil ? (void *)value : (void *)[NSNull null];
				_snapshotCount++;
			}
		}
		_snapshotGeneration = [_table generation];
	}
	if (state->state != 0) {
		return 0;
	}
	/* THE VALUES, with `mutationsPtr` shared with the keys' enumerator: a mutation during the walk is caught
	 * whichever side is being enumerated. */
	state->itemsPtr = (id *)_snapshotValues;
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
	[coder encodeObject:[self dictionaryRepresentation] forKey:@"NS.map"];
}

- (instancetype)initWithCoder:(NSCoder *)coder
{
	self = [self initWithKeyOptions:NSPointerFunctionsObjectPointerPersonality
			   valueOptions:NSPointerFunctionsObjectPointerPersonality
			       capacity:0];
	if (self != nil) {
		NSDictionary *representation = [coder decodeObjectForKey:@"NS.map"];
		NSArray *keys = [representation allKeys];
		NSUInteger i;

		for (i = 0; i < [keys count]; i++) {
			id key = [keys objectAtIndex:i];
			id value = [representation objectForKey:key];

			(void)[_table fnSetKey:(void *)key
				  value:(value != nil && ![value isKindOfClass:[NSNull class]]
					 ? (void *)value : NULL)];
		}
	}
	return self;
}

@end
