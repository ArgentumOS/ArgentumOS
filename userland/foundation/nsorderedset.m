/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * nsorderedset.m — a set that remembers the order (F13.8e). MANUAL OWNERSHIP.
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

#import <foundation/NSMutableOrderedSet.h>
#import <foundation/NSArray.h>
#import <foundation/NSEnumerator.h>
#import <foundation/NSSet.h>
#import <foundation/NSPredicate.h>
#import <foundation/NSException.h>
#import <foundation/NSString.h>

@implementation NSOrderedSet

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
	for (i = 0; i < [_members count]; i++) {
		if ([[_members objectAtIndex:i] isEqual:object]) {
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
	return [_members count] > 0 ? [_members objectAtIndex:0] : nil;
}

- (nullable id)lastObject
{
	return [_members count] > 0 ? [_members objectAtIndex:[_members count] - 1] : nil;
}

- (NSArray *)array
{
	return _members;
}

- (NSSet *)set
{
	return [NSSet setWithArray:_members];
}

- (NSEnumerator *)objectEnumerator
{
	return [_members objectEnumerator];
}

- (NSEnumerator *)reverseObjectEnumerator
{
	NSMutableArray *reversed = [NSMutableArray arrayWithCapacity:[_members count]];
	NSUInteger i = [_members count];

	/* Built by hand rather than borrowed from NSArray: this class promises the reversal, and
	 * whether the array underneath offers one of its own is not part of this class's contract. */
	while (i > 0) {
		i--;
		[reversed addObject:[_members objectAtIndex:i]];
	}
	return [reversed objectEnumerator];
}

- (void)enumerateObjectsUsingBlock:(void (^)(id object, BOOL *stop))block
{
	NSUInteger i;
	BOOL stop = NO;

	if (block == NULL) {
		return;
	}
	for (i = 0; i < [_members count]; i++) {
		block([_members objectAtIndex:i], &stop);
		if (stop) {
			break;
		}
	}
}

- (void)getObjects:(id __unsafe_unretained _Nonnull * _Nonnull)objects range:(NSRange)range
{
	if (range.location + range.length > [_members count]) {
		[NSException raise:NSRangeException
			    format:@"NSOrderedSet: the range {%lu, %lu} is beyond the end (%lu members)",
				   (unsigned long)range.location, (unsigned long)range.length,
				   (unsigned long)[_members count]];
	}
	[_members getObjects:objects range:range];
}

/* ORDER IS PART OF THE VALUE, which is the whole reason this class exists: the same members in a
 * different order are NOT equal. */
- (BOOL)isEqualToOrderedSet:(NSOrderedSet *)other
{
	NSUInteger i;

	if (other == nil || [other count] != [self count]) {
		return NO;
	}
	for (i = 0; i < [_members count]; i++) {
		if (![[_members objectAtIndex:i] isEqual:[other objectAtIndex:i]]) {
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
	for (i = 0; i < [_members count]; i++) {
		if ([other containsObject:[_members objectAtIndex:i]]) {
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
	for (i = 0; i < [_members count]; i++) {
		if (![other containsObject:[_members objectAtIndex:i]]) {
			return NO;
		}
	}
	return YES;
}

- (instancetype)filteredOrderedSetUsingPredicate:(NSPredicate *)predicate
{
	NSMutableArray *kept = [NSMutableArray array];
	NSUInteger i;

	if (predicate == nil) {
		return self;
	}
	for (i = 0; i < [_members count]; i++) {
		id object = [_members objectAtIndex:i];

		if ([predicate evaluateWithObject:object]) {
			[kept addObject:object];
		}
	}
	return [[[self class] alloc] initWithArray:kept];
}

- (NSArray *)sortedArrayUsingDescriptors:(NSArray *)descriptors
{
	return [_members sortedArrayUsingDescriptors:descriptors];
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

	for (i = 0; i < [_members count]; i++) {
		hash = ((hash << 5) + hash) ^ [[_members objectAtIndex:i] hash];
	}
	return hash ^ [_members count];
}

- (NSString *)description
{
	NSMutableString *out = [NSMutableString stringWithString:@"{("];
	NSUInteger i;

	for (i = 0; i < [_members count]; i++) {
		if (i > 0) {
			[out appendString:@", "];
		}
		[out appendString:[[_members objectAtIndex:i] description]];
	}
	[out appendString:@")}"];
	return out;
}

- (id)copyWithZone:(nullable NSZone *)zone
{
	(void)zone;
	return self;
}

- (id)mutableCopyWithZone:(nullable NSZone *)zone
{
	(void)zone;
	return [[NSMutableOrderedSet alloc] initWithOrderedSet:self];
}

- (unsigned long)countByEnumeratingWithState:(NSFastEnumerationState *)state
				     objects:(id __unsafe_unretained *)buffer
				       count:(unsigned long)length
{
	NSUInteger start;
	NSUInteger remaining;
	NSUInteger batch;

	if (length == 0) {
		return 0;
	}
	start = (NSUInteger)state->state;
	if (start >= [_members count]) {
		return 0;
	}
	remaining = [_members count] - start;
	batch = remaining < (NSUInteger)length ? remaining : (NSUInteger)length;
	if (start == 0) {
		state->mutationsPtr = &_mutations;
	}
	[_members getObjects:buffer range:NSMakeRange(start, batch)];
	state->itemsPtr = buffer;
	state->state = (unsigned long)(start + batch);
	return (unsigned long)batch;
}

@end

@implementation NSMutableOrderedSet

+ (instancetype)orderedSetWithCapacity:(NSUInteger)capacity
{
	return [[self alloc] initWithCapacity:capacity];
}

- (instancetype)initWithCapacity:(NSUInteger)capacity
{
	(void)capacity;
	return [self initWithObjects:NULL count:0];
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
	[members addObjectsFromArray:_members];
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
	[members addObjectsFromArray:[_members subarrayWithRange:NSMakeRange(0, index)]];
	[members addObject:object];
	[members addObjectsFromArray:[_members subarrayWithRange:
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
	[members addObjectsFromArray:_members];
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
	[members addObjectsFromArray:_members];
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
	[members addObjectsFromArray:_members];
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
	[members addObjectsFromArray:_members];
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

	for (i = 0; i < [_members count]; i++) {
		if ([other containsObject:[_members objectAtIndex:i]]) {
			[kept addObject:[_members objectAtIndex:i]];
		}
	}
	[self fnReplaceMembers:kept];
}

- (void)intersectSet:(NSSet *)other
{
	NSMutableArray *kept = [NSMutableArray array];
	NSUInteger i;

	for (i = 0; i < [_members count]; i++) {
		if ([other containsObject:[_members objectAtIndex:i]]) {
			[kept addObject:[_members objectAtIndex:i]];
		}
	}
	[self fnReplaceMembers:kept];
}

- (void)sortUsingDescriptors:(NSArray *)descriptors
{
	[self fnReplaceMembers:[_members sortedArrayUsingDescriptors:descriptors]];
}

- (void)filterUsingPredicate:(NSPredicate *)predicate
{
	NSMutableArray *kept = [NSMutableArray array];
	NSUInteger i;

	if (predicate == nil) {
		return;
	}
	for (i = 0; i < [_members count]; i++) {
		if ([predicate evaluateWithObject:[_members objectAtIndex:i]]) {
			[kept addObject:[_members objectAtIndex:i]];
		}
	}
	[self fnReplaceMembers:kept];
}

/* A MUTABLE ORDERED SET COPIES BY VALUE, and a copy of one is the immutable snapshot the house's
 * rule asks for. */
- (id)copyWithZone:(nullable NSZone *)zone
{
	(void)zone;
	return [[NSOrderedSet alloc] initWithArray:_members];
}

- (id)mutableCopyWithZone:(nullable NSZone *)zone
{
	(void)zone;
	return [[NSMutableOrderedSet alloc] initWithArray:_members];
}

@end
