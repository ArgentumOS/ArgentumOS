/*
 * NSDictionary / NSMutableDictionary — key → value.
 * docs/design/foundation-plan.md, F3.
 *
 * KEYS ARE COPIED, VALUES ARE RETAINED. That asymmetry is Cocoa's, and it is not
 * a detail: a key is a lookup TOKEN, so if the caller hands over a mutable string
 * and then edits it, a table that merely retained the key would keep the token
 * that no longer hashes where it was filed and the value would become
 * unreachable — a slow, silent bug. Copying the key freezes its hash and its
 * equality. Values are the caller's objects, held by reference like an array's
 * elements.
 *
 * A key's equality and hash are NSObject's contract (`-isEqual:`/`-hash`), so
 * anything can be a key as long as it implements those two — and `-copy`.
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

- (unsigned long)count;
- (id)objectForKey:(id)key;
- (BOOL)isEqualToDictionary:(NSDictionary *)other;

@end

@interface NSMutableDictionary : NSDictionary

+ (NSMutableDictionary *)dictionary;

- (void)setObject:(id)value forKey:(id)key;
- (void)removeObjectForKey:(id)key;
- (void)removeAllObjects;

@end

#endif /* FOUNDATION_NSDICTIONARY_H */
