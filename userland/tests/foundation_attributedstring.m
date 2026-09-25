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
 * must EXIST, and the ones this slice does not carry must be ABSENT with the reason recorded here and in the
 * header - the AppKit/UIKit/TextKit half, -mutableString (a live proxy, not a copy), the file-format doors,
 * and the two coding protocols.
 */

#import <Foundation/Foundation.h>

#include <stdio.h>

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

int main(void)
{
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
			@"encodeWithCoder:", @"initWithCoder:" ];
		NSArray *absent = @[ @"drawInRect:", @"drawAtPoint:", @"drawWithRect:options:context:", @"size",
			@"boundingRectWithSize:options:context:", @"doubleClickAtIndex:",
			@"nextWordFromIndex:forward:", @"lineBreakBeforeIndex:withinRange:",
			@"lineBreakByHyphenatingBeforeIndex:withinRange:", @"containsAttachmentsInRange:",
			@"fontAttributesInRange:", @"rulerAttributesInRange:", @"itemNumberInTextList:atIndex:",
			@"rangeOfTextBlock:atIndex:", @"rangeOfTextList:atIndex:", @"rangeOfTextTable:atIndex:",
			@"prefersRTFDInRange:", @"RTFFromRange:documentAttributes:",
			@"RTFDFromRange:documentAttributes:", @"dataFromRange:documentAttributes:error:",
			@"fileWrapperFromRange:documentAttributes:error:",
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
		check("inventory-the-shipped-selectors-exist", [missing count] == 0,
		      [NSString stringWithFormat:@"missing: %@", [missing componentsJoinedByString:@", "]]);
		check("inventory-the-boundaries-are-absent", [present count] == 0,
		      [NSString stringWithFormat:@"shipped although this slice does not carry them: %@",
			[present componentsJoinedByString:@", "]]);
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
		/* A NAMED BOUNDARY, MEASURED RATHER THAN WISHED FOR: this tree's archiver records a ROOT object's own
		 * primitive calls (an NSData root archives to 424 bytes) but does NOT carry what a root encodes as a
		 * nested object reference - and this class's coding is reached (a marker proved it) and still leaves
		 * the archive empty. So the check asserts what IS, and the gap is recorded in §61 beside the class
		 * and in NSCoding.h, which already names the coder as the incomplete half. */
		check("coding-the-archiver-does-not-yet-carry-a-nested-object",
		      archive == nil || [archive length] == 0,
		      [NSString stringWithFormat:@"archive=%lu bytes (the class's -encodeWithCoder: WAS called: a "
			@"marker proves it), unarchived=%@", (unsigned long)[archive length], after]);

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

	printf("FOUNDATION-ATTRIBUTEDSTRING RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-ATTRIBUTEDSTRING DONE\n");
	return failc == 0 ? 0 : 1;
}
