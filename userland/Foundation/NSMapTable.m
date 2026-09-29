/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSMapTable.m — W13b. See the header for the two independent sides and for the nil-value rule.
 *
 * THE ONLY THING THIS FILE ADDS TO THE SHARED TABLE IS A SECOND SET OF FUNCTIONS: `FNPointerTable` was
 * written with a value array precisely so that this class would not need a table of its own, and the four
 * convenience constructors below are nothing but a pairing of two options words.
 *
 * FAST ENUMERATION HANDS BACK THE VALUES, which is what Apple's map table enumerates (the keys have their own
 * `-keyEnumerator`), and the snapshot is kept in the two parallel arrays the header declares.
 *
 * MANUAL OWNERSHIP (MRC: the whole library is).
 */

#import <Foundation/NSMapTable.h>
#import <Foundation/FNLegacyMapTable.h>
#import <Foundation/FNPointerTable.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSEnumerator.h>
#import <Foundation/NSException.h>
#import <Foundation/NSString.h>
#import <Foundation/NSCoder.h>
#import <Foundation/NSValue.h>
/* NSMutableString, NSMutableArray and NSMutableDictionary live in their immutable classes headers in this tree,
 * which is why they are not imported by name - the gate refuses an import that resolves to no header. */
#include <stdlib.h>
#include <string.h>
#import <Foundation/NSNull.h>

#include <stdlib.h>

@implementation NSMapTable

- (instancetype)initWithKeyOptions:(NSMapTableOptions)keyOptions
		       valueOptions:(NSMapTableOptions)valueOptions
			   capacity:(NSUInteger)initialCapacity
{
	return [self initWithKeyPointerFunctions:
			[[[NSPointerFunctions alloc] initWithOptions:keyOptions] autorelease]
		     valuePointerFunctions:
			[[[NSPointerFunctions alloc] initWithOptions:valueOptions] autorelease]
				  capacity:initialCapacity];
}

- (instancetype)initWithKeyPointerFunctions:(NSPointerFunctions *)keyFunctions
		     valuePointerFunctions:(NSPointerFunctions *)valueFunctions
				  capacity:(NSUInteger)initialCapacity
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_table = [[FNPointerTable alloc] initWithKeyFunctions:keyFunctions
					     valueFunctions:valueFunctions
						   capacity:initialCapacity];
	_mutations = 1;
	return self;
}

+ (instancetype)mapTableWithKeyOptions:(NSMapTableOptions)keyOptions
			  valueOptions:(NSMapTableOptions)valueOptions
{
	return [[[self alloc] initWithKeyOptions:keyOptions
				    valueOptions:valueOptions
					capacity:0] autorelease];
}

+ (instancetype)strongToStrongObjectsMapTable
{
	return [self mapTableWithKeyOptions:NSPointerFunctionsObjectPersonality
			       valueOptions:NSPointerFunctionsObjectPersonality];
}

+ (instancetype)weakToStrongObjectsMapTable
{
	/* THE OBSERVER REGISTRY: the keys are not owned (and not zeroed), the values are. */
	return [self mapTableWithKeyOptions:
			(NSPointerFunctionsObjectPointerPersonality | NSPointerFunctionsWeakMemory)
			       valueOptions:NSPointerFunctionsObjectPersonality];
}

+ (instancetype)strongToWeakObjectsMapTable
{
	return [self mapTableWithKeyOptions:NSPointerFunctionsObjectPointerPersonality
			       valueOptions:
			(NSPointerFunctionsObjectPointerPersonality | NSPointerFunctionsWeakMemory)];
}

+ (instancetype)weakToWeakObjectsMapTable
{
	return [self mapTableWithKeyOptions:
			(NSPointerFunctionsObjectPointerPersonality | NSPointerFunctionsWeakMemory)
			       valueOptions:
			(NSPointerFunctionsObjectPointerPersonality | NSPointerFunctionsWeakMemory)];
}

- (void)dealloc
{
	[_table release];
	free(_snapshotKeys);
	free(_snapshotValues);
	[super dealloc];
}

- (NSPointerFunctions *)keyPointerFunctions { return [_table keyFunctions]; }
- (NSPointerFunctions *)valuePointerFunctions { return [_table valueFunctions]; }

- (id)objectForKey:(id)aKey
{
	return aKey != nil ? (id)[_table fnValueForKey:(void *)aKey] : nil;
}

- (void)setObject:(id)anObject forKey:(id)aKey
{
	if (aKey == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSMapTable: nil is not a key (and a NULL pointer cannot be one)"];
	}
	/* A NIL VALUE IS STORED, not a removal: the entry exists with an empty value. See the header. */
	(void)[_table fnSetKey:(void *)aKey value:(void *)anObject];
	_mutations++;
}

- (void)removeObjectForKey:(id)aKey
{
	if (aKey != nil) {
		[_table fnRemoveKey:(void *)aKey];
		_mutations++;
	}
}

- (void)removeAllObjects
{
	[_table fnRemoveAll];
	_mutations++;
}

- (NSUInteger)count
{
	return [_table count];
}

- (NSEnumerator *)keyEnumerator
{
	return [[self fnKeysArray] objectEnumerator];
}

- (NSEnumerator *)objectEnumerator
{
	NSMutableArray *values = [NSMutableArray arrayWithCapacity:[self count]];
	NSEnumerator *keys = [self keyEnumerator];
	id key;

	/* OVER THE PRIMITIVES (§C.3 item 5): the values, in key order, through -objectForKey:. */
	while ((key = [keys nextObject]) != nil) {
		id value = [self objectForKey:key];

		[values addObject:(value != nil ? value : (id)[NSNull null])];
	}
	return [values objectEnumerator];
}

- (NSDictionary *)dictionaryRepresentation
{
	/*
	 * OVER THE PRIMITIVES (§C.3 item 5), AND THE NSNull IS THE POINT: this family CAN hold an empty value for a
	 * key, and a dictionary cannot hold nil, so representing it as NSNull beats losing the pair.
	 */
	NSMutableDictionary *representation = [NSMutableDictionary dictionary];
	NSEnumerator *keys = [self keyEnumerator];
	id key;

	while ((key = [keys nextObject]) != nil) {
		id value = [self objectForKey:key];

		[representation setObject:(value != nil ? value : (id)[NSNull null]) forKey:key];
	}
	return representation;
}

- (NSArray *)fnKeysArray
{
	NSMutableArray *keys = [NSMutableArray arrayWithCapacity:[_table count]];
	NSUInteger i;
	NSUInteger slots = [_table slotCount];

	for (i = 0; i < slots; i++) {
		if ([_table fnStateAtSlot:i] == FN_SLOT_FULL) {
			[keys addObject:(id)[_table fnKeyAtSlot:i]];
		}
	}
	return keys;
}

- (NSArray *)fnValuesArray
{
	NSMutableArray *values = [NSMutableArray arrayWithCapacity:[_table count]];
	NSUInteger i;
	NSUInteger slots = [_table slotCount];

	for (i = 0; i < slots; i++) {
		if ([_table fnStateAtSlot:i] == FN_SLOT_FULL) {
			id value = (id)[_table fnValueAtSlot:i];

			[values addObject:(value != nil ? value : (id)[NSNull null])];
		}
	}
	return values;
}

- (id)copy
{
	NSMapTable *copy = [[[self class] alloc]
		initWithKeyPointerFunctions:[_table keyFunctions]
		     valuePointerFunctions:[_table valueFunctions]
				  capacity:[_table count]];
	NSUInteger i;
	NSUInteger slots = [_table slotCount];

	for (i = 0; i < slots; i++) {
		if ([_table fnStateAtSlot:i] == FN_SLOT_FULL) {
			(void)[copy->_table fnSetKey:(void *)[_table fnKeyAtSlot:i]
					       value:(void *)[_table fnValueAtSlot:i]];
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
	NSEnumerator *enumerator = [self keyEnumerator];
	id object;

	/* A MAP TABLE ENUMERATES ITS KEYS, over the primitives through the CALLER'S buffer: state->state is the
	 * cursor, and the snapshot is what -keyEnumerator already hands out. */
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
	[coder encodeObject:[self dictionaryRepresentation] forKey:@"NS.map"];
}

- (instancetype)initWithCoder:(NSCoder *)coder
{
	self = [self initWithKeyOptions:NSPointerFunctionsObjectPointerPersonality
			   valueOptions:NSPointerFunctionsObjectPointerPersonality
			       capacity:0];
	if (self != nil) {
		NSDictionary *representation = [coder decodeObjectForKey:@"NS.map"];
		NSArray *keys = [representation allKeys];
		NSUInteger i;

		for (i = 0; i < [keys count]; i++) {
			id key = [keys objectAtIndex:i];
			id value = [representation objectForKey:key];

			(void)[_table fnSetKey:(void *)key
				  value:(value != nil && ![value isKindOfClass:[NSNull class]]
					 ? (void *)value : NULL)];
		}
	}
	return self;
}

@end

/* ===================================================================================================
 * THE LEGACY C API (§62.44), AND IT IS A SUBCLASS BECAUSE A BRIDGE CANNOT BE ONE — see the header's note:
 * Apple's call-backs take the TABLE, and a C function pointer cannot close over one, so the only faithful shape
 * is a table that carries its own call-backs and hands itself to them. THE MODERN PATH IS UNTOUCHED.
 * =================================================================================================== */


@implementation FNLegacyMapTable

- (instancetype)fnInitWithKeyCallBacks:(NSMapTableKeyCallBacks)keyCallBacks
			valueCallBacks:(NSMapTableValueCallBacks)valueCallBacks
			     capacity:(NSUInteger)capacity
{
	self = [super init];
	if (self != nil) {
		_keyCallBacks = keyCallBacks;
		_valueCallBacks = valueCallBacks;
		_legacyCapacity = capacity > 0 ? capacity : 4;
		_legacyKeys = (void **)calloc(_legacyCapacity, sizeof(void *));
		_legacyValues = (void **)calloc(_legacyCapacity, sizeof(void *));
		if (_legacyKeys == NULL || _legacyValues == NULL) {
			free(_legacyKeys);
			free(_legacyValues);
			[self release];
			return nil;
		}
	}
	return self;
}

/* THE LINEAR SCAN, AND IT IS STATED RATHER THAN APOLOGISED FOR: a legacy table's callers are ancient and its
 * tables are small, and a scan is the one implementation of "is this key here" that cannot disagree with the scan
 * that finds its position - which a hashed index COULD, when a call-back hashes two keys equally and compares
 * them unequally (or the reverse).
 *
 * NAMED CONSEQUENCE, AND THE PROBE MEASURES IT: A CALLER'S `hash` CALL-BACK IS NEVER CONSULTED. Apple's legacy
 * table hashes; this one compares. A call-back that only hashes (and compares everything equal) would therefore
 * behave differently here - which is a DEVIATION rather than a detail, and it is written where the code is and
 * asserted where the behaviour is. */
- (NSUInteger)fnIndexForKey:(const void *)key
{
	NSUInteger i;

	for (i = 0; i < _legacyCount; i++) {
		if (_keyCallBacks.isEqual != NULL) {
			if (_keyCallBacks.isEqual(self, _legacyKeys[i], key)) {
				return i;
			}
		} else if (_legacyKeys[i] == key) {
			return i;
		}
	}
	return NSNotFound;
}

- (void *)fnKeyAtIndex:(NSUInteger)index { return index < _legacyCount ? _legacyKeys[index] : NULL; }
- (void *)fnValueAtIndex:(NSUInteger)index { return index < _legacyCount ? _legacyValues[index] : NULL; }

- (void)fnAppendKey:(const void *)key value:(const void *)value
{
	if (_legacyCount == _legacyCapacity) {
		NSUInteger grown = _legacyCapacity * 2;

		_legacyKeys = (void **)realloc(_legacyKeys, sizeof(void *) * grown);
		_legacyValues = (void **)realloc(_legacyValues, sizeof(void *) * grown);
		_legacyCapacity = grown;
	}
	if (_keyCallBacks.retain != NULL) {
		_keyCallBacks.retain(self, key);
	}
	if (_valueCallBacks.retain != NULL) {
		_valueCallBacks.retain(self, value);
	}
	_legacyKeys[_legacyCount] = (void *)key;
	_legacyValues[_legacyCount] = (void *)value;
	_legacyCount++;
}

- (NSUInteger)count { return _legacyCount; }

- (id)objectForKey:(id)aKey
{
	NSUInteger index = [self fnIndexForKey:(const void *)aKey];

	return index == NSNotFound ? nil : (id)_legacyValues[index];
}

- (void)setObject:(id)anObject forKey:(id)aKey
{
	NSUInteger index = [self fnIndexForKey:(const void *)aKey];

	if (index != NSNotFound) {
		/* THE KEY STAYS AND THE VALUE MOVES: Apple's rule for a replace, and the reason a table that owns its
		 * keys does not leak or double-free one here. */
		if (_valueCallBacks.release != NULL) {
			_valueCallBacks.release(self, _legacyValues[index]);
		}
		if (_valueCallBacks.retain != NULL) {
			_valueCallBacks.retain(self, (const void *)anObject);
		}
		_legacyValues[index] = (void *)anObject;
		return;
	}
	[self fnAppendKey:(const void *)aKey value:(const void *)anObject];
}

- (void)removeObjectForKey:(id)aKey
{
	NSUInteger index = [self fnIndexForKey:(const void *)aKey];

	if (index == NSNotFound) {
		return;
	}
	if (_keyCallBacks.release != NULL) {
		_keyCallBacks.release(self, _legacyKeys[index]);
	}
	if (_valueCallBacks.release != NULL) {
		_valueCallBacks.release(self, _legacyValues[index]);
	}
	_legacyCount--;
	if (index < _legacyCount) {
		memmove(&_legacyKeys[index], &_legacyKeys[index + 1], sizeof(void *) * (_legacyCount - index));
		memmove(&_legacyValues[index], &_legacyValues[index + 1], sizeof(void *) * (_legacyCount - index));
	}
}

- (void)removeAllObjects
{
	NSUInteger i;

	for (i = 0; i < _legacyCount; i++) {
		if (_keyCallBacks.release != NULL) {
			_keyCallBacks.release(self, _legacyKeys[i]);
		}
		if (_valueCallBacks.release != NULL) {
			_valueCallBacks.release(self, _legacyValues[i]);
		}
	}
	_legacyCount = 0;
}

/* THE ENUMERATORS HAND BACK NSValue-WRAPPED POINTERS, AND THE HEADER SAYS WHY: an NSEnumerator carries OBJECTS,
 * and a pointer or integer personality's keys are not objects - wrapping is what lets the object API answer at all
 * for them, and the C enumeration is the door a legacy caller uses. */
- (NSEnumerator *)keyEnumerator
{
	NSMutableArray *wrapped = [[NSMutableArray alloc] init];
	NSUInteger i;

	for (i = 0; i < _legacyCount; i++) {
		[wrapped addObject:[NSValue valueWithPointer:_legacyKeys[i]]];
	}
	return [[wrapped autorelease] objectEnumerator];
}

- (NSEnumerator *)objectEnumerator
{
	NSMutableArray *wrapped = [[NSMutableArray alloc] init];
	NSUInteger i;

	for (i = 0; i < _legacyCount; i++) {
		[wrapped addObject:[NSValue valueWithPointer:_legacyValues[i]]];
	}
	return [[wrapped autorelease] objectEnumerator];
}

- (NSDictionary *)dictionaryRepresentation
{
	NSMutableDictionary *out = [[NSMutableDictionary alloc] init];
	NSUInteger i;

	for (i = 0; i < _legacyCount; i++) {
		[out setObject:[NSValue valueWithPointer:_legacyValues[i]]
			forKey:[NSValue valueWithPointer:_legacyKeys[i]]];
	}
	return [out autorelease];
}

/* A COPY CARRIES THE CALL-BACKS AND ITS OWN PAIRS, which is what `NSCopyMapTableWithZone` needs and what a copy of
 * a legacy table has to be: a different table with the same ownership rules. */
- (id)copy
{
	FNLegacyMapTable *copy = [[FNLegacyMapTable alloc] fnInitWithKeyCallBacks:_keyCallBacks
								  valueCallBacks:_valueCallBacks
								       capacity:_legacyCount];
	NSUInteger i;

	for (i = 0; i < _legacyCount; i++) {
		[copy setObject:(id)_legacyValues[i] forKey:(id)_legacyKeys[i]];
	}
	return copy;
}



- (void)dealloc
{
	if (_keyCallBacks.release != NULL || _valueCallBacks.release != NULL) {
		[self removeAllObjects];
	}
	free(_legacyKeys);
	free(_legacyValues);
	[super dealloc];
}

@end

/* --- THE THIRTEEN PRE-BUILT CALL-BACK SETS --------------------------------------------------------- */

static unsigned fn_ptr_hash(NSMapTable *table, const void *key)
{
	(void)table;
	return (unsigned)((uintptr_t)key >> 2);
}

static BOOL fn_ptr_equal(NSMapTable *table, const void *a, const void *b)
{
	(void)table;
	return a == b;
}

static NSString *fn_ptr_describe(NSMapTable *table, const void *key)
{
	(void)table;
	return [NSString stringWithFormat:@"%p", key];
}

static unsigned fn_object_hash(NSMapTable *table, const void *key)
{
	(void)table;
	return (unsigned)[(id)key hash];
}

static BOOL fn_object_equal(NSMapTable *table, const void *a, const void *b)
{
	(void)table;
	return [(id)a isEqual:(id)b];
}

static void fn_object_retain(NSMapTable *table, const void *key)
{
	(void)table;
	[(id)key retain];
}

static void fn_object_release(NSMapTable *table, const void *key)
{
	(void)table;
	[(id)key release];
}

static NSString *fn_object_describe(NSMapTable *table, const void *key)
{
	(void)table;
	return [(id)key description];
}

static unsigned fn_int_hash(NSMapTable *table, const void *key)
{
	(void)table;
	return (unsigned)(long)key;
}

static BOOL fn_int_equal(NSMapTable *table, const void *a, const void *b)
{
	(void)table;
	return (long)a == (long)b;
}

static NSString *fn_int_describe(NSMapTable *table, const void *key)
{
	(void)table;
	return [NSString stringWithFormat:@"%ld", (long)key];
}

static void fn_owned_release(NSMapTable *table, const void *key)
{
	(void)table;
	free((void *)key);
}

const NSMapTableKeyCallBacks NSObjectMapKeyCallBacks = {
	fn_object_hash, fn_object_equal, fn_object_retain, fn_object_release, fn_object_describe,
	NSNotAPointerMapKey
};

const NSMapTableValueCallBacks NSObjectMapValueCallBacks = {
	fn_object_retain, fn_object_release, fn_object_describe
};

const NSMapTableKeyCallBacks NSNonOwnedPointerMapKeyCallBacks = {
	fn_ptr_hash, fn_ptr_equal, NULL, NULL, fn_ptr_describe, NSNotAPointerMapKey
};

const NSMapTableKeyCallBacks NSNonOwnedPointerOrNullMapKeyCallBacks = {
	fn_ptr_hash, fn_ptr_equal, NULL, NULL, fn_ptr_describe, NSNotAPointerMapKey
};

const NSMapTableValueCallBacks NSNonOwnedPointerMapValueCallBacks = {
	NULL, NULL, fn_ptr_describe
};

const NSMapTableKeyCallBacks NSNonRetainedObjectMapKeyCallBacks = {
	fn_object_hash, fn_object_equal, NULL, NULL, fn_object_describe, NSNotAPointerMapKey
};

const NSMapTableValueCallBacks NSNonRetainedObjectMapValueCallBacks = {
	NULL, NULL, fn_object_describe
};

const NSMapTableKeyCallBacks NSOwnedPointerMapKeyCallBacks = {
	fn_ptr_hash, fn_ptr_equal, NULL, fn_owned_release, fn_ptr_describe, NSNotAPointerMapKey
};

const NSMapTableValueCallBacks NSOwnedPointerMapValueCallBacks = {
	NULL, fn_owned_release, fn_ptr_describe
};

const NSMapTableKeyCallBacks NSIntMapKeyCallBacks = {
	fn_int_hash, fn_int_equal, NULL, NULL, fn_int_describe, NSNotAnIntMapKey
};

const NSMapTableValueCallBacks NSIntMapValueCallBacks = {
	NULL, NULL, fn_int_describe
};

const NSMapTableKeyCallBacks NSIntegerMapKeyCallBacks = {
	fn_int_hash, fn_int_equal, NULL, NULL, fn_int_describe, NSNotAnIntegerMapKey
};

const NSMapTableValueCallBacks NSIntegerMapValueCallBacks = {
	NULL, NULL, fn_int_describe
};

/* --- THE FUNCTIONS --------------------------------------------------------------------------------- */

NSMapTable *NSCreateMapTable(NSMapTableKeyCallBacks keyCallBacks,
			     NSMapTableValueCallBacks valueCallBacks,
			     NSUInteger capacity)
{
	/* NO ZONE FORM, AND THE HEADER SAYS WHY: this library has no `NSZone` type, so the zone-taking variant is not
	 * declared and this is a legacy table's only creation door. */
	return [[FNLegacyMapTable alloc] fnInitWithKeyCallBacks:keyCallBacks
						valueCallBacks:valueCallBacks
						     capacity:capacity];
}

void NSFreeMapTable(NSMapTable *table) { [table release]; }
void NSResetMapTable(NSMapTable *table) { [table removeAllObjects]; }
NSUInteger NSCountMapTable(NSMapTable *table) { return [table count]; }

void *NSMapGet(NSMapTable *table, const void *key)
{
	return (void *)[table objectForKey:(id)key];
}

void NSMapInsert(NSMapTable *table, const void *key, const void *value)
{
	[table setObject:(id)value forKey:(id)key];
}

/* KNOWN ABSENT MEANS THE CALLER HAS ALREADY ESTABLISHED IT, so this is the same insert: the difference is a
 * CONTRACT rather than a behaviour, and pretending to check would be the second implementation of the scan. */
void NSMapInsertKnownAbsent(NSMapTable *table, const void *key, const void *value)
{
	[table setObject:(id)value forKey:(id)key];
}

void *NSMapInsertIfAbsent(NSMapTable *table, const void *key, const void *value)
{
	void *existing = (void *)[table objectForKey:(id)key];

	if (existing != NULL) {
		return existing;
	}
	[table setObject:(id)value forKey:(id)key];
	return NULL;
}

void NSMapRemove(NSMapTable *table, const void *key)
{
	[table removeObjectForKey:(id)key];
}

BOOL NSMapMember(NSMapTable *table, const void *key, void **originalKey, void **value)
{
	FNLegacyMapTable *legacy = (FNLegacyMapTable *)table;
	NSUInteger index = [legacy fnIndexForKey:key];

	if (index == NSNotFound) {
		return NO;
	}
	if (originalKey != NULL) {
		*originalKey = [legacy fnKeyAtIndex:index];
	}
	if (value != NULL) {
		*value = [legacy fnValueAtIndex:index];
	}
	return YES;
}

NSString *NSStringFromMapTable(NSMapTable *table)
{
	NSMutableString *text = [[NSMutableString alloc] initWithString:@"{"];
	NSUInteger i, count = [table count];
	NSEnumerator *keys = [table keyEnumerator];
	id key;

	for (i = 0; (key = [keys nextObject]) != nil; i++) {
		id value = [table objectForKey:key];

		[text appendFormat:@"%@ = %@%@", key, value, (i + 1 < count) ? @"; " : @""];
	}
	[text appendString:@"}"];
	return [text autorelease];
}

NSArray *NSAllMapTableKeys(NSMapTable *table)
{
	NSMutableArray *keys = [[NSMutableArray alloc] init];
	FNLegacyMapTable *legacy = (FNLegacyMapTable *)table;
	NSUInteger i;

	for (i = 0; i < [legacy count]; i++) {
		[keys addObject:[NSValue valueWithPointer:[legacy fnKeyAtIndex:i]]];
	}
	return [keys autorelease];
}

NSArray *NSAllMapTableValues(NSMapTable *table)
{
	NSMutableArray *values = [[NSMutableArray alloc] init];
	FNLegacyMapTable *legacy = (FNLegacyMapTable *)table;
	NSUInteger i;

	for (i = 0; i < [legacy count]; i++) {
		[values addObject:[NSValue valueWithPointer:[legacy fnValueAtIndex:i]]];
	}
	return [values autorelease];
}

NSMapEnumerator NSEnumerateMapTable(NSMapTable *table)
{
	NSMapEnumerator enumerator;

	enumerator.table = table;
	enumerator.index = 0;
	enumerator.keys = [NSAllMapTableKeys(table) retain];
	return enumerator;
}

BOOL NSNextMapEnumeratorPair(NSMapEnumerator *enumerator, void **key, void **value)
{
	if (enumerator == NULL || enumerator->index >= [enumerator->keys count]) {
		return NO;
	}
	if (key != NULL) {
		*key = [(NSValue *)[enumerator->keys objectAtIndex:enumerator->index] pointerValue];
	}
	if (value != NULL) {
		*value = NSMapGet(enumerator->table, *key);
	}
	enumerator->index++;
	return YES;
}

void NSEndMapTableEnumeration(NSMapEnumerator *enumerator)
{
	if (enumerator != NULL) {
		[enumerator->keys release];
		enumerator->keys = nil;
	}
}

BOOL NSCompareMapTables(NSMapTable *table1, NSMapTable *table2)
{
	FNLegacyMapTable *first = (FNLegacyMapTable *)table1;
	NSUInteger i;

	if ([table1 count] != [table2 count]) {
		return NO;
	}
	for (i = 0; i < [first count]; i++) {
		void *key = [first fnKeyAtIndex:i];
		void *mine = [first fnValueAtIndex:i];

		if (!NSMapMember(table2, key, NULL, NULL) || NSMapGet(table2, key) != mine) {
			return NO;
		}
	}
	return YES;
}


