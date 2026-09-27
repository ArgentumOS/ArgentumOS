/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_presentationintent — §62.65's acceptance: NSPresentationIntent, the last row of "Strings with
 * Metadata".
 *
 * ONE unit, importing only <Foundation/Foundation.h>.
 *
 * WHAT IS ASSERTED IS THE TWO THINGS THIS CLASS ANSWERS THAT ITS FIELDS DO NOT, because those are the two a
 * caller cannot compute from the object's own state:
 *
 *   * THE INDENTATION OF A NESTED LIST, which is read off the parent CHAIN. Apple's sentence has three clauses
 *     and the third is the one that decides the implementation — "all elements within the same list have the
 *     same indentation level" — so the check builds the document the sentence describes (a list, an item in it,
 *     a list nested inside that item, an item in THAT) and requires 0, 0, 1, 1.
 *   * EQUIVALENCE, which compares ATTRIBUTES AND NOT IDENTITY. The check that matters is the pair: two intents
 *     differing only in identity are equivalent, and two differing in a field are not — a method that answered
 *     YES to everything, or NO to everything, fails one of the two.
 *
 * AND THE TWELVE FACTORIES ARE CHECKED IN ONE PASS, because what distinguishes them is exactly what they set:
 * a kind, and that kind's fields.
 */

#import <Foundation/Foundation.h>

#include <stdio.h>

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-PRESENTATIONINTENT %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-PRESENTATIONINTENT %s FAIL %s\n", name,
		       detail != nil ? [detail UTF8String] : "");
	}
}

int main(void)
{
	{
		/* TWELVE FACTORIES, TWELVE KINDS — the whole reason the class has no -init. */
		NSArray *intents = @[
			[NSPresentationIntent paragraphIntentWithIdentity:1 nestedInsideIntent:nil],
			[NSPresentationIntent headerIntentWithIdentity:2 level:3 nestedInsideIntent:nil],
			[NSPresentationIntent orderedListIntentWithIdentity:3 nestedInsideIntent:nil],
			[NSPresentationIntent unorderedListIntentWithIdentity:4 nestedInsideIntent:nil],
			[NSPresentationIntent listItemIntentWithIdentity:5 ordinal:2 nestedInsideIntent:nil],
			[NSPresentationIntent codeBlockIntentWithIdentity:6 languageHint:@"c" nestedInsideIntent:nil],
			[NSPresentationIntent blockQuoteIntentWithIdentity:7 nestedInsideIntent:nil],
			[NSPresentationIntent thematicBreakIntentWithIdentity:8 nestedInsideIntent:nil],
			[NSPresentationIntent tableIntentWithIdentity:9 columnCount:2 alignments:nil nestedInsideIntent:nil],
			[NSPresentationIntent tableHeaderRowIntentWithIdentity:10 nestedInsideIntent:nil],
			[NSPresentationIntent tableRowIntentWithIdentity:11 row:4 nestedInsideIntent:nil],
			[NSPresentationIntent tableCellIntentWithIdentity:12 column:1 nestedInsideIntent:nil]
		];
		NSArray *kinds = @[ @(NSPresentationIntentKindParagraph), @(NSPresentationIntentKindHeader),
				    @(NSPresentationIntentKindOrderedList),
				    @(NSPresentationIntentKindUnorderedList),
				    @(NSPresentationIntentKindListItem),
				    @(NSPresentationIntentKindCodeBlock),
				    @(NSPresentationIntentKindBlockQuote),
				    @(NSPresentationIntentKindThematicBreak),
				    @(NSPresentationIntentKindTable),
				    @(NSPresentationIntentKindTableHeaderRow),
				    @(NSPresentationIntentKindTableRow),
				    @(NSPresentationIntentKindTableCell) ];
		BOOL all = YES;
		NSUInteger i;

		for (i = 0; i < [intents count]; i++) {
			NSPresentationIntent *intent = [intents objectAtIndex:i];

			if (intent == nil || [intent intentKind] != (NSPresentationIntentKind)
			    [[kinds objectAtIndex:i] integerValue] ||
			    [intent identity] != (NSInteger)(i + 1)) {
				all = NO;
			}
		}
		check("each-factory-answers-its-kind-and-identity", all,
		      [NSString stringWithFormat:@"%lu intents built", (unsigned long)[intents count]]);
	}

	{
		/* THE KIND-SPECIFIC FIELDS, each from the factory that owns it. */
		NSPresentationIntent *header = [NSPresentationIntent headerIntentWithIdentity:1
										level:2
								   nestedInsideIntent:nil];
		NSPresentationIntent *item = [NSPresentationIntent listItemIntentWithIdentity:2
										ordinal:7
								   nestedInsideIntent:nil];
		NSPresentationIntent *row = [NSPresentationIntent tableRowIntentWithIdentity:3
										  row:4
								 nestedInsideIntent:nil];
		NSPresentationIntent *cell = [NSPresentationIntent tableCellIntentWithIdentity:4
										column:5
								   nestedInsideIntent:nil];
		NSPresentationIntent *table = [NSPresentationIntent tableIntentWithIdentity:5
										columnCount:2
										 alignments:@[ @(NSPresentationIntentTableColumnAlignmentLeft),
											       @(NSPresentationIntentTableColumnAlignmentRight) ]
								   nestedInsideIntent:nil];
		NSPresentationIntent *code = [NSPresentationIntent codeBlockIntentWithIdentity:6
									  languageHint:@"sh"
								    nestedInsideIntent:nil];

		check("each-kind-carries-the-field-its-factory-set",
		      [header headerLevel] == 2 && [item ordinal] == 7 && [row row] == 4 &&
		      [cell column] == 5 && [table columnCount] == 2 &&
		      [[table columnAlignments] count] == 2 && [[code languageHint] isEqualToString:@"sh"],
		      [NSString stringWithFormat:@"level=%ld ordinal=%ld row=%ld column=%ld columns=%ld",
			(long)[header headerLevel], (long)[item ordinal], (long)[row row],
			(long)[cell column], (long)[table columnCount]]);
	}

	{
		/* THE PARENT LINK IS THE CHAIN, and the top of a document has none. */
		NSPresentationIntent *list = [NSPresentationIntent unorderedListIntentWithIdentity:1
									     nestedInsideIntent:nil];
		NSPresentationIntent *item = [NSPresentationIntent listItemIntentWithIdentity:2
										ordinal:1
								   nestedInsideIntent:list];

		check("the-parent-link-is-the-chain",
		      [list parentIntent] == nil && [item parentIntent] == list,
		      [NSString stringWithFormat:@"top=%@ child=%@", [list parentIntent], [item parentIntent]]);
	}

	{
		/* APPLE'S OWN EXAMPLE, CLAUSE BY CLAUSE: the initial list is 0, its item is 0 (the clause that decides
		 * the implementation), a list nested inside that item is 1, and its item is 1. */
		NSPresentationIntent *list0 = [NSPresentationIntent unorderedListIntentWithIdentity:1
									      nestedInsideIntent:nil];
		NSPresentationIntent *item0 = [NSPresentationIntent listItemIntentWithIdentity:2
										 ordinal:1
								    nestedInsideIntent:list0];
		NSPresentationIntent *list1 = [NSPresentationIntent unorderedListIntentWithIdentity:3
									      nestedInsideIntent:item0];
		NSPresentationIntent *item1 = [NSPresentationIntent listItemIntentWithIdentity:4
										 ordinal:1
								    nestedInsideIntent:list1];

		check("an-indentation-level-counts-nested-lists",
		      [list0 indentationLevel] == 0 && [item0 indentationLevel] == 0 &&
		      [list1 indentationLevel] == 1 && [item1 indentationLevel] == 1,
		      [NSString stringWithFormat:@"list0=%ld item0=%ld list1=%ld item1=%ld (want 0 0 1 1)",
			(long)[list0 indentationLevel], (long)[item0 indentationLevel],
			(long)[list1 indentationLevel], (long)[item1 indentationLevel]]);
	}

	{
		/* A FIELD THE KIND DOES NOT CARRY ANSWERS ZERO (ours: Apple publishes an absence rule for exactly one
		 * of them), and the one Apple DOES publish — a non-table's alignments — answers nil. */
		NSPresentationIntent *paragraph = [NSPresentationIntent paragraphIntentWithIdentity:1
										       nestedInsideIntent:nil];

		check("a-field-the-kind-does-not-carry-answers-zero-and-alignments-are-nil",
		      [paragraph headerLevel] == 0 && [paragraph ordinal] == 0 && [paragraph row] == 0 &&
		      [paragraph column] == 0 && [paragraph columnCount] == 0 &&
		      [paragraph languageHint] == nil && [paragraph columnAlignments] == nil,
		      [NSString stringWithFormat:@"level=%ld alignments=%@ language=%@",
			(long)[paragraph headerLevel], [paragraph columnAlignments],
			[paragraph languageHint]]);
	}

	{
		/* EQUIVALENCE, THE PAIR THAT MATTERS: identity is IGNORED (Apple's own words) and a field is NOT. */
		NSPresentationIntent *one = [NSPresentationIntent headerIntentWithIdentity:1
									     level:2
									nestedInsideIntent:nil];
		NSPresentationIntent *sameButForIdentity = [NSPresentationIntent headerIntentWithIdentity:99
												   level:2
											  nestedInsideIntent:nil];
		NSPresentationIntent *differentLevel = [NSPresentationIntent headerIntentWithIdentity:1
											       level:3
										  nestedInsideIntent:nil];

		check("equivalence-ignores-identity-and-notices-a-field",
		      [one isEquivalentToPresentationIntent:sameButForIdentity] &&
		      ![one isEquivalentToPresentationIntent:differentLevel],
		      [NSString stringWithFormat:@"sameIdentityIgnored=%d differentLevelEquivalent=%d",
			(int)[one isEquivalentToPresentationIntent:sameButForIdentity],
			(int)[one isEquivalentToPresentationIntent:differentLevel]]);
	}

	{
		/* AND IT DOES NOT LOOK AT THE PARENT EITHER, which is OUR reading of "their attributes": two intents
		 * that differ only in where they hang are the case the method exists for. */
		NSPresentationIntent *parentA = [NSPresentationIntent blockQuoteIntentWithIdentity:1
										nestedInsideIntent:nil];
		NSPresentationIntent *parentB = [NSPresentationIntent blockQuoteIntentWithIdentity:2
										nestedInsideIntent:nil];
		NSPresentationIntent *inA = [NSPresentationIntent paragraphIntentWithIdentity:3
									       nestedInsideIntent:parentA];
		NSPresentationIntent *inB = [NSPresentationIntent paragraphIntentWithIdentity:4
									       nestedInsideIntent:parentB];

		check("equivalence-does-not-look-at-the-parent",
		      [inA isEquivalentToPresentationIntent:inB],
		      @"two paragraphs with different parents compare equivalent");
	}

	{
		/* THE LANGUAGE HINT IS A SNAPSHOT, not the caller's string: a MUTABLE source is mutated after being
		 * handed over, so an assigning factory would answer the mutated string. */
		NSMutableString *hint = [[NSMutableString alloc] initWithString:@"c"];
		NSPresentationIntent *code = [NSPresentationIntent codeBlockIntentWithIdentity:1
									  languageHint:hint
								    nestedInsideIntent:nil];

		[hint appendString:@"-mutated"];
		check("the-language-hint-is-a-snapshot",
		      [[code languageHint] isEqualToString:@"c"],
		      [NSString stringWithFormat:@"languageHint=%@ (want c)", [code languageHint]]);
	}

	{
		/* A TABLE KNOWS ITS COLUMNS AND A NON-TABLE DOES NOT — the one absence rule Apple publishes, checked
		 * from both sides so "always nil" cannot pass it. */
		NSPresentationIntent *table = [NSPresentationIntent tableIntentWithIdentity:1
										columnCount:1
										 alignments:@[ @(NSPresentationIntentTableColumnAlignmentCenter) ]
								   nestedInsideIntent:nil];
		NSPresentationIntent *row = [NSPresentationIntent tableRowIntentWithIdentity:2
										  row:0
								 nestedInsideIntent:table];

		check("only-a-table-carries-column-alignments",
		      [[table columnAlignments] count] == 1 && [row columnAlignments] == nil,
		      [NSString stringWithFormat:@"table=%@ row=%@", [table columnAlignments],
			[row columnAlignments]]);
	}

	printf("FOUNDATION-PRESENTATIONINTENT RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-PRESENTATIONINTENT DONE\n");
	return failc == 0 ? 0 : 1;
}
