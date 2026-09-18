/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_predicate, unit 2 of 2 — the checks (ARC). docs/design/foundation-plan.md, F11a.
 *
 *   pred-value          +predicateWithValue: and its rendering — it does not care what is asked
 *   pred-block          +predicateWithBlock:, and the half of Cocoa's shape that is always nil
 *   pred-and            the tree node: AND, and that the first NO DECIDES
 *   pred-or             OR, and that the first YES decides
 *   pred-not            NOT, with its single child
 *   pred-identities     AND of nothing is YES and OR of nothing is NO — the identities
 *   pred-nested         a tree of trees, rendered
 *   pred-filter         -[NSArray filteredArrayUsingPredicate:]: order, and the receiver
 *   pred-filter-mutable -[NSMutableArray filterUsingPredicate:]: in place
 *   pred-abstract       THE BASE RAISES rather than answering a default that would be a lie
 *   pred-nil-filter     a nil predicate raises rather than quietly answering an empty array
 *   pred-refusals       NSExpression, NSComparisonPredicate and the format grammar are ABSENT
 *   cross-tu            a predicate built in the other unit filters here
 */

#import "foundation_predicate.h"
#include <stdio.h>
#include <string.h>
#import <objc/runtime.h>	/* objc_getClass + sel_registerName: the refusals are asserted absent */

static int okc, failc;

static void check(const char *name, int ok, const char *detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-PREDICATE %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-PREDICATE %s FAIL %s\n", name, detail ? detail : "");
	}
}

/* THE DETAIL CARRIES THE MEASUREMENT (§9's lesson): for a filter that is what was KEPT, and for
 * a count it is the number. */
static const char *fn_kept(NSArray *array)
{
	static char why[160];
	NSUInteger i;

	why[0] = '\0';
	if (array == nil) {
		return "(nil array)";
	}
	for (i = 0; i < [array count] && i < 10; i++) {
		char piece[16];

		snprintf(piece, sizeof piece, "%d ", [[array objectAtIndex:i] intValue]);
		strncat(why, piece, sizeof why - strlen(why) - 1);
	}
	if ([array count] == 0) {
		snprintf(why, sizeof why, "(empty)");
	}
	return why;
}

/* A nil predicate, FETCHED rather than written: a literal nil argument is a -Wnonnull finding of
 * its own, and the point of the check below is to pass one. */
static NSPredicate *fn_no_predicate(void)
{
	return nil;
}

static const char *fn_format(id predicate)
{
	if (predicate == nil) {
		return "(nil)";
	}
	return [[(NSPredicate *)predicate predicateFormat] UTF8String];
}

int main(void)
{
	{
		NSPredicate *yes = [NSPredicate predicateWithValue:YES];
		NSPredicate *no = [NSPredicate predicateWithValue:NO];

		/* The value leaf does not CARE what it is asked about, which is what makes it a leaf
		 * and what makes `evaluateWithObject:nil` a legal question. */
		check("pred-value",
		      yes != nil && no != nil &&
		      [yes evaluateWithObject:@1] && [no evaluateWithObject:@1] == NO &&
		      [yes evaluateWithObject:nil] && [no evaluateWithObject:nil] == NO &&
		      [[yes predicateFormat] isEqualToString:@"TRUEPREDICATE"] &&
		      [[no predicateFormat] isEqualToString:@"FALSEPREDICATE"],
		      fn_format(yes));
	}

	{
		static int sawNilBindings;
		NSPredicate *even = [NSPredicate predicateWithBlock:^BOOL(id object, NSDictionary *bindings) {
			sawNilBindings = (bindings == nil);
			return [object intValue] % 2 == 0;
		}];

		sawNilBindings = 0;
		check("pred-block",
		      even != nil &&
		      [even evaluateWithObject:@4] &&
		      ![even evaluateWithObject:@3] &&
		      /* THE DOCUMENTED HALF OF COCOA'S SHAPE: nothing here substitutes, so `bindings`
		       * is nil rather than an empty dictionary. */
		      sawNilBindings == 1 &&
		      [[even predicateFormat] isEqualToString:@"BLOCKPREDICATE"],
		      fn_format(even));
	}

	{
		NSCompoundPredicate *andNo = nil;
		NSCompoundPredicate *andYes = nil;
		NSPredicate *yes = [NSPredicate predicateWithValue:YES];
		NSPredicate *no = [NSPredicate predicateWithValue:NO];
		NSPredicate *c1 = foundation_predicate_counting(NO);
		NSPredicate *c2 = foundation_predicate_counting(NO);
		NSPredicate *c3 = foundation_predicate_counting(NO);
		NSPredicate *c4 = foundation_predicate_counting(NO);
		int callsAfterFalse = -1;
		int callsAfterTrue = -1;
		int ok = 0;
		char detail[140];

		/* The counting leaves are BOUND and GUARDED rather than passed inline: a nullable
		 * factory result in an array literal is a nullable-to-nonnull conversion, which is an
		 * error here on purpose (F6). */
		if (yes != nil && no != nil && c1 != nil && c2 != nil && c3 != nil && c4 != nil) {
			andNo = [NSCompoundPredicate andPredicateWithSubpredicates:@[no, c1, c2]];
			foundation_predicate_reset_calls();
			(void)[andNo evaluateWithObject:@1];
			callsAfterFalse = foundation_predicate_calls();

			andYes = [NSCompoundPredicate andPredicateWithSubpredicates:@[yes, c3, c4]];
			foundation_predicate_reset_calls();
			(void)[andYes evaluateWithObject:@1];
			callsAfterTrue = foundation_predicate_calls();

			/* THE SHORT-CIRCUIT, MEASURED FROM BOTH SIDES: a FALSE first stops the AND before
			 * any leaf is asked (0 calls), and a TRUE first lets exactly ONE counting leaf run
			 * before its NO decides (1 call, not 2). "It stopped" alone would pass for a chain
			 * that never ran. */
			ok = ![andNo evaluateWithObject:@1] && ![andYes evaluateWithObject:@1] &&
			     callsAfterFalse == 0 && callsAfterTrue == 1 &&
			     [andYes compoundPredicateType] == NSAndPredicateType &&
			     [[andYes subpredicates] count] == 3;
		}
		snprintf(detail, sizeof detail,
			 "calls after a FALSE first = %d, after a TRUE first = %d",
			 callsAfterFalse, callsAfterTrue);
		check("pred-and", ok, detail);
	}

	{
		NSCompoundPredicate *or = nil;
		NSPredicate *first = [NSPredicate predicateWithValue:YES];
		NSPredicate *c1 = foundation_predicate_counting(YES);
		NSPredicate *c2 = foundation_predicate_counting(YES);
		int ok = 0;

		if (first != nil && c1 != nil && c2 != nil) {
			or = [NSCompoundPredicate orPredicateWithSubpredicates:@[first, c1, c2]];
			foundation_predicate_reset_calls();
			ok = or != nil && [or evaluateWithObject:@1] &&
			     foundation_predicate_calls() == 0 &&
			     [or compoundPredicateType] == NSOrPredicateType;
		}
		check("pred-or", ok, fn_format(or));
	}

	{
		NSPredicate *inner = [NSPredicate predicateWithValue:NO];
		NSCompoundPredicate *not = nil;

		if (inner != nil) {
			not = [NSCompoundPredicate notPredicateWithSubpredicate:inner];
		}
		/* NOT stores ONE child, so -subpredicates answers one element — the same shape as AND
		 * and OR, which is what lets a walker have a single case. */
		check("pred-not",
		      not != nil && [not evaluateWithObject:@1] &&
		      [not compoundPredicateType] == NSNotPredicateType &&
		      [[not subpredicates] count] == 1 &&
		      [[[not subpredicates] objectAtIndex:0] isEqual:inner] &&
		      [[not predicateFormat] isEqualToString:@"NOT FALSEPREDICATE"],
		      fn_format(not));
	}

	{
		NSPredicate *and = [NSCompoundPredicate andPredicateWithSubpredicates:@[]];
		NSPredicate *or = [NSCompoundPredicate orPredicateWithSubpredicates:@[]];

		/* The IDENTITIES, not special cases: nothing can falsify an empty AND, and nothing can
		 * satisfy an empty OR. */
		check("pred-identities",
		      and != nil && or != nil &&
		      [and evaluateWithObject:@1] && [or evaluateWithObject:@1] == NO &&
		      [[and predicateFormat] isEqualToString:@"()"] &&
		      [[or predicateFormat] isEqualToString:@"()"],
		      fn_format(and));
	}

	{
		NSPredicate *even = foundation_predicate_even();
		NSCompoundPredicate *both = nil;

		/* An `if`, not a ternary: clang's nullability narrowing follows a GUARD, so a ternary
		 * would pass a nullable in an array literal — which is an error here on purpose. */
		if (even != nil) {
			NSCompoundPredicate *notEven =
				[NSCompoundPredicate notPredicateWithSubpredicate:even];

			both = [NSCompoundPredicate orPredicateWithSubpredicates:@[even, notEven]];
		}
		/* A TREE OF TREES: whatever the number is, one of the two branches is true — which is
		 * the only way to assert a disjunction of a leaf and its own negation. */
		check("pred-nested",
		      both != nil &&
		      [both evaluateWithObject:@2] && [both evaluateWithObject:@3] &&
		      [[both predicateFormat]
			isEqualToString:@"(BLOCKPREDICATE OR NOT BLOCKPREDICATE)"],
		      fn_format(both));
	}

	{
		NSArray *numbers = foundation_predicate_numbers();
		NSPredicate *even = foundation_predicate_even();
		NSArray *kept = (numbers != nil && even != nil)
			? [numbers filteredArrayUsingPredicate:even] : nil;

		check("pred-filter",
		      kept != nil && [kept count] == 2 &&
		      [[kept objectAtIndex:0] intValue] == 2 &&
		      [[kept objectAtIndex:1] intValue] == 4 &&
		      /* THE RECEIVER IS UNTOUCHED, and the order is the input's. */
		      [numbers count] == 4 &&
		      [[numbers objectAtIndex:0] intValue] == 1,
		      fn_kept(kept));
	}

	{
		NSArray *numbers = foundation_predicate_numbers();
		NSPredicate *even = foundation_predicate_even();
		NSMutableArray *mutable = [[NSMutableArray alloc] init];

		if (numbers != nil && even != nil) {
			[mutable addObjectsFromArray:numbers];
			[mutable filterUsingPredicate:even];
		}
		check("pred-filter-mutable",
		      [mutable count] == 2 && [[mutable objectAtIndex:0] intValue] == 2,
		      fn_kept(mutable));
	}

	{
		NSPredicate *base = [[NSPredicate alloc] init];
		int evaluateRaised = 0;
		int formatRaised = 0;

		@try {
			(void)[base evaluateWithObject:@1];
		} @catch (NSException *e) {
			(void)e;
			evaluateRaised = 1;
		}
		@try {
			(void)[base predicateFormat];
		} @catch (NSException *e) {
			(void)e;
			formatRaised = 1;
		}
		/* A DEFAULT OF NO WOULD BE A LIE: the caller would read "does not match" where the
		 * truth is "nothing was asked". Both halves of the base say so. */
		check("pred-abstract",
		      evaluateRaised == 1 && formatRaised == 1,
		      evaluateRaised == 0 ? "-evaluateWithObject: answered instead of raising"
		      : "-predicateFormat answered instead of raising");
	}

	{
		NSArray *numbers = foundation_predicate_numbers();
		int raised = 0;

		@try {
			if (numbers != nil) {
				/* nil, ON PURPOSE: this is the bug under test. The conversion diagnostic is right
				 * to complain about it in real code — which is why the nil is FETCHED by a helper
				 * rather than written at the call site. */
				(void)[numbers filteredArrayUsingPredicate:fn_no_predicate()];
			}
		} @catch (NSException *e) {
			(void)e;
			raised = 1;
		}
		check("pred-nil-filter", numbers != nil && raised == 1,
		      raised ? "raised" : "a nil predicate was accepted");
	}

	{
		/* THE REFUSALS, asserted ABSENT. +predicateWithFormat: is F11b — it moves into the
		 * REQUIRED set when the grammar lands, the same way the sort names did. */
		check("pred-refusals",
		      objc_getClass("NSExpression") == NULL &&
		      objc_getClass("NSComparisonPredicate") == NULL &&
		      ![NSPredicate respondsToSelector:sel_registerName("predicateWithFormat:")] &&
		      ![NSPredicate instancesRespondToSelector:
			sel_registerName("predicateWithSubstitutionVariables:")] &&
		      ![NSPredicate instancesRespondToSelector:sel_registerName("allowEvaluation")] &&
		      [NSPredicate respondsToSelector:sel_registerName("predicateWithValue:")] &&
		      [NSPredicate respondsToSelector:sel_registerName("predicateWithBlock:")],
		      "NSExpression, NSComparisonPredicate and the format grammar are absent");
	}

	{
		NSArray *numbers = foundation_predicate_numbers();
		NSPredicate *theirs = foundation_predicate_even();
		NSArray *kept = nil;

		if (numbers != nil && theirs != nil) {
			kept = [numbers filteredArrayUsingPredicate:theirs];
		}
		check("cross-tu",
		      kept != nil && [kept count] == 2 && [[kept objectAtIndex:1] intValue] == 4,
		      fn_kept(kept));
	}

	printf("FOUNDATION-PREDICATE RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-PREDICATE DONE\n");
	return failc ? 1 : 0;
}
