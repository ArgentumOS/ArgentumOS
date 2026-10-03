/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSCountedSet.m — the set that remembers how many of each (F13.8d). MANUAL OWNERSHIP.
 *
 * The counts are INDEX-ALIGNED with the inherited member array: _counts[i] is how many times
 * _members[i] was added. Everything here keeps those two in step.
 *
 * THE INITIALISERS GO THROUGH -addObject:, and that is not a stylistic choice: the superclass's
 * -initWithArray: DEDUPLICATES, so an array holding "x" twice would arrive as one member and the
 * multiplicity — the entire point of this class — would be gone before the counts were built.
 * Starting from an empty set and adding one element at a time lets the counts see the duplicates.
 *
 * NSSet's own -addObjectsFromArray:, -unionSet: and -minusSet: already route through -addObject: and
 * -removeObject:, which land here, so they need no override. -intersectSet:, -filterUsingPredicate:
 * and -removeAllObjects: DO: they replace the member array directly and would leave the counts
 * describing members that are gone.
 */

#import <Foundation/NSCountedSet.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSNumber.h>

/* THE COUNTED SET'S OWN CONCRETE CLASS (plan §C.5, M3: "counted-set storage as its own concrete class").
 * The storage is NSCountedSet's own ivar - a count array index-aligned with the inherited member array -
 * so this class adds no code and is the NAME that -class answers. */
@interface AGCountedSet : NSCountedSet
@end

@implementation NSCountedSet

/* THE DOOR (§C.3 item 1), routed exactly once, and the counted class answers ITSELF to an archiver
 * (§C.3 item 4). */
+ (id)alloc
{
	if (self != [NSCountedSet class]) {
		return [super alloc];
	}
	return [AGCountedSet alloc];
}

- (Class)classForCoder
{
	return [NSCountedSet class];
}


+ (instancetype)setWithCapacity:(NSUInteger)capacity
{
	return [[self alloc] initWithCapacity:capacity];
}

/* THE INITIALISER FUNNEL: NSMutableSet's -initWithCapacity: calls -initWithObjects:count:, NSSet's
 * -initWithSet: calls -initWithArray:, and both of those are overridden here — so every door into an
 * NSCountedSet passes through one of these two. */
- (instancetype)initWithObjects:(const id _Nonnull * _Nullable)objects count:(NSUInteger)count
{
	NSUInteger i;

	self = [super initWithObjects:NULL count:0];
	if (self == nil) {
		return nil;
	}
	_counts = [NSMutableArray array];
	for (i = 0; i < count; i++) {
		if (objects[i] != nil) {
			[self addObject:objects[i]];
		}
	}
	return self;
}

- (instancetype)initWithArray:(NSArray *)array
{
	NSUInteger i;

	self = [super initWithObjects:NULL count:0];
	if (self == nil) {
		return nil;
	}
	_counts = [NSMutableArray array];
	for (i = 0; i < [array count]; i++) {
		[self addObject:[array objectAtIndex:i]];
	}
	return self;
}

- (instancetype)initWithCapacity:(NSUInteger)capacity
{
	(void)capacity;
	return [self initWithObjects:NULL count:0];
}

/* BY VALUE, like the set itself: the index of the member that is EQUAL to `object`, or -1. */
- (NSInteger)fnIndexOfMember:(id)object
{
	id stored;

	if (object == nil) {
		return -1;
	}
	stored = [self member:object];
	if (stored == nil) {
		return -1;
	}
	return (NSInteger)[_members indexOfObject:stored];
}

- (void)addObject:(id)object
{
	NSInteger index;

	if (object == nil) {
		return;
	}
	index = [self fnIndexOfMember:object];
	if (index >= 0) {
		/* ALREADY A MEMBER — even one that is merely EQUAL to what is here — so only the count
		 * moves. This is the line that makes it a counted set rather than a set. */
		NSUInteger count = [[_counts objectAtIndex:(NSUInteger)index] unsignedIntegerValue] + 1;

		[_counts replaceObjectAtIndex:(NSUInteger)index
				   withObject:[NSNumber numberWithUnsignedInteger:count]];
		return;
	}
	[super addObject:object];
	[_counts addObject:[NSNumber numberWithUnsignedInteger:1]];
}

- (void)removeObject:(id)object
{
	NSInteger index = [self fnIndexOfMember:object];

	if (index < 0) {
		return;			/* nothing to take away */
	}
	if ([[_counts objectAtIndex:(NSUInteger)index] unsignedIntegerValue] > 1) {
		NSUInteger count = [[_counts objectAtIndex:(NSUInteger)index] unsignedIntegerValue] - 1;

		[_counts replaceObjectAtIndex:(NSUInteger)index
				   withObject:[NSNumber numberWithUnsignedInteger:count]];
		return;
	}
	/* THE LAST ONE GOES: both arrays drop the same index, so they stay aligned. */
	[_counts removeObjectAtIndex:(NSUInteger)index];
	[super removeObject:object];
}

- (NSUInteger)countForObject:(id)object
{
	NSInteger index = [self fnIndexOfMember:object];

	if (index < 0) {
		return 0;
	}
	return [[_counts objectAtIndex:(NSUInteger)index] unsignedIntegerValue];
}

/* THESE THREE REPLACE THE MEMBER ARRAY BEHIND THE PUBLIC DOORS — the superclass versions call
 * -fnReplaceMembers: — so inheriting them would leave the counts describing members that are gone.
 * Each therefore rebuilds its own counts alongside. */
- (void)removeAllObjects
{
	[super removeAllObjects];
	[_counts removeAllObjects];
}

- (void)intersectSet:(NSSet *)other
{
	NSMutableArray *keptCounts = [NSMutableArray array];
	NSUInteger i;

	for (i = 0; i < [_members count]; i++) {
		if ([other containsObject:[_members objectAtIndex:i]]) {
			[keptCounts addObject:[_counts objectAtIndex:i]];
		}
	}
	[super intersectSet:other];
	_counts = keptCounts;
}

@end

@implementation AGCountedSet

/* NOTHING TO IMPLEMENT: NSCountedSet's implementation IS the counted storage implementation, and what a
 * caller gains is the NAME that -class answers. */

@end
