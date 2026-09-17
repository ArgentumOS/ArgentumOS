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

		check("utf8", [u length] == 6 && [u characterCount] == 5 &&
		      [u characterAtIndex:1] == 0xE9 &&
		      strcmp([u UTF8String], bytes) == 0,
		      "-length 6 BYTES, -characterCount 5 CHARACTERS");
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
			"stringWithUTF8String:",
			"stringWithFormat:",
			"stringWithFormat:arguments:",
			"stringWithContentsOfFile:encoding:error:",
			"stringWithContentsOfFile:usedEncoding:error:",
			NULL
		};
		static const char *instanceSelectors[] = {
			"init",
			"initWithString:",
			"initWithUTF8String:",
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
			"stringWithCharacters:length:",
			"initWithCharacters:length:",
			"initWithCharactersNoCopy:length:freeWhenDone:",
			"getCharacters:range:",
			"rangeOfCharacterFromSet:",
			"componentsSeparatedByCharactersInSet:",
			"stringByTrimmingCharactersInSet:",
			"propertyList",
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
		      [accented length] == 6 && [accented characterCount] == 5 &&
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

	printf("FOUNDATION-STRING RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-STRING DONE\n");
	return failc ? 1 : 0;
}
