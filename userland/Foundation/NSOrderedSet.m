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

- (NSSet *)set
{
	return [NSSet setWithArray:[self array]];
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

- (void)enumerateObjectsUsingBlock:(void (^)(id object, BOOL *stop))block
{
	NSUInteger i;
	BOOL stop = NO;

	if (block == NULL) {
		return;
	}
	for (i = 0; i < [self count]; i++) {
		block([self objectAtIndex:i], &stop);
		if (stop) {
			break;
		}
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
