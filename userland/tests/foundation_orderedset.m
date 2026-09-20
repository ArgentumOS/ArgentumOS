/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_orderedset, unit of 1 — F13.8e's acceptance for NSOrderedSet / NSMutableOrderedSet.
 * docs/design/foundation-plan.md §10.
 *
 * ONE unit, like the set probe: the claim is what an ORDERED collection does, not a
 * cross-translation-unit boundary. It imports only <Foundation/Foundation.h>, which also proves the
 * umbrella exports both headers.
 *
 * THE MEASUREMENT THAT EARNS ITS PLACE IS `ordered-equality-is-order-sensitive`: two ordered sets
 * with the SAME MEMBERS in a DIFFERENT ORDER must not be equal — while their `-set` views are equal
 * to each other, because the membership really is the same. One check therefore pins the difference
 * between this class and NSSet from both sides, and no constant in this file could produce it.
 */

#import <Foundation/Foundation.h>

#include <stdio.h>

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-ORDEREDSET %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-ORDEREDSET %s FAIL %s\n", name,
		       detail != nil ? [detail UTF8String] : "");
	}
}

/* The order as a string, for a detail that CARRIES its measurement. */
static NSString *fn_order(id collection)
{
	NSMutableString *out = [NSMutableString stringWithString:@""];
	NSUInteger i;

	for (i = 0; i < [collection count]; i++) {
		if (i > 0) {
			[out appendString:@","];
		}
		[out appendString:[[collection objectAtIndex:i] description]];
	}
	return out;
}

int main(void)
{
	{
		/* THE DUPLICATE IS IN THE MIDDLE, so a build that kept the LAST occurrence would put "a"
		 * at position 2 instead of 1 — which the positional assertions below would catch. */
		NSOrderedSet *ordered = [NSOrderedSet orderedSetWithArray:@[@"c", @"a", @"b", @"a"]];

		check("ordered-keeps-the-order-it-was-given",
		      ordered != nil && [ordered count] == 3 &&
		      [[ordered objectAtIndex:0] isEqualToString:@"c"] &&
		      [[ordered objectAtIndex:1] isEqualToString:@"a"] &&
		      [[ordered objectAtIndex:2] isEqualToString:@"b"] &&
		      [[ordered array] count] == 3,
		      [NSString stringWithFormat:@"order=%@ count=%lu",
			fn_order(ordered), (unsigned long)(ordered != nil ? [ordered count] : 0)]);
	}

	{
		NSOrderedSet *ordered = [NSOrderedSet orderedSetWithArray:@[@"c", @"a", @"b"]];
		NSString *fresh = [NSString stringWithFormat:@"%@", @"a"];

		check("ordered-index-and-lookup",
		      ordered != nil &&
		      [[ordered firstObject] isEqualToString:@"c"] &&
		      [[ordered lastObject] isEqualToString:@"b"] &&
		      [[ordered objectAtIndexedSubscript:1] isEqualToString:@"a"] &&
		      [ordered indexOfObject:fresh] == 1 &&
		      [ordered indexOfObject:@"nope"] == NSNotFound &&
		      [ordered containsObject:fresh] && ![ordered containsObject:@"nope"],
		      [NSString stringWithFormat:@"index(fresh a)=%lu index(nope)=%lu",
			(unsigned long)[ordered indexOfObject:fresh],
			(unsigned long)[ordered indexOfObject:@"nope"]]);
	}

	{
		NSOrderedSet *ordered = [NSOrderedSet orderedSetWithArray:@[@"c", @"a", @"b"]];
		NSSet *asSet = [ordered set];

		check("ordered-lends-its-membership-to-a-set",
		      asSet != nil && [asSet isKindOfClass:[NSSet class]] && [asSet count] == 3 &&
		      [asSet containsObject:@"a"] && [asSet containsObject:@"c"] &&
		      ![asSet containsObject:@"nope"],
		      [NSString stringWithFormat:@"set=%@ count=%lu", asSet,
			(unsigned long)(asSet != nil ? [asSet count] : 0)]);
	}

	{
		NSOrderedSet *first = [NSOrderedSet orderedSetWithArray:@[@"c", @"a", @"b"]];
		NSOrderedSet *same = [NSOrderedSet orderedSetWithArray:@[@"c", @"a", @"b"]];
		NSOrderedSet *reordered = [NSOrderedSet orderedSetWithArray:@[@"a", @"b", @"c"]];

		check("ordered-equality-is-order-sensitive",
		      [first isEqualToOrderedSet:same] && [first isEqual:same] &&
		      [first hash] == [same hash] &&
		      ![first isEqualToOrderedSet:reordered] && ![first isEqual:reordered] &&
		      /* ...WHILE THE MEMBERSHIP IS THE SAME: the two `-set` views ARE equal, which is what
		       * makes the inequality above a statement about ORDER rather than about contents. */
		      [[first set] isEqualToSet:[reordered set]],
		      [NSString stringWithFormat:@"a=%@ b=%@ setsEqual=%d",
			fn_order(first), fn_order(reordered),
			(int)[[first set] isEqualToSet:[reordered set]]]);
	}

	{
		NSOrderedSet *small = [NSOrderedSet orderedSetWithArray:@[@"a", @"b"]];
		NSOrderedSet *big = [NSOrderedSet orderedSetWithArray:@[@"b", @"a", @"c"]];
		NSOrderedSet *other = [NSOrderedSet orderedSetWithArray:@[@"c", @"d"]];
		NSOrderedSet *disjoint = [NSOrderedSet orderedSetWithArray:@[@"x"]];

		check("ordered-relations",
		      [small isSubsetOfOrderedSet:big] &&
		      ![big isSubsetOfOrderedSet:small] &&
		      [big intersectsOrderedSet:other] &&
		      ![big intersectsOrderedSet:disjoint],
		      [NSString stringWithFormat:@"subset=%d intersects=%d",
			(int)[small isSubsetOfOrderedSet:big], (int)[big intersectsOrderedSet:other]]);
	}

	{
		NSMutableOrderedSet *mutable = [NSMutableOrderedSet orderedSetWithCapacity:4];

		[mutable addObject:@"a"];
		[mutable addObject:@"b"];
		[mutable addObject:@"a"];		/* already a member: no second copy... */
		[mutable insertObject:@"front" atIndex:0];
		[mutable addObject:@"tail"];
		[mutable replaceObjectAtIndex:2 withObject:@"B"];
		[mutable exchangeObjectAtIndex:0 withObjectAtIndex:1];
		[mutable removeObject:@"tail"];

		check("ordered-mutation-keeps-the-order",
		      mutable != nil && [mutable count] == 3 &&
		      [[mutable objectAtIndex:0] isEqualToString:@"a"] &&
		      [[mutable objectAtIndex:1] isEqualToString:@"front"] &&
		      [[mutable objectAtIndex:2] isEqualToString:@"B"] &&
		      ![mutable containsObject:@"b"] &&
		      [mutable containsObject:@"B"],
		      [NSString stringWithFormat:@"order=%@ count=%lu",
			fn_order(mutable), (unsigned long)(mutable != nil ? [mutable count] : 0)]);
	}

	{
		NSOrderedSet *ordered = [NSOrderedSet orderedSetWithArray:@[@"c", @"a", @"b"]];
		NSMutableString *walked = [NSMutableString stringWithString:@""];
		NSMutableString *reversed = [NSMutableString stringWithString:@""];
		NSUInteger count = 0;
		NSEnumerator *back;
		id object;

		for (NSString *member in ordered) {
			[walked appendString:member];
			count++;
		}
		back = [ordered reverseObjectEnumerator];
		while ((object = [back nextObject]) != nil) {
			[reversed appendString:[object description]];
		}
		check("ordered-enumeration-both-ways",
		      count == 3 && [walked isEqualToString:@"cab"] &&
		      [reversed isEqualToString:@"bac"],
		      [NSString stringWithFormat:@"forward=%@ reverse=%@ count=%lu",
			walked, reversed, (unsigned long)count]);
	}

	{
		NSOrderedSet *ordered = [NSOrderedSet orderedSetWithArray:@[@"pear", @"apple", @"plum"]];
		NSOrderedSet *kept = [ordered filteredOrderedSetUsingPredicate:
				      [NSPredicate predicateWithFormat:@"SELF BEGINSWITH \"p\""]];
		NSArray *sorted = [ordered sortedArrayUsingDescriptors:
				   @[[NSSortDescriptor sortDescriptorWithKey:@"self" ascending:YES]]];

		check("ordered-predicate-and-sort",
		      kept != nil && [kept count] == 2 &&
		      [[kept objectAtIndex:0] isEqualToString:@"pear"] &&
		      [[kept objectAtIndex:1] isEqualToString:@"plum"] &&
		      sorted != nil && [sorted count] == 3 &&
		      [[sorted objectAtIndex:0] isEqualToString:@"apple"] &&
		      [[sorted objectAtIndex:2] isEqualToString:@"plum"],
		      [NSString stringWithFormat:@"kept=%@ sorted=%@",
			fn_order(kept), fn_order(sorted)]);
	}

	{
		NSMutableOrderedSet *mutable = [NSMutableOrderedSet orderedSetWithArray:@[@"a", @"b"]];
		NSOrderedSet *snapshot;

		[mutable addObject:@"c"];
		snapshot = [mutable copy];
		[mutable addObject:@"d"];
		check("ordered-copy-semantics",
		      snapshot != nil && [snapshot count] == 3 && [mutable count] == 4 &&
		      ![snapshot isKindOfClass:[NSMutableOrderedSet class]] &&
		      [mutable isKindOfClass:[NSOrderedSet class]],
		      [NSString stringWithFormat:@"snapshot=%lu mutable=%lu",
			(unsigned long)(snapshot != nil ? [snapshot count] : 0),
			(unsigned long)[mutable count]]);
	}

	printf("FOUNDATION-ORDEREDSET RESULT ok=%d fail=%d\n", okc, failc);
	/* The exit status, in the probe's OWN output. After a probe the console can stop
	 * serving INPUT for a while (the tier residual), so an `echo $?` that the harness
	 * types may never run. The probe knows exactly what it returns, so it says so - and
	 * this is the same value: failc ? 1 : 0 is the return statement below. */
	printf("FOUNDATION-ORDEREDSET-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-ORDEREDSET DONE\n");
	return failc ? 1 : 0;
}
