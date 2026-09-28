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

/* THE FIVE NAMES APPLE DECLARES BESIDE THE CLASS, and they are macros here as they are there: each one is the
 * NSPointerFunctions option it names, so a caller who writes `NSMapTableWeakMemory` gets exactly the policy
 * `+mapTableWithKeyOptions:valueOptions:` already understands - there is no second vocabulary to keep in step.
 *
 * `NSMapTableZeroingWeakMemory` IS THE DEPRECATED ONE and it aliases the flag NSPointerFunctions.h declares as
 * declared-not-interpreted (see that comment): the name compiles, and the zeroing behaviour it asks for is the
 * one this library's runtime cannot provide. */
#define NSMapTableStrongMemory			NSPointerFunctionsStrongMemory
#define NSMapTableZeroingWeakMemory		NSPointerFunctionsZeroingWeakMemory
#define NSMapTableCopyIn			NSPointerFunctionsCopyIn
#define NSMapTableObjectPointerPersonality	NSPointerFunctionsObjectPointerPersonality
#define NSMapTableWeakMemory			NSPointerFunctionsWeakMemory

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


/* THE SENTINELS: a call-back returns one of these when there is no answer, and each is a pointer no real key of
 * that personality can be. THEY SIT OUTSIDE THE NULLABILITY REGION because a MACRO is not a declaration — and
 * `(void *)-1` is precisely the value the annotation would have to lie about. */
#define NSNotAnIntMapKey	((void *)(long)-1)
#define NSNotAnIntegerMapKey	((void *)(long)-1)
#define NSNotAPointerMapKey	((void *)-1)

NS_ASSUME_NONNULL_BEGIN

/* ===================================================================================================
 * THE LEGACY C API, LANDED BECAUSE §62.24 RETIRED THE DEPRECATION GROUND (§62.44)
 *
 * These are the pre-10.5 functions and call-back STRUCTS Apple deprecated when `NSPointerFunctions` arrived, and
 * an old application's source calls them. THE SHAPE HERE IS A PRIVATE SUBCLASS rather than a bridge to
 * `NSPointerFunctions`, AND THAT WAS A MEASUREMENT RATHER THAN A PREFERENCE: the engine's function pointers take
 * `(const void *item, …)` while Apple's legacy call-backs take `(NSMapTable *table, const void *key)` — and A C
 * FUNCTION POINTER CANNOT CLOSE OVER ITS TABLE, so no fixed adapter can forward a caller's call-back with the
 * table it is promised. The subclass carries the call-backs AND the pairs, hands `self` to every one of them
 * (Apple's contract, satisfied), and leaves the modern object API untouched: the table a legacy caller gets is an
 * `NSMapTable` that answers the object API too, which is what Apple's own does.
 *
 * TWO OF THE THIRTY-EIGHT NAMES ARE NOT HERE, AND THE REASON IS A DECISION RATHER THAN A GAP:
 * `NSCreateMapTableWithZone` and `NSCopyMapTableWithZone` take an `NSZone`, and THIS LIBRARY HAS NO ZONE TYPE —
 * `NSObjCRuntime.h` records the user's own sequence that removed every zone-taking method, then every
 * zone-returning one, and finally the type itself (zones are 32-bit API). A header cannot spell a type the library
 * removed on purpose, so those two ledger rows stay OWED with that ground, and the other thirty-six land here.
 *
 * FIELD ORDER AND THE THREE SENTINELS ARE OURS UNDER §11.6.1 D2: Apple publishes the field NAMES and what a
 * sentinel is FOR, and the probe pins what this header defines.
 * =================================================================================================== */

typedef struct {
	unsigned (*hash)(NSMapTable *table, const void *key);
	BOOL (*isEqual)(NSMapTable *table, const void *key1, const void *key2);
	void (*retain)(NSMapTable *table, const void *key);
	void (*release)(NSMapTable *table, const void *key);
	NSString * _Nullable (* _Nullable describe)(NSMapTable *table, const void *key);
	const void *notAKeyMarker;
} NSMapTableKeyCallBacks;

typedef struct {
	void (*retain)(NSMapTable *table, const void *value);
	void (*release)(NSMapTable *table, const void *value);
	NSString * _Nullable (* _Nullable describe)(NSMapTable *table, const void *value);
} NSMapTableValueCallBacks;

/* THE ENUMERATOR IS A VALUE THE CALLER HOLDS: the table, a position, and the key snapshot the walk was opened
 * with — which is what makes `NSNextMapEnumeratorPair` a function of the struct alone, as Apple's is. */
typedef struct {
	NSMapTable *table;
	NSUInteger index;
	NSArray *keys;
} NSMapEnumerator;

extern const NSMapTableKeyCallBacks NSIntMapKeyCallBacks;
extern const NSMapTableValueCallBacks NSIntMapValueCallBacks;
extern const NSMapTableKeyCallBacks NSIntegerMapKeyCallBacks;
extern const NSMapTableValueCallBacks NSIntegerMapValueCallBacks;
extern const NSMapTableKeyCallBacks NSNonOwnedPointerMapKeyCallBacks;
extern const NSMapTableValueCallBacks NSNonOwnedPointerMapValueCallBacks;
extern const NSMapTableKeyCallBacks NSNonOwnedPointerOrNullMapKeyCallBacks;
extern const NSMapTableKeyCallBacks NSNonRetainedObjectMapKeyCallBacks;
extern const NSMapTableValueCallBacks NSNonRetainedObjectMapValueCallBacks;
extern const NSMapTableKeyCallBacks NSObjectMapKeyCallBacks;
extern const NSMapTableValueCallBacks NSObjectMapValueCallBacks;
extern const NSMapTableKeyCallBacks NSOwnedPointerMapKeyCallBacks;
extern const NSMapTableValueCallBacks NSOwnedPointerMapValueCallBacks;

NSMapTable *NSCreateMapTable(NSMapTableKeyCallBacks keyCallBacks,
			     NSMapTableValueCallBacks valueCallBacks,
			     NSUInteger capacity);
void NSFreeMapTable(NSMapTable *table);
void NSResetMapTable(NSMapTable *table);
BOOL NSCompareMapTables(NSMapTable *table1, NSMapTable *table2);
void *NSMapGet(NSMapTable *table, const void *key);
void NSMapInsert(NSMapTable *table, const void *key, const void *value);
void NSMapInsertKnownAbsent(NSMapTable *table, const void *key, const void *value);
/* NULL when the entry was inserted, the existing value when it was not - see NSHashTable's note. */
void * _Nullable NSMapInsertIfAbsent(NSMapTable *table, const void *key, const void *value);
void NSMapRemove(NSMapTable *table, const void *key);
BOOL NSMapMember(NSMapTable *table, const void *key,
		 void * _Nullable * _Nullable originalKey,
		 void * _Nullable * _Nullable value);
NSUInteger NSCountMapTable(NSMapTable *table);
NSString *NSStringFromMapTable(NSMapTable *table);
NSArray *NSAllMapTableKeys(NSMapTable *table);
NSArray *NSAllMapTableValues(NSMapTable *table);
NSMapEnumerator NSEnumerateMapTable(NSMapTable *table);
BOOL NSNextMapEnumeratorPair(NSMapEnumerator *enumerator,
			     void * _Nullable * _Nullable key,
			     void * _Nullable * _Nullable value);
void NSEndMapTableEnumeration(NSMapEnumerator *enumerator);

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSMAPTABLE_H */
