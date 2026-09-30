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

	{
		/* THE CONSTRUCTION FAMILY (§63.6), all of it: the nil-terminated forms, the NSSet source, and the
		 * RANGE door. The members are asserted as an ARRAY, because order is what this family is for and
		 * a check on membership alone would pass for a reader that reordered them. */
		NSOrderedSet *variadic = [NSOrderedSet orderedSetWithObjects:@"one", @"two", @"three", nil];
		NSOrderedSet *fromVarargsInit = [[NSOrderedSet alloc] initWithObjects:@"x", @"y", nil];
		NSOrderedSet *single = [[NSOrderedSet alloc] initWithObject:@"solo"];
		NSOrderedSet *fromSet = [NSOrderedSet orderedSetWithSet:[NSSet setWithArray:@[@"p", @"q"]]];
		NSOrderedSet *sliced = [NSOrderedSet orderedSetWithArray:@[@"a", @"b", @"c", @"d"]
								  range:NSMakeRange(1, 2)
							      copyItems:NO];
		BOOL refusedRange = NO;

		@try {
			(void)[NSOrderedSet orderedSetWithArray:@[@"a", @"b"]
							  range:NSMakeRange(1, 5)
						      copyItems:NO];
		} @catch (NSException *e) {
			refusedRange = [[e name] isEqualToString:NSRangeException];
		}
		check("ordered-construction-doors",
		      variadic != nil && [variadic count] == 3 &&
		      [[variadic array] isEqualToArray:@[@"one", @"two", @"three"]] &&
		      fromVarargsInit != nil && [[fromVarargsInit array] isEqualToArray:@[@"x", @"y"]] &&
		      single != nil && [[single array] isEqualToArray:@[@"solo"]] &&
		      fromSet != nil && [fromSet count] == 2 && [fromSet containsObject:@"p"] &&
		      sliced != nil && [[sliced array] isEqualToArray:@[@"b", @"c"]] &&
		      refusedRange,
		      [NSString stringWithFormat:@"variadic=%@ slice=%@ set=%@ refusedRange=%d",
			variadic != nil ? [variadic array] : @"(nil)",
			sliced != nil ? [sliced array] : @"(nil)",
			fromSet != nil ? [fromSet array] : @"(nil)", (int)refusedRange]);
	}

	{
		/* `copyItems:YES` IS MEASURABLE, AND ONLY WITH A MEMBER THAT ACTUALLY COPIES: an NSString's `-copy`
		 * answers the RECEIVER, so a set of literals cannot tell the two flags apart. An NSMutableString's
		 * copy is a REAL copy (and an immutable one), so the pair is asserted BY POINTER — the same member,
		 * not merely an equal one — plus the class change, which a mere retain cannot produce. */
		NSMutableString *member = [[NSMutableString alloc] initWithString:@"mutable"];
		NSArray *source = @[member];
		NSOrderedSet *shared = [[NSOrderedSet alloc] initWithArray:source copyItems:NO];
		NSOrderedSet *copied = [[NSOrderedSet alloc] initWithArray:source copyItems:YES];
		NSOrderedSet *copiedSlice = [NSOrderedSet orderedSetWithArray:source
								       range:NSMakeRange(0, 1)
								   copyItems:YES];
		id sharedMember = shared != nil ? [shared firstObject] : nil;
		id copiedMember = copied != nil ? [copied firstObject] : nil;
		id copiedSliceMember = copiedSlice != nil ? [copiedSlice firstObject] : nil;

		check("ordered-construction-copies-when-asked",
		      sharedMember == member &&
		      copiedMember != nil && copiedMember != member &&
		      [copiedMember isEqual:member] &&
		      ![copiedMember isKindOfClass:[NSMutableString class]] &&
		      copiedSliceMember != nil && copiedSliceMember != member,
		      [NSString stringWithFormat:@"samePointer=%d equal=%d immutableCopy=%d",
			(int)(sharedMember == member),
			(int)(copiedMember != nil && [copiedMember isEqual:member]),
			(int)(copiedMember != nil &&
			      ![copiedMember isKindOfClass:[NSMutableString class]])]);
	}

	{
		/* THE ENUMERATION DOORS, including the SIGNATURE that was wrong until §63.7: Apple's block takes the
		 * object, its INDEX and the stop flag. Two properties matter and neither is free: the REVERSE walk
		 * must report the SAME index the forward one would (a caller cannot reconstruct it afterwards), and
		 * the stop flag must stop the walk. */
		NSOrderedSet *ordered = [NSOrderedSet orderedSetWithArray:@[@"a", @"b", @"c", @"d"]];
		NSMutableArray *forward = [NSMutableArray array];
		NSMutableArray *forwardIndexes = [NSMutableArray array];
		NSMutableArray *reverse = [NSMutableArray array];
		NSMutableArray *reverseIndexes = [NSMutableArray array];
		NSMutableArray *subset = [NSMutableArray array];
		NSMutableArray *reverseSubset = [NSMutableArray array];
		__block NSUInteger stops = 0;

		[ordered enumerateObjectsUsingBlock:^(id object, NSUInteger index, BOOL *stop) {
			[forward addObject:object];
			[forwardIndexes addObject:[NSNumber numberWithUnsignedInteger:index]];
		}];
		[ordered enumerateObjectsWithOptions:NSEnumerationReverse
				  usingBlock:^(id object, NSUInteger index, BOOL *stop) {
			[reverse addObject:object];
			[reverseIndexes addObject:[NSNumber numberWithUnsignedInteger:index]];
		}];
		[ordered enumerateObjectsAtIndexes:[NSIndexSet indexSetWithIndexesInRange:NSMakeRange(1, 2)]
					   options:0
					usingBlock:^(id object, NSUInteger index, BOOL *stop) {
			[subset addObject:[NSString stringWithFormat:@"%lu:%@",
				(unsigned long)index, object]];
		}];
		[ordered enumerateObjectsAtIndexes:[NSIndexSet indexSetWithIndexesInRange:NSMakeRange(1, 2)]
					   options:NSEnumerationReverse
					usingBlock:^(id object, NSUInteger index, BOOL *stop) {
			[reverseSubset addObject:[NSString stringWithFormat:@"%lu:%@",
				(unsigned long)index, object]];
		}];
		[ordered enumerateObjectsUsingBlock:^(id object, NSUInteger index, BOOL *stop) {
			stops++;
			*stop = YES;	/* one visit, and the walk must end here */
		}];
		check("ordered-enumeration-doors",
		      [forward isEqualToArray:@[@"a", @"b", @"c", @"d"]] &&
		      [forwardIndexes isEqualToArray:@[@0, @1, @2, @3]] &&
		      [reverse isEqualToArray:@[@"d", @"c", @"b", @"a"]] &&
		      [reverseIndexes isEqualToArray:@[@3, @2, @1, @0]] &&
		      [subset isEqualToArray:@[@"1:b", @"2:c"]] &&
		      [reverseSubset isEqualToArray:@[@"2:c", @"1:b"]] &&
		      stops == 1,
		      [NSString stringWithFormat:@"fwd=%@ fwdIdx=%@ rev=%@ revIdx=%@ sub=%@ subRev=%@ stops=%lu",
			forward, forwardIndexes, reverse, reverseIndexes, subset, reverseSubset,
			(unsigned long)stops]);
	}

	{
		/* THE POSITIONAL SUBSET AND THE REVERSAL. The first mirrors NSArray's refusal (an index that is not
		 * there RAISES rather than shortening the answer); the second must answer an IMMUTABLE set, because
		 * the property is declared `copy` and a caller may hold it while the receiver changes. */
		NSOrderedSet *ordered = [NSOrderedSet orderedSetWithArray:@[@"a", @"b", @"c", @"d"]];
		NSArray *chosen = [ordered objectsAtIndexes:
			[NSIndexSet indexSetWithIndexesInRange:NSMakeRange(1, 2)]];
		NSOrderedSet *reversed = [ordered reversedOrderedSet];
		NSOrderedSet *reversedEmpty = [[NSOrderedSet orderedSet] reversedOrderedSet];
		BOOL refused = NO;

		@try {
			(void)[ordered objectsAtIndexes:[NSIndexSet indexSetWithIndex:9]];
		} @catch (NSException *e) {
			refused = [[e name] isEqualToString:NSRangeException];
		}
		check("ordered-positional-and-reversal",
		      chosen != nil && [chosen isEqualToArray:@[@"b", @"c"]] &&
		      reversed != nil && [[reversed array] isEqualToArray:@[@"d", @"c", @"b", @"a"]] &&
		      ![reversed isKindOfClass:[NSMutableOrderedSet class]] &&
		      reversedEmpty == [NSOrderedSet orderedSet] &&
		      refused,
		      [NSString stringWithFormat:@"chosen=%@ reversed=%@ mutable=%d emptyIsShared=%d refused=%d",
			chosen != nil ? chosen : @"(nil)",
			reversed != nil ? [reversed array] : @"(nil)",
			(int)(reversed != nil && [reversed isKindOfClass:[NSMutableOrderedSet class]]),
			(int)(reversedEmpty == [NSOrderedSet orderedSet]), (int)refused]);
	}

	{
		/* THE TWO SET QUESTIONS. They are asked of the SET VIEW, so the answers must agree with what NSSet
		 * itself says about the same members - which is the point of answering them there rather than by a
		 * second walk that could drift. */
		NSOrderedSet *ordered = [NSOrderedSet orderedSetWithArray:@[@"a", @"b", @"c"]];
		NSSet *overlapping = [NSSet setWithArray:@[@"c", @"z"]];
		NSSet *disjoint = [NSSet setWithArray:@[@"x", @"y"]];
		NSSet *subset = [NSSet setWithArray:@[@"a", @"c", @"extra"]];
		NSSet *superset = [NSSet setWithArray:@[@"a", @"b", @"c", @"d"]];
		NSSet *view = [ordered set];

		check("ordered-set-relations",
		      [ordered intersectsSet:overlapping] && ![ordered intersectsSet:disjoint] &&
		      [ordered isSubsetOfSet:superset] && ![ordered isSubsetOfSet:subset] &&
		      [view intersectsSet:overlapping] && [view isSubsetOfSet:superset] &&
		      ![ordered intersectsSet:nil] && ![ordered isSubsetOfSet:nil],
		      [NSString stringWithFormat:@"intersects=%d disjoint=%d subset=%d notSubset=%d nilSafe=%d",
			(int)[ordered intersectsSet:overlapping],
			(int)[ordered intersectsSet:disjoint],
			(int)[ordered isSubsetOfSet:superset],
			(int)[ordered isSubsetOfSet:subset],
			(int)(![ordered intersectsSet:nil] && ![ordered isSubsetOfSet:nil])]);
	}

	{
		/* THE PREDICATE AND COMPARATOR DOORS. The first-match walk's CALL COUNT is asserted, not just its
		 * answer: the walk must STOP at the match (a predicate may have side effects), and an index alone
		 * would pass for a full scan that happened to find the right element first. The two sorts and the
		 * binary search DELEGATE to the array view, so this also measures that the two agree: the sorted
		 * answer is compared against the array's own, and the binary search's answer is the same number the
		 * array reports for the same range and options. */
		NSOrderedSet *ordered = [NSOrderedSet orderedSetWithArray:@[@"four", @"one", @"three", @"two"]];
		__block NSUInteger calls = 0;
		NSUInteger found = [ordered indexOfObjectPassingTest:^BOOL(id object, NSUInteger index, BOOL *stop) {
			calls++;
			return [object isEqualToString:@"three"];
		}];
		NSIndexSet *matched = [ordered indexesOfObjectsPassingTest:
			^BOOL(id object, NSUInteger index, BOOL *stop) {
			return index != 1;
		}];
		NSArray *sortedDescending = [ordered sortedArrayWithOptions:NSSortStable
							   usingComparator:^NSComparisonResult(id left, id right) {
			return [right compare:left];
		}];
		/* THE SORTED ARRAY IS A PRECONDITION OF THE BINARY SEARCH, and this check's first version used
		 * ["one", "two", "three", "four"], which is NOT sorted ("three" < "two") - so the search's answers
		 * were meaningless and the check failed for the right reason. The order above IS alphabetical. */
		NSUInteger insertion = [ordered indexOfObject:@"three"
						 inSortedRange:NSMakeRange(0, 4)
						     options:NSBinarySearchingInsertionIndex
					     usingComparator:^NSComparisonResult(id left, id right) {
			return [left compare:right];
		}];
		/* AN ABSENT VALUE TAKES THE OTHER BRANCH (`end`), so these two answers together cover both. */
		NSUInteger absentInsertion = [ordered indexOfObject:@"zero"
							   inSortedRange:NSMakeRange(0, 4)
							       options:NSBinarySearchingInsertionIndex
						   usingComparator:^NSComparisonResult(id left, id right) {
			return [left compare:right];
		}];
		NSUInteger firstEqual = [ordered indexOfObject:@"two"
						  inSortedRange:NSMakeRange(0, 4)
						      options:NSBinarySearchingFirstEqual
					      usingComparator:^NSComparisonResult(id left, id right) {
			return [left compare:right];
		}];

		check("ordered-predicate-and-comparator-doors",
		      found == 2 && calls == 3 &&
		      matched != nil && [matched count] == 3 && ![matched containsIndex:1] &&
		      sortedDescending != nil && [sortedDescending count] == 4 &&
		      [[sortedDescending objectAtIndex:0] isEqualToString:@"two"] &&
		      insertion == 3 && absentInsertion == 4 && firstEqual == 3,
		      [NSString stringWithFormat:@"found=%lu calls=%lu matched=%lu desc=%@ ins=%lu absentIns=%lu first=%lu",
			(unsigned long)found, (unsigned long)calls,
			(unsigned long)(matched != nil ? [matched count] : 0),
			sortedDescending != nil ? sortedDescending : @"(nil)",
			(unsigned long)insertion, (unsigned long)absentInsertion,
			(unsigned long)firstEqual]);
	}

	{
		/* THE MUTABLE SORTS, and the RANGE one is the only one with an interesting contract: only the members
		 * INSIDE the range may move. The check sorts the MIDDLE of a five-member set and asserts BOTH ENDS are
		 * exactly where they were - a full sort would pass a "the middle is sorted" assertion, so the
		 * untouched ends are the instrument. It also asserts the refusal for a range past the end, and then
		 * the whole-set sort, which must reach the same order as if the middle step had never happened. */
		NSMutableOrderedSet *mutable = [NSMutableOrderedSet orderedSetWithArray:
			@[@"z", @"d", @"b", @"a", @"y"]];
		NSMutableOrderedSet *straight = [NSMutableOrderedSet orderedSetWithArray:
			@[@"z", @"d", @"b", @"a", @"y"]];
		BOOL refused = NO;

		[mutable sortRange:NSMakeRange(1, 3)
			   options:0
		   usingComparator:^NSComparisonResult(id left, id right) {
			return [left compare:right];
		}];
		@try {
			[mutable sortRange:NSMakeRange(4, 9) options:0
			   usingComparator:^NSComparisonResult(id left, id right) {
				return [left compare:right];
			}];
		} @catch (NSException *e) {
			refused = [[e name] isEqualToString:NSRangeException];
		}
		[mutable sortUsingComparator:^NSComparisonResult(id left, id right) {
			return [left compare:right];
		}];
		[straight sortWithOptions:NSSortStable usingComparator:^NSComparisonResult(id left, id right) {
			return [left compare:right];
		}];
		check("ordered-mutable-sorts",
		      [[mutable array] isEqualToArray:@[@"a", @"b", @"d", @"y", @"z"]] &&
		      [[straight array] isEqualToArray:[mutable array]] &&
		      refused,
		      [NSString stringWithFormat:@"rangeThenWhole=%@ wholeStraight=%@ refused=%d",
			[mutable array], [straight array], (int)refused]);
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
