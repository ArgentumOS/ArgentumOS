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
			/* THE UTF-16 BOUNDARY: a DELIBERATE deviation rather than debt.
			 * -length counts bytes and character access is by character, so these
			 * forms have no honest UTF-8 implementation here. */
			"stringWithCharacters:length:",
			"initWithCharacters:length:",
			"initWithCharactersNoCopy:length:freeWhenDone:",
			"getCharacters:range:",
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

	{
		/* THE AUDITED COCOA INVENTORY for NSCharacterSet, the class the three
		 * ...InSet: methods above are specified in terms of. */
		static const char *classSelectors[] = {
			"characterSetWithCharactersInString:", "characterSetWithRange:",
			"whitespaceCharacterSet", "whitespaceAndNewlineCharacterSet",
			"newlineCharacterSet", "decimalDigitCharacterSet", "letterCharacterSet",
			"alphanumericCharacterSet", "punctuationCharacterSet", "controlCharacterSet",
			"lowercaseLetterCharacterSet", "uppercaseLetterCharacterSet",
			NULL
		};
		static const char *mutableClassSelectors[] = {
			"characterSet", NULL
		};
		static const char *instanceSelectors[] = {
			"initWithCharactersInString:", "initWithRange:", "characterIsMember:",
			"invertedSet", "isSupersetOfSet:", "isEqualToCharacterSet:",
			"isEqual:", "hash", "description", "copy", "mutableCopy", NULL
		};
		static const char *mutableSelectors[] = {
			"addCharactersInString:", "addCharactersInRange:",
			"removeCharactersInString:", "removeCharactersInRange:",
			"invert", "formUnionWithCharacterSet:",
			"formIntersectionWithCharacterSet:", NULL
		};
		static const char *excluded[] = {
			/* Needs the Unicode character tables. */
			"symbolCharacterSet", "capitalizedLetterCharacterSet",
			"nonBaseCharacterSet", "decomposableCharacterSet",
			"illegalCharacterSet", "longCharacterIsMember:", "hasMemberInPlane:",
			/* Needs a bitmap representation or a file to read one from. */
			"bitmapRepresentation", "characterSetWithBitmapRepresentation:",
			"characterSetWithContentsOfFile:", NULL
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
		      [upper_i length] == 2 && [upper_i byteAtIndex:0] == 0xC4 &&
		      [upper_i byteAtIndex:1] == 0xB0 &&
		      [upper_i isEqualToString:@"\xC4\xB0"] &&
		      [[lower_I uppercaseStringWithLocale:turkish] isEqualToString:@"I"] &&
		      [upper_istanbul length] == 9 && [upper_istanbul byteAtIndex:0] == 0xC4 &&
		      [upper_istanbul byteAtIndex:1] == 0xB0 &&
		      [upper_istanbul byteAtIndex:2] == 'S' && [upper_istanbul byteAtIndex:8] == 'L',
		      "upper(i) must be the two bytes c4 b0 (İ), upper(ı) is I, and ASCII still upper-cases");

		check("locale-turkic-lower",
		      [lower_I length] == 2 && [lower_I byteAtIndex:0] == 0xC4 &&
		      [lower_I byteAtIndex:1] == 0xB1 &&
		      [[upper_i lowercaseStringWithLocale:turkish] isEqualToString:@"i"],
		      "lower(I) must be the two bytes c4 b1 (ı), and lower(İ) is i");

		check("locale-turkic-neutral",
		      [[@"i" uppercaseStringWithLocale:neutral] isEqualToString:@"I"] &&
		      [[@"I" lowercaseStringWithLocale:neutral] isEqualToString:@"i"] &&
		      [[@"i" uppercaseStringWithLocale:nil] isEqualToString:@"I"] &&
		      [[lower_I lowercaseStringWithLocale:neutral] isEqualToString:lower_I],
		      "a neutral locale and a nil locale keep the plain mapping");

		check("locale-literal-high-byte",
		      [@"\xC4\xB0" length] == 2 && [@"\xC4\xB0" byteAtIndex:0] == 0xC4 &&
		      [@"\xC4\xB0" byteAtIndex:1] == 0xB0 &&
		      [@"\xC4\xB0" characterCount] == 1 &&
		      strcmp([@"\xC4\xB0" UTF8String], "\xC4\xB0") == 0 &&
		      [@"\xC4\xB1" length] == 2 && [@"\xC4\xB1" byteAtIndex:0] == 0xC4 &&
		      [@"\xC4\xB1" byteAtIndex:1] == 0xB1 &&
		      [@"\xE2\x82\xAC" length] == 3 && [@"\xE2\x82\xAC" byteAtIndex:0] == 0xE2 &&
		      [@"\xE2\x82\xAC" byteAtIndex:1] == 0x82 &&
		      [@"\xE2\x82\xAC" byteAtIndex:2] == 0xAC &&
		      [@"\xF0\x9F\x98\x80" length] == 4 &&
		      [@"\xF0\x9F\x98\x80" byteAtIndex:0] == 0xF0 &&
		      [@"\xF0\x9F\x98\x80" byteAtIndex:3] == 0x80 &&
		      [@"\xF0\x9F\x98\x80" characterCount] == 1,
		      "2-, 3- and 4-byte non-ASCII literals must decode to their own UTF-8 bytes (clang emits them as UTF-16)");
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
			"isEqual:", "hash", "description", "copy", "copyWithZone:", NULL
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

	printf("FOUNDATION-STRING RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-STRING DONE\n");
	return failc ? 1 : 0;
}
