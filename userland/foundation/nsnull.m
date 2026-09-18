/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * nsnull.m — the object that stands for "nothing" (F13.8c). ARC file.
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
	 * hand back a plain NSObject, which then aborted on -copyWithZone:. */
	if (fn_shared_null == nil) {
		fn_shared_null = [super allocWithZone:NULL];
	}
	return fn_shared_null;
}

/* THE SIGNATURES MATCH NSObject'S OWN: `allocWithZone:` takes a NULLABLE zone (NULL is the norm)
 * and `isEqual:` a nonnull object there, and a re-declaration may not disagree with what it
 * overrides. `-copyWithZone:` below also keeps its nullable zone, because NSCopying declares that
 * one nullable. */
+ (instancetype)allocWithZone:(nullable NSZone *)zone
{
	(void)zone;
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

- (id)copyWithZone:(nullable NSZone *)zone
{
	(void)zone;
	return self;
}

@end
