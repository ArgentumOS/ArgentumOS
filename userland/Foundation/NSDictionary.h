/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSDictionary / NSMutableDictionary — key → value.
 * docs/design/foundation-plan.md, F3; subscripting and the nil-key guard from
 * the public-API audit.
 *
 * KEYS ARE COPIED, VALUES ARE RETAINED. That asymmetry is Cocoa's, and it is not
 * a detail: a key is a lookup TOKEN, so if the caller hands over a mutable string
 * and then edits it, a table that merely retained the key would keep the token
 * that no longer hashes where it was filed and the value would become
 * unreachable — a slow, silent bug. Copying the key freezes its hash and its
 * equality. Values are the caller's objects, held by reference like an array's
 * elements.
 *
 * A key's equality and hash are NSObject's contract, so anything can be a key as
 * long as it implements `-isEqual:`, `-hash` AND the copying protocol. That last
 * requirement was aspirational until the audit: NSNumber did not conform to
 * NSCopying, and `[key copy]` on it returned NIL instead of raising, so a number
 * key filed a phantom entry and its value was unreachable. The table now REFUSES
 * a nil key-copy rather than filing one.
 *
 * The table is CHAINING over a power-of-two bucket array, which grows before the
 * load factor reaches one. No class cluster (v1's rule): this is the class.
 */

#ifndef FOUNDATION_NSDICTIONARY_H
#define FOUNDATION_NSDICTIONARY_H

#import <Foundation/NSObject.h>
#import <Foundation/NSFastEnumeration.h>
#import <Foundation/NSEnumerator.h>

@class NSArray<ObjectType>, NSEnumerator<ObjectType>;

/* NULLABILITY (F6): NONNULL by default. -objectForKey: and its subscript answer
 * nil for a key that is not there, and -setObject:forKeyedSubscript: takes nil
 * because `dict[k] = nil` REMOVES the key (Cocoa's rule). */
NS_ASSUME_NONNULL_BEGIN

struct FNDictEntry;			/* opaque; defined in NSDictionary.m */

@interface NSDictionary<__covariant KeyType, __covariant ObjectType> : NSObject <NSCopying, NSFastEnumeration>
{
	struct FNDictEntry **_buckets;	/* power-of-two count, so index = hash & (count-1) */
	unsigned long _bucketCount;
	unsigned long _count;
	unsigned long _mutations;	/* bumped by every mutation, for fast enumeration */
	id __unsafe_unretained *_keys;	/* built lazily for enumeration; dropped on mutation */
	unsigned long _keyCount;
}

+ (NSDictionary *)dictionary;
+ (NSDictionary<KeyType, ObjectType> *)dictionaryWithObject:(ObjectType)value forKey:(KeyType <NSCopying>)key;
+ (NSDictionary<KeyType, ObjectType> *)dictionaryWithDictionary:(NSDictionary<KeyType, ObjectType> *)other;
+ (NSDictionary<KeyType, ObjectType> *)dictionaryWithObjects:(const ObjectType _Nonnull * _Nonnull)values
				 forKeys:(const KeyType _Nonnull * _Nonnull)keys
				   count:(NSUInteger)count;
+ (NSDictionary *)dictionaryWithObjectsAndKeys:(id)firstObject, ... NS_REQUIRES_NIL_TERMINATION;
+ (NSDictionary<KeyType, ObjectType> *)dictionaryWithObjects:(NSArray<ObjectType> *)objects forKeys:(NSArray<KeyType> *)keys;

- (id)initWithObject:(id)value forKey:(id)key;
- (id)initWithDictionary:(NSDictionary<KeyType, ObjectType> *)other;
- (id)initWithObjects:(const ObjectType _Nonnull * _Nonnull)values
	      forKeys:(const KeyType _Nonnull * _Nonnull)keys
		count:(NSUInteger)count;
- (id)initWithObjectsAndKeys:(id)firstObject, ... NS_REQUIRES_NIL_TERMINATION;

- (NSUInteger)count;
- (nullable ObjectType)objectForKey:(KeyType)key;
- (NSArray<KeyType> *)allKeys;
- (NSArray<ObjectType> *)allValues;
- (NSArray<KeyType> *)allKeysForObject:(ObjectType)object;
- (NSArray<ObjectType> *)objectsForKeys:(NSArray<KeyType> *)keys notFoundMarker:(ObjectType)marker;
- (NSArray<KeyType> *)keysSortedByValueUsingSelector:(SEL)comparator;
- (NSArray<KeyType> *)keysSortedByValueUsingComparator:(NSComparator)comparator;
- (void)enumerateKeysAndObjectsUsingBlock:(void (^)(KeyType key, ObjectType obj, BOOL *stop))block;
- (NSEnumerator<KeyType> *)keyEnumerator;
- (NSEnumerator<ObjectType> *)objectEnumerator;
- (void)getObjects:(ObjectType __unsafe_unretained _Nonnull * _Nonnull)objects
	   andKeys:(KeyType __unsafe_unretained _Nonnull * _Nonnull)keys;
/* Cocoa's subscript: `dict[k]` lowers to this. */
- (nullable ObjectType)objectForKeyedSubscript:(KeyType)key;

- (BOOL)isEqualToDictionary:(NSDictionary<KeyType, ObjectType> *)other;

@end

@interface NSMutableDictionary<KeyType, ObjectType> : NSDictionary<KeyType, ObjectType> <NSMutableCopying>

+ (NSMutableDictionary *)dictionary;
+ (NSMutableDictionary *)dictionaryWithCapacity:(NSUInteger)capacity;
- (id)initWithCapacity:(NSUInteger)capacity;

- (void)addEntriesFromDictionary:(NSDictionary<KeyType, ObjectType> *)other;
- (void)setDictionary:(NSDictionary<KeyType, ObjectType> *)other;
- (void)removeObjectsForKeys:(NSArray<KeyType> *)keys;

- (void)setObject:(ObjectType)value forKey:(KeyType)key;
- (void)removeObjectForKey:(KeyType)key;
- (void)removeAllObjects;

/* `dict[k] = v` lowers to this, and `dict[k] = nil` REMOVES the key (Cocoa's
 * rule) — which is why it cannot simply forward to -setObject:forKey:. */
- (void)setObject:(nullable ObjectType)object forKeyedSubscript:(KeyType)key;

NS_ASSUME_NONNULL_END

@end

#endif /* FOUNDATION_NSDICTIONARY_H */
