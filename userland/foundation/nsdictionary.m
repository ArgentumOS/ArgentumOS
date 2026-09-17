/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * nsdictionary.m — the hash table. See NSDictionary.h for why keys are copied.
 *
 * ARC file: the entry slots are C storage, so they are retained and released by
 * hand with objc_retain/objc_release.
 *
 * STRUCTURE NOTE: the table's mutation (grow, insert, unlink) lives on the BASE
 * class as private methods, and the mutable class's public API is a thin wrapper
 * over them. That is not tidiness for its own sake: -copy and -mutableCopy have
 * to build an instance of the OTHER class, and they can only do that through
 * storage the base class owns.
 */

#import <foundation/NSDictionary.h>
#import <foundation/NSString.h>
#import <foundation/NSArray.h>	/* the allKeys/allValues family returns one */
#import <objc/runtime.h>
#include <objc/objc-arc.h>	/* objc_retain/objc_release: the C slots are not ARC-managed */
#include <stdlib.h>

struct FNDictEntry {
	struct FNDictEntry *next;
	id key;			/* COPIED on insert */
	id value;		/* retained */
};

/* The private surface, declared where it belongs: here. */
@interface NSDictionary ()
- (id)initAsCopyOf:(NSDictionary *)source;
- (void)fillWithFirstObject:(id)firstObject arguments:(va_list)args;
- (void)setObjectInternal:(id)value forKey:(id)key;
- (void)removeObjectInternalForKey:(id)key;
- (void)removeAllInternalObjects;
- (id __unsafe_unretained *)keySnapshot;
- (void)dropKeySnapshot;
@end

static unsigned long dict_bucket_count_for(unsigned long needed)
{
	unsigned long count = 8;

	while (count < needed + needed / 2) {	/* keep the load factor under 2/3 */
		count *= 2;
	}
	return count;
}

static struct FNDictEntry **dict_buckets_alloc(unsigned long count)
{
	return (struct FNDictEntry **)calloc(count, sizeof(struct FNDictEntry *));
}

static void dict_entries_free(struct FNDictEntry **buckets, unsigned long count)
{
	unsigned long i;

	for (i = 0; i < count; i++) {
		struct FNDictEntry *entry = buckets[i];

		while (entry != NULL) {
			struct FNDictEntry *next = entry->next;

			objc_release(entry->key);
			objc_release(entry->value);
			free(entry);
			entry = next;
		}
	}
	free(buckets);
}

@implementation NSDictionary

+ (NSDictionary *)dictionary
{
	return [[self alloc] init];
}

+ (NSDictionary *)dictionaryWithObject:(id)value forKey:(id)key
{
	return [[self alloc] initWithObject:value forKey:key];
}

- (id)initWithObject:(id)value forKey:(id)key
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	if (value != nil && key != nil) {
		[self setObjectInternal:value forKey:key];
	}
	return self;
}

- (id)initAsCopyOf:(NSDictionary *)source
{
	unsigned long i;

	self = [super init];
	if (self == nil) {
		return nil;
	}
	for (i = 0; i < source->_bucketCount; i++) {
		struct FNDictEntry *entry;

		for (entry = source->_buckets[i]; entry != NULL; entry = entry->next) {
			[self setObjectInternal:entry->value forKey:entry->key];
		}
	}
	return self;
}

- (void)dealloc
{
	[self dropKeySnapshot];
	dict_entries_free(_buckets, _bucketCount);
}

- (unsigned long)count
{
	return _count;
}

- (id)objectForKey:(id)key
{
	struct FNDictEntry *entry;
	unsigned long index;

	if (key == nil) {
		return nil;
	}
	/* A zero-bucket table has never been written to, so nothing can be found. */
	if (_bucketCount == 0) {
		return nil;
	}
	index = [key hash] & (_bucketCount - 1);
	for (entry = _buckets[index]; entry != NULL; entry = entry->next) {
		if (entry->key == key || [entry->key isEqual:key]) {
			return entry->value;
		}
	}
	return nil;
}

/* Cocoa's subscript: `dict[key]` lowers to this. */
- (id)objectForKeyedSubscript:(id)key
{
	return [self objectForKey:key];
}

- (void)setObjectInternal:(id)value forKey:(id)key
{
	struct FNDictEntry *entry;
	unsigned long index;

	/* Grow before the chains get long: the load factor stays under 1. */
	if (_bucketCount == 0 || _count + 1 >= _bucketCount) {
		unsigned long grown = dict_bucket_count_for(_count + 1);
		struct FNDictEntry **buckets = dict_buckets_alloc(grown);
		unsigned long i;

		if (buckets == NULL) {
			return;
		}
		for (i = 0; i < _bucketCount; i++) {
			struct FNDictEntry *older = _buckets[i];

			while (older != NULL) {
				struct FNDictEntry *next = older->next;
				unsigned long where = [older->key hash] & (grown - 1);

				older->next = buckets[where];
				buckets[where] = older;
				older = next;
			}
		}
		free(_buckets);
		_buckets = buckets;
		_bucketCount = grown;
	}
	index = [key hash] & (_bucketCount - 1);
	for (entry = _buckets[index]; entry != NULL; entry = entry->next) {
		if (entry->key == key || [entry->key isEqual:key]) {
			/* Keep the ORIGINAL key, replace the value: the key's identity is
			 * what the table is filed under, and it is already a copy. */
			objc_retain(value);
			objc_release(entry->value);
			entry->value = value;
			_mutations++;
			[self dropKeySnapshot];
			return;
		}
	}
	entry = (struct FNDictEntry *)calloc(1, sizeof(struct FNDictEntry));
	if (entry == NULL) {
		return;
	}
	{
		id copied = [key copy];

		/*
		 * THE AUDIT'S FIX A. A class that does not implement the copying
		 * protocol does NOT raise here — the runtime's forwarding path answers
		 * nil — and filing an entry under a nil key produced a PHANTOM: the
		 * count grew, the value was unreachable, and nothing complained. Refuse
		 * instead of filing it. (When F4 brings exceptions, this becomes one.)
		 */
		if (copied == nil) {
			free(entry);
			return;
		}
		entry->key = objc_retain(copied);
	}
	entry->value = objc_retain(value);
	entry->next = _buckets[index];
	_buckets[index] = entry;
	_count++;
	_mutations++;
	[self dropKeySnapshot];
}

- (void)removeObjectInternalForKey:(id)key
{
	struct FNDictEntry *entry;
	struct FNDictEntry *previous = NULL;
	unsigned long index;

	if (key == nil || _bucketCount == 0) {
		return;
	}
	index = [key hash] & (_bucketCount - 1);
	for (entry = _buckets[index]; entry != NULL; entry = entry->next) {
		if (entry->key == key || [entry->key isEqual:key]) {
			if (previous == NULL) {
				_buckets[index] = entry->next;
			} else {
				previous->next = entry->next;
			}
			objc_release(entry->key);
			objc_release(entry->value);
			free(entry);
			_count--;
			_mutations++;
			[self dropKeySnapshot];
			return;
		}
		previous = entry;
	}
}

- (void)removeAllInternalObjects
{
	unsigned long i;

	for (i = 0; i < _bucketCount; i++) {
		struct FNDictEntry *entry = _buckets[i];

		while (entry != NULL) {
			struct FNDictEntry *next = entry->next;

			objc_release(entry->key);
			objc_release(entry->value);
			free(entry);
			entry = next;
		}
		_buckets[i] = NULL;
	}
	_count = 0;
	_mutations++;
	[self dropKeySnapshot];
}

+ (NSDictionary *)dictionaryWithDictionary:(NSDictionary *)other
{
	return [[self alloc] initAsCopyOf:other];
}

+ (NSDictionary *)dictionaryWithObjects:(const id *)values
				forKeys:(const id *)keys
				  count:(NSUInteger)count
{
	return [[self alloc] initWithObjects:values forKeys:keys count:count];
}

+ (NSDictionary *)dictionaryWithObjectsAndKeys:(id)firstObject, ...
{
	va_list args;
	id result = [[self alloc] init];

	va_start(args, firstObject);
	[result fillWithFirstObject:firstObject arguments:args];
	va_end(args);
	return result;
}

- (id)initWithDictionary:(NSDictionary *)other
{
	return [self initAsCopyOf:other];
}

- (id)initWithObjects:(const id *)values
	      forKeys:(const id *)keys
		count:(NSUInteger)count
{
	NSUInteger i;

	self = [super init];
	if (self == nil) {
		return nil;
	}
	for (i = 0; i < count; i++) {
		if (values[i] != nil && keys[i] != nil) {
			[self setObjectInternal:values[i] forKey:keys[i]];
		}
	}
	return self;
}

/*
 * The nil-terminated form takes OBJECT, KEY, OBJECT, KEY ... so the FIRST
 * argument is named and the list carries the rest — the same trap the array
 * family hit, where reading the list as though it began at the named argument
 * silently dropped it. Here the named argument is the first VALUE, and its key
 * is the first list entry.
 */
- (id)initWithObjectsAndKeys:(id)firstObject, ...
{
	va_list args;

	self = [self init];
	if (self == nil) {
		return nil;
	}
	va_start(args, firstObject);
	[self fillWithFirstObject:firstObject arguments:args];
	va_end(args);
	return self;
}

/*
 * THE PAIR WALK, in ONE place, because a variadic method cannot be handed a
 * va_list: the class factory below used to write `initWithObjectsAndKeys:firstObject,
 * args`, which is a COMMA EXPRESSION rather than a call, so the va_list went in as
 * the first variadic argument — the crash the step markers localized. Both entry
 * points hand the list to this instead.
 */
- (void)fillWithFirstObject:(id)firstObject arguments:(va_list)args
{
	id value = firstObject;
	id key;

	if (value == nil) {
		return;
	}
	for (;;) {
		key = va_arg(args, id);
		if (key == nil) {
			break;
		}
		[self setObjectInternal:value forKey:key];
		value = va_arg(args, id);
		if (value == nil) {
			break;
		}
	}
}

- (NSArray *)allKeys
{
	NSMutableArray *keys = [[NSMutableArray alloc] init];
	unsigned long i;

	for (i = 0; i < _bucketCount; i++) {
		struct FNDictEntry *entry;

		for (entry = _buckets[i]; entry != NULL; entry = entry->next) {
			[keys addObject:entry->key];
		}
	}
	return keys;
}

- (NSArray *)allValues
{
	NSMutableArray *values = [[NSMutableArray alloc] init];
	unsigned long i;

	for (i = 0; i < _bucketCount; i++) {
		struct FNDictEntry *entry;

		for (entry = _buckets[i]; entry != NULL; entry = entry->next) {
			[values addObject:entry->value];
		}
	}
	return values;
}

- (NSArray *)allKeysForObject:(id)object
{
	NSMutableArray *keys = [[NSMutableArray alloc] init];
	unsigned long i;

	for (i = 0; i < _bucketCount; i++) {
		struct FNDictEntry *entry;

		for (entry = _buckets[i]; entry != NULL; entry = entry->next) {
			if (entry->value == object || [entry->value isEqual:object]) {
				[keys addObject:entry->key];
			}
		}
	}
	return keys;
}

- (NSArray *)objectsForKeys:(NSArray *)keys notFoundMarker:(id)marker
{
	NSMutableArray *values = [[NSMutableArray alloc] init];
	NSUInteger i;

	for (i = 0; i < [keys count]; i++) {
		id key = [keys objectAtIndex:i];
		id found = [self objectForKey:key];

		[values addObject:(found != nil) ? found : marker];
	}
	return values;
}

- (void)getObjects:(id __unsafe_unretained *)objects
	   andKeys:(id __unsafe_unretained *)keys
{
	unsigned long i;
	unsigned long n = 0;

	for (i = 0; i < _bucketCount; i++) {
		struct FNDictEntry *entry;

		for (entry = _buckets[i]; entry != NULL; entry = entry->next) {
			if (keys != NULL) {
				keys[n] = entry->key;
			}
			if (objects != NULL) {
				objects[n] = entry->value;
			}
			n++;
		}
	}
}

- (BOOL)isEqualToDictionary:(NSDictionary *)other
{
	unsigned long i;

	if (other == nil) {
		return NO;
	}
	if (other == self) {
		return YES;
	}
	if ([other count] != _count) {
		return NO;
	}
	for (i = 0; i < _bucketCount; i++) {
		struct FNDictEntry *entry;

		for (entry = _buckets[i]; entry != NULL; entry = entry->next) {
			id mine = entry->value;
			id theirs = [other objectForKey:entry->key];

			if (mine == theirs) {
				continue;
			}
			if (theirs == nil || ![mine isEqual:theirs]) {
				return NO;
			}
		}
	}
	return YES;
}

- (BOOL)isEqual:(id)other
{
	if (other == self) {
		return YES;
	}
	if (other == nil || ![other isKindOfClass:[NSDictionary class]]) {
		return NO;
	}
	return [self isEqualToDictionary:(NSDictionary *)other];
}

- (unsigned long)hash
{
	/*
	 * ORDER-INDEPENDENT, because equality is: two dictionaries with the same
	 * pairs are equal however they were built, so their hashes must agree. A
	 * fold over bucket order would not.
	 */
	unsigned long h = 2166136261UL;
	unsigned long i;

	h ^= _count;
	h *= 16777619UL;
	for (i = 0; i < _bucketCount; i++) {
		struct FNDictEntry *entry;

		for (entry = _buckets[i]; entry != NULL; entry = entry->next) {
			h += ([entry->key hash] ^ [entry->value hash]);
		}
	}
	return h;
}

- (NSString *)description
{
	NSMutableString *out = [[NSMutableString alloc] initWithUTF8String:"{"];
	unsigned long i;
	int first = 1;

	for (i = 0; i < _bucketCount; i++) {
		struct FNDictEntry *entry;

		for (entry = _buckets[i]; entry != NULL; entry = entry->next) {
			if (!first) {
				[out appendString:@", "];
			}
			first = 0;
			[out appendString:[entry->key description]];
			[out appendString:@" = "];
			[out appendString:[entry->value description]];
		}
	}
	[out appendString:@"}"];
	return out;
}

- (id)copy
{
	return self;		/* immutable */
}

- (id)mutableCopy
{
	return [[NSMutableDictionary alloc] initAsCopyOf:self];
}

- (id)copyWithZone:(NSZone *)zone
{
	(void)zone;
	return [self copy];
}

- (id __unsafe_unretained *)keySnapshot
{
	unsigned long i;
	unsigned long n = 0;

	if (_keys != NULL || _count == 0) {
		return _keys;
	}
	_keys = (id *)calloc(_count, sizeof(id));
	if (_keys == NULL) {
		return NULL;
	}
	for (i = 0; i < _bucketCount; i++) {
		struct FNDictEntry *entry;

		for (entry = _buckets[i]; entry != NULL; entry = entry->next) {
			if (n >= _count) {
				break;
			}
			_keys[n++] = objc_retain(entry->key);
		}
	}
	_keyCount = n;
	return _keys;
}

- (void)dropKeySnapshot
{
	unsigned long i;

	if (_keys == NULL) {
		return;
	}
	for (i = 0; i < _keyCount; i++) {
		objc_release(_keys[i]);
	}
	free(_keys);
	_keys = NULL;
	_keyCount = 0;
}

- (unsigned long)countByEnumeratingWithState:(NSFastEnumerationState *)state
                                     objects:(id __unsafe_unretained *)buffer
                                       count:(unsigned long)length
{
	id __unsafe_unretained *keys;

	(void)buffer;
	(void)length;

	/* Enumerating a dictionary yields its KEYS (Cocoa's rule). The batch is a
	 * snapshot, because the chains are rebuilt by any mutation and a loop must
	 * not be handed storage that a mutation can free under it. */
	if (state->state != 0) {
		return 0;
	}
	keys = [self keySnapshot];
	state->itemsPtr = keys;
	state->mutationsPtr = &_mutations;
	state->state = 1;
	return _keyCount;
}

@end

@implementation NSMutableDictionary

+ (NSMutableDictionary *)dictionary
{
	return [[self alloc] init];
}

- (void)setObject:(id)value forKey:(id)key
{
	if (value == nil || key == nil) {
		return;		/* Cocoa raises; v1 has no exceptions yet (F4) */
	}
	[self setObjectInternal:value forKey:key];
}

- (void)removeObjectForKey:(id)key
{
	if (key == nil) {
		return;
	}
	[self removeObjectInternalForKey:key];
}

- (void)removeAllObjects
{
	[self removeAllInternalObjects];
}

/* `dict[k] = v` lowers to this, and `dict[k] = nil` REMOVES the key (Cocoa's
 * rule) — which is why it cannot simply forward to -setObject:forKey:. */
- (void)setObject:(id)object forKeyedSubscript:(id)key
{
	if (object == nil) {
		[self removeObjectForKey:key];
		return;
	}
	[self setObject:object forKey:key];
}

+ (NSMutableDictionary *)dictionaryWithCapacity:(NSUInteger)capacity
{
	(void)capacity;		/* the bucket array grows on demand */
	return [[self alloc] init];
}

- (id)initWithCapacity:(NSUInteger)capacity
{
	(void)capacity;
	return [super init];
}

- (void)addEntriesFromDictionary:(NSDictionary *)other
{
	/* Through allKeys, so nothing is read from a chain the insert may rebuild. */
	NSArray *keys = [other allKeys];
	NSUInteger i;

	for (i = 0; i < [keys count]; i++) {
		id key = [keys objectAtIndex:i];

		[self setObject:[other objectForKey:key] forKey:key];
	}
}

- (void)setDictionary:(NSDictionary *)other
{
	[self removeAllObjects];
	[self addEntriesFromDictionary:other];
}

- (void)removeObjectsForKeys:(NSArray *)keys
{
	NSUInteger i;

	for (i = 0; i < [keys count]; i++) {
		[self removeObjectForKey:[keys objectAtIndex:i]];
	}
}

- (id)copy
{
	/* A snapshot, and an IMMUTABLE one: the point of copying a mutable
	 * dictionary is to stop it changing. */
	return [[NSDictionary alloc] initAsCopyOf:self];
}

@end
