/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_string, unit 2 of 2 — the checks (ARC, the house default).
 *
 * The checks are grouped by what they prove:
 *   tiny        a SHORT literal is a tagged pointer, decoded from the tag
 *   owned       a LONG literal and an owned string are the same value
 *   mixed       a TAGGED string and an owned one are the same value (and hash)
 *   utf8        -length counts BYTES, -characterCount characters
 *   mutable     mutation works, and -copy is a snapshot
 *   description the root class's, an override, and a string's own
 *   cross-tu    a constant string that came from the other unit
 */

#import "foundation_string.h"
#include <stdio.h>
#include <string.h>
#import <objc/runtime.h>
#include <string.h>
#include <stdlib.h>		/* malloc, for the ...Characters: ownership check below */

static int okc, failc;

static void check(const char *name, int ok, const char *detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-STRING %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-STRING %s FAIL %s\n", name, detail ? detail : "");
	}
}

int main(void)
{
	/* A short literal is not an object: clang packs it (tag 4), and the class
	 * the Foundation registers at that tag decodes it. */
	{
		NSString *c = @"hello";

		check("tiny", [c length] == 5 && [c characterCount] == 5 &&
		      strcmp([c UTF8String], "hello") == 0 &&
		      [c isKindOfClass:[NSString class]],
		      "a 5-character literal decodes from its tag");
	}

	/* The object path, and value semantics across the two representations. */
	{
		NSString *big = @"this is long enough";
		NSString *owned = [NSString stringWithUTF8String:"this is long enough"];

		/* The length is DERIVED, not hand-counted: an off-by-one in a test
		 * constant is a test bug that looks like a library bug. */
		check("owned", [big length] == strlen("this is long enough") &&
		      [big isEqualToString:owned] && [owned isEqualToString:big] &&
		      [big hash] == [owned hash],
		      "a long literal (an object, not a tag) equals an owned string");
	}

	{
		NSString *tagged = @"hello";
		NSString *owned = [NSString stringWithUTF8String:"hello"];

		check("mixed", [tagged isEqualToString:owned] &&
		      [owned isEqualToString:tagged] && [tagged hash] == [owned hash],
		      "a TAGGED string and an owned one are one value");
	}

	/* The documented UTF-8 contract: bytes for -length, characters for
	 * -characterCount, and -characterAtIndex: indexing CHARACTERS. Built from
	 * explicit bytes so no source-escape ambiguity is involved. */
	{
		static const char bytes[] = { 'h', (char)0xC3, (char)0xA9, 'l', 'l', 'o', 0 };
		NSString *u = [NSString stringWithUTF8String:bytes];

		/* -length IS UTF-16 CODE UNITS NOW (W1 slice 3, Apple's contract), and the
		 * BYTE count has its own door. Both are asserted, because the difference
		 * between them is the whole point of the unit. */
		check("utf8", [u length] == 5 && [u characterCount] == 5 &&
		      [u lengthOfBytesUsingEncoding:NSUTF8StringEncoding] == 6 &&
		      [u characterAtIndex:1] == 0xE9 &&
		      [[u substringWithRange:NSMakeRange(1, 1)] isEqualToString:@"\xC3\xA9"] &&
		      [[u substringFromIndex:1] isEqualToString:@"\xC3\xA9llo"] &&
		      [u rangeOfString:@"llo"].location == 2 &&
		      strcmp([u UTF8String], bytes) == 0,
		      "-length 5 UNITS (6 BYTES), -characterCount 5 CHARACTERS, and the ranges index units");
	}

	/* Mutation, and the snapshot rule for -copy of a mutable string. */
	{
		NSMutableString *m = [[NSMutableString alloc] initWithUTF8String:"ab"];
		NSString *snapshot;

		[m appendString:@"cd"];
		snapshot = [m copy];
		[m appendString:@"!"];

		check("mutable", strcmp([m UTF8String], "abcd!") == 0 &&
		      strcmp([snapshot UTF8String], "abcd") == 0,
		      "mutation works and -copy is a snapshot");
	}

	/* Description: the root class's names the class, an override wins, and a
	 * string describes itself. */
	{
		NSObject *rootObject = [[NSObject alloc] init];
		NSString *literal = @"hello";
		NamedThing *named = [[NamedThing alloc] init];

		check("description",
		      strcmp([[rootObject description] UTF8String], "NSObject") == 0 &&
		      strcmp([[named description] UTF8String],
			     "a NamedThing, thank you") == 0 &&
		      [literal description] == literal,
		      "root class names itself; an override wins; a string is its own");
	}

	/* A constant string that came from the other translation unit. */
	{
		check("cross-tu", [foundation_string_constant() length] == 4 &&
		      [foundation_string_constant() isEqualToString:@"supt"],
		      "a tagged constant string from the support unit");
	}


	{
		/*
		 * THE AUDITED COCOA INVENTORY, not a list of what we happened to declare.
		 *
		 * The previous version of this check was SELF-REFERENTIAL: it asserted
		 * that every selector in OUR headers existed, which cannot see a method we
		 * never declared — and that is precisely how +stringWithFormat:arguments:
		 * shipped missing while this reported "complete". The lists below are the
		 * DOCUMENTED Cocoa surface, split into what we implement and what we
		 * deliberately do not, and the check runs BOTH ways: everything in
		 * `implemented` must exist, and everything in `excluded` must NOT (so
		 * shipping one of them forces the inventory to be updated rather than
		 * quietly widening the gap).
		 */
		static const char *classSelectors[] = {
			"string",
			"stringWithString:",
			"stringWithCharacters:length:", "stringWithUTF8String:",
			"stringWithFormat:",
			"stringWithFormat:arguments:",
			"stringWithContentsOfFile:encoding:error:",
			"stringWithContentsOfFile:usedEncoding:error:",
			NULL
		};
		static const char *instanceSelectors[] = {
			"init",
			"initWithString:",
			"initWithCharacters:length:", "initWithCharactersNoCopy:length:freeWhenDone:",
			"getCharacters:range:", "initWithUTF8String:",
			"initWithFormat:",
			"initWithFormat:arguments:",
			"initWithData:encoding:",
			"length",
			"characterCount",
			"byteAtIndex:",
			"characterAtIndex:",
			"UTF8String",
			"lengthOfBytesUsingEncoding:",
			"dataUsingEncoding:",
			"cStringUsingEncoding:",
			"isEqualToString:",
			"compare:",
			"caseInsensitiveCompare:",
			"compare:options:",
			"compare:options:range:",
			"compare:options:range:locale:",
			"localizedCompare:",
			"localizedCaseInsensitiveCompare:",
			"hasPrefix:",
			"hasSuffix:",
			"containsString:",
			"rangeOfString:",
			"rangeOfString:options:",
			"rangeOfString:options:range:",
			"rangeOfString:options:range:locale:",
			"uppercaseString",
			"lowercaseString",
			"capitalizedString",
			"uppercaseStringWithLocale:",
			"lowercaseStringWithLocale:",
			"substringFromIndex:",
			"substringToIndex:",
			"substringWithRange:",
			"stringByAppendingString:",
			"stringByAppendingFormat:",
			"stringByAppendingPathComponent:",
			"stringByAppendingPathExtension:",
			"stringByPaddingToLength:withString:startingAtIndex:",
			"stringByReplacingOccurrencesOfString:withString:",
			"stringByReplacingOccurrencesOfString:withString:options:range:",
			"stringByReplacingCharactersInRange:withString:",
			"componentsSeparatedByString:",
			"lastPathComponent",
			"pathExtension",
			"pathComponents",
			"stringByDeletingLastPathComponent",
			"stringByDeletingPathExtension",
			"stringByStandardizingPath",
			"isAbsolutePath",
			"intValue",
			"integerValue",
			"longLongValue",
			"floatValue",
			"doubleValue",
			"boolValue",
			"writeToFile:atomically:encoding:error:",
			"isEqual:",
			"hash",
			"description",
			"copy",
			"mutableCopy",
			"rangeOfCharacterFromSet:",
			"componentsSeparatedByCharactersInSet:",
			"stringByTrimmingCharactersInSet:",
			NULL
		};
		static const char *mutableClassSelectors[] = {
			"string", "stringWithCapacity:", "stringWithString:", NULL
		};
		static const char *mutableSelectors[] = {
			"initWithCapacity:", "setString:", "appendString:", "appendFormat:",
			"appendUTF8String:", "insertString:atIndex:", "deleteCharactersInRange:",
			"replaceCharactersInRange:withString:",
			"replaceOccurrencesOfString:withString:options:range:",
			"stringByAppendingFormat:", NULL
		};

		/*
		 * DELIBERATELY ABSENT, and asserted absent: each of these is Cocoa API we
		 * do not ship, and every one is recorded in the plan's checklist with its
		 * reason. Adding one to the library without moving it out of this list
		 * fails the check, so the inventory cannot drift away from the code.
		 */
		static const char *excluded[] = {
			/* EMPTY, and that is the point: THE FOUR ...Characters: FORMS WERE THE
			 * LAST THING THIS CLASS WAS MISSING. The unit shipped them (W1 slice 4),
			 * so they moved UP into the required lists above — which is the inventory
			 * rule working in the direction it was written for: what ships must be
			 * DEMANDED, and what does not must be ABSENT. */
			NULL
		};
		NSString *probe = @"x";
		NSMutableString *mutable = [[NSMutableString alloc] init];
		int complete = 1;
		int i;

		for (i = 0; classSelectors[i] != NULL; i++) {
			if (![NSString respondsToSelector:sel_registerName(classSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-STRING missing +%s\n", classSelectors[i]);
			}
		}
		for (i = 0; instanceSelectors[i] != NULL; i++) {
			if (![probe respondsToSelector:sel_registerName(instanceSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-STRING missing -%s\n", instanceSelectors[i]);
			}
		}
		for (i = 0; mutableClassSelectors[i] != NULL; i++) {
			if (![NSMutableString respondsToSelector:sel_registerName(mutableClassSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-STRING missing +%s (mutable)\n", mutableClassSelectors[i]);
			}
		}
		for (i = 0; mutableSelectors[i] != NULL; i++) {
			if (![mutable respondsToSelector:sel_registerName(mutableSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-STRING missing -%s (mutable)\n", mutableSelectors[i]);
			}
		}
		for (i = 0; excluded[i] != NULL; i++) {
			if ([probe respondsToSelector:sel_registerName(excluded[i])]) {
				complete = 0;
				printf("FOUNDATION-STRING present but EXCLUDED: %s (move it into the inventory)\n",
				       excluded[i]);
			}
		}
		check("string-api-complete", complete,
		      "the audited Cocoa inventory: every implemented selector exists, and nothing listed as excluded does");
	}


	{
		/* The format engine: conversions, width/precision, and Cocoa's (null). */
		NSString *rendered = [NSString stringWithFormat:@"%d/%s/%@/%g/%%",
				      42, "xy", [NSNumber numberWithInt:7], 1.5];
		NSString *padded = [NSString stringWithFormat:@"[%05d][%.2f][%8s]", 42, 1.5, "ab"];
		NSString *wide = [NSString stringWithFormat:@"%ld/%llu", 1234567890L, 99ULL];
		NSString *nilObject = [NSString stringWithFormat:@"<%@>", nil];

		check("string-format",
		      [rendered isEqualToString:@"42/xy/7/1.5/%"] &&
		      [padded isEqualToString:@"[00042][1.50][      ab]"] &&
		      [wide isEqualToString:@"1234567890/99"] &&
		      [nilObject isEqualToString:@"<(null)>"] &&
		      [[NSString stringWithFormat:@"%@-%@", @"a", @"b"] isEqualToString:@"a-b"],
		      "the conversions render, width and precision pass through, nil is (null)");

		/* `%@` with a TAGGED literal — named rather than incidental. A literal of
		 * fewer than 9 ASCII characters is a pointer clang packs, not an object, so
		 * this is the representation that most easily goes wrong. `string-format`
		 * above has always covered one; this check says so on purpose, because when
		 * the F4 work faulted this path was SUSPECTED and cleared: the run that
		 * faulted printed `string-format ok` first, and that check ends with exactly
		 * this conversion. Tagged, owned and boxed values must all render. */
		{
			NSString *tagged = [NSString stringWithFormat:@"<%@>", @"x"];
			NSString *owned = [NSString stringWithFormat:@"<%@>",
					   [NSString stringWithUTF8String:"owned"]];
			NSString *boxed = [NSString stringWithFormat:@"<%@>",
					   [NSNumber numberWithInt:7]];

			check("string-format-tagged-object",
			      [tagged isEqualToString:@"<x>"] &&
			      [owned isEqualToString:@"<owned>"] &&
			      [boxed isEqualToString:@"<7>"],
			      "a TAGGED literal, an owned string and a boxed number all render through %@");
		}

		/* The class-side form with an explicit list, from a unit that does not
		 * implement it — the first caller any probe ever gave it. It renders
		 * through a handed-over `va_copy` and requires the OWNER's list to still
		 * work in the same call: C99 7.15.1.4 says the callee consumes what it is
		 * handed, so the caller passes a copy. The support unit records what each
		 * version of this check cost; the plan records the F4 crash it explains. */
		check("class-format-arguments", foundation_string_class_arguments_ok(),
		      "the handed-over copy renders, and the owner's list still works afterwards");
	}

	{
		/* Comparison, search and ranges. */
		NSString *hay = @"the quick brown fox";
		NSRange found = [hay rangeOfString:@"brown"];
		NSRange missing = [hay rangeOfString:@"zebra"];
		NSRange fromIndex = [hay rangeOfString:@"o" options:NSLiteralSearch
						 range:NSMakeRange(10, 9)];

		check("string-compare",
		      [@"abc" compare:@"abd"] == NSOrderedAscending &&
		      [@"abd" compare:@"abc"] == NSOrderedDescending &&
		      [@"abc" compare:@"abc"] == NSOrderedSame &&
		      [@"abc" compare:@"ab"] == NSOrderedDescending &&
		      [@"ABC" caseInsensitiveCompare:@"abc"] == NSOrderedSame &&
		      [@"ABC" compare:@"abc" options:NSCaseInsensitiveSearch] == NSOrderedSame &&
		      [@"ABC" compare:@"abc" options:NSLiteralSearch] != NSOrderedSame &&
		      [hay hasPrefix:@"the "] && [hay hasSuffix:@"fox"] &&
		      ![hay hasPrefix:@"quick"] && ![hay hasSuffix:@"the"] &&
		      [hay containsString:@"quick"] && ![hay containsString:@"zebra"] &&
		      found.location == 10 && found.length == 5 &&
		      missing.location == NSNotFound &&
		      fromIndex.location == 12 && fromIndex.length == 1,
		      "ordering, case-insensitivity, prefix/suffix/contains, and NSNotFound");
	}

	{
		/* Case, substrings, appending, replacing, splitting. */
		static const unsigned char accentedBytes[] = { 'h', 0xC3, 0xA9, 'l', 'l', 'o', 0 };
		NSArray *parts = [@"a,b,,c" componentsSeparatedByString:@","];
		NSString *joined = @"hello world";
		NSString *accented = [NSString stringWithUTF8String:(const char *)accentedBytes];

		check("string-transform",
		      [[@"MiXeD" uppercaseString] isEqualToString:@"MIXED"] &&
		      [[@"MiXeD" lowercaseString] isEqualToString:@"mixed"] &&
		      [[@"hello world" capitalizedString] isEqualToString:@"Hello World"] &&
		      [accented length] == 5 && [accented characterCount] == 5 &&
		      [accented lengthOfBytesUsingEncoding:NSUTF8StringEncoding] == 6 &&
		      [[@"abcdef" substringFromIndex:3] isEqualToString:@"def"] &&
		      [[@"abcdef" substringToIndex:2] isEqualToString:@"ab"] &&
		      [[@"abcdef" substringWithRange:NSMakeRange(1, 3)] isEqualToString:@"bcd"] &&
		      [[@"abcdef" substringFromIndex:99] isEqualToString:@""] &&
		      [[@"a" stringByAppendingString:@"b"] isEqualToString:@"ab"] &&
		      [[@"a" stringByAppendingFormat:@"%d%@", 1, @"z"] isEqualToString:@"a1z"] &&
		      [[@"a-b-a" stringByReplacingOccurrencesOfString:@"a" withString:@"X"]
		          isEqualToString:@"X-b-X"] &&
		      [[@"a-b-a" stringByReplacingOccurrencesOfString:@"A" withString:@"X"
						   options:NSCaseInsensitiveSearch
						     range:NSMakeRange(0, 5)]
		          isEqualToString:@"X-b-X"] &&
		      [parts count] == 4 && [[parts objectAtIndex:2] isEqualToString:@""] &&
		      [[@"solo" componentsSeparatedByString:@","] count] == 1 &&
		      [joined length] == 11,
		      "case, substrings, appending, replacing and splitting");
	}

	{
		/* Conversions, with Cocoa's documented -boolValue rule. */
		check("string-convert",
		      [@"42" intValue] == 42 && [@"-7" integerValue] == -7 &&
		      [@"9223372036854775807" longLongValue] == 9223372036854775807LL &&
		      [@"1.5" floatValue] == 1.5f && [@"-2.25" doubleValue] == -2.25 &&
		      [@"  12abc" intValue] == 12 &&
		      [@"YES" boolValue] == YES && [@"true" boolValue] == YES &&
		      [@"1" boolValue] == YES && [@"0" boolValue] == NO &&
		      [@"" boolValue] == NO && [@"no" boolValue] == NO,
		      "the numeric conversions, and -boolValue's first-character rule");
	}

	{
		/* Paths — pure string operations on FSH-shaped, slash-separated paths. */
		NSArray *components = [@"/System/Devices/ATA" pathComponents];

		check("string-path",
		      [[@"/System/Devices/ATA" lastPathComponent] isEqualToString:@"ATA"] &&
		      [[@"/a/b/" lastPathComponent] isEqualToString:@"b"] &&
		      [[@"/" lastPathComponent] isEqualToString:@"/"] &&
		      [[@"x.conf" pathExtension] isEqualToString:@"conf"] &&
		      [[@"/a/b.conf" pathExtension] isEqualToString:@"conf"] &&
		      [[@"noext" pathExtension] isEqualToString:@""] &&
		      [[@"/a/b/c" stringByDeletingLastPathComponent] isEqualToString:@"/a/b"] &&
		      [[@"/a/b.conf" stringByDeletingPathExtension] isEqualToString:@"/a/b"] &&
		      [[@"/a" stringByAppendingPathComponent:@"b"] isEqualToString:@"/a/b"] &&
		      [[@"/a/" stringByAppendingPathComponent:@"b"] isEqualToString:@"/a/b"] &&
		      [components count] == 4 &&
		      [[components objectAtIndex:3] isEqualToString:@"ATA"],
		      "last component, extension, deleting, appending, components");
	}

	{
		/* Encodings: only the storage encoding is real, and the ASCII query
		 * answers honestly for non-ASCII content. */
		static const unsigned char encAccented[] = { 'h', 0xC3, 0xA9, 'l', 'l', 'o', 0 };
		NSString *accented = [NSString stringWithUTF8String:(const char *)encAccented];
		NSData *utf8 = [@"hello" dataUsingEncoding:NSUTF8StringEncoding];
		NSString *roundTrip = [[NSString alloc] initWithData:utf8
							    encoding:NSUTF8StringEncoding];

		check("string-encoding",
		      [utf8 length] == 5 &&
		      [roundTrip isEqualToString:@"hello"] &&
		      [accented lengthOfBytesUsingEncoding:NSUTF8StringEncoding] == 6 &&
		      [accented lengthOfBytesUsingEncoding:NSASCIIStringEncoding] == 0 &&
		      [@"abc" lengthOfBytesUsingEncoding:NSASCIIStringEncoding] == 3 &&
		      [@"x" dataUsingEncoding:NSUnicodeStringEncoding] == nil,
		      "the UTF-8 round trip, and honest answers for the encodings we do not store");
	}

	{
		/* THE UNIT'S OWN FAMILY (W1 slice 4), and it carries the sharpest case in the
		 * whole class: a SURROGATE PAIR — one character, two units, four bytes. */
		static const unichar units[] = { 'h', 0xD83D, 0xDE00, 'i' };
		NSString *s = [NSString stringWithCharacters:units length:4];
		unichar readBack[4];
		NSMutableString *borrowed;
		static unichar pool[2] = { 'O', 'K' };

		[s getCharacters:readBack range:NSMakeRange(0, 4)];
		/* A BORROWED BUFFER: freeWhenDone:NO means the receiver never frees it and
		 * never writes it — and a MUTATION still must not write it. */
		borrowed = [[NSMutableString alloc] initWithCharactersNoCopy:pool length:2
							     freeWhenDone:NO];
		[borrowed appendString:@"!"];
		check("characters-family",
		      s != nil && [s length] == 4 && [s characterCount] == 3 &&
		      [s characterAtIndex:1] == 0xD83D && [s characterAtIndex:2] == 0xDE00 &&
		      [s lengthOfBytesUsingEncoding:NSUTF8StringEncoding] == 6 &&
		      strcmp([s UTF8String], "h\xF0\x9F\x98\x80i") == 0 &&
		      readBack[0] == 'h' && readBack[1] == 0xD83D &&
		      readBack[2] == 0xDE00 && readBack[3] == 'i' &&
		      [[[NSString alloc] initWithCharactersNoCopy:(unichar *)pool length:2
						     freeWhenDone:NO] isEqualToString:@"OK"] &&
		      [borrowed isEqualToString:@"OK!"] && pool[0] == 'O' && pool[1] == 'K',
		      "a surrogate pair is 1 character / 2 units / 4 bytes; -getCharacters:range: reads units back; and a borrowed buffer is never written");
		{
			/* freeWhenDone:YES hands ownership over, so this one is freed by the string. */
			unichar *owned = (unichar *)malloc(2 * sizeof(unichar));

			owned[0] = 'y';
			owned[1] = 'y';
			check("characters-family-ownership",
			      [[[NSString alloc] initWithCharactersNoCopy:owned length:2
							     freeWhenDone:YES] isEqualToString:@"yy"],
			      "freeWhenDone:YES transfers the buffer to the receiver");
		}
	}

	{
		/* The mutable family. */
		NSMutableString *mutable = [NSMutableString stringWithCapacity:8];
		NSMutableString *snapshotSource;

		[mutable appendFormat:@"%d", 12];
		[mutable appendString:@"ab"];
		[mutable insertString:@"-" atIndex:2];
		[mutable deleteCharactersInRange:NSMakeRange(0, 1)];
		[mutable replaceCharactersInRange:NSMakeRange(0, 1) withString:@"X"];
		snapshotSource = [[NSMutableString alloc] initWithString:mutable];
		{
			NSMutableString *before = [snapshotSource copy];

			[mutable appendString:@"!"];
			check("string-mutable",
			      [mutable isEqualToString:@"X-ab!"] &&
			      [before isEqualToString:@"X-ab"] &&
			      [mutable replaceOccurrencesOfString:@"X" withString:@"Y"
						       options:NSLiteralSearch
							 range:NSMakeRange(0, [mutable length])] == 1 &&
			      [mutable isEqualToString:@"Y-ab!"] &&
			      [[NSMutableString string] length] == 0,
			      "append/insert/delete/replace, the count-returning replace, and -copy as a snapshot");
		}
	}

	{
		/* THE AUDITED COCOA INVENTORY for NSCharacterSet, the class the three
		 * ...InSet: methods above are specified in terms of. */
		static const char *classSelectors[] = {
			"characterSetWithCharactersInString:", "characterSetWithRange:",
			"whitespaceCharacterSet", "whitespaceAndNewlineCharacterSet",
			"newlineCharacterSet", "decimalDigitCharacterSet", "letterCharacterSet",
			"alphanumericCharacterSet", "punctuationCharacterSet", "controlCharacterSet",
			"lowercaseLetterCharacterSet", "uppercaseLetterCharacterSet",
						"characterSetWithBitmapRepresentation:", "characterSetWithContentsOfFile:",
			"illegalCharacterSet",
			"symbolCharacterSet", "capitalizedLetterCharacterSet",
			"nonBaseCharacterSet", "decomposableCharacterSet",
NULL
		};
		static const char *mutableClassSelectors[] = {
			"characterSet", NULL
		};
		static const char *instanceSelectors[] = {
			"initWithCharactersInString:", "initWithRange:", "characterIsMember:",
			"invertedSet", "isSupersetOfSet:", "isEqualToCharacterSet:",
			"isEqual:", "hash", "description", "copy", "mutableCopy", 			"longCharacterIsMember:", "hasMemberInPlane:", "bitmapRepresentation",
NULL
		};
		static const char *mutableSelectors[] = {
			"addCharactersInString:", "addCharactersInRange:",
			"removeCharactersInString:", "removeCharactersInRange:",
			"invert", "formUnionWithCharacterSet:",
			"formIntersectionWithCharacterSet:", NULL
		};
		static const char *excluded[] = {
			/* THE FOUR THAT USED TO BE LISTED HERE NOW SHIP, and they are DEMANDED above: Apple
			 * defines three of them by Unicode GENERAL CATEGORY (S*, Lt, M*) and the fourth by
			 * Unicode 3.2's STANDARD DECOMPOSITION, and ICU - already linked into this library for
			 * five of its files - answers both. "Needs the Unicode tables" was the wrong reason
			 * (§15.5), which is why they were DEFECTS rather than necessities. */
			/* illegalCharacterSet USED TO BE LISTED HERE. It is a RULE rather than a table - the
			 * surrogates plus the noncharacters - so it SHIPS, and it is DEMANDED above. */
			/* FIVE USED TO BE NAMED HERE as needing "the Unicode character tables" or "a bitmap
			 * representation". They needed NEITHER: the class is BMP-only, so an astral code
			 * point is exactly a non-member, and the bitmap layout is this library's with the
			 * round trip as the contract. They are DEMANDED above in the same change. */
			NULL
		};
		NSCharacterSet *probe = [NSCharacterSet whitespaceCharacterSet];
		NSMutableCharacterSet *mutable = [[NSMutableCharacterSet alloc] init];
		int complete = 1;
		int i;

		for (i = 0; mutableClassSelectors[i] != NULL; i++) {
			if (![NSMutableCharacterSet respondsToSelector:sel_registerName(mutableClassSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-STRING missing +%s (mutable)\n", mutableClassSelectors[i]);
			}
		}
		for (i = 0; classSelectors[i] != NULL; i++) {
			if (![NSCharacterSet respondsToSelector:sel_registerName(classSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-STRING missing +%s\n", classSelectors[i]);
			}
		}
		for (i = 0; instanceSelectors[i] != NULL; i++) {
			if (![probe respondsToSelector:sel_registerName(instanceSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-STRING missing -%s\n", instanceSelectors[i]);
			}
		}
		for (i = 0; mutableSelectors[i] != NULL; i++) {
			if (![mutable respondsToSelector:sel_registerName(mutableSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-STRING missing -%s (mutable)\n", mutableSelectors[i]);
			}
		}
		for (i = 0; excluded[i] != NULL; i++) {
			if ([probe respondsToSelector:sel_registerName(excluded[i])]) {
				complete = 0;
				printf("FOUNDATION-STRING present but EXCLUDED: %s\n", excluded[i]);
			}
		}
		check("characterset-api-complete", complete,
		      "the audited Cocoa inventory for NSCharacterSet/NSMutableCharacterSet");
	}

	{
		/* NSLocale (stage E): the identifier value type and the canon it enforces.
		 * The locale-SENSITIVE behaviour is the next two blocks. */
		NSLocale *turkish = [NSLocale localeWithLocaleIdentifier:@"TR-tr"];
		NSLocale *same = [NSLocale localeWithLocaleIdentifier:@"tr_TR"];
		NSLocale *other = [NSLocale localeWithLocaleIdentifier:@"en_US"];
		NSLocale *chinese = [NSLocale localeWithLocaleIdentifier:@"zh-Hans-CN"];
		NSLocale *posix = [NSLocale localeWithLocaleIdentifier:@"C"];
		NSDictionary *components = [NSLocale componentsFromLocaleIdentifier:@"tr_TR"];
		NSDictionary *rebuilt = [NSDictionary dictionaryWithObjectsAndKeys:
					 @"tr", NSLocaleLanguageCode,
					 @"TR", NSLocaleCountryCode, nil];

		check("locale-basics",
		      [[turkish localeIdentifier] isEqualToString:@"tr_TR"] &&
		      [[turkish objectForKey:NSLocaleLanguageCode] isEqualToString:@"tr"] &&
		      [[turkish objectForKey:NSLocaleCountryCode] isEqualToString:@"TR"] &&
		      [[turkish objectForKey:NSLocaleIdentifier] isEqualToString:[turkish localeIdentifier]] &&
		      [[chinese objectForKey:NSLocaleScriptCode] isEqualToString:@"Hans"] &&
		      [[chinese objectForKey:NSLocaleCountryCode] isEqualToString:@"CN"] &&
		      [[posix localeIdentifier] isEqualToString:@"en_US_POSIX"] &&
		      [[NSLocale canonicalLanguageIdentifierFromString:@"zh-Hans"] isEqualToString:@"zh"] &&
		      [[NSLocale canonicalLocaleIdentifierFromString:@"tr-tr.UTF-8"] isEqualToString:@"tr_TR"] &&
		      [[components objectForKey:NSLocaleLanguageCode] isEqualToString:@"tr"] &&
		      [[NSLocale localeIdentifierFromComponents:rebuilt] isEqualToString:@"tr_TR"] &&
		      [turkish isEqual:same] && [turkish hash] == [same hash] &&
		      ![turkish isEqual:other] && ![turkish isEqual:@"tr_TR"] &&
		      [turkish objectForKey:@"NSLocaleDecimalSeparator"] == nil &&
		      [[NSLocale availableLocaleIdentifiers] count] == 2 &&
		      [turkish copy] == turkish &&
		      [[turkish description] isEqualToString:@"<NSLocale: tr_TR>"],
		      "canonicalisation, subtag access, components, equality/hash, the POSIX name and the nil data key");
	}

	{
		/* THE TURKIC CASE RULE, which is what "the localised comparisons" mean
		 * here. Two DIFFERENT claims are asserted separately, because the failure
		 * of one says nothing about the other:
		 *
		 *   - the FOLD's output, asserted as BYTES (İ is U+0130 = c4 b0 and ı is
		 *     U+0131 = c4 b1, the two code points Unicode's SpecialCasing makes
		 *     conditional for tr/az), with the Turkic operands BUILT by the rule so
		 *     the checks do not rest on a literal;
		 *   - a two-byte non-ASCII LITERAL round-tripping — its own check below.
		 */
		NSLocale *turkish = [NSLocale localeWithLocaleIdentifier:@"tr_TR"];
		NSLocale *neutral = [NSLocale localeWithLocaleIdentifier:@"en_US"];
		NSString *upper_i = [@"i" uppercaseStringWithLocale:turkish];		/* İ */
		NSString *lower_I = [@"I" lowercaseStringWithLocale:turkish];		/* ı */
		NSString *upper_istanbul = [[upper_i stringByAppendingString:@"stanbul"]
					     uppercaseStringWithLocale:turkish];

		check("locale-turkic-upper",
		      [upper_i length] == 1 &&
		      [upper_i lengthOfBytesUsingEncoding:NSUTF8StringEncoding] == 2 &&
		      [upper_i byteAtIndex:0] == 0xC4 &&
		      [upper_i byteAtIndex:1] == 0xB0 &&
		      [upper_i isEqualToString:@"\xC4\xB0"] &&
		      [[lower_I uppercaseStringWithLocale:turkish] isEqualToString:@"I"] &&
		      [upper_istanbul length] == 8 &&
		      [upper_istanbul lengthOfBytesUsingEncoding:NSUTF8StringEncoding] == 9 &&
		      [upper_istanbul byteAtIndex:0] == 0xC4 &&
		      [upper_istanbul byteAtIndex:1] == 0xB0 &&
		      [upper_istanbul byteAtIndex:2] == 'S' && [upper_istanbul byteAtIndex:8] == 'L',
		      "upper(i) must be ONE UNIT and two bytes c4 b0 (İ), upper(ı) is I, and ASCII still upper-cases");

		check("locale-turkic-lower",
		      [lower_I length] == 1 &&
		      [lower_I lengthOfBytesUsingEncoding:NSUTF8StringEncoding] == 2 &&
		      [lower_I byteAtIndex:0] == 0xC4 &&
		      [lower_I byteAtIndex:1] == 0xB1 &&
		      [[upper_i lowercaseStringWithLocale:turkish] isEqualToString:@"i"],
		      "lower(I) must be the two bytes c4 b1 (ı), and lower(İ) is i");

		check("locale-turkic-neutral",
		      [[@"i" uppercaseStringWithLocale:neutral] isEqualToString:@"I"] &&
		      [[@"I" lowercaseStringWithLocale:neutral] isEqualToString:@"i"] &&
		      [[@"i" uppercaseStringWithLocale:nil] isEqualToString:@"I"] &&
		      [[lower_I lowercaseStringWithLocale:neutral] isEqualToString:lower_I],
		      "a neutral locale and a nil locale keep the plain mapping");

		/* THE UNIT SPACE'S SHARPEST CASE IS THE LAST ONE: a 4-byte character is ONE
		 * character, TWO UTF-16 units and FOUR bytes — and -characterAtIndex: must
		 * answer the SURROGATE HALVES, which the old scalar space could not (it
		 * answered 0xFFFD). Every literal here asserts all three numbers. */
		check("locale-literal-high-byte",
		      [@"\xC4\xB0" length] == 1 && [@"\xC4\xB0" characterCount] == 1 &&
		      [@"\xC4\xB0" lengthOfBytesUsingEncoding:NSUTF8StringEncoding] == 2 &&
		      [@"\xC4\xB0" byteAtIndex:0] == 0xC4 && [@"\xC4\xB0" byteAtIndex:1] == 0xB0 &&
		      [@"\xC4\xB0" characterAtIndex:0] == 0x0130 &&
		      strcmp([@"\xC4\xB0" UTF8String], "\xC4\xB0") == 0 &&
		      [@"\xC4\xB1" length] == 1 &&
		      [@"\xC4\xB1" lengthOfBytesUsingEncoding:NSUTF8StringEncoding] == 2 &&
		      [@"\xC4\xB1" byteAtIndex:0] == 0xC4 && [@"\xC4\xB1" byteAtIndex:1] == 0xB1 &&
		      [@"\xE2\x82\xAC" length] == 1 &&
		      [@"\xE2\x82\xAC" lengthOfBytesUsingEncoding:NSUTF8StringEncoding] == 3 &&
		      [@"\xE2\x82\xAC" byteAtIndex:0] == 0xE2 && [@"\xE2\x82\xAC" byteAtIndex:1] == 0x82 &&
		      [@"\xE2\x82\xAC" byteAtIndex:2] == 0xAC &&
		      [@"\xE2\x82\xAC" characterAtIndex:0] == 0x20AC &&
		      [@"\xF0\x9F\x98\x80" length] == 2 &&
		      [@"\xF0\x9F\x98\x80" characterCount] == 1 &&
		      [@"\xF0\x9F\x98\x80" lengthOfBytesUsingEncoding:NSUTF8StringEncoding] == 4 &&
		      [@"\xF0\x9F\x98\x80" byteAtIndex:0] == 0xF0 && [@"\xF0\x9F\x98\x80" byteAtIndex:3] == 0x80 &&
		      [@"\xF0\x9F\x98\x80" characterAtIndex:0] == 0xD83D &&
		      [@"\xF0\x9F\x98\x80" characterAtIndex:1] == 0xDE00,
		      "2-, 3- and 4-byte literals: -length counts UNITS (the 4-byte one is 2), the bytes have their own door, and -characterAtIndex: answers the surrogate halves");
	}

	{
		/* The COMPARISONS, and the two boundaries the header states: ORDERING stays
		 * byte order (no collation tables ship), and SEARCH folding stays byte-wise
		 * (the Turkic fold changes lengths, and -rangeOfString: answers a range into
		 * the receiver). The ranged form applies ONE range to both strings, so every
		 * clause here works on operands of the same length. */
		NSLocale *turkish = [NSLocale localeWithLocaleIdentifier:@"tr_TR"];
		NSLocale *neutral = [NSLocale localeWithLocaleIdentifier:@"en_US"];
		NSString *upper_i = [@"i" uppercaseStringWithLocale:turkish];	/* İ */
		NSString *lower_I = [@"I" lowercaseStringWithLocale:turkish];	/* ı */
		NSRange whole = NSMakeRange(0, 1);

		check("locale-turkic-compare",
		      [@"i" compare:@"I" options:NSCaseInsensitiveSearch
			     range:whole locale:turkish] != NSOrderedSame &&
		      [@"i" compare:@"I" options:NSCaseInsensitiveSearch
			     range:whole locale:neutral] == NSOrderedSame &&
		      [lower_I compare:upper_i options:NSCaseInsensitiveSearch
			     range:NSMakeRange(0, 2) locale:turkish] != NSOrderedSame &&
		      [@"a" compare:@"B" options:NSCaseInsensitiveSearch
			     range:whole locale:turkish] == NSOrderedAscending,
		      "the Turkic fold changes the case-insensitive comparison; a neutral locale does not");

		check("locale-boundaries",
		      [@"b" localizedCompare:@"a"] == NSOrderedDescending &&
		      [@"i" rangeOfString:@"I" options:NSCaseInsensitiveSearch
				  range:NSMakeRange(0, 1) locale:turkish].location == 0,
		      "ordering stays byte order (no collation) and search folding stays byte-wise");
	}

	{
		/* +currentLocale READS THE ENVIRONMENT, and the localised case-insensitive
		 * comparison USES it — the end-to-end assertion that the two are wired
		 * together. <stdlib.h> is included here because this block is its only
		 * user; the environment is put back as it was found. */
		#include <stdlib.h>
		char *kept_all = getenv("LC_ALL") ? strdup(getenv("LC_ALL")) : NULL;
		char *kept_lang = getenv("LANG") ? strdup(getenv("LANG")) : NULL;
		int wired;

		setenv("LC_ALL", "tr_TR.UTF-8", 1);
		wired = [[[NSLocale currentLocale] localeIdentifier] isEqualToString:@"tr_TR"] &&
			[@"i" localizedCaseInsensitiveCompare:@"I"] != NSOrderedSame;
		unsetenv("LC_ALL");
		unsetenv("LANG");
		wired = wired &&
			[[[NSLocale currentLocale] localeIdentifier] isEqualToString:@"en_US_POSIX"] &&
			[@"i" localizedCaseInsensitiveCompare:@"I"] == NSOrderedSame;
		setenv("LANG", "de_DE", 1);
		wired = wired &&
			[[[NSLocale currentLocale] localeIdentifier] isEqualToString:@"de_DE"] &&
			[@"i" localizedCaseInsensitiveCompare:@"I"] == NSOrderedSame;
		if (kept_all != NULL) {
			setenv("LC_ALL", kept_all, 1);
			free(kept_all);
		} else {
			unsetenv("LC_ALL");
		}
		if (kept_lang != NULL) {
			setenv("LANG", kept_lang, 1);
			free(kept_lang);
		} else {
			unsetenv("LANG");
		}
		check("locale-current", wired,
		      "currentLocale reads LC_ALL then LANG, defaults to en_US_POSIX, and localisedCaseInsensitiveCompare: follows it");
	}

	{
		/* The audited Cocoa inventory for NSLocale: what SHIPS, and — asserted
		 * absent — the data-driven half, which needs the locale database. */
		static const char *classSelectors[] = {
			"currentLocale", "localeWithLocaleIdentifier:",
			"availableLocaleIdentifiers",
			"componentsFromLocaleIdentifier:", "localeIdentifierFromComponents:",
			"canonicalLanguageIdentifierFromString:",
			"canonicalLocaleIdentifierFromString:", NULL
		};
		static const char *classExcluded[] = {
			/* The database: a separate system locale, change-notified locales, the
			 * identifier catalogues and the ISO code registries. */
			"autoupdatingCurrentLocale", "systemLocale", "preferredLanguages",
			"ISOLanguageCodes", "ISOCountryCodes", NULL
		};
		static const char *instanceSelectors[] = {
			"initWithLocaleIdentifier:", "localeIdentifier", "objectForKey:",
			"isEqual:", "hash", "description", "copy", NULL
		};
		static const char *excluded[] = {
			/* Needs the locale's name tables. */
			"displayNameForKey:value:",
			/* Deprecated in Cocoa in favour of -objectForKey: with a key. */
			"languageCode", "countryCode", NULL
		};
		NSLocale *probe = [NSLocale localeWithLocaleIdentifier:@"en_US"];
		int complete = 1;
		int i;

		for (i = 0; classSelectors[i] != NULL; i++) {
			if (![NSLocale respondsToSelector:sel_registerName(classSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-STRING missing +%s (locale)\n", classSelectors[i]);
			}
		}
		for (i = 0; classExcluded[i] != NULL; i++) {
			if ([NSLocale respondsToSelector:sel_registerName(classExcluded[i])]) {
				complete = 0;
				printf("FOUNDATION-STRING present but EXCLUDED (locale class): %s\n", classExcluded[i]);
			}
		}
		for (i = 0; instanceSelectors[i] != NULL; i++) {
			if (![probe respondsToSelector:sel_registerName(instanceSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-STRING missing -%s (locale)\n", instanceSelectors[i]);
			}
		}
		for (i = 0; excluded[i] != NULL; i++) {
			if ([probe respondsToSelector:sel_registerName(excluded[i])]) {
				complete = 0;
				printf("FOUNDATION-STRING present but EXCLUDED (locale): %s\n", excluded[i]);
			}
		}
		check("locale-api-complete", complete,
		      "the audited Cocoa inventory for NSLocale");
	}

	{
		/* FIVE REFUSALS THAT NEEDED NO DATA AT ALL (D7's kind (D)): the class is BMP-only, so an
		 * ASTRAL code point is exactly a non-member and plane 0 is exactly non-empty; the bitmap's
		 * byte layout is this library's and the ROUND TRIP is the contract. */
		NSString *path = @"/System/Temporary Files/fncharset-bits";
		NSCharacterSet *set = [NSCharacterSet characterSetWithCharactersInString:@"abc"];
		NSData *bits = [set bitmapRepresentation];
		NSCharacterSet *back = bits != nil ? [NSCharacterSet characterSetWithBitmapRepresentation:bits] : nil;
		BOOL wrote = [bits writeToFile:path atomically:YES];
		NSCharacterSet *fromFile = wrote ? [NSCharacterSet characterSetWithContentsOfFile:path] : nil;

		check("charset-bitmap-and-planes",
		      bits != nil && [bits length] == 8 + 8192 &&
		      back != nil && [back characterIsMember:(unichar)'a'] &&
		      ![back characterIsMember:(unichar)'z'] &&
		      fromFile != nil && [fromFile characterIsMember:(unichar)'b'] &&
		      [set longCharacterIsMember:(UTF32Char)'a'] &&
		      ![set longCharacterIsMember:0x1F600] &&
		      [set hasMemberInPlane:0] && ![set hasMemberInPlane:1],
		      [[NSString stringWithFormat:@"bits=%lu back=%d file=%d astral=%d planes=%d/%d",
			(unsigned long)(bits != nil ? [bits length] : 0), (int)(back != nil),
			(int)(fromFile != nil), (int)[set longCharacterIsMember:0x1F600],
			(int)[set hasMemberInPlane:0], (int)[set hasMemberInPlane:1]] UTF8String]);
		remove([path UTF8String]);
	}

	{
		/* illegalCharacterSet IS A RULE, NOT A TABLE (D7's kind (D)). The negatives matter as much
		 * as the positives: a set built from the wrong ranges would still contain 0xD800. */
		NSCharacterSet *illegal = [NSCharacterSet illegalCharacterSet];

		check("charset-illegal",
		      illegal != nil &&
		      [illegal characterIsMember:(unichar)0xD800] &&
		      [illegal characterIsMember:(unichar)0xDFFF] &&
		      [illegal characterIsMember:(unichar)0xFDD0] &&
		      [illegal characterIsMember:(unichar)0xFDEF] &&
		      [illegal characterIsMember:(unichar)0xFFFE] &&
		      [illegal characterIsMember:(unichar)0xFFFF] &&
		      ![illegal characterIsMember:(unichar)0xD7FF] &&
		      ![illegal characterIsMember:(unichar)0xFDCF] &&
		      ![illegal characterIsMember:(unichar)0xFDF0] &&
		      ![illegal characterIsMember:(unichar)'a'],
		      [[NSString stringWithFormat:@"d800=%d dfff=%d fdd0=%d ffef=%d d7ff=%d fdf0=%d",
			(int)[illegal characterIsMember:(unichar)0xD800],
			(int)[illegal characterIsMember:(unichar)0xDFFF],
			(int)[illegal characterIsMember:(unichar)0xFDD0],
			(int)[illegal characterIsMember:(unichar)0xFDEF],
			(int)[illegal characterIsMember:(unichar)0xD7FF],
			(int)[illegal characterIsMember:(unichar)0xFDF0]] UTF8String]);
	}

	{
		/* THE ICU-BACKED RULE SETS (§15.5). The NEGATIVES carry as much of the claim as the
		 * positives - a set built from the wrong categories would still contain '$' - and each
		 * negative below is the category NEXT TO the one claimed, so the check fails for a set
		 * built from L*, N*, P* or Zs rather than only for an empty one. */
		NSCharacterSet *symbols = [NSCharacterSet symbolCharacterSet];
		NSCharacterSet *titled = [NSCharacterSet capitalizedLetterCharacterSet];
		NSCharacterSet *marks = [NSCharacterSet nonBaseCharacterSet];
		NSCharacterSet *decomposable = [NSCharacterSet decomposableCharacterSet];

		check("charset-symbols",
		      symbols != nil &&
		      [symbols characterIsMember:(unichar)'$'] &&		/* U+0024, Sc */
		      [symbols characterIsMember:(unichar)'+'] &&		/* U+002B, Sm */
		      [symbols characterIsMember:(unichar)'^'] &&		/* U+005E, Sk */
		      [symbols characterIsMember:(unichar)0x00A9] &&	/* U+00A9 (c), So */
		      ![symbols characterIsMember:(unichar)'A'] &&
		      ![symbols characterIsMember:(unichar)'1'] &&
		      ![symbols characterIsMember:(unichar)' '] &&
		      ![symbols characterIsMember:(unichar)0x0301],
		      "S*: $ + ^ (c) in; a letter, a digit, a space and a mark out");

		check("charset-titled",
		      titled != nil &&
		      [titled characterIsMember:(unichar)0x01C5] &&	/* Dz with caron, the Lt letter */
		      ![titled characterIsMember:(unichar)0x01C4] &&	/* its UPPERCASE twin, Lu */
		      ![titled characterIsMember:(unichar)'A'] &&
		      ![titled characterIsMember:(unichar)'a'],
		      /* PRINTED RATHER THAN ASSERTED: Apple specifies +uppercaseLetterCharacterSet as Lu
		       * AND Lt, so it must be a superset of this set. This library's is `NSMakeRange('A',
		       * 26)` - Latin capitals only - so the relation reads 0 here and is recorded as an open
		       * item (§15.5) rather than asserted into a red check for a reason this probe did not
		       * measure. */
		      [[NSString stringWithFormat:@"01c5=%d 01c4=%d A=%d | inUppercase=%d",
			(int)[titled characterIsMember:(unichar)0x01C5],
			(int)[titled characterIsMember:(unichar)0x01C4],
			(int)[titled characterIsMember:(unichar)'A'],
			(int)[[NSCharacterSet uppercaseLetterCharacterSet] isSupersetOfSet:titled]] UTF8String]);

		check("charset-non-base",
		      marks != nil &&
		      [marks characterIsMember:(unichar)0x0301] &&	/* Mn: combining acute */
		      [marks characterIsMember:(unichar)0x20DD] &&	/* Me: combining enclosing circle */
		      [marks characterIsMember:(unichar)0x0903] &&	/* Mc: devanagari sign visarga */
		      ![marks characterIsMember:(unichar)'a'] &&
		      ![marks characterIsMember:(unichar)0x00C0],
		      "M*: Mn, Me and Mc in; a letter and the PRECOMPOSED A-grave out");

		check("charset-decomposable",
		      decomposable != nil &&
		      [decomposable characterIsMember:(unichar)0x00C0] &&	/* A-grave: canonical */
		      ![decomposable characterIsMember:(unichar)0xFB00] &&	/* ff ligature: COMPATIBILITY */
		      ![decomposable characterIsMember:(unichar)0x00A0] &&	/* noBreak space: noBreak */
		      ![decomposable characterIsMember:(unichar)'A'],
		      "STANDARD decomposition only: A-grave in, the ff ligature and noBreak space out");
	}

	{
		/* THE SETS THAT ALREADY SHIPPED ARE CATEGORIES NOW (§16) - the same rule machinery the four
		 * above use. Each of these was an ASCII or Latin-1 APPROXIMATION: +whitespaceCharacterSet
		 * was the space alone, +uppercaseLetterCharacterSet was 'A'-'Z' (26 members where Apple
		 * specifies Lu AND Lt), +letterCharacterSet stopped at LATIN-1. Nothing caught that, because
		 * the api-complete inventory asserts a documented set EXISTS and never asserted what is IN
		 * one - which is exactly what these three checks do. */
		NSCharacterSet *ws = [NSCharacterSet whitespaceCharacterSet];
		NSCharacterSet *wsp = [NSCharacterSet whitespaceAndNewlineCharacterSet];
		NSCharacterSet *nl = [NSCharacterSet newlineCharacterSet];
		NSCharacterSet *up = [NSCharacterSet uppercaseLetterCharacterSet];
		NSCharacterSet *lo = [NSCharacterSet lowercaseLetterCharacterSet];
		NSCharacterSet *letters = [NSCharacterSet letterCharacterSet];
		NSCharacterSet *alnum = [NSCharacterSet alphanumericCharacterSet];
		NSCharacterSet *punct = [NSCharacterSet punctuationCharacterSet];
		NSCharacterSet *ctrl = [NSCharacterSet controlCharacterSet];
		/* ITS OWN LOCAL because the class methods are declared NULLABLE (the F6 sweep's truthful
		 * constructors): passing one straight into -isSupersetOfSet:'s nonnull parameter is
		 * -Wnullable-to-nonnull-conversion, which this build makes an ERROR. */
		NSCharacterSet *titlecase = [NSCharacterSet capitalizedLetterCharacterSet];

		check("charset-whitespace-family",
		      ws != nil && wsp != nil && nl != nil &&
		      [ws characterIsMember:(unichar)0x09] &&		/* TAB, named separately by Apple */
		      [ws characterIsMember:(unichar)0x00A0] &&		/* NBSP, Zs */
		      [ws characterIsMember:(unichar)0x2003] &&		/* EM SPACE, Zs */
		      ![ws characterIsMember:(unichar)0x0A] &&		/* LF is not Zs */
		      ![ws characterIsMember:(unichar)'a'] &&
		      [wsp characterIsMember:(unichar)0x20] &&
		      [wsp characterIsMember:(unichar)0x00A0] &&
		      [wsp characterIsMember:(unichar)0x2028] &&		/* Zl */
		      [wsp characterIsMember:(unichar)0x0A] &&
		      [wsp characterIsMember:(unichar)0x85] &&
		      /* THE TAB IS NOT HERE, and that is Apple's CURRENT definition (Z*, U+000A-U+000D,
		       * U+0085) against the older OpenStep/GNUstep text that includes it. Asserted on both
		       * sides - in +whitespaceCharacterSet above, out of this one - rather than left to a
		       * comment, because the two sources genuinely disagree. */
		      ![wsp characterIsMember:(unichar)0x09] &&
		      [nl characterIsMember:(unichar)0x0B] &&		/* the U+000A-U+000D span */
		      [nl characterIsMember:(unichar)0x0C] &&
		      [nl characterIsMember:(unichar)0x85] &&
		      [nl characterIsMember:(unichar)0x2029] &&
		      ![nl characterIsMember:(unichar)0x20] &&
		      ![nl characterIsMember:(unichar)'a'],
		      "Zs+TAB, Z*+the newlines (no tab), and the literal newline list");

		check("charset-letter-family",
		      up != nil && lo != nil && letters != nil && alnum != nil &&
		      [up characterIsMember:(unichar)'A'] &&
		      [up characterIsMember:(unichar)0x00C0] &&		/* A-grave, Lu */
		      [up characterIsMember:(unichar)0x01C5] &&		/* Dz, Lt - Lu AND Lt */
		      ![up characterIsMember:(unichar)'a'] &&
		      [lo characterIsMember:(unichar)0x00E9] &&		/* e-acute, Ll */
		      ![lo characterIsMember:(unichar)'A'] &&
		      [letters characterIsMember:(unichar)0x4E00] &&	/* Lo: CJK */
		      [letters characterIsMember:(unichar)0x0301] &&	/* M* counts as a letter here */
		      ![letters characterIsMember:(unichar)'0'] &&
		      [alnum characterIsMember:(unichar)0x2160] &&	/* Nl: Roman numeral one */
		      [alnum characterIsMember:(unichar)0x0660] &&	/* Nd: Arabic-Indic zero */
		      ![alnum characterIsMember:(unichar)'!'] &&
		      /* THE RELATION THE LATIN-ONLY SETS FAILED: Apple specifies uppercase as Lu AND Lt, so
		       * the titlecase letters must be a SUBSET of it. Measured 0 before §16. */
		      [up isSupersetOfSet:titlecase],
		      "Lu+Lt (with Lt inside), Ll, L*&M*, and L*,M*,N* with Nl and Nd");

		check("charset-punct-and-control",
		      punct != nil && ctrl != nil &&
		      [punct characterIsMember:(unichar)'!'] &&
		      [punct characterIsMember:(unichar)0x2014] &&	/* em dash, Pd */
		      [punct characterIsMember:(unichar)0x201C] &&	/* left double quote, Pi */
		      ![punct characterIsMember:(unichar)'A'] &&
		      ![punct characterIsMember:(unichar)'$'] &&		/* Sc is a SYMBOL, not punctuation */
		      [ctrl characterIsMember:(unichar)0x1F] &&
		      [ctrl characterIsMember:(unichar)0x7F] &&
		      [ctrl characterIsMember:(unichar)0x00AD] &&	/* soft hyphen, Cf */
		      [ctrl characterIsMember:(unichar)0x200B] &&	/* zero-width space, Cf */
		      ![ctrl characterIsMember:(unichar)' '],
		      "P* (including the em dash and the curly quote) and Cc+Cf (including the Cf joiners)");
	}

	printf("FOUNDATION-STRING RESULT ok=%d fail=%d\n", okc, failc);
	/* The exit status, in the probe's OWN output. After a probe the console can stop
	 * serving INPUT for a while (the tier residual), so an `echo $?` that the harness
	 * types may never run. The probe knows exactly what it returns, so it says so - and
	 * this is the same value: failc ? 1 : 0 is the return statement below. */
	printf("FOUNDATION-STRING-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-STRING DONE\n");
	return failc ? 1 : 0;
}
