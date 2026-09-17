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

#import <foundation/NSObject.h>
#import <foundation/NSFastEnumeration.h>

struct FNDictEntry;			/* opaque; defined in nsdictionary.m */

@interface NSDictionary : NSObject <NSCopying, NSFastEnumeration>
{
	struct FNDictEntry **_buckets;	/* power-of-two count, so index = hash & (count-1) */
	unsigned long _bucketCount;
	unsigned long _count;
	unsigned long _mutations;	/* bumped by every mutation, for fast enumeration */
	id __unsafe_unretained *_keys;	/* built lazily for enumeration; dropped on mutation */
	unsigned long _keyCount;
}

+ (NSDictionary *)dictionary;
+ (NSDictionary *)dictionaryWithObject:(id)value forKey:(id)key;

- (id)initWithObject:(id)value forKey:(id)key;

- (NSUInteger)count;
- (id)objectForKey:(id)key;
/* Cocoa's subscript: `dict[k]` lowers to this. */
- (id)objectForKeyedSubscript:(id)key;

- (BOOL)isEqualToDictionary:(NSDictionary *)other;

@end

@interface NSMutableDictionary : NSDictionary <NSMutableCopying>

+ (NSMutableDictionary *)dictionary;

- (void)setObject:(id)value forKey:(id)key;
- (void)removeObjectForKey:(id)key;
- (void)removeAllObjects;

/* `dict[k] = v` lowers to this, and `dict[k] = nil` REMOVES the key (Cocoa's
 * rule) — which is why it cannot simply forward to -setObject:forKey:. */
- (void)setObject:(id)object forKeyedSubscript:(id)key;

@end

#endif /* FOUNDATION_NSDICTIONARY_H */
