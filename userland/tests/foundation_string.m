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
 *   utf8        -length counts UTF-16 CODE UNITS, and the byte count has its own door
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
#include <stdarg.h>		/* va_list, for the locale-format-arguments case below */
#include <unistd.h>		/* symlink, for §63.48's real-link resolution check */

static int okc, failc;
static int lastcheck;

static void check(const char *name, int ok, const char *detail)
{
	lastcheck = ok;	/* read by covers(): a claim can only follow an assertion that held */
	if (ok) {
		okc++;
		printf("FOUNDATION-STRING %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-STRING %s FAIL %s\n", name, detail ? detail : "");
	}
}

/*
 * covers("NSString", "characterAtIndex:") — THE BEHAVIOURAL CLAIM, piggybacked on the check above it.
 *
 * It takes NO condition of its own: check() records its result and covers() prints only when that result was
 * true, so a claim cannot be printed beside a failed assertion. tools/foundation-cov.py reads these to tell
 * `asserted` from `named`, and fails on a shipped selector that nothing claims.
 *
 * AND EVERY CLAIM BELOW WAS FILTERED AGAINST THE LEDGER BEFORE IT WAS WRITTEN: only an exact
 * (owner, selector) SHIPPED pair is claimed, because a claim for a row the ledger does not carry is INERT -
 * it counts for nothing and lengthens the gate's inert advisory, which is only useful while it is short.
 */
static void covers_(const char *cls, const char *sel)
{
	if (lastcheck) {
		printf("COVERS %s %s\n", cls, sel);
	}
}
#define covers(cls, sel) covers_(cls, sel)

/* A va_list a caller owns, for -initWithFormat:locale:arguments: — the counterpart of the probe's own
 * -initWithFormat:arguments: check. */
static NSString *fn_render_locale_arguments(NSString *format, ...)
{
	va_list args;
	NSString *result;

	va_start(args, format);
	result = [[NSString alloc] initWithFormat:format locale:nil arguments:args];
	va_end(args);
	return result;
}

/* THE TWO va_list VALIDATED-FORMAT DOORS NEED A CALLER THAT OWNS A va_list, so they get one each: the
 * variadic doors cannot be asked to prove that a va_list argument is forwarded correctly. Both are the
 * probe's own code, at file scope, because C has no nested functions here. */
static NSString *fn_validated_list(id obj, NSString *format, NSString *specifiers, NSError **errorPtr, ...)
{
	va_list arguments;
	NSString *result;

	va_start(arguments, errorPtr);
	result = [obj initWithValidatedFormat:format validFormatSpecifiers:specifiers
				    arguments:arguments error:errorPtr];
	va_end(arguments);
	return result;
}

static NSString *fn_validated_list_locale(id obj, NSString *format, NSString *specifiers, id locale,
					  NSError **errorPtr, ...)
{
	va_list arguments;
	NSString *result;

	va_start(arguments, errorPtr);
	result = [obj initWithValidatedFormat:format validFormatSpecifiers:specifiers locale:locale
				    arguments:arguments error:errorPtr];
	va_end(arguments);
	return result;
}

int main(void)
{
	{
		/* §63.74: THE LEGACY PERCENT PAIR, WHOSE SET IS RFC 2396's. The assertions carry the SET rather than a
		 * round trip alone, because the set is the whole content of the door: `~!*'()` must SURVIVE (`! * ' ( )`
		 * were dropped by RFC 3986, so a door that used the modern rule would escape them) and space and `/` must
		 * be ESCAPED. AND THE ENCODING ARGUMENT IS ASSERTED BYTE-WISE, which is what separates this door from
		 * the UTF-8-always modern one. */
		NSString *src = [NSString stringWithUTF8String:"a b/c~d!e*f'g(h)i\xC3\xA9"];
		NSString *esc = [src stringByAddingPercentEscapesUsingEncoding:NSUTF8StringEncoding];
		NSString *back = [esc stringByReplacingPercentEscapesUsingEncoding:NSUTF8StringEncoding];
		NSString *latin = [[NSString stringWithUTF8String:"caf\xC3\xA9"] stringByAddingPercentEscapesUsingEncoding:NSISOLatin1StringEncoding];

		check("legacy-percent-pair",
		      esc != nil && back != nil && [back isEqualToString:src] &&
		      [esc rangeOfString:@"%20"].location != NSNotFound &&
		      [esc rangeOfString:@"%2F"].location != NSNotFound &&
		      [esc rangeOfString:@"~"].location != NSNotFound &&
		      /* ⚠ THE SET IS ASSERTED BY WHAT IS **ABSENT**, WHICH IS HOW IT IS ACTUALLY OBSERVABLE:
		       * the first version asked for the contiguous substring `!*'()` and that is a bug in the CHECK —
		       * the four characters are separate in the input, so the substring never exists. What identifies the
		       * RFC 2396 rule is that a MODERN door would have escaped them: `%21 %2A %27 %28 %29` must NOT
		       * appear, while `~ ! * ' ( )` must. */
		      [esc rangeOfString:@"%21"].location == NSNotFound &&
		      [esc rangeOfString:@"%2A"].location == NSNotFound &&
		      [esc rangeOfString:@"%27"].location == NSNotFound &&
		      [esc rangeOfString:@"%28"].location == NSNotFound &&
		      [esc rangeOfString:@"%29"].location == NSNotFound &&
		      [esc rangeOfString:@"%C3%A9"].location != NSNotFound &&
		      latin != nil && [latin isEqualToString:@"caf%E9"] &&
		      [@"%2" stringByReplacingPercentEscapesUsingEncoding:NSUTF8StringEncoding] == nil,
		      [[NSString stringWithFormat:@"esc=%@ back=%@ latin=%@", esc, back, latin] UTF8String]);
	covers("NSString", "stringByAddingPercentEscapesUsingEncoding:");
	covers("NSString", "stringByReplacingPercentEscapesUsingEncoding:");
	}

	{
		/* §63.73: APPLE'S DETECTION DOOR, ICU'S DETECTOR BEHIND IT. Two assertions, and the second is the
		 * one that carries the option keys: the FIRST asks the DETECTOR (which is a guess, and a good one on a
		 * long sample), and the SECOND asks for ONE SUGGESTED ENCODING ONLY, which is deterministic whatever
		 * the detector would have said. */
		/* ⚠ SPLIT LITERALS, AND NOT FOR STYLE: A C HEX ESCAPE IS GREEDY, so `"fa\xC3\xA7ade"` parses
		 * `\xA7a` as ONE escape (0x7A7) and `"\xC3\x9Cber"` parses `\x9Cb` — both out of range for a
		 * char, which is how this line failed to compile. Ending the literal at the escape is the fix. */
		NSString *src = [NSString stringWithUTF8String:"caf\xC3\xA9 na\xC3\xAFve fa\xC3\xA7" "ade \xC3\x9C" "ber"];
		NSData *data = [src dataUsingEncoding:NSUTF8StringEncoding];
		NSString *back = nil;
		BOOL lossy = YES;
		NSStringEncoding detected = [NSString stringEncodingForData:data encodingOptions:nil
							 convertedString:&back usedLossyConversion:&lossy];
		NSData *latin = [[NSString stringWithUTF8String:"caf\xC3\xA9"] dataUsingEncoding:NSISOLatin1StringEncoding];
		NSString *latinBack = nil;
		NSDictionary *only = [NSDictionary dictionaryWithObjectsAndKeys:
			[NSArray arrayWithObject:[NSNumber numberWithUnsignedLongLong:NSISOLatin1StringEncoding]],
			NSStringEncodingDetectionSuggestedEncodingsKey,
			[NSNumber numberWithBool:YES], NSStringEncodingDetectionUseOnlySuggestedEncodingsKey, nil];
		NSStringEncoding chosen = [NSString stringEncodingForData:latin encodingOptions:only
							    convertedString:&latinBack usedLossyConversion:NULL];

		check("detect-encoding-for-data",
		      detected == NSUTF8StringEncoding && back != nil && [back isEqualToString:src] && lossy == NO &&
		      chosen == NSISOLatin1StringEncoding && latinBack != nil &&
		      [latinBack isEqualToString:@"caf\xC3\xA9"],
		      [[NSString stringWithFormat:@"utf8 detected=%lu back=%@ lossy=%d ; latin1 chosen=%lu back=%@",
			(unsigned long)detected, (back != nil) ? back : @"(nil)", (int)lossy,
			(unsigned long)chosen, (latinBack != nil) ? latinBack : @"(nil)"] UTF8String]);
	covers("NSString", "stringEncodingForData:encodingOptions:convertedString:usedLossyConversion:");
	}

	{
		/* §63.72: THE CONVERTED ENCODINGS THROUGH THE C-STRING DOORS. `-getCString:…` COPIES, so a converted
		 * encoding is a plain conversion; `-cStringUsingEncoding:` BORROWS, and for a converted encoding the
		 * buffer is the POOL's. ⚠ EVERY NUMBER IS ASSERTED AGAINST THE BYTE DOOR, so the two agree by
		 * construction rather than by a constant this file would have to guess. */
		NSString *cafe = [NSString stringWithUTF8String:"caf\xC3\xA9"];
		char room[32];
		BOOL got = [cafe getCString:room maxLength:sizeof(room) encoding:NSISOLatin1StringEncoding];
		NSData *latin1 = [cafe dataUsingEncoding:NSISOLatin1StringEncoding];
		const char *borrowed = [cafe cStringUsingEncoding:NSISOLatin1StringEncoding];

		check("cstring-doors-convert",
		      latin1 != nil && [latin1 length] == 4 && got == YES &&
		      strlen(room) == [latin1 length] &&
		      memcmp(room, [latin1 bytes], [latin1 length]) == 0 &&
		      room[[latin1 length]] == '\0' &&
		      borrowed != (const char *)NULL && strcmp(borrowed, room) == 0 &&
		      /* and a STORAGE encoding still hands back the storage's own bytes, which is what keeps the
		       * UTF-8 answer free and its pointer per-instance */
		      [cafe cStringUsingEncoding:NSUTF8StringEncoding] == [cafe UTF8String],
		      [[NSString stringWithFormat:@"latin1Bytes=%lu getCString=%d strlen=%lu borrowed=%s",
			(unsigned long)[latin1 length], (int)got, (unsigned long)strlen(room),
			(borrowed != (const char *)NULL) ? borrowed : "(NULL)"] UTF8String]);
	}


	/* A short literal is not an object: clang packs it (tag 4), and the class
	 * the Foundation registers at that tag decodes it. */
	{
		NSString *c = @"hello";

		check("tiny", [c length] == 5 &&
		      strcmp([c UTF8String], "hello") == 0 &&
		      [c isKindOfClass:[NSString class]],
		      "a 5-character literal decodes from its tag");
	covers("NSString", "length");
	covers("NSString", "UTF8String");
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
	covers("NSString", "length");
	covers("NSString", "isEqualToString:");
	covers("NSString", "hash");
	covers("NSString", "stringWithUTF8String:");
	}

	{
		NSString *tagged = @"hello";
		NSString *owned = [NSString stringWithUTF8String:"hello"];

		check("mixed", [tagged isEqualToString:owned] &&
		      [owned isEqualToString:tagged] && [tagged hash] == [owned hash],
		      "a TAGGED string and an owned one are one value");
	covers("NSString", "isEqualToString:");
	covers("NSString", "stringWithUTF8String:");
	}

	/* The documented contract: -length counts UTF-16 CODE UNITS, the byte count has its own door, and
	 * -characterAtIndex: indexing UNITS. Built from
	 * explicit bytes so no source-escape ambiguity is involved. */
	{
		static const char bytes[] = { 'h', (char)0xC3, (char)0xA9, 'l', 'l', 'o', 0 };
		NSString *u = [NSString stringWithUTF8String:bytes];

		/* -length IS UTF-16 CODE UNITS (W1 slice 3, Apple's contract), and the
		 * BYTE count has its own door. Both are asserted, because the difference
		 * between them is the whole point of the unit. */
		check("utf8", [u length] == 5 &&
		      [u lengthOfBytesUsingEncoding:NSUTF8StringEncoding] == 6 &&
		      [u characterAtIndex:1] == 0xE9 &&
		      [[u substringWithRange:NSMakeRange(1, 1)] isEqualToString:@"\xC3\xA9"] &&
		      [[u substringFromIndex:1] isEqualToString:@"\xC3\xA9llo"] &&
		      [u rangeOfString:@"llo"].location == 2 &&
		      strcmp([u UTF8String], bytes) == 0,
		      "-length 5 UNITS (6 BYTES), and the ranges index units");
	covers("NSString", "length");
	covers("NSString", "lengthOfBytesUsingEncoding:");
	covers("NSString", "characterAtIndex:");
	covers("NSString", "substringWithRange:");
	covers("NSString", "substringFromIndex:");
	covers("NSString", "rangeOfString:");
	covers("NSString", "UTF8String");
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
	covers("NSMutableString", "appendString:");
	covers("NSString", "UTF8String");
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
	covers("NSString", "description");
	covers("NSString", "UTF8String");
	}

	/* A constant string that came from the other translation unit. */
	{
		check("cross-tu", [foundation_string_constant() length] == 4 &&
		      [foundation_string_constant() isEqualToString:@"supt"],
		      "a tagged constant string from the support unit");
	covers("NSString", "length");
	covers("NSString", "isEqualToString:");
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
			/* §63.236: the width-variant door, DEMANDED here because it ships now. */
			"variantFittingPresentationWidth:",
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
		/* §63.236: -variantFittingPresentationWidth: — a string this library builds carries no width
		 * variants, so the spelling that fits any width is the receiver ITSELF (Apple's answer for the
		 * no-variant case), and the check asserts identity rather than merely equality. */
		NSString *s = @"presentation";

		check("string-variant-fitting-presentation-width",
		      [s variantFittingPresentationWidth:40] == s,
		      "a string with no variants answers itself, identically, for every width");
	covers("NSString", "variantFittingPresentationWidth:");
	}


	{
		/*
		 * ENCODING INTROSPECTION, WHICH IS NOT THE BLOCKED CONVERSION CLUSTER (§63.2): each answer
		 * below is a fact about THIS library's storage rather than a conversion through a repertoire
		 * table, and each is asserted with its NUMBER. The two strings are a pure-7-bit one and one
		 * holding a single UTF-8 high byte (U+00E9), so -smallestEncoding is measured rather than
		 * guessed for both branches.
		 */
		const NSStringEncoding *available = [NSString availableStringEncodings];
		NSString *ascii = @"plain ascii";
		NSString *wide = [NSString stringWithUTF8String:"caf\xC3\xA9"];

		check("string-smallest-encoding",
		      [ascii smallestEncoding] == NSASCIIStringEncoding &&
		      [wide smallestEncoding] == NSUTF8StringEncoding,
		      "-smallestEncoding is ASCII (1) for a 7-bit string, UTF-8 (4) once a high byte is present");
	covers("NSString", "smallestEncoding");

		check("string-fastest-encoding",
		      [ascii fastestEncoding] == NSUTF8StringEncoding &&
		      [wide fastestEncoding] == NSUTF8StringEncoding,
		      "-fastestEncoding is the storage, UTF-8 (4), for both");
	covers("NSString", "fastestEncoding");

		check("string-default-cstring-encoding",
		      [NSString defaultCStringEncoding] == NSUTF8StringEncoding,
		      "+defaultCStringEncoding is UTF-8 (4), the one honest default C-string encoding here");
	covers("NSString", "defaultCStringEncoding");

		check("string-available-encodings",
		      available != NULL && available[0] == NSASCIIStringEncoding &&
		      available[1] == NSUTF8StringEncoding && available[2] != 0 &&
		      /* ⚠ §63.71: THE ASSERTION IS THE LIST'S MEANING AND NOT A FROZEN COUNT — every member
		       * after the two storage encodings must actually CONVERT and have a name, which is what makes the
		       * list and `-dataUsingEncoding:`/`+localizedNameOfStringEncoding:` agree. A count would have had to
		       * be edited for every converter added, and edited wrongly the day one stopped opening. */
		      [@"café" dataUsingEncoding:available[2]] != nil &&
		      [NSString localizedNameOfStringEncoding:available[2]] != nil,
		      "+​availableStringEncodings starts with ASCII(1) and UTF-8(4), its every later member CONVERTS and has a name, and the list is zero-terminated");
	covers("NSString", "availableStringEncodings");
	covers("NSString", "dataUsingEncoding:");
	covers("NSString", "localizedNameOfStringEncoding:");
	}

	{
		/* THE ENCODING INTROSPECTION DOORS §63.30 was still missing: the localized NAME of an encoding (ours,
		 * naming only what this library can store) and the lossy-flag form of -dataUsingEncoding:, whose flag
		 * can never change the answer here because the stored encodings are the lossless ones. Numbers carry. */
		NSString *utf8Name = [NSString localizedNameOfStringEncoding:NSUTF8StringEncoding];
		NSString *asciiName = [NSString localizedNameOfStringEncoding:NSASCIIStringEncoding];
		NSString *latinName = [NSString localizedNameOfStringEncoding:NSISOLatin1StringEncoding];
		NSData *wide = [@"café" dataUsingEncoding:NSUTF8StringEncoding];
		NSData *wideLossy = [@"café" dataUsingEncoding:NSUTF8StringEncoding allowLossyConversion:YES];
		NSData *wideLossless = [@"café" dataUsingEncoding:NSUTF8StringEncoding allowLossyConversion:NO];
		NSData *utf16 = [@"caf\xC3\xA9" dataUsingEncoding:NSUTF16StringEncoding allowLossyConversion:YES];

		check("string-encoding-introspection",
		      /* the names are ours, and they name what CONVERTS — including Latin-1, which stopped being
		       * "not stored" in §63.71 */
		      utf8Name != nil && [utf8Name isEqualToString:@"UTF-8"] && [utf8Name length] == 5 &&
		      asciiName != nil && [asciiName isEqualToString:@"ASCII"] && [asciiName length] == 5 &&
		      latinName != nil && [latinName length] > 0 &&
		      /* the lossy flag can never change the answer: all three forms of "café" agree at 5 UTF-8 bytes */
		      wide != nil && [wide length] == 5 &&
		      wideLossy != nil && [wideLossy length] == 5 && [wideLossy isEqualToData:wide] &&
		      wideLossless != nil && [wideLossless isEqualToData:wide] &&
		      /* ⚠ AND AN ENCODING WITH NO CONVERTER IS STILL REFUSED, whatever the flag says. THE ASSERTION
		       * IS SELF-CONSISTENT rather than a byte count this file would have to guess: the converted bytes
		       * and the door that counts them must agree. NeXTSTEP is the encoding no converter here claims. */
		      utf16 != nil &&
		      [utf16 length] == [@"caf\xC3\xA9" lengthOfBytesUsingEncoding:NSUTF16StringEncoding] &&
		      [@"caf\xC3\xA9" dataUsingEncoding:NSNEXTSTEPStringEncoding] == nil,
		      "+localizedNameOfStringEncoding: answers \"UTF-8\" (5), \"ASCII\" (5) and a name for Latin-1, which now CONVERTS; -dataUsingEncoding:allowLossyConversion: answers the same 5 UTF-8 bytes for \"café\" whether the flag is YES or NO; UTF-16's bytes agree with its own byte count; and NeXTSTEP — no converter here — is refused");
	covers("NSString", "localizedNameOfStringEncoding:");
	covers("NSString", "dataUsingEncoding:allowLossyConversion:");
	}


	{
		/*
		 * THE FILE/URL CONTENTS DOORS (§63.29's neighbours), EXERCISED AS A ROUND TRIP. The path is
		 * built from NSTemporaryDirectory() — this tree's own door, NOT a hard-named system path — so
		 * the write and the read meet the same bytes, and every assertion carries its NUMBER.
		 */
		NSString *dir = NSTemporaryDirectory();
		NSString *path = [dir stringByAppendingPathComponent:@"foundation-string-probe.txt"];
		NSString *body = @"foundation string file probe";	/* 28 bytes, all ASCII */
		NSString *readBack, *viaClass, *viaNoEnc, *viaDeprecated;
		NSError *err = nil;
		BOOL wrote = [body writeToFile:path atomically:NO encoding:NSUTF8StringEncoding error:&err];

		readBack = [[NSString alloc] initWithContentsOfFile:path encoding:NSUTF8StringEncoding error:NULL];
		viaClass = [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:NULL];
		viaNoEnc = [[NSString alloc] initWithContentsOfFile:path];	/* the deprecated no-encoding door */
		viaDeprecated = [NSString stringWithContentsOfFile:path];	/* its class twin */

		check("string-file-contents-doors",
		      wrote == YES && err == nil &&
		      [readBack isEqualToString:body] &&
		      [readBack lengthOfBytesUsingEncoding:NSUTF8StringEncoding] == 28 &&
		      [viaClass isEqualToString:body] && [viaNoEnc isEqualToString:body] &&
		      [viaDeprecated isEqualToString:body],
		      "writeToFile:atomically:encoding:error: writes 28 bytes and all four file read doors round-trip them");

		NSURL *url = [NSURL fileURLWithPath:path];
		NSString *body2 = @"url round trip 9";		/* 16 bytes, all ASCII */
		NSStringEncoding used = 0;
		NSString *urlRead, *urlUsed, *urlDeprecated;

		err = nil;
		wrote = [body2 writeToURL:url atomically:NO encoding:NSUTF8StringEncoding error:&err];
		urlRead = [[NSString alloc] initWithContentsOfURL:url encoding:NSUTF8StringEncoding error:NULL];
		urlUsed = [[NSString alloc] initWithContentsOfURL:url usedEncoding:&used error:NULL];
		urlDeprecated = [NSString stringWithContentsOfURL:url];

		check("string-url-contents-doors",
		      wrote == YES && err == nil &&
		      [urlRead isEqualToString:body2] &&
		      [urlRead lengthOfBytesUsingEncoding:NSUTF8StringEncoding] == 16 &&
		      [urlUsed isEqualToString:body2] && used == NSUTF8StringEncoding &&
		      [urlDeprecated isEqualToString:body2],
		      "writeToURL:atomically:encoding:error: writes 16 bytes and the URL read doors round-trip them, reporting UTF-8 (4)");
	covers("NSString", "writeToURL:atomically:encoding:error:");
	covers("NSString", "stringWithContentsOfURL:encoding:error:");
	covers("NSString", "stringWithContentsOfURL:usedEncoding:error:");

		NSURL *remote = [NSURL URLWithString:@"https://foundation.invalid/probe"];
		NSError *refused = nil;
		/*	§63.71: THIS USED UTF-16, WHICH NOW CONVERTS. The refusal this check is about is a REAL one,
		 * so it asks for an encoding no converter here claims — NeXTSTEP, which §63.70 gave its Apple
		 * value and which this library still cannot store. */
		BOOL encOk = [@"x" writeToURL:remote atomically:NO encoding:NSNEXTSTEPStringEncoding error:&refused];

		check("string-write-refuses-unstored-encoding",
		      encOk == NO && refused != nil &&
		      [refused code] == 517 && [[refused domain] isEqualToString:@"NSCocoaErrorDomain"],
		      "-writeToURL:…:encoding:…: answers NO with NSCocoaErrorDomain 517 for the unstored UTF-16 encoding (114)");
	}


	{
		/*
		 * THE LOCALE-TAKING FORMAT DOORS (§63.27's neighbours). The locale is IGNORED — a locale is
		 * honoured for CASE only in this library — so each answer is the locale-free one, and every
		 * assertion carries its NUMBER: -length in UTF-16 units.
		 */
		NSString *loc = [[NSString alloc] initWithFormat:@"%d-%@" locale:nil, 7, @"x"];
		NSString *viaList = fn_render_locale_arguments(@"%d+%d", 2, 3);
		NSString *localized = [NSString localizedStringWithFormat:@"%d items", 5];

		check("string-init-format-locale",
		      [loc isEqualToString:@"7-x"] && [loc length] == 3,
		      "-initWithFormat:locale: renders \"%d-%@\" with 7 and \"x\" as \"7-x\" (3 units)");
	covers("NSString", "initWithFormat:locale:");
	covers("NSString", "length");

		check("string-init-format-locale-arguments",
		      [viaList isEqualToString:@"2+3"] && [viaList length] == 3,
		      "-initWithFormat:locale:arguments: renders \"%d+%d\" with 2 and 3 as \"2+3\" (3 units)");
	covers("NSString", "initWithFormat:locale:arguments:");

		check("string-localized-string-with-format",
		      [localized isEqualToString:@"5 items"] && [localized length] == 7,
		      "+localizedStringWithFormat: renders \"%d items\" with 5 as \"5 items\" (7 units)");
	covers("NSString", "localizedStringWithFormat:");
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
	covers("NSString", "stringWithFormat:");
	covers("NSString", "isEqualToString:");

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
	covers("NSString", "stringWithFormat:");
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
	covers("NSString", "compare:");
	covers("NSString", "caseInsensitiveCompare:");
	covers("NSString", "compare:options:");
	covers("NSString", "hasPrefix:");
	covers("NSString", "hasSuffix:");
	covers("NSString", "containsString:");
	covers("NSString", "rangeOfString:");
	covers("NSString", "rangeOfString:options:range:");
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
		      [accented length] == 5 &&
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
	covers("NSString", "uppercaseString");
	covers("NSString", "lowercaseString");
	covers("NSString", "capitalizedString");
	covers("NSString", "substringFromIndex:");
	covers("NSString", "substringToIndex:");
	covers("NSString", "substringWithRange:");
	covers("NSString", "stringByAppendingString:");
	covers("NSString", "stringByAppendingFormat:");
	covers("NSString", "stringByReplacingOccurrencesOfString:withString:");
	covers("NSString", "stringByReplacingOccurrencesOfString:withString:options:range:");
	covers("NSString", "componentsSeparatedByString:");
	covers("NSString", "length");
	covers("NSString", "lengthOfBytesUsingEncoding:");
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
	covers("NSString", "intValue");
	covers("NSString", "integerValue");
	covers("NSString", "longLongValue");
	covers("NSString", "floatValue");
	covers("NSString", "doubleValue");
	covers("NSString", "boolValue");
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
	covers("NSString", "lastPathComponent");
	covers("NSString", "pathExtension");
	covers("NSString", "stringByDeletingLastPathComponent");
	covers("NSString", "stringByDeletingPathExtension");
	covers("NSString", "stringByAppendingPathComponent:");
	covers("NSString", "pathComponents");
	}

	{
		/* THE PATH DOORS THE LEDGER STILL HAD OPEN: joining, mapping, the filesystem representation, and the
		 * two tilde forms. ⚠ THE TILDE ASSERTIONS ARE MEASURED AGAINST NSHomeDirectory() — the same door the
		 * implementation reads — so they carry NO hard-named system path and hold wherever HOME is. The
		 * abbreviating direction is asserted as the EXACT INVERSE of expansion (expand then abbreviate), which
		 * is robust whether HOME ends in a slash or not, rather than assuming a particular home layout. */
		NSArray *joins = [NSArray arrayWithObjects:@"a", @"b", @"c", nil];
		NSArray *absoluteJoins = [NSArray arrayWithObjects:@"/", @"a", @"b", nil];
		NSArray *mapComponents = [NSArray arrayWithObjects:@"b", @"c", nil];
		NSString *joined = [NSString pathWithComponents:joins];
		NSString *absolute = [NSString pathWithComponents:absoluteJoins];
		NSArray *appended = [@"a" stringsByAppendingPaths:mapComponents];
		NSString *home = NSHomeDirectory();
		NSString *expanded = [@"~/dir/file" stringByExpandingTildeInPath];
		NSString *roundTrip = [expanded stringByAbbreviatingWithTildeInPath];
		char fsbuf[64];
		char tiny[2];

		check("string-path-doors",
		      /* joining: "a/b/c", and a leading "/" component makes it absolute rather than doubled */
		      joined != nil && [joined isEqualToString:@"a/b/c"] &&
		      absolute != nil && [absolute isEqualToString:@"/a/b"] &&
		      /* mapping: two elements, each the component-appended form */
		      appended != nil && [appended count] == 2 &&
		      [[appended objectAtIndex:0] isEqualToString:@"a/b"] &&
		      [[appended objectAtIndex:1] isEqualToString:@"a/c"] &&
		      /* the filesystem representation is the UTF-8 bytes, byte for byte */
		      strcmp([@"/a/b" fileSystemRepresentation], "/a/b") == 0 &&
		      /* the length-taking form copies when it fits (5 bytes with the NUL <= 64) and refuses a 2-byte
		       * buffer (5 > 2), leaving the small buffer alone */
		      [@"/a/b" getFileSystemRepresentation:fsbuf maxLength:sizeof fsbuf] == YES &&
		      strcmp(fsbuf, "/a/b") == 0 &&
		      [@"/a/b" getFileSystemRepresentation:tiny maxLength:sizeof tiny] == NO &&
		      /* `~` expands through the account database: the answer CHANGES, and it begins with HOME */
		      [home length] > 0 && expanded != nil &&
		      ![expanded isEqualToString:@"~/dir/file"] && [expanded hasPrefix:home] &&
		      /* and abbreviating is the inverse: the round trip is "~/dir/file" (10 units) */
		      [roundTrip isEqualToString:@"~/dir/file"] && [roundTrip length] == 10 &&
		      /* a path with no leading `~` is answered unchanged by both doors */
		      [[@"rel/path" stringByExpandingTildeInPath] isEqualToString:@"rel/path"] &&
		      [[@"rel/path" stringByAbbreviatingWithTildeInPath] isEqualToString:@"rel/path"],
		      "+pathWithComponents: joins \"a/b/c\" and \"/a/b\" (leading slash not doubled), -stringsByAppendingPaths: maps two elements to a/b and a/c, fileSystemRepresentation is \"/a/b\" byte for byte, getFileSystemRepresentation:maxLength: copies 5 bytes into a 64-byte buffer and refuses a 2-byte one, ~ expands to HOME and abbreviates back to \"~/dir/file\" (10 units), and a tilde-free path is unchanged");
	covers("NSString", "pathWithComponents:");
	covers("NSString", "stringsByAppendingPaths:");
	covers("NSString", "fileSystemRepresentation");
	covers("NSString", "getFileSystemRepresentation:maxLength:");
	covers("NSString", "stringByExpandingTildeInPath");
	covers("NSString", "stringByAbbreviatingWithTildeInPath");
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
		      /*	§63.71: UNICODE IS A STORED... no — it is CONVERTED now, so the assertion is the
		       * SELF-CONSISTENT one instead of a refusal: the bytes and the byte COUNT are the same
		       * conversion. That is the assertion that stays true whatever the converter's BOM is. */
		      [@"x" dataUsingEncoding:NSUnicodeStringEncoding] != nil &&
		      [[@"x" dataUsingEncoding:NSUnicodeStringEncoding] length] ==
			[@"x" lengthOfBytesUsingEncoding:NSUnicodeStringEncoding] &&
		      [@"x" dataUsingEncoding:NSNEXTSTEPStringEncoding] == nil,
		      "the UTF-8 round trip, and honest answers for the encodings we do not store");
	covers("NSString", "dataUsingEncoding:");
	covers("NSString", "initWithData:encoding:");
	covers("NSString", "lengthOfBytesUsingEncoding:");
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
		      s != nil && [s length] == 4 &&
		      [s characterAtIndex:1] == 0xD83D && [s characterAtIndex:2] == 0xDE00 &&
		      [s lengthOfBytesUsingEncoding:NSUTF8StringEncoding] == 6 &&
		      strcmp([s UTF8String], "h\xF0\x9F\x98\x80i") == 0 &&
		      readBack[0] == 'h' && readBack[1] == 0xD83D &&
		      readBack[2] == 0xDE00 && readBack[3] == 'i' &&
		      [[[NSString alloc] initWithCharactersNoCopy:(unichar *)pool length:2
						     freeWhenDone:NO] isEqualToString:@"OK"] &&
		      [borrowed isEqualToString:@"OK!"] && pool[0] == 'O' && pool[1] == 'K',
		      "a surrogate pair is 1 character / 2 units / 4 bytes; -getCharacters:range: reads units back; and a borrowed buffer is never written");
	covers("NSString", "stringWithCharacters:length:");
	covers("NSString", "getCharacters:range:");
	covers("NSString", "characterAtIndex:");
	covers("NSString", "lengthOfBytesUsingEncoding:");
	covers("NSString", "UTF8String");
	covers("NSString", "initWithCharactersNoCopy:length:freeWhenDone:");
	covers("NSMutableString", "appendString:");
		{
			/* freeWhenDone:YES hands ownership over, so this one is freed by the string. */
			unichar *owned = (unichar *)malloc(2 * sizeof(unichar));

			owned[0] = 'y';
			owned[1] = 'y';
			check("characters-family-ownership",
			      [[[NSString alloc] initWithCharactersNoCopy:owned length:2
							     freeWhenDone:YES] isEqualToString:@"yy"],
			      "freeWhenDone:YES transfers the buffer to the receiver");
	covers("NSString", "initWithCharactersNoCopy:length:freeWhenDone:");
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
	covers("NSMutableString", "stringWithCapacity:");
	covers("NSMutableString", "appendFormat:");
	covers("NSMutableString", "appendString:");
	covers("NSMutableString", "insertString:atIndex:");
	covers("NSMutableString", "deleteCharactersInRange:");
	covers("NSMutableString", "replaceCharactersInRange:withString:");
	covers("NSMutableString", "replaceOccurrencesOfString:withString:options:range:");
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
			/* §63.58: `characterSet` was listed here and is GONE — it was this tree's shorthand for
			 * `[[NSMutableCharacterSet alloc] init]` and Apple declares no bare `characterSet` on either
			 * class. The list is now EMPTY and that is the honest statement: the public mutable subclass adds
			 * mutators, not class constructors, so there is nothing of its own for the inventory to demand.
			 * KEPT AS AN EMPTY ARRAY rather than deleted so the audit line below still says which class it
			 * covered. */
NULL
		};
		static const char *instanceSelectors[] = {
			"initWithCharactersInString:", "initWithRange:", "characterIsMember:",
			"invertedSet", "isSupersetOfSet:",
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
		      [@"\xC4\xB0" length] == 1 &&
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
	covers("NSLocale", "currentLocale");
	covers("NSLocale", "localeIdentifier");
	covers("NSString", "localizedCaseInsensitiveCompare:");
	}

	{
		/* The audited Cocoa inventory for NSLocale: what SHIPS, and — asserted
		 * absent — the data-driven half, which needs the locale database. */
		static const char *classSelectors[] = {
			"currentLocale", "localeWithLocaleIdentifier:",
			/* §62.24 retired the "no data" boundary the exclusion below once drew: ICU ships these,
			 * so they SHIP (moved out of classExcluded in the 2026-09-30 locale slice). */
			"autoupdatingCurrentLocale", "systemLocale",
			"ISOLanguageCodes", "ISOCountryCodes", "ISOCurrencyCodes", "commonISOCurrencyCodes",
			"localeIdentifierFromWindowsLocaleCode:", "windowsLocaleCodeFromLocaleIdentifier:",
			"characterDirectionForLanguage:", "lineDirectionForLanguage:",
			"availableLocaleIdentifiers",
			"componentsFromLocaleIdentifier:", "localeIdentifierFromComponents:",
			"canonicalLanguageIdentifierFromString:",
			"canonicalLocaleIdentifierFromString:",
			/* §63.235 SHIPPED the ordered-preference door with its reading stated: the list this system
			 * answers is the ONE locale it RESOLVES rather than an ordering invented here. It moved UP out
			 * of classExcluded below, which is what the inventory rule asks for. */
			"preferredLanguages", NULL
		};
		static const char *classExcluded[] = {
			/* EMPTY NOW, and that is the point: the one class-side row that used to be named here was
			 * +preferredLanguages ("a user's ordered PREFERRED-LANGUAGE LIST is a PREFERENCE, not locale
			 * data ... and ICU has no API for it"), and §63.235 shipped it. */
			NULL
		};
		static const char *instanceSelectors[] = {
			"initWithLocaleIdentifier:", "localeIdentifier", "objectForKey:",
			"displayNameForKey:value:",
			/* THE -localizedStringFor…: DOORS ARE INSTANCE METHODS: Apple declares them on NSLocale
			 * (the receiver is the display locale), so they belong HERE, not in the class array — the
			 * class array is asked with [NSLocale respondsToSelector:], and these are not class doors. */
			"localizedStringForLocaleIdentifier:", "localizedStringForLanguageCode:",
			"localizedStringForCountryCode:", "localizedStringForScriptCode:",
			"localizedStringForCalendarIdentifier:", "localizedStringForCollationIdentifier:",
			"localizedStringForCollatorIdentifier:", "localizedStringForCurrencyCode:",
			/* The deprecated subtag spellings (§62.24 says they are OWED, and they moved out of the
			 * exclusion below), then the subtag doors and the keys ICU now answers. */
			"languageCode", "scriptCode", "countryCode", "variantCode", "regionCode",
			"languageIdentifier",
			"calendarIdentifier", "collationIdentifier", "collatorIdentifier",
			"currencyCode", "currencySymbol", "decimalSeparator", "groupingSeparator",
			"quotationBeginDelimiter", "quotationEndDelimiter",
			"alternateQuotationBeginDelimiter", "alternateQuotationEndDelimiter",
			"exemplarCharacterSet", "usesMetricSystem",
			"isEqual:", "hash", "description", "copy", NULL
		};
		static const char *excluded[] = {
			/* -displayNameForKey:value: USED TO BE LISTED HERE as needing "the locale's name
			 * tables". It is DEMANDED above now: the tables are ICU's and this library already
			 * links them (§18). */
			/* -localizedStringForVariantCode: IS the one locale row that stays out: ICU's variant
			 * display table answers EMPTY on this data (measured: uloc_getDisplayVariant returns
			 * length 0 for 1901/POSIX/BOONT/VALENCIA/SAAHO/AREVELA/BISKE/POLYTONI in en_US and de_DE),
			 * so a door for it could never open. Asserted ABSENT. */
			"localizedStringForVariantCode:", NULL
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
	covers("NSCharacterSet", "bitmapRepresentation");
	covers("NSCharacterSet", "characterSetWithBitmapRepresentation:");
	covers("NSCharacterSet", "characterSetWithContentsOfFile:");
	covers("NSCharacterSet", "longCharacterIsMember:");
	covers("NSCharacterSet", "hasMemberInPlane:");
	covers("NSCharacterSet", "characterIsMember:");
	covers("NSData", "writeToFile:atomically:");
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
	covers("NSCharacterSet", "illegalCharacterSet");
	covers("NSCharacterSet", "characterIsMember:");
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
	covers("NSCharacterSet", "symbolCharacterSet");
	covers("NSCharacterSet", "characterIsMember:");

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
	covers("NSCharacterSet", "capitalizedLetterCharacterSet");
	covers("NSCharacterSet", "characterIsMember:");
	covers("NSCharacterSet", "uppercaseLetterCharacterSet");
	covers("NSCharacterSet", "isSupersetOfSet:");

		check("charset-non-base",
		      marks != nil &&
		      [marks characterIsMember:(unichar)0x0301] &&	/* Mn: combining acute */
		      [marks characterIsMember:(unichar)0x20DD] &&	/* Me: combining enclosing circle */
		      [marks characterIsMember:(unichar)0x0903] &&	/* Mc: devanagari sign visarga */
		      ![marks characterIsMember:(unichar)'a'] &&
		      ![marks characterIsMember:(unichar)0x00C0],
		      "M*: Mn, Me and Mc in; a letter and the PRECOMPOSED A-grave out");
	covers("NSCharacterSet", "nonBaseCharacterSet");
	covers("NSCharacterSet", "characterIsMember:");

		check("charset-decomposable",
		      decomposable != nil &&
		      [decomposable characterIsMember:(unichar)0x00C0] &&	/* A-grave: canonical */
		      ![decomposable characterIsMember:(unichar)0xFB00] &&	/* ff ligature: COMPATIBILITY */
		      ![decomposable characterIsMember:(unichar)0x00A0] &&	/* noBreak space: noBreak */
		      ![decomposable characterIsMember:(unichar)'A'],
		      "STANDARD decomposition only: A-grave in, the ff ligature and noBreak space out");
	covers("NSCharacterSet", "decomposableCharacterSet");
	covers("NSCharacterSet", "characterIsMember:");
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
	covers("NSCharacterSet", "whitespaceCharacterSet");
	covers("NSCharacterSet", "whitespaceAndNewlineCharacterSet");
	covers("NSCharacterSet", "newlineCharacterSet");
	covers("NSCharacterSet", "characterIsMember:");

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
	covers("NSCharacterSet", "uppercaseLetterCharacterSet");
	covers("NSCharacterSet", "lowercaseLetterCharacterSet");
	covers("NSCharacterSet", "letterCharacterSet");
	covers("NSCharacterSet", "alphanumericCharacterSet");
	covers("NSCharacterSet", "characterIsMember:");
	covers("NSCharacterSet", "isSupersetOfSet:");

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
	covers("NSCharacterSet", "punctuationCharacterSet");
	covers("NSCharacterSet", "controlCharacterSet");
	covers("NSCharacterSet", "characterIsMember:");
	}

	{
		/* THE DISPLAY NAMES (D7's kind (D), landed §18). Apple's OWN examples are the assertions:
		 * on en_GB, naming fr_FR is "French (France)" and en_US is "English (United States)"; on
		 * fr_FR the same value comes back in French. The nil cases are Apple's too - its page
		 * allows them ("not all locale property keys have values with display name values") - and
		 * a NON-STRING value is nil rather than a guess. */
		NSLocale *british = [NSLocale localeWithLocaleIdentifier:@"en_GB"];
		NSLocale *french = [NSLocale localeWithLocaleIdentifier:@"fr_FR"];

		check("locale-display-names",
		      british != nil && french != nil &&
		      [[british displayNameForKey:NSLocaleIdentifier value:@"fr_FR"] isEqualToString:@"French (France)"] &&
		      [[british displayNameForKey:NSLocaleIdentifier value:@"en_US"] isEqualToString:@"English (United States)"] &&
		      [[french displayNameForKey:NSLocaleIdentifier value:@"fr_FR"] isEqualToString:@"français (France)"] &&
		      [[british displayNameForKey:NSLocaleLanguageCode value:@"fr"] isEqualToString:@"French"] &&
		      [[british displayNameForKey:NSLocaleCountryCode value:@"GB"] isEqualToString:@"United Kingdom"] &&
		      [[british displayNameForKey:NSLocaleScriptCode value:@"Latn"] isEqualToString:@"Latin"] &&
		      [british displayNameForKey:@"NSLocaleCurrencyCode" value:@"EUR"] == nil &&
		      [british displayNameForKey:NSLocaleLanguageCode value:[NSNumber numberWithInt:7]] == nil,
		      [[NSString stringWithFormat:@"en_GB/fr_FR=%@ en_US=%@ fr=%@ GB=%@ Latn=%@",
			[british displayNameForKey:NSLocaleIdentifier value:@"fr_FR"],
			[british displayNameForKey:NSLocaleIdentifier value:@"en_US"],
			[british displayNameForKey:NSLocaleLanguageCode value:@"fr"],
			[british displayNameForKey:NSLocaleCountryCode value:@"GB"],
			[british displayNameForKey:NSLocaleScriptCode value:@"Latn"]] UTF8String]);
	covers("NSLocale", "displayNameForKey:value:");
	covers("NSLocale", "localeWithLocaleIdentifier:");
	}

	{
		/* ---- THE DATA HALF, NOW THAT ICU IS BOUND (the 2026-09-30 locale slice) --------------------------
		 *
		 * The two checks above are the SPEC and stay green: -objectForKey: answers the SUBTAG keys and
		 * nils every DATA key, and -displayNameForKey:value: names the SUBTAG keys and nils the rest.
		 * THESE checks open the NEW doors - the property family and -localizedStringFor…: - which read
		 * ICU DIRECTLY, deliberately NOT through those two narrow doors. Every number was measured on
		 * the host against ICU 76.1 (the version the guest stages) before the check was written, so a
		 * method that returned a constant compiled into the library would fail here. */
		NSLocale *us = [NSLocale localeWithLocaleIdentifier:@"en_US"];
		NSLocale *fr = [NSLocale localeWithLocaleIdentifier:@"fr_FR"];
		NSLocale *gb = [NSLocale localeWithLocaleIdentifier:@"en_GB"];

		{
			NSLocaleLanguageDirection rtl = [NSLocale characterDirectionForLanguage:@"ar"];
			NSLocaleLanguageDirection ltr = [NSLocale characterDirectionForLanguage:@"en"];
			NSLocaleLanguageDirection line = [NSLocale lineDirectionForLanguage:@"en"];

			check("locale-direction-follows-the-script",
			      rtl == NSLocaleLanguageDirectionRightToLeft &&
			      ltr == NSLocaleLanguageDirectionLeftToRight &&
			      line == NSLocaleLanguageDirectionTopToBottom,
			      [[NSString stringWithFormat:@"ar=%d en=%d line=%d (RTL=2 LTR=1 TTB=3)",
				(int)rtl, (int)ltr, (int)line] UTF8String]);
		}
		{
			uint32_t lcidUs = [NSLocale windowsLocaleCodeFromLocaleIdentifier:@"en_US"];
			uint32_t lcidFr = [NSLocale windowsLocaleCodeFromLocaleIdentifier:@"fr_FR"];
			NSString *back = [NSLocale localeIdentifierFromWindowsLocaleCode:1033];

			check("locale-windows-locale-code-round-trips",
			      lcidUs == 1033 && lcidFr == 1036 && [back isEqualToString:@"en_US"],
			      [[NSString stringWithFormat:@"en_US=%u fr_FR=%u 1033=%@", (unsigned)lcidUs,
				(unsigned)lcidFr, back] UTF8String]);
		}
		check("locale-localized-string-names-a-language",
		      [[gb localizedStringForLanguageCode:@"fr"] isEqualToString:@"French"] &&
		      [[fr localizedStringForLanguageCode:@"fr"] isEqualToString:@"français"],
		      [[NSString stringWithFormat:@"en_GB=%@ fr_FR=%@",
			[gb localizedStringForLanguageCode:@"fr"],
			[fr localizedStringForLanguageCode:@"fr"]] UTF8String]);
		check("locale-localized-string-names-a-country",
		      [[gb localizedStringForCountryCode:@"GB"] isEqualToString:@"United Kingdom"] &&
		      [[fr localizedStringForCountryCode:@"GB"] isEqualToString:@"Royaume-Uni"],
		      [[NSString stringWithFormat:@"en_GB=%@ fr_FR=%@",
			[gb localizedStringForCountryCode:@"GB"],
			[fr localizedStringForCountryCode:@"GB"]] UTF8String]);
	covers("NSLocale", "localizedStringForCountryCode:");
		check("locale-localized-string-names-a-currency",
		      [[us localizedStringForCurrencyCode:@"EUR"] isEqualToString:@"Euro"],
		      [[NSString stringWithFormat:@"en_US EUR=%@",
			[us localizedStringForCurrencyCode:@"EUR"]] UTF8String]);
	covers("NSLocale", "localizedStringForCurrencyCode:");
		check("locale-localized-string-names-a-calendar",
		      [[us localizedStringForCalendarIdentifier:@"hebrew"] isEqualToString:@"Hebrew Calendar"],
		      [[NSString stringWithFormat:@"en_US hebrew=%@",
			[us localizedStringForCalendarIdentifier:@"hebrew"]] UTF8String]);
	covers("NSLocale", "localizedStringForCalendarIdentifier:");
		check("locale-separators-come-from-the-locale",
		      [[us decimalSeparator] isEqualToString:@"."] &&
		      [[us groupingSeparator] isEqualToString:@","] &&
		      [[fr decimalSeparator] isEqualToString:@","] &&
		      [[fr groupingSeparator] isEqualToString:[NSString stringWithFormat:@"%C", (unichar)0x202F]],
		      [[NSString stringWithFormat:@"en_US '%@'/'%@' fr_FR '%@'/U+%04X",
			[us decimalSeparator], [us groupingSeparator], [fr decimalSeparator],
			(unsigned)[[fr groupingSeparator] length] > 0 ?
				(unsigned)[[fr groupingSeparator] characterAtIndex:0] : 0] UTF8String]);
	covers("NSLocale", "decimalSeparator");
	covers("NSLocale", "groupingSeparator");
		check("locale-quotation-delimiters-are-the-locales",
		      [[us quotationBeginDelimiter] isEqualToString:[NSString stringWithFormat:@"%C", (unichar)0x201C]] &&
		      [[us quotationEndDelimiter] isEqualToString:[NSString stringWithFormat:@"%C", (unichar)0x201D]] &&
		      [[fr quotationBeginDelimiter] isEqualToString:[NSString stringWithFormat:@"%C", (unichar)0x00AB]] &&
		      [[fr quotationEndDelimiter] isEqualToString:[NSString stringWithFormat:@"%C", (unichar)0x00BB]],
		      "en_US U+201C/U+201D and fr_FR U+00AB/U+00BB");
	covers("NSLocale", "quotationBeginDelimiter");
	covers("NSLocale", "quotationEndDelimiter");
		check("locale-currency-code-and-symbol-are-the-locales",
		      [[us currencyCode] isEqualToString:@"USD"] &&
		      [[us currencySymbol] isEqualToString:@"$"] &&
		      [[fr currencyCode] isEqualToString:@"EUR"],
		      [[NSString stringWithFormat:@"en_US %@/%@ fr_FR %@", [us currencyCode],
			[us currencySymbol], [fr currencyCode]] UTF8String]);
	covers("NSLocale", "currencyCode");
	covers("NSLocale", "currencySymbol");
		check("locale-calendar-and-collation-come-from-the-locale",
		      [[us calendarIdentifier] isEqualToString:@"gregorian"] &&
		      [[us collationIdentifier] isEqualToString:@"standard"],
		      [[NSString stringWithFormat:@"calendar=%@ collation=%@", [us calendarIdentifier],
			[us collationIdentifier]] UTF8String]);
	covers("NSLocale", "calendarIdentifier");
	covers("NSLocale", "collationIdentifier");
		{
			NSCharacterSet *exemplar = [us exemplarCharacterSet];

			check("locale-exemplar-set-is-the-locales-alphabet",
			      exemplar != nil && [exemplar characterIsMember:(unichar)'a'] &&
			      [exemplar characterIsMember:(unichar)'z'] &&
			      ![exemplar characterIsMember:(unichar)'0'],
			      [[NSString stringWithFormat:@"a=%d z=%d 0=%d",
				(int)[exemplar characterIsMember:(unichar)'a'],
				(int)[exemplar characterIsMember:(unichar)'z'],
				(int)[exemplar characterIsMember:(unichar)'0']] UTF8String]);
	covers("NSLocale", "exemplarCharacterSet");
	covers("NSCharacterSet", "characterIsMember:");
		}
		check("locale-measurement-system-is-the-locales",
		      ![us usesMetricSystem] && [fr usesMetricSystem] && [gb usesMetricSystem],
		      [[NSString stringWithFormat:@"en_US=%d fr_FR=%d en_GB=%d", (int)[us usesMetricSystem],
			(int)[fr usesMetricSystem], (int)[gb usesMetricSystem]] UTF8String]);
	covers("NSLocale", "usesMetricSystem");
		{
			NSArray *langs = [NSLocale ISOLanguageCodes];
			NSArray *ctries = [NSLocale ISOCountryCodes];
			NSArray *curs = [NSLocale ISOCurrencyCodes];
			NSArray *common = [NSLocale commonISOCurrencyCodes];

			check("locale-iso-catalogues-are-populated",
			      [langs count] > 100 && [langs containsObject:@"en"] &&
			      [ctries count] > 100 && [ctries containsObject:@"US"] &&
			      [curs containsObject:@"USD"] && [common count] > 100,
			      [[NSString stringWithFormat:@"langs=%lu(en=%d) countries=%lu(US=%d) USD=%d common=%lu",
				(unsigned long)[langs count], (int)[langs containsObject:@"en"],
				(unsigned long)[ctries count], (int)[ctries containsObject:@"US"],
				(int)[curs containsObject:@"USD"], (unsigned long)[common count]] UTF8String]);
	covers("NSLocale", "ISOLanguageCodes");
	covers("NSLocale", "ISOCountryCodes");
	covers("NSLocale", "ISOCurrencyCodes");
	covers("NSLocale", "commonISOCurrencyCodes");
		}
		{
			NSLocale *sys = [NSLocale systemLocale];
			NSLocale *automatic = [NSLocale autoupdatingCurrentLocale];

			check("locale-sources-answer",
			      sys != nil && [[sys localeIdentifier] length] > 0 && automatic != nil,
			      [[NSString stringWithFormat:@"system=%@ autoupdating=%@", [sys localeIdentifier],
				[automatic localeIdentifier]] UTF8String]);
	covers("NSLocale", "systemLocale");
	covers("NSLocale", "autoupdatingCurrentLocale");
	covers("NSLocale", "localeIdentifier");
		}
		{
			NSLocale *zh = [NSLocale localeWithLocaleIdentifier:@"zh_Hans_CN"];
			NSLocale *ca = [NSLocale localeWithLocaleIdentifier:@"ca_ES_VALENCIA"];

			check("locale-subtags-are-the-identifiers-own",
			      [[zh languageCode] isEqualToString:@"zh"] &&
			      [[zh scriptCode] isEqualToString:@"Hans"] &&
			      [[zh regionCode] isEqualToString:@"CN"] &&
			      [[zh languageIdentifier] isEqualToString:@"zh-Hans"] &&
			      [[ca variantCode] isEqualToString:@"VALENCIA"],
			      [[NSString stringWithFormat:@"zh %@/%@/%@=>%@ ca variant=%@",
				[zh languageCode], [zh scriptCode], [zh regionCode], [zh languageIdentifier],
				[ca variantCode]] UTF8String]);
	covers("NSLocale", "languageCode");
	covers("NSLocale", "scriptCode");
	covers("NSLocale", "regionCode");
	covers("NSLocale", "languageIdentifier");
	covers("NSLocale", "variantCode");
		}
		/* THE REGRESSION GUARDS: the two narrow doors above must keep nilling the DATA keys, or the
		 * locale-basics and locale-display-names checks go red again (which is exactly what an earlier
		 * version of this work did). */
		check("locale-data-keys-stay-nil-through-the-two-narrow-doors",
		      [us objectForKey:NSLocaleDecimalSeparator] == nil &&
		      [gb displayNameForKey:NSLocaleCurrencyCode value:@"EUR"] == nil &&
		      [gb displayNameForKey:NSLocaleLanguageCode value:[NSNumber numberWithInt:7]] == nil,
		      [[NSString stringWithFormat:@"objectForKey:decimal=%@ currencyName=%@ nonStringName=%@",
			[us objectForKey:NSLocaleDecimalSeparator],
			[gb displayNameForKey:NSLocaleCurrencyCode value:@"EUR"],
			[gb displayNameForKey:NSLocaleLanguageCode value:[NSNumber numberWithInt:7]]] UTF8String]);
	covers("NSLocale", "objectForKey:");
	covers("NSLocale", "displayNameForKey:value:");
		check("locale-variant-display-stays-open",
		      ![us respondsToSelector:sel_registerName("localizedStringForVariantCode:")],
		      "-localizedStringForVariantCode: is absent (ICU's variant display table answers empty)");
	}

	{
		/* THE ENCODING AND OPTION SETS, AND THE PROPERTY THAT MATTERS: the options are a BIT SET, so no two of
		 * them may share a value, and the two legacy members keep the numbers they have always had (a search
		 * with NSLiteralSearch must still mean "no options" and NSCaseInsensitiveSearch must still be 1).
		 * A collision here would make an option unrequestable, silently. */
		unsigned long opt[] = { (unsigned long)NSLiteralSearch, (unsigned long)NSCaseInsensitiveSearch,
			(unsigned long)NSAnchoredSearch, (unsigned long)NSBackwardsSearch,
			(unsigned long)NSDiacriticInsensitiveSearch, (unsigned long)NSForcedOrderingSearch,
			(unsigned long)NSNumericSearch, (unsigned long)NSRegularExpressionSearch,
			(unsigned long)NSWidthInsensitiveSearch };
		int distinct = 1;
		unsigned i, j;

		for (i = 0; i < sizeof opt / sizeof opt[0]; i++) {
			if (i > 0 && opt[i] != 0 && (opt[i] & (opt[i] - 1)) == 0) {
				/* a power of two (or zero for NSLiteralSearch): what a bit set member must be */
			} else if (i > 0) {
				distinct = 0;
			}
			for (j = i + 1; j < sizeof opt / sizeof opt[0]; j++) {
				if (opt[i] != 0 && opt[i] == opt[j]) {
					distinct = 0;
				}
			}
		}
		check("option-set-members-are-distinct-bits",
		      distinct && NSLiteralSearch == 0 && NSCaseInsensitiveSearch == 1 &&
		      (NSBackwardsSearch & NSCaseInsensitiveSearch) == 0 &&
		      NSASCIIStringEncoding == 1 && NSUTF8StringEncoding == 4 &&
		      NSUnicodeStringEncoding == 10 && NSUTF16StringEncoding == NSUnicodeStringEncoding &&
		      NSISOLatin1StringEncoding == 5 && NSShiftJISStringEncoding == 8 &&
		      NSWindowsCP1252StringEncoding == 12 && NSMacOSRomanStringEncoding == 30 &&
		      NSUTF16LittleEndianStringEncoding == 0x94000100 && NSUTF32BigEndianStringEncoding == 0x98000100,
		      "the values are APPLE'S, not a private block: measured against NSString.h in the SDK \u2014 three "
		      "were right and twenty-one were not before \u00a763.70");
		check("transforms-and-keys-are-their-own-names",
		      [NSStringTransformLatinToCyrillic isEqualToString:@"NSStringTransformLatinToCyrillic"] &&
		      [NSStringTransformStripDiacritics isEqualToString:@"NSStringTransformStripDiacritics"] &&
		      [NSStringEncodingDetectionSuggestedEncodingsKey
			isEqualToString:@"NSStringEncodingDetectionSuggestedEncodingsKey"] &&
		      [NSCharacterConversionException isEqualToString:@"NSCharacterConversionException"] &&
		      [NSParseErrorException isEqualToString:@"NSParseErrorException"],
		      "each transform, detection key and exception name equals its own name");
	}

	{
		/* THE LOCALE KEYS ARE THE CONVENTION THIS LIBRARY ALREADY HAD (value == name), and the property worth
		 * asserting is that they are DISTINCT - two keys sharing a string would make two locale properties
		 * indistinguishable - plus that the locale-change notification is one of them. */
		check("locale-keys-are-their-own-names",
		      [NSLocaleCurrencyCode isEqualToString:@"NSLocaleCurrencyCode"] &&
		      [NSLocaleVariantCode isEqualToString:@"NSLocaleVariantCode"] &&
		      [NSLocaleUsesMetricSystem isEqualToString:@"NSLocaleUsesMetricSystem"] &&
		      [NSCurrentLocaleDidChangeNotification isEqualToString:@"NSCurrentLocaleDidChangeNotification"] &&
		      [NSLocaleCurrencyCode isEqualToString:NSLocaleVariantCode] == 0,
		      "four locale keys read back as themselves and two stay distinct");
	}

	{
		/* THE LINE DOORS, AND THE CHECKS SKIP THE ARITHMETIC IN FAVOUR OF THE TWO THINGS A READER WILL
		 * DOUBT: (1) NEL (U+0085) is on Apple's terminator list and is the one easy to forget, so a
		 * string carrying it must break the same way as one carrying \n; (2) CRLF IS ONE TERMINATOR, so
		 * the line's `end` covers BOTH units while `contentsEnd` stops before them. */
		NSString *crlf = @"one\r\ntwo";          /* o0 n1 e2 \r3 \n4 t5 w6 o7  (length 8) */
		/* NEL CANNOT BE SPELLED AS `\u0085` — C refuses a universal character name that denotes a
		 * CONTROL character, and clang says so ("universal character name refers to a control
		 * character"). It is BUILT rather than written, which also makes the test unambiguous about
		 * what it is testing: one U+0085 code unit between the words. */
		unichar nelUnits[7] = { 'o', 'n', 'e', 0x0085, 't', 'w', 'o' };
		NSString *nel = [NSString stringWithCharacters:nelUnits length:7];
		NSString *mixed = @"a\u2028b\u2029c";    /* LS then PS, each one code unit */
		NSUInteger s = 0, e = 0, c = 0;
		NSRange r;

		[crlf getLineStart:&s end:&e contentsEnd:&c forRange:NSMakeRange(0, 0)];
		check("line-crlf-is-one-terminator",
		      s == 0 && c == 3 && e == 5,
		      "CRLF ends the text at contentsEnd 3 and the line itself at end 5 - one terminator, not two");
	covers("NSString", "getLineStart:end:contentsEnd:forRange:");

		[crlf getLineStart:&s end:&e contentsEnd:&c forRange:NSMakeRange(5, 0)];
		check("line-after-crlf-starts-past-both-units",
		      s == 5 && c == 8 && e == 8,
		      "the second line starts at 5, so the CRLF was consumed whole");
	covers("NSString", "getLineStart:end:contentsEnd:forRange:");

		[nel getLineStart:&s end:&e contentsEnd:&c forRange:NSMakeRange(0, 0)];
		check("line-nel-terminates",
		      s == 0 && c == 3 && e == 4,
		      "U+0085 ends a line: contentsEnd 3, end 4");
	covers("NSString", "getLineStart:end:contentsEnd:forRange:");

		[nel getLineStart:&s end:&e contentsEnd:&c forRange:NSMakeRange(4, 0)];
		check("line-starts-after-nel",
		      s == 4 && c == 7,
		      "the text after a NEL is its own line");
	covers("NSString", "getLineStart:end:contentsEnd:forRange:");

		[mixed getLineStart:&s end:&e contentsEnd:&c forRange:NSMakeRange(3, 0)];
		check("line-ls-and-ps-terminate",
		      s == 2 && c == 3 && e == 4,
		      "LS and PS break lines too: the middle line of \"a<LS>b<PS>c\" is b");
	covers("NSString", "getLineStart:end:contentsEnd:forRange:");

		r = [crlf lineRangeForRange:NSMakeRange(0, 0)];
		check("line-range-includes-the-terminator",
		      r.location == 0 && r.length == 5,
		      "the line RANGE covers the terminator while contentsEnd does not");
	covers("NSString", "lineRangeForRange:");

		r = [crlf lineRangeForRange:NSMakeRange(1, 5)];
		check("line-range-is-the-line-containing-the-range",
		      r.location == 0 && r.length == 8,
		      "a range reaching into the next line answers both lines, whole");
	covers("NSString", "lineRangeForRange:");

		{
			NSMutableArray *lines = [NSMutableArray array];

			[@"a\nb" enumerateLinesUsingBlock:^(NSString *line, BOOL *stop) {
				[lines addObject:line];
			}];
			check("line-enumerator-drops-the-terminator",
			      [lines count] == 2 &&
			      [[lines objectAtIndex:0] isEqualToString:@"a"] &&
			      [[lines objectAtIndex:1] isEqualToString:@"b"],
			      "two lines, neither carrying its terminator");
	covers("NSString", "enumerateLinesUsingBlock:");

			lines = [NSMutableArray array];
			[@"a\n\nb" enumerateLinesUsingBlock:^(NSString *line, BOOL *stop) {
				[lines addObject:line];
			}];
			check("line-enumerator-keeps-a-blank-line",
			      [lines count] == 3 && [[lines objectAtIndex:1] length] == 0,
			      "a blank line is a line; dropping it would change a caller's line count");
	covers("NSString", "enumerateLinesUsingBlock:");

			lines = [NSMutableArray array];
			[@"one\n" enumerateLinesUsingBlock:^(NSString *line, BOOL *stop) {
				[lines addObject:line];
			}];
			check("line-enumerator-has-no-trailing-empty-line",
			      [lines count] == 1 && [[lines objectAtIndex:0] isEqualToString:@"one"],
			      "a trailing terminator does not make a phantom final line");
	covers("NSString", "enumerateLinesUsingBlock:");

			lines = [NSMutableArray array];
			[@"" enumerateLinesUsingBlock:^(NSString *line, BOOL *stop) {
				[lines addObject:line];
			}];
			check("line-enumerator-of-an-empty-string-yields-nothing",
			      [lines count] == 0,
			      "no lines at all, rather than one empty one");
	covers("NSString", "enumerateLinesUsingBlock:");

			lines = [NSMutableArray array];
			[@"a\nb\nc" enumerateLinesUsingBlock:^(NSString *line, BOOL *stop) {
				[lines addObject:line];
				if ([lines count] == 2) {
					*stop = YES;
				}
			}];
			check("line-enumerator-honours-stop",
			      [lines count] == 2,
			      "setting *stop ends the walk");
	covers("NSString", "enumerateLinesUsingBlock:");
		}

		{
			int caught = 0;

			@try {
				[crlf lineRangeForRange:NSMakeRange(1, 99)];
			} @catch (NSException *exception) {
				caught = [exception.name isEqualToString:NSRangeException];
			}
			check("line-invalid-range-raises",
			      caught,
			      "a range past the end raises NSRangeException, as Apple's page says it does");
	covers("NSString", "lineRangeForRange:");
		}
	}

	{
		/* THE OPTIONS DOORS. The clauses worth checking are the ones a reader would DOUBT: the option
		 * pair Apple names and no others, the two DIFFERENT exceptions, the no-normalization rule the
		 * page states outright, and the receiver-spelling rule for a common prefix. */
		NSString *s = @"abcabc";                 /* a0 b1 c2 a3 b4 c5 */
		NSCharacterSet *ac = [NSCharacterSet characterSetWithCharactersInString:@"ac"];
		NSCharacterSet *onlyA = [NSCharacterSet characterSetWithCharactersInString:@"a"];
		NSCharacterSet *none = [NSCharacterSet characterSetWithCharactersInString:@"z"];
		NSRange r;

		r = [s rangeOfCharacterFromSet:ac];
		check("set-search-no-options-door-survives",
		      r.location == 0 && r.length == 1,
		      "the oldest door answers the first member - the one it delegates to kept its behaviour");
	covers("NSString", "rangeOfCharacterFromSet:");

		r = [s rangeOfCharacterFromSet:ac options:NSBackwardsSearch];
		check("set-search-backwards-answers-the-last",
		      r.location == 5 && r.length == 1,
		      "NSBackwardsSearch is the other end of the same scan, not a second scan");
	covers("NSString", "rangeOfCharacterFromSet:options:");

		r = [s rangeOfCharacterFromSet:ac options:NSAnchoredSearch];
		check("set-search-anchored-forward",
		      r.location == 0 && r.length == 1,
		      "anchored forward matches at the range's FIRST character");
	covers("NSString", "rangeOfCharacterFromSet:options:");

		r = [s rangeOfCharacterFromSet:onlyA options:NSAnchoredSearch range:NSMakeRange(1, 5)];
		check("set-search-anchored-does-not-scan",
		      r.location == NSNotFound,
		      "an anchored search does not scan: the a at 3 is past the boundary, so there is no match");
	covers("NSString", "rangeOfCharacterFromSet:options:range:");

		r = [s rangeOfCharacterFromSet:ac options:NSAnchoredSearch | NSBackwardsSearch];
		check("set-search-anchored-backward-at-the-end",
		      r.location == 5 && r.length == 1,
		      "anchored backward matches at the range's LAST character");
	covers("NSString", "rangeOfCharacterFromSet:options:");

		r = [s rangeOfCharacterFromSet:onlyA options:NSAnchoredSearch | NSBackwardsSearch];
		check("set-search-anchored-backward-elsewhere-is-not-a-match",
		      r.location == NSNotFound,
		      "the last character is c, so an a anywhere else is not an anchored match");
	covers("NSString", "rangeOfCharacterFromSet:options:");

		r = [s rangeOfCharacterFromSet:onlyA options:0 range:NSMakeRange(1, 5)];
		check("set-search-range-limits-the-scan",
		      r.location == 3 && r.length == 1,
		      "a range starts the scan later: the first a in {1,5} is at 3");
	covers("NSString", "rangeOfCharacterFromSet:options:range:");

		r = [s rangeOfCharacterFromSet:none];
		check("set-search-not-found-answers-notfound",
		      r.location == NSNotFound && r.length == 0,
		      "nothing found is {NSNotFound, 0}");
	covers("NSString", "rangeOfCharacterFromSet:");

		/* APPLE'S OWN NO-NORMALIZATION EXAMPLE, as close as a probe can hold it: a decomposed "u" plus
		 * COMBINING DIAERESIS is not the precomposed "u with diaeresis", and the page says so. */
		{
			NSString *decomposed = @"stru\u0308del";
			NSCharacterSet *precomposed = [NSCharacterSet characterSetWithCharactersInString:@"\u00fc"];

			r = [decomposed rangeOfCharacterFromSet:precomposed];
			check("set-search-does-not-normalize",
			      r.location == NSNotFound,
			      "a canonically equivalent pair does not match - no normalization is performed");
	covers("NSString", "rangeOfCharacterFromSet:");
		}

		{
			int caughtInvalid = 0, caughtRange = 0;
			NSCharacterSet *noSet = nil;	/* a VARIABLE null: -Wnonnull only fires on a null constant */

			@try {
				[s rangeOfCharacterFromSet:noSet];
			} @catch (NSException *exception) {
				caughtInvalid = [exception.name isEqualToString:NSInvalidArgumentException];
			}
			check("set-search-nil-set-raises",
			      caughtInvalid,
			      "a nil set raises NSInvalidArgumentException, which Apple's page names");
	covers("NSString", "rangeOfCharacterFromSet:");

			@try {
				[s rangeOfCharacterFromSet:ac options:0 range:NSMakeRange(2, 99)];
			} @catch (NSException *exception) {
				caughtRange = [exception.name isEqualToString:NSRangeException];
			}
			check("set-search-invalid-range-raises",
			      caughtRange,
			      "a range past the end raises NSRangeException - a DIFFERENT exception from the nil set");
	covers("NSString", "rangeOfCharacterFromSet:options:range:");
		}

		{
			NSString *prefix;

			prefix = [@"abcdef" commonPrefixWithString:@"abcxyz" options:0];
			check("common-prefix-stops-where-they-part",
			      [prefix isEqualToString:@"abc"],
			      "the shared opening run and nothing more");
	covers("NSString", "commonPrefixWithString:options:");

			prefix = [@"ABCdef" commonPrefixWithString:@"abcdef" options:NSCaseInsensitiveSearch];
			check("common-prefix-is-the-receiver-characters",
			      [prefix isEqualToString:@"ABCdef"],
			      "the two differ ONLY in case, so the fold makes them equal throughout and the answer is "
			      "the RECEIVER's spelling - not the argument's lowercase one");
	covers("NSString", "commonPrefixWithString:options:");

			prefix = [@"abcdef" commonPrefixWithString:@"abcdef" options:0];
			check("common-prefix-of-equals-is-the-whole-string",
			      [prefix isEqualToString:@"abcdef"],
			      "an identical argument gives everything");
	covers("NSString", "commonPrefixWithString:options:");

			prefix = [@"abc" commonPrefixWithString:@"xyz" options:0];
			check("common-prefix-of-strangers-is-empty",
			      [prefix length] == 0,
			      "nothing in common is an empty prefix, not nil");
	covers("NSString", "commonPrefixWithString:options:");

			NSString *noString = nil;	/* a VARIABLE null, so no -Wnonnull constant */
			prefix = [@"abc" commonPrefixWithString:noString options:0];
			check("common-prefix-nil-argument-is-empty",
			      prefix != nil && [prefix length] == 0,
			      "a nil argument has nothing in common, and the door still answers a string");
	covers("NSString", "commonPrefixWithString:options:");
		}

		/* (A .strings check STOOD HERE AND IS WITHDRAWN WITH ITS DOOR. The measurement it produced is
		 * kept, because it is the whole reason the door is gone: this library's old-style plist reader
		 * answers NIL for a BRACE-LESS body — `"a" = "b";`, which is what a `.strings` file contains —
		 * so -propertyListFromStringsFileFormat could not honour its contract. The defect is in the
		 * READER and belongs to its own unit; the header says so, and the ledger keeps the row open.) */
	}

	{
		/* PERCENT-ENCODING, BOTH DIRECTIONS. The checks aim at the clauses a reader would doubt rather than
		 * at the arithmetic: the ASCII-ONLY allowed set, the UTF-8 byte expansion, and the three different
		 * ways a decode can be invalid. */
		NSCharacterSet *alnum = [NSCharacterSet characterSetWithCharactersInString:
		                         @"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"];
		NSCharacterSet *accents = [NSCharacterSet characterSetWithCharactersInString:@"\u00e9"];
		NSString *encoded, *decoded;

		encoded = [@"a b" stringByAddingPercentEncodingWithAllowedCharacters:alnum];
		check("percent-encodes-what-the-set-excludes",
		      [encoded isEqualToString:@"a%20b"],
		      "the space is not in the set, so it becomes %20");
	covers("NSString", "stringByAddingPercentEncodingWithAllowedCharacters:");

		encoded = [@"a-._~" stringByAddingPercentEncodingWithAllowedCharacters:
		           [NSCharacterSet characterSetWithCharactersInString:@"a-._~"]];
		check("percent-leaves-the-allowed-characters-alone",
		      [encoded isEqualToString:@"a-._~"],
		      "every character is in the set, so nothing is encoded");
	covers("NSString", "stringByAddingPercentEncodingWithAllowedCharacters:");

		encoded = [@"caf\u00e9" stringByAddingPercentEncodingWithAllowedCharacters:alnum];
		check("percent-encodes-by-utf8-bytes",
		      [encoded isEqualToString:@"caf%C3%A9"],
		      "one character became the TWO bytes UTF-8 gives it - the page's own rule");
	covers("NSString", "stringByAddingPercentEncodingWithAllowedCharacters:");

		/* THE SET HOLDS THE THREE ASCII LETTERS *AND* THE E-ACUTE, which is what makes this the rule's own
		 * test: only the ASCII members can be honoured, so `caf` passes through untouched while the e-acute
		 * — a member of the set — is encoded anyway. (The first version of this check allowed ONLY the
		 * e-acute, so `caf` was correctly encoded too and the check failed on its own expectation.) */
		encoded = [@"caf\u00e9" stringByAddingPercentEncodingWithAllowedCharacters:
		           [NSCharacterSet characterSetWithCharactersInString:@"acf\u00e9"]];
		check("percent-ignores-a-non-ascii-set-member",
		      [encoded isEqualToString:@"caf%C3%A9"],
		      "a set member outside 7-bit ASCII is IGNORED, so the e-acute is encoded anyway");
	covers("NSString", "stringByAddingPercentEncodingWithAllowedCharacters:");

		encoded = [@"abc" stringByAddingPercentEncodingWithAllowedCharacters:nil];
		check("percent-encoding-with-no-set-is-nil",
		      encoded == nil,
		      "a nil set is the one way this door cannot do its job, and it says so with nil");
	covers("NSString", "stringByAddingPercentEncodingWithAllowedCharacters:");

		decoded = [@"a%20b" stringByRemovingPercentEncoding];
		check("percent-decodes",
		      [decoded isEqualToString:@"a b"],
		      "%20 is a space again");
	covers("NSString", "stringByRemovingPercentEncoding");

		decoded = [@"caf%c3%a9" stringByRemovingPercentEncoding];
		check("percent-decodes-lowercase-hex",
		      [decoded isEqualToString:@"caf\u00e9"],
		      "hex digits are accepted in either case, and the two bytes become one character");
	covers("NSString", "stringByRemovingPercentEncoding");

		encoded = [@"a b/c?d" stringByAddingPercentEncodingWithAllowedCharacters:alnum];
		decoded = [encoded stringByRemovingPercentEncoding];
		check("percent-round-trips",
		      [decoded isEqualToString:@"a b/c?d"],
		      "what the encoder escaped, the decoder gives back unchanged");
	covers("NSString", "stringByAddingPercentEncodingWithAllowedCharacters:");
	covers("NSString", "stringByRemovingPercentEncoding");

		decoded = [@"abc%ZZ" stringByRemovingPercentEncoding];
		check("percent-decode-nil-on-a-bad-digit",
		      decoded == nil,
		      "a % followed by a non-hex character is an invalid sequence");
	covers("NSString", "stringByRemovingPercentEncoding");

		decoded = [@"abc%" stringByRemovingPercentEncoding];
		check("percent-decode-nil-on-a-truncated-tail",
		      decoded == nil,
		      "a % at the end has no two digits to read");
	covers("NSString", "stringByRemovingPercentEncoding");

		decoded = [@"%FF" stringByRemovingPercentEncoding];
		check("percent-decode-nil-on-non-utf8",
		      decoded == nil,
		      "0xFF is not UTF-8, so there are no matching characters to answer with");
	covers("NSString", "stringByRemovingPercentEncoding");

		decoded = [@"%C0%AF" stringByRemovingPercentEncoding];
		check("percent-decode-nil-on-an-overlong-form",
		      decoded == nil,
		      "C0 AF is a two-byte OVERLONG encoding of /, which no encoder produces");
	covers("NSString", "stringByRemovingPercentEncoding");

		decoded = [@"plain" stringByRemovingPercentEncoding];
		check("percent-decode-leaves-plain-text-alone",
		      [decoded isEqualToString:@"plain"],
		      "nothing to decode is not an error");
	covers("NSString", "stringByRemovingPercentEncoding");
	}

	{
		/* COMPOSED CHARACTER SEQUENCES. Two different things are pinned here: THIS door's own rule (a base
		 * letter plus the combining characters that follow), and the divergence from the engine's UAX#29
		 * clusters for -enumerateSubstringsInRange:options:, MEASURED rather than asserted in a comment. */
		NSString *base = @"e\u0301";			/* e + COMBINING ACUTE ACCENT: a base and one mark */
		NSString *astral = @"a\U0001D165";		/* a + a combining mark ABOVE U+FFFF */
		NSString *crlf = @"\r\n";				/* UAX#29: ONE cluster. This door's rule: two. */
		NSString *flag = @"\U0001F1FA\U0001F1F8";	/* a regional-indicator pair: UAX#29 one, here two */
		NSRange r;
		BOOL caughtIndex = NO;
		BOOL caughtIndexRange = NO;

		r = [@"abc" rangeOfComposedCharacterSequenceAtIndex:1];
		check("composed-sequence-of-a-plain-character",
		      NSEqualRanges(r, NSMakeRange(1, 1)),
		      "a character with no marks is a sequence of itself");

		r = [base rangeOfComposedCharacterSequenceAtIndex:1];
		check("composed-sequence-from-the-mark-walks-back-to-the-base",
		      NSEqualRanges(r, NSMakeRange(0, 2)),
		      "the index names the MARK, and the page's rule is the base letter AT OR BEFORE it");

		r = [base rangeOfComposedCharacterSequenceAtIndex:0];
		check("composed-sequence-from-the-base-covers-its-marks",
		      NSEqualRanges(r, NSMakeRange(0, 2)),
		      "from the base forward over the combining characters that follow");

		/* THE ASTRAL BOUNDARY, MEASURED RATHER THAN ASSUMED. U+1D165 IS a combining mark (general category
		 * Mc) and the sequence still ends after the base — because +nonBaseCharacterSet is a BMP set in this
		 * library. The door asks the set for the SCALAR, so it follows the set if the set ever grows; until
		 * then this check pins the measured answer, and a change on either side goes red instead of quiet. */
		r = [astral rangeOfComposedCharacterSequenceAtIndex:0];
		check("composed-sequence-stops-at-a-bmp-set-boundary",
		      NSEqualRanges(r, NSMakeRange(0, 1)),
		      "U+1D165 is a combining mark and yet does not extend the sequence: the M* set holds no astral members");

		r = [crlf rangeOfComposedCharacterSequenceAtIndex:0];
		check("composed-sequence-splits-crlf",
		      NSEqualRanges(r, NSMakeRange(0, 1)),
		      "CR is not a mark, so CR and LF are TWO sequences here - UAX#29 clusters them as one");

		r = [flag rangeOfComposedCharacterSequenceAtIndex:0];
		check("composed-sequence-splits-a-flag-pair",
		      NSEqualRanges(r, NSMakeRange(0, 2)),
		      "one character and no marks: the second regional indicator is its own sequence, not part of a cluster");

		r = [@"abc" rangeOfComposedCharacterSequenceAtIndex:3];
		check("composed-sequence-at-the-end-is-empty",
		      NSEqualRanges(r, NSMakeRange(3, 0)),
		      "an index equal to the length is the end of the string, not past it");

		r = [base rangeOfComposedCharacterSequencesForRange:NSMakeRange(1, 1)];
		check("composed-sequences-range-grows-back-to-the-base",
		      NSEqualRanges(r, NSMakeRange(0, 2)),
		      "a range that is only the mark grows back over the base it belongs to");

		r = [base rangeOfComposedCharacterSequencesForRange:NSMakeRange(0, 1)];
		check("composed-sequences-range-grows-forward-over-the-marks",
		      NSEqualRanges(r, NSMakeRange(0, 2)),
		      "a range that is only the base grows forward over its marks");

		r = [base rangeOfComposedCharacterSequencesForRange:NSMakeRange(1, 0)];
		check("composed-sequences-empty-range-is-the-sequence-at-its-location",
		      NSEqualRanges(r, NSMakeRange(0, 2)),
		      "an empty range overlaps nothing, so this answers the containing sequence, as the index door does");

		@try {
			(void)[@"abc" rangeOfComposedCharacterSequenceAtIndex:99];
		} @catch (NSException *exception) {
			caughtIndex = [exception.name isEqualToString:NSRangeException];
		}
		check("composed-sequence-index-past-the-end-raises",
		      caughtIndex,
		      "the page says the index must not exceed the bounds, and an exception is what exceeding them gets");

		@try {
			(void)[@"abc" rangeOfComposedCharacterSequencesForRange:NSMakeRange(1, 99)];
		} @catch (NSException *exception) {
			caughtIndexRange = [exception.name isEqualToString:NSRangeException];
		}
		check("composed-sequences-range-past-the-end-raises",
		      caughtIndexRange,
		      "the same bounds rule as the index door, on the range this time");
	}

	{
		/* UNICODE NORMALIZATION (§63.24): THE TWO AXES, MEASURED ON MEANING RATHER THAN ON LENGTHS ALONE.
		 * "café" is written twice below — `\u00e9` (ONE code point, composed) and `e`+`\u0301` (two,
		 * decomposed) — because those are the SAME TEXT in different units. A door that did nothing would pass a
		 * length test on either one and fail the equality between them, which is why the equality is here. */
		NSString *composed = @"caf\u00e9";
		NSString *decomposed = @"cafe\u0301";
		NSString *ligature = @"\ufb01";			/* the ﬁ LIGATURE (U+FB01) */
		NSString *nfc = [decomposed precomposedStringWithCanonicalMapping];
		NSString *nfd = [composed decomposedStringWithCanonicalMapping];
		NSString *nfkc = [ligature precomposedStringWithCompatibilityMapping];
		NSString *nfkd = [ligature decomposedStringWithCompatibilityMapping];

		check("normalization-canonical-composes-and-decomposes",
		      [nfc isEqualToString:composed] && [nfc length] == 4 &&
		      [nfd isEqualToString:decomposed] && [nfd length] == 5 &&
		      /* A FIXED POINT IS THE PROPERTY, not a coincidence: normalizing an already-normal string is the
		       * identity, and that is what puts the two forms in two equivalence classes. */
		      [[nfc precomposedStringWithCanonicalMapping] isEqualToString:nfc] &&
		      [[nfd decomposedStringWithCanonicalMapping] isEqualToString:nfd] &&
		      /* ASCII IS A FIXED POINT OF ALL FOUR — the cheapest invariant there is. */
		      [@"plain ASCII" isEqualToString:[@"plain ASCII" precomposedStringWithCanonicalMapping]] &&
		      [@"plain ASCII" isEqualToString:[@"plain ASCII" decomposedStringWithCanonicalMapping]],
		      "U+00E9 and e+U+0301 compose/decompose, a normalized string is a fixed point of its own form, and ASCII is untouched");
	covers("NSString", "precomposedStringWithCanonicalMapping");
	covers("NSString", "decomposedStringWithCanonicalMapping");

		check("normalization-compatibility-folds-what-canonical-keeps",
		      [nfkc isEqualToString:@"fi"] && [nfkd isEqualToString:@"fi"] && [nfkc length] == 2 &&
		      /* AND THE CANONICAL PAIR LEAVES THE SAME LIGATURE ALONE: this pair of assertions is what makes the
		       * answer about COMPATIBILITY rather than about normalization in general. */
		      [[ligature precomposedStringWithCanonicalMapping] isEqualToString:ligature] &&
		      [[ligature decomposedStringWithCanonicalMapping] isEqualToString:ligature],
		      "the fi ligature folds to \"fi\" under NFKC/NFKD while NFC/NFD keep it — the axis that separates the two pairs");
	covers("NSString", "precomposedStringWithCanonicalMapping");
	covers("NSString", "decomposedStringWithCanonicalMapping");
	}

	{
		/* THE TRANSFORM AND FOLDING DOORS. The strip transform is NFD + dropping the combining marks, so a
		 * "café" folds to "cafe"; the folding door folds case and/or diacritics and REFUSES an option it cannot
		 * fold. Numbers carry, and the diacritic cases are BMP marks (é = e + U+0301). */
		NSString *accented = [NSString stringWithUTF8String:"caf\xc3\xa9"];	/* café, precomposed */
		NSString *stripped = [accented stringByApplyingTransform:NSStringTransformStripCombiningMarks reverse:NO];
		NSString *strippedDiacritics = [accented stringByApplyingTransform:NSStringTransformStripDiacritics reverse:NO];
		NSString *unknownTransform = [@"x" stringByApplyingTransform:@"NoSuchTransformer" reverse:NO];
		NSString *caseFolded = [@"HELLO" stringByFoldingWithOptions:NSCaseInsensitiveSearch locale:nil];
		NSString *markFolded = [accented stringByFoldingWithOptions:NSDiacriticInsensitiveSearch locale:nil];
		NSString *bothFolded = [accented stringByFoldingWithOptions:(NSCaseInsensitiveSearch | NSDiacriticInsensitiveSearch) locale:nil];
		NSString *unchanged = [@"MiXeD" stringByFoldingWithOptions:0 locale:nil];
		BOOL widthRefused = NO;

		@try {
			(void)[@"x" stringByFoldingWithOptions:NSWidthInsensitiveSearch locale:nil];
		} @catch (NSException *exception) {
			widthRefused = [exception.name isEqualToString:NSInvalidArgumentException];
		}

		check("string-transform-and-folding",
		      /* both strip transforms turn "café" (4 units) into "cafe" (4 units) */
		      stripped != nil && [stripped isEqualToString:@"cafe"] && [stripped length] == 4 &&
		      strippedDiacritics != nil && [strippedDiacritics isEqualToString:@"cafe"] &&
		      /* a transform this library does not implement answers nil, not the input */
		      unknownTransform == nil &&
		      /* case folding is the ASCII rule: "HELLO" -> "hello" (5 units) */
		      caseFolded != nil && [caseFolded isEqualToString:@"hello"] && [caseFolded length] == 5 &&
		      /* diacritic folding drops the mark from "café" -> "cafe", with or without case folding */
		      markFolded != nil && [markFolded isEqualToString:@"cafe"] &&
		      bothFolded != nil && [bothFolded isEqualToString:@"cafe"] &&
		      /* no options folds nothing: the string comes back unchanged (5 units) */
		      unchanged != nil && [unchanged isEqualToString:@"MiXeD"] && [unchanged length] == 5 &&
		      /* an option the library cannot fold RAISES rather than silently doing nothing */
		      widthRefused,
		      "the strip transforms give \"cafe\" (4 units) from \"café\", an unimplemented transform is nil, folding \"HELLO\" gives \"hello\" (5) and folding \"café\" gives \"cafe\", no options leaves \"MiXeD\" (5) unchanged, and NSWidthInsensitiveSearch raises NSInvalidArgumentException");
	}

	{
		/* §63.240: -applyTransform:reverse:range:updatedRange: — THE IN-PLACE FORM of the transform above.
		 * The check asserts a SPLICE (a range in the MIDDLE of a longer string, so "the whole string" cannot
		 * pass for it), the range it reports, and its TWO REFUSALS: a transform this library cannot apply,
		 * and a range that is invalid — which -substringWithRange: does NOT detect, so this door must. */
		NSMutableString *inPlace = [[NSMutableString alloc] initWithUTF8String:"x caf\xc3\xa9 y"];
		NSRange got = NSMakeRange(NSNotFound, 0);
		BOOL unknownRaised = NO;
		BOOL rangeRaised = NO;
		char detail[200];

		[inPlace applyTransform:NSStringTransformStripDiacritics
				reverse:NO
				  range:NSMakeRange(2, 4)
			   updatedRange:&got];

		@try {
			[inPlace applyTransform:@"NoSuchTransformer" reverse:NO
					  range:NSMakeRange(0, 1) updatedRange:NULL];
		} @catch (NSException *exception) {
			unknownRaised = [exception.name isEqualToString:NSInvalidArgumentException];
		}
		@try {
			[inPlace applyTransform:NSStringTransformStripDiacritics reverse:NO
					  range:NSMakeRange(0, 99) updatedRange:NULL];
		} @catch (NSException *exception) {
			rangeRaised = [exception.name isEqualToString:NSRangeException];
		}

		snprintf(detail, sizeof detail, "got \"%s\" {%lu, %lu} unknownRaised=%d rangeRaised=%d",
			 [inPlace UTF8String], (unsigned long)got.location, (unsigned long)got.length,
			 (int)unknownRaised, (int)rangeRaised);
		check("string-apply-transform-in-place",
		      /* "x café y" (8 units) with {2, 4} = "café" stripped in place -> "x cafe y" (8 units) */
		      [inPlace isEqualToString:@"x cafe y"] && [inPlace length] == 8 &&
		      got.location == 2 && got.length == 4 &&
		      unknownRaised && rangeRaised,
		      detail);
	}

	{
		/* §63.240'S REAL FIND. The mutable splices computed their BYTE offsets from UTF-16 UNIT indices, which
		 * is invisible on an all-ASCII string and corrupts any string with a multibyte character at or before
		 * the range: the trailing BYTE of that character was left behind and decoded as its own character. THE
		 * ORACLE IS DELIBERATELY ASCII-ONLY, so the check cannot depend on the doors it is testing.
		 *
		 * "\u00e9ab" IS 3 UNITS AND 4 BYTES, which is what makes the two readings tell apart: unit 3 is the END,
		 * byte 3 is INSIDE "\u00e9"; unit {0,2} is "\u00e9a", byte {0,2} is "\u00e9". The string is built from
		 * SPLIT literals because a two-byte escape followed by a HEX DIGIT would be read as one longer escape —
		 * the same trap the encoding checks above already split around. */
		NSString *mb = [NSString stringWithUTF8String:"\xc3\xa9" "ab"];	/* \u00e9ab */
		NSMutableString *ins = [NSMutableString stringWithString:mb];
		NSMutableString *del = [NSMutableString stringWithString:mb];
		NSString *rep = [mb stringByReplacingCharactersInRange:NSMakeRange(1, 1) withString:@"Z"];

		[ins insertString:@"--" atIndex:3];			/* the END in units; byte 3 is inside "\u00e9" */
		[del deleteCharactersInRange:NSMakeRange(0, 2)];	/* "\u00e9a" in units; "\u00e9" in bytes */
		check("string-mutable-splice-is-unit-based",
		      /* correct: "\u00e9ab" + "--" is 5 units, deleting {0,2} leaves "b", and rep is 3 units ending "Zb" */
		      [ins length] == 5 && [ins hasSuffix:@"--"] &&
		      [del isEqualToString:@"b"] &&
		      [rep length] == 3 && [[rep substringFromIndex:1] isEqualToString:@"Zb"],
		      [[NSString stringWithFormat:@"ins=\"%@\" (%lu) del=\"%@\" rep=\"%@\" (%lu)",
			[ins description], (unsigned long)[ins length], [del description],
			[rep description], (unsigned long)[rep length]] UTF8String]);
	}

	{
		/* §63.241: THE RANGE IS HONOURED. -stringByReplacingOccurrencesOfString:withString:options:range:
		 * used to IGNORE its range and replace through the whole receiver — which the check above could not
		 * see, because the range it passed WAS the whole string. A range covering only the first "a" must
		 * leave the second alone, and one covering only the middle must change nothing at all. */
		NSString *whole = @"a-b-a";
		NSString *firstOnly = [whole stringByReplacingOccurrencesOfString:@"a"
								      withString:@"X"
									 options:NSLiteralSearch
									   range:NSMakeRange(0, 1)];
		NSString *middle = [whole stringByReplacingOccurrencesOfString:@"a"
							    withString:@"X"
							       options:NSLiteralSearch
								 range:NSMakeRange(1, 3)];
		/* AND ON A MULTIBYTE RECEIVER, where a byte-shaped range would land in the wrong place entirely:
		 * "\u00e9a\u00e9a" is 4 units and 6 bytes, so unit 3 is the LAST "a" and byte 3 is inside "\u00e9". */
		NSString *mb = [NSString stringWithUTF8String:"\xc3\xa9" "a" "\xc3\xa9" "a"];
		NSString *mbLast = [mb stringByReplacingOccurrencesOfString:@"a"
							       withString:@"Z"
								  options:NSLiteralSearch
								    range:NSMakeRange(3, 1)];

		check("string-range-is-honoured-by-replacement",
		      [firstOnly isEqualToString:@"X-b-a"] &&
		      [middle isEqualToString:@"a-b-a"] &&
		      /* the multibyte case: the last "a" became "Z" and EVERYTHING BEFORE IT came back untouched */
		      [mbLast length] == 4 &&
		      [[mbLast substringFromIndex:3] isEqualToString:@"Z"] &&
		      [[mbLast substringToIndex:2] isEqualToString:[mb substringToIndex:2]],
		      "a replacement confined to a range must leave the rest of the receiver alone");
	}

	{
		/* §63.242: THE MUTABLE TWIN. -replaceOccurrencesOfString:withString:options:range: ignored its range
		 * exactly as the immutable door §63.241 fixed did, so the SAME assertions apply — and the COUNT it
		 * answers must cover the range only, which is the half the immutable door does not have. */
		NSMutableString *scoped = [NSMutableString stringWithString:@"a-b-a"];
		NSUInteger inRange = [scoped replaceOccurrencesOfString:@"a"
							    withString:@"X"
							       options:NSLiteralSearch
								 range:NSMakeRange(0, 1)];
		NSMutableString *wholeRange = [NSMutableString stringWithString:@"a-b-a"];
		NSUInteger inWhole = [wholeRange replaceOccurrencesOfString:@"a"
								withString:@"X"
								   options:NSLiteralSearch
								     range:NSMakeRange(0, 5)];
		/* AND ON A MULTIBYTE RECEIVER: "\u00e9a\u00e9a" is 4 units and 6 bytes, so unit 3 is the LAST "a" and
		 * byte 3 is inside "\u00e9". */
		NSString *mbSource = [NSString stringWithUTF8String:"\xc3\xa9" "a" "\xc3\xa9" "a"];
		NSMutableString *mb = [NSMutableString stringWithString:mbSource];
		NSUInteger inMb = [mb replaceOccurrencesOfString:@"a"
						     withString:@"Z"
							options:NSLiteralSearch
							  range:NSMakeRange(3, 1)];

		check("string-mutable-replace-honours-its-range",
		      [scoped isEqualToString:@"X-b-a"] && inRange == 1 &&
		      [wholeRange isEqualToString:@"X-b-X"] && inWhole == 2 &&
		      inMb == 1 && [mb length] == 4 &&
		      [[mb substringFromIndex:3] isEqualToString:@"Z"] &&
		      [[mb substringToIndex:2] isEqualToString:[mbSource substringToIndex:2]],
		      [[NSString stringWithFormat:@"scoped=\"%@\" (%lu) whole=\"%@\" (%lu) mb=\"%@\" (%lu)",
			[scoped description], (unsigned long)inRange, [wholeRange description],
			(unsigned long)inWhole, [mb description], (unsigned long)inMb] UTF8String]);
	}

	{
		/* THE LOCALE-AWARE CASE DOORS (§63.25): ONE LIVE DOOR AND THREE DEPRECATED SPELLINGS, MEASURED WHERE THE
		 * LOCALE ACTUALLY CHANGES THE ANSWER. This library's case mapping is ASCII except for the one localised
		 * rule it ships (the Turkic i/İ and I/ı pairing, which the NSLocale checks above already prove), so the
		 * interesting case is exactly the one where the two doors must DIFFER. */
		NSLocale *turkish = [[NSLocale alloc] initWithLocaleIdentifier:@"tr_TR"];
		NSLocale *posix = [[NSLocale alloc] initWithLocaleIdentifier:@"en_US_POSIX"];

		check("case-capitalization-is-locale-aware",
		      /* THE TURKISH ANSWER, and the plain door's answer beside it: "istanbul" capitalizes with a DOTTED
		       * İ here, while `-capitalizedString` gives "Istanbul" — the same string, two languages. */
		      [[@"istanbul" capitalizedStringWithLocale:turkish] isEqualToString:@"İstanbul"] &&
		      [[@"istanbul" capitalizedString] isEqualToString:@"Istanbul"] &&
		      /* AND THE DOTLESS PAIR INSIDE A WORD: "ISIK" keeps its word-initial "I" and folds the second one to
		       * "ı", which is the Turkic lowercase of "I". */
		      [[@"ISIK" capitalizedStringWithLocale:turkish] isEqualToString:@"Isık"] &&
		      /* THE CONTROL FOR THE FIRST LINE: a NON-Turkic locale must agree with the plain door exactly, which
		       * is what makes the Turkish answer about the LOCALE rather than about the word rule. */
		      [[@"istanbul" capitalizedStringWithLocale:posix] isEqualToString:[@"istanbul" capitalizedString]] &&
		      /* AND THE WORD RULE SURVIVES THE LOCALE: the separator set is the plain door's own. */
		      [[@"two-words here" capitalizedStringWithLocale:turkish] isEqualToString:@"Two-Words Here"] &&
		      /* THE DEPRECATED SPELLINGS DELEGATE TO THE CURRENT LOCALE — one rule, not two. */
		      [[@"istanbul" localizedCapitalizedString]
			isEqualToString:[@"istanbul" capitalizedStringWithLocale:[NSLocale currentLocale]]] &&
		      [[@"ISTANBUL" localizedLowercaseString]
			isEqualToString:[@"ISTANBUL" lowercaseStringWithLocale:[NSLocale currentLocale]]] &&
		      [[@"istanbul" localizedUppercaseString]
			isEqualToString:[@"istanbul" uppercaseStringWithLocale:[NSLocale currentLocale]]],
		      "the Turkish locale capitalizes istanbul as İstanbul where the plain door gives Istanbul, the dotless pair folds inside a word, a non-Turkic locale matches the plain door, and the three deprecated spellings delegate to the current locale");
	covers("NSString", "capitalizedStringWithLocale:");
	covers("NSString", "localizedCapitalizedString");
	covers("NSString", "localizedLowercaseString");
	covers("NSString", "localizedUppercaseString");
	covers("NSString", "uppercaseStringWithLocale:");
	covers("NSString", "lowercaseStringWithLocale:");
	}

	{
		/* THE LOCALISED SEARCH DOORS (§63.26): CASE AND DIACRITICS, AND WHERE THE TWO DOORS DIFFER. */
		NSRange found = [@"café au lait" localizedStandardRangeOfString:@"CAFE"];
		NSRange missing = [@"plain" localizedStandardRangeOfString:@"xyz"];

		check("localized-search-folds-case-and-diacritics",
		      /* ⚠ THE ANSWER IS IN THE RECEIVER'S UNITS, WHICH IS THE WHOLE POINT OF THE MAP: the match is the
		       * first FOUR units of "café au lait" — "café" WITH its accent — not four units of some folded string
		       * the caller never held, which is what a folded-index answer would have said. */
		      found.location == 0 && found.length == 4 &&
		      [[@"café au lait" substringWithRange:found] isEqualToString:@"café"] &&
		      /* AND THE FOLD IS SYMMETRIC: an ACCENTED needle finds an unaccented haystack too. */
		      [@"cafe au lait" localizedStandardRangeOfString:@"café"].length == 4 &&
		      /* NO MATCH IS NSNotFound, and the contains door answers in its own vocabulary. */
		      missing.location == NSNotFound &&
		      [@"café" localizedStandardContainsString:@"CAFE"] &&
		      ![@"plain" localizedStandardContainsString:@"xyz"] &&
		      /* THE PAIR THAT MAKES THE TWO DOORS DIFFERENT RATHER THAN REDUNDANT: the case-insensitive door
		       * folds case ONLY, so an accent must still match an accent there — and A NON-ASCII CASE PAIR IS NOT
		       * A FOLD THIS LIBRARY MAKES, which the first draft of this check got wrong: the case mapping here is
		       * ASCII-only (the header says so, and the locale adds only the Turkic pair), so "CAFÉ" does NOT
		       * case-fold to "café" and an assertion that it does is a claim about Unicode, not about this door. */
		      [@"CAFE" localizedCaseInsensitiveContainsString:@"cafe"] &&
		      [@"cafe" localizedCaseInsensitiveContainsString:@"CAFE"] &&
		      ![@"cafe" localizedCaseInsensitiveContainsString:@"café"] &&
		      [@"café" localizedStandardContainsString:@"cafe"] &&
		      /* AND A NEEDLE THAT IS NOT THERE IS STILL NOT THERE, whatever the fold. */
		      ![@"café" localizedCaseInsensitiveContainsString:@"tea"],
		      "the standard doors fold case and diacritics (the match lands on the receiver's own \"café\"), the case-insensitive door folds case only, and a miss stays NSNotFound/NO");
	covers("NSString", "localizedStandardRangeOfString:");
	covers("NSString", "localizedStandardContainsString:");
	covers("NSString", "localizedCaseInsensitiveContainsString:");
	}

	{
		/* THE VALIDATED FORMATS (§63.27): A FORMAT CHECKED AGAINST WHAT THE CALLER ALLOWS, AND A REFUSAL THAT
		 * SAYS WHAT WENT WRONG IN APPLE'S OWN VOCABULARY. */
		NSError *error = nil;
		NSError *refusedError = nil;
		NSError *escapeError = nil;
		NSString *ok = [NSString stringWithValidatedFormat:@"%d and %@" validFormatSpecifiers:@"%@ %d"
							     error:&error, 42, @"text"];
		NSString *refused = [NSString stringWithValidatedFormat:@"%d" validFormatSpecifiers:@"%@"
								  error:&refusedError, 42];
		NSString *escaped = [NSString stringWithValidatedFormat:@"100%% of %d" validFormatSpecifiers:@"%d"
								  error:&escapeError, 5];

		check("validated-format-allows-only-the-listed-specifiers",
		      /* THE HAPPY PATH: both specifiers listed, both rendered, and NO error reported. */
		      ok != nil && [ok isEqualToString:@"42 and text"] && error == nil &&
		      /* THE REFUSAL: nil, an error, the Cocoa domain and the formatting code — which is the whole point
		       * of the door existing rather than the caller scanning the format itself. */
		      refused == nil && refusedError != nil &&
		      [[refusedError domain] isEqualToString:NSCocoaErrorDomain] &&
		      [refusedError code] == NSFormattingError &&
		      /* ⚠ AND `%%` IS NOT A DIRECTIVE, which is the assertion that measures the PARSER rather than the
		       * happy path: a format whose only other percent is the literal escape must be accepted for the
		       * specifier set "%d". */
		      escaped != nil && [escaped isEqualToString:@"100% of 5"] && escapeError == nil &&
		      /* AND THE LOCALIZED SPELLING IS THE SAME RULE, including its NULL error pointer. */
		      [NSString localizedStringWithValidatedFormat:@"%@" validFormatSpecifiers:@"%@" error:NULL,
			   @"x"] != nil,
		      "a listed format renders with no error, an unlisted specifier is refused with NSCocoaErrorDomain/NSFormattingError, %% is not a directive, and the localized spelling shares the rule");
	}

	{
		/* THE INSTANCE VALIDATED-FORMAT DOORS (§63.27's siblings): the SAME rule the class doors apply, reached
		 * through -init. The assertion MEASURES the shared rule rather than re-testing it: the same format that
		 * renders through the class door renders here, and the same unlisted specifier is refused with the SAME
		 * error vocabulary. The locale form ACCEPTS a locale and IGNORES it, so it renders the locale-free
		 * answer. Both variadic doors are exercised; they forward to their `arguments:` twin, which is the one
		 * place the validation lives. */
		NSError *initErr = nil;
		NSError *initBadErr = nil;
		NSString *ok = [[NSString alloc] initWithValidatedFormat:@"%d and %@"
						       validFormatSpecifiers:@"%@ %d"
								   error:&initErr, 42, @"text"];
		NSString *bad = [[NSString alloc] initWithValidatedFormat:@"%d"
							validFormatSpecifiers:@"%@"
								    error:&initBadErr, 42];
		NSString *loc = [[NSString alloc] initWithValidatedFormat:@"%d-%@"
							validFormatSpecifiers:@"%d %@"
								    locale:nil
								     error:NULL, 7, @"x"];

		check("validated-format-instance-doors",
		      /* the happy path: both listed, both rendered, 11 UTF-16 units, no error reported */
		      ok != nil && [ok isEqualToString:@"42 and text"] && [ok length] == 11 && initErr == nil &&
		      /* the refusal is the class rule's: nil, error, Cocoa domain, the formatting code */
		      bad == nil && initBadErr != nil &&
		      [[initBadErr domain] isEqualToString:NSCocoaErrorDomain] &&
		      [initBadErr code] == NSFormattingError &&
		      /* the locale form renders the locale-free answer: "7-x", 3 units */
		      loc != nil && [loc isEqualToString:@"7-x"] && [loc length] == 3,
		      "the -init validated-format doors share the + rule: \"%d and %@\" with (42, \"text\") renders 11 units with no error, an unlisted \"%d\" is refused NSCocoaErrorDomain/NSFormattingError, and the locale form renders \"7-x\" (3 units)");
	}

	{
		/* CREATION FROM A C STRING WITH AN ENCODING (§63.28): THE MIRROR OF `-cStringUsingEncoding:`, SO THE
		 * ASSERTION THAT MATTERS IS WHERE THE TWO REFUSALS LAND — a byte string is not reinterpreted, and a
		 * label is not believed. */
		NSString *utf8 = [NSString stringWithCString:"plain ASCII" encoding:NSUTF8StringEncoding];
		NSString *ascii = [NSString stringWithCString:"plain ASCII" encoding:NSASCIIStringEncoding];
		NSString *utf8BytesAsASCII = [NSString stringWithCString:"caf\xc3\xa9" encoding:NSASCIIStringEncoding];
		/*	§63.71: LATIN-1 CONVERTS NOW, so this is the POSITIVE case in that encoding — and the
		 * refusals it used to stand for are still asserted, one line down, by name. */
		NSString *latin1 = [NSString stringWithCString:"caf\xe9" encoding:NSISOLatin1StringEncoding];
		NSString *unstored = [NSString stringWithCString:"x" encoding:NSNEXTSTEPStringEncoding];
		const char *noBytes = NULL;	/* a VARIABLE null, so no -Wnonnull constant */
		NSString *nul = [NSString stringWithCString:noBytes encoding:NSUTF8StringEncoding];

		check("cstring-with-encoding-refuses-what-it-cannot-store",
		      utf8 != nil && [utf8 isEqualToString:@"plain ASCII"] &&
		      ascii != nil && [ascii isEqualToString:@"plain ASCII"] &&
		      /* ⚠ THE HIGH BYTE IS NOT ASCII WHATEVER THE LABEL SAYS, and this is the assertion that separates a
		       * real refusal from a label-trusting one: the same bytes are legal UTF-8, so a door that trusted the
		       * argument would answer a string here. */
		      utf8BytesAsASCII == nil &&
		      /* AND AN ENCODING THIS LIBRARY DOES NOT STORE IS REFUSED RATHER THAN REINTERPRETED AS UTF-8. */
		      latin1 != nil && [latin1 isEqualToString:@"caf\xC3\xA9"] &&
		      unstored == nil &&
		      nul == nil &&
		      /* AND THE POSITIVE CASE IN THE ENCODING IT WAS WRITTEN IN COMES BACK INTACT, accents and all. */
		      [[NSString stringWithCString:"caf\xc3\xa9" encoding:NSUTF8StringEncoding]
			isEqualToString:@"café"],
		      "UTF-8 and ASCII round-trip, a high byte labelled ASCII is refused, an unstored encoding is refused, NULL is nil, and real UTF-8 answers the string it names");
	covers("NSString", "stringWithCString:encoding:");
	}

	{
		/* THE INSTANCE C-STRING / BYTE DOORS (§63.30's C-string family): the -init mirrors of the class doors
		 * above, so the SAME two refusals are asserted through the other door and neither can pass on one side
		 * alone. Every length is the UTF-16 UNIT count and carries its number. */
		NSString *ascii = [[NSString alloc] initWithCString:"plain ASCII" encoding:NSASCIIStringEncoding];
		NSString *utf8 = [[NSString alloc] initWithCString:"caf\xc3\xa9" encoding:NSUTF8StringEncoding];
		NSString *highAsAscii = [[NSString alloc] initWithCString:"caf\xc3\xa9" encoding:NSASCIIStringEncoding];
		NSString *latin1 = [[NSString alloc] initWithCString:"caf\xe9" encoding:NSISOLatin1StringEncoding];
		NSString *unstored = [[NSString alloc] initWithCString:"x" encoding:NSNEXTSTEPStringEncoding];
		NSString *deprecated = [[NSString alloc] initWithCString:"plain ASCII"];
		NSString *deprecatedLen = [[NSString alloc] initWithCString:"abcdef" length:3];
		NSString *bytesLen = [[NSString alloc] initWithBytes:"xyz\0abc" length:3 encoding:NSUTF8StringEncoding];
		NSString *bytesUtf8 = [[NSString alloc] initWithBytes:"caf\xc3\xa9" length:5 encoding:NSUTF8StringEncoding];
		NSString *bytesAsAscii = [[NSString alloc] initWithBytes:"caf\xc3\xa9" length:5 encoding:NSASCIIStringEncoding];
		/*	§63.71: THIS USED UTF-16, WHICH NOW CONVERTS. `unstored` must stay a REAL refusal, so it asks
		 * for NeXTSTEP — the encoding no converter here claims. */
		NSString *bytesUnstored = [[NSString alloc] initWithBytes:"abc" length:3 encoding:NSNEXTSTEPStringEncoding];

		check("cstring-init-doors",
		      /* the -init mirrors of the class doors: the same positive answers, 11 and 4 UTF-16 units */
		      ascii != nil && [ascii isEqualToString:@"plain ASCII"] && [ascii length] == 11 &&
		      utf8 != nil && [utf8 isEqualToString:@"café"] && [utf8 length] == 4 &&
		      /* and the SAME two refusals: a high byte under the ASCII label, and an unstored encoding */
		      highAsAscii == nil && unstored == nil &&
		      /* the deprecated no-encoding name uses UTF-8, so it round-trips the same bytes */
		      deprecated != nil && [deprecated isEqualToString:@"plain ASCII"] &&
		      /* the length-taking deprecated name reads EXACTLY 3 bytes and needs no terminating NUL */
		      deprecatedLen != nil && [deprecatedLen isEqualToString:@"abc"] && [deprecatedLen length] == 3 &&
		      /* -initWithBytes:length:encoding: honours length (the buffer holds "xyz\0abc") and reads UTF-8 */
		      bytesLen != nil && [bytesLen isEqualToString:@"xyz"] && [bytesLen length] == 3 &&
		      bytesUtf8 != nil && [bytesUtf8 isEqualToString:@"café"] &&
		      /* and the byte door refuses an unstored encoding and a high byte under ASCII, like its C-string twin */
		      bytesAsAscii == nil && bytesUnstored == nil,
		      "the -init C-string/byte doors mirror the class doors: UTF-8 and ASCII round-trip (4 and 11 units), the deprecated no-encoding name uses UTF-8, a length of 3 reads \"abc\" and \"xyz\" (3 units) without a NUL, and a high byte labelled ASCII and an unstored encoding are both refused");
	covers("NSString", "initWithCString:encoding:");
	covers("NSString", "initWithCString:");
	covers("NSString", "initWithCString:length:");
	covers("NSString", "initWithBytes:length:encoding:");
	}

	{
		/* THE URL FORMS (§63.29): A FILE URL IS THE PATH DOOR, AND A SCHEME WITH NOTHING BEHIND IT IS REFUSED.
		 * The file arm is measured as a ROUND TRIP through the two doors — written by the path door, read by
		 * the URL door — so the assertion is about the URL arm rather than about the filesystem. */
		/* ⚠ AND THE FILE ARM IS ASSERTED ON WHICHEVER BRANCH THE ENVIRONMENT ACTUALLY PROVIDES, because
		 * `NSTemporaryDirectory()` in this tree answers the FSH's path (`/System/Temporary Files`), WHICH THE
		 * GUEST HAS AND A HOST BUILD DOES NOT — measured: the write fails there, the read answers nil, and a
		 * check that demanded the round trip was red on the host for a reason that is not the door's. SO BOTH
		 * BRANCHES ASSERT THE DOOR AND NEITHER PASSES VACUOUSLY: where the bytes can be written the round trip
		 * must hold, and where they cannot the door must answer nil WITH AN ERROR rather than an empty string.
		 * The detail says which branch ran. */
		NSString *path = [NSTemporaryDirectory() stringByAppendingString:@"/fnstring-url-door"];
		NSString *marker = @"url door: café ✓";
		/* ⚠ THE CASTS ARE THE HOUSE IDIOM FOR "this literal is known to parse": the doors' parameters are
		 * nonnull as Apple declares them, and +URLWithString:/+fileURLWithPath: answer nullable. */
		NSURL *fileURL = (NSURL * _Nonnull)[NSURL fileURLWithPath:path];
		NSURL *nothingBehind = (NSURL * _Nonnull)[NSURL URLWithString:@"fnx-no-transport://nothing"];
		NSError *fileError = nil;
		NSError *transportError = nil;
		NSString *back;
		NSString *refused;
		BOOL wrote;

		wrote = [marker writeToFile:path atomically:YES encoding:NSUTF8StringEncoding error:NULL];
		back = [NSString stringWithContentsOfURL:fileURL encoding:NSUTF8StringEncoding error:&fileError];
		refused = [NSString stringWithContentsOfURL:nothingBehind
						  encoding:NSUTF8StringEncoding
						     error:&transportError];

		check("url-doors-round-trip-a-file-url-and-refuse-an-unreachable-scheme",
		      /* THE FILE ARM, ON WHICHEVER BRANCH THE ENVIRONMENT GAVE US: a round trip where the bytes could be
		       * written (the guest, whose FSH directory exists), and a REFUSAL — nil plus an error — where they
		       * could not (a host build, where NSTemporaryDirectory() names a path that host does not have). */
		      (wrote ? (back != nil && [back isEqualToString:marker] && fileError == nil)
			     : (back == nil && fileError != nil)) &&
		      /* AND THE TRANSPORT ARM REFUSES: nil with the loader's own error, rather than a silent empty
		       * string, which is what a door that swallowed its failure would answer. */
		      refused == nil && transportError != nil,
		      /* ⚠ AND THE DETAIL CARRIES ITS NUMBERS, which the first version of this check did NOT — and that
		       * omission is why a host-only failure took a diagnostic program to explain rather than being
		       * visible in the probe's own output. This probe's opening comment asks every detail to carry its
		       * numbers; this one now does, INCLUDING WHICH BRANCH RAN. `-UTF8String` is the bridge because
		       * `check()` takes the C string this probe's other details are written as — and the buffer is the
		       * autoreleased string's, valid for the call. */
		      [[NSString stringWithFormat:@"wrote=%d path=%@ back=%@ fileError=%@ refused=%@ transportError=%@",
			(int)wrote, path,
			back != nil ? back : @"(nil)",
			fileError != nil ? [fileError description] : @"(none)",
			refused != nil ? refused : @"(nil)",
			transportError != nil ? [transportError description] : @"(none)"] UTF8String]);
	covers("NSString", "writeToFile:atomically:encoding:error:");
	covers("NSString", "stringWithContentsOfURL:encoding:error:");
	}

	{
		/* THE C-STRING AND CHARACTER-COPY DOORS (§63.30). The default C-string encoding here is UTF-8 —
		 * the storage itself — so -cString, -lossyCString and -cStringLength answer the storage's own
		 * bytes, and the NUL-terminated copy door is asserted byte for byte INCLUDING the terminator.
		 * "café" is chosen because it is 4 units but 5 bytes, so a byte/unit confusion cannot pass. */
		NSString *s = [NSString stringWithUTF8String:"caf\xc3\xa9"];
		char buf[16];
		BOOL copied;

		memset(buf, 'Z', sizeof(buf));
		copied = [s getCString:buf maxLength:sizeof(buf) encoding:NSUTF8StringEncoding];

		check("cstring-doors-copy-byte-for-byte",
		      strcmp([s UTF8String], "caf\xc3\xa9") == 0 &&
		      strcmp([s cString], "caf\xc3\xa9") == 0 &&
		      strcmp([s lossyCString], "caf\xc3\xa9") == 0 &&
		      [s cStringLength] == 5 &&
		      [s lengthOfBytesUsingEncoding:NSUTF8StringEncoding] == 5 &&
		      [s length] == 4 &&
		      copied == YES && strcmp(buf, "caf\xc3\xa9") == 0 &&
		      (unsigned char)buf[5] == 0,	/* the terminator was written, not left as the fill */
		      [[NSString stringWithFormat:@"cString=%s lossy=%s cStringLength=%lu bytes=%lu copied=%d buf=%s",
			[s cString], [s lossyCString], (unsigned long)[s cStringLength],
			(unsigned long)[s lengthOfBytesUsingEncoding:NSUTF8StringEncoding], (int)copied, buf] UTF8String]);
	covers("NSString", "cString");
	covers("NSString", "lossyCString");
	covers("NSString", "cStringLength");
	covers("NSString", "getCString:maxLength:encoding:");
	}

	{
		/* THE THREE REFUSALS OF THE COPY-OUT DOOR, each for its own reason: the string plus its NUL does
		 * not fit (a 5-byte buffer for a 5-byte string + NUL), the encoding is one this library does not
		 * store, and the bytes are not ASCII whatever the ASCII label says. */
		NSString *s = [NSString stringWithUTF8String:"caf\xc3\xa9"];
		char small[5];		/* one short of the 5 bytes + NUL */
		char room[8];
		BOOL fits = [s getCString:small maxLength:sizeof(small) encoding:NSUTF8StringEncoding];
		/*	§63.72: THIS USED UTF-16, WHICH NOW CONVERTS. `unstored` must stay a REAL refusal, so it asks
		 * for NeXTSTEP — the encoding no converter here claims. */
		BOOL unstored = [s getCString:room maxLength:sizeof(room) encoding:NSNEXTSTEPStringEncoding];
		BOOL labelledAscii = [s getCString:room maxLength:sizeof(room) encoding:NSASCIIStringEncoding];

		check("cstring-doors-refuse-what-cannot-fit",
		      fits == NO && unstored == NO && labelledAscii == NO,
		      [[NSString stringWithFormat:@"fits=%d (maxLength=5, need 6) unstored=%d labelledAscii=%d",
			(int)fits, (int)unstored, (int)labelledAscii] UTF8String]);
	}

	{
		/* THE TWO DEPRECATED INCOMING DOORS. +stringWithCString: takes a NUL-terminated C string; the
		 * length-taking form must read EXACTLY its length and NOT run on to a NUL, so the buffer's tail is
		 * poisoned with bytes that would lengthen the answer if the door overran. */
		static const char notTerminated[] = { 'a', 'b', 'c', '!', '!', '!' };
		NSString *nulTerminated = [NSString stringWithCString:"caf\xc3\xa9"];
		NSString *bounded = [NSString stringWithCString:notTerminated length:3];
		NSString *emptyLen = [NSString stringWithCString:notTerminated length:0];
		/* ⚠ THE NULL IS PASSED THROUGH A VARIABLE, not as a literal: -Wnonnull fires on a null LITERAL at a
		 * nonnull parameter, and this unit adds no new diagnostic (the §63.28 check's own NULL literal
		 * predates it and is left exactly as it shipped). */
		const char *missing = NULL;
		NSString *nul = [NSString stringWithCString:missing];

		check("cstring-without-encoding-round-trips",
		      nulTerminated != nil && [nulTerminated isEqualToString:@"caf\xc3\xa9"] &&
		      bounded != nil && [bounded isEqualToString:@"abc"] &&
		      emptyLen != nil && [emptyLen length] == 0 &&
		      nul == nil,
		      [[NSString stringWithFormat:@"nulTerminated=%@ bounded=%@ emptyLen=%lu nul=%@",
			nulTerminated, bounded, (unsigned long)[emptyLen length],
			nul != nil ? nul : @"(nil)"] UTF8String]);
	}

	{
		/* -getCharacters: (the deprecated no-range form) copies EVERY unit and adds no terminator — it is
		 * the range form over the whole extent, so every unit must equal -characterAtIndex:. */
		NSString *s = [NSString stringWithUTF8String:"caf\xc3\xa9"];
		unichar units[8];
		int allEqual = 1;
		NSUInteger i;

		memset(units, 0, sizeof(units));
		[s getCharacters:units];
		for (i = 0; i < [s length]; i++) {
			if (units[i] != [s characterAtIndex:i]) {
				allEqual = 0;
			}
		}
		check("getcharacters-copies-every-unit",
		      [s length] == 4 && allEqual &&
		      units[0] == 'c' && units[1] == 'a' && units[2] == 'f' && units[3] == 0xE9,
		      [[NSString stringWithFormat:@"length=%lu allEqual=%d units=%04x/%04x/%04x/%04x",
			(unsigned long)[s length], allEqual,
			(unsigned)units[0], (unsigned)units[1], (unsigned)units[2], (unsigned)units[3]] UTF8String]);
	covers("NSString", "getCharacters:");
	}

	{
		/* ENCODING INTROSPECTION over the storage. -canBeConvertedToEncoding: is YES exactly when the
		 * storage IS the encoding (UTF-8) or every byte is 7-bit (ASCII); -maximumLengthOfBytesUsingEncoding:
		 * is the UTF-8 byte count for a possible conversion and 0 for an impossible one. */
		NSString *ascii = [NSString stringWithUTF8String:"plain"];
		NSString *accents = [NSString stringWithUTF8String:"caf\xc3\xa9"];

		check("can-be-converted-follows-the-storage",
		      [ascii canBeConvertedToEncoding:NSUTF8StringEncoding] &&
		      [ascii canBeConvertedToEncoding:NSASCIIStringEncoding] &&
		      [accents canBeConvertedToEncoding:NSUTF8StringEncoding] &&
		      ![accents canBeConvertedToEncoding:NSASCIIStringEncoding] &&
		      /*	§63.71: IT CAN NOW, and that is the point of the engine — UTF-16 is a converter this
		       * library has, so the ASCII boundary is the ONLY one left on this string. */
		      [accents canBeConvertedToEncoding:NSUTF16StringEncoding] &&
		      [accents canBeConvertedToEncoding:NSISOLatin1StringEncoding],
		      [[NSString stringWithFormat:@"ascii -> utf8=%d ascii=%d ; accents -> utf8=%d ascii=%d utf16=%d",
			(int)[ascii canBeConvertedToEncoding:NSUTF8StringEncoding],
			(int)[ascii canBeConvertedToEncoding:NSASCIIStringEncoding],
			(int)[accents canBeConvertedToEncoding:NSUTF8StringEncoding],
			(int)[accents canBeConvertedToEncoding:NSASCIIStringEncoding],
			(int)[accents canBeConvertedToEncoding:NSUTF16StringEncoding]] UTF8String]);
	covers("NSString", "canBeConvertedToEncoding:");
	}

	{
		NSString *ascii = [NSString stringWithUTF8String:"plain"];
		NSString *accents = [NSString stringWithUTF8String:"caf\xc3\xa9"];

		check("maximum-length-follows-the-storage",
		      [ascii maximumLengthOfBytesUsingEncoding:NSUTF8StringEncoding] == 5 &&
		      [ascii maximumLengthOfBytesUsingEncoding:NSASCIIStringEncoding] == 5 &&
		      [accents maximumLengthOfBytesUsingEncoding:NSUTF8StringEncoding] == 5 &&
		      [accents maximumLengthOfBytesUsingEncoding:NSASCIIStringEncoding] == 0 &&
		      /*	§63.71: and the maximum is now the CONVERSION'S OWN length, asserted against the
		       * byte count rather than a number this file would have to guess. */
		      [accents maximumLengthOfBytesUsingEncoding:NSUTF16StringEncoding] ==
			[accents lengthOfBytesUsingEncoding:NSUTF16StringEncoding] &&
		      [accents maximumLengthOfBytesUsingEncoding:NSUTF16StringEncoding] > 0,
		      [[NSString stringWithFormat:@"ascii -> utf8=%lu ascii=%lu ; accents -> utf8=%lu ascii=%lu utf16=%lu",
			(unsigned long)[ascii maximumLengthOfBytesUsingEncoding:NSUTF8StringEncoding],
			(unsigned long)[ascii maximumLengthOfBytesUsingEncoding:NSASCIIStringEncoding],
			(unsigned long)[accents maximumLengthOfBytesUsingEncoding:NSUTF8StringEncoding],
			(unsigned long)[accents maximumLengthOfBytesUsingEncoding:NSASCIIStringEncoding],
			(unsigned long)[accents maximumLengthOfBytesUsingEncoding:NSUTF16StringEncoding]] UTF8String]);
	covers("NSString", "maximumLengthOfBytesUsingEncoding:");
	}

	/* --- §63.46: THE PARAGRAPH DOORS, AND THE ENGINE ALIGNED TO THEM -------------------------------- */
	{
		NSString *text = @"a\n\nb";
		NSUInteger start = 99, end = 99, contentsEnd = 99;

		/* THE FIRST PARAGRAPH OWNS ITS TERMINATOR: `end` is past the break, `contentsEnd` is where the TEXT
		 * stops. The three out-parameters are the whole reason the door has three. */
		[text getParagraphStart:&start end:&end contentsEnd:&contentsEnd forRange:NSMakeRange(0, 0)];
		check("paragraph-door-three-out-parameters",
		      start == 0 && end == 2 && contentsEnd == 1 &&
		      NSEqualRanges([text paragraphRangeForRange:NSMakeRange(0, 0)], NSMakeRange(0, 2)),
		      [[NSString stringWithFormat:@"\"a\\n\\nb\" paragraph 0 is 0..2 with its text ending at 1 "
						@"(got %lu/%lu/%lu) and -paragraphRangeForRange: agrees",
						(unsigned long)start, (unsigned long)end,
						(unsigned long)contentsEnd] UTF8String]);
	covers("NSString", "getParagraphStart:end:contentsEnd:forRange:");
	covers("NSString", "paragraphRangeForRange:");

		/* ⚠ A BLANK LINE IS AN EMPTY PARAGRAPH — the observable half of the decision. The engine used to
		 * SKIP a blank line as a separator and merge the non-blank lines around it, so this string had TWO
		 * paragraphs; Apple's three-character rule makes it THREE, and the middle one has no text at all. */
		[text getParagraphStart:&start end:&end contentsEnd:&contentsEnd forRange:NSMakeRange(2, 0)];
		check("paragraph-blank-line-is-an-empty-paragraph",
		      start == 2 && end == 3 && contentsEnd == 2 &&
		      NSEqualRanges([text paragraphRangeForRange:NSMakeRange(3, 0)], NSMakeRange(3, 1)),
		      [[NSString stringWithFormat:@"the blank line is its own EMPTY paragraph (start %lu, end %lu, "
						@"contentsEnd %lu) and the text after it is the third",
						(unsigned long)start, (unsigned long)end,
						(unsigned long)contentsEnd] UTF8String]);
	covers("NSString", "getParagraphStart:end:contentsEnd:forRange:");
	covers("NSString", "paragraphRangeForRange:");

		/* AND THE PARAGRAPH'S TERMINATOR SET IS NARROWER THAN A LINE'S: NEL (U+0085) and LINE SEPARATOR
		 * (U+2028) end a LINE and NOT a paragraph, so this string is ONE paragraph and TWO lines. That
		 * difference is the reason the engine has two walks where it used to have one. */
		{
			/* ⚠ BUILT FROM CODE UNITS RATHER THAN WRITTEN AS ESCAPES, AND THAT IS A LANGUAGE RULE RATHER THAN A
			 * STYLE: clang REFUSES `\u0085` with "universal character name refers to a control character", so a
			 * string literal cannot spell NEL at all. The same escape for LS/PS is legal, but one constructor for
			 * both is the one rule. */
			unichar nelUnits[3] = { 'a', 0x0085, 'b' };
			unichar psUnits[3] = { 'a', 0x2029, 'b' };
			NSString *nel = [NSString stringWithCharacters:nelUnits length:3];
			NSString *ps = [NSString stringWithCharacters:psUnits length:3];
			NSRange paragraph = [nel paragraphRangeForRange:NSMakeRange(0, 0)];
			NSRange line = [nel lineRangeForRange:NSMakeRange(0, 0)];
			NSRange psParagraph = [ps paragraphRangeForRange:NSMakeRange(0, 0)];

			check("nel-and-ls-end-a-line-but-not-a-paragraph",
			      nel.length == 3 &&
			      NSEqualRanges(paragraph, NSMakeRange(0, 3)) &&
			      NSEqualRanges(line, NSMakeRange(0, 2)) &&
			      NSEqualRanges(psParagraph, NSMakeRange(0, 2)),
			      [[NSString stringWithFormat:@"NEL is one paragraph (0..%lu) but two lines (0..%lu), while "
						@"PARAGRAPH SEPARATOR ends a paragraph (0..%lu)",
						(unsigned long)paragraph.length, (unsigned long)line.length,
						(unsigned long)psParagraph.length] UTF8String]);
		}

		/* THE DOOR AND THE ENUMERATION ARE ONE RULE, which is Apple's own claim about them ("Equivalent to
		 * paragraphRangeForRange:") and the reason the ENGINE was changed rather than only the door. */
		{
			__block NSUInteger seen = 0;
			__block BOOL same = YES;

			[text enumerateSubstringsInRange:NSMakeRange(0, [text length])
						 options:NSStringEnumerationByParagraphs
					      usingBlock:^(NSString *substring, NSRange substringRange,
							   NSRange enclosingRange, BOOL *stop) {
				(void)substring;
				(void)enclosingRange;
				(void)stop;
				if (!NSEqualRanges(substringRange,
						   [text paragraphRangeForRange:NSMakeRange(substringRange.location, 0)])) {
					same = NO;
				}
				seen++;
			}];
			check("paragraphs-via-enumeration-match-the-door",
			      same && seen == 3,
			      [[NSString stringWithFormat:@"-enumerateSubstringsInRange:options:ByParagraphs answers "
						@"%lu ranges and every one is its own -paragraphRangeForRange: (%@)",
						(unsigned long)seen, same ? @"yes" : @"NO"] UTF8String]);
		}
	}

	/* --- §63.47: THE BORROWED-BUFFER FAMILY ---------------------------------------------------------- */
	{
		/* ⚠ THE OWNERSHIP CLAUSE IS THE POINT, AND IT IS TESTED BY FREEING THE CALLER'S BUFFER OUT FROM
		 * UNDER THE STRING: if the receiver had BORROWED the storage rather than copying it, the reads below
		 * would be reads of freed memory. `freeWhenDone:YES` means the buffer is disposed of exactly once —
		 * by the receiver — and the string it built must survive that. */
		char *bytes = (char *)malloc(6);
		id owned = nil;

		memcpy(bytes, "hello", 5);
		owned = [[NSString alloc] initWithBytesNoCopy:bytes length:5 encoding:NSUTF8StringEncoding
						 freeWhenDone:YES];
		check("borrowed-bytes-are-copied-so-freeing-the-buffer-is-safe",
		      owned != nil && [owned isEqualToString:@"hello"] && [owned length] == 5,
		      [[NSString stringWithFormat:@"a freeWhenDone:YES string still reads as \"%@\" after the "
						@"caller's buffer was disposed of — which is what proves the copy",
						owned] UTF8String]);

		/* THE BLOCK SPELLING RUNS THE CALLER'S BLOCK INSTEAD OF FREEING, and the two arguments it receives
		 * are the buffer and its LENGTH — observable, so it is asserted rather than assumed. */
		{
			unichar *units = (unichar *)malloc(sizeof(unichar) * 3);
			__block NSUInteger ran = 0;
			__block NSUInteger ranLength = 0;
			id blockOwned = nil;

			units[0] = 'a';
			units[1] = 'b';
			units[2] = 'c';
			blockOwned = [[NSString alloc] initWithCharactersNoCopy:units
									length:3
								   deallocator:^(unichar *buffer, NSUInteger length) {
				ran++;
				ranLength = length;
				free(buffer);
			}];
			check("no-copy-deallocator-runs-once-with-the-length",
			      blockOwned != nil && [blockOwned isEqualToString:@"abc"] &&
			      ran == 1 && ranLength == 3,
			      [[NSString stringWithFormat:@"the caller's block ran %lu time(s) with length %lu, and the "
						@"string it built reads as \"%@\"",
						(unsigned long)ran, (unsigned long)ranLength, blockOwned] UTF8String]);
	covers("NSString", "initWithCharactersNoCopy:length:deallocator:");
		}

		/* `-getBytes:…`: A NULL BUFFER IS THE SIZE FORM; a CONVERTED encoding is converted with ITS OWN
		 * length; and an encoding with NO CONVERTER is REFUSED with the whole range reported unconverted.
		 * ⚠ §63.72: THE SIZE FORM AND THE WRITE FORM ARE ASSERTED AGAINST THE BYTE DOOR for the converted
		 * case, because the bug this check found was exactly a length taken from the WRONG ENCODING. */
		{
			NSString *text = @"abc";
			NSUInteger needed = 0;
			NSRange leftover = NSMakeRange(0, 0);
			char buffer[8];
			NSUInteger used = 0;
			NSRange range = NSMakeRange(0, 3);
			BOOL refused;

			BOOL asked = [text getBytes:NULL maxLength:0 usedLength:&needed encoding:NSUTF8StringEncoding
					      options:0 range:range remainingRange:&leftover];
			BOOL wrote = [text getBytes:buffer maxLength:sizeof(buffer) usedLength:&used
					   encoding:NSUTF8StringEncoding options:0 range:range
				     remainingRange:&leftover];
			/* §63.72: THIS ASKED FOR UTF-16, WHICH NOW CONVERTS. `refused` must stay a REAL refusal, so
			 * it asks for NeXTSTEP — the encoding no converter here claims. */
			refused = ![text getBytes:buffer maxLength:sizeof(buffer) usedLength:&used
					 encoding:NSNEXTSTEPStringEncoding options:0 range:range
				       remainingRange:&leftover];
			/* AND THE POSITIVE CASE IN A CONVERTED ENCODING, with the buffer and the count the BYTE DOOR
			 * produces — so this check fails if the door ever takes a length from another encoding again. */
			{
				char wide[32];
				NSUInteger wideUsed = 0;
				/* ⚠ ITS OWN leftover, AND THAT IS NOT TIDINESS: this block runs BETWEEN the refusal above
				 * and the check below, so reusing `leftover` would clobber the range the check reads — which
				 * is exactly how this check failed once, reporting "the whole range (0) left unconverted"
				 * about a refusal that had correctly reported 3. A probe must not share state across the
				 * values it asserts. */
				NSRange wideLeftover = NSMakeRange(99, 99);
				NSData *wideData = [text dataUsingEncoding:NSUTF16StringEncoding];
				BOOL wideOk = [text getBytes:wide maxLength:sizeof(wide) usedLength:&wideUsed
						   encoding:NSUTF16StringEncoding options:0 range:range
					     remainingRange:&wideLeftover];

				asked = asked && wideOk && wideData != nil &&
					wideUsed == [wideData length] && wideLeftover.length == 0 &&
					memcmp(wide, [wideData bytes], wideUsed) == 0;
			}
			check("getbytes-size-form-and-refusal",
			      asked && needed == 3 && wrote && used == 3 &&
			      memcmp(buffer, "abc", 3) == 0 && refused && leftover.length == 3,
			      [[NSString stringWithFormat:@"the NULL-buffer form answers 3 bytes needed, the write "
						@"form writes 3, and an unstored encoding is REFUSED with the whole range (%lu) "
						@"left unconverted",
						(unsigned long)leftover.length] UTF8String]);
		}

		/* THE DEPRECATED RANGE FORM converts the RANGE and reports what it could not convert. */
		{
			NSString *text = @"abcdef";
			char buffer[8];
			NSRange leftover = NSMakeRange(99, 99);
			BOOL fits;

			[text getCString:buffer maxLength:sizeof(buffer) range:NSMakeRange(1, 3)
			   remainingRange:&leftover];
			fits = (strcmp(buffer, "bcd") == 0);
			check("getcstring-range-form-converts-the-range",
			      fits && leftover.location == 4 && leftover.length == 0,
			      [[NSString stringWithFormat:@"the range form converts 1..4 to \"%s\" and reports the "
						@"leftover at %lu with length %lu",
						buffer, (unsigned long)leftover.location,
						(unsigned long)leftover.length] UTF8String]);
		}
	}

	/* --- §63.48: THE TWO FILESYSTEM DOORS, AND THE FINDER COMPARISON --------------------------------- */
	{
		NSString *tmp = @"/System/Temporary Files";
		NSString *target = [tmp stringByAppendingPathComponent:@"link-63-48-target.txt"];
		NSString *link = [tmp stringByAppendingPathComponent:@"link-63-48.txt"];
		NSFileManager *fm = [NSFileManager defaultManager];

		[fm createFileAtPath:target contents:[NSData dataWithBytes:"x" length:1] attributes:nil];

		/* ⚠ A REAL SYMLINK, MADE AND RESOLVED — the door is a FILESYSTEM LOOKUP now, so the check that
		 * proves it is one is a check that would answer DIFFERENTLY under the lexical rule this class used to
		 * have. `removeItemAtPath:` first, so a leftover from an earlier run cannot turn the second pass into
		 * a no-op that passes. */
		[fm removeItemAtPath:link error:NULL];
		if (symlink([target fileSystemRepresentation], [link fileSystemRepresentation]) == 0) {
			NSString *resolved = [[link stringByResolvingSymlinksInPath] lastPathComponent];

			check("symlink-resolution-follows-a-real-link",
			      [resolved isEqualToString:@"link-63-48-target.txt"],
			      [[NSString stringWithFormat:@"a symlink resolves to its REFERENT (got %@) — the lexical "
						@"rule would have answered link-63-48.txt", resolved] UTF8String]);
	covers("NSString", "stringByResolvingSymlinksInPath");
		} else {
			check("symlink-resolution-follows-a-real-link", 0,
			      "could not create the symlink this check needs");
	covers("NSString", "stringByResolvingSymlinksInPath");
		}

		/* AND THE UNRESOLVABLE PATH IS THE DOCUMENTED FAILURE: UNCHANGED, which is the lexical answer. */
		{
			NSString *absent = [tmp stringByAppendingPathComponent:@"no-such-63-48/deeper"];
			NSString *answer = [absent stringByResolvingSymlinksInPath];

			check("unresolvable-path-resolves-to-itself",
			      [answer isEqualToString:absent],
			      [[NSString stringWithFormat:@"a path that does not exist comes back UNMODIFIED (%@)",
						answer] UTF8String]);
		}

		/* THE FINDER ORDERING IS CASE-INSENSITIVE AND NUMERIC, which is the half Apple's own note names:
		 * "abc2" before "abc100" is that note's example, and BYTE order would put "abc100" first. */
		/* ⚠ WHAT THIS DOOR CAN HONOUR HERE IS THE CASE HALF; THE NUMERIC HALF IS A RECORDED DEFECT RATHER
		 * THAN A CLAIM. `-localizedStandardCompare:` is spelled over `NSCaseInsensitiveSearch |
		 * NSNumericSearch` — Apple's own note names both — but **`NSNumericSearch` IS A DECLARED OPTION THAT
		 * NOTHING HONOURS**: `-compare:options:` is a byte walk and never looks at it. So the check asserts
		 * the case half AND ASSERTS THE NUMERIC HALF AS THE BYTE-ORDER ANSWER IT ACTUALLY GETS, which is what
		 * makes the gap visible in the probe instead of assumed away. §63.48 records it with its own unit. */
		check("localized-standard-compare-folds-case-and-reports-its-numeric-gap",
		      [@"ABC" localizedStandardCompare:@"abc"] == NSOrderedSame &&
		      [@"b" localizedStandardCompare:@"a"] == NSOrderedDescending &&
		      [@"abc2" localizedStandardCompare:@"abc100"] == NSOrderedDescending,
		      "case does not decide - and \"abc2\" still compares AFTER \"abc100\", which is the "
		      "NSNumericSearch gap SS63.48 records rather than a claim about Apple's ordering");
	covers("NSString", "localizedStandardCompare:");

		/* THE COMPLETION DOOR: a UNIQUE prefix is NAMED, every match is listed, and filterTypes narrows by
		 * extension. The fixtures are made here so the check does not depend on the image's own contents. */
		{
			NSString *unique = [tmp stringByAppendingPathComponent:@"cmp-63-48-uniq.plist"];
			NSString *other1 = [tmp stringByAppendingPathComponent:@"cmp-63-48-a.md"];
			NSString *other2 = [tmp stringByAppendingPathComponent:@"cmp-63-48-b.md"];
			NSString *partial = [tmp stringByAppendingPathComponent:@"cmp-63-48-uniq"];
			NSString *prefix = [tmp stringByAppendingPathComponent:@"cmp-63-48-"];
			NSString *completed = nil;
			NSArray *matches = nil;
			NSArray *filtered = nil;
			NSUInteger uniqueCount;
			NSUInteger filteredCount;

			[fm createFileAtPath:unique contents:[NSData dataWithBytes:"u" length:1] attributes:nil];
			[fm createFileAtPath:other1 contents:[NSData dataWithBytes:"a" length:1] attributes:nil];
			[fm createFileAtPath:other2 contents:[NSData dataWithBytes:"b" length:1] attributes:nil];

			uniqueCount = [partial completePathIntoString:&completed caseSensitive:YES
					     matchesIntoArray:&matches filterTypes:nil];
			filteredCount = [prefix completePathIntoString:NULL caseSensitive:YES
					       matchesIntoArray:&filtered
						    filterTypes:[NSArray arrayWithObject:@"md"]];

			check("completepathintostring-completes-and-filters",
			      uniqueCount == 1 && [completed isEqualToString:unique] && [matches count] == 1 &&
			      filteredCount == 2 && [filtered count] == 2,
			      [[NSString stringWithFormat:@"a unique prefix is NAMED (%@) and listed once, and the "
						@"shared prefix with a filter answers the %lu .md matches",
						[completed lastPathComponent],
						(unsigned long)filteredCount] UTF8String]);
	covers("NSString", "completePathIntoString:caseSensitive:matchesIntoArray:filterTypes:");
		}
	}

	/* --- §63.49: THE DEPRECATED LINGUISTIC PAIR ---------------------------------------------------- */
	{
		NSString *sentence = @"Tom runs. He is fast.";
		NSArray *tags;
		NSArray *tokenRanges = nil;
		NSMutableArray *blockTags = [[NSMutableArray alloc] init];
		NSMutableArray *blockRanges = [[NSMutableArray alloc] init];
		NSRange whole = NSMakeRange(0, [sentence length]);

		/* ⚠ THE TWO DOORS ARE ONE WALK, WHICH IS THE ONLY THING THEY PROMISE EACH OTHER: the array form
		 * collects where the block form calls, so the tags and the token ranges must come out IDENTICAL. */
		tags = [sentence linguisticTagsInRange:whole
						scheme:NSLinguisticTagSchemeTokenType
					       options:0
					   orthography:nil
					  tokenRanges:&tokenRanges];
		[sentence enumerateLinguisticTagsInRange:whole
						  scheme:NSLinguisticTagSchemeTokenType
						 options:0
					     orthography:nil
					      usingBlock:^(NSLinguisticTag tag, NSRange tokenRange,
							   NSRange sentenceRange, BOOL *stop) {
			(void)sentenceRange;
			(void)stop;
			if (tag != nil) {
				[blockTags addObject:tag];
			}
			[blockRanges addObject:[NSValue valueWithRange:tokenRange]];
		}];
		check("linguistic-tags-pair-agree",
		      [tags count] > 0 && tokenRanges != nil &&
		      [tags isEqualToArray:blockTags] && [tokenRanges isEqualToArray:blockRanges],
		      [[NSString stringWithFormat:@"the array door and the block door answer the SAME %lu tags and "
						@"the same %lu token ranges",
						(unsigned long)[tags count],
						(unsigned long)[tokenRanges count]] UTF8String]);
	covers("NSString", "linguisticTagsInRange:scheme:options:orthography:tokenRanges:");
	covers("NSString", "enumerateLinguisticTagsInRange:scheme:options:orthography:usingBlock:");

		/* AND THE `sentenceRange` THE BLOCK CARRIES IS THE TAGGER'S OWN DOOR, not a second notion of a
		 * sentence: the check re-asks the tagger and compares. */
		{
			NSLinguisticTagger *tagger = [[NSLinguisticTagger alloc]
				initWithTagSchemes:[NSArray arrayWithObject:NSLinguisticTagSchemeTokenType]
					   options:0];
			NSRange firstToken = [[tokenRanges objectAtIndex:0] rangeValue];
			__block NSRange seen = NSMakeRange(NSNotFound, 0);

			[tagger setString:sentence];
			[sentence enumerateLinguisticTagsInRange:whole
							  scheme:NSLinguisticTagSchemeTokenType
							 options:0
						     orthography:nil
						      usingBlock:^(NSLinguisticTag tag, NSRange tokenRange,
								   NSRange sentenceRange, BOOL *stop) {
				(void)tag;
				(void)stop;
				if (tokenRange.location == firstToken.location) {
					seen = sentenceRange;
				}
			}];
			check("linguistic-sentence-range-is-the-taggers-own",
			      seen.location != NSNotFound &&
			      NSEqualRanges(seen, [tagger sentenceRangeForRange:firstToken]),
			      [[NSString stringWithFormat:@"the block's sentenceRange for the first token (%lu,%lu) is "
						@"what the tagger's own -sentenceRangeForRange: answers",
						(unsigned long)seen.location,
						(unsigned long)seen.length] UTF8String]);
		}
	}


	{
		/* §63.189: THE SIX URL COMPONENT SETS, asserted at their BOUNDARIES — a set that is merely "some URL
		 * characters" would pass a spot-check and still let a delimiter through. Each assertion names the
		 * production it comes from: query keeps "?" and "/" but not "#"; path keeps "/" and "@" but neither
		 * "?" nor "#"; host keeps ":" and the IP-literal brackets but no "/"; userinfo keeps ":" but not "@".
		 * A CLASS PROPERTY IS READ BY SENDING THE CLASS THE MESSAGE — there is no bare-identifier form. */
		char detail[256];
		BOOL allow = [[NSCharacterSet URLQueryAllowedCharacterSet] characterIsMember:(unichar)'?'] &&
			     [[NSCharacterSet URLQueryAllowedCharacterSet] characterIsMember:(unichar)'/'] &&
			     [[NSCharacterSet URLQueryAllowedCharacterSet] characterIsMember:(unichar)':'] &&
			     [[NSCharacterSet URLQueryAllowedCharacterSet] characterIsMember:(unichar)'@'] &&
			     [[NSCharacterSet URLPathAllowedCharacterSet] characterIsMember:(unichar)'/'] &&
			     [[NSCharacterSet URLPathAllowedCharacterSet] characterIsMember:(unichar)'@'] &&
			     ![[NSCharacterSet URLPathAllowedCharacterSet] characterIsMember:(unichar)'?'] &&
			     [[NSCharacterSet URLHostAllowedCharacterSet] characterIsMember:(unichar)':'] &&
			     [[NSCharacterSet URLHostAllowedCharacterSet] characterIsMember:(unichar)'['] &&
			     ![[NSCharacterSet URLHostAllowedCharacterSet] characterIsMember:(unichar)'/'] &&
			     [[NSCharacterSet URLUserAllowedCharacterSet] characterIsMember:(unichar)':'] &&
			     ![[NSCharacterSet URLUserAllowedCharacterSet] characterIsMember:(unichar)'@'] &&
			     [[NSCharacterSet URLUserAllowedCharacterSet] characterIsMember:(unichar)'~'] &&
			     [[NSCharacterSet URLUserAllowedCharacterSet] characterIsMember:(unichar)'!'];

		snprintf(detail, sizeof detail, "query(? / : @)=%d%d%d%d path(/ @ !?)=%d%d%d host(: [ !/)=%d%d%d user(: !@ ~ !)=%d%d%d%d",
			 [[NSCharacterSet URLQueryAllowedCharacterSet] characterIsMember:(unichar)'?'],
			 [[NSCharacterSet URLQueryAllowedCharacterSet] characterIsMember:(unichar)'/'],
			 [[NSCharacterSet URLQueryAllowedCharacterSet] characterIsMember:(unichar)':'],
			 [[NSCharacterSet URLQueryAllowedCharacterSet] characterIsMember:(unichar)'@'],
			 [[NSCharacterSet URLPathAllowedCharacterSet] characterIsMember:(unichar)'/'],
			 [[NSCharacterSet URLPathAllowedCharacterSet] characterIsMember:(unichar)'@'],
			 [[NSCharacterSet URLPathAllowedCharacterSet] characterIsMember:(unichar)'?'],
			 [[NSCharacterSet URLHostAllowedCharacterSet] characterIsMember:(unichar)':'],
			 [[NSCharacterSet URLHostAllowedCharacterSet] characterIsMember:(unichar)'['],
			 [[NSCharacterSet URLHostAllowedCharacterSet] characterIsMember:(unichar)'/'],
			 [[NSCharacterSet URLUserAllowedCharacterSet] characterIsMember:(unichar)':'],
			 [[NSCharacterSet URLUserAllowedCharacterSet] characterIsMember:(unichar)'@'],
			 [[NSCharacterSet URLUserAllowedCharacterSet] characterIsMember:(unichar)'~'],
			 [[NSCharacterSet URLUserAllowedCharacterSet] characterIsMember:(unichar)'!']);
		check("url-sets-allow-what-their-component-allows", allow, detail);
	}
	{
		/* THE NEGATIVE HALF, WHICH IS THE HALF THAT MATTERS: "#" is the fragment delimiter and is in NO set,
		 * "%" is produced by escaping rather than being "allowed", and a space is in none of them. */
		NSCharacterSet *sets[6];
		BOOL clean = YES;
		int i;
		char detail[128];

		sets[0] = [NSCharacterSet URLUserAllowedCharacterSet];
		sets[1] = [NSCharacterSet URLPasswordAllowedCharacterSet];
		sets[2] = [NSCharacterSet URLHostAllowedCharacterSet];
		sets[3] = [NSCharacterSet URLPathAllowedCharacterSet];
		sets[4] = [NSCharacterSet URLQueryAllowedCharacterSet];
		sets[5] = [NSCharacterSet URLFragmentAllowedCharacterSet];
		for (i = 0; i < 6; i++) {
			if ([sets[i] characterIsMember:(unichar)'#'] ||
			    [sets[i] characterIsMember:(unichar)'%'] ||
			    [sets[i] characterIsMember:(unichar)' ']) {
				clean = NO;
			}
		}
		snprintf(detail, sizeof detail, "no #,%%,space in any of the six = %d", clean);
		check("url-sets-exclude-the-delimiters-they-must", clean, detail);
	}
	{
		/* THE TWO DERIVED EQUALITIES ARE ASSERTED, because they are a READING written in the header rather
		 * than an accident of two string literals that happen to match. */
		check("url-sets-derived-pairs-are-equal",
		      [NSCharacterSet URLUserAllowedCharacterSet] == [NSCharacterSet URLPasswordAllowedCharacterSet] &&
		      [NSCharacterSet URLQueryAllowedCharacterSet] == [NSCharacterSet URLFragmentAllowedCharacterSet],
		      "password aliases userinfo and fragment aliases query");
	}
	{
		/* AND THE OBSERVABLE THAT MAKES THE SETS WORTH HAVING: the query set through the escaping door
		 * encodes exactly the octets it must — the space and the "#", and nothing else. */
		NSString *enc = [@"a b/c?d#e" stringByAddingPercentEncodingWithAllowedCharacters:
					[NSCharacterSet URLQueryAllowedCharacterSet]];
		char detail[256];

		snprintf(detail, sizeof detail, "encoded=[%s]", enc != nil ? [enc UTF8String] : "(nil)");
		check("url-query-set-encodes-only-what-must-be", [enc isEqualToString:@"a%20b/c?d%23e"], detail);
	}

	{
		/* THE CONTENTS DOORS, NEW ASSERTIONS RATHER THAN A CLAIM ON AN EXISTING CHECK. The fixture follows
		 * this file's URL check, AND SO DOES ITS RULE: NSTemporaryDirectory() answers the FSH's path, which
		 * the guest has and a host build does not, so BOTH branches assert the door and neither passes
		 * vacuously - where the bytes can be written every reader must answer them, and where they cannot
		 * every reader must answer nil. */
		NSString *body = @"caf\xc3\xa9 line\n";	/* non-ASCII, so an ASCII read must refuse */
		NSString *path = [NSTemporaryDirectory() stringByAppendingPathComponent:@"fn_string_contents.txt"];
		NSError *werr = nil, *eerr = nil;
		BOOL wrote = [body writeToFile:path atomically:YES encoding:NSUTF8StringEncoding error:&werr];
		NSString *byClass = [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:NULL];
		NSString *byInit = [[NSString alloc] initWithContentsOfFile:path encoding:NSUTF8StringEncoding error:NULL];
		NSStringEncoding used = 0;
		NSString *byUsed = [[NSString alloc] initWithContentsOfFile:path usedEncoding:&used error:NULL];
		NSString *legacy = [NSString stringWithContentsOfFile:path];
		NSString *asAscii = [NSString stringWithContentsOfFile:path encoding:NSASCIIStringEncoding error:&eerr];

		check("contents-doors-round-trip-or-refuse-with-an-error",
		      wrote
			? ([byClass isEqualToString:body] && [byInit isEqualToString:body] &&
			   [byUsed isEqualToString:body] && used == NSUTF8StringEncoding &&
			   [legacy isEqualToString:body] &&
			   /* AND A HIGH BYTE UNDER THE ASCII LABEL IS A REFUSAL THAT CARRIES ITS ERROR, which is
			    * the half a nil alone cannot prove. */
			   asAscii == nil && eerr != nil && [eerr.domain isEqualToString:@"NSCocoaErrorDomain"])
			: (byClass == nil && byInit == nil && byUsed == nil && legacy == nil),
		      [[NSString stringWithFormat:@"wrote=%d class=%@ init=%@ used=%@ legacy=%@ usedEnc=%lu asAscii=%@ err=%@",
			(int)wrote, byClass, byInit, byUsed, legacy, (unsigned long)used, asAscii,
			eerr != nil ? [eerr description] : @"(none)"] UTF8String]);
		covers("NSString", "stringWithContentsOfFile:");
		covers("NSString", "initWithContentsOfFile:encoding:error:");
		covers("NSString", "initWithContentsOfFile:usedEncoding:error:");
	}

	{
		/* THE SAME FIXTURE THROUGH THE URL DOORS, and the two refusals that hold in EVERY environment:
		 * a path with no file behind it, and a scheme with nothing behind it. Both are asserted OUTSIDE
		 * the branch, because neither depends on the temporary directory existing. */
		NSString *body = @"caf\xc3\xa9 line\n";
		NSString *path = [NSTemporaryDirectory() stringByAppendingPathComponent:@"fn_string_contents.txt"];
		NSError *werr = nil, *merr = nil, *uerr = nil;
		BOOL wrote = [body writeToFile:path atomically:YES encoding:NSUTF8StringEncoding error:&werr];
		NSURL *fileURL = [NSURL fileURLWithPath:path];
		NSURL *deadScheme = [NSURL URLWithString:@"fnxnoscheme://example.invalid/x"];
		NSString *byURL = [NSString stringWithContentsOfURL:fileURL encoding:NSUTF8StringEncoding error:NULL];
		NSStringEncoding usedURL = 0;
		NSString *initURL = [[NSString alloc] initWithContentsOfURL:fileURL usedEncoding:&usedURL error:NULL];
		NSString *legacyURL = [NSString stringWithContentsOfURL:fileURL];
		NSString *missing = [NSString stringWithContentsOfFile:@"/nonexistent/fn_string_missing"
							      encoding:NSUTF8StringEncoding error:&merr];
		NSString *unreachable = [NSString stringWithContentsOfURL:deadScheme
								 encoding:NSUTF8StringEncoding error:&uerr];

		check("url-contents-doors-mirror-the-path-doors",
		      wrote
			? ([byURL isEqualToString:body] && [initURL isEqualToString:body] &&
			   usedURL == NSUTF8StringEncoding && [legacyURL isEqualToString:body])
			: (byURL == nil && initURL == nil && legacyURL == nil),
		      [[NSString stringWithFormat:@"wrote=%d url=%@ init=%@ used=%@ legacy=%@ usedEnc=%lu",
			(int)wrote, byURL, initURL, legacyURL, (unsigned long)usedURL] UTF8String]);
		covers("NSString", "initWithContentsOfURL:encoding:error:");
		covers("NSString", "initWithContentsOfURL:usedEncoding:error:");
		covers("NSString", "stringWithContentsOfURL:");

		check("a-missing-file-and-a-dead-scheme-are-refused-with-an-error",
		      missing == nil && merr != nil && unreachable == nil && uerr != nil,
		      [[NSString stringWithFormat:@"missing=%@ missingErr=%@ unreachable=%@ schemeErr=%@",
			missing, merr != nil ? [merr description] : @"(none)", unreachable,
			uerr != nil ? [uerr description] : @"(none)"] UTF8String]);
	}

	{
		/* THE VALIDATED-FORMAT INSTANCE FAMILY, NEW ASSERTIONS. One rule, four doors: the specifier list
		 * is a WHITELIST, so a format that uses a specifier it did not list is refused WITH an error, and
		 * a listed one renders exactly as the unvalidated door would. The locale door accepts a locale and
		 * ignores it (the stance -initWithFormat:locale:arguments: records), which is asserted by asking
		 * the SAME question in two locales and requiring the same answer. */
		NSError *okErr = nil, *badErr = nil, *listErr = nil, *locErr = nil, *listLocErr = nil, *classErr = nil;
		NSString *good = [[NSString alloc] initWithValidatedFormat:@"%d and %@"
					 validFormatSpecifiers:@"%d %@" error:&okErr, 42, @"text"];
		NSString *bad = [[NSString alloc] initWithValidatedFormat:@"%d and %@"
					validFormatSpecifiers:@"%d" error:&badErr, 42, @"text"];
		NSString *listed = fn_validated_list([[NSString alloc] init], @"%d-%@", @"%d %@", &listErr, 7, @"x");
		NSString *localeArg = [[NSString alloc] initWithValidatedFormat:@"%d-%@"
					  validFormatSpecifiers:@"%d %@" locale:[NSLocale currentLocale]
					  error:&locErr, 7, @"x"];
		NSString *localeList = fn_validated_list_locale([[NSString alloc] init], @"%d-%@", @"%d %@",
								[NSLocale currentLocale], &listLocErr, 7, @"x");
		NSString *localized = [NSString localizedStringWithValidatedFormat:@"%d-%@"
					   validFormatSpecifiers:@"%d %@" error:&classErr, 7, @"x"];

		check("validated-format-instance-doors-refuse-an-unlisted-specifier",
		      good != nil && [good isEqualToString:@"42 and text"] && good.length == 11 && okErr == nil &&
		      bad == nil && badErr != nil && [badErr.domain isEqualToString:@"NSCocoaErrorDomain"] &&
		      listed != nil && [listed isEqualToString:@"7-x"] && listed.length == 3 && listErr == nil &&
		      localeArg != nil && [localeArg isEqualToString:@"7-x"] && locErr == nil &&
		      localeList != nil && [localeList isEqualToString:@"7-x"] && listLocErr == nil &&
		      localized != nil && [localized isEqualToString:@"7-x"] && classErr == nil,
		      [[NSString stringWithFormat:@"good=%@ okErr=%@ bad=%@ badErr=%@ list=%@ locArg=%@ locList=%@ class=%@",
			good, okErr != nil ? [okErr description] : @"(none)", bad,
			badErr != nil ? [badErr description] : @"(none)", listed, localeArg, localeList,
			localized] UTF8String]);
		covers("NSString", "initWithValidatedFormat:validFormatSpecifiers:error:");
		covers("NSString", "initWithValidatedFormat:validFormatSpecifiers:arguments:error:");
		covers("NSString", "initWithValidatedFormat:validFormatSpecifiers:locale:error:");
		covers("NSString", "initWithValidatedFormat:validFormatSpecifiers:locale:arguments:error:");
		covers("NSString", "localizedStringWithValidatedFormat:validFormatSpecifiers:error:");
	}

	{
		/* THE BYTE-OUT DOOR, NEW ASSERTIONS. The library's own note records why this door is worth
		 * asserting: it takes its length from the CONVERSION rather than from the wrong encoding's byte
		 * count, so every number here is the converted one. Apple's three answers are all asserted - a NULL
		 * buffer asks for the size, a buffer big enough gets the bytes and reports the range it consumed,
		 * and a buffer too small is refused WITH the whole range left over - plus the range's start, so a
		 * door that copied from the beginning whatever it was asked for cannot pass. */
		NSString *s = [NSString stringWithUTF8String:"caf\xC3\xA9"];
		NSRange all = NSMakeRange(0, [s length]);
		NSRange tail = NSMakeRange(1, [s length] - 1);	/* "afé": 4 bytes, so a start-offset copy differs */
		char buf[32], offsetBuf[32], small[3];
		NSUInteger sized = 0, copied = 0, offset = 0, refusedUsed = 0;
		NSRange sizedLeft = NSMakeRange(NSNotFound, 0), copiedLeft = NSMakeRange(NSNotFound, 0);
		NSRange offsetLeft = NSMakeRange(NSNotFound, 0), refusedLeft = NSMakeRange(NSNotFound, 0);
		BOOL sizedOK, copiedOK, offsetOK, refusedOK;

		memset(buf, 'Z', sizeof(buf));
		memset(offsetBuf, 'Z', sizeof(offsetBuf));
		memset(small, 'Z', sizeof(small));
		sizedOK = [s getBytes:NULL maxLength:0 usedLength:&sized encoding:NSUTF8StringEncoding
				options:0 range:all remainingRange:&sizedLeft];
		copiedOK = [s getBytes:buf maxLength:sizeof(buf) usedLength:&copied encoding:NSUTF8StringEncoding
				 options:0 range:all remainingRange:&copiedLeft];
		offsetOK = [s getBytes:offsetBuf maxLength:sizeof(offsetBuf) usedLength:&offset encoding:NSUTF8StringEncoding
				 options:0 range:tail remainingRange:&offsetLeft];
		refusedOK = [s getBytes:small maxLength:sizeof(small) usedLength:&refusedUsed
				  encoding:NSUTF8StringEncoding options:0 range:all remainingRange:&refusedLeft];

		check("byte-out-door-sizes-copies-and-refuses",
		      /* the size form: 5 UTF-8 bytes for 4 units, and the whole range consumed */
		      sizedOK && sized == 5 && sizedLeft.location == 4 && sizedLeft.length == 0 &&
		      /* the copy form: the bytes AND the consumed range, asserted together */
		      copiedOK && copied == 5 && (unsigned char)buf[5] == 'Z' &&
		      memcmp(buf, [s UTF8String], 5) == 0 && copiedLeft.location == 4 &&
		      /* the RANGE IS HONOURED: starting at 1 copies "afé", 4 bytes, not the first 4 of "café" */
		      offsetOK && offset == 4 && memcmp(offsetBuf, "af\xC3\xA9", 4) == 0 &&
		      /* and a buffer that cannot hold the conversion is refused with the range for another try */
		      refusedOK == NO && refusedLeft.location == 0 && refusedLeft.length == 4 &&
		      (unsigned char)small[0] == 'Z',
		      [[NSString stringWithFormat:
			@"sized=%lu/%lu,%lu copied=%lu/%lu,%lu offset=%lu/%lu,%lu refused=%d/%lu,%lu"
			@" | ok=%d,%d,%d,%d buf5=%d small0=%d wholeEq=%d tailEq=%d",
			(unsigned long)sized, (unsigned long)sizedLeft.location, (unsigned long)sizedLeft.length,
			(unsigned long)copied, (unsigned long)copiedLeft.location, (unsigned long)copiedLeft.length,
			(unsigned long)offset, (unsigned long)offsetLeft.location, (unsigned long)offsetLeft.length,
			(int)refusedOK, (unsigned long)refusedLeft.location, (unsigned long)refusedLeft.length,
			(int)sizedOK, (int)copiedOK, (int)offsetOK, (int)refusedOK,
			(int)(unsigned char)buf[5], (int)(unsigned char)small[0],
			(int)(memcmp(buf, [s UTF8String], 5) == 0), (int)(memcmp(offsetBuf, "af\xC3\xA9", 4) == 0)]
			UTF8String]);
		covers("NSString", "getBytes:maxLength:usedLength:encoding:options:range:remainingRange:");
	}

	{
		/* THE COMPATIBILITY FORMS OF THE NORMALIZATION DOORS, NEW ASSERTIONS: the axis that separates them
		 * from the canonical pair is exactly the ligature the canonical pair leaves alone. The probe
		 * already measures that split through the OTHER spellings (the NFKC/NFKD checks), so these two
		 * doors are asserted against the same fixture and the same answer. */
		NSString *ligature = @"\uFB01";	/* the fi ligature */
		NSString *nfc = [ligature precomposedStringWithCanonicalMapping];
		NSString *nfd = [ligature decomposedStringWithCanonicalMapping];
		NSString *nfkc = [ligature precomposedStringWithCompatibilityMapping];
		NSString *nfkd = [ligature decomposedStringWithCompatibilityMapping];

		check("compatibility-normalization-folds-the-ligature-canonical-keeps",
		      [nfc isEqualToString:ligature] && [nfd isEqualToString:ligature] &&
		      [nfkc isEqualToString:@"fi"] && [nfkd isEqualToString:@"fi"] &&
		      nfkc.length == 2 && nfkd.length == 2,
		      [[NSString stringWithFormat:@"byDoor nfc=%lu nfkc=%@ nfkd=%@ canonicalKeeps=%d",
			(unsigned long)nfc.length, nfkc, nfkd,
			(int)([nfc isEqualToString:ligature] && [nfd isEqualToString:ligature])] UTF8String]);
		covers("NSString", "precomposedStringWithCompatibilityMapping");
		covers("NSString", "decomposedStringWithCompatibilityMapping");
	}

	{
		/* NEW ASSERTIONS: THE FOLDING DOOR. It applies a rule the library STATES - ASCII case plus the
		 * non-base marks - and the case that separates the two options is asserted beside the combined one,
		 * so a door that folded only one of them cannot pass. The refusal of width/numeric folding (this
		 * library ships no tables for them, so it raises rather than answering a different question) is the
		 * option-taking comparisons' contract and is asserted in their family, not here. */
		NSString *eAcute = [NSString stringWithUTF8String:"caf\xC3\xA9"];
		NSString *both = [eAcute stringByFoldingWithOptions:
					(NSCaseInsensitiveSearch | NSDiacriticInsensitiveSearch) locale:nil];
		NSString *caseOnly = [eAcute stringByFoldingWithOptions:NSCaseInsensitiveSearch locale:nil];
		NSString *marksOnly = [@"CAF\xC3\x89" stringByFoldingWithOptions:NSDiacriticInsensitiveSearch
									 locale:nil];
		NSString *plain = [@"plain" stringByFoldingWithOptions:NSCaseInsensitiveSearch locale:nil];

		check("folding-takes-case-and-marks-separately",
		      both != nil && [both isEqualToString:@"cafe"] && both.length == 4 &&
		      /* CASE ONLY MUST KEEP THE ACCENT - the assertion that makes the first line about the marks
		       * and not about a fold that happens to drop them. "café" is 4 units, so this is a real
		       * difference and not a length coincidence. */
		      [caseOnly isEqualToString:eAcute] && caseOnly.length == 4 &&
		      /* AND MARKS ONLY MUST KEEP THE CASE */
		      [marksOnly isEqualToString:@"CAFE"] && marksOnly.length == 4 &&
		      [plain isEqualToString:@"plain"],
		      [[NSString stringWithFormat:@"both=%@ caseOnly=%@ marksOnly=%@", both, caseOnly,
			marksOnly] UTF8String]);
		covers("NSString", "stringByFoldingWithOptions:locale:");
	}

	{
		/* NEW ASSERTIONS: THE TRANSFORM DOOR, on the two transform names this library implements. The
		 * strip is "decompose canonically, then remove every non-base mark", so its answers are the
		 * canonical pair's plus the removal - which is why the ligature is asserted to SURVIVE here (a
		 * canonical decomposition does not touch a compatibility ligature; the compatibility
		 * normalization doors are the ones that fold it, and they are asserted above). And `reverse` is
		 * ignored for a strip, which the library states, so the flagged answer must equal the unflagged
		 * one rather than merely being plausible. */
		NSString *eAcute = [NSString stringWithUTF8String:"caf\xC3\xA9"];
		NSString *stripped = [eAcute stringByApplyingTransform:NSStringTransformStripDiacritics
							       reverse:NO];
		NSString *stripCombining = [eAcute stringByApplyingTransform:NSStringTransformStripCombiningMarks
								     reverse:NO];
		NSString *reversed = [eAcute stringByApplyingTransform:NSStringTransformStripDiacritics
							       reverse:YES];
		NSString *ligature = [@"\uFB01" stringByApplyingTransform:NSStringTransformStripDiacritics
								  reverse:NO];
		NSString *plain = [@"plain" stringByApplyingTransform:NSStringTransformStripDiacritics reverse:NO];

		check("strip-diacritics-decomposes-then-removes-marks",
		      stripped != nil && [stripped isEqualToString:@"cafe"] && stripped.length == 4 &&
		      stripCombining != nil && [stripCombining isEqualToString:@"cafe"] &&
		      /* `reverse` is meaningless for a strip: the library says so, and here it is measured rather
		       * than believed. */
		      reversed != nil && [reversed isEqualToString:stripped] &&
		      /* THE COMPATIBILITY LIGATURE SURVIVES A CANONICAL STRIP, which is the boundary this door
		       * shares with the canonical normalization pair. */
		      ligature != nil && [ligature isEqualToString:@"\uFB01"] && ligature.length == 1 &&
		      [plain isEqualToString:@"plain"],
		      [[NSString stringWithFormat:@"strip=%@ combining=%@ reversed=%@ ligature=%lu units plain=%@",
			stripped, stripCombining, reversed, (unsigned long)ligature.length, plain] UTF8String]);
		covers("NSString", "stringByApplyingTransform:reverse:");
	}

	{
		/* NEW ASSERTION: THE DEPRECATED RANGED C-STRING DOOR. Its contract is in the library's own note - it
		 * converts a RANGE and REPORTS what it could not convert, and a failure leaves the buffer alone -
		 * and the terminator is the delegated door's own rule, so it is asserted rather than assumed. */
		NSString *s = [NSString stringWithUTF8String:"caf\xC3\xA9"];
		char room[32], small[3];
		NSRange left = NSMakeRange(NSNotFound, 0), failedLeft = NSMakeRange(NSNotFound, 0);

		memset(room, 'Z', sizeof(room));
		memset(small, 'Z', sizeof(small));
		[s getCString:room maxLength:sizeof(room) range:NSMakeRange(1, 3) remainingRange:&left];
		[s getCString:small maxLength:sizeof(small) range:NSMakeRange(0, [s length])
		    remainingRange:&failedLeft];

		check("ranged-cstring-door-converts-a-range-and-reports-the-rest",
		      /* "afe" + the accent from unit 1: 4 bytes, terminated, and the range reported as consumed */
		      memcmp(room, "af\xC3\xA9", 4) == 0 && room[4] == '\0' &&
		      left.location == 4 && left.length == 0 &&
		      /* AND A BUFFER THAT CANNOT HOLD IT IS REFUSED WITH THE WHOLE RANGE LEFT OVER and the buffer
		       * untouched - the half the library's note calls out by name. */
		      (unsigned char)small[0] == 'Z' && failedLeft.location == 0 && failedLeft.length == 4,
		      [[NSString stringWithFormat:@"room=%s room4=%d left=%lu,%lu small0=%d failedLeft=%lu,%lu",
			room, (int)room[4], (unsigned long)left.location, (unsigned long)left.length,
			(int)(unsigned char)small[0], (unsigned long)failedLeft.location,
			(unsigned long)failedLeft.length] UTF8String]);
		covers("NSString", "getCString:maxLength:range:remainingRange:");
	}

	{
		/* NEW ASSERTIONS: THE THREE NO-COPY CONSTRUCTORS. What is OBSERVABLE is asserted - the value, the
		 * length, and for the deallocator form THE BLOCK RUNNING with the buffer's length, which
		 * distinguishes a block that is never called from one called with the wrong count. The doors build
		 * through the copying constructor and then release the buffer, so the string never IS the caller's
		 * bytes; that shape is asserted by the values, and the buffer discipline by the deallocator. */
		char *heap = (char *)malloc(8);
		char *heap2 = (char *)malloc(8);
		char *cstr = (char *)malloc(8);
		__block BOOL deallocatorRan = NO;
		__block NSUInteger deallocatorLength = 0;
		NSString *byFreeWhenDone, *byDeallocator, *byCStringNoCopy;

		memcpy(heap, "abcdefgh", 8);
		memcpy(heap2, "xyz", 3);
		memcpy(cstr, "OK", 3);
		byFreeWhenDone = [[NSString alloc] initWithBytesNoCopy:heap length:5
							      encoding:NSUTF8StringEncoding freeWhenDone:YES];
		byDeallocator = [[NSString alloc] initWithBytesNoCopy:heap2 length:3
							     encoding:NSUTF8StringEncoding
							    deallocator:^(void *bytes, NSUInteger length) {
			deallocatorRan = YES;
			deallocatorLength = length;
			free(bytes);
		}];
		byCStringNoCopy = [[NSString alloc] initWithCStringNoCopy:cstr length:2 freeWhenDone:YES];

		check("no-copy-constructors-build-the-value-and-honour-their-buffers",
		      byFreeWhenDone != nil && [byFreeWhenDone isEqualToString:@"abcde"] &&
		      byFreeWhenDone.length == 5 &&
		      byDeallocator != nil && [byDeallocator isEqualToString:@"xyz"] &&
		      deallocatorRan && deallocatorLength == 3 &&
		      byCStringNoCopy != nil && [byCStringNoCopy isEqualToString:@"OK"] &&
		      byCStringNoCopy.length == 2,
		      [[NSString stringWithFormat:@"freeWhenDone=%@ deallocator=%@ ran=%d len=%lu cStringNoCopy=%@",
			byFreeWhenDone, byDeallocator, (int)deallocatorRan, (unsigned long)deallocatorLength,
			byCStringNoCopy] UTF8String]);
		covers("NSString", "initWithBytesNoCopy:length:encoding:freeWhenDone:");
		covers("NSString", "initWithBytesNoCopy:length:encoding:deallocator:");
		covers("NSString", "initWithCStringNoCopy:length:freeWhenDone:");
	}

	{
		/* NEW ASSERTION: THE LENGTH-TAKING C-STRING CREATOR. The library's note states the rule - the length
		 * is honoured and the NUL is not - so the fixture has NO terminator at the count and other bytes
		 * beyond it. A door that scanned for a terminator would answer 5 units for `two`. */
		static const char bytes[] = { 'a', 'b', 'c', 'd', 'e' };
		NSString *two = [NSString stringWithCString:bytes length:2];
		NSString *five = [NSString stringWithCString:bytes length:5];
		const char *noBytes = NULL;
		NSString *nullBytes = [NSString stringWithCString:noBytes length:3];

		check("cstring-with-length-reads-exactly-that-many-bytes",
		      two != nil && [two isEqualToString:@"ab"] && two.length == 2 &&
		      five != nil && [five isEqualToString:@"abcde"] && five.length == 5 &&
		      nullBytes == nil,
		      [[NSString stringWithFormat:@"two=%@ (%lu) five=%@ (%lu) null=%d", two,
			(unsigned long)two.length, five, (unsigned long)five.length,
			(int)(nullBytes == nil)] UTF8String]);
		covers("NSString", "stringWithCString:length:");
	}

	{
		/* NEW ASSERTIONS: THE COMPOSED-CHARACTER-SEQUENCE RANGE DOORS, on the definition this family states
		 * - a base plus the marks that follow it. So "e" + U+0301 is ONE sequence of two units, index 1
		 * belongs to it, and a range that spans part of it expands to the whole. AND A SURROGATE PAIR IS
		 * ONE COMPOSED SEQUENCE, which is the case a mark-walking implementation can miss. */
		static const unichar pairUnits[] = { 'h', 0xD83D, 0xDE00, 'i' };
		NSString *marked = [NSString stringWithUTF8String:"e\xCC\x81"];
		NSString *pair = [NSString stringWithCharacters:pairUnits length:4];
		NSRange at0 = [marked rangeOfComposedCharacterSequenceAtIndex:0];
		NSRange at1 = [marked rangeOfComposedCharacterSequenceAtIndex:1];
		NSRange spanning = [marked rangeOfComposedCharacterSequencesForRange:NSMakeRange(1, 1)];
		NSRange pairAt1 = [pair rangeOfComposedCharacterSequenceAtIndex:1];
		NSRange pairWhole = [pair rangeOfComposedCharacterSequencesForRange:NSMakeRange(1, 2)];

		check("composed-sequence-ranges-name-the-base-and-its-marks",
		      marked.length == 2 &&
		      at0.location == 0 && at0.length == 2 &&
		      at1.location == 0 && at1.length == 2 &&
		      spanning.location == 0 && spanning.length == 2 &&
		      pairAt1.location == 1 && pairAt1.length == 2 &&
		      pairWhole.location == 1 && pairWhole.length == 2,
		      [[NSString stringWithFormat:
			@"at0=%lu,%lu at1=%lu,%lu spanning=%lu,%lu pairAt1=%lu,%lu pairWhole=%lu,%lu",
			(unsigned long)at0.location, (unsigned long)at0.length,
			(unsigned long)at1.location, (unsigned long)at1.length,
			(unsigned long)spanning.location, (unsigned long)spanning.length,
			(unsigned long)pairAt1.location, (unsigned long)pairAt1.length,
			(unsigned long)pairWhole.location, (unsigned long)pairWhole.length] UTF8String]);
		covers("NSString", "rangeOfComposedCharacterSequenceAtIndex:");
		covers("NSString", "rangeOfComposedCharacterSequencesForRange:");
	}

	{
		/* NEW ASSERTION: THE CLASS-SIDE VALIDATED-FORMAT DOOR, the sibling of the instance family asserted
		 * above. One rule through both: the specifier list is a whitelist, so a listed specifier renders as
		 * the unvalidated door would and an unlisted one is refused WITH the error. The numbers are that
		 * check's own, so the two doors are required to AGREE rather than each to be plausible. */
		NSError *okErr = nil, *badErr = nil;
		NSString *good = [NSString stringWithValidatedFormat:@"%d and %@" validFormatSpecifiers:@"%d %@"
							       error:&okErr, 42, @"text"];
		NSString *bad = [NSString stringWithValidatedFormat:@"%d and %@" validFormatSpecifiers:@"%d"
							      error:&badErr, 42, @"text"];

		check("class-validated-format-door-shares-the-rule",
		      good != nil && [good isEqualToString:@"42 and text"] && good.length == 11 && okErr == nil &&
		      bad == nil && badErr != nil && [badErr.domain isEqualToString:@"NSCocoaErrorDomain"],
		      [[NSString stringWithFormat:@"good=%@ (%lu) okErr=%@ bad=%@ badErr=%@", good,
			(unsigned long)good.length, okErr != nil ? [okErr description] : @"(none)", bad,
			badErr != nil ? [badErr description] : @"(none)"] UTF8String]);
		covers("NSString", "stringWithValidatedFormat:validFormatSpecifiers:error:");
	}

	{
		/* NEW ASSERTION: THE THREE DEPRECATED C-STRING SPELLINGS THAT NOTHING CALLED. Each contract is stated
		 * at its own door and they differ in exactly the ways their names say: the one-argument copy takes
		 * the whole string and terminates it (no length is passed, so Apple's "buffer is large enough" is
		 * followed rather than a bound only this library could know), the length-taking one DROPS the BOOL
		 * and leaves the buffer alone when it cannot fit, and the creator fixes UTF-8 so a NULL answers nil.
		 * The silent refusal is the half a "returns NO" test could not see, which is why the buffer's
		 * contents are asserted rather than a return value that does not exist. */
		NSString *s = [NSString stringWithUTF8String:"caf\xC3\xA9"];
		char whole[16], bounded[16], small[3];
		NSString *made = [NSString stringWithCString:"caf\xC3\xA9"];
		const char *noBytes = NULL;
		NSString *fromNull = [NSString stringWithCString:noBytes];

		memset(whole, 'Z', sizeof(whole));
		memset(bounded, 'Z', sizeof(bounded));
		memset(small, 'Z', sizeof(small));
		[s getCString:whole];
		[s getCString:bounded maxLength:sizeof(bounded)];
		[s getCString:small maxLength:sizeof(small)];	/* 5 bytes plus their NUL cannot fit in 3 */

		check("deprecated-cstring-spellings-copy-terminate-and-refuse-silently",
		      memcmp(whole, "caf\xC3\xA9", 5) == 0 && whole[5] == '\0' &&
		      memcmp(bounded, "caf\xC3\xA9", 5) == 0 && bounded[5] == '\0' &&
		      (unsigned char)small[0] == 'Z' &&
		      made != nil && [made isEqualToString:s] && made.length == 4 &&
		      fromNull == nil,
		      [[NSString stringWithFormat:@"whole=%s whole5=%d bounded=%s small0=%d made=%@ fromNull=%d",
			whole, (int)whole[5], bounded, (int)(unsigned char)small[0], made,
			(int)(fromNull == nil)] UTF8String]);
		covers("NSString", "getCString:");
		covers("NSString", "getCString:maxLength:");
		covers("NSString", "stringWithCString:");
	}

	{
		/* OUR DOOR, NOT APPLE'S (the header says so, and the ledger question is why): the receiver as an
		 * absolute path. EVERY CLAUSE IS A PROPERTY OF THE CONTRACT, not a literal this file would have to
		 * guess: an already-absolute path answers the STANDARDISED one, applying the same lexical rule
		 * -stringByStandardizingPath: states (so ".." POPS the component before it), a relative one is joined
		 * to the working directory and standardised the same way, and the empty string is answered as itself,
		 * because there is nothing there to make absolute. */
		NSString *already = @"/System/Shared/../Shared/tests";
		NSString *made = [@"relative/./path" absolutePath];
		NSString *empty = [@"" absolutePath];

		check("absolute-path-standardises-and-joins-the-working-directory",
		      [[already absolutePath] isEqualToString:@"/System/Shared/tests"] &&
		      [made isAbsolutePath] && [made hasSuffix:@"/relative/path"] &&
		      [empty isEqualToString:@""],
		      [[NSString stringWithFormat:@"already=%@ made=%@ emptyLength=%lu", [already absolutePath], made,
			(unsigned long)[empty length]] UTF8String]);
		covers("NSString", "absolutePath");
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
