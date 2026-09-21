/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSMapTable — a MAP WHOSE KEYS AND VALUES ARE POINTERS, with separate NSPointerFunctions for each side.
 * docs/design/foundation-plan.md §12.3 W13b.
 *
 * THE TWO SIDES ARE CONFIGURED INDEPENDENTLY, which is the whole difference between this and NSDictionary:
 * the keys may be held weakly while the values are held strongly (`+weakToStrongObjectsMapTable`, the
 * observer registry), or the other way round (a cache whose keys outlive their values), and either side may
 * use a personality that is not an object at all.
 *
 * A KEY MAY HAVE NO VALUE, and a value of nil is STORED rather than treated as a removal: the entry exists
 * with an empty value, which is why `-objectForKey:` answering nil cannot be read as "absent" for such a
 * table. That ambiguity is Apple's, and it is stated here rather than papered over.
 *
 * A NULL POINTER CANNOT BE A KEY, for the same reason it cannot be a member of NSHashTable: the empty slots
 * are spelled with NULL. A NULL VALUE is fine — see above.
 */

#ifndef FOUNDATION_NSMAPTABLE_H
#define FOUNDATION_NSMAPTABLE_H

#import <Foundation/NSObject.h>
#import <Foundation/NSPointerFunctions.h>
#import <Foundation/NSFastEnumeration.h>
#import <Foundation/NSCoding.h>

@class NSArray;
@class NSDictionary;
@class NSEnumerator;
@class FNPointerTable;

NS_ASSUME_NONNULL_BEGIN

/* As with NSHashTable, Apple's map-table options ARE NSPointerFunctions' options under a name of their own. */
typedef NSPointerFunctionsOptions NSMapTableOptions;

@interface NSMapTable : NSObject <NSCopying, NSFastEnumeration, NSSecureCoding>
{
	FNPointerTable *_table;
	void **_snapshotKeys;	/* `void **` for the reason NSHashTable.h gives */
	void **_snapshotValues;
	NSUInteger _snapshotCount;
	NSUInteger _snapshotGeneration;
	unsigned long _mutations;
}

- (instancetype)initWithKeyOptions:(NSMapTableOptions)keyOptions
		       valueOptions:(NSMapTableOptions)valueOptions
			   capacity:(NSUInteger)initialCapacity;
- (instancetype)initWithKeyPointerFunctions:(NSPointerFunctions *)keyFunctions
		     valuePointerFunctions:(NSPointerFunctions *)valueFunctions
				  capacity:(NSUInteger)initialCapacity;

+ (instancetype)mapTableWithKeyOptions:(NSMapTableOptions)keyOptions
			  valueOptions:(NSMapTableOptions)valueOptions;
+ (instancetype)strongToStrongObjectsMapTable;
+ (instancetype)weakToStrongObjectsMapTable;
+ (instancetype)strongToWeakObjectsMapTable;
+ (instancetype)weakToWeakObjectsMapTable;

- (NSPointerFunctions *)keyPointerFunctions;
- (NSPointerFunctions *)valuePointerFunctions;

- (nullable id)objectForKey:(nullable id)aKey;
- (void)setObject:(nullable id)anObject forKey:(nullable id)aKey;
- (void)removeObjectForKey:(nullable id)aKey;
- (void)removeAllObjects;

- (NSUInteger)count;
- (NSEnumerator *)keyEnumerator;
- (NSEnumerator *)objectEnumerator;
- (NSDictionary *)dictionaryRepresentation;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSMAPTABLE_H */
