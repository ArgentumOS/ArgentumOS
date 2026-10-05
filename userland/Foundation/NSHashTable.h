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

@class NSArray<ObjectType>;
@class NSSet<ObjectType>;
@class NSEnumerator<ObjectType>;
@class FNPointerTable;

NS_ASSUME_NONNULL_BEGIN

/* Apple's options for a hash table ARE NSPointerFunctions' options, under a name of its own — the same word,
 * spelled the same way, because `NSHashTableOptions` says WHICH set of options it is. */
typedef NSPointerFunctionsOptions NSHashTableOptions;

/* ===================================================================================================
 * NSHASHTABLE AND §C.3 (2026-09-29, M7). This family is a FRONT rather than a cluster of private concrete
 * classes — the same shape NSHashTable's neighbours have here — and it has ONE private subclass, FNLegacyHashTable,
 * which the legacy C API builds. What the contract asks of it, and what it therefore has:
 *
 *     -count      -member:      -objectEnumerator:
 *
 * A class answering those three is correct through -anyObject, -allObjects, -containsObject:,
 * THE MUTATORS ARE NOT OVER THEM, and that is a boundary rather than an omission: this family is MUTABLE,
 * so its mutators ARE the storage implementation, exactly as the array family's mutable class is.
 * -classForCoder answers NSHashTable, so the legacy subclass's name never reaches an archive.
 * =================================================================================================== */

@interface NSHashTable<ObjectType> : NSObject <NSCopying, NSFastEnumeration, NSSecureCoding>
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

+ (NSHashTable<ObjectType> *)weakObjectsHashTable;
+ (NSHashTable<ObjectType> *)hashTableWithOptions:(NSHashTableOptions)options;

- (NSPointerFunctions *)pointerFunctions;

- (NSUInteger)count;
- (nullable ObjectType)anyObject;
- (NSArray<ObjectType> *)allObjects;
- (NSSet<ObjectType> *)setRepresentation;
- (BOOL)containsObject:(nullable ObjectType)anObject;
- (nullable ObjectType)member:(nullable ObjectType)object;
- (NSEnumerator<ObjectType> *)objectEnumerator;

- (void)addObject:(nullable ObjectType)object;
- (void)removeObject:(nullable ObjectType)object;
- (void)removeAllObjects;

/* The set operations, each in place on the receiver. */
- (void)intersectHashTable:(NSHashTable<ObjectType> *)other;
- (BOOL)intersectsHashTable:(NSHashTable<ObjectType> *)other;
- (BOOL)isSubsetOfHashTable:(NSHashTable<ObjectType> *)other;
- (BOOL)isEqualToHashTable:(NSHashTable<ObjectType> *)other;
- (void)minusHashTable:(NSHashTable<ObjectType> *)other;
- (void)unionHashTable:(NSHashTable<ObjectType> *)other;

@end

NS_ASSUME_NONNULL_END


/* ===================================================================================================
 * THE LEGACY C API (§62.45), THE SAME DESIGN AS NSMapTable'S AND FOR THE SAME REASON
 *
 * These are the pre-10.5 functions and the call-back STRUCT Apple deprecated when `NSPointerFunctions` arrived.
 * Apple's call-backs are promised THE TABLE — `unsigned (*hash)(NSHashTable *table, const void *pointer)` — and a
 * C function pointer cannot close over one, so the engine's own function pointers (which take no table) cannot be
 * what backs them. The implementation is a private subclass that carries the call-backs and hands itself to every
 * one of them; see NSMapTable.h's note for the full argument.
 *
 * TWO OF THE TWENTY-EIGHT NAMES ARE NOT HERE, AND THE GROUND IS THE LIBRARY'S OWN: `NSCreateHashTableWithZone` and
 * `NSCopyHashTableWithZone` take an `NSZone`, and `NSObjCRuntime.h` records the user-driven sequence that removed
 * every zone-taking method, then every zone-returning one, and finally the type itself. A header cannot spell a
 * type this library removed on purpose, so those two rows stay owed; the other twenty-six land.
 *
 * FIELD ORDER AND THE SENTINEL ARE OURS UNDER §11.6.1 D2.
 * =================================================================================================== */

NS_ASSUME_NONNULL_BEGIN

typedef struct {
	unsigned (*hash)(NSHashTable *table, const void *pointer);
	BOOL (*isEqual)(NSHashTable *table, const void *pointer1, const void *pointer2);
	void (*retain)(NSHashTable *table, const void *pointer);
	void (*release)(NSHashTable *table, const void *pointer);
	NSString * _Nullable (* _Nullable describe)(NSHashTable *table, const void *pointer);
	const void *notAKeyMarker;
} NSHashTableCallBacks;

/* THE ENUMERATOR IS A VALUE THE CALLER HOLDS, as the map table's is. */
typedef struct {
	NSHashTable *table;
	NSUInteger index;
	NSArray *objects;
} NSHashEnumerator;

/* THE EIGHT PRE-BUILT SETS, and the legacy OPTION constant that came with them: `NSHashTableZeroingWeakMemory` was
 * a flag passed where the modern API takes `NSHashTableOptions`, and its value is ours (D2) — declared rather than
 * interpreted, because nothing here can honour a zeroing weak table that the modern options do not describe.
 *
 * THE FOUR NAMES BESIDE IT ARE MACROS, as Apple declares them and for the same reason NSMapTable.h gives: each is
 * the NSPointerFunctions option it names, so `+hashTableWithOptions:` already understands every one of them. */
#define NSHashTableStrongMemory			NSPointerFunctionsStrongMemory
#define NSHashTableCopyIn			NSPointerFunctionsCopyIn
#define NSHashTableObjectPointerPersonality	NSPointerFunctionsObjectPointerPersonality
#define NSHashTableWeakMemory			NSPointerFunctionsWeakMemory
extern const NSHashTableCallBacks NSIntHashCallBacks;
extern const NSHashTableCallBacks NSIntegerHashCallBacks;
extern const NSHashTableCallBacks NSNonOwnedPointerHashCallBacks;
extern const NSHashTableCallBacks NSNonRetainedObjectHashCallBacks;
extern const NSHashTableCallBacks NSObjectHashCallBacks;
extern const NSHashTableCallBacks NSOwnedObjectIdentityHashCallBacks;
extern const NSHashTableCallBacks NSOwnedPointerHashCallBacks;
extern const NSHashTableCallBacks NSPointerToStructHashCallBacks;
extern const NSUInteger NSHashTableZeroingWeakMemory;

NSHashTable *NSCreateHashTable(NSHashTableCallBacks callBacks, NSUInteger capacity);
void NSFreeHashTable(NSHashTable *table);
void NSResetHashTable(NSHashTable *table);
BOOL NSCompareHashTables(NSHashTable *table1, NSHashTable *table2);
void *NSHashGet(NSHashTable *table, const void *pointer);
void NSHashInsert(NSHashTable *table, const void *pointer);
void NSHashInsertKnownAbsent(NSHashTable *table, const void *pointer);
/* NULL IS AN ANSWER, NOT AN ERROR: the function returns the object that was ALREADY present, or NULL when it
 * inserted a new one (Apple's contract). The return was declared non-null, which made the honest NULL a lie the
 * compiler reported. */
void * _Nullable NSHashInsertIfAbsent(NSHashTable *table, const void *pointer);
void NSHashRemove(NSHashTable *table, const void *pointer);
NSUInteger NSCountHashTable(NSHashTable *table);
NSString *NSStringFromHashTable(NSHashTable *table);
NSArray *NSAllHashTableObjects(NSHashTable *table);
NSHashEnumerator NSEnumerateHashTable(NSHashTable *table);
/* NULL IS THE END OF THE ENUMERATION (Apple's contract), not an error: the return was
 * declared non-null and the honest NULL was reported as a lie. */
void * _Nullable NSNextHashEnumeratorItem(NSHashEnumerator *enumerator);
void NSEndHashTableEnumeration(NSHashEnumerator *enumerator);

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSHASHTABLE_H */
