/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSOrderedSet.m — a set that remembers the order (F13.8e). MANUAL OWNERSHIP.
 *
 * THE STORAGE IS AN IMMUTABLE ARRAY OF MEMBERS, exactly as NSSet's is, and every mutation REPLACES
 * it rather than editing it — so a loop that is running over an ordered set cannot be handed storage
 * a later mutation frees. The lookups are LINEAR by -isEqual:, so "the same value" means the same
 * VALUE and not the same pointer, and the first occurrence of a duplicate is the one that stays.
 *
 * THE ENUMERATION IS THE PROTOCOL'S, in the same multi-batch shape NSSet uses: each call fills the
 * CALLER'S buffer and advances state->state, with mutationsPtr set once. It is duplicated here
 * deliberately and visibly — the two classes share no superclass, and a shared helper would have to
 * reach into another class's storage.
 */

#import <Foundation/NSMutableOrderedSet.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSEnumerator.h>
#import <Foundation/NSSet.h>
#import <Foundation/NSPredicate.h>
#import <Foundation/NSException.h>
#import <Foundation/NSString.h>
#include <stdarg.h>		/* the nil-terminated construction doors walk a va_list */
/* THE KEYED ARCHIVE'S KEY NAMES, shared with NSKeyedArchiver's structural branch so the NSCoding doors below
 * and that branch cannot spell the same key differently (§63.10). */
#import <Foundation/FNKeyedWire.h>

/* ===================================================================================================
 * THE PRIVATE CONCRETE CLASSES (plan §C.3, M6): the same shape as the array, dictionary, set and number
 * families. THE STORAGE IS THE FRONT'S OWN IVARS, and NSMutableOrderedSet defines NO initializers at all -
 * it inherits every one from the front - so the class-choosing guard is a MEMBERSHIP test.
 * =================================================================================================== */
@interface AGOrderedSetEmpty : NSOrderedSet
+ (AGOrderedSetEmpty *)emptyOrderedSet;
@end

@interface AGOrderedSetItems : NSOrderedSet
@end

@interface AGOrderedSetMutable : NSMutableOrderedSet
@end


@implementation NSOrderedSet

/* THE DOOR (§C.3 item 1), routed exactly once at the front. */
+ (id)alloc
{
	if (self != [NSOrderedSet class]) {
		return [super alloc];
	}
	return [AGOrderedSetItems alloc];
}

/* §C.3 item 4: the private classes name the PUBLIC class. NSMutableOrderedSet answers ITSELF below, being a
 * public subclass - the shape NSDecimalNumber needs in the number family. */
- (Class)classForCoder
{
	return [NSOrderedSet class];
}


+ (instancetype)orderedSet
{
	return [[self alloc] initWithObjects:NULL count:0];
}

+ (instancetype)orderedSetWithObject:(id)object
{
	return [[self alloc] initWithObjects:&object count:1];
}

+ (instancetype)orderedSetWithObjects:(const id _Nonnull * _Nullable)objects count:(NSUInteger)count
{
	return [[self alloc] initWithObjects:objects count:count];
}

+ (instancetype)orderedSetWithArray:(NSArray *)array
{
	return [[self alloc] initWithArray:array];
}

+ (instancetype)orderedSetWithOrderedSet:(NSOrderedSet *)set
{
	return [[self alloc] initWithOrderedSet:set];
}

- (instancetype)initWithObjects:(const id _Nonnull * _Nullable)objects count:(NSUInteger)count
{
	NSMutableArray *members;
	NSUInteger i;

	/* THE CLASS IS CHOSEN BY THE DATA (§C.3 item 2), for this family's general class only: a
	 * mutable receiver inherits this implementation and must keep it. Both constructions are
	 * COMPLETE - the member array is assigned at the END - which is what makes answering the
	 * shared empty instance safe. */
	if ([self isMemberOfClass:[AGOrderedSetItems class]] && count == 0) {
		[self release];	/* never initialized: the storage was never built */
		return (id)[AGOrderedSetEmpty emptyOrderedSet];
	}
	self = [super init];
	if (self == nil) {
		return nil;
	}
	members = [NSMutableArray arrayWithCapacity:count];
	for (i = 0; i < count; i++) {
		id object = objects[i];
		BOOL seen = NO;
		NSUInteger j;

		if (object == nil) {
			continue;
		}
		/* THE FIRST OCCURRENCE STAYS, and the ORDER is the order of first arrival — which is the
		 * whole difference from NSSet, whose members have no order to preserve. */
		for (j = 0; j < [members count]; j++) {
			if ([[members objectAtIndex:j] isEqual:object]) {
				seen = YES;
				break;
			}
		}
		if (!seen) {
			[members addObject:object];
		}
	}
	_members = [members copy];
	_mutations = 0;
	return self;
}

- (instancetype)initWithArray:(NSArray *)array
{
	NSMutableArray *members;
	NSUInteger i;

	/* THE CLASS IS CHOSEN BY THE DATA (§C.3 item 2), for this family's general class only: a
	 * mutable receiver inherits this implementation and must keep it. Both constructions are
	 * COMPLETE - the member array is assigned at the END - which is what makes answering the
	 * shared empty instance safe. */
	if ([self isMemberOfClass:[AGOrderedSetItems class]] && [array count] == 0) {
		[self release];	/* never initialized: the storage was never built */
		return (id)[AGOrderedSetEmpty emptyOrderedSet];
	}
	self = [super init];
	if (self == nil) {
		return nil;
	}
	members = [NSMutableArray arrayWithCapacity:[array count]];
	for (i = 0; i < [array count]; i++) {
		id object = [array objectAtIndex:i];
		BOOL seen = NO;
		NSUInteger j;

		if (object == nil) {
			continue;
		}
		for (j = 0; j < [members count]; j++) {
			if ([[members objectAtIndex:j] isEqual:object]) {
				seen = YES;
				break;
			}
		}
		if (!seen) {
			[members addObject:object];
		}
	}
	_members = [members copy];
	_mutations = 0;
	return self;
}

- (instancetype)initWithOrderedSet:(NSOrderedSet *)set
{
	return [self initWithArray:[set array]];
}

/* ===================================================================================================
 * THE CONSTRUCTION FAMILY COMPLETE (§63.6). Apple's doors take a SOURCE, a RANGE and a COPY flag, and the
 * whole family funnels through `-initWithArray:` above - which is where the class-choosing rule and the
 * shared empty instance already live - so each door below is ONE LINE and there is ONE place the range
 * contract and the copying rule can be wrong. TWO SMALL HELPERS carry those two rules:
 * =================================================================================================== */

/* THE RANGE, AND APPLE'S CONTRACT IS A RAISE RATHER THAN A CLAMP: "If range is not within array's bounds,
 * this method raises an NSRangeException." That is NOT what `NSArray -subarrayWithRange:` does in this tree
 * (it CLAMPS, §13, its own recorded behaviour) - the difference is real and deliberate here, and it is
 * stated rather than quietly matched, because clamping would answer a set the caller never asked for. The
 * bounds test is written so it cannot overflow: `location + length` on a caller's unsigned range WRAPS, and
 * a wrapped sum compares SMALLER, which is how a bounds check passes exactly the case it exists for. */
static NSArray *fn_slice(NSArray *array, NSRange range)
{
	NSMutableArray *out;
	NSUInteger count = [array count];
	NSUInteger i;

	if (range.location > count || range.length > count - range.location) {
		[NSException raise:NSRangeException
			    format:@"NSOrderedSet: the range {%lu, %lu} is not within an array of %lu",
				   (unsigned long)range.location, (unsigned long)range.length,
				   (unsigned long)count];
	}
	out = [NSMutableArray arrayWithCapacity:range.length];
	for (i = 0; i < range.length; i++) {
		[out addObject:[array objectAtIndex:range.location + i]];
	}
	return out;
}

/* `copyItems:YES` COPIES EACH MEMBER, and the copy is an OWNED `+1` that the array's own retain takes over -
 * balanced here rather than left to an autorelease pool that may not exist (§15.2's owned families). A
 * member that cannot copy raises `-doesNotRecognizeSelector:`, the same answer Cocoa gives. */
static NSArray *fn_copies(NSArray *array)
{
	NSMutableArray *out = [NSMutableArray arrayWithCapacity:[array count]];
	NSUInteger i;

	for (i = 0; i < [array count]; i++) {
		id copied = [[array objectAtIndex:i] copy];

		[out addObject:copied];
		[copied release];
	}
	return out;
}

/* THE NIL-TERMINATED LIST AS AN ARRAY, in ONE pass and shared by the two variadic doors. `NSArray`'s own
 * variadic factory counts first because it sizes a `calloc` exactly; a mutable array needs no count, so the
 * copy-of-the-va_list that the two-pass form exists for is not needed here. */
static NSArray *fn_from_varargs(id firstObject, va_list args)
{
	NSMutableArray *out;
	id object;

	if (firstObject == nil) {
		return @[];
	}
	out = [NSMutableArray array];
	[out addObject:firstObject];
	while ((object = va_arg(args, id)) != nil) {
		[out addObject:object];
	}
	return out;
}

- (instancetype)initWithObject:(id)object
{
	return [self initWithObjects:&object count:1];
}

- (instancetype)initWithObjects:(id)firstObject, ...
{
	va_list args;
	NSArray *members;

	va_start(args, firstObject);
	members = fn_from_varargs(firstObject, args);
	va_end(args);
	return [self initWithArray:members];
}

- (instancetype)initWithArray:(NSArray *)array copyItems:(BOOL)flag
{
	return [self initWithArray:flag ? fn_copies(array) : array];
}

- (instancetype)initWithArray:(NSArray *)array range:(NSRange)range copyItems:(BOOL)flag
{
	NSArray *slice = fn_slice(array, range);

	return [self initWithArray:flag ? fn_copies(slice) : slice];
}

- (instancetype)initWithOrderedSet:(NSOrderedSet *)set copyItems:(BOOL)flag
{
	return [self initWithArray:[set array] copyItems:flag];
}

- (instancetype)initWithOrderedSet:(NSOrderedSet *)set range:(NSRange)range copyItems:(BOOL)flag
{
	return [self initWithArray:[set array] range:range copyItems:flag];
}

- (instancetype)initWithSet:(NSSet *)set
{
	return [self initWithArray:[set allObjects]];
}

- (instancetype)initWithSet:(NSSet *)set copyItems:(BOOL)flag
{
	return [self initWithArray:[set allObjects] copyItems:flag];
}

+ (instancetype)orderedSetWithObjects:(id)firstObject, ...
{
	va_list args;
	NSArray *members;

	va_start(args, firstObject);
	members = fn_from_varargs(firstObject, args);
	va_end(args);
	return [[self alloc] initWithArray:members];
}

+ (instancetype)orderedSetWithArray:(NSArray *)array range:(NSRange)range copyItems:(BOOL)flag
{
	return [[self alloc] initWithArray:array range:range copyItems:flag];
}

+ (instancetype)orderedSetWithOrderedSet:(NSOrderedSet *)set range:(NSRange)range copyItems:(BOOL)flag
{
	return [[self alloc] initWithOrderedSet:set range:range copyItems:flag];
}

+ (instancetype)orderedSetWithSet:(NSSet *)set
{
	return [[self alloc] initWithSet:set];
}

+ (instancetype)orderedSetWithSet:(NSSet *)set copyItems:(BOOL)flag
{
	return [[self alloc] initWithSet:set copyItems:flag];
}

- (NSUInteger)count
{
	return [_members count];
}

- (nullable id)objectAtIndex:(NSUInteger)index
{
	if (index >= [_members count]) {
		/* COCOA'S RULE: a bad index is an exception, not nil. It is the one failure an indexed
		 * collection is asked for by an index rather than by a value. */
		[NSException raise:NSRangeException
			    format:@"NSOrderedSet: index %lu is beyond the end (%lu members)",
				   (unsigned long)index, (unsigned long)[_members count]];
	}
	return [_members objectAtIndex:index];
}

- (nullable id)objectAtIndexedSubscript:(NSUInteger)index
{
	return [self objectAtIndex:index];
}

- (NSUInteger)indexOfObject:(id)object
{
	NSUInteger i;

	if (object == nil) {
		return NSNotFound;
	}
	for (i = 0; i < [self count]; i++) {
		if ([[self objectAtIndex:i] isEqual:object]) {
			return i;
		}
	}
	return NSNotFound;
}

- (BOOL)containsObject:(id)object
{
	return [self indexOfObject:object] != NSNotFound;
}

- (nullable id)firstObject
{
	return [self count] > 0 ? [self objectAtIndex:0] : nil;
}

- (nullable id)lastObject
{
	return [self count] > 0 ? [self objectAtIndex:[self count] - 1] : nil;
}

- (NSArray *)array
{
	NSMutableArray *out = [NSMutableArray arrayWithCapacity:[self count]];
	NSEnumerator *enumerator = [self objectEnumerator];
	id object;

	/* OVER THE PRIMITIVES (§C.3 item 5). This used to hand back the internal member array, which a concrete
	 * class with a different layout does not have - the empty one has none at all. */
	while ((object = [enumerator nextObject]) != nil) {
		[out addObject:object];
	}
	return out;
}

/* ===================================================================================================
 * THE NSCoding DOORS (§63.10). What they are FOR, since the archiver does not need them, is in the header.
 * The pair is symmetric through ONE key, and `-initWithArray:` is the funnel both ends meet at, so the
 * order, the dedup rule and the class-choosing rule stay the INITIALIZER's.
 * =================================================================================================== */
- (nullable instancetype)initWithCoder:(NSCoder *)coder
{
	return [self initWithArray:[coder decodeObjectForKey:FNKeyedObjectsKey]];
}

- (void)encodeWithCoder:(NSCoder *)coder
{
	/* A COPY, AND THAT IS THE POINT: encoding `self` under this key would ask the coder for the very object it
	 * is in the middle of writing, and the archive would record the ordered set referring to itself. The copy
	 * is a distinct array holding the same members, which is exactly the payload `NS.objects` names. */
	[coder encodeObject:[NSArray arrayWithArray:[self array]] forKey:FNKeyedObjectsKey];
}

- (NSSet *)set
{
	return [NSSet setWithArray:[self array]];
}

/* THE POSITIONAL SUBSET, mirroring `NSArray -objectsAtIndexes:` INCLUDING its refusal: an index that is not
 * there is an exception rather than a short answer, because the caller asked for an element. The walk goes
 * through the INDEX SET's own order (ascending), which for a position subset of an ordered set IS the
 * receiver's order - so the result needs no sorting and the two cannot disagree. */
- (NSArray *)objectsAtIndexes:(NSIndexSet *)indexes
{
	NSMutableArray *selected = [NSMutableArray array];
	NSUInteger index;

	if (indexes == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSOrderedSet: -objectsAtIndexes: needs an index set"];
	}
	index = [indexes firstIndex];
	while (index != NSNotFound) {
		if (index >= [self count]) {
			[NSException raise:NSRangeException
				    format:@"NSOrderedSet: index %lu is beyond bounds %lu",
					   (unsigned long)index, (unsigned long)[self count]];
		}
		[selected addObject:[self objectAtIndex:index]];
		index = [indexes indexGreaterThanIndex:index];
	}
	return selected;
}

/* THE REVERSAL. It answers an IMMUTABLE ordered set on purpose, which the `copy` attribute on the property
 * promises and a mutable answer would break: a caller may hold this value while the receiver is mutated, and
 * the reversal must not change with it. Building it through `+orderedSetWithArray:` ALSO routes the empty
 * case to the shared empty instance, which the hand-built mutable version did not. */
- (NSOrderedSet *)reversedOrderedSet
{
	NSMutableArray *reversed = [NSMutableArray arrayWithCapacity:[self count]];
	NSUInteger i = [self count];

	while (i > 0) {
		i--;
		[reversed addObject:[self objectAtIndex:i]];
	}
	return [NSOrderedSet orderedSetWithArray:reversed];
}

- (NSEnumerator *)objectEnumerator
{
	return [_members objectEnumerator];
}

- (NSEnumerator *)reverseObjectEnumerator
{
	NSMutableArray *reversed = [NSMutableArray arrayWithCapacity:[self count]];
	NSUInteger i = [self count];

	/* Built by hand rather than borrowed from NSArray: this class promises the reversal, and
	 * whether the array underneath offers one of its own is not part of this class's contract. */
	while (i > 0) {
		i--;
		[reversed addObject:[self objectAtIndex:i]];
	}
	return [reversed objectEnumerator];
}

- (void)enumerateObjectsUsingBlock:(void (^)(id object, NSUInteger index, BOOL *stop))block
{
	[self enumerateObjectsWithOptions:0 usingBlock:block];
}

/* ONE WALK, TWO DOORS, AND THE OPTIONS ARE THE ONLY DIFFERENCE - which is why the plain form above is one
 * line. `NSEnumerationConcurrent` is a HINT (`NSIndexSet.h` records that this library does not take it), so
 * the only option with meaning here is `NSEnumerationReverse`, and the REVERSE WALK CARRIES THE SAME INDEX
 * the forward one would: a caller asking where an object sits must not have to know which way it was
 * enumerated. `stop` is one shared flag, so a block that stops the walk stops THIS walk. */
- (void)enumerateObjectsWithOptions:(NSEnumerationOptions)options
			 usingBlock:(void (^)(id object, NSUInteger index, BOOL *stop))block
{
	NSUInteger i;
	BOOL stop = NO;

	if (block == NULL) {
		return;
	}
	if ((options & NSEnumerationReverse) != 0) {
		for (i = [self count]; i > 0; i--) {
			block([self objectAtIndex:i - 1], i - 1, &stop);
			if (stop) {
				break;
			}
		}
		return;
	}
	for (i = 0; i < [self count]; i++) {
		block([self objectAtIndex:i], i, &stop);
		if (stop) {
			break;
		}
	}
}

/* THE SAME WALK RESTRICTED TO A POSITION SET, over the two bounds rules `-objectsAtIndexes:` uses: a nil
 * index set is a caller error, and an index past the end RAISES rather than being skipped - an enumerator
 * that silently ignored one would report a shorter visit than the caller asked for. The reverse walk starts
 * at the index set's LAST index, so `NSEnumerationReverse` means the same thing here as it does above. */
- (void)enumerateObjectsAtIndexes:(NSIndexSet *)indexes
			  options:(NSEnumerationOptions)options
		       usingBlock:(void (^)(id object, NSUInteger index, BOOL *stop))block
{
	NSUInteger index;
	BOOL stop = NO;

	if (block == NULL) {
		return;
	}
	if (indexes == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSOrderedSet: -enumerateObjectsAtIndexes: needs an index set"];
	}
	if ((options & NSEnumerationReverse) != 0) {
		index = [indexes lastIndex];
		while (index != NSNotFound) {
			if (index >= [self count]) {
				[NSException raise:NSRangeException
					    format:@"NSOrderedSet: index %lu is beyond bounds %lu",
						   (unsigned long)index, (unsigned long)[self count]];
			}
			block([self objectAtIndex:index], index, &stop);
			if (stop) {
				return;
			}
			index = [indexes indexLessThanIndex:index];
		}
		return;
	}
	index = [indexes firstIndex];
	while (index != NSNotFound) {
		if (index >= [self count]) {
			[NSException raise:NSRangeException
				    format:@"NSOrderedSet: index %lu is beyond bounds %lu",
					   (unsigned long)index, (unsigned long)[self count]];
		}
		block([self objectAtIndex:index], index, &stop);
		if (stop) {
			return;
		}
		index = [indexes indexGreaterThanIndex:index];
	}
}

- (void)getObjects:(id __unsafe_unretained _Nonnull * _Nonnull)objects range:(NSRange)range
{
	if (range.location + range.length > [self count]) {
		[NSException raise:NSRangeException
			    format:@"NSOrderedSet: the range {%lu, %lu} is beyond the end (%lu members)",
				   (unsigned long)range.location, (unsigned long)range.length,
				   (unsigned long)[self count]];
	}
	/* OVER THE DERIVED -array, which is over the PRIMITIVES: reading the ivar here left a concrete class
	 * with a different layout silently NOT filling the caller's buffer - the failure mode M3's audit
	 * exists to catch, found by the same audit here. */
	[[self array] getObjects:objects range:range];
}

/* ORDER IS PART OF THE VALUE, which is the whole reason this class exists: the same members in a
 * different order are NOT equal. */
- (BOOL)isEqualToOrderedSet:(NSOrderedSet *)other
{
	NSUInteger i;

	if (other == nil || [other count] != [self count]) {
		return NO;
	}
	for (i = 0; i < [self count]; i++) {
		if (![[self objectAtIndex:i] isEqual:[other objectAtIndex:i]]) {
			return NO;
		}
	}
	return YES;
}

- (BOOL)intersectsOrderedSet:(NSOrderedSet *)other
{
	NSUInteger i;

	if (other == nil) {
		return NO;
	}
	for (i = 0; i < [self count]; i++) {
		if ([other containsObject:[self objectAtIndex:i]]) {
			return YES;
		}
	}
	return NO;
}

- (BOOL)isSubsetOfOrderedSet:(NSOrderedSet *)other
{
	NSUInteger i;

	if (other == nil || [self count] > [other count]) {
		return NO;
	}
	for (i = 0; i < [self count]; i++) {
		if (![other containsObject:[self objectAtIndex:i]]) {
			return NO;
		}
	}
	return YES;
}

/* THE TWO SET QUESTIONS, answered by the SET VIEW rather than by a second walk: the question is about
 * membership, the ordered-ness is not part of it, and NSSet already answers both over its own -hash/-isEqual:
 * rules. Written this way, the ordered set and a set of the same members cannot disagree about either. */
- (BOOL)intersectsSet:(NSSet *)set
{
	return [[self set] intersectsSet:set];
}

- (BOOL)isSubsetOfSet:(NSSet *)set
{
	return [[self set] isSubsetOfSet:set];
}

- (instancetype)filteredOrderedSetUsingPredicate:(NSPredicate *)predicate
{
	NSMutableArray *kept = [NSMutableArray array];
	NSUInteger i;

	if (predicate == nil) {
		return self;
	}
	for (i = 0; i < [self count]; i++) {
		id object = [self objectAtIndex:i];

		if ([predicate evaluateWithObject:object]) {
			[kept addObject:object];
		}
	}
	return [[[self class] alloc] initWithArray:kept];
}

- (NSArray *)sortedArrayUsingDescriptors:(NSArray *)descriptors
{
	return [[self array] sortedArrayUsingDescriptors:descriptors];
}

/* THE PREDICATE SEARCHES, over this class's own primitives - the walk is the contract, so it is not delegated.
 * `-indexOfObjectPassingTest:` STOPS at the first match: a predicate may have side effects, and Apple's
 * contract is the LOWEST matching index rather than a survey of them, so implementing it as
 * `[[self indexesOfObjectsPassingTest:] firstIndex]` would answer the same number while calling the caller's
 * predicate for every element after the match. The indexes form is the exhaustive one, on purpose. */
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
	NSMutableIndexSet *matches = [NSMutableIndexSet indexSet];
	NSUInteger i;
	BOOL stop = NO;

	if (predicate == NULL) {
		return matches;
	}
	for (i = 0; i < [self count] && !stop; i++) {
		if (predicate([self objectAtIndex:i], i, &stop)) {
			[matches addIndex:i];
		}
	}
	return matches;
}

/* THE TWO COMPARATOR SORTS DELEGATE, exactly as `-sortedArrayUsingDescriptors:` above does: the array view IS
 * the same sequence - same members, same order, same indexes - so there is one sort implementation and it
 * cannot drift from itself. A nil comparator is the array family's own answer (a copy, unchanged). */
- (NSArray *)sortedArrayUsingComparator:(NSComparator)comparator
{
	return [[self array] sortedArrayUsingComparator:comparator];
}

- (NSArray *)sortedArrayWithOptions:(NSSortOptions)options usingComparator:(NSComparator)comparator
{
	return [[self array] sortedArrayWithOptions:options usingComparator:comparator];
}

/* THE BINARY SEARCH BY COMPARATOR, and it delegates for a reason that is specific to THIS door: its answer is
 * an INDEX, and an index into the ordered set and into its array view are the same number. Re-deriving it
 * here would create a second place the first-equal / last-equal / insertion rules live, for no gain. */
- (NSUInteger)indexOfObject:(id)object
	      inSortedRange:(NSRange)range
		  options:(NSBinarySearchingOptions)options
	      usingComparator:(NSComparator)comparator
{
	return [[self array] indexOfObject:object inSortedRange:range options:options
			     usingComparator:comparator];
}

- (BOOL)isEqual:(nullable id)other
{
	if (other == self) {
		return YES;
	}
	if (other == nil || ![other isKindOfClass:[NSOrderedSet class]]) {
		return NO;
	}
	return [self isEqualToOrderedSet:(NSOrderedSet *)other];
}

/* ORDER-SENSITIVE, because equality is: two ordered sets that are equal are equal member by member
 * in the same positions, so a hash that walks them in order keeps that promise. */
- (NSUInteger)hash
{
	NSUInteger hash = 5381;
	NSUInteger i;

	for (i = 0; i < [self count]; i++) {
		hash = ((hash << 5) + hash) ^ [[self objectAtIndex:i] hash];
	}
	return hash ^ [self count];
}

- (NSString *)description
{
	NSMutableString *out = [NSMutableString stringWithString:@"{("];
	NSUInteger i;

	for (i = 0; i < [self count]; i++) {
		if (i > 0) {
			[out appendString:@", "];
		}
		[out appendString:[[self objectAtIndex:i] description]];
	}
	[out appendString:@")}"];
	return out;
}

- (id)copy
{
	return [self retain];	/* +1: `copy` is an OWNED family (plan §15.2) */
}

- (id)mutableCopy
{
	return [[NSMutableOrderedSet alloc] initWithOrderedSet:self];
}

- (unsigned long)countByEnumeratingWithState:(NSFastEnumerationState *)state
				     objects:(id __unsafe_unretained *)buffer
				       count:(unsigned long)length
{
	unsigned long cursor = state->state;
	unsigned long produced = 0;
	unsigned long skip;
	NSEnumerator *enumerator = [self objectEnumerator];
	id object;

	/* OVER THE PRIMITIVES, THROUGH THE CALLER'S BUFFER: state->state is the cursor, so a short batch
	 * RESUMES, and `objects` is caller-provided scratch that stays valid for the batch. */
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

@end

@implementation NSMutableOrderedSet

+ (id)alloc
{
	if (self != [NSMutableOrderedSet class]) {
		return [super alloc];
	}
	return [AGOrderedSetMutable alloc];
}

- (Class)classForCoder
{
	return [NSMutableOrderedSet class];
}


+ (instancetype)orderedSetWithCapacity:(NSUInteger)capacity
{
	return [[self alloc] initWithCapacity:capacity];
}

- (instancetype)initWithCapacity:(NSUInteger)capacity
{
	(void)capacity;
	return [self initWithObjects:NULL count:0];
}

/* THE SAME DOOR ON THE MUTABLE CLASS, which Apple declares here too and which therefore needs an
 * implementation HERE - a declaration satisfied only by a SUPERCLASS's implementation is what
 * `--unimplemented` calls a declared-but-undefined selector, and it is right to: this class is the one a
 * caller named. `[super initWithCoder:]` reaches the front's, which funnels through `-initWithArray:` with
 * `self` still being the MUTABLE class - so the class-choosing rule (which sends only the immutable concrete
 * class to the shared empty instance) leaves a mutable answer mutable. */
- (nullable instancetype)initWithCoder:(NSCoder *)coder
{
	return [super initWithCoder:coder];
}

/* ONE PLACE REPLACES THE MEMBERS, so the mutation token moves exactly when the storage does. */
- (void)fnReplaceMembers:(NSArray *)members
{
	_members = [members copy];
	_mutations++;
}

- (void)addObject:(id)object
{
	NSMutableArray *members;

	if (object == nil || [self containsObject:object]) {
		return;			/* a set holds each value once */
	}
	members = [NSMutableArray arrayWithCapacity:[self count] + 1];
	[members addObjectsFromArray:[self array]];
	[members addObject:object];
	[self fnReplaceMembers:members];
}

- (void)addObjectsFromArray:(NSArray *)array
{
	NSUInteger i;

	for (i = 0; i < [array count]; i++) {
		[self addObject:[array objectAtIndex:i]];
	}
}

- (void)insertObject:(id)object atIndex:(NSUInteger)index
{
	NSMutableArray *members;

	if (object == nil || [self containsObject:object]) {
		return;
	}
	if (index > [self count]) {
		[NSException raise:NSRangeException
			    format:@"NSMutableOrderedSet: index %lu is beyond the end (%lu members)",
				   (unsigned long)index, (unsigned long)[self count]];
	}
	members = [NSMutableArray arrayWithCapacity:[self count] + 1];
	[members addObjectsFromArray:[[self array] subarrayWithRange:NSMakeRange(0, index)]];
	[members addObject:object];
	[members addObjectsFromArray:[[self array] subarrayWithRange:
					NSMakeRange(index, [self count] - index)]];
	[self fnReplaceMembers:members];
}

- (void)replaceObjectAtIndex:(NSUInteger)index withObject:(id)object
{
	NSMutableArray *members;
	NSUInteger existing;

	if (index >= [self count]) {
		[NSException raise:NSRangeException
			    format:@"NSMutableOrderedSet: index %lu is beyond the end (%lu members)",
				   (unsigned long)index, (unsigned long)[self count]];
	}
	if (object == nil) {
		return;
	}
	existing = [self indexOfObject:object];
	if (existing != NSNotFound && existing != index) {
		/* THE REPLACEMENT IS ALREADY A MEMBER: the set is left as it is rather than acquiring a
		 * duplicate, which is the only way to keep "each value once" true. */
		return;
	}
	members = [NSMutableArray arrayWithCapacity:[self count]];
	[members addObjectsFromArray:[self array]];
	[members replaceObjectAtIndex:index withObject:object];
	[self fnReplaceMembers:members];
}

- (void)setObject:(id)object atIndexedSubscript:(NSUInteger)index
{
	[self replaceObjectAtIndex:index withObject:object];
}

- (void)removeObject:(id)object
{
	NSUInteger index = [self indexOfObject:object];

	if (index == NSNotFound) {
		return;
	}
	[self removeObjectAtIndex:index];
}

- (void)removeObjectAtIndex:(NSUInteger)index
{
	NSMutableArray *members;

	if (index >= [self count]) {
		[NSException raise:NSRangeException
			    format:@"NSMutableOrderedSet: index %lu is beyond the end (%lu members)",
				   (unsigned long)index, (unsigned long)[self count]];
	}
	members = [NSMutableArray arrayWithCapacity:[self count]];
	[members addObjectsFromArray:[self array]];
	[members removeObjectAtIndex:index];
	[self fnReplaceMembers:members];
}

- (void)removeObjectsInRange:(NSRange)range
{
	NSMutableArray *members;

	if (range.location + range.length > [self count]) {
		[NSException raise:NSRangeException
			    format:@"NSMutableOrderedSet: the range {%lu, %lu} is beyond the end (%lu members)",
				   (unsigned long)range.location, (unsigned long)range.length,
				   (unsigned long)[self count]];
	}
	members = [NSMutableArray arrayWithCapacity:[self count]];
	[members addObjectsFromArray:[self array]];
	[members removeObjectsInRange:range];
	[self fnReplaceMembers:members];
}

- (void)removeAllObjects
{
	[self fnReplaceMembers:[NSArray array]];
}

- (void)exchangeObjectAtIndex:(NSUInteger)first withObjectAtIndex:(NSUInteger)second
{
	NSMutableArray *members;
	id held;

	if (first >= [self count] || second >= [self count]) {
		[NSException raise:NSRangeException
			    format:@"NSMutableOrderedSet: cannot exchange %lu and %lu of %lu members",
				   (unsigned long)first, (unsigned long)second, (unsigned long)[self count]];
	}
	members = [NSMutableArray arrayWithCapacity:[self count]];
	[members addObjectsFromArray:[self array]];
	held = [members objectAtIndex:first];
	[members replaceObjectAtIndex:first withObject:[members objectAtIndex:second]];
	[members replaceObjectAtIndex:second withObject:held];
	[self fnReplaceMembers:members];
}

- (void)unionOrderedSet:(NSOrderedSet *)other
{
	NSArray *theirs = [other array];
	NSUInteger i;

	for (i = 0; i < [theirs count]; i++) {
		[self addObject:[theirs objectAtIndex:i]];
	}
}

- (void)unionSet:(NSSet *)other
{
	NSArray *theirs = [other allObjects];
	NSUInteger i;

	for (i = 0; i < [theirs count]; i++) {
		[self addObject:[theirs objectAtIndex:i]];
	}
}

- (void)minusOrderedSet:(NSOrderedSet *)other
{
	NSArray *theirs = [other array];
	NSUInteger i;

	for (i = 0; i < [theirs count]; i++) {
		[self removeObject:[theirs objectAtIndex:i]];
	}
}

- (void)minusSet:(NSSet *)other
{
	NSArray *theirs = [other allObjects];
	NSUInteger i;

	for (i = 0; i < [theirs count]; i++) {
		[self removeObject:[theirs objectAtIndex:i]];
	}
}

- (void)intersectOrderedSet:(NSOrderedSet *)other
{
	NSMutableArray *kept = [NSMutableArray array];
	NSUInteger i;

	for (i = 0; i < [self count]; i++) {
		if ([other containsObject:[self objectAtIndex:i]]) {
			[kept addObject:[self objectAtIndex:i]];
		}
	}
	[self fnReplaceMembers:kept];
}

- (void)intersectSet:(NSSet *)other
{
	NSMutableArray *kept = [NSMutableArray array];
	NSUInteger i;

	for (i = 0; i < [self count]; i++) {
		if ([other containsObject:[self objectAtIndex:i]]) {
			[kept addObject:[self objectAtIndex:i]];
		}
	}
	[self fnReplaceMembers:kept];
}

- (void)sortUsingDescriptors:(NSArray *)descriptors
{
	[self fnReplaceMembers:[[self array] sortedArrayUsingDescriptors:descriptors]];
}

/* THE COMPARATOR SORTS, and the plain one IS the options one: `NSSortConcurrent` is a hint this library does
 * not take (`NSObjCRuntime.h` says why) and stability is a property of the algorithm rather than a branch, so
 * there is nothing for the options form to do differently. -sortRange:options:usingComparator: is the one with
 * a contract of its own: only the members INSIDE the range move. It is built as prefix + sorted slice + suffix
 * and handed to the same private door every mutation uses, so the consistency token moves exactly once. */
- (void)sortUsingComparator:(NSComparator)comparator
{
	[self sortWithOptions:0 usingComparator:comparator];
}

- (void)sortWithOptions:(NSSortOptions)options usingComparator:(NSComparator)comparator
{
	if (comparator == NULL) {
		return;
	}
	[self fnReplaceMembers:[[self array] sortedArrayWithOptions:options
						   usingComparator:comparator]];
}

- (void)sortRange:(NSRange)range options:(NSSortOptions)options usingComparator:(NSComparator)comparator
{
	NSArray *all;
	NSMutableArray *out;
	NSUInteger i;

	if (comparator == NULL) {
		return;
	}
	/* THE RANGE IS VALIDATED HERE rather than left to `-subarrayWithRange:`, which CLAMPS: a caller who asked
	 * for a range that is not there is making an error, and silently sorting a shorter one would answer a
	 * question they did not ask. Same contract as the construction family's range doors (§63.6). */
	if (range.location > [self count] || range.length > [self count] - range.location) {
		[NSException raise:NSRangeException
			    format:@"NSMutableOrderedSet: the range {%lu, %lu} is beyond the end (%lu members)",
				   (unsigned long)range.location, (unsigned long)range.length,
				   (unsigned long)[self count]];
	}
	all = [self array];
	out = [NSMutableArray arrayWithCapacity:[all count]];
	for (i = 0; i < range.location; i++) {
		[out addObject:[all objectAtIndex:i]];
	}
	[out addObjectsFromArray:[[all subarrayWithRange:range] sortedArrayWithOptions:options
								     usingComparator:comparator]];
	for (i = range.location + range.length; i < [all count]; i++) {
		[out addObject:[all objectAtIndex:i]];
	}
	[self fnReplaceMembers:out];
}

- (void)filterUsingPredicate:(NSPredicate *)predicate
{
	NSMutableArray *kept = [NSMutableArray array];
	NSUInteger i;

	if (predicate == nil) {
		return;
	}
	for (i = 0; i < [self count]; i++) {
		if ([predicate evaluateWithObject:[self objectAtIndex:i]]) {
			[kept addObject:[self objectAtIndex:i]];
		}
	}
	[self fnReplaceMembers:kept];
}

/* ===================================================================================================
 * THE INDEX-SET AND COUNT MUTATORS (§63.9). Every one of them REBUILDS the member array and hands it to
 * `-fnReplaceMembers:`, which is the single door every mutation in this class uses - so the for-in
 * consistency token moves exactly once per call, however many members moved.
 *
 * TWO RULES ARE SHARED AND THEREFORE STATED ONCE HERE RATHER THAN IN EACH METHOD:
 *   * A RANGE OR AN INDEX OUTSIDE THE RECEIVER RAISES NSRangeException - the same contract the construction
 *     family's range doors (§63.6) and `-sortRange:options:usingComparator:` (§63.8) took. Index sets are
 *     VALIDATED UP FRONT, before a single member is touched, so a refused call changes nothing.
 *   * THE SET RULE HOLDS THROUGH EVERY ONE OF THEM: a member is in the result once, and an operation that
 *     would introduce a second copy of one already present does not.
 * =================================================================================================== */

- (void)addObjects:(const id _Nonnull * _Nullable)objects count:(NSUInteger)count
{
	NSMutableArray *out = [NSMutableArray arrayWithArray:[self array]];
	NSUInteger i;

	for (i = 0; i < count; i++) {
		if (objects[i] != nil && ![out containsObject:objects[i]]) {
			[out addObject:objects[i]];
		}
	}
	[self fnReplaceMembers:out];
}

- (void)removeObjectsInArray:(NSArray *)array
{
	NSArray *all = [self array];
	NSMutableArray *out = [NSMutableArray arrayWithCapacity:[all count]];
	NSUInteger i;

	for (i = 0; i < [all count]; i++) {
		if (![array containsObject:[all objectAtIndex:i]]) {
			[out addObject:[all objectAtIndex:i]];
		}
	}
	[self fnReplaceMembers:out];
}

/* REMOVING BY POSITION IS THE ONE THAT CANNOT BE DONE ASCENDING, which is why it is written as "the members
 * whose position is NOT in the set": walking the index set upward while removing would shift every later
 * position out from under itself. */
- (void)removeObjectsAtIndexes:(NSIndexSet *)indexes
{
	NSArray *all;
	NSMutableArray *out;
	NSUInteger index;
	NSUInteger i;

	if (indexes == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSMutableOrderedSet: -removeObjectsAtIndexes: needs an index set"];
	}
	for (index = [indexes firstIndex]; index != NSNotFound;
	     index = [indexes indexGreaterThanIndex:index]) {
		if (index >= [self count]) {
			[NSException raise:NSRangeException
				    format:@"NSMutableOrderedSet: index %lu is beyond bounds %lu",
					   (unsigned long)index, (unsigned long)[self count]];
		}
	}
	all = [self array];
	out = [NSMutableArray arrayWithCapacity:[all count]];
	for (i = 0; i < [all count]; i++) {
		if (![indexes containsIndex:i]) {
			[out addObject:[all objectAtIndex:i]];
		}
	}
	[self fnReplaceMembers:out];
}

/* ONE INDEX PER OBJECT, AND THE INDEXES ARE POSITIONS IN THE RESULT - which is what makes the ascending walk
 * correct: each insertion shifts what follows, and the next index is already stated in terms of the shifted
 * set. A count mismatch is a caller error and raises rather than silently inserting what it can. */
- (void)insertObjects:(NSArray *)objects atIndexes:(NSIndexSet *)indexes
{
	NSMutableArray *out;
	NSUInteger index;
	NSUInteger i = 0;

	if (indexes == nil || [objects count] != [indexes count]) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSMutableOrderedSet: -insertObjects:atIndexes: needs one index per object "
				   "(%lu object(s), %lu index(es))",
				   (unsigned long)[objects count], (unsigned long)[indexes count]];
	}
	out = [NSMutableArray arrayWithArray:[self array]];
	for (index = [indexes firstIndex]; index != NSNotFound;
	     index = [indexes indexGreaterThanIndex:index]) {
		id object = [objects objectAtIndex:i++];

		if (index > [out count]) {
			[NSException raise:NSRangeException
				    format:@"NSMutableOrderedSet: insertion index %lu is beyond bounds %lu",
					   (unsigned long)index, (unsigned long)[out count]];
		}
		if (object != nil && ![out containsObject:object]) {
			[out insertObject:object atIndex:index];
		}
	}
	[self fnReplaceMembers:out];
}

/* `-setObject:atIndex:` IS `-replaceObjectAtIndex:withObject:` in Apple's header too, so it DELEGATES rather
 * than being a second implementation of the same rule - the anti-drift choice this class's read doors made
 * (§63.7), for the same reason. */
- (void)setObject:(id)object atIndex:(NSUInteger)index
{
	[self replaceObjectAtIndex:index withObject:object];
}

/* THE RANGE FORM MAY CHANGE THE COUNT, which is why it is not a swap: the members in the range are replaced
 * by `count` NEW ones, so the set can grow or shrink. The tail is deduplicated AGAINST THE RESULT, because a
 * set cannot hold a member twice and the replacement may already contain one that is also in the tail. */
- (void)replaceObjectsInRange:(NSRange)range
		  withObjects:(const id _Nonnull * _Nullable)objects
			count:(NSUInteger)count
{
	NSArray *all = [self array];
	NSMutableArray *out = [NSMutableArray arrayWithCapacity:[all count] + count];
	NSUInteger i;

	if (range.location > [all count] || range.length > [all count] - range.location) {
		[NSException raise:NSRangeException
			    format:@"NSMutableOrderedSet: the range {%lu, %lu} is beyond the end (%lu members)",
				   (unsigned long)range.location, (unsigned long)range.length,
				   (unsigned long)[all count]];
	}
	for (i = 0; i < range.location; i++) {
		[out addObject:[all objectAtIndex:i]];
	}
	for (i = 0; i < count; i++) {
		if (objects[i] != nil && ![out containsObject:objects[i]]) {
			[out addObject:objects[i]];
		}
	}
	for (i = range.location + range.length; i < [all count]; i++) {
		if (![out containsObject:[all objectAtIndex:i]]) {
			[out addObject:[all objectAtIndex:i]];
		}
	}
	[self fnReplaceMembers:out];
}

/* THE INDEX-SET FORM OF THE SAME IDEA, one replacement per position, walked ASCENDING with the objects
 * cursor kept in step - so the pairing is "the nth index gets the nth object", which is what "one per
 * position" means when both sides are in ascending order. */
- (void)replaceObjectsAtIndexes:(NSIndexSet *)indexes withObjects:(NSArray *)objects
{
	NSArray *all;
	NSMutableArray *out;
	NSUInteger index;
	NSUInteger cursor = 0;
	NSUInteger i;

	if (indexes == nil || [objects count] != [indexes count]) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSMutableOrderedSet: -replaceObjectsAtIndexes:withObjects: needs one object "
				   "per index (%lu index(es), %lu object(s))",
				   (unsigned long)[indexes count], (unsigned long)[objects count]];
	}
	for (index = [indexes firstIndex]; index != NSNotFound;
	     index = [indexes indexGreaterThanIndex:index]) {
		if (index >= [self count]) {
			[NSException raise:NSRangeException
				    format:@"NSMutableOrderedSet: index %lu is beyond bounds %lu",
					   (unsigned long)index, (unsigned long)[self count]];
		}
	}
	all = [self array];
	out = [NSMutableArray arrayWithCapacity:[all count]];
	index = [indexes firstIndex];
	for (i = 0; i < [all count]; i++) {
		id member;

		if (index == i) {
			member = [objects objectAtIndex:cursor++];
			index = [indexes indexGreaterThanIndex:index];
		} else {
			member = [all objectAtIndex:i];
		}
		if (member != nil && ![out containsObject:member]) {
			[out addObject:member];
		}
	}
	[self fnReplaceMembers:out];
}

/* MOVING BY POSITION: the members at the given indexes are removed and re-inserted together at `destination`,
 * which is an index IN THE RESULT AFTER THE REMOVAL - so a destination past what is left is clamped to the
 * end rather than raising, because "move them to the end" is what a caller means by a large number. */
- (void)moveObjectsAtIndexes:(NSIndexSet *)indexes toIndex:(NSUInteger)destination
{
	NSArray *all;
	NSMutableArray *moving = [NSMutableArray array];
	NSMutableArray *out;
	NSUInteger index;
	NSUInteger placed = 0;
	NSUInteger i;

	if (indexes == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSMutableOrderedSet: -moveObjectsAtIndexes:toIndex: needs an index set"];
	}
	for (index = [indexes firstIndex]; index != NSNotFound;
	     index = [indexes indexGreaterThanIndex:index]) {
		if (index >= [self count]) {
			[NSException raise:NSRangeException
				    format:@"NSMutableOrderedSet: index %lu is beyond bounds %lu",
					   (unsigned long)index, (unsigned long)[self count]];
		}
	}
	all = [self array];
	for (index = [indexes firstIndex]; index != NSNotFound;
	     index = [indexes indexGreaterThanIndex:index]) {
		[moving addObject:[all objectAtIndex:index]];
	}
	if (destination > [all count] - [moving count]) {
		destination = [all count] - [moving count];
	}
	out = [NSMutableArray arrayWithCapacity:[all count]];
	for (i = 0; i < [all count]; i++) {
		if ([indexes containsIndex:i]) {
			continue;
		}
		if (placed == destination) {
			[out addObjectsFromArray:moving];
			placed += [moving count];
		}
		[out addObject:[all objectAtIndex:i]];
		placed++;
	}
	if (placed <= destination) {
		[out addObjectsFromArray:moving];
	}
	[self fnReplaceMembers:out];
}

/* A MUTABLE ORDERED SET COPIES BY VALUE, and a copy of one is the immutable snapshot the house's
 * rule asks for. */
- (id)copy
{
	return [[NSOrderedSet alloc] initWithArray:[self array]];
}

- (id)mutableCopy
{
	return [[NSMutableOrderedSet alloc] initWithArray:[self array]];
}

@end



/* ===================================================================================================
 * THE CONCRETE CLASSES (§C.3 items 2, 3 and 8).
 * =================================================================================================== */

@implementation AGOrderedSetItems

/* [[NSOrderedSet alloc] init] IS A LEGITIMATE THING TO WRITE (§C.3 item 1) AND IT IS THE EMPTY CASE. */
- (instancetype)init
{
	[self release];
	return (id)[AGOrderedSetEmpty emptyOrderedSet];
}

@end

@implementation AGOrderedSetEmpty

+ (AGOrderedSetEmpty *)emptyOrderedSet
{
	static AGOrderedSetEmpty *shared = nil;

	if (shared == nil) {
		shared = [[AGOrderedSetEmpty alloc] init];
	}
	return shared;
}

/* IMMORTAL, the price of a singleton in a library with no `+allocWithZone:` and no collector. */
- (id)retain { return self; }
- (void)release { }
- (id)autorelease { return self; }
- (NSUInteger)retainCount { return NSUIntegerMax; }

/* THE PRIMITIVES (§C.3 item 5) AND NOTHING ELSE. */
- (NSUInteger)count
{
	return 0;
}

- (nullable id)objectAtIndex:(NSUInteger)index
{
	/* An ordered set with nothing in it has no such index: Cocoa RAISES, and the probe's third-party case is
	 * what keeps this honest for a subclass with the same shape. */
	[NSException raise:NSRangeException
		    format:@"-[NSOrderedSet objectAtIndex:]: index %lu beyond bounds for empty ordered set",
			   (unsigned long)index];
	return nil;
}

- (NSEnumerator *)objectEnumerator
{
	return [[NSArray array] objectEnumerator];
}

@end

@implementation AGOrderedSetMutable

/* NOTHING TO IMPLEMENT: NSMutableOrderedSet's implementation IS the mutable storage implementation, and
 * what a caller gains is the NAME that -class answers. */

@end
