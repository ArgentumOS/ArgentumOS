/*
 * nsarray.m — the ordered collection.
 *
 * ARC file: it owns its storage but implements no -retain/-release, so the
 * slots are managed by hand with objc_retain/objc_release.
 */

#import <foundation/NSArray.h>
#import <foundation/NSString.h>
#import <objc/runtime.h>
#include <objc/objc-arc.h>	/* objc_retain/objc_release: the C slots are not ARC-managed */
#include <stdlib.h>

/* Grow to hold at least `needed`, never shrinking. */
static id *array_grow(id *items, unsigned long *capacity, unsigned long needed)
{
	unsigned long grown = (*capacity == 0) ? 4 : *capacity;

	while (grown < needed) {
		grown *= 2;
	}
	items = (id *)realloc(items, grown * sizeof(id));
	if (items == NULL) {
		return NULL;
	}
	*capacity = grown;
	return items;
}

@implementation NSArray

+ (NSArray *)array
{
	return [[self alloc] initWithObjects:NULL count:0];
}

+ (NSArray *)arrayWithObject:(id)object
{
	return [[self alloc] initWithObject:object];
}

+ (NSArray *)arrayWithObjects:(const id *)objects count:(unsigned long)count
{
	return [[self alloc] initWithObjects:objects count:count];
}

- (id)initWithObject:(id)object
{
	return [self initWithObjects:&object count:1];
}

- (id)initWithObjects:(const id *)objects count:(unsigned long)count
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_count = count;
	_capacity = count;
	_items = NULL;
	if (count > 0) {
		unsigned long i;

		_items = (id *)calloc(count, sizeof(id));
		if (_items == NULL) {
			_count = 0;
			_capacity = 0;
			return nil;
		}
		for (i = 0; i < count; i++) {
			_items[i] = objc_retain(objects[i]);
		}
	}
	return self;
}

- (void)dealloc
{
	unsigned long i;

	for (i = 0; i < _count; i++) {
		objc_release(_items[i]);
	}
	free(_items);
}

- (unsigned long)count
{
	return _count;
}

- (id)objectAtIndex:(unsigned long)index
{
	/* Out of range is nil rather than an exception: v1 has no exception
	 * objects yet (F4), and a silent nil is checkable where a crash is not. */
	if (index >= _count) {
		return nil;
	}
	return _items[index];
}

- (id)firstObject
{
	return (_count == 0) ? nil : _items[0];
}

- (id)lastObject
{
	return (_count == 0) ? nil : _items[_count - 1];
}

- (unsigned long)indexOfObject:(id)object
{
	unsigned long i;

	for (i = 0; i < _count; i++) {
		if (_items[i] == object || [_items[i] isEqual:object]) {
			return i;
		}
	}
	return (unsigned long)-1;
}

- (BOOL)containsObject:(id)object
{
	return [self indexOfObject:object] != (unsigned long)-1;
}

- (NSArray *)arrayByAddingObject:(id)object
{
	NSMutableArray *copy = [[NSMutableArray alloc] initWithObjects:_items count:_count];

	[copy addObject:object];
	return copy;
}

- (BOOL)isEqualToArray:(NSArray *)other
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
	for (i = 0; i < _count; i++) {
		id a = _items[i];
		id b = [other objectAtIndex:i];

		if (a == b) {
			continue;
		}
		if (a == nil || b == nil || ![a isEqual:b]) {
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
	if (other == nil || ![other isKindOfClass:[NSArray class]]) {
		return NO;
	}
	return [self isEqualToArray:(NSArray *)other];
}

- (unsigned long)hash
{
	/* Order-dependent, matching equality: two arrays are equal only in the
	 * same order, so the fold must be too. */
	unsigned long h = 2166136261UL;
	unsigned long i;

	h ^= _count;
	h *= 16777619UL;
	for (i = 0; i < _count; i++) {
		h ^= [_items[i] hash];
		h *= 16777619UL;
	}
	return h;
}

- (NSString *)description
{
	NSMutableString *out = [[NSMutableString alloc] initWithUTF8String:"("];
	unsigned long i;

	/* One line, elements joined by ", " — not Cocoa's multi-line form, which
	 * is a display convention rather than a contract. */
	for (i = 0; i < _count; i++) {
		if (i > 0) {
			[out appendString:@", "];
		}
		[out appendString:[_items[i] description]];
	}
	[out appendString:@")"];
	return out;
}

- (id)copy
{
	return self;		/* immutable */
}

- (id)mutableCopy
{
	return [[NSMutableArray alloc] initWithObjects:_items count:_count];
}

/* NSCopying keeps Cocoa's SHAPE with the zone accepted and ignored (the plan's
 * §7 decision): there are no zones, but the selector stays for compatibility. */
- (id)copyWithZone:(void *)zone
{
	(void)zone;
	return [self copy];
}

- (unsigned long)countByEnumeratingWithState:(NSFastEnumerationState *)state
                                     objects:(id __unsafe_unretained *)buffer
                                       count:(unsigned long)length
{
	(void)buffer;
	(void)length;

	/*
	 * ONE BATCH, and the batch is our OWN storage: an array's elements are
	 * stable for its lifetime (only a mutable array moves them, and that is
	 * what the mutation word below is for), so there is nothing to copy and
	 * nothing to keep in `buffer`.
	 */
	if (state->state != 0) {
		return 0;
	}
	state->itemsPtr = _items;
	state->mutationsPtr = &_mutations;
	state->state = 1;
	return _count;
}

@end

@implementation NSMutableArray

+ (NSMutableArray *)array
{
	return [[self alloc] init];
}

+ (NSMutableArray *)arrayWithCapacity:(unsigned long)capacity
{
	return [[self alloc] initWithCapacity:capacity];
}

- (id)initWithCapacity:(unsigned long)capacity
{
	self = [super init];
	if (self != nil && capacity > 0) {
		_items = (id *)calloc(capacity, sizeof(id));
		if (_items == NULL) {
			return nil;
		}
		_capacity = capacity;
	}
	return self;
}

- (void)addObject:(id)object
{
	[self insertObject:object atIndex:_count];
}

- (void)insertObject:(id)object atIndex:(unsigned long)index
{
	if (index > _count) {
		return;
	}
	if (_count + 1 > _capacity) {
		id *grown = array_grow(_items, &_capacity, _count + 1);

		if (grown == NULL) {
			return;
		}
		_items = grown;
	}
	if (index < _count) {
		unsigned long i;

		for (i = _count; i > index; i--) {
			_items[i] = _items[i - 1];
		}
	}
	_items[index] = objc_retain(object);
	_count++;
	_mutations++;
}

- (void)removeObjectAtIndex:(unsigned long)index
{
	unsigned long i;

	if (index >= _count) {
		return;
	}
	objc_release(_items[index]);
	for (i = index; i + 1 < _count; i++) {
		_items[i] = _items[i + 1];
	}
	_count--;
	_items[_count] = NULL;
	_mutations++;
}

- (void)removeAllObjects
{
	unsigned long i;

	for (i = 0; i < _count; i++) {
		objc_release(_items[i]);
		_items[i] = NULL;
	}
	_count = 0;
	_mutations++;
}

- (id)copy
{
	/* A snapshot, like every other mutable type here. */
	return [[NSArray alloc] initWithObjects:_items count:_count];
}

@end
