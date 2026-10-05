/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * FNPointerTable — the open-addressed table NSPointerArray's two siblings are built on. W13b, internal.
 *
 * THIS IS NOT PUBLIC API AND IS NOT IN Foundation.h: Apple has no such class, and it is here so that
 * NSHashTable and NSMapTable share ONE implementation of probing, growth and tombstone reuse rather than
 * carrying two that could drift. The map table is exactly this table with values; the hash table passes no
 * value functions and the value array is simply absent.
 *
 * OPEN ADDRESSING WITH LINEAR PROBING AND TOMBSTONES, which is the design that needs no per-entry allocation
 * and so keeps the pointer policy in one place: the table stores the pointers the KEY functions handed it,
 * and it never dereferences one. A slot is EMPTY, FULL or a TOMBSTONE — the third exists because a removal
 * that merely emptied its slot would break the probe chain of everything that collided past it.
 *
 * THE CALLER OWNS THE MEANING: hashing, equality, aquisition and release all come from the NSPointerFunctions
 * object the caller supplies, so this file knows only that a pointer is a pointer.
 *
 * A NULL KEY IS NOT STORABLE, and that is a real limit rather than an oversight: the empty slots are spelled
 * with NULL, so a pointer of zero has no separate representation. An INTEGER personality therefore cannot
 * store the value 0, which the public headers say.
 */

#ifndef FOUNDATION_FNPOINTERTABLE_H
#define FOUNDATION_FNPOINTERTABLE_H

#import <Foundation/NSObject.h>
#import <Foundation/NSPointerFunctions.h>

NS_ASSUME_NONNULL_BEGIN

/* The slot states. */
#define FN_SLOT_EMPTY		0
#define FN_SLOT_FULL		1
#define FN_SLOT_TOMBSTONE	2

@interface FNPointerTable : NSObject
{
	void **_keys;
	void **_values;			/* NULL when the caller passed no value functions */
	unsigned char *_states;
	NSUInteger _capacity;		/* always a power of two */
	NSUInteger _count;		/* FULL slots */
	NSUInteger _used;		/* FULL + TOMBSTONE, which is what decides a rehash */
	NSPointerFunctions *_keyFunctions;
	NSPointerFunctions *_valueFunctions;
	NSUInteger _generation;		/* bumped by every mutation, for the enumeration buffer */
}

- (instancetype)initWithKeyFunctions:(NSPointerFunctions *)keyFunctions
		      valueFunctions:(nullable NSPointerFunctions *)valueFunctions
			    capacity:(NSUInteger)capacity;

- (NSPointerFunctions *)keyFunctions;
- (nullable NSPointerFunctions *)valueFunctions;
- (NSUInteger)count;
- (NSUInteger)slotCount;

/* The lookup doors. `fnMember:` answers the pointer ALREADY in the table that is equal to `key` (which is the
 * set's `-member:`), and `fnValueForKey:` answers the value beside it. */
- (nullable void *)fnMember:(void *)key;
- (nullable void *)fnValueForKey:(void *)key;

/* Insertion and removal. `fnAddKey:` answers NO when the key was already there — the SET's semantics, where a
 * duplicate is not an insertion. */
- (BOOL)fnAddKey:(void *)key;
- (BOOL)fnSetKey:(void *)key value:(void *)value;
- (void)fnRemoveKey:(void *)key;
- (void)fnRemoveAll;

/* The slots, for everything that has to walk the table: a caller reads a slot only when its state is FULL.
 * `-generation` is what tells an enumeration buffer whether it is still current. */
- (unsigned char)fnStateAtSlot:(NSUInteger)slot;
- (nullable void *)fnKeyAtSlot:(NSUInteger)slot;
- (nullable void *)fnValueAtSlot:(NSUInteger)slot;
- (NSUInteger)generation;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_FNPOINTERTABLE_H */
