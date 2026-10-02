/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSDictionary.m — the hash table. See NSDictionary.h for why keys are copied.
 *
 * MANUAL OWNERSHIP: the entry slots are C storage, so they are retained and released by
 * hand with objc_retain/objc_release.
 *
 * STRUCTURE NOTE: the table's mutation (grow, insert, unlink) lives on the BASE
 * class as private methods, and the mutable class's public API is a thin wrapper
 * over them. That is not tidiness for its own sake: -copy and -mutableCopy have
 * to build an instance of the OTHER class, and they can only do that through
 * storage the base class owns.
 */

#import <Foundation/NSException.h>
#import <Foundation/NSCoder.h>	/* the NSCoding doors call the coder's methods, not just its type */
#import <Foundation/NSDictionary.h>
#import <Foundation/NSString.h>
#import <Foundation/NSArray.h>	/* the allKeys/allValues family returns one */
#import <objc/runtime.h>
#include <objc/objc-arc.h>	/* objc_retain/objc_release: the C slots are not ARC-managed */
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
/* THE KEYED ARCHIVE'S KEY NAMES, shared with NSKeyedArchiver's structural branch so the NSCoding doors below
 * and that branch cannot spell the same key differently (§63.13). */
#import <Foundation/FNKeyedWire.h>
/* THE F3 AUDIT'S ADDED DOORS lean on neighbours that already ship: the file-attribute KEY NAMES and the
 * numeric conversions they read THROUGH, and the plist/file/URL machinery the URL/error doors delegate to. */
#import <Foundation/NSFileManager.h>
#import <Foundation/NSNumber.h>
#import <Foundation/NSDate.h>
#import <Foundation/NSData.h>
#import <Foundation/NSURL.h>
#import <Foundation/NSError.h>
#import <Foundation/NSPropertyListSerialization.h>

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

/* ===================================================================================================
 * THE PRIVATE CONCRETE CLASSES (plan §C.3, M2). The same shape the array family uses, and for the same
 * reasons: Apple's article's sentence ("You don't, and can't, choose the actual class of the instance")
 * is the contract, §C.3 item 8 says the names are ours, and §C.4 is why none of them may reach an archive.
 *
 * THE STORAGE IS THE FRONT'S OWN IVARS (§C.3 item 5 is about the METHODS, not the layout), which is why
 * the mutable family needs no second declaration of them - and why THIS family's general implementation
 * stays where it is: NSMutableDictionary defines NO initializers at all and inherits every one of them
 * from the front, so the class-choosing guard below is a MEMBERSHIP test. A kind test would send every
 * mutable construction into the immutable family.
 * =================================================================================================== */

/* THE EMPTY CASE, AND IT IS A SINGLETON: one shared instance, immortal, and the answer to -init. */
@interface AGDictionaryEmpty : NSDictionary
+ (AGDictionaryEmpty *)emptyDictionary;
@end

/* THE GENERAL IMMUTABLE CASE. It adds no code: the front carries the general implementation over its own
 * storage, and this class is the NAME that implementation answers to (§C.3 item 3). */
@interface AGDictionaryItems : NSDictionary
@end

/* THE MUTABLE CASE (§C.3 item 2: a mutable constructor answers a mutable concrete class). Also a name:
 * NSMutableDictionary's own implementation is the mutable storage implementation. */
@interface AGDictionaryMutable : NSMutableDictionary
@end

@implementation NSDictionary

/* THE DOOR IS `+alloc` (§C.3 item 1), AND THERE IS NO `+allocWithZone:` IN THIS LIBRARY. A concrete class
 * INHERITS this method, and `[super alloc]` in a class method starts the lookup at NSDictionary's
 * superclass with the receiver still being the class that was asked - so the routing happens exactly ONCE,
 * at the front, and a concrete class asking for an instance gets one. */
+ (id)alloc
{
	if (self != [NSDictionary class]) {
		return [super alloc];
	}
	return [AGDictionaryItems alloc];
}

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
	/* The one-pair form with either half missing IS the empty case (the body below stores nothing). */
	if ([self isMemberOfClass:[AGDictionaryItems class]] && (value == nil || key == nil)) {
		[self release];
		return (id)[AGDictionaryEmpty emptyDictionary];
	}
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

	/* THE CLASS IS CHOSEN BY THE DATA (§C.3 item 2), for this family's general class only - a mutable
	 * receiver inherits this very implementation and must keep it. An EMPTY source copies to the shared
	 * empty instance rather than to a general one with nothing in it. */
	if ([self isMemberOfClass:[AGDictionaryItems class]] && [source count] == 0) {
		[self release];	/* never initialized: the storage was never built */
		return (id)[AGDictionaryEmpty emptyDictionary];
	}
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
	[super dealloc];	/* NSObject's -dealloc is what frees the instance */
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
	/* THE SAME TWO REFUSALS AT THE DOOR EVERY INITIALISER COMES THROUGH: -initWithObjects:forKeys:count:,
	 * -initWithDictionary: and the literal forms reach the table HERE, and a bare object filed as a key with
	 * a nil value is exactly what the attributed-string store tripped over (a FRESH table, count 0,
	 * receiving a plain NSObject key with nothing under it). A key that does not conform to NSCopying raises
	 * with a message that NAMES THE TABLE AND THE KEY, which is more than [key copy] would have said. */
	if (value == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"-[%s insert]: value must not be nil - use -removeObjectForKey:",
				   class_getName(object_getClass(self))];
	}
	if (key != nil && ![key conformsToProtocol:@protocol(NSCopying)]) {
		[NSException raise:NSInvalidArgumentException
			    format:@"-[%s insert]: key <%s> does not conform to NSCopying",
				   class_getName(object_getClass(self)), class_getName(object_getClass(key))];
	}
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

+ (NSDictionary *)dictionaryWithObjects:(NSArray *)objects forKeys:(NSArray *)keys
{
	/*
	 * TWO ARRAYS, and this is the shape the inventory corrected: the method written
	 * first was a VARIADIC +dictionaryWithObjects:, which is not Cocoa's selector at
	 * all — Cocoa's takes one array of objects and one of keys. (The variadic pair
	 * constructor is the separate +dictionaryWithObjectsAndKeys:.) Mismatched
	 * lengths are CLAMPED to the shorter rather than raising, and that is documented
	 * here because Cocoa raises.
	 */
	NSMutableDictionary *built = [[NSMutableDictionary alloc] init];
	NSUInteger n = [objects count];
	NSUInteger i;

	if ([keys count] < n) {
		n = [keys count];
	}
	for (i = 0; i < n; i++) {
		[built setObject:[objects objectAtIndex:i] forKey:[keys objectAtIndex:i]];
	}
	return built;
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

/* ===================================================================================================
 * THE NSCoding DOORS (§63.13), and this family's are the only pair that is NOT a substitution: TWO payloads,
 * written and read as a PAIR. The decoder builds through the public doors (`-setObject:forKey:` into a mutable
 * dictionary, then `-initWithDictionary:`) rather than through a C array, because the PAIRING is the point and
 * this way it is visible in the code; the funnel then applies the class-choosing rule as it does for every
 * other construction.
 * =================================================================================================== */
- (nullable instancetype)initWithCoder:(NSCoder *)coder
{
	NSArray *keys = [coder decodeObjectForKey:FNKeyedKeysKey];
	NSArray *values = [coder decodeObjectForKey:FNKeyedObjectsKey];
	NSMutableDictionary *built;
	NSUInteger n;
	NSUInteger i;

	id nothing[1] = { nil };		/* a REAL pointer with nothing in it: count is what says so */

	if (keys == nil || values == nil || (n = [keys count]) != [values count]) {
		/* NOTHING TO PAIR, and a MISMATCH is refused rather than half-read: a key with no value has no
		 * meaning in a dictionary, and the two arrays are written from one enumeration so a mismatch is a
		 * corrupt archive rather than a case with an answer. An empty (or absent) payload is the empty
		 * dictionary, through the same canonical constructor every other construction uses — and the
		 * constructor's two pointers are annotated NONNULL, so an EMPTY ARRAY is passed rather than NULL: at
		 * count zero neither is ever dereferenced, and the annotation means what it says for every count that
		 * is not zero. */
		(void)nothing;
		return [self initWithObjects:nothing forKeys:nothing count:0];
	}
	built = [NSMutableDictionary dictionaryWithCapacity:n];
	for (i = 0; i < n; i++) {
		id key = [keys objectAtIndex:i];

		if (key != nil) {
			[built setObject:[values objectAtIndex:i] forKey:key];
		}
	}
	return [self initWithDictionary:built];
}

- (void)encodeWithCoder:(NSCoder *)coder
{
	NSArray *keys = [self allKeys];
	NSMutableArray *values = [NSMutableArray arrayWithCapacity:[keys count]];
	NSUInteger i;

	/* ONE ENUMERATION, TWO ARRAYS, SO THE PAIRING CANNOT FALL OUT OF STEP — which is also why the decoder
	 * treats a length mismatch as a corrupt archive. */
	for (i = 0; i < [keys count]; i++) {
		[values addObject:[self objectForKey:[keys objectAtIndex:i]]];
	}
	[coder encodeObject:keys forKey:FNKeyedKeysKey];
	[coder encodeObject:values forKey:FNKeyedObjectsKey];
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

	/* THE CANONICAL CONSTRUCTOR, so this is where the empty case belongs: `[[NSDictionary alloc] init]`
	 * reaches it through AGDictionaryItems' -init below, and a zero count is the shared empty instance. */
	if ([self isMemberOfClass:[AGDictionaryItems class]] && count == 0) {
		[self release];
		return (id)[AGDictionaryEmpty emptyDictionary];
	}
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
	NSEnumerator *enumerator = [self keyEnumerator];
	id key;

	/* OVER THE PRIMITIVES (§C.3 item 5), which is what lets a concrete class with a DIFFERENT LAYOUT - or
	 * no storage at all, like the empty one - answer this correctly. */
	while ((key = [enumerator nextObject]) != nil) {
		[keys addObject:key];
	}
	return keys;
}

- (NSArray *)allValues
{
	NSMutableArray *values = [[NSMutableArray alloc] init];
	NSEnumerator *enumerator = [self keyEnumerator];
	id key;

	while ((key = [enumerator nextObject]) != nil) {
		id value = [self objectForKey:key];

		if (value != nil) {
			[values addObject:value];
		}
	}
	return values;
}

- (NSArray *)allKeysForObject:(id)object
{
	NSMutableArray *keys = [[NSMutableArray alloc] init];
	NSEnumerator *enumerator = [self keyEnumerator];
	id key;

	while ((key = [enumerator nextObject]) != nil) {
		id value = [self objectForKey:key];

		if (value == object || [value isEqual:object]) {
			[keys addObject:key];
		}
	}
	return keys;
}

- (NSArray *)keysSortedByValueUsingSelector:(SEL)comparator
{
	NSArray *keys = [self allKeys];

	return [keys sortedArrayUsingComparator:^NSComparisonResult(id left, id right) {
		return ((NSComparisonResult (*)(id, SEL, id))objc_msgSend)(
			[self objectForKey:left], comparator, [self objectForKey:right]);
	}];
}

- (NSArray *)keysSortedByValueUsingComparator:(NSComparator)comparator
{
	NSArray *keys = [self allKeys];

	if (comparator == NULL) {
		return keys;
	}
	return [keys sortedArrayUsingComparator:^NSComparisonResult(id left, id right) {
		return comparator([self objectForKey:left], [self objectForKey:right]);
	}];
}

- (void)enumerateKeysAndObjectsUsingBlock:(void (^)(id key, id value, BOOL *stop))block
{
	/* Over the key SNAPSHOT: the chains are rebuilt by any mutation, and a block
	 * that mutates the dictionary must not be handed storage that is freed under
	 * it — the same rule -countByEnumeratingWithState: follows. */
	NSArray *keys = [self allKeys];
	NSUInteger i;
	BOOL stop = NO;

	if (block == NULL) {
		return;
	}
	for (i = 0; i < [keys count]; i++) {
		id key = [keys objectAtIndex:i];

		block(key, [self objectForKey:key], &stop);
		if (stop) {
			break;
		}
	}
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
	NSEnumerator *enumerator = [self keyEnumerator];
	unsigned long n = 0;
	id key;

	/* The PAIRING is the contract (Cocoa fixes no order), and it holds here because both sides are read
	 * for the same key in the same step. */
	while ((key = [enumerator nextObject]) != nil) {
		if (keys != NULL) {
			keys[n] = key;
		}
		if (objects != NULL) {
			objects[n] = [self objectForKey:key];
		}
		n++;
	}
}

- (NSEnumerator *)keyEnumerator
{
	/*
	 * THE PRIMITIVE (§C.3 item 5), AND IT MUST NOT GO THROUGH -allKeys. -allKeys is now written OVER this
	 * method, so asking it here was MUTUAL RECURSION - measured rather than feared: the guest overflowed
	 * its stack inside dict-constructors and the kernel dumped the process maps, with [stack] in them.
	 * A primitive is allowed to read the class's own storage, which is what this does; the enumerator then
	 * holds a SNAPSHOT, so a loop is never handed storage that a mutation can free under it.
	 */
	[self keySnapshot];
	return [[NSEnumerator alloc] initWithSequence:[NSArray arrayWithObjects:_keys count:_keyCount]
					       reverse:NO];
}

- (NSEnumerator *)objectEnumerator
{
	return [[NSEnumerator alloc] initWithSequence:[self allValues] reverse:NO];
}

- (BOOL)isEqualToDictionary:(NSDictionary *)other
{
	NSEnumerator *enumerator;
	id key;

	if (other == nil) {
		return NO;
	}
	if (other == self) {
		return YES;
	}
	if ([other count] != [self count]) {
		return NO;
	}
	enumerator = [self keyEnumerator];
	while ((key = [enumerator nextObject]) != nil) {
		id mine = [self objectForKey:key];
		id theirs = [other objectForKey:key];

		if (mine == theirs) {
			continue;
		}
		if (theirs == nil || ![mine isEqual:theirs]) {
			return NO;
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
	NSEnumerator *enumerator = [self keyEnumerator];
	id key;

	/* THE SUM IS COMMUTATIVE, so reading the pairs through the enumerator instead of the buckets cannot
	 * change this value - which is what keeps -hash consistent with equality for every concrete class. */
	h ^= [self count];
	h *= 16777619UL;
	while ((key = [enumerator nextObject]) != nil) {
		id value = [self objectForKey:key];

		h += ([key hash] ^ [value hash]);
	}
	return h;
}

- (NSString *)description
{
	NSMutableString *out = [[NSMutableString alloc] initWithUTF8String:"{"];
	NSEnumerator *enumerator = [self keyEnumerator];
	id key;
	int first = 1;

	while ((key = [enumerator nextObject]) != nil) {
		id value = [self objectForKey:key];

		if (!first) {
			[out appendString:@", "];
		}
		first = 0;
		[out appendString:[key description]];
		[out appendString:@" = "];
		[out appendString:[value description]];
	}
	[out appendString:@"}"];
	return out;
}

/* §C.3 item 4, on the immutable front: an archiver asks for THIS, never for -class, so the PUBLIC name
 * is what an archive holds and no private concrete name can appear in one (§C.4). */
- (Class)classForCoder
{
	return [NSDictionary class];
}

- (id)copy
{
	return [self retain];	/* +1: `copy` is an OWNED family (plan §15.2) — immutable */
}

- (id)mutableCopy
{
	return [[NSMutableDictionary alloc] initAsCopyOf:self];
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
	unsigned long cursor = state->state;
	unsigned long produced = 0;
	unsigned long skip;
	NSEnumerator *enumerator = [self keyEnumerator];
	id key;

	/* Enumerating a dictionary yields its KEYS (Cocoa's rule), AND THIS GOES OVER THE PRIMITIVES THROUGH
	 * THE CALLER'S OWN BUFFER. It used to hand out the class's internal key snapshot in one batch, which
	 * is storage a concrete class with a different layout does not have; `objects` is caller-provided
	 * scratch that stays valid for the batch, which is what the protocol says it is for. `state->state` is
	 * the cursor, so a batch smaller than the count is RESUMED: the enumerator is re-walked and the cursor
	 * skipped. */
	for (skip = 0; skip < cursor && (key = [enumerator nextObject]) != nil; skip++) {
	}
	while (produced < length && (key = [enumerator nextObject]) != nil) {
		buffer[produced++] = key;
	}
	if (produced == 0) {
		return 0;
	}
	state->itemsPtr = buffer;
	state->mutationsPtr = &_mutations;
	state->state = cursor + produced;
	return produced;
}

/* ===================================================================================================
 * THE TWO-ARRAY AND COPY-ITEMS CONSTRUCTORS (F3 audit). Both funnel through -setObjectInternal:forKey:, so
 * the key-copy and the nil refusals are the table's OWN and cannot be spelled differently here.
 * =================================================================================================== */
- (id)initWithObjects:(NSArray *)objects forKeys:(NSArray *)keys
{
	NSUInteger n = [objects count];
	NSUInteger i;

	if ([keys count] < n) {
		n = [keys count];	/* clamped to the shorter, as the class factory above is */
	}
	self = [self init];
	if (self == nil) {
		return nil;
	}
	for (i = 0; i < n; i++) {
		[self setObjectInternal:[objects objectAtIndex:i] forKey:[keys objectAtIndex:i]];
	}
	return self;
}

- (id)initWithDictionary:(NSDictionary *)other copyItems:(BOOL)flag
{
	NSArray *keys = [other allKeys];
	NSUInteger i;

	self = [self init];
	if (self == nil) {
		return nil;
	}
	for (i = 0; i < [keys count]; i++) {
		id key = [keys objectAtIndex:i];
		id value = [other objectForKey:key];
		id copied = flag ? [value copy] : nil;

		/* A value that does not answer -copy (answers nil in this runtime) is held BY REFERENCE rather
		 * than filed as the copy that never came — the same refusal of a phantom the key path makes. The
		 * copy's +1 is balanced here; the table keeps its own retain. */
		[self setObjectInternal:(copied != nil ? copied : value) forKey:key];
		if (copied != nil) {
			objc_release(copied);
		}
	}
	return self;
}

- (void)getObjects:(id __unsafe_unretained *)objects andKeys:(id __unsafe_unretained *)keys count:(NSUInteger)count
{
	/* Bounded by `count`: a caller sizes its buffers from -count, and this walk writes at most that many
	 * pairs over the same key primitive the unbounded form uses. */
	NSEnumerator *enumerator = [self keyEnumerator];
	unsigned long n = 0;
	id key;

	while (n < count && (key = [enumerator nextObject]) != nil) {
		if (keys != NULL) {
			keys[n] = key;
		}
		if (objects != NULL) {
			objects[n] = [self objectForKey:key];
		}
		n++;
	}
}

/* ===================================================================================================
 * THE OPTIONS-ENUMERATION AND ENTRY-FILTER DOORS (F3 audit), over the SAME key snapshot the plain block
 * form uses so a mutating block is never handed storage that is freed under it.
 * =================================================================================================== */
- (void)enumerateKeysAndObjectsWithOptions:(NSEnumerationOptions)opts
				usingBlock:(void (^)(id key, id value, BOOL *stop))block
{
	NSArray *keys = [self allKeys];
	BOOL stop = NO;
	NSUInteger i;

	if (block == NULL) {
		return;
	}
	/* NSEnumerationConcurrent is a hint Apple's page says a caller must not rely on; this library ignores it
	 * and walks serially, which is what NSEnumerationReverse does NOT relax. */
	if (opts & NSEnumerationReverse) {
		for (i = [keys count]; i > 0 && !stop; i--) {
			id key = [keys objectAtIndex:i - 1];

			block(key, [self objectForKey:key], &stop);
		}
		return;
	}
	for (i = 0; i < [keys count] && !stop; i++) {
		id key = [keys objectAtIndex:i];

		block(key, [self objectForKey:key], &stop);
	}
}

- (NSArray *)keysOfEntriesPassingTest:(BOOL (^)(id key, id value, BOOL *stop))predicate
{
	return [self keysOfEntriesWithOptions:0 passingTest:predicate];
}

- (NSArray *)keysOfEntriesWithOptions:(NSEnumerationOptions)opts passingTest:(BOOL (^)(id key, id value, BOOL *stop))predicate
{
	NSMutableArray *matched = [[NSMutableArray alloc] init];
	NSArray *keys = [self allKeys];
	BOOL stop = NO;
	NSUInteger i;

	if (predicate == NULL) {
		return matched;
	}
	if (opts & NSEnumerationReverse) {
		for (i = [keys count]; i > 0 && !stop; i--) {
			id key = [keys objectAtIndex:i - 1];

			if (predicate(key, [self objectForKey:key], &stop)) {
				[matched addObject:key];
			}
		}
		return matched;
	}
	for (i = 0; i < [keys count] && !stop; i++) {
		id key = [keys objectAtIndex:i];

		if (predicate(key, [self objectForKey:key], &stop)) {
			[matched addObject:key];
		}
	}
	return matched;
}

- (NSArray *)keysSortedByValueWithOptions:(NSSortOptions)opts usingComparator:(NSComparator)comparator
{
	/* The sort the called-through method already uses KEEPS NSSortStable (NSObjCRuntime.h's note), so the
	 * only option Apple defines a meaning for here is already honoured; NSSortConcurrent is a hint nothing
	 * in this library takes. */
	(void)opts;
	return [self keysSortedByValueUsingComparator:comparator];
}

/* ===================================================================================================
 * THE URL/ERROR DOORS (F3 audit). The path forms live in the plist skin; these take an NSError so a caller
 * can see WHY, and they delegate to the same NSPropertyListSerialization endpoints and the same root-class
 * refusal (D7's kind (D)) the skin uses.
 * =================================================================================================== */
- (nullable instancetype)initWithContentsOfURL:(NSURL *)url error:(NSError **)error
{
	NSData *data = [NSData dataWithContentsOfURL:url];
	id plist;

	if (data == nil) {
		if (error != NULL) {
			*error = [NSError errorWithDomain:NSCocoaErrorDomain
						     code:NSFileReadNoSuchFileError
						 userInfo:nil];
		}
		return nil;
	}
	plist = [NSPropertyListSerialization propertyListWithData:data
							  options:NSPropertyListImmutable
							   format:NULL
							    error:error];
	if (plist == nil) {
		return nil;	/* the parse error is already in *error */
	}
	if (![plist isKindOfClass:[NSDictionary class]]) {
		/* THE ROOT-CLASS REFUSAL: a valid plist whose root is another kind is not this constructor's answer,
		 * and it is REPORTED rather than coerced or half-read. */
		if (error != NULL) {
			*error = [NSError errorWithDomain:NSCocoaErrorDomain
						     code:NSFileReadCorruptFileError
						 userInfo:nil];
		}
		return nil;
	}
	return [self initWithDictionary:plist];
}

+ (NSDictionary *)dictionaryWithContentsOfURL:(NSURL *)url error:(NSError **)error
{
	return [[self alloc] initWithContentsOfURL:url error:error];
}

- (BOOL)writeToURL:(NSURL *)url error:(NSError **)error
{
	NSData *data = [NSPropertyListSerialization dataWithPropertyList:self
								  format:NSPropertyListXMLFormat_v1_0
								 options:0
								   error:error];

	if (data == nil) {
		return NO;
	}
	return [data writeToURL:url options:NSDataWritingAtomic error:error];
}

@end

@implementation NSMutableDictionary

/* THE SAME DOOR (§C.3 item 1) - it is what makes a mutable constructor answer a mutable concrete class -
 * and the mutable front answers ITSELF to an archiver, which is the bullet that keeps the private names
 * out of every archive (§C.4). */
+ (id)alloc
{
	if (self != [NSMutableDictionary class]) {
		return [super alloc];
	}
	return [AGDictionaryMutable alloc];
}

- (Class)classForCoder
{
	return [NSMutableDictionary class];
}

+ (NSMutableDictionary *)dictionary
{
	return [[self alloc] init];
}

- (void)setObject:(id)value forKey:(id)key
{
	/* APPLE'S CONTRACT, NOW THAT THE EXCEPTIONS IT WAS WAITING FOR ARE HERE. The v1 comment read "Cocoa
	 * raises; v1 has no exceptions yet (F4)" - and F4 brought them, so the deferral is due: a NIL VALUE
	 * raises NSInvalidArgumentException (Apple: "use -removeObjectForKey:"), and so does a NIL KEY, rather
	 * than a silent drop, which is how a caller loses an entry and never learns. */
	if (value == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"-[%s setObject:forKey:]: value must not be nil - use -removeObjectForKey:",
				   class_getName(object_getClass(self))];
	}
	if (key == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"-[%s setObject:forKey:]: key must not be nil",
				   class_getName(object_getClass(self))];
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

/* THE SAME DOOR ON THE MUTABLE CLASS (§63.13), declared in its own block and therefore needing its own body:
 * `--unimplemented` counts an implementation in the class or a SUBCLASS, so the front's does not satisfy it.
 * `[super initWithCoder:]` reaches the front's, which funnels through `-initWithDictionary:` with `self` still
 * the MUTABLE class — and the class-choosing rule sends only the immutable concrete class to the shared empty
 * instance, so a mutable answer stays mutable. */
- (nullable instancetype)initWithCoder:(NSCoder *)coder
{
	return [super initWithCoder:coder];
}

- (void)addEntriesFromDictionary:(NSDictionary *)other
{
	/* Through allKeys, so nothing is read from a chain the insert may rebuild. */
	NSArray *keys = [other allKeys];
	NSUInteger i;

	for (i = 0; i < [keys count]; i++) {
		id key = [keys objectAtIndex:i];
		id value = [other objectForKey:key];

		[self setObject:value forKey:key];
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


/* ===================================================================================================
 * THE CONCRETE CLASSES (§C.3 items 2, 3 and 8).
 * =================================================================================================== */

@implementation AGDictionaryItems

/* THIS CLASS DELIBERATELY DOES NOT OVERRIDE -init, AND THE ARRAY FAMILY DOES - MEASURED DIFFERENCE, NOT AN
 * OVERSIGHT. `[[NSDictionary alloc] init]` must answer an EMPTY instance (§C.3 item 1), and here that is a
 * PLAIN one, because this family has ALLOCATE-THEN-FILL constructors: the nil-terminated pair form and the
 * variadic core below allocate, call -init, and then store into the instance. An -init that answered the
 * SHARED empty singleton would capture those stores into the singleton itself - the first such dictionary
 * would fill it and every later one would inherit its contents. The probe caught exactly that: three
 * existing dictionary checks failed (dict-constructors, dictionary-plist-file, dictionary-plist-url) while
 * every new cluster check passed.
 *
 * SO THE SINGLETON BELONGS TO THE COMPLETE CONSTRUCTIONS, where the data is already known:
 * -initWithObjects:forKeys:count: with a zero count, and -initAsCopyOf: with an empty source. Both are
 * whole answers, so neither can be swept out from under a later store. */

@end

@implementation AGDictionaryEmpty

+ (AGDictionaryEmpty *)emptyDictionary
{
	static AGDictionaryEmpty *shared = nil;

	if (shared == nil) {
		shared = [[AGDictionaryEmpty alloc] init];
	}
	return shared;
}

/* IMMORTAL, the price of a singleton in a library with no `+allocWithZone:` and no collector. */
- (id)retain { return self; }
- (void)release { }
- (id)autorelease { return self; }
- (NSUInteger)retainCount { return NSUIntegerMax; }

/* THE THREE PRIMITIVES (§C.3 item 5): -count, -objectForKey: and -keyEnumerator. Everything else in this
 * family is written over them, so an empty dictionary answers -allKeys, -allValues, -hash, -description,
 * -isEqualToDictionary;, -getObjects:andKeys: and fast enumeration correctly with no code here. */
- (unsigned long)count
{
	return 0;
}

- (id)objectForKey:(id)key
{
	(void)key;
	return nil;
}

- (NSEnumerator *)keyEnumerator
{
	return [[NSArray array] objectEnumerator];
}

- (unsigned long)countByEnumeratingWithState:(NSFastEnumerationState *)state
                                     objects:(id __unsafe_unretained *)buffer
                                       count:(unsigned long)length
{
	(void)buffer;
	(void)length;
	state->mutationsPtr = &state->extra[0];
	return 0;
}

@end

@implementation AGDictionaryMutable

/* NOTHING TO IMPLEMENT, AND THAT IS THE POINT: NSMutableDictionary's own implementation IS the mutable
 * storage implementation, and what a caller gains is the NAME that -class answers. */

@end

/* ===================================================================================================
 * NSFileAttributes (F3 audit): each accessor reads ONE key of a file-attributes dictionary and converts it.
 * An ABSENT key reads as 0 / NO / nil through the same nil-tolerant -objectForKey:, which is exactly what a
 * file system that has no such attribute (this kernel's has no HFS creator code) reports.
 * =================================================================================================== */
@implementation NSDictionary (NSFileAttributes)

- (NSDate *)fileCreationDate
{
	return [self objectForKey:NSFileCreationDate];
}

- (BOOL)fileExtensionHidden
{
	return [[self objectForKey:NSFileExtensionHidden] boolValue];
}

- (NSNumber *)fileGroupOwnerAccountID
{
	return [self objectForKey:NSFileGroupOwnerAccountID];
}

- (NSString *)fileGroupOwnerAccountName
{
	return [self objectForKey:NSFileGroupOwnerAccountName];
}

- (unsigned int)fileHFSCreatorCode
{
	return [[self objectForKey:NSFileHFSCreatorCode] unsignedIntValue];
}

- (unsigned int)fileHFSTypeCode
{
	return [[self objectForKey:NSFileHFSTypeCode] unsignedIntValue];
}

- (BOOL)fileIsAppendOnly
{
	return [[self objectForKey:NSFileAppendOnly] boolValue];
}

- (BOOL)fileIsImmutable
{
	return [[self objectForKey:NSFileImmutable] boolValue];
}

- (NSDate *)fileModificationDate
{
	return [self objectForKey:NSFileModificationDate];
}

- (NSNumber *)fileOwnerAccountID
{
	return [self objectForKey:NSFileOwnerAccountID];
}

- (NSString *)fileOwnerAccountName
{
	return [self objectForKey:NSFileOwnerAccountName];
}

- (NSUInteger)filePosixPermissions
{
	return [[self objectForKey:NSFilePosixPermissions] unsignedLongValue];
}

- (unsigned long long)fileSize
{
	return [[self objectForKey:NSFileSize] unsignedLongLongValue];
}

- (NSInteger)fileSystemFileNumber
{
	return [[self objectForKey:NSFileSystemFileNumber] integerValue];
}

- (NSInteger)fileSystemNumber
{
	return [[self objectForKey:NSFileSystemNumber] integerValue];
}

- (NSString *)fileType
{
	return [self objectForKey:NSFileType];
}

@end


/* ================== THE SHARED-KEY-SET PAIR (§63.78) ==================
 * See the note in the header: Apple declares no such CLASS, the token is opaque, and the only stated contract is
 * that the second door throws unless it is handed what the first door answered. THIS TOKEN IS THE MINIMUM THAT
 * SATISFIES THAT CONTRACT — it COPIES the keys (Apple: "the keys are copied from the array and must be
 * copyable") and forgets the duplicates (Apple: "may contain duplicates, which are ignored"). */
@interface FNSharedKeySet : NSObject
{
	NSArray *_keys;		/* retained: the copied, de-duplicated key set */
}
- (id)initWithKeys:(NSArray *)keys;
- (NSUInteger)fnKeyCount;
@end

@implementation FNSharedKeySet

- (id)initWithKeys:(NSArray *)keys
{
	self = [super init];
	if (self != nil) {
		_keys = [keys copy];
	}
	return self;
}

- (NSUInteger)fnKeyCount
{
	return [_keys count];
}

- (void)dealloc
{
	[_keys release];
	[super dealloc];
}

@end

@implementation NSDictionary (NSSharedKeySetDictionary)

+ (id)sharedKeySetForKeys:(NSArray *)keys
{
	NSMutableArray *unique;
	NSUInteger i;

	/* APPLE'S CONTRACT CLAUSE BY CLAUSE: nil or not-an-array RAISES, duplicates are IGNORED, and an empty array
	 * answers an EMPTY KEY SET rather than an error. */
	if (keys == nil || ![keys isKindOfClass:[NSArray class]]) {
		[NSException raise:NSInvalidArgumentException
			    format:@"+[NSDictionary sharedKeySetForKeys:] needs an NSArray of keys"];
		return nil;
	}
	unique = [NSMutableArray array];
	for (i = 0; i < [keys count]; i++) {
		id key = [keys objectAtIndex:i];

		if ([unique indexOfObject:key] == NSNotFound) {
			[unique addObject:key];
		}
	}
	return [[[FNSharedKeySet alloc] initWithKeys:unique] autorelease];
}

@end

@implementation NSMutableDictionary (NSSharedKeySetDictionary)

+ (NSMutableDictionary *)dictionaryWithSharedKeySet:(id)keyset
{
	/* BOTH OF APPLE'S REFUSALS ARE HERE, AND THEY ARE THE WHOLE OF THIS DOOR'S CONTRACT: nil raises, and so
	 * does anything that is not what `+sharedKeySetForKeys:` answers. */
	if (keyset == nil || ![keyset isKindOfClass:[FNSharedKeySet class]]) {
		[NSException raise:NSInvalidArgumentException
			    format:@"+[NSMutableDictionary dictionaryWithSharedKeySet:] needs an object answered by "
				   @"+[NSDictionary sharedKeySetForKeys:]"];
		return nil;
	}
	return [[[NSMutableDictionary alloc] initWithCapacity:[(FNSharedKeySet *)keyset fnKeyCount]] autorelease];
}

@end
