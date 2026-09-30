/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSArray.m — the ordered collection.
 *
 * MANUAL OWNERSHIP: it owns its storage but implements no -retain/-release, so the
 * slots are managed by hand with objc_retain/objc_release.
 */

#import <Foundation/NSArray.h>
#import <Foundation/NSCoder.h>	/* the NSCoding doors call the coder's methods, not just its type */
#import <Foundation/NSURL.h>
#import <Foundation/NSData.h>
#import <Foundation/NSPropertyListSerialization.h>
#import <Foundation/NSString.h>
#import <Foundation/NSException.h>
#import <Foundation/NSIndexSet.h>
#import <Foundation/NSSortDescriptor.h>
#import <Foundation/NSPredicate.h>
#import <objc/runtime.h>
#include <objc/objc-arc.h>	/* objc_retain/objc_release: the C slots are not ARC-managed */
#include <stdlib.h>
/* THE KEYED ARCHIVE'S KEY NAMES, shared with NSKeyedArchiver's structural branch so the NSCoding doors below
 * and that branch cannot spell the same key differently (§63.12). */
#import <Foundation/FNKeyedWire.h>

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

/* ===================================================================================================
 * THE PRIVATE CONCRETE CLASSES (plan §C.3, M1). Apple's article's sentence - "You don't, and can't,
 * choose the actual class of the instance" - is the shape: the public class is the FRONT, and these are
 * the classes it answers with. §C.3 item 8: Apple does not publish its names, so ours are a free choice,
 * they are named HERE and in no header, and §C.4 is the reason none of them may ever reach an archive
 * (-classForCoder answers the front, and an archiver asks for THAT).
 *
 * THE STORAGE IS THE FRONT'S OWN IVARS. §C.3 item 5 is about the METHODS, not the layout: every class
 * below that wants the general layout inherits _items/_count/_capacity/_mutations from NSArray, which is
 * why the mutable family declares no second copy of them.
 * =================================================================================================== */

/* THE SMALL-CASE BOUNDARY. Apple publishes no number (its own small case is a single-element class), so
 * this is OURS, stated in one place, and the probe asserts the boundary from both sides. */
#define AG_ARRAY_SMALL_MAX 8

/* THE EMPTY CASE, AND IT IS A SINGLETON: one shared instance, immortal, and the answer to -init. */
@interface AGArrayEmpty : NSArray
+ (AGArrayEmpty *)emptyArray;
@end

/* THE ONE-ELEMENT CASE: the object IS the storage, so there is no heap block and no capacity. */
@interface AGArrayOne : NSArray
{
	id __unsafe_unretained _one;
}
- (id)initWithObject:(id)object;
@end

/* THE SMALL CASE: inline storage for AG_ARRAY_SMALL_MAX elements, beyond which the general class takes
 * over. */
@interface AGArraySmall : NSArray
{
	id __unsafe_unretained _small[AG_ARRAY_SMALL_MAX];
	unsigned long _smallCount;
}
- (id)initWithObjects:(const id *)objects count:(unsigned long)count;
@end

/* THE GENERAL IMMUTABLE CASE. It adds no code: the front carries the general implementation over its own
 * storage (see the note at the top of this file), and this class is the NAME that implementation answers
 * to, so `[[NSArray array] class] != [NSArray class]` holds for the general case too (§C.3 item 3). */
@interface AGArrayItems : NSArray
@end

/* THE MUTABLE CASE (§C.3 item 2: "a mutable constructor answers a mutable concrete class"). Also a name:
 * NSMutableArray's own implementation is the mutable storage implementation. It is a SUBCLASS of
 * NSMutableArray, so -isKindOfClass:, the mutable doors and the inherited layout all hold. */
@interface AGArrayMutable : NSMutableArray
@end

@implementation NSArray

/* THE DOOR IS `+alloc` (§C.3 item 1), AND THAT IS NOT A CHOICE HERE: this library has no `+allocWithZone:`
 * (NSObject.h says so and says why), so `+alloc` is the one place a cluster can substitute a class.
 *
 * THE `self != [NSArray class]` TEST IS NOT A SMUGGLE. A concrete class INHERITS this method, and
 * `[super alloc]` in a class method starts the lookup at NSArray's superclass with the receiver still
 * being the class that was asked - so the routing happens exactly ONCE, at the front, and a concrete
 * class asking for an instance gets one. */
+ (id)alloc
{
	if (self != [NSArray class]) {
		return [super alloc];
	}
	return [AGArrayItems alloc];
}

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
	/*
	 * THE CLASS IS CHOSEN BY THE DATA (§C.3 item 2), AND ONLY FOR THIS FAMILY'S GENERAL CLASS. The guard
	 * is a MEMBERSHIP test rather than a kind test because a mutable receiver (AGArrayMutable, through
	 * NSMutableArray's +alloc) inherits this very implementation and must keep it: its layout is the same
	 * and its family chooses no class at all. `objects != NULL` guards the two NON-EMPTY cases only: a
	 * nonzero count with no storage is a caller error that must still reach the bounds check rather than
	 * become a null dereference.
	 */
	if ([self isMemberOfClass:[AGArrayItems class]]) {
		if (count == 0) {
			/*
			 * THE EMPTY CASE IS FIRST AND IT EXPECTS NO STORAGE POINTER: `+array` calls this with NULL and
			 * count 0. The probe caught the earlier version, whose guard read `objects != NULL && ...` -
			 * which sent the empty case down the GENERAL path, so `[NSArray array]` answered the general
			 * class instead of the shared empty instance and the four cases stopped being four.
			 */
			[self release];	/* never initialized: the storage was never built */
			return [AGArrayEmpty emptyArray];
		}
		if (objects != NULL && count == 1) {
			[self release];
			return [[AGArrayOne alloc] initWithObject:objects[0]];
		}
		if (objects != NULL && count <= AG_ARRAY_SMALL_MAX) {
			[self release];
			return [[AGArraySmall alloc] initWithObjects:objects count:count];
		}
	}
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
	[super dealloc];	/* NSObject's -dealloc is what frees the instance */
}

- (unsigned long)count
{
	return _count;
}

- (id)objectAtIndex:(unsigned long)index
{
	/*
	 * D10, AND COCOA RAISES HERE. The comment this replaces said "out of range is nil rather than an
	 * exception: v1 has no exception objects yet (F4)" - a reason that EXPIRED AT F4 and was left
	 * standing for several milestones, so a programming error travelled as a wrong answer instead of
	 * stopping at the line that made it. The behaviour was pinned by a check precisely so that
	 * changing it had to be deliberate; the check now asserts the raise.
	 */
	if (index >= _count) {
		if (_count == 0) {
			[NSException raise:NSRangeException
			            format:@"-[NSArray objectAtIndex:]: index %lu beyond bounds for empty array",
			                   index];
		}
		[NSException raise:NSRangeException
		            format:@"-[NSArray objectAtIndex:]: index %lu beyond bounds [0 .. %lu]",
		                   index, _count - 1];
	}
	return _items[index];
}

/* Cocoa's subscript: `array[0]` lowers to this. */
- (id)objectAtIndexedSubscript:(NSUInteger)index
{
	return [self objectAtIndex:index];
}

- (id)firstObject
{
	return ([self count] == 0) ? nil : [self objectAtIndex:0];
}

- (id)lastObject
{
	unsigned long n = [self count];

	return (n == 0) ? nil : [self objectAtIndex:n - 1];
}

- (unsigned long)indexOfObject:(id)object
{
	unsigned long n = [self count];
	unsigned long i;

	/* OVER THE PRIMITIVES (§C.3 item 5) - which is what makes a concrete class with INLINE storage, and
	 * therefore none of this class's ivars, work through every door below. */
	for (i = 0; i < n; i++) {
		id element = [self objectAtIndex:i];

		if (element == object || [element isEqual:object]) {
			return i;
		}
	}
	return NSNotFound;
}

- (BOOL)containsObject:(id)object
{
	return [self indexOfObject:object] != NSNotFound;
}

- (NSArray *)arrayByAddingObject:(id)object
{
	NSMutableArray *copy = [[NSMutableArray alloc] initWithArray:self];

	[copy addObject:object];
	return copy;
}

/*
 * Build an array from a nil-terminated variadic list.
 *
 * `firstObject` IS the argument NAMED in the method signature, and va_start
 * points the list at the argument AFTER it. Reading the list as though it began
 * at firstObject therefore DROPPED the first element — ["x", "y"] came out as
 * ["y"] — which is exactly what the array-extras check caught. So firstObject is
 * element 0 by hand, and the list supplies the rest.
 *
 * Two passes over the list, because the storage has to be exactly sized and a
 * va_list cannot be rewound without a copy of it.
 */
static NSArray *array_from_varargs(Class cls, id firstObject, va_list args)
{
	va_list counter;
	id *objects;
	size_t extra = 0;
	size_t i;
	NSArray *result;

	if (firstObject == nil) {
		return [[cls alloc] initWithObjects:NULL count:0];
	}
	va_copy(counter, args);
	while (va_arg(counter, id) != nil) {
		extra++;
	}
	va_end(counter);
	objects = (id *)calloc(extra + 2, sizeof(id));
	if (objects == NULL) {
		return nil;
	}
	objects[0] = firstObject;
	for (i = 0; i < extra; i++) {
		objects[i + 1] = va_arg(args, id);
	}
	result = [[cls alloc] initWithObjects:objects count:extra + 1];
	free(objects);
	return result;
}

+ (NSArray *)arrayWithArray:(NSArray *)other
{
	return [[self alloc] initWithArray:other];
}

+ (NSArray *)arrayWithObjects:(id)firstObject, ...
{
	va_list args;
	NSArray *result;

	va_start(args, firstObject);
	result = array_from_varargs(self, firstObject, args);
	va_end(args);
	return result;
}

/* ===================================================================================================
 * THE NSCoding DOORS (§63.12). What they are FOR, since the archiver does not need them, is in the header.
 * One key, and `-initWithArray:` is the funnel both ends meet at — so the class-choosing rule stays the
 * INITIALIZER's.
 * =================================================================================================== */
- (nullable instancetype)initWithCoder:(NSCoder *)coder
{
	return [self initWithArray:[coder decodeObjectForKey:FNKeyedObjectsKey]];
}

- (void)encodeWithCoder:(NSCoder *)coder
{
	/* A COPY, AND THAT IS THE POINT: encoding `self` under this key would ask the coder for the very object it
	 * is in the middle of writing, and the archive would record the array referring to itself. */
	[coder encodeObject:[NSArray arrayWithArray:self] forKey:FNKeyedObjectsKey];
}

- (id)initWithArray:(NSArray *)other
{
	/*
	 * THROUGH THE PUBLIC ACCESSOR. `other->_items` was the only cross-instance
	 * field access in this class, and the check that faulted in the guest was the
	 * only one containing it; every check that uses accessors passes.
	 * -getObjects:range: is the class's own public answer for this job.
	 */
	NSUInteger n = [other count];
	id __unsafe_unretained *objects;
	id result;

	if (n == 0) {
		return [self initWithObjects:NULL count:0];
	}
	objects = (id __unsafe_unretained *)calloc(n, sizeof(id));
	if (objects == NULL) {
		return nil;
	}
	[other getObjects:objects range:NSMakeRange(0, n)];
	result = [self initWithObjects:objects count:n];
	free(objects);
	return result;
}

- (id)initWithObjects:(id)firstObject, ...
{
	va_list args;
	NSArray *built;

	va_start(args, firstObject);
	built = array_from_varargs([NSArray class], firstObject, args);
	va_end(args);
	return [self initWithArray:built];	/* and that goes through the accessor too */
}

- (NSArray *)arrayByAddingObjectsFromArray:(NSArray *)other
{
	NSMutableArray *copy = [[NSMutableArray alloc] initWithArray:self];

	[copy addObjectsFromArray:other];
	return copy;
}

- (NSArray *)subarrayWithRange:(NSRange)range
{
	unsigned long n = [self count];
	size_t start = range.location;
	size_t length = range.length;
	id __unsafe_unretained *objects;
	NSArray *result;

	if (start > n) {
		start = n;
	}
	if (length > n - start) {
		length = n - start;
	}
	objects = (id __unsafe_unretained *)calloc(length + 1, sizeof(id));
	if (objects == NULL) {
		return nil;
	}
	[self getObjects:objects range:NSMakeRange(start, length)];
	result = [[NSArray alloc] initWithObjects:objects count:length];
	free(objects);
	return result;
}

- (void)getObjects:(id __unsafe_unretained *)buffer range:(NSRange)range
{
	size_t i;
	unsigned long n = [self count];

	if (buffer == NULL) {
		return;
	}
	for (i = 0; i < range.length; i++) {
		buffer[i] = (range.location + i < n) ? [self objectAtIndex:range.location + i] : nil;
	}
}

- (NSUInteger)indexOfObject:(id)object inRange:(NSRange)range
{
	size_t i;

	for (i = range.location; i < [self count] && i < range.location + range.length; i++) {
		id element = [self objectAtIndex:i];

		if (element == object || [element isEqual:object]) {
			return i;
		}
	}
	return NSNotFound;
}

- (NSUInteger)indexOfObjectIdenticalTo:(id)object
{
	size_t i;

	for (i = 0; i < [self count]; i++) {
		if ([self objectAtIndex:i] == object) {
			return i;
		}
	}
	return NSNotFound;
}

/* The comparator sort, shared by the immutable and mutable forms. */
static NSArray *array_sorted_with_comparator(NSArray *source, NSComparator comparator)
{
	NSMutableArray *sorted = [[NSMutableArray alloc] init];
	NSUInteger i;

	[sorted addObjectsFromArray:source];
	for (i = 1; i < [sorted count]; i++) {
		id key = [sorted objectAtIndex:i];
		NSUInteger j = i;

		while (j > 0 && comparator([sorted objectAtIndex:j - 1], key) ==
			       NSOrderedDescending) {
			[sorted replaceObjectAtIndex:j
					   withObject:[sorted objectAtIndex:j - 1]];
			j--;
		}
		[sorted replaceObjectAtIndex:j withObject:key];
	}
	return sorted;
}

- (NSArray *)sortedArrayUsingComparator:(NSComparator)comparator
{
	if (comparator == NULL) {
		return [[NSArray alloc] initWithArray:self];
	}
	return array_sorted_with_comparator(self, comparator);
}

/* THE OPTIONS FORM (§62.104): the same algorithm, and the option is a FACT ABOUT IT rather than a branch —
 * the insertion sort keeps equal elements in the order they arrived, which is what NSSortStable asks for. A
 * concurrency hint is not taken (nothing here sorts concurrently), and the probe asserts the stability. */
- (NSArray *)sortedArrayWithOptions:(NSSortOptions)options usingComparator:(NSComparator)comparator
{
	(void)options;
	return array_sorted_with_comparator(self, comparator);
}

/* THE DESCRIPTOR CHAIN, and the reason the helper above is an INSERTION sort: it moves an element
 * only while the comparison says NSOrderedDescending, so two elements that compare EQUAL keep
 * the order they arrived in. A chain PROMISES that (F10), and the probe measures the promise
 * rather than assuming it. The chain itself is the other half of the rule: the first descriptor
 * decides, and only a TIE falls through to the next. */
- (NSArray *)sortedArrayUsingDescriptors:(NSArray *)sortDescriptors
{
	NSUInteger count = [sortDescriptors count];

	if (count == 0) {
		return [[NSArray alloc] initWithArray:self];
	}
	return array_sorted_with_comparator(self, ^NSComparisonResult(id left, id right) {
		NSUInteger d;

		for (d = 0; d < count; d++) {
			NSSortDescriptor *descriptor = [sortDescriptors objectAtIndex:d];
			NSComparisonResult order = [descriptor compareObject:left toObject:right];

			if (order != NSOrderedSame) {
				return order;
			}
		}
		return NSOrderedSame;
	});
}

/* A C FUNCTION COMPARATOR is a function POINTER, not a table — so it is a rule like the rest,
 * and `context` is handed back to it untouched. */
- (NSArray *)sortedArrayUsingFunction:(NSInteger (*)(id, id, void *))comparator
			      context:(nullable void *)context
{
	if (comparator == NULL) {
		return [[NSArray alloc] initWithArray:self];
	}
	return array_sorted_with_comparator(self, ^NSComparisonResult(id left, id right) {
		return (NSComparisonResult)comparator(left, right, context);
	});
}

/* THE PREDICATE FILTER (F11a): keep what the predicate answers YES for, in order. The receiver is
 * untouched — this is the immutable form, and -filterUsingPredicate: is the mutable one. A NIL
 * predicate raises rather than quietly answering an empty array, which is what a message to nil
 * would have produced by accident. */
- (NSArray *)filteredArrayUsingPredicate:(NSPredicate *)predicate
{
	NSMutableArray *kept = [[NSMutableArray alloc] init];
	NSUInteger i;

	if (predicate == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"-filteredArrayUsingPredicate: needs a predicate"];
	}
	for (i = 0; i < [self count]; i++) {
		id element = [self objectAtIndex:i];	/* ONE READ: the predicate must not be asked twice */

		if ([predicate evaluateWithObject:element]) {
			[kept addObject:element];
		}
	}
	return kept;
}

- (void)enumerateObjectsUsingBlock:(void (^)(id object, NSUInteger index, BOOL *stop))block
{
	NSUInteger i;
	BOOL stop = NO;

	if (block == NULL) {
		return;
	}
	for (i = 0; i < [self count]; i++) {
		block([self objectAtIndex:i], i, &stop);
		if (stop) {
			break;
		}
	}
}

- (NSArray *)objectsAtIndexes:(NSIndexSet *)indexes
{
	NSMutableArray *selected = [[NSMutableArray alloc] init];
	NSUInteger index = [indexes firstIndex];

	while (index != NSNotFound) {
		if (index >= [self count]) {
			/* Cocoa RAISES here rather than returning a short array: the caller
			 * asked for an element that is not there. */
			[NSException raise:NSRangeException
				    format:@"-[NSArray objectsAtIndexes:]: index %lu beyond bounds %lu",
					   (unsigned long)index, (unsigned long)[self count]];
		}
		[selected addObject:[self objectAtIndex:index]];
		index = [indexes indexGreaterThanIndex:index];
	}
	return selected;
}

/* THE FIRST MATCH, AND IT STOPS THERE. `-indexesOfObjectsPassingTest:` below is the EXHAUSTIVE form and is
 * deliberately a different walk: implementing this one as `[[self indexesOf…] firstIndex]` would answer the
 * same index while calling a caller's predicate for every element after the match - and a predicate may have
 * side effects, so "the same answer" is not the same behaviour. A nil predicate and no match both answer
 * NSNotFound, which is Cocoa's "there is no such index". */
- (NSUInteger)indexOfObjectPassingTest:(BOOL (^)(id object, NSUInteger index, BOOL *stop))predicate
{
	NSUInteger i;
	BOOL stop = NO;

	if (predicate == NULL) {
		return NSNotFound;
	}
	for (i = 0; i < [self count] && !stop; i++) {
		if (predicate([self objectAtIndex:i], i, &stop)) {
			return i;
		}
	}
	return NSNotFound;
}

- (NSIndexSet *)indexesOfObjectsPassingTest:(BOOL (^)(id object, NSUInteger index, BOOL *stop))predicate
{
	NSMutableIndexSet *matches = [[NSMutableIndexSet alloc] init];
	NSUInteger i;
	BOOL stop = NO;

	for (i = 0; i < [self count] && !stop; i++) {
		if (predicate([self objectAtIndex:i], i, &stop)) {
			[matches addIndex:i];
		}
	}
	return matches;
}

- (NSUInteger)indexOfObject:(id)object
	      inSortedRange:(NSRange)range
		  options:(NSBinarySearchingOptions)options
	      usingComparator:(NSComparator)comparator
{
	/*
	 * A LINEAR SCAN over an assumed-sorted range, which answers every option the
	 * set defines: the first equal index, the last, or where the object would be
	 * inserted. The contract asks for the ANSWER, not for a binary search.
	 */
	size_t start = range.location;
	size_t end = start + range.length;
	size_t i;
	NSUInteger first = NSNotFound;
	NSUInteger last = NSNotFound;

	if (end > [self count]) {
		end = [self count];
	}
	for (i = start; i < end; i++) {
		NSComparisonResult order = comparator([self objectAtIndex:i], object);

		if (order == NSOrderedSame) {
			if (first == NSNotFound) {
				first = i;
			}
			last = i;
			continue;
		}
		if (order == NSOrderedDescending && first == NSNotFound) {
			if (options & NSBinarySearchingInsertionIndex) {
				return i;
			}
		}
	}
	if (options & NSBinarySearchingInsertionIndex) {
		return (first != NSNotFound) ? last + 1 : end;
	}
	if (options & NSBinarySearchingLastEqual) {
		return (last != NSNotFound) ? last : NSNotFound;
	}
	return first;
}

- (NSString *)componentsJoinedByString:(NSString *)separator
{
	NSMutableString *out = [[NSMutableString alloc] initWithUTF8String:""];
	size_t i;

	for (i = 0; i < [self count]; i++) {
		if (i > 0) {
			[out appendString:separator];
		}
		[out appendString:[[self objectAtIndex:i] description]];
	}
	return out;
}

/* An insertion sort over the elements' -compare: (or whatever the comparator
 * selector answers). Quadratic, and honest about being a v1: correct first. */
- (NSArray *)sortedArrayUsingSelector:(SEL)comparator
{
	NSMutableArray *sorted = [[NSMutableArray alloc] initWithArray:self];
	size_t i;

	for (i = 1; i < [sorted count]; i++) {
		id key = [sorted objectAtIndex:i];
		size_t j = i;

		while (j > 0 &&
		       ((NSComparisonResult (*)(id, SEL, id))objc_msgSend)(
			       [sorted objectAtIndex:j - 1], comparator, key) ==
		       NSOrderedDescending) {
			[sorted replaceObjectAtIndex:j
					   withObject:[sorted objectAtIndex:j - 1]];
			j--;
		}
		[sorted replaceObjectAtIndex:j withObject:key];
	}
	return sorted;
}

- (NSEnumerator *)objectEnumerator
{
	return [[NSEnumerator alloc] initWithSequence:self reverse:NO];
}

- (NSEnumerator *)reverseObjectEnumerator
{
	return [[NSEnumerator alloc] initWithSequence:self reverse:YES];
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
	if ([other count] != [self count]) {
		return NO;
	}
	for (i = 0; i < [self count]; i++) {
		id a = [self objectAtIndex:i];
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

	h ^= [self count];
	h *= 16777619UL;
	for (i = 0; i < [self count]; i++) {
		h ^= [[self objectAtIndex:i] hash];
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
	for (i = 0; i < [self count]; i++) {
		if (i > 0) {
			[out appendString:@", "];
		}
		[out appendString:[[self objectAtIndex:i] description]];
	}
	[out appendString:@")"];
	return out;
}

/* §C.3 item 4, AND THIS IS THE BULLET THAT KEEPS THE REST OF THE LIBRARY WORKING: an archiver asks for
 * -classForCoder, never for -class, so the PUBLIC name is what an archive holds and none of the private
 * concrete names above can appear in one (§C.4). -classForArchiver defaults to this (M0, NSObject's). */
- (Class)classForCoder
{
	return [NSArray class];
}

- (id)copy
{
	return [self retain];	/* +1: `copy` is an OWNED family (plan §15.2) — immutable */
}

- (id)mutableCopy
{
	return [[NSMutableArray alloc] initWithArray:self];
}

/* NSCopying keeps Cocoa's SHAPE with the zone accepted and ignored (the plan's
 * §7 decision): there are no zones, but the selector stays for compatibility. */

- (unsigned long)countByEnumeratingWithState:(NSFastEnumerationState *)state
                                     objects:(id __unsafe_unretained *)buffer
                                       count:(unsigned long)length
{
	unsigned long n = [self count];
	unsigned long cursor = state->state;
	unsigned long batch;
	unsigned long i;

	/*
	 * OVER THE PRIMITIVES, THROUGH THE CALLER'S OWN BUFFER (§C.3 item 5). The version this replaces answered
	 * `state->itemsPtr = _items` in ONE batch, which requires CONTIGUOUS storage - and the one-element and
	 * small concrete classes HAVE NONE, so that version would have handed a fast-enumeration loop a pointer
	 * into the front's ivars. `objects` IS caller-provided scratch that stays valid for the batch, which is
	 * exactly what the protocol says it is for; `state->state` is the cursor, so a batch smaller than the
	 * count is RESUMED rather than restarted.
	 */
	if (cursor >= n) {
		return 0;
	}
	batch = n - cursor;
	if (batch > length) {
		batch = length;
	}
	if (batch == 0) {
		return 0;	/* no scratch was offered this time round */
	}
	for (i = 0; i < batch; i++) {
		buffer[i] = [self objectAtIndex:cursor + i];
	}
	state->itemsPtr = buffer;
	state->mutationsPtr = &_mutations;
	state->state = cursor + batch;
	return batch;
}



@end

@implementation NSMutableArray

/* THE SAME DOOR AS THE IMMUTABLE FRONT (§C.3 item 1), and it is what makes a mutable constructor answer a
 * mutable concrete class: NSMutableArray's own implementation stays where it is and AGArrayMutable is the
 * name a caller sees. */
+ (id)alloc
{
	if (self != [NSMutableArray class]) {
		return [super alloc];
	}
	return [AGArrayMutable alloc];
}

/* AND THE MUTABLE FRONT ANSWERS ITSELF TO AN ARCHIVER, for the same reason as NSArray's. */
- (Class)classForCoder
{
	return [NSMutableArray class];
}

/*
 * A MUTABLE ARRAY ENUMERATES BY COPYING, and that is the difference between an exception and a
 * CRASH (D6 of §11.6.1, measured: STATUS=139). NSArray's implementation hands out `itemsPtr =
 * _items` — its OWN storage — and that is safe there because an immutable array's elements are
 * stable for its lifetime. A mutable array's are not: a mutation during the loop grows the
 * storage, and a loop reading the old pointer is reading freed memory. The mutation word CANNOT
 * save that, because it is only consulted when the loop comes back — after the read. Copying the
 * batch into the caller's buffer makes the batch the loop's own, and the mutation is then what the
 * word says it is: detected at the next check, and raised as NSGenericException.
 */
- (NSUInteger)countByEnumeratingWithState:(NSFastEnumerationState *)state
				  objects:(id __unsafe_unretained *)buffer
				    count:(NSUInteger)len
{
	unsigned long done = 0;

	state->itemsPtr = buffer;
	state->mutationsPtr = &_mutations;
	while (state->state < _count && done < len) {
		buffer[done++] = _items[state->state];
		state->state++;
	}
	return done;
}

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

/* THE SAME DOOR ON THE MUTABLE CLASS (§63.12), declared in its own block and therefore needing its own body:
 * `--unimplemented` counts an implementation in the class or a SUBCLASS, so the front's does not satisfy it.
 * `[super initWithCoder:]` reaches the front's, which funnels through `-initWithArray:` with `self` still the
 * MUTABLE class — and the class-choosing rule sends only the immutable concrete class to the shared empty
 * instance, so a mutable answer stays mutable. */
- (nullable instancetype)initWithCoder:(NSCoder *)coder
{
	return [super initWithCoder:coder];
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
		[NSException raise:NSRangeException
				format:@"-[NSMutableArray removeObjectAtIndex:]: index %lu beyond bounds", index];
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

- (void)replaceObjectAtIndex:(NSUInteger)index withObject:(id)object
{
	if (index >= _count) {
		[NSException raise:NSRangeException
				format:@"-[NSMutableArray replaceObjectAtIndex:withObject:]: index %lu beyond bounds", index];
	}
	if (object == nil) {
		[NSException raise:NSInvalidArgumentException
				format:@"-[NSMutableArray replaceObjectAtIndex:withObject:]: object cannot be nil"];
	}
	objc_retain(object);
	objc_release(_items[index]);
	_items[index] = object;
	_mutations++;
}

/* `array[i] = x`: Cocoa replaces at i, and APPENDS when i == count. */
- (void)setObject:(id)object atIndexedSubscript:(NSUInteger)index
{
	if (object == nil) {
		return;
	}
	if (index == _count) {
		[self addObject:object];
		return;
	}
	[self replaceObjectAtIndex:index withObject:object];
}

- (id)copy
{
	/* A snapshot, like every other mutable type here. */
	return [[NSArray alloc] initWithObjects:_items count:_count];
}

- (void)addObjectsFromArray:(NSArray *)other
{
	NSUInteger i;

	for (i = 0; i < [other count]; i++) {
		[self addObject:[other objectAtIndex:i]];
	}
}

- (void)insertObjects:(NSArray *)objects atIndexes:(NSIndexSet *)indexes
{
	NSUInteger index;
	NSUInteger taken = 0;
	NSUInteger offset = 0;

	if ([objects count] != [indexes count]) {
		[NSException raise:NSInvalidArgumentException
			    format:@"-[NSMutableArray insertObjects:atIndexes:]: %lu objects but %lu indexes",
				   (unsigned long)[objects count], (unsigned long)[indexes count]];
	}
	index = [indexes firstIndex];
	while (index != NSNotFound) {
		/* Every insertion shifts the LATER positions right, which is what the
		 * offset is for. */
		[self insertObject:[objects objectAtIndex:taken] atIndex:index + offset];
		offset++;
		taken++;
		index = [indexes indexGreaterThanIndex:index];
	}
}

- (void)removeObjectsAtIndexes:(NSIndexSet *)indexes
{
	/* DESCENDING: a removal shifts everything after it, so walking down means
	 * every remaining position is still the one the caller asked for. */
	NSUInteger index = [indexes lastIndex];

	while (index != NSNotFound) {
		[self removeObjectAtIndex:index];
		index = [indexes indexLessThanIndex:index];
	}
}

- (void)replaceObjectsAtIndexes:(NSIndexSet *)indexes withObjects:(NSArray *)objects
{
	NSUInteger index;
	NSUInteger taken = 0;

	if ([objects count] != [indexes count]) {
		[NSException raise:NSInvalidArgumentException
			    format:@"-[NSMutableArray replaceObjectsAtIndexes:withObjects:]: %lu objects but %lu indexes",
				   (unsigned long)[objects count], (unsigned long)[indexes count]];
	}
	index = [indexes firstIndex];
	while (index != NSNotFound) {
		[self replaceObjectAtIndex:index withObject:[objects objectAtIndex:taken]];
		taken++;
		index = [indexes indexGreaterThanIndex:index];
	}
}

- (void)removeLastObject
{
	if (_count > 0) {
		[self removeObjectAtIndex:_count - 1];
	}
}

- (void)removeObject:(id)object
{
	[self removeObject:object inRange:NSMakeRange(0, _count)];
}

- (void)removeObjectIdenticalTo:(id)object
{
	NSUInteger i;

	for (i = 0; i < _count; i++) {
		if (_items[i] == object) {
			[self removeObjectAtIndex:i];
			return;
		}
	}
}

- (void)removeObjectIdenticalTo:(id)object inRange:(NSRange)range
{
	NSUInteger i;

	for (i = range.location; i < _count && i < range.location + range.length; i++) {
		if (_items[i] == object) {
			[self removeObjectAtIndex:i];
			return;
		}
	}
}

- (void)sortUsingComparator:(NSComparator)comparator
{
	if (comparator == NULL) {
		return;
	}
	[self setArray:array_sorted_with_comparator(self, comparator)];
}

- (void)sortWithOptions:(NSSortOptions)options usingComparator:(NSComparator)comparator
{
	if (comparator == NULL) {
		return;
	}
	[self setArray:array_sorted_with_comparator(self, comparator)];
}

- (void)sortUsingDescriptors:(NSArray *)sortDescriptors
{
	[self setArray:[self sortedArrayUsingDescriptors:sortDescriptors]];
}

- (void)sortUsingFunction:(NSInteger (*)(id, id, void *))comparator context:(nullable void *)context
{
	[self setArray:[self sortedArrayUsingFunction:comparator context:context]];
}

/* In place, because that is what MUTABLE means: the kept elements stay in the receiver. */
- (void)filterUsingPredicate:(NSPredicate *)predicate
{
	[self setArray:[self filteredArrayUsingPredicate:predicate]];
}

- (void)replaceObjectsInRange:(NSRange)range
	 withObjectsFromArray:(NSArray *)other
			range:(NSRange)otherRange
{
	/*
	 * Built into a FRESH array and swapped in: replacing in place while reading
	 * from a range of a possibly-identical array is the aliasing hazard the
	 * collection work already met once.
	 */
	NSMutableArray *built = [[NSMutableArray alloc] init];
	NSUInteger i;

	for (i = 0; i < [self count]; i++) {
		if (i == range.location) {
			NSUInteger k;

			for (k = 0; k < otherRange.length && k < [other count]; k++) {
				[built addObject:[other objectAtIndex:otherRange.location + k]];
			}
			for (k = 0; k < range.length && range.location + k < [self count]; k++) {
				(void)k;
			}
			i = range.location + (range.length > 0 ? range.length - 1 : 0);
			continue;
		}
		[built addObject:[self objectAtIndex:i]];
	}
	[self setArray:built];
}

- (void)removeObject:(id)object inRange:(NSRange)range
{
	NSUInteger i;

	for (i = range.location; i < _count && i < range.location + range.length; i++) {
		if (_items[i] == object || [_items[i] isEqual:object]) {
			[self removeObjectAtIndex:i];
			return;
		}
	}
}

- (void)removeObjectsInRange:(NSRange)range
{
	NSUInteger i;

	for (i = 0; i < range.length; i++) {
		if (range.location < _count) {
			[self removeObjectAtIndex:range.location];
		}
	}
}

- (void)setArray:(NSArray *)other
{
	[self removeAllObjects];
	[self addObjectsFromArray:other];
}

- (void)exchangeObjectAtIndex:(NSUInteger)first withObjectAtIndex:(NSUInteger)second
{
	id held;

	if (first >= _count || second >= _count || first == second) {
		return;
	}
	held = _items[first];
	_items[first] = _items[second];
	_items[second] = held;
	_mutations++;
}

- (void)replaceObjectsInRange:(NSRange)range withObjectsFromArray:(NSArray *)other
{
	NSUInteger i = 0;

	if (range.location >= _count) {
		return;
	}
	for (i = 0; i < range.length && range.location + i < _count; i++) {
		if (i < [other count]) {
			[self replaceObjectAtIndex:range.location + i
				     withObject:[other objectAtIndex:i]];
		} else {
			[self removeObjectAtIndex:range.location + i];
		}
	}
}

- (void)sortUsingSelector:(SEL)comparator
{
	NSArray *sorted = [[NSArray alloc] initWithObjects:_items count:_count];

	[self setArray:[sorted sortedArrayUsingSelector:comparator]];
}

@end


/* ===================================================================================================
 * THE CONCRETE CLASSES (§C.3 items 2, 3 and 8): the four immutable cases and the mutable one.
 * =================================================================================================== */

@implementation AGArrayItems

/* [[NSArray alloc] init] IS A LEGITIMATE THING TO WRITE (§C.3 item 1) AND IT IS THE EMPTY CASE: this is
 * the class `+alloc` answers with, so this is where the empty answer belongs. It is implemented HERE and
 * NOT on the front on purpose - a concrete class that inherited such an -init from the front could not say
 * `[super init]` without becoming the singleton, which is how a one-element array would silently turn into
 * an empty one. */
- (id)init
{
	[self release];	/* never initialized: the storage was never built */
	return (id)[AGArrayEmpty emptyArray];
}

@end

@implementation AGArrayEmpty

+ (AGArrayEmpty *)emptyArray
{
	static AGArrayEmpty *shared = nil;

	if (shared == nil) {
		shared = [[AGArrayEmpty alloc] init];
	}
	return shared;
}

/* IMMORTAL, AND THAT IS THE PRICE OF A SINGLETON IN A LIBRARY WITH NO `+allocWithZone:` AND NO COLLECTOR:
 * a caller who retains or releases the shared instance must not be able to free it out from under every
 * other caller. `-retainCount` is not a contract (NSObject.h says so); it answers unbounded rather than 1,
 * because 1 is the one answer that invites a release. */
- (id)retain { return self; }
- (void)release { }
- (id)autorelease { return self; }
- (NSUInteger)retainCount { return NSUIntegerMax; }

/* THE TWO PRIMITIVES (§C.3 item 5) AND NOTHING ELSE: every other read in this family is written over
 * these, so an empty array answers -hash, -isEqualToArray:, -subarrayWithRange:, -description and fast
 * enumeration correctly without a line of code here. */
- (unsigned long)count
{
	return 0;
}

- (id)objectAtIndex:(unsigned long)index
{
	[NSException raise:NSRangeException
	            format:@"-[NSArray objectAtIndex:]: index %lu beyond bounds for empty array", index];
	return nil;
}

/* A FAST PATH, not a requirement: an empty collection enumerates nothing, so this skips the caller's
 * buffer entirely. */
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

@implementation AGArrayOne

- (id)initWithObject:(id)object
{
	self = [super init];
	if (self != nil) {
		_one = objc_retain(object);
	}
	return self;
}

- (void)dealloc
{
	objc_release(_one);
	[super dealloc];
}

- (unsigned long)count
{
	return 1;
}

- (id)objectAtIndex:(unsigned long)index
{
	if (index != 0) {
		[NSException raise:NSRangeException
		            format:@"-[NSArray objectAtIndex:]: index %lu beyond bounds [0 .. 0]", index];
	}
	return _one;
}

@end

@implementation AGArraySmall

- (id)initWithObjects:(const id *)objects count:(unsigned long)count
{
	unsigned long i;

	self = [super init];
	if (self == nil) {
		return nil;
	}
	_smallCount = count;	/* <= AG_ARRAY_SMALL_MAX by construction: this class is chosen by that bound */
	for (i = 0; i < count; i++) {
		_small[i] = objc_retain(objects[i]);
	}
	return self;
}

- (void)dealloc
{
	unsigned long i;

	for (i = 0; i < _smallCount; i++) {
		objc_release(_small[i]);
	}
	[super dealloc];
}

- (unsigned long)count
{
	return _smallCount;
}

- (id)objectAtIndex:(unsigned long)index
{
	if (index >= _smallCount) {
		[NSException raise:NSRangeException
		            format:@"-[NSArray objectAtIndex:]: index %lu beyond bounds [0 .. %lu]",
		                   index, _smallCount - 1];
	}
	return _small[index];
}

@end

@implementation AGArrayMutable

/* NOTHING TO IMPLEMENT, AND THAT IS THE POINT: NSMutableArray's own implementation IS the mutable storage
 * implementation, this class inherits it, and what a caller gains is the NAME that -class answers (§C.3
 * items 2 and 3). */

@end
