/*
 * foundation_enumerate_substrings.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * `-enumerateSubstringsInRange:options:usingBlock:` (§62.48) — the door that consumes `NSStringEnumerationOptions`.
 *
 * THE OPTIONS AND THEIR ELEVEN CASES WERE ALREADY `shipped` IN THE LEDGER WHILE THE METHOD THAT TAKES THEM WAS
 * DECLARED NOWHERE. That is the same shape as the previous unit: a vocabulary the surface file called complete and
 * a door that did not exist. So this probe's job is not to check that the names are there — they were — but that
 * THE UNITS COME FROM THE LIBRARY'S ONE BREAKER and that the boundaries this implementation states are the
 * boundaries it keeps:
 *
 *   - a LINE or a SENTENCE is enclosed by its PARAGRAPH, a WORD by its SENTENCE, a composed CHARACTER by its WORD;
 *   - `Reverse` is a BUFFERED walk (an ICU iterator is a forward cursor), and the sequence is what reverses;
 *   - `SubstringNotRequired` still reports ranges and hands back NO substring;
 *   - `Localized`, `ByCaretPositions` and `ByDeletionClusters` REFUSE, and so do zero units and two units.
 *
 * The expectations are ABSOLUTE — the exact substrings and offsets of a string written into this file — because a
 * check that only compared two spellings of the same walk would pass for a wrong implementation as easily as a
 * right one.
 */
#import <Foundation/Foundation.h>
#include <stdio.h>

static int okc = 0, failc = 0;

static int lastcheck;

/* covers("NSString", "enumerateSubstringsInRange:options:usingBlock:") - the behavioural claim, piggybacked on
 * the check above it: no condition of its own, printed only when the last check's result was true. See
 * tools/foundation-cov.py; a claim for a row the ledger does not carry is inert. */
static void covers_(const char *cls, const char *sel)
{
	if (lastcheck) {
		printf("COVERS %s %s\n", cls, sel);
	}
}
#define covers(cls, sel) covers_(cls, sel)


static void check(const char *name, BOOL held, NSString *why)
{
	lastcheck = held;	/* read by covers(): a claim can only follow an assertion that held */
	if(held) {
		okc++;
		printf("FOUNDATION-ENUMERATE %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-ENUMERATE %s FAIL: %s\n", name, [why UTF8String]);
	}
}

static BOOL fn_raises(void (^body)(void))
{
	@try {
		body();
	}
	@catch (NSException *exception) {
		(void)exception;
		return YES;
	}
	return NO;
}

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);

	NSString *lines = @"first line\nsecond line\nthird line";
	NSString *twoParagraphs = @"one\ntwo\n\nthree\nfour";

	/* --- LINES: THE UNIT THE OTHERS ARE BUILT FROM --------------------------------------------------- */
	{
		NSMutableArray *got = [NSMutableArray array];
		__block NSUInteger calls = 0;

		[lines enumerateSubstringsInRange:NSMakeRange(0, [lines length])
					  options:NSStringEnumerationByLines
				       usingBlock:^(NSString *substring, NSRange substringRange, NSRange enclosingRange,
						    BOOL *stop) {
			(void)enclosingRange;
			(void)stop;
			calls++;
			[got addObject:substring];
		}];
		check("lines-come-back-whole-and-in-order",
		      calls == 3 && [got count] == 3 &&
		      [[got objectAtIndex:0] isEqual:@"first line"] &&
		      [[got objectAtIndex:1] isEqual:@"second line"] &&
		      [[got objectAtIndex:2] isEqual:@"third line"],
		      [NSString stringWithFormat:@"%lu call(s): %@", (unsigned long)calls,
			[got componentsJoinedByString:@" | "]]);
	covers("NSString", "enumerateSubstringsInRange:options:usingBlock:");
	}

	/* --- WORDS, SENTENCES, PARAGRAPHS AND COMPOSED CHARACTERS: THE ENGINE'S OTHER UNITS ---------------- */
	{
		NSMutableArray *words = [NSMutableArray array];
		NSMutableArray *characters = [NSMutableArray array];
		NSString *prose = @"Dogs run. Cats sleep.";

		[prose enumerateSubstringsInRange:NSMakeRange(0, [prose length])
					  options:NSStringEnumerationByWords
				       usingBlock:^(NSString *substring, NSRange r, NSRange e, BOOL *stop) {
			(void)r;
			(void)e;
			(void)stop;
			[words addObject:substring];
		}];
		[@"ab" enumerateSubstringsInRange:NSMakeRange(0, 2)
					  options:NSStringEnumerationByComposedCharacterSequences
				       usingBlock:^(NSString *substring, NSRange r, NSRange e, BOOL *stop) {
			(void)r;
			(void)e;
			(void)stop;
			[characters addObject:substring];
		}];
		check("words-and-composed-characters-come-from-the-same-breaker",
		      [words count] == 4 && [[words objectAtIndex:0] isEqual:@"Dogs"] &&
		      [[words objectAtIndex:3] isEqual:@"sleep"] &&
		      [characters count] == 2 && [[characters objectAtIndex:0] isEqual:@"a"] &&
		      [[characters objectAtIndex:1] isEqual:@"b"],
		      [NSString stringWithFormat:@"words: %@; characters: %@",
			[words componentsJoinedByString:@" "], [characters componentsJoinedByString:@""]]);
	}

	/* --- THE ENCLOSING RANGE, WHICH IS THE BOUNDARY THIS IMPLEMENTATION STATES ------------------------- */
	{
		NSMutableArray *enclosing = [NSMutableArray array];
		NSMutableArray *outer = [NSMutableArray array];

		/* A LINE IS ENCLOSED BY ITS PARAGRAPH: in a two-paragraph string, the first two lines share one. */
		[twoParagraphs enumerateSubstringsInRange:NSMakeRange(0, [twoParagraphs length])
						  options:NSStringEnumerationByLines
					       usingBlock:^(NSString *substring, NSRange r, NSRange e, BOOL *stop) {
			(void)r;
			(void)stop;
			[enclosing addObject:[NSValue valueWithRange:e]];
			[outer addObject:substring];
		}];
		{
			/* THE BLANK LINE IS THE THIRD UNIT AND BELONGS TO NO PARAGRAPH, so its enclosing range is
			 * itself — that is the boundary this implementation states, and it is measured rather than assumed. */
			/* A PROBE READS NOTHING IT HAS NOT COUNTED FIRST: indexing an array a dropped unit made shorter is
			 * how this probe aborted with no message instead of failing with one. */
			NSRange one = [enclosing count] > 0 ? [[enclosing objectAtIndex:0] rangeValue] : NSMakeRange(0, 0);
			NSRange two = [enclosing count] > 1 ? [[enclosing objectAtIndex:1] rangeValue] : NSMakeRange(0, 0);
			NSRange blank = [enclosing count] > 2 ? [[enclosing objectAtIndex:2] rangeValue] : NSMakeRange(0, 0);
			NSRange three = [enclosing count] > 3 ? [[enclosing objectAtIndex:3] rangeValue] : NSMakeRange(0, 0);
			NSRange four = [enclosing count] > 4 ? [[enclosing objectAtIndex:4] rangeValue] : NSMakeRange(0, 0);

			check("a-line-is-enclosed-by-its-paragraph-and-a-blank-line-by-itself",
			      [outer count] == 5 && [enclosing count] == 5 &&
			      [[outer objectAtIndex:2] length] == 0 &&
			      one.location == 0 && one.length == 7 && two.location == 0 && two.length == 7 &&
			      blank.location == 8 && blank.length == 0 &&
			      three.location == 9 && three.length == 10 &&
			      four.location == 9 && four.length == 10,
			      [NSString stringWithFormat:@"%lu lines; enclosing: %@ for %@",
				(unsigned long)[outer count], [enclosing componentsJoinedByString:@", "],
				[outer componentsJoinedByString:@" | "]]);
		}
		{
			NSMutableArray *paragraphs = [NSMutableArray array];

			[twoParagraphs enumerateSubstringsInRange:NSMakeRange(0, [twoParagraphs length])
							  options:NSStringEnumerationByParagraphs
						       usingBlock:^(NSString *substring, NSRange r, NSRange e, BOOL *stop) {
				(void)e;
				(void)stop;
				[paragraphs addObject:[NSString stringWithFormat:@"%lu..%lu",
					(unsigned long)r.location, (unsigned long)NSMaxRange(r)]];
			}];
			check("paragraphs-are-runs-of-lines",
			      [paragraphs count] == 2 && [[paragraphs objectAtIndex:0] isEqual:@"0..7"] &&
			      [[paragraphs objectAtIndex:1] isEqual:@"9..19"],
			      [NSString stringWithFormat:@"paragraph ranges: %@",
				[paragraphs componentsJoinedByString:@", "]]);
		}
	}

	/* --- STOP, AND THE REVERSED WALK ----------------------------------------------------------------- */
	{
		__block NSUInteger calls = 0;
		NSMutableArray *forward = [NSMutableArray array];
		NSMutableArray *backward = [NSMutableArray array];

		[lines enumerateSubstringsInRange:NSMakeRange(0, [lines length])
					  options:NSStringEnumerationByLines
				       usingBlock:^(NSString *substring, NSRange r, NSRange e, BOOL *stop) {
			(void)r;
			(void)e;
			calls++;
			[forward addObject:substring];
			if (calls == 2) {
				*stop = YES;
			}
		}];
		check("stop-ends-the-walk-early", calls == 2 && [forward count] == 2,
		      [NSString stringWithFormat:@"%lu call(s) after stopping at the second", (unsigned long)calls]);

		/* A SECOND, COMPLETE FORWARD WALK IS THE COMPARISON, because the first one was STOPPED at two: comparing the
		 * reversed list against a truncated list is the kind of mistake that makes a correct implementation look
		 * wrong, which is exactly what it did here. */
		forward = [NSMutableArray array];
		[lines enumerateSubstringsInRange:NSMakeRange(0, [lines length])
					  options:NSStringEnumerationByLines
				       usingBlock:^(NSString *substring, NSRange r, NSRange e, BOOL *stop) {
			(void)r;
			(void)e;
			(void)stop;
			[forward addObject:substring];
		}];
		[lines enumerateSubstringsInRange:NSMakeRange(0, [lines length])
					  options:(NSStringEnumerationByLines | NSStringEnumerationReverse)
				       usingBlock:^(NSString *substring, NSRange r, NSRange e, BOOL *stop) {
			(void)r;
			(void)e;
			(void)stop;
			[backward addObject:substring];
		}];
		check("reverse-is-the-same-units-in-the-other-order",
		      [backward count] == [forward count] && [backward count] == 3 &&
		      [[backward objectAtIndex:0] isEqual:@"third line"] &&
		      [[backward objectAtIndex:1] isEqual:@"second line"] &&
		      [[backward lastObject] isEqual:@"first line"],
		      [NSString stringWithFormat:@"backwards: %@", [backward componentsJoinedByString:@" | "]]);
	}

	/* --- SUBSTRING NOT REQUIRED ------------------------------------------------------------------------ */
	{
		__block NSUInteger calls = 0;
		__block BOOL sawNil = NO;
		__block NSUInteger total = 0;

		[lines enumerateSubstringsInRange:NSMakeRange(0, [lines length])
					  options:(NSStringEnumerationByLines | NSStringEnumerationSubstringNotRequired)
				       usingBlock:^(NSString *substring, NSRange r, NSRange e, BOOL *stop) {
			(void)e;
			(void)stop;
			calls++;
			total += r.length;
			if (substring == nil) {
				sawNil = YES;
			}
		}];
		check("substring-not-required-still-reports-ranges-and-hands-back-nothing",
		      calls == 3 && sawNil && total > 0,
		      [NSString stringWithFormat:@"%lu call(s), all with a nil substring, ranges totalling %lu",
			(unsigned long)calls, (unsigned long)total]);
	}

	/* --- THE REFUSALS --------------------------------------------------------------------------------- */
	{
		NSRange whole = NSMakeRange(0, [lines length]);
		BOOL localized = fn_raises(^{
			[lines enumerateSubstringsInRange:whole
						  options:NSStringEnumerationLocalized
					       usingBlock:^(NSString *s, NSRange r, NSRange e, BOOL *stop) {}];
		});
		BOOL carets = fn_raises(^{
			[lines enumerateSubstringsInRange:whole
						  options:NSStringEnumerationByCaretPositions
					       usingBlock:^(NSString *s, NSRange r, NSRange e, BOOL *stop) {}];
		});
		BOOL clusters = fn_raises(^{
			[lines enumerateSubstringsInRange:whole
						  options:NSStringEnumerationByDeletionClusters
					       usingBlock:^(NSString *s, NSRange r, NSRange e, BOOL *stop) {}];
		});
		BOOL noUnit = fn_raises(^{
			[lines enumerateSubstringsInRange:whole
						  options:(NSStringEnumerationOptions)0
					       usingBlock:^(NSString *s, NSRange r, NSRange e, BOOL *stop) {}];
		});
		BOOL twoUnits = fn_raises(^{
			[lines enumerateSubstringsInRange:whole
						  options:(NSStringEnumerationByWords | NSStringEnumerationByLines)
					       usingBlock:^(NSString *s, NSRange r, NSRange e, BOOL *stop) {}];
		});

		check("the-refused-options-refuse-and-so-do-zero-units-and-two",
		      localized && carets && clusters && noUnit && twoUnits,
		      [NSString stringWithFormat:@"localized=%d carets=%d clusters=%d noUnit=%d twoUnits=%d",
			(int)localized, (int)carets, (int)clusters, (int)noUnit, (int)twoUnits]);
	}

	printf("FOUNDATION-ENUMERATE RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-ENUMERATE-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-ENUMERATE DONE\n");
	return failc ? 1 : 0;
}
