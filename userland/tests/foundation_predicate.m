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

/* PARSE OR NIL, KEEPING WHY: a refusal is only useful if a caller can say what was refused, so
 * the reason is kept for the checks that assert a refusal NAMES its construct. */
static char fn_last_reason[200];

static NSPredicate *fn_parse(NSString *format)
{
	fn_last_reason[0] = '\0';
	@try {
		return [NSPredicate predicateWithFormat:format];
	} @catch (NSException *e) {
		snprintf(fn_last_reason, sizeof fn_last_reason, "%s", [[e reason] UTF8String]);
		return nil;
	}
}

static const char *fn_format(id predicate)
{
	if (predicate == nil) {
		/* THE REASON, not "(nil)": when a PARSE is what failed, the message IS the measurement
		 * — and a detail that hides it sends the reader looking in the wrong place (the §9
		 * lesson, one level down). */
		return fn_last_reason[0] != '\0' ? fn_last_reason : "(nil)";
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
		/* THE EXPRESSION DOOR: a comparison built from two NSExpressions instead of parsed — the
		 * forms the GRAMMAR still refuses (IN, BETWEEN) and the quantifiers (ANY/ALL) included,
		 * because a caller with expressions in hand never needed a parser. The last term is the
		 * anti-drift check: the SAME comparison through both doors must answer the same. */
		NSExpression *five = [NSExpression expressionForConstantValue:@5];
		NSExpression *ten = [NSExpression expressionForConstantValue:@10];
		NSExpression *bounds = [NSExpression expressionForConstantValue:@[@1, @7]];
		NSExpression *list = [NSExpression expressionForConstantValue:@[@3, @5, @7]];
		NSExpression *caps = [NSExpression expressionForConstantValue:@"ABC"];
		NSExpression *lower = [NSExpression expressionForConstantValue:@"abc"];
		NSComparisonPredicate *less =
			[NSComparisonPredicate predicateWithLeftExpression:five
						  rightExpression:ten
							 modifier:NSDirectPredicateModifier
							     type:NSLessThanPredicateOperatorType
							  options:0];
		NSComparisonPredicate *greater =
			[NSComparisonPredicate predicateWithLeftExpression:ten
						  rightExpression:five
							 modifier:NSDirectPredicateModifier
							     type:NSGreaterThanPredicateOperatorType
							  options:0];
		NSComparisonPredicate *between =
			[NSComparisonPredicate predicateWithLeftExpression:five
						  rightExpression:bounds
							 modifier:NSDirectPredicateModifier
							     type:NSBetweenPredicateOperatorType
							  options:0];
		NSComparisonPredicate *in =
			[NSComparisonPredicate predicateWithLeftExpression:five
						  rightExpression:list
							 modifier:NSDirectPredicateModifier
							     type:NSInPredicateOperatorType
							  options:0];
		NSComparisonPredicate *any =
			[NSComparisonPredicate predicateWithLeftExpression:list
						  rightExpression:five
							 modifier:NSAnyPredicateModifier
							     type:NSEqualToPredicateOperatorType
							  options:0];
		NSComparisonPredicate *all =
			[NSComparisonPredicate predicateWithLeftExpression:list
						  rightExpression:five
							 modifier:NSAllPredicateModifier
							     type:NSEqualToPredicateOperatorType
							  options:0];
		NSComparisonPredicate *folded =
			[NSComparisonPredicate predicateWithLeftExpression:caps
						  rightExpression:lower
							 modifier:NSDirectPredicateModifier
							     type:NSEqualToPredicateOperatorType
							  options:NSCaseInsensitivePredicateOption];
		NSPredicate *parsed = [NSPredicate predicateWithFormat:@"5 < 10"];

		check("pred-comparison-expression",
		      less != nil && [less evaluateWithObject:nil] &&
		      greater != nil && [greater evaluateWithObject:nil] &&
		      between != nil && [between evaluateWithObject:nil] &&
		      in != nil && [in evaluateWithObject:nil] &&
		      any != nil && [any evaluateWithObject:nil] &&
		      all != nil && ![all evaluateWithObject:nil] &&
		      folded != nil && [folded evaluateWithObject:nil] &&
		      /* THE SAME ANSWER AS THE GRAMMAR'S OWN LEAF, which is what keeps the one rule one. */
		      parsed != nil && [parsed evaluateWithObject:nil] == [less evaluateWithObject:nil] &&
		      [[less predicateFormat] isEqualToString:@"5 < 10"],
		      [[NSString stringWithFormat:@"less=%d all=%d format=%@",
			(int)[less evaluateWithObject:nil], (int)[all evaluateWithObject:nil],
			[less predicateFormat]] UTF8String]);
	}

	{
		/* THE INVENTORY RULE IN BOTH DIRECTIONS, re-stated: F13.10 landed NSExpression and F13.11
		 * lands NSComparisonPredicate, so the two names that USED to be required ABSENT are now
		 * required PRESENT — while the format grammar's own refusals (IN, ANY, `$`) are still
		 * checked, in format-refusals below. */
		check("pred-refusals",
		      objc_getClass("NSExpression") != NULL &&
		      objc_getClass("NSComparisonPredicate") != NULL &&
		      ![NSPredicate instancesRespondToSelector:
			sel_registerName("predicateWithSubstitutionVariables:")] &&
		      ![NSPredicate instancesRespondToSelector:sel_registerName("allowEvaluation")] &&
		      [NSPredicate respondsToSelector:sel_registerName("predicateWithValue:")] &&
		      [NSPredicate respondsToSelector:sel_registerName("predicateWithBlock:")] &&
		      [NSPredicate respondsToSelector:sel_registerName("predicateWithFormat:")] &&
		      [NSPredicate instancesRespondToSelector:sel_registerName("initWithFormat:")],
		      "NSExpression and NSComparisonPredicate are present; substitution is absent; "
		      "the format grammar is present");
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

	/* --------------------------------------------------------------- F11b: the grammar */

	{
		/* EACH PARSE KEEPS ITS OWN REASON: a check that says only "the parse failed" costs a
		 * round trip, and one that names WHICH string failed and why costs a snprintf. */
		char equalReason[200];
		char olderReason[200];
		char detail[460];
		NSPredicate *equal = fn_parse(@"name = \"ann\"");
		NSPredicate *older;
		NSDictionary *ann;
		NSDictionary *kid;

		snprintf(equalReason, sizeof equalReason, "%s", fn_last_reason);
		older = fn_parse(@"age > 30");
		snprintf(olderReason, sizeof olderReason, "%s", fn_last_reason);

		ann = @{ @"name": @"ann", @"age": @40 };
		/* THE OTHER OBJECT IS NOT AN ANN, and is younger than 30: ONE object separates both
		 * halves of this check, so a predicate that ignored its comparison could not pass. */
		kid = @{ @"name": @"bob", @"age": @10 };

		snprintf(detail, sizeof detail, "equal=%s older=%s four-char-key=%s self-shape=%s",
			 equal == nil ? equalReason : "(parsed)",
			 older == nil ? olderReason : "(parsed)",
			 /* CONTROLS for the one remaining difference: `SELF > 2` parses and `age > 30`
			  * does not, and the same comparison INSIDE a compound parses. So the experiment
			  * is that comparison with a four-character key, and with a keyword operator. */
			 fn_parse(@"rank > 30") == nil ? "FAILED" : "parsed",
			 fn_parse(@"age CONTAINS \"3\"") == nil ? "FAILED" : "parsed");
		/* THE OBJECTS ARE DICTIONARIES on purpose: that makes the key path exercise KVC's
		 * dictionary form (F9) rather than needing a new fixture class. */
		check("format-compare",
		      equal != nil && older != nil &&
		      [equal evaluateWithObject:ann] && ![equal evaluateWithObject:kid] &&
		      [older evaluateWithObject:ann] && ![older evaluateWithObject:kid],
		      detail);
	}

	{
		NSDictionary *ann = @{ @"name": @"annabel" };
		NSPredicate *contains = fn_parse(@"name CONTAINS \"nna\"");
		NSPredicate *begins = fn_parse(@"name BEGINSWITH \"anna\"");
		NSPredicate *ends = fn_parse(@"name ENDSWITH \"bel\"");
		NSPredicate *misses = fn_parse(@"name CONTAINS \"zzz\"");

		check("format-string-ops",
		      contains != nil && begins != nil && ends != nil && misses != nil &&
		      [contains evaluateWithObject:ann] && [begins evaluateWithObject:ann] &&
		      [ends evaluateWithObject:ann] && ![misses evaluateWithObject:ann],
		      fn_format(contains));
	}

	{
		NSDictionary *text = @{ @"name": @"a*b" };
		NSDictionary *other = @{ @"name": @"axxb" };
		NSPredicate *star = fn_parse(@"name LIKE \"a*b\"");
		NSPredicate *one = fn_parse(@"name LIKE \"a?b\"");
		/* THE ESCAPE, WRITTEN WITH A DOUBLED BACKSLASH: the string scanner unescapes the four
		 * pairs it knows and "anything else is itself" (the same deliberate rule the house's
		 * other parsers use), so `\\*` in the FORMAT is what leaves `\*` in the PATTERN — and a
		 * `\*` in the pattern is what makes LIKE read a literal star. */
		NSPredicate *literalStar = fn_parse(@"name LIKE \"a\\\\*b\"");
		char detail[220];

		/* EVERY MEASUREMENT, because this check has already cost three rounds by failing with
		 * only a rendering to look at (the §9 lesson, learned again). */
		snprintf(detail, sizeof detail,
			 "star(al*)=%d/alxxb=%d one(a?b)=%d/alxxb=%d literal(a\\*b)=%d/axxb=%d",
			 [star evaluateWithObject:text] ? 1 : 0,
			 [star evaluateWithObject:other] ? 1 : 0,
			 [one evaluateWithObject:text] ? 1 : 0,
			 [one evaluateWithObject:other] ? 1 : 0,
			 [literalStar evaluateWithObject:text] ? 1 : 0,
			 [literalStar evaluateWithObject:other] ? 1 : 0);
		check("format-like",
		      star != nil && one != nil && literalStar != nil &&
		      [star evaluateWithObject:text] && [star evaluateWithObject:other] &&
		      /* `a?b` MATCHES `a*b` — three characters, and the middle one is whatever it is —
		       * and does NOT match the four-character one. The pair is what makes the `?`
		       * claim mean something: on its own, `[one evaluateWithObject:that]` would pass
		       * for a matcher that ignored `?` entirely. */
		      [one evaluateWithObject:text] && ![one evaluateWithObject:other] &&
		      /* And a pattern WITH a `*` matches only a literal `*`. */
		      [literalStar evaluateWithObject:text] &&
		      ![literalStar evaluateWithObject:other],
		      detail);
	}

	{
		NSDictionary *shout = @{ @"name": @"ANN" };
		NSPredicate *exact = fn_parse(@"name = \"ann\"");
		NSPredicate *folded = fn_parse(@"name = \"ann\"[c]");
		NSPredicate *prefix = fn_parse(@"name BEGINSWITH \"an\"[c]");

		check("format-case",
		      exact != nil && folded != nil && prefix != nil &&
		      ![exact evaluateWithObject:shout] &&
		      [folded evaluateWithObject:shout] &&
		      [prefix evaluateWithObject:shout] &&
		      [[folded predicateFormat] rangeOfString:@"[c]"].location != NSNotFound,
		      fn_format(folded));
	}

	{
		NSDictionary *ann = @{ @"name": @"ann", @"age": @40 };
		NSDictionary *kid = @{ @"name": @"ann", @"age": @10 };
		NSDictionary *bob = @{ @"name": @"bob", @"age": @40 };
		/* PRECEDENCE: NOT binds tighter than AND, and AND tighter than OR — so this is
		 * (`ann` AND `40`) OR `bob`, and the kid is the object that separates the two. */
		NSPredicate *mixed = fn_parse(@"name = \"bob\" OR name = \"ann\" AND age > 30");
		NSPredicate *negated = fn_parse(@"NOT name = \"ann\"");

		check("format-connectives",
		      mixed != nil && negated != nil &&
		      [mixed evaluateWithObject:ann] &&
		      [mixed evaluateWithObject:bob] &&
		      ![mixed evaluateWithObject:kid] &&
		      [negated evaluateWithObject:bob] && ![negated evaluateWithObject:ann],
		      fn_format(mixed));
	}

	{
		NSDictionary *bare = @{ @"name": @"ann" };
		NSPredicate *always = fn_parse(@"TRUEPREDICATE");
		NSPredicate *never = fn_parse(@"FALSEPREDICATE");
		NSPredicate *isNothing = fn_parse(@"missing = NULL");
		NSPredicate *isNotNothing = fn_parse(@"name != NULL");

		check("format-constants",
		      always != nil && never != nil && isNothing != nil && isNotNothing != nil &&
		      [always evaluateWithObject:bare] && ![never evaluateWithObject:bare] &&
		      /* A missing dictionary key answers nil through KVC, so this is a real NULL. */
		      [isNothing evaluateWithObject:bare] && [isNotNothing evaluateWithObject:bare],
		      fn_format(isNothing));
	}

	{
		NSPredicate *three = fn_parse(@"SELF = 3");
		NSPredicate *more = fn_parse(@"SELF > 2");
		NSPredicate *less = fn_parse(@"SELF < 2");

		check("format-self",
		      three != nil && more != nil && less != nil &&
		      [three evaluateWithObject:@3] && ![three evaluateWithObject:@4] &&
		      [more evaluateWithObject:@3] && ![less evaluateWithObject:@3],
		      fn_format(three));
	}

	{
		NSPredicate *half = fn_parse(@"SELF = 2.5");
		NSPredicate *atLeast = fn_parse(@"SELF >= 2.5");
		NSPredicate *negative = fn_parse(@"SELF = -4");

		check("format-numbers",
		      half != nil && atLeast != nil && negative != nil &&
		      [half evaluateWithObject:@2.5] && ![half evaluateWithObject:@2] &&
		      [atLeast evaluateWithObject:@3] && [atLeast evaluateWithObject:@2.5] &&
		      [negative evaluateWithObject:@(-4)],
		      fn_format(atLeast));
	}

	{
		/* THE ROUND TRIP, WITH CONTENT: a rendered predicate must parse back to something that
		 * ANSWERS THE SAME. Idempotence alone would pass for a renderer that produced nothing
		 * usable, so every object is asked of BOTH trees. */
		NSPredicate *first = fn_parse(@"name BEGINSWITH \"a\" AND age > 30");
		NSString *rendered = first != nil ? [first predicateFormat] : nil;
		NSPredicate *second = rendered != nil ? fn_parse(rendered) : nil;
		NSDictionary *ann = @{ @"name": @"ann", @"age": @40 };
		NSDictionary *kid = @{ @"name": @"ann", @"age": @10 };
		NSDictionary *bob = @{ @"name": @"bob", @"age": @40 };

		check("format-round-trip",
		      first != nil && second != nil && [rendered length] > 0 &&
		      [first evaluateWithObject:ann] && [second evaluateWithObject:ann] &&
		      ![first evaluateWithObject:bob] && ![second evaluateWithObject:bob] &&
		      ![first evaluateWithObject:kid] && ![second evaluateWithObject:kid] &&
		      [rendered isEqualToString:[second predicateFormat]],
		      rendered == nil ? "(no rendering)" : [rendered UTF8String]);
	}

	{
		/* F13.7d: MATCHES AND [d] SHIP, so they are no longer in this list — and the checks
		 * after it assert them POSITIVELY. That is the pair rule the calendars got too: a
		 * refusal deleted without a presence check is indistinguishable from a probe that
		 * stopped looking. */
		NSPredicate *membership = fn_parse(@"name IN {\"ann\"}");
		NSPredicate *quantifier = fn_parse(@"ANY children.age > 3");
		NSPredicate *variable = fn_parse(@"name = $NAME");
		NSPredicate *truncated = fn_parse(@"name =");

		/* EVERY REMAINING REFUSAL NAMES ITSELF. That is what refusing loudly MEANS, so the
		 * message is part of the check rather than a nicety: a caller has to be able to see
		 * WHICH construct was refused. */
		check("format-refusals",
		      membership == nil && quantifier == nil && variable == nil &&
		      truncated == nil &&
		      strstr(fn_last_reason, "operand") != NULL,
		      fn_last_reason);
	}

	{
		/* MATCHES, on the engine musl ships inside libc. THE ANCHORING IS THE CLAIM: Cocoa's
		 * MATCHES is a WHOLE-STRING match, so "ann" MATCHES "n" is NO while ".*n.*" is YES —
		 * a bare regexec, which is unanchored, would say YES to both.
		 *
		 * THE MODIFIER GOES AFTER THE RIGHT OPERAND here, which is this grammar's spelling;
		 * Cocoa writes it between the operator and the operand (`MATCHES[c] "ANN"`), and that
		 * difference is recorded in the plan as the remaining fidelity gap rather than
		 * papered over here. */
		NSPredicate *whole = fn_parse(@"name MATCHES \"a.*\"");
		NSPredicate *partial = fn_parse(@"name MATCHES \"n\"");
		NSPredicate *wrapped = fn_parse(@"name MATCHES \".*n.*\"");
		NSPredicate *folded = fn_parse(@"name MATCHES \"ANN\"[c]");
		NSDictionary *person = @{ @"name": @"ann" };

		check("format-matches",
		      whole != nil && partial != nil && wrapped != nil && folded != nil &&
		      [whole evaluateWithObject:person] &&
		      ![partial evaluateWithObject:person] &&
		      [wrapped evaluateWithObject:person] &&
		      [folded evaluateWithObject:person],
		      fn_last_reason);
	}

	{
		/* `[d]`, through ICU's collator: ACCENTS STOP MATTERING WHILE CASE KEEPS MATTERING —
		 * the difference between `[d]` and `[c]`, and the reason `[d]` alone is not "ignore
		 * everything". (The fixture is "änn" against "ann": at primary strength the umlaut is
		 * its base letter, so the two are three characters each and compare EQUAL.) */
		NSPredicate *folding = fn_parse(@"name = \"ann\"[d]");
		NSPredicate *strict = fn_parse(@"name = \"ann\"");
		NSPredicate *caseStillMatters = fn_parse(@"name = \"ANN\"[d]");
		NSPredicate *both = fn_parse(@"name = \"ANN\"[cd]");
		NSDictionary *accented = @{ @"name": @"änn" };
		NSDictionary *plain = @{ @"name": @"ann" };

		check("format-diacritic",
		      folding != nil && strict != nil && caseStillMatters != nil && both != nil &&
		      [folding evaluateWithObject:accented] &&
		      ![strict evaluateWithObject:accented] &&
		      [folding evaluateWithObject:plain] &&
		      ![caseStillMatters evaluateWithObject:plain] &&
		      [both evaluateWithObject:plain],
		      fn_last_reason);
	}

	{
		/* A BAD PATTERN IS REFUSED, LOUDLY, and the message names the construct — the same
		 * standard every other refusal here is held to. NOTE WHERE IT HAPPENS: the parse
		 * SUCCEEDS (the grammar builds the comparison) and the REGEX IS COMPILED when the
		 * predicate runs, so the raise comes from evaluation. Measured, not assumed — the
		 * first version of this check expected the parse to refuse and was wrong. */
		NSPredicate *broken = fn_parse(@"name MATCHES \"([\"");
		BOOL raised = NO;
		BOOL names = NO;

		if (broken != nil) {
			@try {
				(void)[broken evaluateWithObject:@{ @"name": @"ann" }];
			} @catch (NSException *exception) {
				raised = YES;
				names = [[exception reason] rangeOfString:@"MATCHES"].location
					!= NSNotFound;
			}
		}
		check("format-regex-refusal",
		      broken != nil && raised && names,
		      broken == nil ? "the parse refused it"
				    : "it parsed, and then did not raise");
	}

	{
		NSArray *people = @[ @{ @"name": @"ann", @"age": @40 },
				     @{ @"name": @"bob", @"age": @10 },
				     @{ @"name": @"ann", @"age": @9 } ];
		NSPredicate *parsed = fn_parse(@"name = \"ann\" AND age > 30");
		NSArray *kept = parsed != nil ? [people filteredArrayUsingPredicate:parsed] : nil;

		/* THE TWO HALVES JOINED: a predicate WRITTEN as text, used to filter — which is the
		 * whole point of having a grammar at all. */
		check("format-filter",
		      kept != nil && [kept count] == 1 &&
		      [[[kept objectAtIndex:0] valueForKey:@"age"] intValue] == 40 &&
		      [people count] == 3,
		      fn_format(parsed));
	}

	{
		/* THE TWO PREDICATE DOORS (§63.16), over the REAL archive path, and with the DEEP TREE built on
		 * purpose: a compound AND of two comparison predicates of expressions is §63.10-§63.15 in ONE archive —
		 * the compound's door, the comparison's doors, the expression's doors and the array's, each reached
		 * because the one below it asked. Round-tripping one comparison alone would have left the nesting
		 * untested.
		 *
		 * AND THE ROUND TRIP IS MEASURED BY EVALUATING, not only by shape. Asserting the fields would pass a
		 * decoder that restored every field and got the RULE wrong (`AND` that came back as `OR`, a `<` that
		 * came back as `<=`); asking the restored predicate about VALUES is what makes the answer matter. Both
		 * orders are asked, because a decoder that swapped the two expressions would satisfy one. */
		NSExpression *age = [NSExpression expressionForKeyPath:@"age"];
		NSExpression *thirty = [NSExpression expressionForConstantValue:@30];
		NSExpression *name = [NSExpression expressionForKeyPath:@"name"];
		NSExpression *alice = [NSExpression expressionForConstantValue:@"alice"];
		NSComparisonPredicate *older =
			[NSComparisonPredicate predicateWithLeftExpression:age
						  rightExpression:thirty
							 modifier:NSDirectPredicateModifier
							     type:NSGreaterThanPredicateOperatorType
							  options:0];
		NSComparisonPredicate *named =
			[NSComparisonPredicate predicateWithLeftExpression:name
						  rightExpression:alice
							 modifier:NSDirectPredicateModifier
							     type:NSEqualToPredicateOperatorType
							  options:NSCaseInsensitivePredicateOption];
		NSCompoundPredicate *both =
			[NSCompoundPredicate andPredicateWithSubpredicates:@[older, named]];
		NSData *bothData = [NSKeyedArchiver archivedDataWithRootObject:both];
		NSCompoundPredicate *backBoth = bothData != nil
			? [NSKeyedUnarchiver unarchiveObjectWithData:bothData] : nil;
		NSData *olderData = [NSKeyedArchiver archivedDataWithRootObject:older];
		NSComparisonPredicate *backOlder = olderData != nil
			? [NSKeyedUnarchiver unarchiveObjectWithData:olderData] : nil;
		NSArray *passes = @[ @{ @"age" : @40, @"name" : @"ALICE" } ];
		NSArray *tooYoung = @[ @{ @"age" : @20, @"name" : @"ALICE" } ];
		NSArray *wrongName = @[ @{ @"age" : @40, @"name" : @"bob" } ];
		BOOL refusedHalf = NO;

		@try {
			/* A COMPARISON MISSING ONE EXPRESSION, written over the same public door: the class's own
			 * designated initializer refuses it, so the decoder's refusal is that line rather than a second
			 * check — asserted here so the two cannot drift apart. */
			NSMutableData *buffer = [[NSMutableData alloc] init];
			NSKeyedArchiver *writer = [[NSKeyedArchiver alloc]
				initForWritingWithMutableData:buffer];

			[writer encodeObject:[NSExpression expressionForConstantValue:@1]
				     forKey:@"NS.left"];
			[writer encodeInteger:NSGreaterThanPredicateOperatorType forKey:@"NS.operatorType"];
			[writer finishEncoding];
			(void)[[NSComparisonPredicate alloc] initWithCoder:
				[[NSKeyedUnarchiver alloc] initForReadingWithData:buffer]];
		} @catch (NSException *e) {
			refusedHalf = [[e name] isEqualToString:NSInvalidArgumentException];
		}
		/* A DIAGNOSTIC IN A C BUFFER, because THIS probe's check() takes a `const char *` — and because the
		 * first version of this check passed a PROSE detail, which made its failure unreadable: a check that
		 * cannot name the value it failed on is a check that costs a build to diagnose. */
		{
			char detail[320];

			int tCoding = [older conformsToProtocol:@protocol(NSCoding)] &&
				[both conformsToProtocol:@protocol(NSCoding)];
			int tKind = backBoth != nil && [backBoth isKindOfClass:[NSCompoundPredicate class]] &&
				[backBoth compoundPredicateType] == NSAndPredicateType &&
				[[backBoth subpredicates] count] == 2;
			int tChild0 = backBoth != nil &&
				[[[backBoth subpredicates] objectAtIndex:0] isKindOfClass:
					[NSComparisonPredicate class]];
			int tOlder = backOlder != nil &&
				[backOlder predicateOperatorType] == NSGreaterThanPredicateOperatorType &&
				[backOlder comparisonPredicateModifier] == NSDirectPredicateModifier &&
				[[[backOlder leftExpression] keyPath] isEqualToString:@"age"] &&
				[[[[backOlder rightExpression] constantValue] description] isEqualToString:@"30"];
			int tChild1 = backBoth != nil &&
				([[[backBoth subpredicates] objectAtIndex:1] options] &
				 (NSUInteger)NSCaseInsensitivePredicateOption) != 0 &&
				[(NSPredicate *)[[backBoth subpredicates] objectAtIndex:1] evaluateWithObject:
					[passes objectAtIndex:0]];
			int tEvaluates = backBoth != nil &&
				[backBoth evaluateWithObject:[passes objectAtIndex:0]] &&
				![backBoth evaluateWithObject:[tooYoung objectAtIndex:0]] &&
				![backBoth evaluateWithObject:[wrongName objectAtIndex:0]];

			/* EVERY TERM BY NAME, so a failure names the one that failed: the first version of this check
			 * passed a PROSE detail and cost builds to diagnose, which is the probe lesson this thread has now
			 * learned more than once. */
			snprintf(detail, sizeof detail,
				 "coding=%d kind=%d child0=%d older=%d child1=%d evaluates=%d refused=%d type=%ld op=%ld options=%ld children=%lu",
				 tCoding, tKind, tChild0, tOlder, tChild1, tEvaluates, (int)refusedHalf,
				 (long)[backBoth compoundPredicateType], (long)[backOlder predicateOperatorType],
				 (long)[backOlder options],
				 (unsigned long)(backBoth != nil ? [[backBoth subpredicates] count] : 0));
			check("predicate-nscoding-round-trip",
			      tCoding && tKind && tChild0 && tOlder && tChild1 && tEvaluates && refusedHalf,
			      detail);
		}
	}

	printf("FOUNDATION-PREDICATE RESULT ok=%d fail=%d\n", okc, failc);
	/* The exit status, in the probe's OWN output. After a probe the console can stop
	 * serving INPUT for a while (the tier residual), so an `echo $?` that the harness
	 * types may never run. The probe knows exactly what it returns, so it says so - and
	 * this is the same value: failc ? 1 : 0 is the return statement below. */
	printf("FOUNDATION-PREDICATE-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-PREDICATE DONE\n");
	return failc ? 1 : 0;
}
