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

	printf("FOUNDATION-MARKDOWN RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-MARKDOWN DONE\n");
	return failc == 0 ? 0 : 1;
}
