/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSHashTable — an UNORDERED SET OF POINTERS, with NSPointerFunctions deciding what a pointer is.
 * docs/design/foundation-plan.md §12.3 W13b.
 *
 * IT IS THE SET THAT CAN HOLD WHAT NSSet CANNOT: the callouts let it hash by identity rather than equality, or
 * by a C string's bytes, or not to own its members at all. The two convenience constructors name the common
 * cases, and the WEAK one is the reason the class is usually reached for: `+weakObjectsHashTable` holds
 * objects without retaining them, so a table can be a registry of observers that does not keep them alive.
 *
 * ITS MEMBERS ARE NOT OBJECTS IN GENERAL, which is why the whole API speaks of objects only in the object
 * personalities: `-addObject:`, `-containsObject:` and the set operations take `id` because that is what a
 * caller has, and the functions decide what it means. A pointer that is NOT an object — an integer, a struct,
 * an address — is still a legitimate member, and the type is the caller's to remember.
 *
 * ONE LIMIT WORTH STATING WHERE IT BITES: A NULL POINTER CANNOT BE A MEMBER. The empty slots are spelled with
 * NULL, so there is no separate representation for a pointer of zero — and an INTEGER personality therefore
 * cannot store the value 0. The member doors refuse it rather than growing a table that could not be read.
 *
 * THE SET OPERATIONS ARE THE MATHEMATICAL ONES over the same callouts, so `-unionHashTable:` of two tables
 * that mean different things by "equal" is the caller's mistake rather than an undefined one: it will do
 * exactly what the receiving table's functions say.
 */

#ifndef FOUNDATION_NSHASHTABLE_H
#define FOUNDATION_NSHASHTABLE_H

#import <Foundation/NSObject.h>
#import <Foundation/NSPointerFunctions.h>
#import <Foundation/NSFastEnumeration.h>
#import <Foundation/NSCoding.h>

@class NSArray;
@class NSSet;
@class NSEnumerator;
@class FNPointerTable;

NS_ASSUME_NONNULL_BEGIN

/* Apple's options for a hash table ARE NSPointerFunctions' options, under a name of its own — the same word,
 * spelled the same way, because `NSHashTableOptions` says WHICH set of options it is. */
typedef NSPointerFunctionsOptions NSHashTableOptions;

@interface NSHashTable : NSObject <NSCopying, NSFastEnumeration, NSSecureCoding>
{
	FNPointerTable *_table;
	/* THE ENUMERATION SNAPSHOT: a compacted C array of the members, rebuilt whenever the table has changed.
	 * Fast enumeration hands a plain C array to the caller, so the gaps a hash table has cannot be exposed. */
	void **_snapshot;	/* `void **`, not `id *`: an ARC consumer of this header cannot spell `id *` */
	NSUInteger _snapshotCount;
	NSUInteger _snapshotGeneration;
	unsigned long _mutations;
}

- (instancetype)initWithOptions:(NSHashTableOptions)options capacity:(NSUInteger)initialCapacity;
- (instancetype)initWithPointerFunctions:(NSPointerFunctions *)functions
				capacity:(NSUInteger)initialCapacity;

+ (instancetype)weakObjectsHashTable;
+ (instancetype)hashTableWithOptions:(NSHashTableOptions)options;

- (NSPointerFunctions *)pointerFunctions;

- (NSUInteger)count;
- (nullable id)anyObject;
- (NSArray *)allObjects;
- (NSSet *)setRepresentation;
- (BOOL)containsObject:(nullable id)anObject;
- (nullable id)member:(nullable id)object;
- (NSEnumerator *)objectEnumerator;

- (void)addObject:(nullable id)object;
- (void)removeObject:(nullable id)object;
- (void)removeAllObjects;

/* The set operations, each in place on the receiver. */
- (void)intersectHashTable:(NSHashTable *)other;
- (BOOL)intersectsHashTable:(NSHashTable *)other;
- (BOOL)isSubsetOfHashTable:(NSHashTable *)other;
- (BOOL)isEqualToHashTable:(NSHashTable *)other;
- (void)minusHashTable:(NSHashTable *)other;
- (void)unionHashTable:(NSHashTable *)other;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSHASHTABLE_H */
