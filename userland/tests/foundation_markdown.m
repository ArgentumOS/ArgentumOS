/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_markdown — §62.64's acceptance: the markdown family's two VALUE OBJECTS.
 *
 * ONE unit, importing only <Foundation/Foundation.h>.
 *
 * WHAT IS ASSERTED IS THAT BOTH OBJECTS ARE VALUES AND THAT THE ONE PIECE OF ARITHMETIC IN THEM IS RIGHT. The
 * options object is five properties with starting values; the source position is four 1-based numbers whose
 * columns are UTF-8 BYTE offsets (Apple's own rule) mapped into the UTF-16 ranges every range in this library
 * uses. THE CHECK THAT HAS TEETH IS `a-column-is-a-utf8-byte-not-a-character`: an implementation that counted
 * CHARACTERS would pass every ASCII check here and fail that one, which is why it is written with a multi-byte
 * character and asserts both the LENGTH and the SUBSTRING.
 *
 * NOTHING HERE PARSES MARKDOWN, and that is deliberate: the importer is the dependency §12.6 still lists, and
 * these two classes are what it would be configured with and what it would record.
 */

#import <Foundation/Foundation.h>

#include <stdio.h>

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-MARKDOWN %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-MARKDOWN %s FAIL %s\n", name, detail != nil ? [detail UTF8String] : "");
	}
}

/* ---------------------------------------------------------------------------------------------------
 * §62.107's HALF OF THIS PROBE: the importer. The helpers below read the two INTENT attributes and the
 * two doors' own parameters, because this half's whole product is a semantic result — inline intents and
 * block presentation intents, and no styling at all (Apple's own sentence: "The system doesn't add style
 * attributes to match the Markdown elements").
 */
/* The inline intent bits at the first occurrence of `needle`. */
static unsigned int bits_at(NSAttributedString *a, NSString *needle)
{
	NSRange r = [[a string] rangeOfString:needle];
	NSNumber *n;

	if (r.location == NSNotFound) {
		return 0;
	}
	n = [a attribute:NSInlinePresentationIntentAttributeName atIndex:r.location effectiveRange:NULL];
	return (unsigned int)[n unsignedIntValue];
}

static NSPresentationIntent *intent_at(NSAttributedString *a, NSString *needle)
{
	NSRange r = [[a string] rangeOfString:needle];

	if (r.location == NSNotFound) {
		return nil;
	}
	return [a attribute:NSPresentationIntentAttributeName atIndex:r.location effectiveRange:NULL];
}

static id attr_at(NSAttributedString *a, NSAttributedStringKey key, NSUInteger index)
{
	if (index >= [[a string] length]) {
		return nil;
	}
	return [a attribute:key atIndex:index effectiveRange:NULL];
}

static NSString *text_of(NSAttributedString *a)
{
	return [a string];
}

int main(void)
{
	{
		/* TWO OF THE FIVE STARTING VALUES ARE APPLE'S ("The default is NO" for extended attributes, "The
		 * default is nil" for the language code) and three are OURS, stated as such: the full syntax, the
		 * forgiving failure policy, and no source-position attributes. */
		NSAttributedStringMarkdownParsingOptions *options =
			[[NSAttributedStringMarkdownParsingOptions alloc] init];

		check("the-options-start-from-their-two-published-and-three-our-defaults",
		      !options.allowsExtendedAttributes && options.languageCode == nil &&
		      options.interpretedSyntax == NSAttributedStringMarkdownInterpretedSyntaxFull &&
		      options.failurePolicy ==
			NSAttributedStringMarkdownParsingFailureReturnPartiallyParsedIfPossible &&
		      !options.appliesSourcePositionAttributes,
		      [NSString stringWithFormat:@"extended=%d syntax=%d failure=%d language=%@ positionAttributes=%d",
			(int)options.allowsExtendedAttributes, (int)options.interpretedSyntax,
			(int)options.failurePolicy, options.languageCode,
			(int)options.appliesSourcePositionAttributes]);
	}

	{
		/* THE FIVE PROPERTIES ROUND-TRIP, setters included, because they are readwrite. */
		NSAttributedStringMarkdownParsingOptions *options =
			[[NSAttributedStringMarkdownParsingOptions alloc] init];

		options.allowsExtendedAttributes = YES;
		options.appliesSourcePositionAttributes = YES;
		options.interpretedSyntax = NSAttributedStringMarkdownInterpretedSyntaxInlineOnly;
		options.failurePolicy = NSAttributedStringMarkdownParsingFailureReturnError;
		options.languageCode = @"fr";
		check("the-options-properties-round-trip",
		      options.allowsExtendedAttributes && options.appliesSourcePositionAttributes &&
		      options.interpretedSyntax == NSAttributedStringMarkdownInterpretedSyntaxInlineOnly &&
		      options.failurePolicy == NSAttributedStringMarkdownParsingFailureReturnError &&
		      [options.languageCode isEqualToString:@"fr"],
		      [NSString stringWithFormat:@"syntax=%d failure=%d language=%@",
			(int)options.interpretedSyntax, (int)options.failurePolicy, options.languageCode]);
	}

	{
		/* A COPY IS A SNAPSHOT, AND THE LANGUAGE CODE IS COPIED RATHER THAN BORROWED: a MUTABLE source is
		 * handed over and then mutated, so an assigning setter would answer the mutated string. */
		NSAttributedStringMarkdownParsingOptions *options =
			[[NSAttributedStringMarkdownParsingOptions alloc] init];
		NSMutableString *source = [[NSMutableString alloc] initWithString:@"before"];
		NSAttributedStringMarkdownParsingOptions *copy;

		options.languageCode = source;
		options.interpretedSyntax = NSAttributedStringMarkdownInterpretedSyntaxInlineOnly;
		copy = [options copy];
		[source appendString:@"-mutated"];
		options.interpretedSyntax = NSAttributedStringMarkdownInterpretedSyntaxFull;
		check("a-copy-is-a-snapshot-and-the-language-code-is-copied",
		      copy != options &&
		      [[copy languageCode] isEqualToString:@"before"] &&
		      [copy interpretedSyntax] == NSAttributedStringMarkdownInterpretedSyntaxInlineOnly &&
		      [options interpretedSyntax] == NSAttributedStringMarkdownInterpretedSyntaxFull,
		      [NSString stringWithFormat:@"copyLanguage=%@ copySyntax=%d originalSyntax=%d",
			[copy languageCode], (int)[copy interpretedSyntax], (int)[options interpretedSyntax]]);
	}

	{
		/* THE FIVE CASES ARE DISTINCT WITHIN THEIR SETS: two names sharing a value would make two policies
		 * indistinguishable to a caller that stored one. */
		check("the-two-enums-are-distinct-within-themselves",
		      NSAttributedStringMarkdownInterpretedSyntaxFull !=
			NSAttributedStringMarkdownInterpretedSyntaxInlineOnly &&
		      NSAttributedStringMarkdownInterpretedSyntaxInlineOnly !=
			NSAttributedStringMarkdownInterpretedSyntaxInlineOnlyPreservingWhitespace &&
		      NSAttributedStringMarkdownInterpretedSyntaxFull !=
			NSAttributedStringMarkdownInterpretedSyntaxInlineOnlyPreservingWhitespace &&
		      NSAttributedStringMarkdownParsingFailureReturnError !=
			NSAttributedStringMarkdownParsingFailureReturnPartiallyParsedIfPossible,
		      @"the three syntax values and the two failure policies are pairwise distinct");
	}

	{
		/* THE POSITION IS FOUR NUMBERS AND NOTHING ELSE — kept, not interpreted. */
		NSAttributedStringMarkdownSourcePosition *position =
			[[NSAttributedStringMarkdownSourcePosition alloc] initWithStartLine:2
									 startColumn:3
									     endLine:4
									   endColumn:5];
		NSAttributedStringMarkdownSourcePosition *copy = [position copy];

		check("a-source-position-keeps-its-four-numbers",
		      [position startLine] == 2 && [position startColumn] == 3 &&
		      [position endLine] == 4 && [position endColumn] == 5,
		      [NSString stringWithFormat:@"(%ld,%ld)-(%ld,%ld)", (long)[position startLine],
			(long)[position startColumn], (long)[position endLine], (long)[position endColumn]]);
		check("a-source-position-copy-is-a-snapshot",
		      copy != position && [copy startLine] == 2 && [copy endLine] == 4,
		      [NSString stringWithFormat:@"copy=(%ld,%ld)-(%ld,%ld)", (long)[copy startLine],
			(long)[copy startColumn], (long)[copy endLine], (long)[copy endColumn]]);
	}

	{
		/* ONE LINE, END-EXCLUSIVE: bytes 1..5 of line 1 are "hello", and column 6 is the byte after it — so
		 * the range is (0, 5) and NOT (0, 6). That is OURS (Apple publishes the method's purpose and not its
		 * inclusivity), and it is asserted so that a later change to it is a deliberate one. */
		NSString *text = @"hello world\nsecond line\n";
		NSAttributedStringMarkdownSourcePosition *position =
			[[NSAttributedStringMarkdownSourcePosition alloc] initWithStartLine:1
									 startColumn:1
									     endLine:1
									   endColumn:6];
		NSRange range = [position rangeInString:text];

		check("a-range-covers-the-line-between-the-two-columns",
		      range.location == 0 && range.length == 5 &&
		      [[text substringWithRange:range] isEqualToString:@"hello"],
		      [NSString stringWithFormat:@"range=(%lu,%lu) substring=%@",
			(unsigned long)range.location, (unsigned long)range.length,
			[text substringWithRange:range]]);
	}

	{
		/* ACROSS LINES: from the last character of line 1 to the sixth of line 2 — which is what makes the
		 * range a real conversion rather than an offset. */
		NSString *text = @"hello world\nsecond line\n";
		NSAttributedStringMarkdownSourcePosition *position =
			[[NSAttributedStringMarkdownSourcePosition alloc] initWithStartLine:1
									 startColumn:7
									     endLine:2
									   endColumn:7];
		NSRange range = [position rangeInString:text];

		check("a-range-can-span-two-lines",
		      [[text substringWithRange:range] isEqualToString:@"world\nsecond"],
		      [NSString stringWithFormat:@"range=(%lu,%lu) substring=%@",
			(unsigned long)range.location, (unsigned long)range.length,
			[text substringWithRange:range]]);
	}

	{
		/* THE CHECK THAT HAS TEETH: A COLUMN IS A UTF-8 BYTE, NOT A CHARACTER. In "héllo" the é is TWO bytes,
		 * so byte 5 is the LAST 'o'... which is to say the range from column 1 to column 6 covers four
		 * UTF-16 units — "héll" — where a character-counting implementation would answer five ("héllo"). */
		NSString *text = @"héllo";
		NSAttributedStringMarkdownSourcePosition *position =
			[[NSAttributedStringMarkdownSourcePosition alloc] initWithStartLine:1
									 startColumn:1
									     endLine:1
									   endColumn:6];
		NSRange range = [position rangeInString:text];

		check("a-column-is-a-utf8-byte-and-not-a-character",
		      range.length == 4 && [[text substringWithRange:range] isEqualToString:@"héll"],
		      [NSString stringWithFormat:@"length=%lu (want 4) substring=%@ (want héll)",
			(unsigned long)range.length, [text substringWithRange:range]]);
	}

	{
		/* A POSITION PAST THE END IS CLAMPED RATHER THAN REFUSED, and the answer is a range INSIDE the
		 * string — the property a caller relies on when it hands the object a string that was edited. */
		NSString *text = @"one\ntwo\n";
		NSAttributedStringMarkdownSourcePosition *position =
			[[NSAttributedStringMarkdownSourcePosition alloc] initWithStartLine:9
									 startColumn:1
									     endLine:9
									   endColumn:5];
		NSRange range = [position rangeInString:text];

		check("a-position-past-the-end-is-clamped",
		      range.location + range.length <= [text length] && range.length == 0,
		      [NSString stringWithFormat:@"range=(%lu,%lu) length=%lu",
			(unsigned long)range.location, (unsigned long)range.length, (unsigned long)[text length]]);
	}

	/* ===================== §62.107: THE IMPORTER'S CHECKS ===================== */
	{
		NSError *err = nil;
		NSAttributedStringMarkdownParsingOptions *opts;
		NSAttributedString *a;
		NSPresentationIntent *intent;
		NSString *s;

		/* 1. A HEADER AND A PARAGRAPH: the block intents, with the header's own level. */
		a = [[NSAttributedString alloc] initWithMarkdownString:@"# Title\n\nBody text\n"
							       options:nil
							       baseURL:nil
								 error:&err];
		intent = intent_at(a, @"Title");
		check("header-intent-and-level",
		      intent != nil && [intent intentKind] == NSPresentationIntentKindHeader &&
		      [intent headerLevel] == 1 &&
		      [text_of(a) isEqualToString:@"Title\nBody text\n"],
		      [NSString stringWithFormat:@"text=%@ kind=%ld level=%ld",
		       text_of(a), (long)(intent ? [intent intentKind] : -1),
		       (long)(intent ? [intent headerLevel] : -1)]);

		/* 2. THE INLINE INTENTS: each construct's own bit, and only that bit. */
		a = [[NSAttributedString alloc] initWithMarkdownString:@"**b** *i* `c` ~~s~~\n"
							       options:nil baseURL:nil error:&err];
		check("inline-intent-bits",
		      bits_at(a, @"b") == NSInlinePresentationIntentStronglyEmphasized &&
		      bits_at(a, @"i") == NSInlinePresentationIntentEmphasized &&
		      bits_at(a, @"c") == NSInlinePresentationIntentCode &&
		      bits_at(a, @"s") == NSInlinePresentationIntentStrikethrough &&
		      [text_of(a) isEqualToString:@"b i c s\n"],
		      [NSString stringWithFormat:@"strong=%u em=%u code=%u strike=%u text=%@",
		       bits_at(a, @"b"), bits_at(a, @"i"), bits_at(a, @"c"), bits_at(a, @"s"), text_of(a)]);

		/* 3. A LINK, AND THE DOOR'S baseURL: an absolute destination, then a RELATIVE one resolved by
		 * the base. The second half is the check that makes baseURL a parameter rather than a comment. */
		a = [[NSAttributedString alloc] initWithMarkdownString:@"[abs](https://e.com/x) [rel](b)\n"
							       options:nil
							       baseURL:[NSURL URLWithString:@"https://e.com/a/"]
								 error:&err];
		{
			NSRange relRange = [text_of(a) rangeOfString:@"rel"];
			NSUInteger relIndex = relRange.location;
			NSURL *abs = attr_at(a, NSLinkAttributeName, 1);
			NSURL *rel = attr_at(a, NSLinkAttributeName, relIndex);

			check("link-attributes-and-base-url",
			      [abs isKindOfClass:[NSURL class]] &&
			      [[abs absoluteString] isEqualToString:@"https://e.com/x"] &&
			      [rel isKindOfClass:[NSURL class]] &&
			      [[rel absoluteString] isEqualToString:@"https://e.com/a/b"],
			      [NSString stringWithFormat:@"abs=%@ rel=%@ text=%@", [abs absoluteString],
			       [rel absoluteString], text_of(a)]);
		}

		/* 4. A FENCED CODE BLOCK: the intent, the language hint, and NO inline parsing inside it. */
		a = [[NSAttributedString alloc] initWithMarkdownString:@"```sh\n*a*\n```\n"
							       options:nil baseURL:nil error:&err];
		intent = intent_at(a, @"*a*");
		check("code-block-language-and-literal-content",
		      intent != nil && [intent intentKind] == NSPresentationIntentKindCodeBlock &&
		      [[intent languageHint] isEqualToString:@"sh"] &&
		      bits_at(a, @"*a*") == 0 &&
		      [text_of(a) isEqualToString:@"*a*\n"],
		      [NSString stringWithFormat:@"kind=%ld lang=%@ inline=%u text=%@",
		       (long)(intent ? [intent intentKind] : -1),
		       intent ? [intent languageHint] : @"(none)", bits_at(a, @"*a*"), text_of(a)]);

		/* 5. A LIST, AND THE INTENT NESTING: the run carries the INNERMOST intent (the paragraph), and the
	 * list and the item above it are reached through parentIntent — which is what that property is for.
	 * The ordinal is the source's own number and the delimiter rides the run. */
	a = [[NSAttributedString alloc] initWithMarkdownString:@"1. one\n2. two\n"
						       options:nil baseURL:nil error:&err];
	{
		NSPresentationIntent *para = intent_at(a, @"two");
		NSPresentationIntent *item = (para != nil) ? [para parentIntent] : nil;
		NSPresentationIntent *list = (item != nil) ? [item parentIntent] : nil;

		check("list-intent-nesting-ordinal-and-delimiter",
		      para != nil && [para intentKind] == NSPresentationIntentKindParagraph &&
		      item != nil && [item intentKind] == NSPresentationIntentKindListItem &&
		      [item ordinal] == 2 &&
		      list != nil && [list intentKind] == NSPresentationIntentKindOrderedList &&
		      [((NSString *)attr_at(a, NSListItemDelimiterAttributeName,
					    [text_of(a) rangeOfString:@"two"].location)) isEqualToString:@"."],
		      [NSString stringWithFormat:@"kind=%ld item=%ld ordinal=%ld parent=%ld delimiter=%@",
		       (long)(para ? [para intentKind] : -1), (long)(item ? [item intentKind] : -1),
		       (long)(item ? [item ordinal] : -1), (long)(list ? [list intentKind] : -1),
		       attr_at(a, NSListItemDelimiterAttributeName,
			       [text_of(a) rangeOfString:@"two"].location)]);
	}

	/* 6. THE REFUSAL, AS AN ABSENCE: a table produces no table intent — and its text stays text. */
		a = [[NSAttributedString alloc] initWithMarkdownString:@"| a | b |\n| - | - |\n"
							       options:nil baseURL:nil error:&err];
		{
			NSUInteger i, n = [[a string] length];
			BOOL anyTable = NO;

			for (i = 0; i < n; i++) {
				NSPresentationIntent *p = [a attribute:NSPresentationIntentAttributeName
							       atIndex:i effectiveRange:NULL];
				if (p != nil && [p intentKind] == NSPresentationIntentKindTable) {
					anyTable = YES;
				}
			}
			check("table-refused-by-name",
			      !anyTable && [[a string] rangeOfString:@"| a | b |"].location != NSNotFound,
			      [NSString stringWithFormat:@"tableIntent=%d text=%@", (int)anyTable, text_of(a)]);
		}

		/* 7. AN IMAGE: its alt text carries Apple's two attributes — and a RELATIVE destination with no
		 * baseURL leaves NO URL behind, which is this library's stated boundary (NSURL refuses relative
		 * references, so there is nothing to put there). */
		a = [[NSAttributedString alloc] initWithMarkdownString:@"![alt](https://e.com/p.png)\n"
							       options:nil baseURL:nil error:&err];
		{
			NSURL *img = attr_at(a, NSImageURLAttributeName, 0);
			NSString *alt = attr_at(a, NSAlternateDescriptionAttributeName, 0);

			check("image-alt-and-url-attributes",
			      [img isKindOfClass:[NSURL class]] &&
			      [[img absoluteString] isEqualToString:@"https://e.com/p.png"] &&
			      [alt isEqualToString:@"alt"] && [text_of(a) isEqualToString:@"alt\n"],
			      [NSString stringWithFormat:@"url=%@ alt=%@ text=%@", [img absoluteString], alt,
			       text_of(a)]);
		}
		a = [[NSAttributedString alloc] initWithMarkdownString:@"![alt](p.png)\n"
							       options:nil baseURL:nil error:&err];
		check("relative-destination-without-a-base-has-no-url",
		      attr_at(a, NSImageURLAttributeName, 0) == nil &&
		      [attr_at(a, NSAlternateDescriptionAttributeName, 0) isEqualToString:@"alt"],
		      [NSString stringWithFormat:@"url=%@", attr_at(a, NSImageURLAttributeName, 0)]);

		/* 8. ENTITIES AND ESCAPES: decoded and unescaped, neither left as source. */
		a = [[NSAttributedString alloc] initWithMarkdownString:@"a &amp; b \\*c\\*\n"
							       options:nil baseURL:nil error:&err];
		check("entities-and-escapes",
		      [text_of(a) isEqualToString:@"a & b *c*\n"],
		      [NSString stringWithFormat:@"text=%@", text_of(a)]);

		/* 9. THE TWO BREAKS, as " + "the two DIFFERENT bits on the two newlines: two trailing spaces are hard and a
	 * plain newline is soft, and neither leaves the spaces in the text. */
	a = [[NSAttributedString alloc] initWithMarkdownString:@"one  \ntwo\nthree\n"
						       options:nil baseURL:nil error:&err];
	{
		NSRange all = NSMakeRange(0, [text_of(a) length]);
		NSRange firstRange = [text_of(a) rangeOfString:@"\n"];
		NSRange secondRange = [text_of(a) rangeOfString:@"\n" options:0
								     range:NSMakeRange(firstRange.location + 1,
										       all.length - firstRange.location - 1)];
		NSUInteger first = firstRange.location;
		NSUInteger second = secondRange.location;
		NSNumber *hard = (first != NSNotFound) ?
			[a attribute:NSInlinePresentationIntentAttributeName atIndex:first effectiveRange:NULL] : nil;
		NSNumber *soft = (second < [text_of(a) length]) ?
			[a attribute:NSInlinePresentationIntentAttributeName atIndex:second effectiveRange:NULL] : nil;

		check("hard-break-and-soft-break-bits",
		      [text_of(a) isEqualToString:@"one\ntwo\nthree\n"] &&
		      ([hard unsignedIntValue] & NSInlinePresentationIntentLineBreak) != 0 &&
		      ([soft unsignedIntValue] & NSInlinePresentationIntentSoftBreak) != 0,
		      [NSString stringWithFormat:@"text=%@ hard=%u soft=%u", text_of(a),
		       [hard unsignedIntValue], [soft unsignedIntValue]]);
	}

	/* 10. THE FAILURE POLICIES, OBSERVED DIFFERING on a fence that never closes. */
		opts = [[NSAttributedStringMarkdownParsingOptions alloc] init];
		[opts setFailurePolicy:NSAttributedStringMarkdownParsingFailureReturnError];
		err = nil;
		a = [[NSAttributedString alloc] initWithMarkdownString:@"```\nunclosed\n"
							       options:opts baseURL:nil error:&err];
		{
			NSAttributedString *strict = a;

			[opts setFailurePolicy:NSAttributedStringMarkdownParsingFailureReturnPartiallyParsedIfPossible];
			a = [[NSAttributedString alloc] initWithMarkdownString:@"```\nunclosed\n"
								       options:opts baseURL:nil error:&err];
			check("failure-policy-difference",
			      strict == nil && err != nil && a != nil &&
			      [[a string] isEqualToString:@"unclosed\n"],
			      [NSString stringWithFormat:@"strict=%s error=%s partial=%@",
			       strict ? "non-nil" : "nil", err ? "yes" : "no", text_of(a)]);
		}

		/* 11. InlineOnly VERSUS InlineOnlyPreservingWhitespace: the block markers stay literal in both, and the
	 * difference is the whitespace the second one keeps — the first collapses every run of it, and so also
	 * drops the document's trailing one. */
	opts = [[NSAttributedStringMarkdownParsingOptions alloc] init];
	[opts setInterpretedSyntax:NSAttributedStringMarkdownInterpretedSyntaxInlineOnly];
	a = [[NSAttributedString alloc] initWithMarkdownString:@"#  H\n\nx\n"
						       options:opts baseURL:nil error:&err];
	{
		NSAttributedString *collapsed = a;

		[opts setInterpretedSyntax:
			NSAttributedStringMarkdownInterpretedSyntaxInlineOnlyPreservingWhitespace];
		a = [[NSAttributedString alloc] initWithMarkdownString:@"#  H\n\nx\n"
						       options:opts baseURL:nil error:&err];
		check("inline-only-syntaxes",
		      intent_at(collapsed, @"H") == nil &&
		      [[collapsed string] isEqualToString:@"# H x"] &&
		      [[a string] isEqualToString:@"#  H\n\nx\n"],
		      [NSString stringWithFormat:@"collapsed=[%@] preserved=[%@]",
		       text_of(collapsed), text_of(a)]);
	}

	/* 12. THE SOURCE POSITIONS, on the option that asks for them: 1-based lines, and the second
		 * paragraph really is on line 3. */
		opts = [[NSAttributedStringMarkdownParsingOptions alloc] init];
		[opts setAppliesSourcePositionAttributes:YES];
		a = [[NSAttributedString alloc] initWithMarkdownString:@"a\n\nb\n"
							       options:opts baseURL:nil error:&err];
		{
			NSAttributedStringMarkdownSourcePosition *first = attr_at(a, NSMarkdownSourcePositionAttributeName, 0);
			NSAttributedStringMarkdownSourcePosition *second =
				attr_at(a, NSMarkdownSourcePositionAttributeName,
					[[a string] rangeOfString:@"b"].location);

			check("source-position-attributes",
			      first != nil && [first startLine] == 1 &&
			      second != nil && [second startLine] == 3,
			      [NSString stringWithFormat:@"first=%ld second=%ld",
			       (long)(first ? [first startLine] : -1), (long)(second ? [second startLine] : -1)]);
		}

		/* 13. EXTENDED ATTRIBUTES: OFF BY DEFAULT, and then the marker is not interpreted — note what the
	 * uninterpreted form IS, because CommonMark says so: "^" stays literal and the [x](...) half is an
	 * ordinary LINK, which is exactly the syntax Apple's extended attributes extend. When they are on,
	 * a numeric value arrives as a NUMBER. */
	a = [[NSAttributedString alloc] initWithMarkdownString:@"^[x](color: red, n: 3)\n"
						       options:nil baseURL:nil error:&err];
	{
		NSAttributedString *off = a;

		opts = [[NSAttributedStringMarkdownParsingOptions alloc] init];
		[opts setAllowsExtendedAttributes:YES];
		a = [[NSAttributedString alloc] initWithMarkdownString:@"^[x](color: red, n: 3)\n"
						       options:opts baseURL:nil error:&err];
		check("extended-attributes",
		      [[off string] isEqualToString:@"^x\n"] &&
		      attr_at(off, @"color", 0) == nil &&
		      [attr_at(a, @"color", 0) isEqualToString:@"red"] &&
		      [attr_at(a, @"n", 0) intValue] == 3,
		      [NSString stringWithFormat:@"off=[%@] color=%@ n=%@", text_of(off),
		       attr_at(a, @"color", 0), attr_at(a, @"n", 0)]);
	}

	/* 14. THE LANGUAGE CODE, as the one attribute Apple's own words tie to it. */
		opts = [[NSAttributedStringMarkdownParsingOptions alloc] init];
		[opts setLanguageCode:@"fr"];
		a = [[NSAttributedString alloc] initWithMarkdownString:@"bonjour\n"
							       options:opts baseURL:nil error:&err];
		s = attr_at(a, NSLanguageIdentifierAttributeName, 0);
		check("language-code-attribute",
		      [s isEqualToString:@"fr"],
		      [NSString stringWithFormat:@"language=%@", s]);
	}

	/* ================= §62.108: THE STRING-TABLE DOORS AND BOTH MACRO FAMILIES ================= */
	{
		NSBundle *main = [NSBundle mainBundle];
		NSAttributedString *attributed;
		NSString *plain;

		/* 26. THE CLASSIC DOOR'S TWO FALLBACKS, which are Apple's rule: the value when it is non-empty,
		 * and THE KEY ITSELF when it is not. This door is also the repair of a real defect: the four
		 * NSLocalizedString macros have shipped for a long time calling a method no header declared. */
		plain = [main localizedStringForKey:@"FN_NO_SUCH_KEY" value:@"a fallback" table:nil];
		{
			NSString *empty = [main localizedStringForKey:@"FN_NO_SUCH_KEY" value:@"" table:nil];

			check("localized-string-door-fallbacks",
			      main != nil && [plain isEqualToString:@"a fallback"] &&
			      [empty isEqualToString:@"FN_NO_SUCH_KEY"],
			      [NSString stringWithFormat:@"value=%@ key=%@", plain, empty]);
		}

		/* 27. THE ATTRIBUTED DOOR: the same lookup, and the value PARSED AS MARKDOWN — the bit that
		 * makes it a different door rather than a second name for the first. */
		/* NOTE THE TRAILING NEWLINE IN WHAT FOLLOWS, because it is the convention rather than an
		 * accident: the value is parsed as MARKDOWN, Full syntax emits one block per line, and a block
		 * ends with "\n" — so a localized attributed string arrives with it. */
		attributed = [main localizedAttributedStringForKey:@"FN_NO_SUCH_KEY"
							     value:@"plain **bold** text"
							     table:nil];
		check("localized-attributed-door-parses-markdown",
		      [[attributed string] isEqualToString:@"plain bold text\n"] &&
		      bits_at(attributed, @"bold") == NSInlinePresentationIntentStronglyEmphasized,
		      [NSString stringWithFormat:@"text=%@ bits=%u", [attributed string],
		       bits_at(attributed, @"bold")]);

		/* 28. THE FOUR MACROS, all four held to the door's own behaviour: three answer the KEY (their
		 * value argument is empty) and the WITH-DEFAULT one answers its value — parsed, which is the
		 * whole point of the family. */
		{
			NSAttributedString *one = NSLocalizedAttributedString(@"FN_NO_SUCH_KEY", nil);
			NSAttributedString *two = NSLocalizedAttributedStringFromTable(@"FN_NO_SUCH_KEY", nil, nil);
			NSAttributedString *three = NSLocalizedAttributedStringFromTableInBundle(
				@"FN_NO_SUCH_KEY", nil, [NSBundle mainBundle], nil);
			NSAttributedString *four = NSLocalizedAttributedStringWithDefaultValue(
				@"FN_NO_SUCH_KEY", nil, [NSBundle mainBundle], @"an *emphasized* default", nil);

			check("localized-attributed-macros",
			      [[one string] isEqualToString:@"FN_NO_SUCH_KEY\n"] &&
			      [[two string] isEqualToString:@"FN_NO_SUCH_KEY\n"] &&
			      [[three string] isEqualToString:@"FN_NO_SUCH_KEY\n"] &&
			      [[four string] isEqualToString:@"an emphasized default\n"] &&
			      bits_at(four, @"emphasized") == NSInlinePresentationIntentEmphasized,
			      [NSString stringWithFormat:@"one=%@ two=%@ three=%@ four=%@ %u",
			       [one string], [two string], [three string], [four string],
			       bits_at(four, @"emphasized")]);
		}

		/* 29. AND THE CLASSIC MACROS, which this unit repaired: the same two fallbacks through the
		 * macros themselves, since a macro whose door is missing compiles into an unrecognised selector. */
		{
			NSString *keyFallback = NSLocalizedString(@"FN_NO_SUCH_KEY", nil);
			NSString *valueFallback = NSLocalizedStringWithDefaultValue(@"FN_NO_SUCH_KEY", nil,
				[NSBundle mainBundle], @"a default", nil);

			check("localized-string-macros",
			      [keyFallback isEqualToString:@"FN_NO_SUCH_KEY"] &&
			      [valueFallback isEqualToString:@"a default"],
			      [NSString stringWithFormat:@"key=%@ value=%@", keyFallback, valueFallback]);
		}
	}

	printf("FOUNDATION-MARKDOWN RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-MARKDOWN DONE\n");
	return failc == 0 ? 0 : 1;
}
