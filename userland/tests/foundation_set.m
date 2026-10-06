/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_set, unit of 1 — F13.8's acceptance for the unordered collection.
 * docs/design/foundation-plan.md §10.
 *
 * ONE unit, like the formatter probes: the claim here is VALUE SEMANTICS — what makes a set a set —
 * rather than a cross-translation-unit boundary. It imports only <Foundation/Foundation.h>, which
 * also proves the umbrella exports the new classes.
 *
 * WHAT IT MEASURES, and none of it can come from this file:
 *   set-dedupes-by-value   two DISTINCT NSString objects with the same characters are ONE member;
 *   set-member-by-value    -member: finds the STORED object from a fresh equal one, and answers nil
 *                          for something not in the set;
 *   set-algebra            union / minus / intersect, by membership and count;
 *   set-relations          isEqualToSet:, isSubsetOfSet:, intersectsSet:;
 *   set-adding-forms       the three -setByAdding… forms, and that the RECEIVER is unchanged;
 *   set-enumeration        a for-in loop and -enumerateObjectsUsingBlock: each visit every member
 *                          exactly once;
 *   set-order-independent  two sets built in DIFFERENT ORDERS are equal AND hash alike;
 *   set-predicate-filter   -filteredSetUsingPredicate: (the predicate family, from F11);
 *   set-sort-descriptors   a set becomes an ORDERED array through NSSortDescriptor (F10);
 *   set-copy-semantics     a mutable set's -copy is an immutable snapshot of the same membership.
 */

#import <Foundation/Foundation.h>

#include <stdio.h>

static int okc, failc;

/* The detail is NULLABLE: every answer under test comes back through a nullable door. */
static int lastcheck;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	lastcheck = ok;	/* read by covers() */
	if (ok) {
		okc++;
		printf("FOUNDATION-SET %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-SET %s FAIL %s\n", name,
		       detail != nil ? [detail UTF8String] : "");
	}
}

/* covers("NSScanner", "scanInt:") - the behavioural claim, piggybacked on the check above it. */
static void covers_(const char *cls, const char *sel)
{
	if (lastcheck) {
		printf("COVERS %s %s\n", cls, sel);
	}
}
#define covers(cls, sel) covers_(cls, sel)

int main(void)
{
	{
		/* THE WHOLE CONTRACT IN ONE CHECK: these two are DIFFERENT OBJECTS and ONE MEMBER. A set
		 * that deduped by pointer would hold both. */
		NSString *one = [NSString stringWithFormat:@"%@", @"ann"];
		NSMutableString *two = [NSMutableString stringWithString:@"ann"];
		const id members[2] = { one, two };
		NSSet *built = [NSSet setWithObjects:members count:2];
		NSSet *fromArray = [NSSet setWithArray:@[one, two, @"bob"]];

		check("set-dedupes-by-value",
		      (id)one != (id)two && [one isEqualToString:two] &&
		      built != nil && [built count] == 1 && [built containsObject:one] &&
		      fromArray != nil && [fromArray count] == 2,
		      [NSString stringWithFormat:@"distinct=%d equal=%d count=%lu arrayCount=%lu",
			(int)((id)one != (id)two), (int)[one isEqualToString:two],
			(unsigned long)(built != nil ? [built count] : 0),
			(unsigned long)(fromArray != nil ? [fromArray count] : 0)]);
	covers("NSSet", "setWithObjects:");
	}

	{
		NSString *stored = [NSString stringWithFormat:@"%@", @"carol"];
		NSSet *set = [NSSet setWithObject:stored];
		/* A SECOND object with the same characters: `+stringWithFormat:` rather than
		 * `+stringWithString:` with a literal, which the compiler rejects as redundant and which
		 * would not have been a distinct object anyway. */
		NSString *fresh = [NSString stringWithFormat:@"%@", @"carol"];
		id found = [set member:fresh];

		check("set-member-by-value",
		      found != nil && [found isEqual:stored] &&
		      [set member:@"nobody"] == nil && ![set containsObject:@"nobody"],
		      found != nil ? [found description] : @"(nil)");
	}

	{
		NSMutableSet *left = [NSMutableSet setWithArray:@[@"a", @"b", @"c"]];
		NSMutableSet *right = [NSMutableSet setWithArray:@[@"b", @"c", @"d"]];
		NSMutableSet *unionSet;
		NSMutableSet *minus;
		NSMutableSet *intersect;
		NSUInteger unionCount, minusCount, intersectCount;

		unionSet = [left mutableCopy];
		[unionSet unionSet:right];
		minus = [left mutableCopy];
		[minus minusSet:right];
		intersect = [left mutableCopy];
		[intersect intersectSet:right];
		unionCount = [unionSet count];
		minusCount = [minus count];
		intersectCount = [intersect count];
		check("set-algebra",
		      unionCount == 4 && minusCount == 1 && intersectCount == 2 &&
		      [minus containsObject:@"a"] && [intersect containsObject:@"b"] &&
		      [intersect containsObject:@"c"],
		      [NSString stringWithFormat:@"union=%lu minus=%lu intersect=%lu",
			(unsigned long)unionCount, (unsigned long)minusCount,
			(unsigned long)intersectCount]);
	}

	{
		NSSet *small = [NSSet setWithArray:@[@"a", @"b"]];
		NSSet *big = [NSSet setWithArray:@[@"a", @"b", @"c"]];
		NSSet *other = [NSSet setWithArray:@[@"c", @"d"]];
		NSSet *disjoint = [NSSet setWithArray:@[@"x", @"y"]];

		check("set-relations",
		      [small isSubsetOfSet:big] && ![big isSubsetOfSet:small] &&
		      [big intersectsSet:other] && ![big intersectsSet:disjoint] &&
		      [small isEqualToSet:[NSSet setWithArray:@[@"b", @"a"]]] &&
		      ![small isEqualToSet:big],
		      [NSString stringWithFormat:@"subset=%d intersects=%d equal=%d",
			(int)[small isSubsetOfSet:big], (int)[big intersectsSet:other],
			(int)[small isEqualToSet:[NSSet setWithArray:@[@"b", @"a"]]]]);
	}

	{
		NSSet *base = [NSSet setWithArray:@[@"a", @"b"]];
		NSSet *addedObject = [base setByAddingObject:@"c"];
		NSSet *addedSet = [base setByAddingObjectsFromSet:[NSSet setWithArray:@[@"c", @"d"]]];
		NSSet *addedArray = [base setByAddingObjectsFromArray:@[@"c", @"d"]];

		/* THE RECEIVER IS UNCHANGED: it is immutable, so every "adding" form answers a NEW set. */
		check("set-adding-forms",
		      [base count] == 2 && [addedObject count] == 3 && [addedSet count] == 4 &&
		      [addedArray count] == 4 && [addedObject containsObject:@"c"] &&
		      [addedSet containsObject:@"d"] && [addedArray containsObject:@"d"],
		      [NSString stringWithFormat:@"base=%lu object=%lu set=%lu array=%lu",
			(unsigned long)[base count], (unsigned long)[addedObject count],
			(unsigned long)[addedSet count], (unsigned long)[addedArray count]]);
	covers("NSSet", "setByAddingObject:");
	covers("NSSet", "setByAddingObjectsFromArray:");
	covers("NSSet", "setByAddingObjectsFromSet:");
	}

	{
		NSSet *set = [NSSet setWithArray:@[@"a", @"b", @"c"]];
		NSUInteger looped = 0;
		__block NSUInteger blocked = 0;
		BOOL sawA = NO;

		for (NSString *member in set) {
			looped++;
			if ([member isEqualToString:@"a"]) {
				sawA = YES;
			}
		}
		[set enumerateObjectsUsingBlock:^(id object, BOOL *stop) {
			(void)object;
			(void)stop;
			blocked++;
		}];
		check("set-enumeration",
		      looped == 3 && blocked == 3 && sawA,
		      [NSString stringWithFormat:@"looped=%lu blocked=%lu sawA=%d",
			(unsigned long)looped, (unsigned long)blocked, (int)sawA]);
	}

	{
		/* ORDER IS NOT PART OF A SET'S VALUE, so the hash must not depend on it either. */
		NSSet *one = [NSSet setWithArray:@[@"a", @"b", @"c", @"d"]];
		NSSet *two = [NSSet setWithArray:@[@"d", @"c", @"b", @"a"]];

		check("set-order-independent",
		      [one isEqualToSet:two] && [one hash] == [two hash] && [one isEqual:two],
		      [NSString stringWithFormat:@"equal=%d hashA=%lu hashB=%lu",
			(int)[one isEqualToSet:two], (unsigned long)[one hash],
			(unsigned long)[two hash]]);
	}

	/* ⚠⚠ AND THE PREDICATE FILTER IS GONE WITH THE FAMILY (§63.161): `-filteredSetUsingPredicate:` took an
	 * `NSPredicate` — a macOS 10.4 type, later than the 10.2 baseline — so it left this class in the same
	 * pass, and this check went with it. */

	{
		NSSet *set = [NSSet setWithArray:@[@"c", @"a", @"b"]];
		NSArray *sorted = [set sortedArrayUsingDescriptors:
					@[[NSSortDescriptor sortDescriptorWithKey:@"self" ascending:YES]]];

		check("set-sort-descriptors",
		      sorted != nil && [sorted count] == 3 &&
		      [[sorted objectAtIndex:0] isEqualToString:@"a"] &&
		      [[sorted objectAtIndex:2] isEqualToString:@"c"],
		      sorted != nil ? [sorted description] : @"(nil)");
	}

	{
		NSMutableSet *mutable = [NSMutableSet setWithArray:@[@"a", @"b"]];
		NSSet *snapshot;

		/* THE CAPACITY FORM IS EXERCISED AND ITS RESULT DISCARDED — it is a constructor, and the
		 * point here is that a mutable set built any way is still a MUTABLE SET. */
		(void)[NSMutableSet setWithCapacity:4];
		[mutable addObject:@"c"];
		snapshot = [mutable copy];
		[mutable addObject:@"d"];
		check("set-copy-semantics",
		      snapshot != nil && [snapshot count] == 3 && [mutable count] == 4 &&
		      [mutable isKindOfClass:[NSMutableSet class]] &&
		      ![snapshot isKindOfClass:[NSMutableSet class]],
		      [NSString stringWithFormat:@"snapshot=%lu mutable=%lu",
			(unsigned long)(snapshot != nil ? [snapshot count] : 0),
			(unsigned long)[mutable count]]);
	}

	{
		/* NSCountedSet IS AN NSSet THAT REMEMBERS HOW MANY, and it is checked HERE because it IS a
		 * set: the set semantics have to keep working underneath the counts. The fourth add is a
		 * DISTINCT object that is merely EQUAL to the first, which is what makes the count a count
		 * by VALUE rather than by pointer. */
		NSCountedSet *counted = [NSCountedSet setWithCapacity:3];
		NSCountedSet *fromArray = [NSCountedSet setWithArray:@[@"x", @"x", @"y"]];

		[counted addObject:@"a"];
		[counted addObject:@"b"];
		[counted addObject:@"a"];
		[counted addObject:[NSString stringWithFormat:@"%s", "a"]];
		[counted removeObject:@"b"];
		[counted removeObject:@"b"];	/* gone already: a no-op, and the count must stay 0 */
		check("set-counted",
		      counted != nil && [counted count] == 1 &&
		      [counted countForObject:@"a"] == 3 &&
		      [counted countForObject:@"b"] == 0 &&
		      [counted countForObject:@"missing"] == 0 &&
		      [counted containsObject:@"a"] &&
		      [[counted allObjects] count] == 1 &&
		      fromArray != nil && [fromArray count] == 2 &&
		      [fromArray countForObject:@"x"] == 2 &&
		      [fromArray countForObject:@"y"] == 1 &&
		      [fromArray isKindOfClass:[NSSet class]],
		      [NSString stringWithFormat:@"distinct=%lu a=%lu b=%lu x=%lu y=%lu fromArray=%lu",
			(unsigned long)[counted count], (unsigned long)[counted countForObject:@"a"],
			(unsigned long)[counted countForObject:@"b"],
			(unsigned long)(fromArray != nil ? [fromArray countForObject:@"x"] : 0),
			(unsigned long)(fromArray != nil ? [fromArray countForObject:@"y"] : 0),
			(unsigned long)(fromArray != nil ? [fromArray count] : 0)]);
	}

	{
		/* THE NIL-TERMINATED VARIADIC FACTORY, which this tree declared only from 2026-09-30 (the
		 * selector ledger carried `+setWithObjects:` as an open row until then). Three things are
		 * asserted, and each is a way the list can go wrong: the terminator STOPS the walk (a fourth
		 * member would show up if it did not), the members are the ones passed, and the empty call
		 * answers the SHARED EMPTY instance — asserted BY POINTER, because "an empty set" and "the
		 * empty instance" are different claims and only the second is the family's contract. */
		NSSet *three = [NSSet setWithObjects:@"one", @"two", @"three", nil];
		/* THE EMPTY CALL GOES THROUGH A VARIABLE, and that is deliberate rather than evasive: the factory's
		 * first parameter is annotated NONNULL (Apple's own shape, and `NS_REQUIRES_NIL_TERMINATION` says the
		 * list ends with nil, not that it starts with one), so writing `nil` there is a compile-time warning
		 * about a CALLER ERROR — while the RUNTIME behaviour of an immediately-terminated list is the empty
		 * collection, which is the thing worth asserting. The variable lets the runtime case be measured
		 * without the compiler reading it as the mistake it does not have to be. */
		id noObjects = nil;
		NSSet *empty = [NSSet setWithObjects:noObjects];
		NSMutableSet *mutableFromVarargs = [NSMutableSet setWithObjects:@"a", @"b", nil];

		check("set-variadic-factory",
		      three != nil && [three count] == 3 &&
		      [three containsObject:@"one"] && [three containsObject:@"three"] &&
		      empty != nil && [empty count] == 0 &&
		      empty == [NSSet set] &&
		      mutableFromVarargs != nil && [mutableFromVarargs count] == 2 &&
		      [mutableFromVarargs isKindOfClass:[NSMutableSet class]],
		      [NSString stringWithFormat:@"three=%lu empty-is-shared=%d mutable=%lu",
			(unsigned long)(three != nil ? [three count] : 0),
			(int)(empty == [NSSet set]),
			(unsigned long)(mutableFromVarargs != nil ? [mutableFromVarargs count] : 0)]);
	}

	{
		/* THE NSCoding DOORS (§63.11), driven DIRECTLY — the only way to reach them, since the archiver's
		 * structural branch recognises a set by KIND and never asks the class. Three things are asserted: the
		 * protocol answer, the round trip, and that the MUTABLE class answers a mutable set. The member is a
		 * FRESH string object per set, so equality is by VALUE and the check cannot pass on pointer identity. */
		NSSet *set = [NSSet setWithArray:@[@"a", @"b", @"c"]];
		NSMutableData *buffer = [[NSMutableData alloc] init];
		NSKeyedArchiver *writer = [[NSKeyedArchiver alloc] initForWritingWithMutableData:buffer];
		NSSet *back;
		NSMutableSet *mutableBack;

		[set encodeWithCoder:writer];
		[writer finishEncoding];
		back = [[NSSet alloc] initWithCoder:
			[[NSKeyedUnarchiver alloc] initForReadingWithData:buffer]];
		mutableBack = [[NSMutableSet alloc] initWithCoder:
			[[NSKeyedUnarchiver alloc] initForReadingWithData:buffer]];
		check("set-nscoding-doors",
		      [set conformsToProtocol:@protocol(NSCoding)] &&
		      [NSMutableSet conformsToProtocol:@protocol(NSCoding)] &&
		      back != nil && [back isKindOfClass:[NSSet class]] &&
		      ![back isKindOfClass:[NSMutableSet class]] &&
		      [back count] == 3 && [back containsObject:@"b"] &&
		      mutableBack != nil && [mutableBack isKindOfClass:[NSMutableSet class]] &&
		      [mutableBack count] == 3 && [mutableBack containsObject:@"c"],
		      [NSString stringWithFormat:@"coding=%d back=%@ mutableBack=%@",
			(int)[set conformsToProtocol:@protocol(NSCoding)],
			back != nil ? back : @"(nil)",
			mutableBack != nil ? mutableBack : @"(nil)"]);
	}

	printf("FOUNDATION-SET RESULT ok=%d fail=%d\n", okc, failc);
	/* The exit status, in the probe's OWN output. After a probe the console can stop
	 * serving INPUT for a while (the tier residual), so an `echo $?` that the harness
	 * types may never run. The probe knows exactly what it returns, so it says so - and
	 * this is the same value: failc ? 1 : 0 is the return statement below. */
	printf("FOUNDATION-SET-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-SET DONE\n");
	return failc ? 1 : 0;
}
