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
/* FOR `NSCoding` AND THE `NSCoder` ITS TWO DOORS TAKE (§63.13): this collection conforms on Apple's platform,
 * so a class that declares the protocol here is one whose `-conformsToProtocol:` answers the same. */
#import <Foundation/NSCoding.h>
/* FOR `NSEnumerationOptions`, WHICH THE OPTIONS DOORS BELOW TAKE: the option type is published by NSIndexSet
 * (its own enumerators use it first), and that header imports only NSObject.h, so there is no cycle. */
#import <Foundation/NSIndexSet.h>

@class NSArray<ObjectType>, NSEnumerator<ObjectType>;
@class NSString, NSNumber, NSDate, NSError, NSURL;

/* NULLABILITY (F6): NONNULL by default. -objectForKey: and its subscript answer
 * nil for a key that is not there, and -setObject:forKeyedSubscript: takes nil
 * because `dict[k] = nil` REMOVES the key (Cocoa's rule). */
NS_ASSUME_NONNULL_BEGIN

struct FNDictEntry;			/* opaque; defined in NSDictionary.m */

/* ===================================================================================================
 * NSDICTIONARY IS A CLASS CLUSTER (2026-09-29; docs/design/foundation-clusters-plan.md §C.3).
 *
 * THE PRIMITIVE METHODS ARE -count, -objectForKey: AND -keyEnumerator, and they are what the EMPTY
 * concrete class implements: AGDictionaryEmpty answers those three over no storage at all, and every
 * derived read this family publishes (-allKeys, -allValues, -hash, -description, -isEqualToDictionary:,
 * -getObjects:andKeys:, fast enumeration) is correct for it because of them.
 *
 * A SUBCLASS THAT OVERRIDES THOSE THREE GETS THE WHOLE FAMILY. -allKeys, -allValues, -allKeysForObject:,
 * -objectsForKeys:notFoundMarker:, -getObjects:andKeys:, -isEqualToDictionary:, -hash, -description,
 * -enumerateKeysAndObjectsUsingBlock: and fast enumeration are all written OVER them, so a concrete class
 * with a DIFFERENT LAYOUT - the empty one has no storage at all - is correct through every door. The probe
 * proves that with a subclass of its own that implements the three primitives and nothing else.
 *
 * THE FRONT IS PUBLIC AND ITS CONCRETE CLASSES ARE PRIVATE: AGDictionaryEmpty (ONE shared instance, the
 * empty case, and the answer to `[[NSDictionary alloc] init]`), AGDictionaryItems (the general case, over
 * these ivars) and AGDictionaryMutable (the mutable family's, since a mutable constructor must answer a
 * mutable concrete class). `-class` answers one of them; -classForCoder answers the PUBLIC class, so no
 * private name can reach an archive.
 * =================================================================================================== */

@interface NSDictionary<__covariant KeyType, __covariant ObjectType> : NSObject <NSCopying, NSFastEnumeration, NSCoding>
{
	struct FNDictEntry **_buckets;	/* power-of-two count, so index = hash & (count-1) */
	unsigned long _bucketCount;
	unsigned long _count;
	unsigned long _mutations;	/* bumped by every mutation, for fast enumeration */
	id __unsafe_unretained *_keys;	/* built lazily for enumeration; dropped on mutation */
	unsigned long _keyCount;
}

/* THE NSCoding DOORS (§63.13), AND THIS FAMILY CARRIES *TWO* PAYLOADS — which is why it is NOT the array's and
 * the sets' substitution: `NS.keys` (the key names) is paired POSITIONALLY with `NS.objects` (the values), so
 * the encoder writes both arrays and the decoder pairs them on the way back. The second key name was already
 * waiting in the shared wire (FNKeyedWire.h) for exactly this.
 *
 * THE ARCHIVER NEVER CALLS EITHER: its structural branch recognises a dictionary by KIND, and it writes the
 * same two KEYS with a different PAYLOAD SHAPE — an inline array of slots rather than a second array entry.
 * The names are shared so the two spellings cannot drift; the shapes are each path's own, and neither path
 * reads the other's. They exist for a caller who names them, and for `-conformsToProtocol:`. */
- (nullable instancetype)initWithCoder:(NSCoder *)coder;
- (void)encodeWithCoder:(NSCoder *)coder;

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

/* THE TWO-ARRAY AND COPY-ITEMS CONSTRUCTORS (F3 audit), and the COUNTED buffer form. */
- (id)initWithObjects:(NSArray<ObjectType> *)objects forKeys:(NSArray<KeyType> *)keys;
- (id)initWithDictionary:(NSDictionary<KeyType, ObjectType> *)other copyItems:(BOOL)flag;

- (void)getObjects:(ObjectType __unsafe_unretained _Nonnull * _Nonnull)objects
	   andKeys:(KeyType __unsafe_unretained _Nonnull * _Nonnull)keys
	     count:(NSUInteger)count;

/* THE OPTIONS-ENUMERATION AND ENTRY-FILTER DOORS (F3 audit): the reverse walk, the stop flag, the
 * predicate filter and the options sort. */
- (void)enumerateKeysAndObjectsWithOptions:(NSEnumerationOptions)opts
				usingBlock:(void (^)(KeyType key, ObjectType obj, BOOL *stop))block;
- (NSArray<KeyType> *)keysOfEntriesPassingTest:(BOOL (^)(KeyType key, ObjectType obj, BOOL *stop))predicate;
- (NSArray<KeyType> *)keysOfEntriesWithOptions:(NSEnumerationOptions)opts
				    passingTest:(BOOL (^)(KeyType key, ObjectType obj, BOOL *stop))predicate;
- (NSArray<KeyType> *)keysSortedByValueWithOptions:(NSSortOptions)opts
				   usingComparator:(NSComparator)comparator;

/* THE URL/ERROR DOORS (F3 audit): the plist forms with an NSError out-parameter, beside the path forms the
 * skin already publishes. Declared here, implemented here, so the ledger's owner (NSDictionary) matches.
 *
 * ⚠ AND THE GENERIC PARAMETERS ARE APPLE'S EXACT SPELLING — READ OFF THE SDK, NOT REASONED. The sweep's own
 * source URL (tools/foundation-sweep.py:932) fetches the iPhoneOS16.5 SDK's Foundation header, which declares
 * `NSDictionary<NSString *, ObjectType>` for BOTH of these doors: KEYTYPE IS REPLACED BY THE CONCRETE
 * `NSString *`, because a plist read from a URL always has string keys, while the VALUE parameter stays
 * ObjectType. The deprecated non-error spellings beside them keep KeyType, ObjectType.
 *
 * ⚠ AND THE FIRST VERSION OF THESE DOORS COPIED THE CLASS'S PAIR — A DEVIATION FROM APPLE — while my first
 * repair annotated them with the single parameter the tool's per-METHOD parameter list shows, and that is NOT
 * WRITABLE: measured, a method cannot introduce a type parameter ("error: expected a type") and a subset of the
 * class's own is not expressible either ("too few type arguments"). The tool records the parameter NAMES a
 * declaration uses; the declaration itself must spell the WHOLE list, concretely where Apple fixed one.
 * THIS SPELLING IS THE ONE THAT BOTH MATCHES APPLE AND COMPILES. */
+ (nullable NSDictionary<NSString *, ObjectType> *)dictionaryWithContentsOfURL:(NSURL *)url
								     error:(NSError * _Nullable * _Nullable)error;
- (nullable NSDictionary<NSString *, ObjectType> *)initWithContentsOfURL:(NSURL *)url error:(NSError * _Nullable * _Nullable)error;
- (BOOL)writeToURL:(NSURL *)url error:(NSError * _Nullable * _Nullable)error;

- (BOOL)isEqualToDictionary:(NSDictionary<KeyType, ObjectType> *)other;


/* §63.209: THE LOCALE DOORS. The reading is stated once: this tree renders property lists in one canonical
 * way, so the locale is ACCEPTED and does not alter the rendering — which is what Apple's own formatter does
 * for a POSIX locale — and the indent form is the one place indentation is expressed. */
- (NSString *)descriptionWithLocale:(nullable id)locale;
- (NSString *)descriptionWithLocale:(nullable id)locale indent:(NSUInteger)level;
@end

@interface NSMutableDictionary<KeyType, ObjectType> : NSDictionary<KeyType, ObjectType> <NSMutableCopying>

+ (NSMutableDictionary *)dictionary;
+ (NSMutableDictionary *)dictionaryWithCapacity:(NSUInteger)capacity;
- (id)initWithCapacity:(NSUInteger)capacity;
/* APPLE DECLARES THIS ON THE MUTABLE CLASS TOO (§63.13), so it is redeclared here rather than left to
 * inheritance — the ledger's shipped test is a declaration in the owner's own block — and its implementation
 * lives in this class's own `@implementation`, because `--unimplemented` counts an implementation in the class
 * or a SUBCLASS, so the front's body does not satisfy this. */
- (nullable instancetype)initWithCoder:(NSCoder *)coder;

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

/* ===================================================================================================
 * NSFileAttributes (F3 audit): the accessors that read ONE key of a file-attributes dictionary and convert
 * it. Cocoa publishes these as a category on NSDictionary, so they are one here too — the keys are
 * NSFileManager.h's, and the VALUES are the shapes -[NSFileManager attributesOfItemAtPath:error:] hands out.
 * =================================================================================================== */
NS_ASSUME_NONNULL_BEGIN
/* ================== THE SHARED-KEY-SET PAIR (§63.78) ==================
 * APPLE DECLARES NO `NSSharedKeySet` CLASS — measured in the macOS and iOS headers AND in Apple's own
 * documentation index (§63.77) — and what the two doors traffic in is AN OPAQUE `id`, whose only stated
 * contract is Apple's own: `+dictionaryWithSharedKeySet:` throws "If keyset is not an object returned by
 * +sharedKeySetForKeys:". **SO THE TOKEN IS A CLASS OF OURS, PRIVATE, AND UNNAMED IN ANY PUBLIC HEADER** —
 * exactly as Apple's is invisible.
 * ⚠ AND THE OPTIMISATION IS A STATED DEVIATION (§11.6): Apple's dictionary is "optimized for dealing with a
 * known set of keys". THIS ONE IS AN ORDINARY NSMutableDictionary WHOSE CAPACITY IS RESERVED FROM THE KEY SET,
 * which is BEHAVIOURALLY IDENTICAL — Apple's own page promises nothing else, and says outright that "keys that
 * are not in the key set can still be set into the dictionary, but that usage is not optimal". A performance
 * property with no observable behaviour is the one kind of gap a caller cannot trip over, and it is stated here
 * rather than left to be discovered. */

@interface NSDictionary<KeyType, ObjectType> (NSSharedKeySetDictionary)
+ (id)sharedKeySetForKeys:(NSArray<KeyType <NSCopying>> *)keys;
@end

@interface NSMutableDictionary<KeyType, ObjectType> (NSSharedKeySetDictionary)
+ (NSMutableDictionary<KeyType, ObjectType> *)dictionaryWithSharedKeySet:(id)keyset;
@end

@interface NSDictionary (NSFileAttributes)
- (nullable NSDate *)fileCreationDate;
- (BOOL)fileExtensionHidden;
- (nullable NSNumber *)fileGroupOwnerAccountID;
- (nullable NSString *)fileGroupOwnerAccountName;
- (unsigned int)fileHFSCreatorCode;
- (unsigned int)fileHFSTypeCode;
- (BOOL)fileIsAppendOnly;
- (BOOL)fileIsImmutable;
- (nullable NSDate *)fileModificationDate;
- (nullable NSNumber *)fileOwnerAccountID;
- (nullable NSString *)fileOwnerAccountName;
- (NSUInteger)filePosixPermissions;
- (unsigned long long)fileSize;
- (NSInteger)fileSystemFileNumber;
- (NSInteger)fileSystemNumber;
- (nullable NSString *)fileType;
@end
NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSDICTIONARY_H */
