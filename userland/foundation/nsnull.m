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
	/* THIS ANSWERS +1, AND THAT IS THE WHOLE CONTRACT OF AN `alloc`-FAMILY METHOD (fixed
	 * 2026-09-20, and the bug it fixes had been invisible since the class landed). Returning the
	 * shared instance BARE over-released the singleton under ANY ARC caller: `alloc`/`new`/`copy`
	 * are the "owned" families, so ARC owns this result and releases it at the end of the scope —
	 * and that release had no matching retain here. Measured, in three lines:
	 *
	 *     one = [NSNull null]; two = [NSNull null]; made = [[NSNull alloc] init];
	 *
	 * leaves the count at 3 (a base 1 from +null's own alloc, plus ONE retain per +null — the
	 * `made` line adds none), while ARC emits THREE releases at the end of that scope. So the
	 * count reached 0 and the singleton was FREED; the next `[NSNull null]` then returned a
	 * dangling pointer and `objc_retainAutoreleasedReturnValue` died reading its `isa`. What it
	 * read there was glibc's tcache safe-linking word in the freed chunk's own first slot, where
	 * musl leaves the chunk intact — which is exactly why the guest never showed this and the
	 * host run did.
	 *
	 * THE RETAIN BELOW IS WHAT BALANCES IT, and it is permanent by design: the object is a
	 * singleton, so its count is a formality and the instance is never meant to die. */
	return [[self null] retain];
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

/* +1, THE SAME REASON `+alloc` ABOVE ANSWERS +1 (fixed 2026-09-20): `-copy` belongs to the copy
 * family, so its result is OWNED by the caller and ARC releases it. `return self` therefore released
 * the singleton once more than it had been retained — the identical over-release, reached through
 * the other door a program uses. The class stays immutable (the copy IS the receiver); only the
 * ownership answer changes. */
- (id)copy
{
	return [self retain];
}

@end
