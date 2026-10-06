/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_sort, unit 2 of 2 — the checks (ARC). docs/design/foundation-plan.md, F10.
 *
 *   sort-descriptor  the value itself: key, direction, and which comparison kind
 *   sort-reversed    -reversedSortDescriptor flips the DIRECTION and nothing else
 *   sort-array       a sort whose key is resolved by NAME, through KVC
 *   sort-chain       a chain is lexicographic: the first decides, a TIE falls to the next
 *   sort-stable      equal keys keep their INPUT order — the promise, measured
 *   sort-selector    the selector form, called as a SCALAR (F9's trap, not repeated)
 *   sort-comparator  the comparator-block form
 *   sort-function    the C-function form, with its `context` handed through
 *   sort-mutable     the two mutable sorts
 *   sort-nil-value   a nil value RAISES — and a one-element array, which has nothing to
 *                    compare, is what says the rule is about a comparison
 *   sort-refusals    -allowEvaluation and the coder forms are ABSENT
 *   cross-tu         a descriptor built in the other unit sorts here
 */

#import "foundation_sort.h"
#include <stdio.h>
#include <string.h>
#import <objc/runtime.h>

static int okc, failc;

static int lastcheck;

static void check(const char *name, int ok, const char *detail)
{
	lastcheck = ok;	/* read by covers() */
	if (ok) {
		okc++;
		printf("FOUNDATION-SORT %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-SORT %s FAIL %s\n", name, detail ? detail : "");
	}
}

/* covers("NSLocale", "canonicalLocaleIdentifierFromString:") - the behavioural claim, piggybacked on the check above it. */
static void covers_(const char *cls, const char *sel)
{
	if (lastcheck) {
		printf("COVERS %s %s\n", cls, sel);
	}
}
#define covers(cls, sel) covers_(cls, sel)

/*
 * THE DETAIL CARRIES THE MEASUREMENT (§9's lesson): a failure prints the ORDER THAT CAME OUT,
 * because for a sort the order IS the measurement.
 */
static const char *fn_why(NSArray *people)
{
	static char why[200];
	NSUInteger i;
	NSUInteger count;

	why[0] = '\0';
	if (people == nil) {
		return "(nil array)";
	}
	count = [people count];
	for (i = 0; i < count && i < 8; i++) {
		id name = [[people objectAtIndex:i] valueForKey:@"name"];
		id rank = [[people objectAtIndex:i] valueForKey:@"rank"];
		char piece[40];

		snprintf(piece, sizeof piece, "%s(%ld) ",
			 name == nil ? "?" : [(NSString *)name UTF8String],
			 rank == nil ? (long)-1 : (long)[(NSNumber *)rank integerValue]);
		strncat(why, piece, sizeof why - strlen(why) - 1);
	}
	return why;
}

static NSString *fn_names(NSArray *people)
{
	NSMutableString *out = [[NSMutableString alloc] init];
	NSUInteger i;

	for (i = 0; i < [people count]; i++) {
		id name = [[people objectAtIndex:i] valueForKey:@"name"];

		[out appendString:name == nil ? @"?" : (NSString *)name];
	}
	return out;
}

static NSString *fn_ranks(NSArray *people)
{
	NSMutableString *out = [[NSMutableString alloc] init];
	NSUInteger i;

	for (i = 0; i < [people count]; i++) {
		id rank = [[people objectAtIndex:i] valueForKey:@"rank"];
		NSString *text = [NSString stringWithFormat:@"%ld",
				  rank == nil ? (long)-1
				  : (long)[(NSNumber *)rank integerValue]];

		[out appendString:text];
	}
	return out;
}

/* The C function the `-…UsingFunction:context:` form takes — a FUNCTION POINTER, and the
 * context is how this check knows it arrived: every call bumps the counter it points at. */
static NSInteger fn_rank_comparator(id left, id right, void *context)
{
	long l = [[left valueForKey:@"rank"] integerValue];
	long r = [[right valueForKey:@"rank"] integerValue];
	int *calls = (int *)context;

	if (calls != NULL) {
		(*calls)++;
	}
	if (l < r) {
		return NSOrderedAscending;
	}
	if (l > r) {
		return NSOrderedDescending;
	}
	return NSOrderedSame;
}

/* A DIAGNOSTIC IN A C BUFFER, because THIS probe's check() takes a `const char *` — and every value the
 * descriptor check rests on is named in it, so a failure names the term that failed instead of costing a guest
 * run per hypothesis (§63.16's lesson, applied before it could bite a second time). */
static const char *fn_sort_coding_detail(NSSortDescriptor *plain, NSSortDescriptor *backPlain,
					 NSSortDescriptor *withSelector, NSSortDescriptor *backSelector,
					 int refusedComparator)
{
	static char detail[320];

	snprintf(detail, sizeof detail,
		 "coding=%d plainKey=%s plainAsc=%d plainSel=%d backKey=%s backAsc=%d selByName=%d origSel=%d refused=%d fnPair=%d backSelName=%s",
		 (int)[plain conformsToProtocol:@protocol(NSCoding)],
		 backPlain != nil && [backPlain key] != nil
			? [[backPlain key] UTF8String] : "(nil)",
		 (int)(backPlain != nil && [backPlain ascending]),
		 (int)(backPlain != nil && [backPlain selector] != NULL),
		 backSelector != nil && [backSelector key] != nil
			? [[backSelector key] UTF8String] : "(nil)",
		 (int)(backSelector != nil && [backSelector ascending]),
		 (int)(backSelector != nil && [backSelector selector] == @selector(compare:)),
		 (int)([withSelector selector] == @selector(compare:)),
		 refusedComparator,
		 /* THE FUNCTION PAIR ITSELF, tested with `sel_isEqual` AND NOT `==`: in this runtime a selector's
		  * POINTER is not the contract — a typed and an untyped selector for the same name are different
		  * objects — which is why this term read 0 when it was written as `==` while the NAME beside it read
		  * "compare:". The NAME is what both functions promise; `sel_isEqual` is what the runtime provides to
		  * compare them. */
		 (int)sel_isEqual(NSSelectorFromString(NSStringFromSelector(@selector(compare:))),
				  @selector(compare:)),
		 backSelector != nil && [backSelector selector] != NULL
			? sel_getName([backSelector selector]) : "(null)");
	return detail;
}

int main(void)
{
	{
		NSSortDescriptor *d = [NSSortDescriptor sortDescriptorWithKey:@"name" ascending:YES];
		NSSortDescriptor *selectorForm = [[NSSortDescriptor alloc] initWithKey:@"rank"
									  ascending:YES
									   selector:@selector(compare:)];
		NSSortDescriptor *comparatorForm = [[NSSortDescriptor alloc] initWithKey:@"rank"
									    ascending:YES
									   comparator:^NSComparisonResult(id l, id r) {
			return NSOrderedSame;
		}];

		/* The DEFAULT comparison is -compare:, and the three constructors are distinct so
		 * that "how" is never silently assumed. */
		check("sort-descriptor",
		      d != nil && [[d key] isEqualToString:@"name"] && [d ascending] &&
		      [d selector] == NULL && [d comparator] == nil &&
		      selectorForm != nil && [selectorForm selector] == @selector(compare:) &&
		      comparatorForm != nil && [comparatorForm comparator] != nil &&
		      [[d description] length] > 0,
		      d == nil ? "(nil descriptor)" : [[d description] UTF8String]);
	}

	{
		NSArray *people = foundation_sort_people();
		NSSortDescriptor *byName = [NSSortDescriptor sortDescriptorWithKey:@"name" ascending:YES];
		NSSortDescriptor *byNameDown = [byName reversedSortDescriptor];
		NSArray *ascending = (people != nil && byName != nil)
			? [people sortedArrayUsingDescriptors:@[byName]] : nil;
		NSArray *descending = (people != nil && byNameDown != nil)
			? [people sortedArrayUsingDescriptors:@[byNameDown]] : nil;

		/* ONLY the direction is the descriptor's to flip: the key survives, and the two
		 * sorts are exact reverses of one another. */
		check("sort-reversed",
		      byNameDown != nil && [[byNameDown key] isEqualToString:@"name"] &&
		      [byName ascending] && ![byNameDown ascending] &&
		      [fn_names(ascending) isEqualToString:@"annannbobcid"] &&
		      [fn_names(descending) isEqualToString:@"cidbobannann"],
		      fn_why(ascending));
	}

	{
		NSArray *people = foundation_sort_people();
		NSSortDescriptor *byRank = [[NSSortDescriptor alloc] initWithKey:@"rank" ascending:YES];
		NSArray *sorted = (people != nil && byRank != nil)
			? [people sortedArrayUsingDescriptors:@[byRank]] : nil;
		NSString *ranks = sorted != nil ? fn_ranks(sorted) : nil;

		/* THE KEY IS RESOLVED BY NAME through KVC (F9): this unit has no idea what a `rank`
		 * is, and the objects were built in the other unit. The receiver is untouched — its
		 * own -valueForKey: map still reads 3,1,2,4. */
		check("sort-array",
		      sorted != nil && [ranks isEqualToString:@"1234"] &&
		      [[people valueForKey:@"rank"] count] == 4 &&
		      [[[people valueForKey:@"rank"] objectAtIndex:0] intValue] == 3,
		      ranks == nil ? "(nil)" : [ranks UTF8String]);
	}

	{
		NSArray *people = foundation_sort_people();
		NSSortDescriptor *byName = [NSSortDescriptor sortDescriptorWithKey:@"name" ascending:YES];
		NSSortDescriptor *byRank = [[NSSortDescriptor alloc] initWithKey:@"rank" ascending:YES];
		NSArray *chained = (people != nil && byName != nil && byRank != nil)
			? [people sortedArrayUsingDescriptors:@[byName, byRank]] : nil;
		NSString *chainedRanks = chained != nil ? fn_ranks(chained) : nil;

		/* THE CHAIN: the tie on @"ann" falls to the rank descriptor, so ann(2) sorts before
		 * ann(3) — the REVERSE of the input order, which is what makes this check different
		 * from the stability one below. */
		check("sort-chain",
		      chained != nil && [chainedRanks isEqualToString:@"2314"],
		      chainedRanks == nil ? "(nil)" : [chainedRanks UTF8String]);
	}

	{
		NSArray *people = foundation_sort_people();
		NSSortDescriptor *byName = [NSSortDescriptor sortDescriptorWithKey:@"name" ascending:YES];
		NSArray *sorted = (people != nil && byName != nil)
			? [people sortedArrayUsingDescriptors:@[byName]] : nil;

		/* THE PROMISE, MEASURED: with the tie unbroken the two anns keep their INPUT order
		 * (rank 3 before rank 2). An unstable sort passes every single-descriptor test ever
		 * written and fails exactly here, which is why this check exists. */
		check("sort-stable",
		      sorted != nil && [fn_ranks(sorted) isEqualToString:@"3214"],
		      fn_why(sorted));
	}

	{
		NSArray *people = foundation_sort_people();
		NSSortDescriptor *bySelector = [[NSSortDescriptor alloc] initWithKey:@"name"
									  ascending:YES
									   selector:@selector(compare:)];
		NSArray *sorted = (people != nil && bySelector != nil)
			? [people sortedArrayUsingDescriptors:@[bySelector]] : nil;
		NSString *names = sorted != nil ? fn_names(sorted) : nil;

		/* A COMPARISON SELECTOR RETURNS NSComparisonResult — a SCALAR. -performSelector: is
		 * typed as returning `id`, and reading that scalar as a pointer is the crash F9's
		 * probe found; this check is what says the lesson took. */
		check("sort-selector",
		      sorted != nil && [names isEqualToString:@"annannbobcid"],
		      names == nil ? "(nil)" : [names UTF8String]);
	}

	{
		NSArray *people = foundation_sort_people();
		NSSortDescriptor *byBlock = [[NSSortDescriptor alloc] initWithKey:@"rank"
									  ascending:NO
									 comparator:^NSComparisonResult(id l, id r) {
			return [l compare:r];
		}];
		NSArray *sorted = (people != nil && byBlock != nil)
			? [people sortedArrayUsingDescriptors:@[byBlock]] : nil;
		NSString *ranks = sorted != nil ? fn_ranks(sorted) : nil;

		check("sort-comparator",
		      sorted != nil && [ranks isEqualToString:@"4321"] &&
		      [byBlock comparator] != nil,
		      ranks == nil ? "(nil)" : [ranks UTF8String]);
	}

	{
		NSArray *people = foundation_sort_people();
		int calls = 0;
		char detail[80];
		NSArray *sorted = people != nil
			? [people sortedArrayUsingFunction:fn_rank_comparator context:&calls] : nil;
		NSString *ranks = sorted != nil ? fn_ranks(sorted) : nil;

		snprintf(detail, sizeof detail, "ranks=%s comparator calls=%d",
			 ranks == nil ? "(nil)" : [ranks UTF8String], calls);
		check("sort-function",
		      sorted != nil && [ranks isEqualToString:@"1234"] && calls > 0,
		      detail);
	}

	{
		NSArray *people = foundation_sort_people();
		NSMutableArray *mutable = [[NSMutableArray alloc] init];
		NSSortDescriptor *byRank = [[NSSortDescriptor alloc] initWithKey:@"rank" ascending:YES];
		int calls = 0;
		BOOL ok = NO;

		if (people != nil) {
			[mutable addObjectsFromArray:people];
			[mutable sortUsingDescriptors:@[byRank]];
			ok = [fn_ranks(mutable) isEqualToString:@"1234"];
			[mutable sortUsingFunction:fn_rank_comparator context:&calls];
			ok = ok && calls > 0;
		}
		check("sort-mutable", ok, fn_why(mutable));
	}

	{
		NSArray *people = foundation_sort_people();
	covers("NSMutableArray", "sortUsingDescriptors:");
		NSArray *solo = foundation_sort_nil_value();
		NSSortDescriptor *byNote = [NSSortDescriptor sortDescriptorWithKey:@"note" ascending:YES];
		int fourRaised = 0;
		int oneRaised = 0;

		if (people != nil && solo != nil && byNote != nil) {
			@try {
				(void)[people sortedArrayUsingDescriptors:@[byNote]];
			} @catch (NSException *e) {
				(void)e;
				fourRaised = 1;
			}
			@try {
				(void)[solo sortedArrayUsingDescriptors:@[byNote]];
			} @catch (NSException *e) {
				(void)e;
				oneRaised = 1;
			}
		}
		/* The PAIR is the check: four objects with nil values raise, and the one-element
		 * array — which has nothing to compare — does not. "It raised" alone would pass for
		 * a throw from anywhere else, and "it did not raise" alone for a missing rule. */
		check("sort-nil-value",
		      fourRaised == 1 && oneRaised == 0,
		      fourRaised ? "four objects raised, but the single element did too"
		      : "a nil value was ordered silently");
	}

	{
		/* THE CODER FORMS ARRIVED IN §63.17, so the two NEGATIVE assertions that used to stand here are
		 * INVERTED rather than deleted. A check that a class does NOT respond to a door it now implements is
		 * the bug class §11.2's source 1 hid for months ("a probe asserting an ABSENCE asserts a fact about
		 * the tree, and landing the code does not update it") — this check is the SECOND time this thread has
		 * met it, and the assertion is now the positive one it should have become.
		 *
		 * THE EVALUATION SANDBOX IS STILL ABSENT and is still asserted so: `-allowEvaluation` and
		 * `-isEvaluationAllowed` are a different question from coding, and that half is unchanged. */
		check("sort-refusals",
		      ![NSSortDescriptor instancesRespondToSelector:
			sel_registerName("allowEvaluation")] &&
		      ![NSSortDescriptor instancesRespondToSelector:
			sel_registerName("isEvaluationAllowed")] &&
		      [NSSortDescriptor instancesRespondToSelector:
			sel_registerName("initWithCoder:")] &&
		      [NSSortDescriptor instancesRespondToSelector:
			sel_registerName("encodeWithCoder:")] &&
		      [NSSortDescriptor instancesRespondToSelector:
			sel_registerName("compareObject:toObject:")] &&
		      [NSSortDescriptor instancesRespondToSelector:
			sel_registerName("reversedSortDescriptor")],
		      "the evaluation sandbox is still absent; the CODER forms are PRESENT since §63.17 (this half is INVERTED from the version that landed with the class)");
	}

	{
		NSArray *people = foundation_sort_people();
		NSSortDescriptor *theirs = foundation_sort_by_name();
		NSArray *sorted = (people != nil && theirs != nil)
			? [people sortedArrayUsingDescriptors:@[theirs]] : nil;
		NSString *names = sorted != nil ? fn_names(sorted) : nil;

		check("cross-tu",
		      sorted != nil && [names isEqualToString:@"annannbobcid"],
		      names == nil ? "(nil)" : [names UTF8String]);
	}

	{
		/* THE DESCRIPTOR'S NSCoding DOORS (§63.17), over the REAL archive path. TWO descriptors round-trip —
		 * with and without a selector — and the COMPARATOR one is REFUSED, because its fourth field is a BLOCK
		 * and a block cannot cross an archive. The refusal is asserted rather than assumed: the alternative
		 * (writing the other three fields) would produce a descriptor that came back with no rule at all,
		 * which is a silently wrong ORDER.
		 *
		 * AND THE SELECTOR IS THE POINT OF THE SECOND ROUND TRIP: it is written as its NAME and read back
		 * through `NSSelectorFromString`, so the assertion is that the restored descriptor's selector is the
		 * SAME selector — a comparison of SELs, not of strings. */
		NSSortDescriptor *plain = [NSSortDescriptor sortDescriptorWithKey:@"age" ascending:YES];
		NSSortDescriptor *withSelector =
			[NSSortDescriptor sortDescriptorWithKey:@"name"
						    ascending:NO
						     selector:@selector(compare:)];
		NSSortDescriptor *withComparator =
			[NSSortDescriptor sortDescriptorWithKey:@"name"
						    ascending:NO
						   comparator:^NSComparisonResult(id a, id b) {
			return NSOrderedSame;
		}];
		NSData *plainData = [NSKeyedArchiver archivedDataWithRootObject:plain];
		NSData *selectorData = [NSKeyedArchiver archivedDataWithRootObject:withSelector];
		NSSortDescriptor *backPlain = plainData != nil
			? [NSKeyedUnarchiver unarchiveObjectWithData:plainData] : nil;
		NSSortDescriptor *backSelector = selectorData != nil
			? [NSKeyedUnarchiver unarchiveObjectWithData:selectorData] : nil;
		BOOL refusedComparator = NO;

		@try {
			(void)[NSKeyedArchiver archivedDataWithRootObject:withComparator];
		} @catch (NSException *e) {
			refusedComparator = [[e name] isEqualToString:NSInvalidArgumentException];
		}
		check("sortdescriptor-nscoding-round-trip",
		      [plain conformsToProtocol:@protocol(NSCoding)] &&
		      backPlain != nil && [[backPlain key] isEqualToString:@"age"] &&
		      [backPlain ascending] && [backPlain selector] == NULL &&
		      backSelector != nil && [[backSelector key] isEqualToString:@"name"] &&
		      ![backSelector ascending] &&
		      /* THE NAME IS THE CONTRACT, not the pointer: see the note in the diagnostic below. */
		      backSelector != nil && [backSelector selector] != NULL &&
		      strcmp(sel_getName([backSelector selector]), "compare:") == 0 &&
		      refusedComparator,
		      fn_sort_coding_detail(plain, backPlain, withSelector, backSelector,
					    refusedComparator));
	}

	printf("FOUNDATION-SORT RESULT ok=%d fail=%d\n", okc, failc);
	/* The exit status, in the probe's OWN output. After a probe the console can stop
	 * serving INPUT for a while (the tier residual), so an `echo $?` that the harness
	 * types may never run. The probe knows exactly what it returns, so it says so - and
	 * this is the same value: failc ? 1 : 0 is the return statement below. */
	printf("FOUNDATION-SORT-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-SORT DONE\n");
	return failc ? 1 : 0;
}
