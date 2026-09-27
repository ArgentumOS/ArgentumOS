/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_difference — the acceptance for NSOrderedCollectionDifference / NSOrderedCollectionChange
 * (docs/design/foundation-plan.md §62.59, W13's last row).
 *
 * THE CONTRACT THE PLAN NAMES IS THE ONE THING THAT MUST HOLD, AND IT IS THE ONLY THING APPLE SPECIFIES:
 * applying a difference to the SOURCE produces the DESTINATION. Every check below is either that round trip or
 * a fact Apple's pages state in their own words — the partition into -insertions/-removals, the move pairing in
 * -associatedIndex, the suppression options, the equivalence-test door's unset associations, and the exception
 * for broken associations.
 *
 * ONE unit, importing only <Foundation/Foundation.h>. NO FIXTURE: the collections are literals.
 */

#import <Foundation/Foundation.h>

#include <stdio.h>

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-DIFFERENCE %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-DIFFERENCE %s FAIL %s\n", name, detail != nil ? [detail UTF8String] : "");
	}
}

static NSString *fn_s(NSArray *a)
{
	return [a componentsJoinedByString:@","];
}

int main(void)
{
	/* ---- THE CHANGE OBJECT ITSELF ------------------------------------------------------------------- */
	{
		NSOrderedCollectionChange *plain = [NSOrderedCollectionChange changeWithObject:@"x"
										    type:NSCollectionChangeInsert
										   index:4];
		NSOrderedCollectionChange *move = [NSOrderedCollectionChange changeWithObject:@"y"
										   type:NSCollectionChangeRemove
										  index:7
								    associatedIndex:2];

		check("a-change-carries-what-it-was-built-with",
		      [plain object] != nil && [[plain object] isEqual:@"x"] &&
		      [plain changeType] == NSCollectionChangeInsert && [plain index] == 4 &&
		      [plain associatedIndex] == (NSUInteger)NSNotFound,
		      [NSString stringWithFormat:@"plain=%@", plain]);
		check("a-paired-change-keeps-its-associated-index",
		      [move associatedIndex] == 2 && [move index] == 7 &&
		      [move changeType] == NSCollectionChangeRemove,
		      [NSString stringWithFormat:@"move=%@", move]);
	}

	/* ---- THE ROUND TRIP, WHICH IS APPLE'S OWN WORKED EXAMPLE --------------------------------------- */
	{
		NSArray *original = @[@"1", @"2"];
		NSArray *modified = @[@"1", @"2", @"3"];
		NSOrderedCollectionDifference *diff = [modified differenceFromArray:original];
		NSArray *updated = [original arrayByApplyingDifference:diff];

		check("a-difference-inserts-into-the-array-it-was-taken-from",
		      [diff hasChanges] && [[diff insertions] count] == 1 && [[diff removals] count] == 0,
		      [NSString stringWithFormat:@"insertions=%lu removals=%lu",
			 (unsigned long)[[diff insertions] count], (unsigned long)[[diff removals] count]]);
		/* THE INSERTION'S INDEX IS IN THE RECEIVER (the destination), which is the index space Apple's example
		 * pins: @"3" is at index 2 of `modified`. */
		check("an-insertion-index-is-in-the-receiver",
		      [[[diff insertions] objectAtIndex:0] index] == 2,
		      [NSString stringWithFormat:@"index=%lu",
			 (unsigned long)[[[diff insertions] objectAtIndex:0] index]]);
		check("applying-a-difference-produces-the-receiver",
		      [updated isEqualToArray:modified],
		      [NSString stringWithFormat:@"updated=[%@]", fn_s(updated)]);
	}

	/* ---- A REMOVAL'S INDEX IS IN THE ARGUMENT (the source) ---------------------------------------- */
	{
		NSArray *fewer = @[@"1"];
		NSArray *more = @[@"1", @"2", @"3"];
		NSOrderedCollectionDifference *diff = [fewer differenceFromArray:more];

		check("a-removal-index-is-in-the-argument",
		      [[diff removals] count] == 2 && [[diff insertions] count] == 0 &&
		      [[[diff removals] objectAtIndex:0] index] == 1 &&
		      [[[diff removals] objectAtIndex:1] index] == 2,
		      [NSString stringWithFormat:@"removals=%@", [diff removals]]);
		check("a-difference-can-only-remove",
		      [[more arrayByApplyingDifference:diff] isEqualToArray:fewer],
		      [NSString stringWithFormat:@"applied=[%@]", fn_s([more arrayByApplyingDifference:diff])]);
	}

	/* ---- MOVES --------------------------------------------------------------------------------------- */
	{
		NSArray *source = @[@"Red", @"Green", @"Blue"];
		NSArray *dest = @[@"Red", @"Blue", @"Green"];
		NSOrderedCollectionDifference *diff =
			[dest differenceFromArray:source withOptions:NSOrderedCollectionDifferenceCalculationInferMoves];

		check("a-move-is-inferred-as-a-pair",
		      [[diff insertions] count] == 1 && [[diff removals] count] == 1,
		      [NSString stringWithFormat:@"insertions=%lu removals=%lu",
			 (unsigned long)[[diff insertions] count], (unsigned long)[[diff removals] count]]);
		/* Green left index 1 of the source and arrived at index 2 of the destination, so each half names the
		 * other's index — Apple's -associatedIndex page states the pairing in exactly this shape. */
		check("the-two-halves-of-a-move-name-each-other",
		      [[[diff removals] objectAtIndex:0] index] == 1 &&
		      [[[diff removals] objectAtIndex:0] associatedIndex] == 2 &&
		      [[[diff insertions] objectAtIndex:0] index] == 2 &&
		      [[[diff insertions] objectAtIndex:0] associatedIndex] == 1,
		      [NSString stringWithFormat:@"removal=%@ insertion=%@",
			 [[diff removals] objectAtIndex:0], [[diff insertions] objectAtIndex:0]]);
		check("an-inferred-move-still-applies",
		      [[source arrayByApplyingDifference:diff] isEqualToArray:dest],
		      [NSString stringWithFormat:@"applied=[%@]", fn_s([source arrayByApplyingDifference:diff])]);
	}

	/* ---- THE SUPPRESSION OPTIONS ------------------------------------------------------------------- */
	{
		NSArray *source = @[@"a", @"b"];
		NSArray *dest = @[@"a", @"X"];
		NSOrderedCollectionDifference *kept = [dest differenceFromArray:source];
		NSOrderedCollectionDifference *noInserted =
			[dest differenceFromArray:source
				      withOptions:NSOrderedCollectionDifferenceCalculationOmitInsertedObjects];
		NSOrderedCollectionDifference *noRemoved =
			[dest differenceFromArray:source
				      withOptions:NSOrderedCollectionDifferenceCalculationOmitRemovedObjects];

		check("an-unsuppressed-change-carries-its-object",
		      [[[kept insertions] objectAtIndex:0] object] != nil &&
		      [[[kept removals] objectAtIndex:0] object] != nil,
		      @"both sides carry the object by default");
		check("omitting-a-side-leaves-that-side's-object-nil",
		      [[[noInserted insertions] objectAtIndex:0] object] == nil &&
		      [[[noInserted removals] objectAtIndex:0] object] != nil &&
		      [[[noRemoved removals] objectAtIndex:0] object] == nil &&
		      [[[noRemoved insertions] objectAtIndex:0] object] != nil,
		      @"OmitInsertedObjects empties only the insertions (and vice versa)");
	}

	/* ---- THE EQUIVALENCE-TEST DOOR ------------------------------------------------------------------ */
	{
		BOOL (^caseInsensitive)(id, id) = ^BOOL(id one, id two) {
			return [[one lowercaseString] isEqual:[two lowercaseString]];
		};
		NSOrderedCollectionDifference *eq =
			[@[@"A", @"b"] differenceFromArray:@[@"a", @"B"]
					       withOptions:0
				       usingEquivalenceTest:caseInsensitive];
		NSOrderedCollectionDifference *withMoves =
			[@[@"A", @"b"] differenceFromArray:@[@"a", @"B"]
					       withOptions:NSOrderedCollectionDifferenceCalculationInferMoves
				       usingEquivalenceTest:caseInsensitive];

		check("an-equivalence-block-decides-what-counts-as-equal",
		      ![eq hasChanges],
		      [NSString stringWithFormat:@"eq=%@", eq]);
		/* APPLE'S OWN SENTENCE: "don't use the option inferMoves when providing a block for the equivalence
		 * test. The changes returned in the difference object don't include valid values for associatedIndex."
		 * So the move option is IGNORED here and every association stays NSNotFound. */
		check("the-equivalence-form-does-not-infer-moves",
		      ![withMoves hasChanges] ||
		      ([[withMoves insertions] count] > 0 && [[withMoves removals] count] > 0
		       ? [[[withMoves insertions] objectAtIndex:0] associatedIndex] == (NSUInteger)NSNotFound &&
			 [[[withMoves removals] objectAtIndex:0] associatedIndex] == (NSUInteger)NSNotFound
		       : 1),
		      @"no associated index is valid when a block was given");
	}

	/* ---- INVERTING ------------------------------------------------------------------------------------ */
	{
		NSArray *original = @[@"1", @"2", @"3", @"4"];
		NSArray *modified = @[@"2", @"1", @"4", @"3"];
		NSOrderedCollectionDifference *diff = [modified differenceFromArray:original];
		NSArray *applied = [original arrayByApplyingDifference:diff];
		NSArray *back = [applied arrayByApplyingDifference:[diff inverseDifference]];

		check("inverting-a-difference-undoes-it",
		      [applied isEqualToArray:modified] && [back isEqualToArray:original],
		      [NSString stringWithFormat:@"applied=[%@] back=[%@]", fn_s(applied), fn_s(back)]);
	}

	/* ---- TRANSFORMING ------------------------------------------------------------------------------- */
	{
		NSArray *original = @[@"1", @"2"];
		NSArray *modified = @[@"1", @"9", @"2"];
		NSOrderedCollectionDifference *diff = [modified differenceFromArray:original];
		NSOrderedCollectionDifference *mapped =
			[diff differenceByTransformingChangesWithBlock:^NSOrderedCollectionChange *(NSOrderedCollectionChange *c) {
				return [NSOrderedCollectionChange changeWithObject:@"mapped"
									    type:[c changeType]
									   index:[c index]];
			}];

		check("transforming-maps-every-member",
		      [[mapped insertions] count] == [[diff insertions] count] &&
		      [[mapped removals] count] == [[diff removals] count] &&
		      ([[mapped insertions] count] == 0 ||
		       [[[mapped insertions] objectAtIndex:0] object] != nil),
		      [NSString stringWithFormat:@"mapped=%@", mapped]);
	}

	/* ---- THE INDEX-SET FORM --------------------------------------------------------------------------- */
	{
		NSOrderedCollectionDifference *diff =
			[[NSOrderedCollectionDifference alloc]
				initWithInsertIndexes:[NSIndexSet indexSetWithIndex:1]
				     insertedObjects:@[@"one"]
				       removeIndexes:[NSIndexSet indexSetWithIndex:2]
				     removedObjects:@[@"two"]];
		NSArray *applied = [@[@"a", @"b", @"c"] arrayByApplyingDifference:diff];

		/* REMOVALS FIRST, THEN INSERTIONS: index 2 takes @"c" out, leaving [a,b], and the insertion's index 1
		 * is in THAT result — so @"one" lands between a and b. */
		check("the-index-set-form-builds-an-applicable-difference",
		      [diff hasChanges] && [[diff insertions] count] == 1 && [[diff removals] count] == 1 &&
		      [applied isEqualToArray:(@[@"a", @"one", @"b"])],
		      [NSString stringWithFormat:@"applied=[%@]", fn_s(applied)]);
	}

	/* ---- THE BROKEN-ASSOCIATION EXCEPTION APPLE NAMES ------------------------------------------------- */
	{
		BOOL raised = NO;

		@try {
			/* one half of a move, with nothing pointing back: Apple says this raises */
			(void)[[NSOrderedCollectionDifference alloc] initWithChanges:
				@[[NSOrderedCollectionChange changeWithObject:@"x"
									type:NSCollectionChangeRemove
								       index:1
							     associatedIndex:5]]];
		} @catch (NSException *e) {
			raised = [[e name] isEqualToString:NSInvalidArgumentException];
		}
		check("a-broken-association-raises",
		      raised,
		      @"a one-sided association is NSInvalidArgumentException, as Apple's page states");
	}

	/* ---- ORDERING, FAST ENUMERATION, AND THE ORDERED SET ---------------------------------------------- */
	{
		NSArray *original = @[@"a", @"b", @"c", @"d"];
		NSArray *modified = @[@"d", @"c", @"b", @"a"];
		NSOrderedCollectionDifference *diff = [modified differenceFromArray:original];
		NSUInteger enumerated = 0;
		BOOL ascending = YES;
		NSUInteger previous = 0, i;

		for (NSOrderedCollectionChange *c in diff) {
			(void)c;
			enumerated++;
		}
		for (i = 0; i < [[diff insertions] count]; i++) {
			NSUInteger index = [[[diff insertions] objectAtIndex:i] index];

			if (i > 0 && index < previous) {
				ascending = NO;
			}
			previous = index;
		}
		check("fast-enumeration-visits-every-change",
		      enumerated == [[diff insertions] count] + [[diff removals] count] && enumerated > 0,
		      [NSString stringWithFormat:@"enumerated=%lu", (unsigned long)enumerated]);
		check("a-returned-list-is-ascending-by-index",
		      ascending,
		      @"-insertions answers ascending by index (documented as ours)");

		{
			NSOrderedSet *before = [NSOrderedSet orderedSetWithArray:original];
			NSOrderedSet *after = [NSOrderedSet orderedSetWithArray:modified];
			NSOrderedCollectionDifference *setDiff = [after differenceFromOrderedSet:before];
			NSOrderedSet *applied = [before orderedSetByApplyingDifference:setDiff];

			check("an-ordered-set-difference-round-trips",
			      [applied isEqualToOrderedSet:after],
			      [NSString stringWithFormat:@"applied=[%@]", fn_s([applied array])]);
		}
	}

	/* ---- A LARGER CHANGE, WHICH IS WHAT ACTUALLY EXERCISES THE DIFFER ---------------------------------- */
	{
		NSMutableArray *before = [NSMutableArray array];
		NSMutableArray *after = [NSMutableArray array];
		NSOrderedCollectionDifference *diff;
		NSArray *applied;
		NSUInteger i;

		for (i = 0; i < 40; i++) {
			[before addObject:[NSNumber numberWithUnsignedInteger:i]];
		}
		for (i = 0; i < 40; i += 2) {
			[after addObject:[NSNumber numberWithUnsignedInteger:i]];
		}
		[after addObject:[NSNumber numberWithUnsignedInteger:1000]];
		/* COUNTING UP RATHER THAN DOWN: `for (i = 39; i > 0; i -= 2)` is an UNSIGNED underflow — at i == 1 the
		 * decrement wraps to NSUIntegerMax and the loop never ends (it hangs the probe and eats memory). The
		 * index is computed from the counter instead. */
		for (i = 0; i < 20; i++) {
			[after addObject:[NSNumber numberWithUnsignedInteger:39 - 2 * i]];
		}
		diff = [after differenceFromArray:before];
		applied = [before arrayByApplyingDifference:diff];

		check("a-forty-element-change-is-applied-exactly",
		      [applied isEqualToArray:after],
		      [NSString stringWithFormat:@"applied %lu entries, expected %lu",
			 (unsigned long)[applied count], (unsigned long)[after count]]);
	}

	printf("FOUNDATION-DIFFERENCE RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-DIFFERENCE DONE\n");
	return failc == 0 ? 0 : 1;
}
