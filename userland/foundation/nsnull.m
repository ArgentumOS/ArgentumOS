/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * nsnull.m — the object that stands for "nothing" (F13.8c). MANUAL OWNERSHIP.
 *
 * THE SINGLETON IS ENFORCED AT ALLOCATION, which is the part worth writing down: +null alone would
 * leave `[[NSNull alloc] init]` free to build a second one, and then two "the null object"s would
 * not be equal to each other. Routing allocWithZone: through the shared instance makes every door
 * answer THE instance.
 */

#import <foundation/NSNull.h>
#import <foundation/NSString.h>

static NSNull *fn_shared_null = nil;

@implementation NSNull

+ (NSNull *)null
{
	/* The one place a new instance is made, and it goes to the SUPERCLASS's allocator — `[super
	 * allocWithZone:]`, NOT `[NSObject allocWithZone:]`: the former keeps `self` (so the instance is
	 * an NSNull) and skips this class's override, while the latter INSTANTIATES NSObject and answers
	 * something that is not an NSNull at all. Measured the hard way: the wrong spelling made +null
	 * hand back a plain NSObject, which then aborted on -copy. */
	if (fn_shared_null == nil) {
		fn_shared_null = [super alloc];
	}
	return fn_shared_null;
}

/* THE SINGLETON DOOR IS +alloc (2026-09-18). It used to be +allocWithZone:, which took an
 * `NSZone`; that method is removed with the rest of the zone API, so the door moved to the method
 * that remains. `isEqual:`'s signature still matches NSObject's own, because a re-declaration may
 * not disagree with what it overrides. */
+ (instancetype)alloc
{
	return [self null];
}

- (BOOL)isEqual:(id)other
{
	return other == self;
}

/* A CONSTANT, because there is only ever one: any two "null objects" ARE the same object, so any
 * constant will do and this one spells the class. */
- (NSUInteger)hash
{
	return (NSUInteger)0x6e756c6c;
}

- (NSString *)description
{
	return @"<null>";
}

+ (NSString *)description
{
	return @"<null>";
}

- (id)copy
{
	return self;
}

@end
