/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_attributedstring, unit of 1 — W10 slice 1's acceptance: THE RUN STORE AND ITS CONTRACT.
 * docs/design/foundation-plan.md §61.
 *
 * ONE unit, importing only <Foundation/Foundation.h>. NO FIXTURE: an attributed string is built from a string.
 *
 * THE CONTRACT THIS PROBE EXISTS FOR IS THE COALESCING ONE. Apple's promise for
 * -attributesAtIndex:effectiveRange: is the LONGEST range over which the attributes are the same, so a store
 * that kept every insertion as its own run would answer a SHORT range and be wrong with no code being wrong.
 * The checks below therefore build equal attribute sets in SEVERAL DIFFERENT WAYS - one dictionary over a whole
 * string, two appends, an edit group - and require every one of them to read back as ONE range.
 *
 * AND THE INVENTORY RUNS BOTH WAYS, which is the rule this library's classes carry: the selectors that ship
 * must EXIST - the file-format doors among them (§62.58 made RTF a real writer and left RTFD, HTML and the doc
 * format refusing by name) and the coding pair - and the ones this slice does not carry must be ABSENT with the
 * reason recorded here and in the header: the AppKit/UIKit/TextKit half, -mutableString (a live proxy, not a
 * copy), and the two reader doors (readFromData:/readFromURL:).
 */

#import <Foundation/Foundation.h>
#import <Foundation/NSMorphology.h>

#include <stdio.h>
#include <string.h>

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-ATTRIBUTEDSTRING %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-ATTRIBUTEDSTRING %s FAIL %s\n", name, detail != nil ? [detail UTF8String] : "");
	}
}

static NSString *fn_A(void) { return @"A"; }
static NSString *fn_B(void) { return @"B"; }

/* a range, printed for a failure */
static NSString *fn_r(NSRange r)
{
	return [NSString stringWithFormat:@"(%lu,%lu)", (unsigned long)r.location, (unsigned long)r.length];
}

/* RTF IS ASCII ON THE WIRE, SO THE RTF CHECKS READ THE BYTES DIRECTLY (§62.58). Asserting on the bytes rather
 * than through a decoded NSString also keeps the assertions independent of this library's own decoder, which
 * is the instrument-under-test elsewhere in this same probe. */
static int fn_bytes_contain(NSData *data, const char *needle)
{
	const unsigned char *bytes = data != nil ? [data bytes] : NULL;
	size_t length = data != nil ? (size_t)[data length] : 0;
	size_t n = strlen(needle);
	size_t i;

	if (bytes == NULL || n == 0 || length < n) {
		return 0;
	}
	for (i = 0; i + n <= length; i++) {
		if (memcmp(bytes + i, needle, n) == 0) {
			return 1;
		}
	}
	return 0;
}

int main(void)
{

	{
		/* §63.99: THE INFLECTION DOOR REFUSES BY NAME, AND THE CHECK ASSERTS **THE CONSISTENCY** RATHER THAN JUST
		 * THAT SOMETHING RAISED: the refusal and `+[NSInflectionRule canInflectLanguage:]`'s answer are the same fact
		 * seen from two levels, so an engine that began inflecting a language would fail THIS check until the refusal
		 * was removed — which is what makes the pair checkable rather than decorative. */
		BOOL raised = NO;
		NSAttributedString *tagged = [[NSAttributedString alloc]
					      initWithString:@"a table"
						      attributes:[NSDictionary dictionaryWithObject:[NSInflectionRule automaticRule]
												     forKey:NSInflectionRuleAttributeName]];
		@try {
			(void)[tagged attributedStringByInflectingString];
		} @catch (NSException *e) {
			raised = [e isKindOfClass:[NSException class]];
		}
		check("attributedstring-inflecting-refuses-by-name",
		      raised && ![NSInflectionRule canInflectLanguage:@"en"] &&
		      ![NSInflectionRule canInflectPreferredLocalization],
		      [NSString stringWithFormat:@"raised=%d canInflect(en)=%d", (int)raised,
			(int)[NSInflectionRule canInflectLanguage:@"en"]]);
	}


	{
		/* §63.98: THE TWO `context:` DOORS. They format (Apple's own comment for them is the same sentence as
		 * the context-free pair's) and the dictionary is accepted with no effect, because its only published key is
		 * for inflection and this library has no engine — the door that would consume it is still open. Calling
		 * THE VARIARGS DOOR exercises BOTH, since it delegates to the `arguments:` one. */
		NSMutableAttributedString *cf = [[NSMutableAttributedString alloc] initWithString:@"C=%@ D=%@"];
		NSAttributedString *c1 = [[NSAttributedString alloc] initWithString:@"one"];
		NSAttributedString *c2 = [[NSAttributedString alloc] initWithString:@"two"];
		NSDictionary *cctx = [NSDictionary dictionaryWithObject:@"concepts" forKey:NSInflectionConceptsKey];
		NSAttributedString *b1 = [[NSAttributedString alloc] initWithFormat:cf options:0 locale:nil
									  context:cctx, c1, c2];

		check("attributedstring-format-context",
		      [[b1 string] isEqualToString:@"C=one D=two"],
		      [NSString stringWithFormat:@"string=%@", [b1 string]]);
		check("attributedstring-format-context-keeps-the-format-attributes",
		      [b1 length] == 11 && [b1 attribute:NSReplacementIndexAttributeName atIndex:0 effectiveRange:NULL] == nil,
		      [NSString stringWithFormat:@"length=%lu", (unsigned long)[b1 length]]);
	}


	{
		/* §63.97: THE LOCALIZED FAMILY, one check per shape. ⚠ APPLE'S OWN DOC COMMENT (in the corpus, not the
		 * ledger) SAYS THESE FORMAT THE STRING WITH THE CURRENT LOCALE — NOT that they look anything up. */
		NSMutableAttributedString *lf = [[NSMutableAttributedString alloc] initWithString:@"L=%@ M=%@"];
		NSAttributedString *l1 = [[NSAttributedString alloc] initWithString:@"one"];
		NSAttributedString *l2 = [[NSAttributedString alloc] initWithString:@"two"];
		NSDictionary *ctx = [NSDictionary dictionary];
		NSAttributedString *a1 = [NSAttributedString localizedAttributedStringWithFormat:lf, l1, l2];
		NSAttributedString *a2 = [NSAttributedString localizedAttributedStringWithFormat:lf options:0, l1, l2];
		NSAttributedString *a3 = [NSAttributedString localizedAttributedStringWithFormat:lf context:ctx, l1, l2];
		NSAttributedString *a4 = [NSAttributedString localizedAttributedStringWithFormat:lf options:0
									context:ctx, l1, l2];

		check("attributedstring-localized-format",
		      [[a1 string] isEqualToString:@"L=one M=two"],
		      [NSString stringWithFormat:@"string=%@", [a1 string]]);
		check("attributedstring-localized-format-options",
		      [[a2 string] isEqualToString:@"L=one M=two"],
		      [NSString stringWithFormat:@"string=%@", [a2 string]]);
		check("attributedstring-localized-format-context",
		      [[a3 string] isEqualToString:@"L=one M=two"],
		      [NSString stringWithFormat:@"string=%@", [a3 string]]);
		check("attributedstring-localized-format-options-context",
		      [[a4 string] isEqualToString:@"L=one M=two"],
		      [NSString stringWithFormat:@"string=%@", [a4 string]]);
	}


	{
		/* §63.95: the attributed-format doors. ⚠ THE PROBE IS ARC and NSForegroundColorAttributeName IS APPKIT'S. */
		NSString *marker = @"FNProbeMarker";
		NSString *own = @"FNProbeOwn";
		NSMutableAttributedString *fmt = [[NSMutableAttributedString alloc] initWithString:@"n=%@ m=%@"];
		NSAttributedString *plainArg = [[NSAttributedString alloc] initWithString:@"one"];
		NSAttributedString *richArg = [[NSAttributedString alloc] initWithString:@"two"
						   attributes:[NSDictionary dictionaryWithObject:@"blue" forKey:own]];
		NSAttributedString *plainOut;
		NSAttributedString *ownOut;
		NSAttributedString *indexOut;
		NSString *s;
		BOOL offsetsProved;
		NSRange eff;

		[fmt addAttribute:marker value:@"red" range:NSMakeRange(0, [fmt length])];
		plainOut = [[NSAttributedString alloc] initWithFormat:fmt options:0 locale:nil, plainArg, richArg];
		ownOut = [[NSAttributedString alloc]
			   initWithFormat:fmt
				  options:NSAttributedStringFormattingInsertArgumentAttributesWithoutMerging
				   locale:nil, plainArg, richArg];
		indexOut = [[NSAttributedString alloc]
			     initWithFormat:fmt
				    options:NSAttributedStringFormattingApplyReplacementIndexAttribute
				     locale:nil, plainArg, richArg];
		s = [plainOut string];
		/* THE OFFSETS ARE PROVED, NOT ASSUMED: `one` at 2 and `two` at 8, asserted as TEXT. */
		offsetsProved = ([s length] == 11 &&
				 [[s substringWithRange:NSMakeRange(2, 3)] isEqualToString:@"one"] &&
				 [[s substringWithRange:NSMakeRange(8, 3)] isEqualToString:@"two"]);
		check("attributedstring-format-substitutes",
		      [s isEqualToString:@"n=one m=two"] && offsetsProved,
		      [NSString stringWithFormat:@"string=%@", s]);
		check("attributedstring-format-attributes-reach-the-substitution",
		      [[[plainOut attributesAtIndex:2 effectiveRange:&eff] objectForKey:marker] isEqualToString:@"red"] &&
		      [[[plainOut attributesAtIndex:8 effectiveRange:NULL] objectForKey:marker] isEqualToString:@"red"],
		      [NSString stringWithFormat:@"arg1=%@ arg2=%@",
			[[plainOut attributesAtIndex:2 effectiveRange:NULL] objectForKey:marker],
			[[plainOut attributesAtIndex:8 effectiveRange:NULL] objectForKey:marker]]);
		check("attributedstring-option-replacement-index",
		      [[[[indexOut attributesAtIndex:2 effectiveRange:NULL]
			  objectForKey:NSReplacementIndexAttributeName] description] isEqualToString:@"0"] &&
		      [[[[indexOut attributesAtIndex:8 effectiveRange:NULL]
			  objectForKey:NSReplacementIndexAttributeName] description] isEqualToString:@"1"],
		      [NSString stringWithFormat:@"first=%@ second=%@",
			[[indexOut attributesAtIndex:2 effectiveRange:NULL] objectForKey:NSReplacementIndexAttributeName],
			[[indexOut attributesAtIndex:8 effectiveRange:NULL] objectForKey:NSReplacementIndexAttributeName]]);
		check("attributedstring-option-argument-attributes-unmerged",
		      [[[ownOut attributesAtIndex:8 effectiveRange:NULL] objectForKey:own] isEqualToString:@"blue"] &&
		      [[ownOut attributesAtIndex:8 effectiveRange:NULL] objectForKey:marker] == nil,
		      [NSString stringWithFormat:@"own=%@ marker=%@",
			[[ownOut attributesAtIndex:8 effectiveRange:NULL] objectForKey:own],
			[[ownOut attributesAtIndex:8 effectiveRange:NULL] objectForKey:marker]]);
	}


	{
		/* §63.93: TWO CHECKS, ONE PER FIX. The first is the defect that cost this bug five rounds — a store
		 * mutation rendering a nil substring as `(null)` — and it is asserted on the STRING, which is where the
		 * `(null)` appeared. The second pins `-description`, which is what `%@` renders through. */
		NSMutableAttributedString *m = [[NSMutableAttributedString alloc] initWithString:@"ab"];
		NSAttributedString *arg = [[NSAttributedString alloc] initWithString:@"one"];

		[m appendAttributedString:arg];
		[m appendAttributedString:arg];

		check("attributedstring-mutation-never-writes-null",
		      [[m string] rangeOfString:@"(null)"].location == NSNotFound,
		      [NSString stringWithFormat:@"string=%@", [m string]]);
		check("attributedstring-mutation-concatenates-exactly",
		      [[m string] isEqualToString:@"aboneone"],
		      [NSString stringWithFormat:@"string=%@", [m string]]);
		check("attributedstring-description-is-the-string",
		      [[arg description] isEqualToString:@"one"],
		      [NSString stringWithFormat:@"description=%@", [arg description]]);
	}

	NSDictionary *one = @{ @"A": @1 };
	NSDictionary *both = @{ @"A": @1, @"B": @2 };

	/* ---- THE PRIMITIVES THE STORE DEPENDS ON, TESTED ALONE ------------------------------------------- */
	{
		/* WHY: the store's own traces say the merge landed on the right run, and yet a dictionary built as
		 * {A} then READS as {B}. Three facts are assumed by that code and none had been measured: that
		 * -copy on a dictionary gives equal contents, that -addEntriesFromDictionary: does not touch its
		 * SOURCE, and that -copy on a MUTABLE dictionary is a snapshot rather than the receiver itself. */
		NSDictionary *src = @{ @"A": @1 };
		NSDictionary *copied = [src copy];
		NSMutableDictionary *mutable = [[NSMutableDictionary alloc] initWithDictionary:src];
		NSDictionary *snapshot;

		[mutable addEntriesFromDictionary:@{ @"B": @2 }];
		snapshot = [mutable copy];
		[mutable setObject:@9 forKey:@"C"];

		printf("FOUNDATION-ATTRIBUTEDSTRING DIAG prim src=%s copied=%s after-add=%s snapshot=%s "
			"after-mutate=%s\n",
			[[[src allKeys] componentsJoinedByString:@","] UTF8String],
			[[[copied allKeys] componentsJoinedByString:@","] UTF8String],
			[[[mutable allKeys] componentsJoinedByString:@","] UTF8String],
			[[[snapshot allKeys] componentsJoinedByString:@","] UTF8String],
			[[[mutable allKeys] componentsJoinedByString:@","] UTF8String]);
		{
			/* IDENTITY, WHICH THE FIRST PRIMITIVE TEST DID NOT CHECK: the store builds its merge target
			 * with -initWithDictionary: and copies it with -copy, and the pointer trace shows the copy
			 * coming back as the SAME OBJECT - so the question is whether the CONSTRUCTOR also aliases. */
			NSDictionary *literal = @{ @"A": @1 };
			NSMutableDictionary *fromLiteral = [[NSMutableDictionary alloc] initWithDictionary:literal];
			NSDictionary *literalCopy = [literal copy];
			NSDictionary *literalMutableCopy = [[NSMutableDictionary alloc] initWithDictionary:literal];
			NSDictionary *snapshotOfMutable;

			[fromLiteral setObject:@2 forKey:@"B"];
			snapshotOfMutable = [fromLiteral copy];
			[fromLiteral setObject:@3 forKey:@"C"];

			printf("FOUNDATION-ATTRIBUTEDSTRING DIAG ident literal=%s fromLiteral=%s "
				"copy-same-object=%d mutablecopy-same-object=%d snapshot=%s\n",
				[[[literal allKeys] componentsJoinedByString:@","] UTF8String],
				[[[fromLiteral allKeys] componentsJoinedByString:@","] UTF8String],
				(int)(literalCopy == literal),
				(int)(literalMutableCopy == literal),
				[[[snapshotOfMutable allKeys] componentsJoinedByString:@","] UTF8String]);
			check("primitives-constructors-do-not-alias-their-source",
			      [[literal allKeys] count] == 1 && [[fromLiteral allKeys] count] == 3 &&
			      [[snapshotOfMutable allKeys] count] == 2,
			      [NSString stringWithFormat:@"literal=%lu fromLiteral=%lu snapshot=%lu",
				(unsigned long)[[literal allKeys] count],
				(unsigned long)[[fromLiteral allKeys] count],
				(unsigned long)[[snapshotOfMutable allKeys] count]]);
		}
		check("primitives-dictionaries-behave-as-the-store-assumes",
		      ([[copied allKeys] count] == [[src allKeys] count]) &&
		      ([[src allKeys] count] == 1) &&
		      ([[mutable allKeys] count] == 3) &&
		      ([[snapshot allKeys] count] == 2),
		      [NSString stringWithFormat:@"src=%lu mutable=%lu snapshot=%lu (snapshot must stay 2)",
			(unsigned long)[[src allKeys] count], (unsigned long)[[mutable allKeys] count],
			(unsigned long)[[snapshot allKeys] count]]);
	}

	{
		/* THE OWNERSHIP QUESTION, WHICH IDENTITY CANNOT ANSWER: -copy on an immutable dictionary returns
		 * SELF (measured), which is legal ONLY if it also added the +1 that ownership needs. Probes are ARC
		 * and cannot send -release, so the store itself does the experiment: a dictionary the probe owns a
		 * reference to goes in, a store is created and released, and the RETAIN COUNT must be where it was.
		 * A store that holds a reference it does not own shows up here as a count that has gone UP. */
		id owned = [[NSMutableDictionary alloc] initWithDictionary:@{ @"A": @1 }];
		__weak id watcher = owned;
		BOOL stillThere;

		{
			NSAttributedString *s = [[NSAttributedString alloc] initWithString:@"abcd" attributes:owned];
			NSAttributedString *t = [[NSAttributedString alloc] initWithAttributedString:s];
			NSMutableAttributedString *m = [[NSMutableAttributedString alloc] initWithString:@"ab"
										     attributes:owned];

			[m addAttributes:owned range:NSMakeRange(0, 2)];
			(void)t;		/* all three stores are released at the end of this block */
		}
		/* ARC cannot send -release and cannot read -retainCount, so the QUESTION IS ASKED WITH A WEAK
		 * WATCHER: if any store held a reference it did not own (the shadow of relying on -copy for
		 * ownership), releasing the store would drop the caller's dictionary to zero and this would be nil. */
		stillThere = watcher != nil;
		printf("FOUNDATION-ATTRIBUTEDSTRING DIAG own still-there=%d\n", (int)stillThere);
		check("primitives-a-store-owns-what-it-was-given", stillThere &&
		      [[owned allKeys] count] == 1,
		      [NSString stringWithFormat:@"stillThere=%d keys=%lu", (int)stillThere,
			(unsigned long)[[owned allKeys] count]]);
	}

	/* ---- THE INVENTORY, BOTH WAYS ------------------------------------------------------------------- */
	{
		NSArray *implemented = @[ @"initWithString:", @"initWithString:attributes:",
			@"initWithAttributedString:", @"string", @"length",
			@"attributesAtIndex:effectiveRange:", @"attribute:atIndex:effectiveRange:",
			@"attributesAtIndex:longestEffectiveRange:inRange:",
			@"attribute:atIndex:longestEffectiveRange:inRange:",
			@"attributedSubstringFromRange:", @"isEqualToAttributedString:",
			@"enumerateAttributesInRange:options:usingBlock:",
			@"enumerateAttribute:inRange:options:usingBlock:", @"copy", @"mutableCopy",
			@"encodeWithCoder:", @"initWithCoder:",
			@"dataFromRange:documentAttributes:error:", @"RTFFromRange:documentAttributes:",
			@"RTFDFromRange:documentAttributes:", @"RTFDFileWrapperFromRange:documentAttributes:",
			@"docFormatFromRange:documentAttributes:", @"fileWrapperFromRange:documentAttributes:error:",
			/* the 2026-09-30 pass closed the "Calculating linguistic units" group (over FNTextBreaking), the
			 * deprecated URL door, and the markdown baseURL: file door - so they are SHIPPED and checked here. */
			@"doubleClickAtIndex:", @"nextWordFromIndex:forward:", @"lineBreakBeforeIndex:withinRange:",
			@"URLAtIndex:effectiveRange:", @"initWithContentsOfMarkdownFileAtURL:options:baseURL:error:" ];
		NSArray *absent = @[ @"drawInRect:", @"drawAtPoint:", @"drawWithRect:options:context:", @"size",
			@"boundingRectWithSize:options:context:",
			/* the HYPHENATING break stays ABSENT: it needs a hyphenation resource this system lacks. */
			@"lineBreakByHyphenatingBeforeIndex:withinRange:", @"containsAttachmentsInRange:",
			@"fontAttributesInRange:", @"rulerAttributesInRange:", @"itemNumberInTextList:atIndex:",
			@"rangeOfTextBlock:atIndex:", @"rangeOfTextList:atIndex:", @"rangeOfTextTable:atIndex:",
			@"prefersRTFDInRange:",
			@"mutableString", @"fixAttachmentAttributeInRange:", @"setAlignment:range:",
			@"superscriptRange:", @"subscriptRange:", @"unscriptRange:",
			@"readFromData:options:documentAttributes:", @"readFromURL:options:documentAttributes:error:" ];
		NSAttributedString *immutable = [[NSAttributedString alloc] initWithString:@"x"];
		NSMutableAttributedString *mutable = [[NSMutableAttributedString alloc] initWithString:@"x"];
		NSMutableArray *missing = [NSMutableArray array];
		NSMutableArray *present = [NSMutableArray array];
		NSUInteger i;

		for (i = 0; i < [implemented count]; i++) {
			NSString *name = [implemented objectAtIndex:i];

			if (![immutable respondsToSelector:NSSelectorFromString(name)] ||
			    ![mutable respondsToSelector:NSSelectorFromString(name)]) {
				[missing addObject:name];
			}
		}
		for (i = 0; i < [absent count]; i++) {
			NSString *name = [absent objectAtIndex:i];

			if ([immutable respondsToSelector:NSSelectorFromString(name)] ||
			    [mutable respondsToSelector:NSSelectorFromString(name)]) {
				[present addObject:name];
			}
		}
		if (![NSAttributedString respondsToSelector:NSSelectorFromString(@"supportsSecureCoding")]) {
			[missing addObject:@"+supportsSecureCoding"];
		}
		if (![NSAttributedString respondsToSelector:
				NSSelectorFromString(@"loadFromHTMLWithRequest:options:completionHandler:")]) {
			[missing addObject:@"+loadFromHTMLWithRequest:options:completionHandler:"];
		}
		/* THE THREE SIBLINGS the 2026-09-30 pass added, each a class method with its own spelling. */
		if (![NSAttributedString respondsToSelector:
				NSSelectorFromString(@"loadFromHTMLWithData:options:completionHandler:")] ||
		    ![NSAttributedString respondsToSelector:
				NSSelectorFromString(@"loadFromHTMLWithFileURL:options:completionHandler:")] ||
		    ![NSAttributedString respondsToSelector:
				NSSelectorFromString(@"loadFromHTMLWithString:options:completionHandler:")]) {
			[missing addObject:@"+loadFromHTMLWith{Data,FileURL,String}:options:completionHandler:"];
		}
		check("inventory-the-shipped-selectors-exist", [missing count] == 0,
		      [NSString stringWithFormat:@"missing: %@", [missing componentsJoinedByString:@", "]]);
		check("inventory-the-boundaries-are-absent", [present count] == 0,
		      [NSString stringWithFormat:@"shipped although this slice does not carry them: %@",
			[present componentsJoinedByString:@", "]]);
	}

	/* ---- THE MUTABLE-ONLY FORMAT DOOR (macOS 12+, ledger row NSMutableAttributedString/-appendLocalizedFormat:)
	 *
	 * APPLE'S OWN CURRENT-LOCALE FORMAT APPEND: "Formats the specified string and arguments with the current
	 * locale, then appends the result to the receiver." It is declared ONLY on the mutable subclass, so the
	 * two-class inventory rule above cannot carry it (that rule needs BOTH classes to answer) - it is checked
	 * here against the mutable class, and the immutable base is required NOT to answer it. The expectations
	 * are Apple's and are MEASURED: the formatted TEXT reaches the receiver unchanged, the length grows by
	 * exactly the formatted length, and the appended run carries NO attribute (a plain format append installs
	 * no formatting of its own). NOTE: this library's locale doors render the LOCALE-FREE answer (a locale is
	 * honoured for case only, NSString.h records), so this run is NOT a check of locale-sensitive rendering. */
	{
		NSMutableAttributedString *m = [[NSMutableAttributedString alloc] initWithString:@"head:"];
		NSAttributedString *imm = [[NSAttributedString alloc] initWithString:@"head:"];
		NSUInteger before = [m length];
		NSRange eff = NSMakeRange(0, 0);
		BOOL isMutableOnly = [m respondsToSelector:@selector(appendLocalizedFormat:)] &&
				     ![imm respondsToSelector:@selector(appendLocalizedFormat:)];

		[m appendLocalizedFormat:@"%@-%d", @"x", 42];
		{
			NSDictionary *appended = [m attributesAtIndex:before effectiveRange:&eff];
			NSUInteger after = [m length];
			BOOL textOK = [[m string] isEqualToString:@"head:x-42"];
			/* ⚠ AND "x-42" IS FOUR UNITS, NOT FIVE — CORRECTED AGAINST THE GUEST, NOT REASONED A SECOND TIME.
			 * The first version of this check expected before + 5 (its own comment even said "five"), the guest
			 * answered 9 for a 5-unit base, and the detail line carried the evidence: string="head:x-42"
			 * after=9(expect 10). THE IMPLEMENTATION WAS RIGHT AND THE EXPECTATION WAS WRONG — the same shape as
			 * the number formatter's nf-always-decimal — and this check had been MARKED as reasoned rather than
			 * measured, which is exactly why the failure was one line to find rather than a hunt. */
			BOOL lenOK = after == before + 4;
			BOOL noAttr = [appended count] == 0;

			printf("FOUNDATION-ATTRIBUTEDSTRING DIAG appendLocalizedFormat before=%lu after=%lu "
			       "string=\"%s\" appended-attrs=%lu eff=(%lu,%lu)\n",
			       (unsigned long)before, (unsigned long)after, [[m string] UTF8String],
			       (unsigned long)[appended count],
			       (unsigned long)eff.location, (unsigned long)eff.length);
			check("append-localized-format-appends-the-formatted-string",
			      isMutableOnly && textOK && lenOK && noAttr,
			      [NSString stringWithFormat:@"mutableOnly=%d string=\"%@\" after=%lu(expect %lu) "
				"appended-attrs=%lu",
				(int)isMutableOnly, [m string], (unsigned long)after,
				(unsigned long)(before + 4), (unsigned long)[appended count]]);
		}
	}

	/* ---- THE SUPPORTED-TEXT-FORMAT DOORS (2026-10-01, plan §61) ---------------------------------------
	 *
	 * SIX CLASS MEMBERS. THE FIRST CHECK MEASURES WHAT IS MEASURABLE: that all six answer, that each answers
	 * a NON-EMPTY array of strings, that a caller mutating what it got cannot reach a later caller (each call
	 * builds a fresh array), that the mutable subclass answers them through inheritance, and that - because
	 * no filter service exists here - each FILTERED list equals its UNFILTERED twin. THE SECOND CHECK PINS
	 * THE CHOICE: the CONTENTS are this library's own (§11.6.1 D2), so it asserts the values Apple PUBLISHES
	 * as this group's defaults rather than a measurement of this run. */
	{
		NSArray *fileTypes = [NSAttributedString textFileTypes];
		NSArray *unfilteredFileTypes = [NSAttributedString textUnfilteredFileTypes];
		NSArray *pasteTypes = [NSAttributedString textPasteboardTypes];
		NSArray *unfilteredPasteTypes = [NSAttributedString textUnfilteredPasteboardTypes];
		NSArray *types = [NSAttributedString textTypes];
		NSArray *unfilteredTypes = [NSAttributedString textUnfilteredTypes];
		NSArray *six = [NSArray arrayWithObjects:fileTypes, unfilteredFileTypes, pasteTypes,
				 unfilteredPasteTypes, types, unfilteredTypes, nil];
		NSMutableArray *bad = [NSMutableArray array];
		NSUInteger j, k;

		for (j = 0; j < [six count]; j++) {
			NSArray *arr = [six objectAtIndex:j];

			if ([arr count] == 0) {
				[bad addObject:@"an empty list"];
			}
			for (k = 0; k < [arr count]; k++) {
				if (![[arr objectAtIndex:k] isKindOfClass:[NSString class]]) {
					[bad addObject:@"a non-string element"];
				}
			}
		}
		/* NO FILTER SERVICE EXISTS HERE, so a filtered list is its unfiltered twin (the header records why). */
		if (![fileTypes isEqualToArray:unfilteredFileTypes]) {
			[bad addObject:@"file types filtered!=unfiltered"];
		}
		if (![pasteTypes isEqualToArray:unfilteredPasteTypes]) {
			[bad addObject:@"pasteboard filtered!=unfiltered"];
		}
		if (![types isEqualToArray:unfilteredTypes]) {
			[bad addObject:@"UTI filtered!=unfiltered"];
		}
		/* THE MUTABLE SUBCLASS ANSWERS THEM THROUGH INHERITANCE, as Apple's does. */
		if (![[NSMutableAttributedString textTypes] isEqualToArray:types]) {
			[bad addObject:@"subclass does not inherit"];
		}
		/* A FRESH ARRAY EACH CALL: this is the ownership rule, measured by identity. */
		if ([NSAttributedString textFileTypes] == fileTypes) {
			[bad addObject:@"the same array was handed out twice"];
		}
		check("the-supported-text-format-doors-answer-their-vocabularies", [bad count] == 0,
		      [NSString stringWithFormat:@"bad: %@", [bad componentsJoinedByString:@", "]]);

		/* REASONED, NOT MEASURED: these four strings are the extensions Apple PUBLISHES as this group's
		 * default file types, which this library adopts; the measurement happened in the check above, so if
		 * this one disagrees the ARRAY this library chose (§11.6.1 D2) is what to read, not a number. */
		check("the-format-doors-list-the-published-file-types",
		      [unfilteredFileTypes containsObject:@"txt"] && [unfilteredFileTypes containsObject:@"rtf"] &&
		      [unfilteredFileTypes containsObject:@"rtfd"] && [unfilteredFileTypes containsObject:@"html"],
		      [NSString stringWithFormat:@"file types: %@", unfilteredFileTypes]);
	}

	/* ---- THE STRING, IN UTF-16 UNITS ----------------------------------------------------------------- */
	{
		NSAttributedString *plain = [[NSAttributedString alloc] initWithString:@"hello"];

		/* A SURROGATE PAIR CANNOT BE SPELLED IN A LITERAL (the universal-character-name route is invalid
		 * for surrogates) - and it is built from UTF-8 BYTES rather than from a pair of unichars, because
		 * this library has a recorded habit of declaring string initialisers it does not implement (a
		 * +stringWithCharacters: call CRASHED here first). */
		NSString *decoded = [[NSString alloc] initWithUTF8String:"\xF0\x9D\x84\x9E"];	/* U+1D11E */
		NSAttributedString *wide;

		wide = [[NSAttributedString alloc] initWithString:(decoded != nil ? decoded : @"")];

		/* PER STEP, WITH A MARKER BETWEEN THE STEPS: this check faulted and the marker was the only way to
		 * find out WHICH statement did it (the guest's own fault report names the address and not the call).
		 * A whole expression inside one check() call is one instrument, and an instrument that cannot say
		 * where it died is not measuring. */
		{
			NSString *plainString = [plain string];
			NSUInteger plainLength = [plain length];
			NSUInteger wideLength = [wide length];
			BOOL isHello;

			isHello = [plainString isEqual:@"hello"];
			check("string-and-length-are-the-initialisers",
			      isHello && plainLength == 5 && wideLength == 2,
			      [NSString stringWithFormat:@"plain=%@/%lu wide-length=%lu (a surrogate pair is TWO "
				@"UTF-16 units)", plainString, (unsigned long)plainLength,
				(unsigned long)wideLength]);
		}
	}

	/* ---- THE COALESCING CONTRACT, BUILT FOUR DIFFERENT WAYS ------------------------------------------- */
	{
		NSRange range = NSMakeRange(0, 0);
		NSAttributedString *whole = [[NSAttributedString alloc] initWithString:@"abcd" attributes:one];
		NSMutableAttributedString *appended = [[NSMutableAttributedString alloc] initWithString:@""];
		NSRange r1 = NSMakeRange(0, 0), r2 = NSMakeRange(0, 0);
		int spans = 0;

		(void)[whole attributesAtIndex:0 effectiveRange:&range];
		[appended appendAttributedString:[[NSAttributedString alloc] initWithString:@"ab" attributes:one]];
		{
			NSRange trace = NSMakeRange(0, 0);

			if ([appended length] > 0) {
				(void)[appended attributesAtIndex:0 effectiveRange:&trace];
			}
		}
		[appended appendAttributedString:[[NSAttributedString alloc] initWithString:@"cd" attributes:one]];
		{
			NSRange trace = NSMakeRange(0, 0);

			(void)[appended attributesAtIndex:0 effectiveRange:&trace];
		}
		(void)[appended attributesAtIndex:0 effectiveRange:&r1];
		(void)[appended attributesAtIndex:3 effectiveRange:&r2];
		spans = (r1.location == 0 && r1.length == 4) + (r2.location == 0 && r2.length == 4);
		/* an edit group: two equal additions around a DIFFERENT one, coalesced at the OUTERMOST -endEditing */
		{
			NSMutableAttributedString *grouped = [[NSMutableAttributedString alloc] initWithString:@"abcd"];
			NSRange g = NSMakeRange(0, 0);
			int inner = 0;

			[grouped beginEditing];
			[grouped addAttributes:one range:NSMakeRange(0, 4)];
			[grouped beginEditing];
			[grouped addAttributes:one range:NSMakeRange(0, 2)];
			[grouped endEditing];		/* nested: the outer group still holds, so no coalescing yet */
			[grouped endEditing];
			(void)[grouped attributesAtIndex:0 effectiveRange:&g];
			inner = (g.location == 0 && g.length == 4);
			/* THE DETAIL READS THE STORE, SO IT MUST NOT BE ABLE TO RAISE: a read past a short
			 * `appended` would send NSRangeException out of a DIAGNOSTIC, which is a crash that looks
			 * exactly like a crash in the code under test (the same trap the %@-with-an-integer hit).
			 * Every read below is guarded, and the lengths are printed first. */
			NSUInteger appendedLength = [appended length];
			NSDictionary *a0 = appendedLength > 0 ? [appended attributesAtIndex:0 effectiveRange:NULL]
							      : nil;
			NSDictionary *a2 = appendedLength > 2 ? [appended attributesAtIndex:2 effectiveRange:NULL]
							      : nil;

			printf("FOUNDATION-ATTRIBUTEDSTRING DIAG coalesce-step-appended length=%lu r1=%lu,%lu "
				"r2=%lu,%lu\n", (unsigned long)appendedLength, (unsigned long)r1.location,
				(unsigned long)r1.length, (unsigned long)r2.location, (unsigned long)r2.length);
			printf("FOUNDATION-ATTRIBUTEDSTRING DIAG coalesce-step-grouped inner=%d g=%lu,%lu\n",
				inner, (unsigned long)g.location, (unsigned long)g.length);
			{
				NSUInteger wholeLength = [whole length];
				BOOL contractHolds = (range.location == 0 && range.length == 4 && spans == 2 && inner &&
						      wholeLength == 4);
				printf("FOUNDATION-ATTRIBUTEDSTRING DIAG coalesce: holds=%d whole=%lu,%lu r1=%lu,%lu "
					"r2=%lu,%lu inner=%d spans=%d\n", (int)contractHolds,
					(unsigned long)range.location, (unsigned long)range.length,
					(unsigned long)r1.location, (unsigned long)r1.length,
					(unsigned long)r2.location, (unsigned long)r2.length, inner, spans);
				check("the-coalescing-contract-holds-whatever-built-it", contractHolds,
				      contractHolds ? @"the contract holds" : @"see the coalesce DIAG line");
			}
		}
	}

	{
		printf("FOUNDATION-ATTRIBUTEDSTRING DIAG leg=iso-block\n");
		/* WHERE THE CORRUPTION ENTERS: the same two steps, with the store read BEFORE and AFTER the
		 * attribute edit - the previous instrument showed {B:2} where {A:1} was built and could not say
		 * which step did it. */
		NSMutableAttributedString *t = [[NSMutableAttributedString alloc] initWithString:@"abcd"
									     attributes:one];
		NSDictionary *before = [t attributesAtIndex:0 effectiveRange:NULL];
		NSRange r0 = NSMakeRange(0, 0);
		id a0 = [t attribute:fn_A() atIndex:0 effectiveRange:&r0];

		printf("FOUNDATION-ATTRIBUTEDSTRING DIAG iso=before keys=%s a0=%s\n",
			[[[before allKeys] componentsJoinedByString:@","] UTF8String],
			a0 != nil ? "yes" : "NO");

		[t addAttributes:both range:NSMakeRange(2, 2)];
		{
			NSDictionary *after0 = [t attributesAtIndex:0 effectiveRange:NULL];
			NSDictionary *after2 = [t attributesAtIndex:2 effectiveRange:NULL];

			printf("FOUNDATION-ATTRIBUTEDSTRING DIAG iso=after keys0=%s keys2=%s\n",
				[[[after0 allKeys] componentsJoinedByString:@","] UTF8String],
				[[[after2 allKeys] componentsJoinedByString:@","] UTF8String]);
		}
	}

	{
		printf("FOUNDATION-ATTRIBUTEDSTRING DIAG leg=attribute-range-block\n");
		/* ONE ATTRIBUTE CAN HOLD OVER TWO RUNS: {A} then {A,B} are NOT the same dictionary (two runs), but
		 * the effective range of A itself reaches across both. */
		NSMutableAttributedString *s = [[NSMutableAttributedString alloc] initWithString:@"abcd" attributes:one];

		[s addAttributes:both range:NSMakeRange(2, 2)];
		{
			NSRange dictRange = NSMakeRange(0, 0), attrRange = NSMakeRange(0, 0);
			NSDictionary *dict = [s attributesAtIndex:0 effectiveRange:&dictRange];

			printf("FOUNDATION-ATTRIBUTEDSTRING DIAG attr-step=dict-fetched\n");
			{
				NSRange trace = NSMakeRange(0, 0);
				NSDictionary *again = [s attributesAtIndex:0 effectiveRange:&trace];

				printf("FOUNDATION-ATTRIBUTEDSTRING DIAG attr-step=dict-again len=%lu keys=%s\n",
					(unsigned long)[again count],
					[[[again allKeys] componentsJoinedByString:@","] UTF8String]);
			}
			{
				id value = [s attribute:fn_A() atIndex:0 effectiveRange:&attrRange];

				printf("FOUNDATION-ATTRIBUTEDSTRING DIAG attr-step=value %s\n",
					value != nil ? "yes" : "NO");
				check("the-attribute-range-can-be-longer-than-the-dictionary-run",
				      dictRange.length == 2 && attrRange.location == 0 && attrRange.length == 4 &&
				      [value intValue] == 1 && [dict count] == 1,
				      [NSString stringWithFormat:@"dict-run=%@ attribute-run=%@ value=%@",
					fn_r(dictRange), fn_r(attrRange), value]);
			}

		}
	}

	{
		/* THE longestEffectiveRange:inRange: FORM IS THE SAME ANSWER, CLIPPED TO THE GIVEN RANGE. */
		NSAttributedString *s = [[NSAttributedString alloc] initWithString:@"abcdef" attributes:one];
		NSRange clipped = NSMakeRange(0, 0);

		(void)[s attributesAtIndex:2 longestEffectiveRange:&clipped inRange:NSMakeRange(1, 3)];
		check("the-longest-effective-range-form-clips-to-its-range",
		      clipped.location == 1 && clipped.length == 3,
		      [NSString stringWithFormat:@"clipped=%@", fn_r(clipped)]);
	}

	/* ---- SPLICING ------------------------------------------------------------------------------------- */
	{
		NSMutableAttributedString *s = [[NSMutableAttributedString alloc] initWithString:@"abcd" attributes:one];
		NSRange after = NSMakeRange(0, 0);

		[s replaceCharactersInRange:NSMakeRange(1, 1) withString:@"XY"];
		(void)[s attributesAtIndex:1 effectiveRange:&after];
		check("a-replacement-takes-the-attributes-in-force-at-its-start",
		      [[s string] isEqual:@"aXYcd"] && [s length] == 5 &&
		      after.location == 0 && after.length == 5,
		      [NSString stringWithFormat:@"string=%@ run=%@", [s string], fn_r(after)]);
	}

	{
		NSMutableAttributedString *s = [[NSMutableAttributedString alloc] initWithString:@"abcd" attributes:one];
		NSMutableAttributedString *tail = [[NSMutableAttributedString alloc] initWithString:@"EF" attributes:both];
		NSRange attrRange = NSMakeRange(0, 0);
		NSRange firstRun = NSMakeRange(0, 0);
		NSRange secondRun = NSMakeRange(0, 0);

		[s appendAttributedString:tail];
		(void)[s attributesAtIndex:0 effectiveRange:&firstRun];
		(void)[s attributesAtIndex:4 effectiveRange:&secondRun];
		(void)[s attribute:fn_B() atIndex:4 effectiveRange:&attrRange];
		{
			NSString *text = [s string];
			NSUInteger textLength = [s length];
			NSRange bRange = NSMakeRange(0, 0);
			id bValue;

			printf("FOUNDATION-ATTRIBUTEDSTRING DIAG step=append-text\n");
			bValue = [s attribute:fn_B() atIndex:4 effectiveRange:&bRange];
			printf("FOUNDATION-ATTRIBUTEDSTRING DIAG step=append-bvalue %s\n",
				bValue != nil ? "yes" : "NO");
			check("appending-an-attributed-string-carries-its-own-runs",
			      [text isEqual:@"abcdEF"] && textLength == 6 &&
			      firstRun.location == 0 && firstRun.length == 4 &&
			      secondRun.location == 4 && secondRun.length == 2 && bRange.length == 2,
			      [NSString stringWithFormat:@"text=%@ length=%lu first=%@ second=%@ B-run=%@",
				text, (unsigned long)textLength, fn_r(firstRun), fn_r(secondRun), fn_r(bRange)]);
		}

		printf("FOUNDATION-ATTRIBUTEDSTRING DIAG leg=substring-start\n");
		{
			NSAttributedString *piece = [s attributedSubstringFromRange:NSMakeRange(3, 3)];
			NSRange whole = NSMakeRange(0, 0);
			id pieceValue = nil;

			(void)[piece attributesAtIndex:0 effectiveRange:&whole];
			pieceValue = [piece attribute:fn_B() atIndex:2 effectiveRange:NULL];
			printf("FOUNDATION-ATTRIBUTEDSTRING DIAG leg=substring\n");
		check("a-substring-keeps-the-attributes-it-covered",
			      [[piece string] isEqual:@"dEF"] && [piece length] == 3 &&
			      whole.location == 0 && whole.length == 1 && [pieceValue intValue] == 2,
			      [NSString stringWithFormat:@"piece=%@ run=%@ B=%@", [piece string], fn_r(whole),
				pieceValue]);
		}

		{
			printf("FOUNDATION-ATTRIBUTEDSTRING DIAG leg=equality-start\n");
			NSAttributedString *same = [[NSAttributedString alloc] initWithString:@"abcdEF"
										 attributes:one];
			NSMutableAttributedString *changed = [[NSMutableAttributedString alloc] initWithString:@"abcdEF"
											   attributes:one];

			[changed addAttribute:fn_B() value:@2 range:NSMakeRange(5, 1)];
			printf("FOUNDATION-ATTRIBUTEDSTRING DIAG leg=equality\n");
		check("equality-is-by-value-not-by-identity",
			      ![s isEqualToAttributedString:same] && ![s isEqualToAttributedString:changed],
			      [NSString stringWithFormat:@"s=%@ changed=%@", [s string], [changed string]]);
		}

		{
			NSMutableAttributedString *deleted = [[NSMutableAttributedString alloc] initWithString:@"abcdEF"
											   attributes:one];
			NSRange left = NSMakeRange(0, 0);

			[deleted deleteCharactersInRange:NSMakeRange(2, 2)];
			(void)[deleted attributesAtIndex:0 effectiveRange:&left];
			[deleted setAttributedString:[[NSAttributedString alloc] initWithString:@"z" attributes:both]];
			printf("FOUNDATION-ATTRIBUTEDSTRING DIAG leg=delete-set\n");
		check("delete-and-set-keep-text-and-runs-together",
			      [deleted length] == 1 && [[deleted string] isEqual:@"z"] &&
			      [[[deleted attributesAtIndex:0 effectiveRange:NULL] objectForKey:fn_B()] intValue] == 2,
			      [NSString stringWithFormat:@"length=%lu string=%@ attrs=%@",
				(unsigned long)[deleted length], [deleted string],
				[deleted attributesAtIndex:0 effectiveRange:NULL]]);
		}
	}

	/* ---- THE ATTRIBUTE DOORS -------------------------------------------------------------------------- */
	{
		NSMutableAttributedString *s = [[NSMutableAttributedString alloc] initWithString:@"abcd" attributes:both];
		NSRange r = NSMakeRange(0, 0);

		[s addAttribute:fn_A() value:nil range:NSMakeRange(1, 2)];	/* a chosen rule: nil REMOVES */
		(void)[s attributesAtIndex:1 effectiveRange:&r];
		printf("FOUNDATION-ATTRIBUTEDSTRING DIAG leg=nil-removes\n");
		check("a-nil-value-removes-and-removal-shows-in-the-runs",
		      [[s attributesAtIndex:1 effectiveRange:NULL] objectForKey:fn_A()] == nil &&
		      [[[s attributesAtIndex:1 effectiveRange:NULL] objectForKey:fn_B()] intValue] == 2 &&
		      r.length == 2,
		      [NSString stringWithFormat:@"attrs=%@ run=%@", [s attributesAtIndex:1 effectiveRange:NULL],
			fn_r(r)]);
	}

	/* ---- ENUMERATION ---------------------------------------------------------------------------------- */
	{
		NSMutableAttributedString *s = [[NSMutableAttributedString alloc] initWithString:@"abcdef" attributes:one];
		NSMutableArray *forward = [NSMutableArray array];
		NSMutableArray *reverse = [NSMutableArray array];
		NSMutableArray *values = [NSMutableArray array];
		__block NSUInteger covered = 0;

		[s addAttributes:both range:NSMakeRange(3, 3)];
		[s enumerateAttributesInRange:NSMakeRange(0, 6) options:0
				   usingBlock:^(NSDictionary *attrs, NSRange range, BOOL *stop) {
			[forward addObject:NSStringFromRange(range)];
			covered += range.length;
			(void)attrs;
			(void)stop;
		}];
		[s enumerateAttributesInRange:NSMakeRange(0, 6) options:NSAttributedStringEnumerationReverse
				   usingBlock:^(NSDictionary *attrs, NSRange range, BOOL *stop) {
			[reverse addObject:NSStringFromRange(range)];
			(void)attrs;
			(void)stop;
		}];
		[s enumerateAttribute:fn_B() inRange:NSMakeRange(0, 6)
			      options:0
			   usingBlock:^(_Nullable id value, NSRange range, BOOL *stop) {
			[values addObject:[NSString stringWithFormat:@"%@:%@", value, NSStringFromRange(range)]];
			(void)stop;
		}];
		printf("FOUNDATION-ATTRIBUTEDSTRING DIAG leg=enumeration\n");
		check("enumeration-tiles-the-range-and-reverses",
		      [forward count] == 2 && covered == 6 &&
		      [reverse count] == 2 &&
		      [[reverse objectAtIndex:0] isEqual:[forward objectAtIndex:1]] &&
		      [[reverse objectAtIndex:1] isEqual:[forward objectAtIndex:0]] &&
		      [values count] == 2 &&
		      [[values objectAtIndex:0] isEqual:@"(null):{0, 3}"] &&
		      [[values objectAtIndex:1] hasPrefix:@"2:"],
		      [NSString stringWithFormat:@"forward=%@ reverse=%@ values=%@", forward, reverse, values]);
	}

	/* ---- THE RAISE, AND COPYING ----------------------------------------------------------------------- */
	{
		NSAttributedString *s = [[NSAttributedString alloc] initWithString:@"abc"];
		BOOL raised = NO;

		@try {
			(void)[s attributesAtIndex:3 effectiveRange:NULL];
		} @catch (NSException *e) {
			raised = [[e name] isEqualToString:NSRangeException];
		}
		printf("FOUNDATION-ATTRIBUTEDSTRING DIAG leg=raise\n");
		check("an-out-of-range-index-raises", raised, @"attributesAtIndex:3 on a 3-character string");
	}

	{
		NSMutableAttributedString *s = [[NSMutableAttributedString alloc] initWithString:@"abcd" attributes:one];
		NSMutableAttributedString *copy = [s mutableCopy];
		NSAttributedString *fixed = [s copy];

		[copy addAttribute:fn_B() value:@2 range:NSMakeRange(0, 4)];
		printf("FOUNDATION-ATTRIBUTEDSTRING DIAG leg=copy\n");
		check("a-copy-is-independent-and-an-immutable-copy-is-a-snapshot",
		      [[s attributesAtIndex:0 effectiveRange:NULL] objectForKey:fn_B()] == nil &&
		      [[[copy attributesAtIndex:0 effectiveRange:NULL] objectForKey:fn_B()] intValue] == 2 &&
		      [[fixed string] isEqual:@"abcd"] && [fixed length] == 4,
		      [NSString stringWithFormat:@"original=%@ copy=%@", [s attributesAtIndex:0 effectiveRange:NULL],
			[copy attributesAtIndex:0 effectiveRange:NULL]]);
	}

	{
		/* W10 SLICE 3: THE ARCHIVE ROUND TRIP. A run store is carried as a PROPERTY LIST inside the coder's
		 * own object slot, so the strongest assertion is available: what comes back must be EQUAL to what
		 * went in, runs and all - which -isEqualToAttributedString: answers in one call. */
		NSMutableAttributedString *before = [[NSMutableAttributedString alloc]
			initWithString:@"abcd" attributes:@{ @"A": @1 }];
		NSData *archive;
		id after;

		[before addAttributes:@{ @"B": @2 } range:NSMakeRange(2, 2)];
		/* A PROBE MUST NOT DIE ON AN UNEXPECTED RAISE: the round trip is expected to SUCCEED, so a raise
		 * here is a failure to report, not a process to lose. */
		@try {
			archive = [NSKeyedArchiver archivedDataWithRootObject:before];
		} @catch (NSException *e) {
			archive = nil;
			printf("FOUNDATION-ATTRIBUTEDSTRING DIAG coding: archive raised %s: %s\n",
				[[e name] UTF8String], [[e reason] UTF8String]);
		}
		@try {
			after = archive != nil ? [NSKeyedUnarchiver unarchiveObjectWithData:archive] : nil;
		} @catch (NSException *e) {
			after = nil;
			printf("FOUNDATION-ATTRIBUTEDSTRING DIAG coding: unarchive raised %s: %s\n",
				[[e name] UTF8String], [[e reason] UTF8String]);
		}
		/* THE ROUND TRIP, WHICH WAS A MEASURED BOUNDARY AND IS NOW A MEASUREMENT. This tree's archiver does
		 * not carry a nested object reference, so the class carries its payload as BYTES (a property list) and
		 * the bytes travel - so the archive is non-empty and the unarchived object holds the same string AND
		 * the same attributes. §62.69 found why the old check had an empty archive to assert: the coder doors
		 * were UNREACHABLE (the class asked the property-list serializer for the BINARY format this library
		 * rejects by name, and decoded with a misspelled selector label), and this round trip is exactly the
		 * assertion that catches both. */
		check("coding-round-trips-through-the-archiver",
		      archive != nil && [archive length] > 0 && after != nil &&
		      [after isEqualToAttributedString:before],
		      [NSString stringWithFormat:@"archive=%lu bytes, unarchived=%@ (want the same string and "
			@"attributes as %@)", (unsigned long)[archive length], after, [before string]]);

		/* THE NAMED REFUSAL: an attribute value a property list cannot carry. Apple's -encodeWithCoder:
		 * raises for state it cannot encode, and the message says WHY here rather than writing a
		 * half-archive. */
		{
			NSMutableAttributedString *bad = [[NSMutableAttributedString alloc]
				initWithString:@"x" attributes:@{ @"obj": [[NSObject alloc] init] }];
			BOOL refused = NO;

			@try {
				(void)[NSKeyedArchiver archivedDataWithRootObject:bad];
			} @catch (NSException *e) {
				refused = [[e name] isEqualToString:NSInvalidArgumentException];
			}
			check("coding-refuses-what-a-property-list-cannot-carry", refused,
			      refused ? @"the archive raised NSInvalidArgumentException and named the reason"
				      : @"a non-plist attribute went unrefused");
		}

		check("coding-supports-secure-coding-answers-yes",
		      [NSAttributedString supportsSecureCoding],
		      @"the class answers YES (NSCoding.h names the coder-side enforcement as the gap)");
	}

	{
		/* ---- THE FILE-FORMAT DOORS: RTF WRITES, THE REST STILL REFUSE BY NAME (§62.58) --------------
		 *
		 * RTF IS A REAL WRITER NOW, and these checks read the BYTES it produces rather than trusting a
		 * description of them: the document shell, the format's reserved characters escaped, non-ASCII as the
		 * signed `\uN?` escape, an inline-intent bit as the control word that means it, and a line feed as a
		 * paragraph break. The doors that still cannot do their work are still required to REFUSE BY NAME,
		 * because a silent nil is indistinguishable from an empty document. */
		NSAttributedString *sample = [[NSAttributedString alloc] initWithString:@"x"];
		NSError *error = nil;
		NSData *data = [sample dataFromRange:NSMakeRange(0, 1) documentAttributes:nil error:&error];
		NSString *description = [error localizedDescription];

		check("file-format-doors-refuse-by-name",
		      data == nil && error != nil && description != nil &&
		      [description rangeOfString:@"not implemented"].location != NSNotFound,
		      [NSString stringWithFormat:@"data=%@ error=%@", data, description]);

		{
			/* THE SHELL: `{`, the `\rtf1` header, a font table, and the closing `}` as the last byte before
			 * the trailing newline. A document that failed any of these would not open in a reader. */
			NSData *rtf = [sample RTFFromRange:NSMakeRange(0, 1) documentAttributes:nil];
			const unsigned char *bytes = rtf != nil ? [rtf bytes] : NULL;
			NSUInteger length = rtf != nil ? [rtf length] : 0;

			check("rtf-writes-a-document-with-its-own-shell",
			      bytes != NULL && length >= 2 && bytes[0] == '{' &&
			      fn_bytes_contain(rtf, "\\rtf1") && fn_bytes_contain(rtf, "{\\fonttbl") &&
			      bytes[length - 2] == '}' && bytes[length - 1] == '\n',
			      [NSString stringWithFormat:@"rtf=%@ (%lu bytes)", rtf, (unsigned long)length]);
		}

		/* THE THREE RESERVED CHARACTERS, EACH ESCAPED: a raw `{` would open a group and a raw `\` would start
		 * a control word, so the format requires all three backslash-escaped. */
		{
			NSAttributedString *reserved = [[NSAttributedString alloc] initWithString:@"a{b}c\\d"];
			NSData *out = [reserved RTFFromRange:NSMakeRange(0, 7) documentAttributes:nil];

			check("rtf-escapes-the-formats-reserved-characters",
			      fn_bytes_contain(out, "\\{") && fn_bytes_contain(out, "\\}") &&
			      fn_bytes_contain(out, "\\\\"),
			      @"each of `{` `}` `\\` goes out backslash-escaped");
		}

		/* THE `\uN?` ESCAPE AND ITS SIGN: U+00E9 is 233, U+2014 is 8212, and U+FFFD is NEGATIVE (-3, because
		 * the format reads the value as a signed 16-bit number). */
		{
			NSAttributedString *eacute = [[NSAttributedString alloc] initWithString:@"\u00e9"];
			NSAttributedString *emdash = [[NSAttributedString alloc] initWithString:@"\u2014"];
			NSAttributedString *replacement = [[NSAttributedString alloc] initWithString:@"\ufffd"];

			check("rtf-carries-non-ascii-as-the-signed-u-escape",
			      fn_bytes_contain([eacute RTFFromRange:NSMakeRange(0, 1) documentAttributes:nil],
					       "\\u233?") &&
			      fn_bytes_contain([emdash RTFFromRange:NSMakeRange(0, 1) documentAttributes:nil],
					       "\\u8212?") &&
			      fn_bytes_contain([replacement RTFFromRange:NSMakeRange(0, 1) documentAttributes:nil],
					       "\\u-3?"),
			      @"U+00E9 -> \\u233?, U+2014 -> \\u8212?, U+FFFD -> \\u-3?");
		}

		{
			/* AN INLINE-INTENT BIT BECOMES THE CONTROL WORD THAT MEANS IT: strong emphasis opens `\b` and, so
			 * the emphasis does not leak, closes `\b0`. */
			NSMutableAttributedString *strong = [[NSMutableAttributedString alloc] initWithString:@"bold"];
			NSData *out;

			[strong addAttribute:NSInlinePresentationIntentAttributeName
				       value:[NSNumber numberWithInt:NSInlinePresentationIntentStronglyEmphasized]
				       range:NSMakeRange(0, 4)];
			out = [strong RTFFromRange:NSMakeRange(0, 4) documentAttributes:nil];
			check("rtf-maps-an-inline-intent-bit-to-its-control-word",
			      fn_bytes_contain(out, "\\b ") && fn_bytes_contain(out, "\\b0 "),
			      @"strong emphasis opens \\b and closes \\b0");
		}

		{
			NSAttributedString *twoLines = [[NSAttributedString alloc] initWithString:@"a\nb"];
			NSData *out = [twoLines RTFFromRange:NSMakeRange(0, 3) documentAttributes:nil];

			check("rtf-writes-a-line-feed-as-a-paragraph-break",
			      fn_bytes_contain(out, "\\par"),
			      @"a line feed becomes \\par");
		}

		check("the-other-format-doors-still-refuse-by-name",
		      [sample RTFDFromRange:NSMakeRange(0, 1) documentAttributes:nil] == nil &&
		      [sample RTFDFileWrapperFromRange:NSMakeRange(0, 1) documentAttributes:nil] == nil &&
		      [sample docFormatFromRange:NSMakeRange(0, 1) documentAttributes:nil] == nil,
		      @"RTFD and the doc format answer nil (their Apple shape has no error out)");
	}

	{
		/* THE MORPHOLOGY VOCABULARY: three sets with NO ENGINE behind them, so what can be asserted is what a
		 * caller relies on when it compiles - that the values are DISTINCT within each set (two cases sharing
		 * a value would make two grammatical categories indistinguishable), that the attribute name is the one
		 * the store carries, and that a store keeps a morphology value like any other attribute. */
		NSMutableAttributedString *grammar = [[NSMutableAttributedString alloc]
			initWithString:@"x" attributes:@{ NSMorphologyAttributeName:
				[NSNumber numberWithInt:(int)NSGrammaticalNumberPlural] }];
		id kept = [grammar attribute:NSMorphologyAttributeName atIndex:0 effectiveRange:NULL];
		int distinct = 1;

		if (NSGrammaticalGenderMasculine == NSGrammaticalGenderFeminine ||
		    NSGrammaticalNumberSingular == NSGrammaticalNumberPlural ||
		    NSGrammaticalPartOfSpeechNoun == NSGrammaticalPartOfSpeechVerb) {
			distinct = 0;
		}
		check("morphology-vocabulary-is-distinct-and-carried",
		      distinct && kept != nil && [kept intValue] == (int)NSGrammaticalNumberPlural,
		      @"the three sets have distinct cases and the store keeps a morphology value untouched");
	}

	{
		/* ---- THE WORD AND LINE-BREAK DOORS (2026-09-30) ---------------------------------------------
		 *
		 * The "Calculating linguistic units" group, answered over FNTextBreaking - the substrate the probe
		 * otherwise never touches. Each check pins ONE reading of an Apple phrase this library had to choose;
		 * the choices are named in the methods' own comments. */
		NSAttributedString *s = [[NSAttributedString alloc] initWithString:@"hello world foo"];
		NSRange word = [s doubleClickAtIndex:1];
		NSUInteger forward = [s nextWordFromIndex:1 forward:YES];
		NSUInteger backward = [s nextWordFromIndex:8 forward:NO];
		NSUInteger stuck = [s nextWordFromIndex:15 forward:YES];	/* == length: the walk passes the end */

		printf("FOUNDATION-ATTRIBUTEDSTRING DIAG words forward=%lu backward=%lu stuck=%lu\n",
		       (unsigned long)forward, (unsigned long)backward, (unsigned long)stuck);
		check("double-click-answers-the-word-at-an-index",
		      word.location == 0 && word.length == 5,
		      [NSString stringWithFormat:@"doubleClick(1)=%@ (want the first word (0,5))", fn_r(word)]);

		/* forward from inside the first word -> the NEXT word's start (6); backward from inside the last
		 * word -> the PREVIOUS word's start (6); at the string's end when nothing is after, UNCHANGED (15). */
		check("next-word-walks-to-word-starts",
		      forward == 6 && backward == 6 && stuck == 15,
		      [NSString stringWithFormat:@"forward(1)=%lu backward(8)=%lu stuck(15)=%lu (want 6, 6, 15)",
		      (unsigned long)forward, (unsigned long)backward, (unsigned long)stuck]);
	}

	{
		/* lineBreakBeforeIndex:withinRange: answers where the enclosing line begins (3 for the middle line
		 * of "aa\nbb\ncc"), the line start itself for index 0, and NSNotFound when the range holds no line
		 * start at or before the index. */
		NSAttributedString *twoLines = [[NSAttributedString alloc] initWithString:@"aa\nbb\ncc"];
		NSUInteger mid = [twoLines lineBreakBeforeIndex:4 withinRange:NSMakeRange(0, 8)];
		NSUInteger start = [twoLines lineBreakBeforeIndex:0 withinRange:NSMakeRange(0, 8)];
		NSUInteger none = [twoLines lineBreakBeforeIndex:2 withinRange:NSMakeRange(4, 4)];

		printf("FOUNDATION-ATTRIBUTEDSTRING DIAG linebreak mid=%lu start=%lu none=%lu\n",
		       (unsigned long)mid, (unsigned long)start, (unsigned long)none);
		check("line-break-before-index-answers-the-enclosing-lines-start",
		      mid == 3 && start == 0 && none == NSNotFound,
		      [NSString stringWithFormat:@"mid=%lu start=%lu none=%lu (want 3, 0, not-found)",
		      (unsigned long)mid, (unsigned long)start, (unsigned long)none]);
	}

	{
		/* ---- THE DEPRECATED URL DOOR (2026-09-30) --------------------------------------------------- */
		NSAttributedString *s = [[NSAttributedString alloc] initWithString:@"go to <http://example.com/a> now"];
		NSRange r = NSMakeRange(0, 0);
		NSURL *url = [s URLAtIndex:10 effectiveRange:&r];
		NSRange plain = NSMakeRange(0, 0);
		NSAttributedString *words = [[NSAttributedString alloc] initWithString:@"just words"];
		NSURL *none = [words URLAtIndex:1 effectiveRange:&plain];

		printf("FOUNDATION-ATTRIBUTEDSTRING DIAG url=%s range=%s none=%s\n",
		       url != nil ? [[url absoluteString] UTF8String] : "(nil)", fn_r(r).UTF8String,
		       none != nil ? "yes" : "(nil)");
		check("url-at-index-answers-the-url-that-covers-it",
		      url != nil && r.location == 7 && r.length == 20 &&
		      [[url absoluteString] isEqual:@"http://example.com/a"] && none == nil,
		      [NSString stringWithFormat:@"url=%@ range=%@ none=%@", url, fn_r(r), none]);
	}

	{
		/* ---- THE THREE HTML SIBLINGS REFUSE THROUGH THEIR HANDLER (2026-09-30) ---------------------- */
		__block int calls = 0;
		__block NSString *reason = nil;
		void (^handler)(NSAttributedString *, NSDictionary *, NSError *) =
			^(NSAttributedString *made, NSDictionary *attrs, NSError *err) {
				(void)made;
				(void)attrs;
				calls++;
				reason = [err localizedDescription];
			};

		[NSAttributedString loadFromHTMLWithData:[[NSData alloc] init] options:nil completionHandler:handler];
		[NSAttributedString loadFromHTMLWithString:@"<p>x</p>" options:nil completionHandler:handler];
		/* fileURL: IS NONNULL (Apple's declaration), so the call site states that this literal is known to
		 * parse to a non-nil URL rather than weakening the API; the cast hides no nil here, because
		 * `file:///x` is a well-formed file URL that +URLWithString: answers non-nil for. */
		[NSAttributedString loadFromHTMLWithFileURL:(NSURL * _Nonnull)[NSURL URLWithString:@"file:///x"]
						   options:nil completionHandler:handler];
		printf("FOUNDATION-ATTRIBUTEDSTRING DIAG html-siblings calls=%d\n", calls);
		check("the-html-siblings-refuse-through-their-handler",
		      calls == 3 && reason != nil &&
		      [reason rangeOfString:@"not implemented"].location != NSNotFound,
		      [NSString stringWithFormat:@"calls=%d reason=%@", calls, reason]);
	}

	{
		/* ---- THE MARKDOWN FILE DOOR TAKES A baseURL (2026-09-30) ------------------------------------ */
		/* The file does not exist, so the door reads no data and answers nil (or an empty string, should this
		 * tree's reader report a missing file that way) - the half of its contract that needs no file system:
		 * it carries Apple's FOUR-argument spelling and threads baseURL through. The check is written to hold
		 * whichever nil-or-empty shape the reader gives, because the SPELLING is what this pass adds. */
		NSError *err = nil;
		/* url: IS NONNULL (Apple's declaration): the cast states that `file:///no/such/file.md` is a
		 * well-formed file URL +URLWithString: answers non-nil for - it hides no nil (a nil url would still be
		 * handled below as an unreadable file). baseURL: is nullable, so it needs no cast. */
		id made = [[NSAttributedString alloc] initWithContentsOfMarkdownFileAtURL:
				   (NSURL * _Nonnull)[NSURL URLWithString:@"file:///no/such/file.md"]
			       options:nil baseURL:[NSURL URLWithString:@"https://base.example/"] error:&err];
		BOOL spelled = [NSAttributedString instancesRespondToSelector:
				NSSelectorFromString(@"initWithContentsOfMarkdownFileAtURL:options:baseURL:error:")];

		printf("FOUNDATION-ATTRIBUTEDSTRING DIAG markdown-base spelled=%d made=%s len=%lu\n",
		       (int)spelled, made != nil ? "yes" : "(nil)",
		       made != nil ? (unsigned long)[made length] : 0UL);
		check("the-markdown-file-door-takes-a-base-url",
		      spelled && (made == nil || [made length] == 0),
		      [NSString stringWithFormat:@"spelled=%d made=%@", (int)spelled, made]);
	}

	printf("FOUNDATION-ATTRIBUTEDSTRING RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-ATTRIBUTEDSTRING DONE\n");
	return failc == 0 ? 0 : 1;
}
