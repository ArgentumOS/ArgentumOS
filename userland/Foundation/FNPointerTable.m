/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * FNPointerTable.m — the open-addressed table. W13b, internal. See the header.
 *
 * THE PROBE IS THE WHOLE ALGORITHM: `fnIndexForKey:` walks from the key's hash until it finds a FULL slot
 * holding an equal pointer, or an EMPTY slot (which ends the chain), and remembers the first TOMBSTONE it
 * passed so an insertion can reuse it. Every other method is that walk plus one decision.
 *
 * GROWTH IS BY DOUBLING AT THREE QUARTERS USED, counting tombstones — which is why `_used` and `_count` are
 * separate: a table that has seen many removals is mostly tombstones and must be rehashed to stay fast, even
 * though it holds few entries.
 *
 * MANUAL OWNERSHIP (MRC: the whole library is).
 */

#import <Foundation/FNPointerTable.h>
#import <Foundation/NSException.h>

#include <stdlib.h>
#include <string.h>

#define FN_TABLE_MIN_CAPACITY		8
#define FN_TABLE_LOAD_NUMERATOR		3
#define FN_TABLE_LOAD_DENOMINATOR	4

@implementation FNPointerTable

static NSUInteger fn_table_round_up(NSUInteger value)
{
	NSUInteger capacity = FN_TABLE_MIN_CAPACITY;

	while (capacity < value) {
		capacity *= 2;
	}
	return capacity;
}

- (instancetype)initWithKeyFunctions:(NSPointerFunctions *)keyFunctions
		      valueFunctions:(NSPointerFunctions *)valueFunctions
			    capacity:(NSUInteger)capacity
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_keyFunctions = [keyFunctions retain];
	_valueFunctions = [valueFunctions retain];
	_capacity = fn_table_round_up(capacity);
	_keys = (void **)calloc(_capacity, sizeof(void *));
	_states = (unsigned char *)calloc(_capacity, sizeof(unsigned char));
	if (_valueFunctions != nil) {
		_values = (void **)calloc(_capacity, sizeof(void *));
	}
	if (_keys == NULL || _states == NULL || (_valueFunctions != nil && _values == NULL)) {
		[self release];
		[NSException raise:NSMallocException format:@"FNPointerTable: no room for its slots"];
	}
	_count = 0;
	_used = 0;
	_generation = 1;
	return self;
}

- (void)dealloc
{
	NSUInteger i;

	/* THE SLOTS ARE RELINQUISHED THROUGH THE CALLER'S OWN FUNCTIONS, which is the only party that knows
	 * whether that means release an object, free a buffer, or do nothing at all. */
	for (i = 0; i < _capacity; i++) {
		if (_states[i] == FN_SLOT_FULL) {
			[_keyFunctions fnRelinquish:_keys[i]];
			if (_values != NULL && _values[i] != NULL) {
				[_valueFunctions fnRelinquish:_values[i]];
			}
		}
	}
	free(_keys);
	free(_states);
	free(_values);
	[_keyFunctions release];
	[_valueFunctions release];
	[super dealloc];
}

- (NSPointerFunctions *)keyFunctions { return _keyFunctions; }
- (NSPointerFunctions *)valueFunctions { return _valueFunctions; }
- (NSUInteger)count { return _count; }
- (NSUInteger)slotCount { return _capacity; }
- (NSUInteger)generation { return _generation; }
- (unsigned char)fnStateAtSlot:(NSUInteger)slot { return _states[slot]; }
- (void *)fnKeyAtSlot:(NSUInteger)slot { return _keys[slot]; }
- (void *)fnValueAtSlot:(NSUInteger)slot { return _values != NULL ? _values[slot] : NULL; }

/* THE WALK. It answers the slot the key occupies, or the slot an insertion should use, and reports whether
 * that slot already holds an equal key. */
- (NSUInteger)fnIndexForKey:(void *)key found:(BOOL *)found
{
	NSUInteger mask = _capacity - 1;
	NSUInteger slot = [_keyFunctions fnHash:key] & mask;
	NSUInteger firstTombstone = (NSUInteger)-1;

	for (;;) {
		if (_states[slot] == FN_SLOT_EMPTY) {
			*found = NO;
			return firstTombstone != (NSUInteger)-1 ? firstTombstone : slot;
		}
		if (_states[slot] == FN_SLOT_TOMBSTONE) {
			if (firstTombstone == (NSUInteger)-1) {
				firstTombstone = slot;
			}
		} else if ([_keyFunctions fnIsEqual:_keys[slot] to:key]) {
			*found = YES;
			return slot;
		}
		slot = (slot + 1) & mask;
	}
}

/* THE TABLE IS REBUILT AT DOUBLE SIZE, and the old slots are emptied by REINSERTING each key rather than by
 * copying: the copy would keep every collision where it was and the point of growing is to spread them. */
- (void)fnGrow
{
	NSUInteger oldCapacity = _capacity;
	void **oldKeys = _keys;
	void **oldValues = _values;
	unsigned char *oldStates = _states;
	NSUInteger i;

	_capacity *= 2;
	_keys = (void **)calloc(_capacity, sizeof(void *));
	_states = (unsigned char *)calloc(_capacity, sizeof(unsigned char));
	if (_valueFunctions != nil) {
		_values = (void **)calloc(_capacity, sizeof(void *));
	} else {
		_values = NULL;
	}
	if (_keys == NULL || _states == NULL || (_valueFunctions != nil && _values == NULL)) {
		[NSException raise:NSMallocException format:@"FNPointerTable: no room to grow"];
	}
	_count = 0;
	_used = 0;
	for (i = 0; i < oldCapacity; i++) {
		if (oldStates[i] == FN_SLOT_FULL) {
			/* THE POINTER MOVES, SO IT IS NOT ACQUIRED AGAIN — the table already owns it. */
			BOOL found = NO;
			NSUInteger slot = [self fnIndexForKey:oldKeys[i] found:&found];

			_keys[slot] = oldKeys[i];
			if (_values != NULL) {
				_values[slot] = oldValues != NULL ? oldValues[i] : NULL;
			}
			_states[slot] = FN_SLOT_FULL;
			_count++;
			_used++;
		}
	}
	free(oldKeys);
	free(oldStates);
	free(oldValues);
}

- (void)fnMaybeGrow
{
	if ((_used + 1) * FN_TABLE_LOAD_DENOMINATOR >= _capacity * FN_TABLE_LOAD_NUMERATOR) {
		[self fnGrow];
	}
}

- (void *)fnMember:(void *)key
{
	BOOL found = NO;
	NSUInteger slot;

	if (key == NULL || _count == 0) {
		return NULL;
	}
	slot = [self fnIndexForKey:key found:&found];
	return found ? _keys[slot] : NULL;
}

- (void *)fnValueForKey:(void *)key
{
	BOOL found = NO;
	NSUInteger slot;

	if (key == NULL || _count == 0) {
		return NULL;
	}
	slot = [self fnIndexForKey:key found:&found];
	return found && _values != NULL ? _values[slot] : NULL;
}

- (BOOL)fnAddKey:(void *)key
{
	BOOL found = NO;
	NSUInteger slot;

	if (key == NULL) {
		[NSException raise:NSInvalidArgumentException
			    format:@"FNPointerTable: a NULL pointer is not a storable key"];
	}
	[self fnMaybeGrow];
	slot = [self fnIndexForKey:key found:&found];
	if (found) {
		return NO;
	}
	_keys[slot] = [_keyFunctions fnAcquire:key];
	if (_values != NULL) {
		_values[slot] = NULL;
	}
	if (_states[slot] != FN_SLOT_TOMBSTONE) {
		_used++;
	}
	_states[slot] = FN_SLOT_FULL;
	_count++;
	_generation++;
	return YES;
}

- (BOOL)fnSetKey:(void *)key value:(void *)value
{
	BOOL found = NO;
	NSUInteger slot;
	void *acquired;
	void *oldKey;
	void *oldValue;

	if (key == NULL) {
		[NSException raise:NSInvalidArgumentException
			    format:@"FNPointerTable: a NULL pointer is not a storable key"];
	}
	if (_values == NULL) {
		[NSException raise:NSInternalInconsistencyException
			    format:@"FNPointerTable: this table holds no values"];
	}
	[self fnMaybeGrow];
	slot = [self fnIndexForKey:key found:&found];
	if (found) {
		oldKey = _keys[slot];
		oldValue = _values[slot];
		/* ACQUIRE BEFORE RELINQUISH: the two may be the SAME object when a caller re-sets a key to its own
		 * value, and releasing first would free it out from under the acquire. */
		acquired = value != NULL ? [_valueFunctions fnAcquire:value] : NULL;
		if (oldValue != NULL) {
			[_valueFunctions fnRelinquish:oldValue];
		}
		_values[slot] = acquired;
		(void)oldKey;
		_generation++;
		return NO;
	}
	_keys[slot] = [_keyFunctions fnAcquire:key];
	_values[slot] = value != NULL ? [_valueFunctions fnAcquire:value] : NULL;
	if (_states[slot] != FN_SLOT_TOMBSTONE) {
		_used++;
	}
	_states[slot] = FN_SLOT_FULL;
	_count++;
	_generation++;
	return YES;
}

- (void)fnRemoveKey:(void *)key
{
	BOOL found = NO;
	NSUInteger slot;

	if (key == NULL || _count == 0) {
		return;
	}
	slot = [self fnIndexForKey:key found:&found];
	if (!found) {
		return;
	}
	[_keyFunctions fnRelinquish:_keys[slot]];
	if (_values != NULL && _values[slot] != NULL) {
		[_valueFunctions fnRelinquish:_values[slot]];
	}
	_keys[slot] = NULL;
	if (_values != NULL) {
		_values[slot] = NULL;
	}
	/* A TOMBSTONE, NOT AN EMPTY SLOT: everything that probed past this one still has to be findable. */
	_states[slot] = FN_SLOT_TOMBSTONE;
	_count--;
	_generation++;
}

- (void)fnRemoveAll
{
	NSUInteger i;

	for (i = 0; i < _capacity; i++) {
		if (_states[i] == FN_SLOT_FULL) {
			[_keyFunctions fnRelinquish:_keys[i]];
			if (_values != NULL && _values[i] != NULL) {
				[_valueFunctions fnRelinquish:_values[i]];
			}
			_keys[i] = NULL;
			if (_values != NULL) {
				_values[i] = NULL;
			}
		}
		_states[i] = FN_SLOT_EMPTY;
	}
	_count = 0;
	_used = 0;
	_generation++;
}

@end
