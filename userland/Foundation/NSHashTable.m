/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSHashTable.m — W13b. See the header for what it is and for the NULL limit.
 *
 * THE TABLE DOES THE WORK; this file is the API over it. Two things are worth pointing out because they are
 * decisions rather than mechanics: the ENUMERATION SNAPSHOT (fast enumeration must hand back a compact C
 * array, and a hash table is full of gaps, so a compacted copy is kept and rebuilt only when the table has
 * changed), and the SET OPERATIONS, which are written against the other table's SLOTS rather than against a
 * snapshot, so no intermediate collection is built.
 *
 * MANUAL OWNERSHIP (MRC: the whole library is).
 */

#import <Foundation/NSHashTable.h>
#import <Foundation/NSValue.h>
/* NSMutableString and NSMutableArray live in their immutable classes headers in this tree - the gate refuses an
 * import that resolves to no header, and it has now caught this same phantom twice. */
#import <Foundation/NSSet.h>
#include <stdlib.h>
#include <string.h>
#import <Foundation/FNPointerTable.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSSet.h>
#import <Foundation/NSEnumerator.h>
#import <Foundation/NSException.h>
#import <Foundation/NSString.h>
#import <Foundation/NSCoder.h>
#include <Foundation/NSNull.h>

#include <stdlib.h>

@implementation NSHashTable

/* §C.3 item 4: an archiver asks for THIS, never for -class - and here it is UNCONDITIONAL, unlike the number
 * family's. NSHashTable's one subclass is PRIVATE (FNLegacyHashTable, which the legacy C API builds), so no
 * public class's name can be rewritten by answering the front for every receiver. */
- (Class)classForCoder
{
	return [NSHashTable class];
}

- (instancetype)initWithOptions:(NSHashTableOptions)options capacity:(NSUInteger)initialCapacity
{
	return [self initWithPointerFunctions:
		[[[NSPointerFunctions alloc] initWithOptions:options] autorelease]
		capacity:initialCapacity];
}

- (instancetype)initWithPointerFunctions:(NSPointerFunctions *)functions
				capacity:(NSUInteger)initialCapacity
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_table = [[FNPointerTable alloc] initWithKeyFunctions:functions
					     valueFunctions:nil
						   capacity:initialCapacity];
	_mutations = 1;
	return self;
}

+ (instancetype)weakObjectsHashTable
{
	/* NOT OWNED, AND NOT ZEROED — the two options say both halves of that, and the header of
	 * NSPointerFunctions says what "not zeroed" costs. */
	return [[[self alloc] initWithOptions:
		 (NSPointerFunctionsObjectPointerPersonality | NSPointerFunctionsWeakMemory)
		capacity:0] autorelease];
}

+ (instancetype)hashTableWithOptions:(NSHashTableOptions)options
{
	return [[[self alloc] initWithOptions:options capacity:0] autorelease];
}

- (void)dealloc
{
	[_table release];
	free(_snapshot);
	[super dealloc];
}

- (NSPointerFunctions *)pointerFunctions
{
	return [_table keyFunctions];
}

- (NSUInteger)count
{
	return [_table count];
}

- (id)anyObject
{
	/* OVER THE PRIMITIVES (§C.3 item 5): any member, from the enumerator. */
	return [[self objectEnumerator] nextObject];
}

- (NSArray *)allObjects
{
	NSMutableArray *objects = [NSMutableArray arrayWithCapacity:[self count]];
	NSEnumerator *enumerator = [self objectEnumerator];
	id object;

	while ((object = [enumerator nextObject]) != nil) {
		[objects addObject:object];
	}
	return objects;
}

- (NSSet *)setRepresentation
{
	return [NSSet setWithArray:[self allObjects]];
}

- (BOOL)containsObject:(id)anObject
{
	return [self member:anObject] != nil;
}

- (id)member:(id)object
{
	return object != nil ? (id)[_table fnMember:(void *)object] : nil;
}

- (NSEnumerator *)objectEnumerator
{
	/*
	 * THE PRIMITIVE (§C.3 item 5), AND IT MUST NOT GO THROUGH -allObjects: that method is written OVER this
	 * one, so asking it here was MUTUAL RECURSION - and this is the SECOND time this session that trap has
	 * fired (the dictionary family's -keyEnumerator had the same shape). A primitive reads the class's own
	 * storage, which is what this does; the enumerator then holds the SNAPSHOT, so mutating while enumerating
	 * changes the TABLE but not the walk, which is the behaviour to state.
	 */
	NSMutableArray *snapshot = [NSMutableArray arrayWithCapacity:[self count]];
	NSUInteger i;
	NSUInteger slots = [_table slotCount];

	for (i = 0; i < slots; i++) {
		if ([_table fnStateAtSlot:i] == FN_SLOT_FULL) {
			[snapshot addObject:(id)[_table fnKeyAtSlot:i]];
		}
	}
	return [snapshot objectEnumerator];
}

- (void)addObject:(id)object
{
	if (object == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSHashTable: nil is not a member (and a NULL pointer cannot be one)"];
	}
	(void)[_table fnAddKey:(void *)object];
	_mutations++;
}

- (void)removeObject:(id)object
{
	if (object != nil) {
		[_table fnRemoveKey:(void *)object];
		_mutations++;
	}
}

- (void)removeAllObjects
{
	[_table fnRemoveAll];
	_mutations++;
}

/* ---- the set operations ---- */

- (void)intersectHashTable:(NSHashTable *)other
{
	NSArray *mine = [self allObjects];
	NSUInteger i;

	for (i = 0; i < [mine count]; i++) {
		id member = [mine objectAtIndex:i];

		if (![other containsObject:member]) {
			[self removeObject:member];
		}
	}
}

- (BOOL)intersectsHashTable:(NSHashTable *)other
{
	NSArray *mine = [self allObjects];
	NSUInteger i;

	for (i = 0; i < [mine count]; i++) {
		if ([other containsObject:[mine objectAtIndex:i]]) {
			return YES;
		}
	}
	return NO;
}

- (BOOL)isSubsetOfHashTable:(NSHashTable *)other
{
	NSArray *mine = [self allObjects];
	NSUInteger i;

	for (i = 0; i < [mine count]; i++) {
		if (![other containsObject:[mine objectAtIndex:i]]) {
			return NO;
		}
	}
	return YES;
}

- (BOOL)isEqualToHashTable:(NSHashTable *)other
{
	return other != nil && [self count] == [other count] && [self isSubsetOfHashTable:other];
}

- (void)minusHashTable:(NSHashTable *)other
{
	NSArray *mine = [self allObjects];
	NSUInteger i;

	for (i = 0; i < [mine count]; i++) {
		id member = [mine objectAtIndex:i];

		if ([other containsObject:member]) {
			[self removeObject:member];
		}
	}
}

- (void)unionHashTable:(NSHashTable *)other
{
	/* THE OTHER TABLE'S SLOTS ARE READ DIRECTLY — an object may read the ivars of another instance of its own
	 * class — so this builds no intermediate collection at all. */
	NSUInteger i;
	NSUInteger slots = [other->_table slotCount];

	for (i = 0; i < slots; i++) {
		if ([other->_table fnStateAtSlot:i] == FN_SLOT_FULL) {
			(void)[_table fnAddKey:(void *)[other->_table fnKeyAtSlot:i]];
		}
	}
	_mutations++;
}

/* ---- the enumerator's half ---- */

- (id)copy
{
	NSHashTable *copy = [[[self class] alloc] initWithPointerFunctions:[self pointerFunctions]
								  capacity:[self count]];
	NSUInteger i;
	NSUInteger slots = [_table slotCount];

	for (i = 0; i < slots; i++) {
		if ([_table fnStateAtSlot:i] == FN_SLOT_FULL) {
			(void)[copy->_table fnAddKey:(void *)[_table fnKeyAtSlot:i]];
		}
	}
	return copy;
}

- (NSUInteger)countByEnumeratingWithState:(NSFastEnumerationState *)state
				  objects:(id *)buffer
				    count:(NSUInteger)length
{
	unsigned long cursor = state->state;
	unsigned long produced = 0;
	unsigned long skip;
	NSEnumerator *enumerator = [self objectEnumerator];
	id object;

	/*
	 * OVER THE PRIMITIVES, THROUGH THE CALLER'S BUFFER (§C.3 item 5). The snapshot machinery this replaces
	 * existed to be SAFE UNDER MUTATION - and it still is, for the same reason: -objectEnumerator hands out a
	 * snapshot of its own, so a mutation during the walk changes the table and not the walk. What it no longer
	 * does is cache that snapshot across calls, which is a cost the contract is worth.
	 */
	for (skip = 0; skip < cursor && (object = [enumerator nextObject]) != nil; skip++) {
	}
	while (produced < length && (object = [enumerator nextObject]) != nil) {
		buffer[produced++] = object;
	}
	state->mutationsPtr = &_mutations;
	if (produced == 0) {
		return 0;
	}
	state->itemsPtr = buffer;
	state->state = cursor + produced;
	return produced;
}

+ (BOOL)supportsSecureCoding
{
	return YES;
}

- (void)encodeWithCoder:(NSCoder *)coder
{
	[coder encodeObject:[self allObjects] forKey:@"NS.objects"];
}

- (instancetype)initWithCoder:(NSCoder *)coder
{
	self = [self initWithOptions:NSPointerFunctionsObjectPointerPersonality capacity:0];
	if (self != nil) {
		NSArray *objects = [coder decodeObjectForKey:@"NS.objects"];
		NSUInteger i;

		for (i = 0; i < [objects count]; i++) {
			(void)[_table fnAddKey:(void *)[objects objectAtIndex:i]];
		}
	}
	return self;
}

@end

/* ===================================================================================================
 * THE LEGACY C API (§62.45): a subclass that WRAPS THE SHARED SCAN. NSMapTable's legacy table carries the
 * call-backs and hands itself to them; a hash table is the same thing WITH NO VALUES, so it stores the element as
 * the key and passes no value call-backs at all — one scan, one ownership rule, and no second implementation of
 * "is this pointer here" that could disagree with the first.
 * =================================================================================================== */

/* ITS OWN SCAN, AND THE REASON IS A TYPE RATHER THAN A PREFERENCE: Apple's hash-table call-backs take an
 * `NSHashTable *` while the map table's take an `NSMapTable *`, so a caller's function pointer CANNOT be handed to
 * the other class's table — the two APIs are different types by design. THE RULE IS THE SAME ONE (a linear scan that
 * consults `isEqual` and never the hash); the storage is this class's, and the shared scan was tried first and
 * failed to compile, which is how the type difference announced itself. */
@interface FNLegacyHashTable : NSHashTable
{
	NSHashTableCallBacks _callBacks;
	void **_members;
	NSUInteger _memberCount;
	NSUInteger _memberCapacity;
	BOOL _membersAreObjects;
}
- (instancetype)fnInitWithCallBacks:(NSHashTableCallBacks)callBacks capacity:(NSUInteger)capacity;
- (NSUInteger)fnIndexOfMember:(const void *)pointer;
- (void *)fnMemberAtIndex:(NSUInteger)index;
@end

@implementation FNLegacyHashTable

- (instancetype)fnInitWithCallBacks:(NSHashTableCallBacks)callBacks capacity:(NSUInteger)capacity
{
	self = [super init];
	if (self != nil) {
		_callBacks = callBacks;
		/* WHETHER A MEMBER IS AN OBJECT IS A FACT ABOUT THE PERSONALITY, and the eight pre-built sets are this
		 * library's own, so the three OBJECT ones are recognised by the function pointers they were built from. A
		 * caller's own call-back set is treated as POINTERS — a stated boundary, and the reason `NSAllHashTableObjects`
		 * wraps: a bare pointer cannot be an element of an `NSArray` without becoming one. */
		_membersAreObjects =
			(callBacks.hash == NSObjectHashCallBacks.hash &&
			 callBacks.isEqual == NSObjectHashCallBacks.isEqual) ||
			(callBacks.hash == NSNonRetainedObjectHashCallBacks.hash &&
			 callBacks.isEqual == NSNonRetainedObjectHashCallBacks.isEqual) ||
			(callBacks.hash == NSOwnedObjectIdentityHashCallBacks.hash &&
			 callBacks.isEqual == NSOwnedObjectIdentityHashCallBacks.isEqual);
		_memberCapacity = capacity > 0 ? capacity : 4;
		_members = (void **)calloc(_memberCapacity, sizeof(void *));
		if (_members == NULL) {
			[self release];
			return nil;
		}
	}
	return self;
}

- (NSUInteger)fnIndexOfMember:(const void *)pointer
{
	NSUInteger i;

	for (i = 0; i < _memberCount; i++) {
		if (_callBacks.isEqual != NULL) {
			if (_callBacks.isEqual(self, _members[i], pointer)) {
				return i;
			}
		} else if (_members[i] == pointer) {
			return i;
		}
	}
	return NSNotFound;
}

- (void *)fnMemberAtIndex:(NSUInteger)index
{
	return index < _memberCount ? _members[index] : NULL;
}

- (NSUInteger)count { return _memberCount; }

- (id)anyObject
{
	return _memberCount > 0 ? (id)_members[0] : nil;
}

- (NSArray *)allObjects
{
	NSMutableArray *out = [[NSMutableArray alloc] init];
	NSUInteger i;

	for (i = 0; i < _memberCount; i++) {
		[out addObject:_membersAreObjects ? (id)_members[i] : (id)[NSValue valueWithPointer:_members[i]]];
	}
	return [out autorelease];
}

- (NSSet *)setRepresentation
{
	return [NSSet setWithArray:[self allObjects]];
}

- (BOOL)containsObject:(id)anObject
{
	return [self fnIndexOfMember:(const void *)anObject] != NSNotFound;
}

- (id)member:(id)object
{
	NSUInteger index = [self fnIndexOfMember:(const void *)object];

	return index == NSNotFound ? nil : (id)_members[index];
}

- (NSEnumerator *)objectEnumerator
{
	return [[self allObjects] objectEnumerator];
}

/* `-addObject:` LEAVES AN EQUAL MEMBER ALONE, which is Apple's own rule for this class. */
- (void)addObject:(id)object
{
	if ([self containsObject:object]) {
		return;
	}
	if (_memberCount == _memberCapacity) {
		_memberCapacity *= 2;
		_members = (void **)realloc(_members, sizeof(void *) * _memberCapacity);
	}
	if (_callBacks.retain != NULL) {
		_callBacks.retain(self, (const void *)object);
	}
	_members[_memberCount++] = (void *)object;
}

- (void)removeObject:(id)object
{
	NSUInteger index = [self fnIndexOfMember:(const void *)object];

	if (index == NSNotFound) {
		return;
	}
	if (_callBacks.release != NULL) {
		_callBacks.release(self, _members[index]);
	}
	_memberCount--;
	if (index < _memberCount) {
		memmove(&_members[index], &_members[index + 1], sizeof(void *) * (_memberCount - index));
	}
}

- (void)removeAllObjects
{
	NSUInteger i;

	for (i = 0; i < _memberCount; i++) {
		if (_callBacks.release != NULL) {
			_callBacks.release(self, _members[i]);
		}
	}
	_memberCount = 0;
}

/* THE FOUR SET-RELATION DOORS ARE OVERRIDDEN RATHER THAN INHERITED, and that is not tidiness: the parent would
 * answer from its OWN (empty) storage, which is a wrong answer rather than a missing one. */
- (void)intersectHashTable:(NSHashTable *)other
{
	NSUInteger i;

	/* IT WALKS THE STORAGE AND NOT `-allObjects`, WHICH IS THE BUG THE FIRST VERSION HAD: that array wraps a
	 * non-object member in an `NSValue`, and a wrapper is not the member it wraps, so every relation answered NO
	 * — a table compared with ITSELF included. */
	for (i = _memberCount; i > 0; i--) {
		if (![other containsObject:(id)_members[i - 1]]) {
			[self removeObject:(id)_members[i - 1]];
		}
	}
}

- (BOOL)intersectsHashTable:(NSHashTable *)other
{
	NSUInteger i;

	for (i = 0; i < _memberCount; i++) {
		if ([other containsObject:(id)_members[i]]) {
			return YES;
		}
	}
	return NO;
}

- (BOOL)isSubsetOfHashTable:(NSHashTable *)other
{
	NSUInteger i;

	for (i = 0; i < _memberCount; i++) {
		if (![other containsObject:(id)_members[i]]) {
			return NO;
		}
	}
	return YES;
}

- (BOOL)isEqualToHashTable:(NSHashTable *)other
{
	return [self count] == [other count] && [self isSubsetOfHashTable:other];
}

- (id)copy
{
	FNLegacyHashTable *copy = [[FNLegacyHashTable alloc] fnInitWithCallBacks:_callBacks
								       capacity:[self count]];
	NSUInteger i;

	for (i = 0; i < _memberCount; i++) {
		[copy addObject:(id)_members[i]];
	}
	return copy;
}

- (void)dealloc
{
	[self removeAllObjects];
	free(_members);
	[super dealloc];
}

@end

/* --- THE EIGHT PRE-BUILT SETS, AND THE LEGACY OPTION ------------------------------------------------ */

static unsigned fn_ptr_hash(NSHashTable *table, const void *pointer)
{
	(void)table;
	return (unsigned)((uintptr_t)pointer >> 2);
}

static BOOL fn_ptr_equal(NSHashTable *table, const void *a, const void *b)
{
	(void)table;
	return a == b;
}

static NSString *fn_ptr_describe(NSHashTable *table, const void *pointer)
{
	(void)table;
	return [NSString stringWithFormat:@"%p", pointer];
}

static unsigned fn_object_hash(NSHashTable *table, const void *pointer)
{
	(void)table;
	return (unsigned)[(id)pointer hash];
}

static BOOL fn_object_equal(NSHashTable *table, const void *a, const void *b)
{
	(void)table;
	return [(id)a isEqual:(id)b];
}

static void fn_object_retain(NSHashTable *table, const void *pointer)
{
	(void)table;
	[(id)pointer retain];
}

static void fn_object_release(NSHashTable *table, const void *pointer)
{
	(void)table;
	[(id)pointer release];
}

static NSString *fn_object_describe(NSHashTable *table, const void *pointer)
{
	(void)table;
	return [(id)pointer description];
}

static unsigned fn_int_hash(NSHashTable *table, const void *pointer)
{
	(void)table;
	return (unsigned)(long)pointer;
}

static BOOL fn_int_equal(NSHashTable *table, const void *a, const void *b)
{
	(void)table;
	return (long)a == (long)b;
}

static NSString *fn_int_describe(NSHashTable *table, const void *pointer)
{
	(void)table;
	return [NSString stringWithFormat:@"%ld", (long)pointer];
}

static void fn_owned_release(NSHashTable *table, const void *pointer)
{
	(void)table;
	free((void *)pointer);
}

const NSHashTableCallBacks NSObjectHashCallBacks = {
	fn_object_hash, fn_object_equal, fn_object_retain, fn_object_release, fn_object_describe,
	(void *)-1
};

const NSHashTableCallBacks NSNonOwnedPointerHashCallBacks = {
	fn_ptr_hash, fn_ptr_equal, NULL, NULL, fn_ptr_describe, (void *)-1
};

const NSHashTableCallBacks NSNonRetainedObjectHashCallBacks = {
	fn_object_hash, fn_object_equal, NULL, NULL, fn_object_describe, (void *)-1
};

/* THE IDENTITY VARIANT: an object's POINTER is its identity, where the object set hashes and compares by value. */
const NSHashTableCallBacks NSOwnedObjectIdentityHashCallBacks = {
	fn_ptr_hash, fn_ptr_equal, fn_object_retain, fn_object_release, fn_object_describe, (void *)-1
};

const NSHashTableCallBacks NSOwnedPointerHashCallBacks = {
	fn_ptr_hash, fn_ptr_equal, NULL, fn_owned_release, fn_ptr_describe, (void *)-1
};

const NSHashTableCallBacks NSPointerToStructHashCallBacks = {
	fn_ptr_hash, fn_ptr_equal, NULL, NULL, fn_ptr_describe, (void *)-1
};

const NSHashTableCallBacks NSIntHashCallBacks = {
	fn_int_hash, fn_int_equal, NULL, NULL, fn_int_describe, (void *)(long)-1
};

const NSHashTableCallBacks NSIntegerHashCallBacks = {
	fn_int_hash, fn_int_equal, NULL, NULL, fn_int_describe, (void *)(long)-1
};

const NSUInteger NSHashTableZeroingWeakMemory = 1;

/* --- THE FUNCTIONS --------------------------------------------------------------------------------- */

NSHashTable *NSCreateHashTable(NSHashTableCallBacks callBacks, NSUInteger capacity)
{
	/* NO ZONE FORM, AND THE HEADER SAYS WHY: this library has no `NSZone` type, so this is a legacy table's only
	 * creation door. */
	return [[FNLegacyHashTable alloc] fnInitWithCallBacks:callBacks capacity:capacity];
}

void NSFreeHashTable(NSHashTable *table) { [table release]; }
void NSResetHashTable(NSHashTable *table) { [table removeAllObjects]; }
NSUInteger NSCountHashTable(NSHashTable *table) { return [table count]; }
NSArray *NSAllHashTableObjects(NSHashTable *table) { return [table allObjects]; }

void *NSHashGet(NSHashTable *table, const void *pointer)
{
	return (void *)[table member:(id)pointer];
}

void NSHashInsert(NSHashTable *table, const void *pointer)
{
	[table addObject:(id)pointer];
}

void NSHashInsertKnownAbsent(NSHashTable *table, const void *pointer)
{
	[table addObject:(id)pointer];
}

void *NSHashInsertIfAbsent(NSHashTable *table, const void *pointer)
{
	void *existing = (void *)[table member:(id)pointer];

	if (existing != NULL) {
		return existing;
	}
	[table addObject:(id)pointer];
	return NULL;
}

void NSHashRemove(NSHashTable *table, const void *pointer)
{
	[table removeObject:(id)pointer];
}

NSString *NSStringFromHashTable(NSHashTable *table)
{
	NSMutableString *text = [[NSMutableString alloc] initWithString:@"{"];
	NSEnumerator *objects = [table objectEnumerator];
	id object;
	NSUInteger i = 0, count = [table count];

	while ((object = [objects nextObject]) != nil) {
		[text appendFormat:@"%@%@", object, (++i < count) ? @"; " : @""];
	}
	[text appendString:@"}"];
	return [text autorelease];
}

NSHashEnumerator NSEnumerateHashTable(NSHashTable *table)
{
	NSHashEnumerator enumerator;

	enumerator.table = table;
	enumerator.index = 0;
	enumerator.objects = [NSAllHashTableObjects(table) retain];
	return enumerator;
}

void *NSNextHashEnumeratorItem(NSHashEnumerator *enumerator)
{
	void *item;

	if (enumerator == NULL || enumerator->index >= [enumerator->objects count]) {
		return NULL;
	}
	item = [(NSValue *)[enumerator->objects objectAtIndex:enumerator->index] pointerValue];
	enumerator->index++;
	return item;
}

void NSEndHashTableEnumeration(NSHashEnumerator *enumerator)
{
	if (enumerator != NULL) {
		[enumerator->objects release];
		enumerator->objects = nil;
	}
}

BOOL NSCompareHashTables(NSHashTable *table1, NSHashTable *table2)
{
	return [table1 isEqualToHashTable:table2];
}
