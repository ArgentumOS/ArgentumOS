/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * nsset.m — the unordered collection with no duplicates (F13.8).
 *
 * ARC file. The two design facts from the header are what this file IS, so they bear repeating:
 * members are RETAINED and lookup is LINEAR by -hash/-isEqual: (not a borrowed dictionary, which
 * would copy its keys), and every mutation REPLACES the member array rather than editing it.
 *
 * FAST ENUMERATION IS THE PROTOCOL'S, done properly: each call fills the CALLER'S buffer with one
 * batch from the member array and advances state->state by what it delivered. Nothing is allocated
 * per loop, and a mutation during the loop is caught by mutationsPtr — the protocol's own answer to
 * that, and a raise rather than a quiet wrong answer.
 */

#import <foundation/NSSet.h>
#import <foundation/NSArray.h>
#import <foundation/NSEnumerator.h>
#import <foundation/NSPredicate.h>
#import <foundation/NSString.h>

@implementation NSSet

+ (instancetype)set
{
	return [[self alloc] initWithObjects:NULL count:0];
}

+ (instancetype)setWithObject:(id)object
{
	return [[self alloc] initWithObjects:&object count:1];
}

+ (instancetype)setWithObjects:(const id _Nonnull * _Nullable)objects count:(NSUInteger)count
{
	return [[self alloc] initWithObjects:objects count:count];
}

+ (instancetype)setWithArray:(NSArray *)array
{
	return [[self alloc] initWithArray:array];
}

+ (instancetype)setWithSet:(NSSet *)set
{
	return [[self alloc] initWithSet:set];
}

- (instancetype)initWithObjects:(const id _Nonnull * _Nullable)objects count:(NSUInteger)count
{
	NSMutableArray *members;
	NSUInteger i;

	self = [super init];
	if (self == nil) {
		return nil;
	}
	members = [NSMutableArray array];
	for (i = 0; i < count; i++) {
		id object = objects[i];
		NSUInteger j;
		BOOL seen = NO;

		if (object == nil) {
			continue;
		}
		/* DEDUPLICATION IS BY VALUE, which is the whole contract: the FIRST of two equal members
		 * is the one that stays, as in Cocoa. */
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
	/* A SNAPSHOT, so -allObjects hands out nothing that can be mutated through this class. */
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
	members = [NSMutableArray array];
	for (i = 0; i < [array count]; i++) {
		id object = [array objectAtIndex:i];
		NSUInteger j;
		BOOL seen = NO;

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

- (instancetype)initWithSet:(NSSet *)set
{
	return [self initWithArray:[set allObjects]];
}

- (NSUInteger)count
{
	return [_members count];
}

- (nullable id)member:(id)object
{
	NSUInteger i;

	if (object == nil) {
		return nil;
	}
	for (i = 0; i < [_members count]; i++) {
		id candidate = [_members objectAtIndex:i];

		if ([candidate isEqual:object]) {
			return candidate;
		}
	}
	return nil;
}

- (BOOL)containsObject:(id)object
{
	return [self member:object] != nil;
}

- (nullable id)anyObject
{
	return [_members count] > 0 ? [_members objectAtIndex:0] : nil;
}

- (NSArray *)allObjects
{
	return _members;
}

- (NSEnumerator *)objectEnumerator
{
	return [_members objectEnumerator];
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

- (BOOL)isEqualToSet:(NSSet *)other
{
	NSUInteger i;

	if (other == nil || [other count] != [self count]) {
		return NO;
	}
	for (i = 0; i < [_members count]; i++) {
		if (![other containsObject:[_members objectAtIndex:i]]) {
			return NO;
		}
	}
	return YES;
}

- (BOOL)isSubsetOfSet:(NSSet *)other
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

- (BOOL)intersectsSet:(NSSet *)other
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

- (instancetype)setByAddingObject:(id)object
{
	NSMutableSet *copy = [self mutableCopy];

	[copy addObject:object];
	return copy;
}

- (instancetype)setByAddingObjectsFromSet:(NSSet *)other
{
	NSMutableSet *copy = [self mutableCopy];

	[copy unionSet:other];
	return copy;
}

- (instancetype)setByAddingObjectsFromArray:(NSArray *)other
{
	NSMutableSet *copy = [self mutableCopy];

	[copy addObjectsFromArray:other];
	return copy;
}

- (NSArray *)sortedArrayUsingDescriptors:(NSArray *)descriptors
{
	return [_members sortedArrayUsingDescriptors:descriptors];
}

- (instancetype)filteredSetUsingPredicate:(NSPredicate *)predicate
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

- (BOOL)isEqual:(id)other
{
	if (other == self) {
		return YES;
	}
	if (other == nil || ![other isKindOfClass:[NSSet class]]) {
		return NO;
	}
	return [self isEqualToSet:(NSSet *)other];
}

/* ORDER-INDEPENDENT BY CONSTRUCTION, which is what a set's hash has to be: equal sets hash alike.
 * Counting is enough to be correct; it is not trying to be clever. */
- (NSUInteger)hash
{
	return [_members count];
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

/* Immutable, so copying is itself and a mutable copy is a real one. */
- (id)copyWithZone:(nullable NSZone *)zone
{
	(void)zone;
	return self;
}

- (id)mutableCopyWithZone:(nullable NSZone *)zone
{
	(void)zone;
	return [[NSMutableSet alloc] initWithSet:self];
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
		/* The consistency token, set once: the runtime compares it and raises if the set changed
		 * while the loop was running. */
		state->mutationsPtr = &_mutations;
	}
	[_members getObjects:buffer range:NSMakeRange(start, batch)];
	state->itemsPtr = buffer;
	state->state = (unsigned long)(start + batch);
	return (unsigned long)batch;
}

@end

@implementation NSMutableSet

+ (instancetype)setWithCapacity:(NSUInteger)capacity
{
	return [[self alloc] initWithCapacity:capacity];
}

- (instancetype)initWithCapacity:(NSUInteger)capacity
{
	(void)capacity;
	return [self initWithObjects:NULL count:0];
}

/* ONE PLACE REPLACES THE MEMBERS, so the mutation token is bumped exactly when the storage changes
 * and never by accident. */
- (void)fnReplaceMembers:(NSArray *)members
{
	_members = [members copy];
	_mutations++;
}

- (void)addObject:(id)object
{
	NSMutableArray *members;

	if (object == nil || [self member:object] != nil) {
		return;			/* a set has no duplicates, and nil is not a member */
	}
	members = [NSMutableArray arrayWithCapacity:[self count] + 1];
	[members addObjectsFromArray:_members];
	[members addObject:object];
	[self fnReplaceMembers:members];
}

- (void)removeObject:(id)object
{
	NSMutableArray *members;
	NSUInteger i;

	if (object == nil) {
		return;
	}
	members = [NSMutableArray arrayWithCapacity:[self count]];
	for (i = 0; i < [_members count]; i++) {
		id candidate = [_members objectAtIndex:i];

		if (![candidate isEqual:object]) {
			[members addObject:candidate];
		}
	}
	[self fnReplaceMembers:members];
}

- (void)removeAllObjects
{
	[self fnReplaceMembers:[NSArray array]];
}

- (void)addObjectsFromArray:(NSArray *)array
{
	NSUInteger i;

	for (i = 0; i < [array count]; i++) {
		[self addObject:[array objectAtIndex:i]];
	}
}

- (void)unionSet:(NSSet *)other
{
	[self addObjectsFromArray:[other allObjects]];
}

- (void)minusSet:(NSSet *)other
{
	NSArray *theirs = [other allObjects];
	NSUInteger i;

	for (i = 0; i < [theirs count]; i++) {
		[self removeObject:[theirs objectAtIndex:i]];
	}
}

- (void)intersectSet:(NSSet *)other
{
	NSMutableArray *members;
	NSUInteger i;

	members = [NSMutableArray arrayWithCapacity:[self count]];
	for (i = 0; i < [_members count]; i++) {
		id candidate = [_members objectAtIndex:i];

		if ([other containsObject:candidate]) {
			[members addObject:candidate];
		}
	}
	[self fnReplaceMembers:members];
}

- (void)setSet:(NSSet *)other
{
	[self fnReplaceMembers:[other allObjects]];
}

- (void)filterUsingPredicate:(NSPredicate *)predicate
{
	NSSet *kept;

	if (predicate == nil) {
		return;
	}
	kept = [[NSSet alloc] initWithArray:_members];
	[self fnReplaceMembers:[[kept filteredSetUsingPredicate:predicate] allObjects]];
}

/* A MUTABLE SET COPIES BY VALUE — the members, not a shared reference — and a copy of one is the
 * immutable snapshot the house's rule asks for. */
- (id)copyWithZone:(nullable NSZone *)zone
{
	NSSet *snapshot;

	(void)zone;
	snapshot = [[NSSet alloc] initWithArray:_members];
	return snapshot;
}

- (id)mutableCopyWithZone:(nullable NSZone *)zone
{
	(void)zone;
	return [[NSMutableSet alloc] initWithArray:_members];
}

@end
