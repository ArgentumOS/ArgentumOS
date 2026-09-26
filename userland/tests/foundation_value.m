/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_value, unit 2 of 2 — the checks (ARC).
 *
 *   number-convert      every scalar conversion
 *   number-value        VALUE semantics ACROSS TYPES: 1 == 1.0, and equal hashes
 *   number-compare      -compare: orders, and returns 0 for equal values
 *   number-description  a number describes itself through NSString
 *   data-roundtrip      bytes/length, and equality by value (true and false)
 *   data-mutable        append is visible, -copy is a snapshot
 *   date                exact epoch round-trip, ordering, a real clock, self-description
 *   cross-tu            values built in the OTHER unit compare equal to local ones
 */

#import "foundation_value.h"
#include <stdio.h>
#import <objc/runtime.h>
#include <string.h>

/*
 * A TRANSFORMER WHOSE NAME IS ITS CLASS NAME, so the fallback path is exercised by the same fixture
 * as the registry path - and one that is REVERSIBLE, so both directions are real arithmetic rather
 * than a pass-through.
 */
@interface FnDoubler : NSValueTransformer
@end

@implementation FnDoubler
+ (Class)transformedValueClass
{
	return [NSNumber class];
}

+ (BOOL)allowsReverseTransformation
{
	return YES;
}

- (id)transformedValue:(id)value
{
	return @([value doubleValue] * 2);
}

- (id)reverseTransformedValue:(id)value
{
	return @([value doubleValue] / 2);
}
@end

static int okc, failc;

static void check(const char *name, int ok, const char *detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-VALUE %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-VALUE %s FAIL %s\n", name, detail ? detail : "");
	}
}

/* A nil NEEDLE ON PURPOSE: the check below asks -rangeOfData: about one to see the answer.
 * Fetching it says so; a literal at the call site would be a -Wnonnull finding of its own. */
static NSData *fn_no_data(void)
{
	return nil;
}

int main(void)
{
	{
		NSNumber *n = [NSNumber numberWithInt:42];
		NSNumber *half = [NSNumber numberWithDouble:1.5];
		NSNumber *yes = [NSNumber numberWithBool:YES];

		check("number-convert",
		      [n intValue] == 42 && [n longLongValue] == 42 &&
		      [n unsignedLongLongValue] == 42 && [n boolValue] == YES &&
		      [n doubleValue] == 42.0 &&
		      [half intValue] == 1 && [half doubleValue] == 1.5 &&
		      [yes boolValue] == YES &&
		      [[NSNumber numberWithBool:NO] boolValue] == NO,
		      "every scalar conversion agrees");
	}

	{
		NSNumber *one = [NSNumber numberWithInt:1];
		NSNumber *oneFloat = [NSNumber numberWithDouble:1.0];
		NSNumber *two = [NSNumber numberWithInt:2];

		check("number-value",
		      [one isEqual:oneFloat] && [oneFloat isEqual:one] &&
		      [one hash] == [oneFloat hash] &&
		      ![one isEqual:two] && ![one isEqual:@"1"] &&
		      [one isEqual:one],
		      "1 == 1.0 with equal hashes; 1 != 2; 1 != an NSString");
	}

	{
		NSNumber *small = [NSNumber numberWithInt:1];
		NSNumber *big = [NSNumber numberWithInt:2];

		check("number-compare",
		      [small compare:big] == -1 && [big compare:small] == 1 &&
		      [small compare:[NSNumber numberWithInt:1]] == 0,
		      "-compare: returns -1/0/1");
	}

	{
		NSNumber *n = [NSNumber numberWithInt:42];
		NSNumber *half = [NSNumber numberWithDouble:1.5];

		check("number-description",
		      strcmp([[n description] UTF8String], "42") == 0 &&
		      strcmp([[half description] UTF8String], "1.5") == 0,
		      "a number describes itself, through NSString");
	}

	{
		static const unsigned char bytes[] = { 0xde, 0xad, 0xbe, 0xef };
		static const unsigned char other[] = { 0xde, 0xad, 0xbe, 0x00 };
		NSData *a = [NSData dataWithBytes:bytes length:4];
		NSData *b = [NSData dataWithBytes:bytes length:4];
		NSData *c = [NSData dataWithBytes:other length:4];

		check("data-roundtrip",
		      [a length] == 4 && memcmp([a bytes], bytes, 4) == 0 &&
		      [a isEqualToData:b] && [a hash] == [b hash] &&
		      ![a isEqualToData:c] &&
		      [[NSData dataWithBytes:bytes length:3] length] == 3,
		      "bytes and length round-trip; equality is by value");
	}

	{
		NSMutableData *m = [NSMutableData dataWithCapacity:2];
		NSData *snapshot;

		[m appendBytes:"ab" length:2];
		snapshot = [m copy];
		[m appendBytes:"cd" length:2];

		check("data-mutable",
		      [m length] == 4 && memcmp([m bytes], "abcd", 4) == 0 &&
		      [snapshot length] == 2 && memcmp([snapshot bytes], "ab", 2) == 0,
		      "append is visible through the immutable interface; -copy is a snapshot");
	}

	{
		NSDate *epoch = [NSDate dateWithTimeIntervalSince1970:1000.0];
		NSDate *later = [NSDate dateWithTimeIntervalSince1970:1001.0];
		NSDate *now = [NSDate date];
		double clock = [now timeIntervalSince1970];

		check("date",
		      [epoch timeIntervalSince1970] == 1000.0 &&
		      [later timeIntervalSinceDate:epoch] == 1.0 &&
		      [epoch compare:later] == -1 && [later compare:epoch] == 1 &&
		      [epoch compare:epoch] == 0 &&
		      [epoch isEqualToDate:[NSDate dateWithTimeIntervalSince1970:1000.0]] &&
		      ![epoch isEqualToDate:later] &&
		      [[epoch earlierDate:later] isEqualToDate:epoch] &&
		      [[epoch laterDate:later] isEqualToDate:later] &&
		      clock >= 0.0 && [[now description] length] > 0,
		      "an epoch round-trips exactly, ordering works, +date reads a clock");
		printf("FOUNDATION-VALUE clock=%g (the guest's own clock)\n", clock);
	}

	{
		check("cross-tu",
		      [[foundation_value_number() description] isEqualToString:@"42"] &&
		      [foundation_value_data() length] == 4 &&
		      [foundation_value_date()
		          isEqualToDate:[NSDate dateWithTimeIntervalSince1970:1000.0]],
		      "values built in the support unit equal local ones");
	}


	{
		/*
		 * THE AUDITED COCOA INVENTORY for NSNumber.
		 *
		 * The previous check was SELF-REFERENTIAL: it asserted that every selector
		 * in OUR header exists, which cannot see a method nobody declared — that is
		 * exactly how +stringWithFormat:arguments: shipped missing. This list is
		 * Cocoa's documented surface, and the excluded list is asserted ABSENT, so
		 * the inventory cannot drift away from the code.
		 */
		static const char *classSelectors[] = {
			"numberWithBool:", "numberWithChar:", "numberWithShort:", "numberWithInt:",
			"numberWithLong:", "numberWithLongLong:", "numberWithInteger:",
			"numberWithUnsignedChar:", "numberWithUnsignedShort:", "numberWithUnsignedInt:",
			"numberWithUnsignedLong:", "numberWithUnsignedLongLong:",
			"numberWithUnsignedInteger:", "numberWithFloat:", "numberWithDouble:",
			NULL
		};
		static const char *instanceSelectors[] = {
			"initWithBool:", "initWithChar:", "initWithShort:", "initWithInt:",
			"initWithLong:", "initWithLongLong:", "initWithInteger:",
			"initWithUnsignedChar:", "initWithUnsignedShort:", "initWithUnsignedInt:",
			"initWithUnsignedLong:", "initWithUnsignedLongLong:",
			"initWithUnsignedInteger:", "initWithFloat:", "initWithDouble:",
			"boolValue", "charValue", "shortValue", "intValue", "longValue",
			"longLongValue", "integerValue", "unsignedCharValue", "unsignedShortValue",
			"unsignedIntValue", "unsignedLongValue", "unsignedLongLongValue",
			"unsignedIntegerValue", "floatValue", "doubleValue",
			"stringValue", "objCType", "descriptionWithLocale:",
			"isEqualToNumber:", "compare:",
			"isEqual:", "hash", "description", "copy", "mutableCopy",
			NULL
		};
		static const char *excluded[] = {
			/* THE THREE DECIMAL ENTRIES THAT STOOD HERE ARE GONE (W3b): decimalValue,
			 * numberWithDecimal: and initWithDecimal: are implemented on NSNumber now, and
			 * NSDecimalNumber carries the type — so the inventory below REQUIRES them, which is the
			 * point of removing an exclusion rather than decorating it. */
			/* THE REASON THIS CARRIED WAS STALE: it said "NSValue is not shipped", and
			 * NSValue HAS shipped (it has its own probe and case). The entries stay —
			 * they are NSValue's methods in Cocoa, and what this inventory checks is
			 * NSNumber's own surface, where they are correctly ABSENT — but a reason
			 * that is false is a claim about the tree, which is the failure mode §11
			 * exists to expose. */
			"valueWithBytes:objCType:",	/* NSValue */
			"initWithBytes:objCType:",	/* NSValue */
			"getValue:",			/* NSValue */
			"valueWithPointer:",		/* NSValue */
			"initWithPointer:",		/* NSValue */
			"pointerValue",			/* NSValue */
			"isEqualToValue:",		/* NSValue */
			NULL
		};
		NSNumber *probe = [NSNumber numberWithInt:1];
		int complete = 1;
		int i;

		for (i = 0; classSelectors[i] != NULL; i++) {
			if (![NSNumber respondsToSelector:sel_registerName(classSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-VALUE missing +%s\n", classSelectors[i]);
			}
		}
		for (i = 0; instanceSelectors[i] != NULL; i++) {
			if (![probe respondsToSelector:sel_registerName(instanceSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-VALUE missing -%s\n", instanceSelectors[i]);
			}
		}
		for (i = 0; excluded[i] != NULL; i++) {
			if ([probe respondsToSelector:sel_registerName(excluded[i])]) {
				complete = 0;
				printf("FOUNDATION-VALUE present but EXCLUDED: %s\n", excluded[i]);
			}
		}
		check("number-api-complete", complete,
		      "the audited Cocoa inventory for NSNumber (implemented present, excluded absent)");
	}


	{
		/* ...and the whole matrix BEHAVES, not merely exists. */
		check("number-matrix",
		      [[NSNumber numberWithChar:'A'] charValue] == 'A' &&
		      [[NSNumber numberWithShort:-2] shortValue] == -2 &&
		      [[NSNumber numberWithLong:3L] longValue] == 3L &&
		      [[NSNumber numberWithInteger:-4] integerValue] == -4 &&
		      [[NSNumber numberWithUnsignedInt:5u] unsignedIntValue] == 5u &&
		      [[NSNumber numberWithUnsignedLongLong:6ull] unsignedLongLongValue] == 6ull &&
		      [[NSNumber numberWithUnsignedInteger:7ul] unsignedIntegerValue] == 7ul &&
		      [[NSNumber numberWithUnsignedShort:8] unsignedShortValue] == 8 &&
		      [[NSNumber numberWithFloat:1.5f] floatValue] == 1.5f &&
		      [[NSNumber numberWithFloat:1.5f] objCType][0] == 'f' &&
		      [[NSNumber numberWithInt:1] objCType][0] == 'i' &&
		      [[NSNumber numberWithBool:YES] boolValue] == YES &&
		      strcmp([[[NSNumber numberWithInt:42] stringValue] UTF8String], "42") == 0 &&
		      strcmp([[NSNumber numberWithInt:42] objCType], "i") == 0 &&
		      strcmp([[NSNumber numberWithDouble:1.5] objCType], "d") == 0 &&
		      [[NSNumber numberWithUnsignedLongLong:18446744073709551615ull] doubleValue] > 0 &&
		      [[NSNumber numberWithInt:-1]
		          isEqual:[NSNumber numberWithUnsignedLongLong:18446744073709551615ull]],
		      "the matrix converts and -objCType reports the CREATION type");
	}


	{
		/*
		 * THE AUDITED COCOA INVENTORY for NSData/NSMutableData.
		 *
		 * `implemented` is Cocoa's documented surface and must all EXIST;
		 * `excluded` is what we deliberately do not ship and must all be ABSENT.
		 * The old check only confirmed what our own header declared — how
		 * +stringWithFormat:arguments: shipped missing — and this one named eight
		 * gaps, all of them implemented in the same pass rather than recorded as
		 * debt: the two :options:error: constructors (they had no error path at
		 * all), +dataWithBase64EncodedString:options: and the base64-Data pair,
		 * -writeToFile:options:error:, -enumerateByteRangesUsingBlock: and
		 * -setData:.
		 */
		static const char *classSelectors[] = {
			"data", "dataWithBytes:length:", "dataWithBytesNoCopy:length:",
			"dataWithBytesNoCopy:length:freeWhenDone:", "dataWithData:",
			"dataWithContentsOfFile:", "dataWithContentsOfFile:options:error:",
			"dataWithBase64EncodedString:", "dataWithBase64EncodedString:options:",
			/* D7's kind (D): these SHIPPED on 2026-09-19, so the inventory demands them. */
			"dataWithContentsOfURL:", "dataWithContentsOfURL:options:error:",
			NULL
		};
		static const char *instanceSelectors[] = {
			"initWithBytes:length:", "initWithBytesNoCopy:length:",
			"initWithBytesNoCopy:length:freeWhenDone:", "initWithData:",
			"initWithContentsOfFile:", "initWithContentsOfFile:options:error:",
			/* F12's codecs: they SHIPPED, so the inventory must demand them. */
			"compressedDataUsingAlgorithm:error:", "decompressedDataUsingAlgorithm:error:",
			"initWithBase64EncodedString:options:", "initWithBase64EncodedData:options:",
			"length", "bytes", "getBytes:length:", "getBytes:range:",
			"subdataWithRange:", "rangeOfData:options:range:",
			"base64EncodedStringWithOptions:", "base64EncodedDataWithOptions:",
			"writeToFile:atomically:", "writeToFile:options:error:",
			"initWithContentsOfURL:", "initWithContentsOfURL:options:error:",
			"writeToURL:atomically:", "writeToURL:options:error:",
			"enumerateByteRangesUsingBlock:",
			"isEqualToData:", "isEqual:", "hash", "description", "copy", "mutableCopy",
			/* NSCoding: implemented, and DEMANDED - the inventory had been silent about these,
			 * which is the gap class worth naming (§11.6.1 D7). */
			"initWithCoder:", "encodeWithCoder:",
			NULL
		};
		static const char *mutableClassSelectors[] = {
			"dataWithCapacity:", "dataWithLength:", NULL
		};
		static const char *mutableSelectors[] = {
			"initWithCapacity:", "initWithLength:", "appendBytes:length:", "appendData:",
			"setLength:", "increaseLengthBy:", "mutableBytes", "setData:",
			"replaceBytesInRange:withBytes:", "replaceBytesInRange:withBytes:length:",
			"resetBytesInRange:", NULL
		};
		static const char *excluded[] = {
			/* Deprecated by Cocoa itself. */
			"dataWithContentsOfMappedFile:",	/* superseded by the :options: form */
			"initWithContentsOfMappedFile:",	/* superseded by the :options: form */
			"getBytes:",				/* superseded by -getBytes:length: */
			/* THE SIX URL-TAKING FORMS USED TO BE LISTED HERE as "not shipped". They are
			 * IMPLEMENTED now (delegation to the file forms once a file URL is a path, with
			 * a REGISTERED refusal for every other scheme — §11.6.1 D9), so they moved to
			 * the two required lists above IN THE SAME CHANGE. This is the retirement the
			 * compression entries' note describes, done on purpose instead of being found
			 * later by a probe going red. */
			/* THE TWO COMPRESSION SELECTORS USED TO BE LISTED HERE as "a compression
			 * codec" this Foundation does not ship. **F12 SHIPPED THEM AND THIS LIST WAS NOT
			 * UPDATED WITH IT, SO THIS PROBE FAILED ITS OWN INVENTORY CHECK** — reporting them
			 * as "present but EXCLUDED", which is the check working correctly against a STALE
			 * CLAIM. That is precisely the failure mode §11's ledger exists to expose: an
			 * `excluded` entry is a claim about the tree, and shipping the selector makes the
			 * claim false. They are ASSERTED PRESENT in `instanceSelectors` above instead, in the
			 * same change, because the inventory runs BOTH ways: what ships must be demanded, and
			 * what is not shipped must be absent. */
			NULL
		};
		NSData *probe = [NSData data];
		NSMutableData *mutable = [[NSMutableData alloc] init];
		int complete = 1;
		int i;

		for (i = 0; classSelectors[i] != NULL; i++) {
			if (![NSData respondsToSelector:sel_registerName(classSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-VALUE missing +%s\n", classSelectors[i]);
			}
		}
		for (i = 0; instanceSelectors[i] != NULL; i++) {
			if (![probe respondsToSelector:sel_registerName(instanceSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-VALUE missing -%s\n", instanceSelectors[i]);
			}
		}
		for (i = 0; mutableClassSelectors[i] != NULL; i++) {
			if (![NSMutableData respondsToSelector:sel_registerName(mutableClassSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-VALUE missing +%s (mutable)\n", mutableClassSelectors[i]);
			}
		}
		for (i = 0; mutableSelectors[i] != NULL; i++) {
			if (![mutable respondsToSelector:sel_registerName(mutableSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-VALUE missing -%s (mutable)\n", mutableSelectors[i]);
			}
		}
		for (i = 0; excluded[i] != NULL; i++) {
			if ([probe respondsToSelector:sel_registerName(excluded[i])]) {
				complete = 0;
				printf("FOUNDATION-VALUE present but EXCLUDED: %s\n", excluded[i]);
			}
		}
		check("data-api-complete", complete,
		      "the audited Cocoa inventory for NSData/NSMutableData (implemented present, excluded absent)");
	}


	{
		/*
		 * THE AUDITED COCOA INVENTORY for NSDate.
		 *
		 * `implemented` is Cocoa's documented surface and must all EXIST;
		 * `excluded` is what we deliberately do not ship and must all be ABSENT,
		 * so shipping one fails here rather than quietly widening the gap. The
		 * previous check was self-referential — it could only confirm what our own
		 * header declared, which is how +stringWithFormat:arguments: shipped
		 * missing. Running this inventory named two gaps, and both are implemented
		 * above rather than recorded as debt:
		 *   +dateWithTimeIntervalSinceReferenceDate: and
		 *   -initWithTimeIntervalSinceReferenceDate:.
		 */
		static const char *classSelectors[] = {
			"date", "dateWithTimeIntervalSinceNow:", "dateWithTimeIntervalSince1970:",
			"dateWithTimeInterval:sinceDate:", "dateWithTimeIntervalSinceReferenceDate:",
			"distantPast", "distantFuture", "timeIntervalSinceReferenceDate",
			NULL
		};
		static const char *instanceSelectors[] = {
			"init", "initWithTimeIntervalSinceNow:", "initWithTimeIntervalSince1970:",
			"initWithTimeInterval:sinceDate:", "initWithTimeIntervalSinceReferenceDate:",
			"timeIntervalSince1970", "timeIntervalSinceReferenceDate",
			"timeIntervalSinceNow", "timeIntervalSinceDate:",
			"dateByAddingTimeInterval:", "earlierDate:", "laterDate:",
			"compare:", "isEqualToDate:",
			"isEqual:", "hash", "description", "descriptionWithLocale:",
			"copy", "mutableCopy",
			"initWithCoder:", "encodeWithCoder:",
			NULL
		};
		static const char *excluded[] = {
			/* Deprecated by Cocoa itself. */
			"addTimeInterval:",					/* superseded by -dateByAddingTimeInterval: */
			"initWithString:",					/* removed from Cocoa's documented API */
			"dateWithString:",					/* removed from Cocoa's documented API */
			/* Needs a piece this Foundation does not ship: a date PARSER. */
			"dateWithNaturalLanguageString:",			/* needs a date parser */
			"dateWithNaturalLanguageString:locale:",		/* needs a date parser */
			/* NOT absent for want of a class — NSCalendar, NSTimeZone and NSLocale all
			 * SHIP (F7 and stage E). These are absent from NSDATE's own surface
			 * because they are the CALENDAR's methods, which is where Cocoa puts them:
			 * the arithmetic lives on the calendar as -dateByAdding…. */
			"descriptionWithCalendarFormat:timeZone:locale:",	/* deprecated, and the calendar's */
			"dateByAddingComponents:toDate:options:",		/* NSCalendar's method, not NSDate's */
			"dateByAddingUnit:value:toDate:options:",		/* NSCalendar's method, not NSDate's */
			/* THE TWO NSCoding ENTRIES USED TO BE LISTED HERE. NSDate implements them now
			 * (the first class in this library to conform - §11.6.1 D7's kind (D)), so they
			 * are DEMANDED in the required lists above instead. */
			NULL
		};
		NSDate *probe = [NSDate dateWithTimeIntervalSince1970:0];
		int complete = 1;
		int i;

		for (i = 0; classSelectors[i] != NULL; i++) {
			if (![NSDate respondsToSelector:sel_registerName(classSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-VALUE missing +%s\n", classSelectors[i]);
			}
		}
		for (i = 0; instanceSelectors[i] != NULL; i++) {
			if (![probe respondsToSelector:sel_registerName(instanceSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-VALUE missing -%s\n", instanceSelectors[i]);
			}
		}
		for (i = 0; excluded[i] != NULL; i++) {
			if ([probe respondsToSelector:sel_registerName(excluded[i])]) {
				complete = 0;
				printf("FOUNDATION-VALUE present but EXCLUDED: %s\n", excluded[i]);
			}
		}
		check("date-api-complete", complete,
		      "the audited Cocoa inventory for NSDate (implemented present, excluded absent)");
	}


	{
		/* NSData's added surface. */
		static const unsigned char bytes[] = { 0xde, 0xad, 0xbe, 0xef };
		NSData *d = [NSData dataWithBytes:bytes length:4];
		NSData *sub = [d subdataWithRange:NSMakeRange(1, 2)];
		NSData *searched = [d rangeOfData:fn_no_data() options:NSDataSearchDefault
					    range:NSMakeRange(0, 4)].location == NSNotFound ? nil : d;
		unsigned char held[3];
		NSMutableData *m = [NSMutableData dataWithLength:4];
		NSData *needle = [NSData dataWithBytes:bytes length:4];
		NSRange found;
		/* F6: the constructors are nullable by MEASUREMENT (a malloc or file-read
		 * failure really can answer nil), so a consumer binds them and checks. The
		 * `!= nil` terms are not decoration: they are inside an && chain, so a nil
		 * here FAILS the check rather than letting it pass vacuously. */
		NSData *absent = [NSData dataWithBytes:"zz" length:2];
		NSData *equalA = [NSData dataWithBytes:bytes length:4];
		NSData *equalB = [NSData dataWithBytes:bytes length:4];

		memset(held, 0, sizeof held);
		[d getBytes:held range:NSMakeRange(1, 2)];
		found = [d rangeOfData:needle options:NSDataSearchDefault range:NSMakeRange(0, 4)];
		[m replaceBytesInRange:NSMakeRange(1, 2) withBytes:"XY"];
		[m resetBytesInRange:NSMakeRange(0, 1)];
		[m increaseLengthBy:2];

		check("data-extras",
		      [sub length] == 2 && memcmp([sub bytes], bytes + 1, 2) == 0 &&
		      held[0] == 0xad && held[1] == 0xbe &&
		      found.location == 0 && found.length == 4 &&
		      absent != nil &&
		      [d rangeOfData:absent
			     options:NSDataSearchDefault
			       range:NSMakeRange(0, 4)].location == NSNotFound &&
		      equalA != nil && equalB != nil &&
		      [equalA isEqualToData:equalB] &&
		      searched != nil,
		      "subdataWithRange:, getBytes:range:, rangeOfData: (found and not found)");
	}

	{
		/* base64, against the standard vectors, both ways. */
		NSData *empty = [NSData data];
		NSData *one = [@"a" dataUsingEncoding:NSUTF8StringEncoding];
		NSData *two = [@"ab" dataUsingEncoding:NSUTF8StringEncoding];
		NSData *three = [@"abc" dataUsingEncoding:NSUTF8StringEncoding];
		NSData *hello = [@"hello" dataUsingEncoding:NSUTF8StringEncoding];
		NSData *roundTrip = [[NSData alloc]
			initWithBase64EncodedString:@"aGVsbG8="
					    options:NSDataBase64DecodingIgnoreUnknownCharacters];

		check("data-base64",
		      [[empty base64EncodedStringWithOptions:NSDataBase64EncodingDefault]
		          isEqualToString:@""] &&
		      [[one base64EncodedStringWithOptions:NSDataBase64EncodingDefault]
		          isEqualToString:@"YQ=="] &&
		      [[two base64EncodedStringWithOptions:NSDataBase64EncodingDefault]
		          isEqualToString:@"YWI="] &&
		      [[three base64EncodedStringWithOptions:NSDataBase64EncodingDefault]
		          isEqualToString:@"YWJj"] &&
		      [[hello base64EncodedStringWithOptions:NSDataBase64EncodingDefault]
		          isEqualToString:@"aGVsbG8="] &&
		      [roundTrip isEqualToData:hello],
		      "the standard vectors encode and decode back");
	}

	{
		/* File round trip through the C library, and the mutable extras. */
		NSString *path = @"/fn-data-probe.bin";
		NSData *payload = [@"payload" dataUsingEncoding:NSUTF8StringEncoding];
		NSMutableData *m = [NSMutableData dataWithLength:4];
		NSData *moved;
		int ok;

		ok = [payload writeToFile:path atomically:YES];
		moved = [NSData dataWithContentsOfFile:path];
		ok = ok && [moved isEqualToData:payload];
		remove("/fn-data-probe.bin");

		[m replaceBytesInRange:NSMakeRange(1, 2) withBytes:"XY"];
		ok = ok && [m length] == 4 && memcmp([m bytes], "\0XY\0", 4) == 0;
		[m resetBytesInRange:NSMakeRange(0, 2)];
		ok = ok && memcmp([m bytes], "\0\0Y\0", 4) == 0;
		[m increaseLengthBy:2];
		ok = ok && [m length] == 6 && ((const unsigned char *)[m bytes])[5] == 0;

		check("data-mutable-extras", ok,
		      "writeToFile:atomically: round-trips, and replaceBytesInRange:/resetBytesInRange:/increaseLengthBy: act in place");
	}

	{
		/* NSDate's added surface. */
		NSDate *epoch = [NSDate dateWithTimeIntervalSince1970:1000.0];
		NSDate *later = [epoch dateByAddingTimeInterval:60.0];
		NSDate *relative = [NSDate dateWithTimeIntervalSinceNow:3600.0];
		NSDate *now = [NSDate date];

		check("date-extras",
		      [later timeIntervalSince1970] == 1060.0 &&
		      [later timeIntervalSinceDate:epoch] == 60.0 &&
		      [epoch timeIntervalSinceReferenceDate] == 1000.0 - 978307200.0 &&
		      [relative timeIntervalSinceNow] > 3500.0 &&
		      [relative timeIntervalSinceNow] < 3700.0 &&
		      [now timeIntervalSinceNow] < 1.0 &&
		      [[NSDate distantPast] compare:[NSDate distantFuture]] == NSOrderedAscending &&
		      [[NSDate distantPast] compare:now] == NSOrderedAscending &&
		      [[NSDate distantFuture] compare:now] == NSOrderedDescending,
		      "relative dates, the reference date, and distant past/future ordering");
	}


	{
		/*
		 * The byte-range enumeration, EXERCISED — the inventory could only prove it
		 * exists, and a block method that never fires is a claim, not a behaviour.
		 * The contract allows the ranges to be any decomposition, so one range
		 * covering everything is legal; what is asserted is that the block is
		 * handed the actual bytes and that `stop` ends the walk.
		 */
		static const unsigned char payload[] = { 1, 2, 3, 4, 5, 6 };
		NSData *d = [NSData dataWithBytes:payload length:6];
		__block NSUInteger total = 0;
		__block NSUInteger calls = 0;
		__block int bytesMatch = 1;
		__block int stoppedEarly = 0;

		[d enumerateByteRangesUsingBlock:^(const void *chunk, NSRange range, BOOL *stop) {
			calls++;
			total += range.length;
			if (range.location != 0 || range.length != 6 ||
			    memcmp(chunk, payload, 6) != 0) {
				bytesMatch = 0;
			}
			*stop = YES;		/* the contract: setting it ends the enumeration */
		}];
		/* A second walk that does NOT stop, to show it runs to the end. */
		[d enumerateByteRangesUsingBlock:^(const void *chunk, NSRange range, BOOL *stop) {
			(void)chunk;
			(void)range;
			(void)stop;
			stoppedEarly++;
		}];

		check("data-block-enumeration",
		      calls == 1 && total == 6 && bytesMatch &&
		      stoppedEarly == 1,
		      "enumerateByteRangesUsingBlock: hands over the real bytes and honours stop");
	}

	{
		/* D9's SPLIT EXPERIMENT, one call per check because printed checks survive an abort: the
		 * last name printed is the step that died. The round trip aborted inside the middle step
		 * of the previous attempt, and the leading suspect is the PATH itself — the FSH's temp
		 * directory contains a SPACE. */
		NSString *path = @"/System/Temporary Files/fndata-url-probe";
		NSURL *fileURL = [NSURL fileURLWithPath:path];

		check("data-url-url",
		      fileURL != nil && [fileURL isFileURL] &&
		      [[fileURL path] isEqualToString:path],
		      fileURL == nil ? "fileURLWithPath: answered nil"
		      : [[NSString stringWithFormat:@"isFile=%d path=%@",
			(int)[fileURL isFileURL], [fileURL path]] UTF8String]);

		(void)path;
	}

	{
		NSString *path = @"/System/Temporary Files/fndata-url-probe";
		NSURL *fileURL = [NSURL fileURLWithPath:path];
		NSData *written = [NSData dataWithBytes:"url-form" length:8];
		BOOL wrote = [written writeToURL:fileURL atomically:YES];

		check("data-url-write", wrote,
		      [[NSString stringWithFormat:@"writeToURL: answered %d", (int)wrote] UTF8String]);
	}

	{
		NSString *path = @"/System/Temporary Files/fndata-url-probe";
		NSURL *fileURL = [NSURL fileURLWithPath:path];
		NSData *back = [NSData dataWithContentsOfURL:fileURL];
		NSData *expect = [NSData dataWithBytes:"url-form" length:8];

		check("data-url-read",
		      back != nil && [back isEqualToData:expect],
		      [[NSString stringWithFormat:@"read back %lu byte(s)",
			(unsigned long)(back != nil ? [back length] : 0)] UTF8String]);
		remove([path UTF8String]);
	}

	{
		/* THE REFUSAL, SPLIT THE SAME WAY — the abort was NOT in the round trip (all three steps
		 * pass), so it is here: building the non-file URL, or the refusal itself (§11.6.1 D9). */
		NSURL *webURL = [NSURL URLWithString:@"http://example.com/x"];

		check("data-url-nonfile-url",
		      webURL != nil && ![webURL isFileURL],
		      webURL == nil ? "URLWithString: answered nil"
		      : [[NSString stringWithFormat:@"isFile=%d", (int)[webURL isFileURL]] UTF8String]);

		(void)webURL;
	}

	{
		NSURL *webURL = [NSURL URLWithString:@"http://example.com/x"];
		NSError *error = nil;
		NSData *refused = [NSData dataWithContentsOfURL:webURL options:0 error:&error];

		check("data-url-nonfile-refuse",
		      refused == nil && error != nil,
		      [[NSString stringWithFormat:@"refused=%d error=%d",
			(int)(refused == nil), (int)(error != nil)] UTF8String]);
	}

	{
		/* THE FIRST NSCoding CLASS IN THE LIBRARY (D7's kind (D)): the protocol, the coder and the
		 * archiver all shipped, and NOTHING OF OURS COULD BE ARCHIVED until NSDate conformed. A
		 * date goes through our own archiver and comes back equal. */
		NSDate *out = [NSDate dateWithTimeIntervalSince1970:1234567890.5];
		NSData *archive = [NSKeyedArchiver archivedDataWithRootObject:out];
		NSDate *back = archive != nil ? [NSKeyedUnarchiver unarchiveObjectWithData:archive] : nil;

		check("date-nscoding-round-trip",
		      archive != nil && back != nil && [back isEqualToDate:out] &&
		      [back timeIntervalSince1970] == [out timeIntervalSince1970],
		      [[NSString stringWithFormat:@"archive=%lu back=%d equal=%d",
			(unsigned long)(archive != nil ? [archive length] : 0), (int)(back != nil),
			(int)(back != nil && [back isEqualToDate:out])] UTF8String]);
	}

	{
		/* NSData'S NSCoding PAIR, through our own archiver (D7's kind (D)). */
		NSData *out = [NSData dataWithBytes:"archived-bytes" length:14];
		NSData *archive = [NSKeyedArchiver archivedDataWithRootObject:out];
		NSData *back = archive != nil ? [NSKeyedUnarchiver unarchiveObjectWithData:archive] : nil;

		check("data-nscoding-round-trip",
		      archive != nil && back != nil && [back isEqualToData:out] && [back length] == 14,
		      [[NSString stringWithFormat:@"archive=%lu back=%lu equal=%d",
			(unsigned long)(archive != nil ? [archive length] : 0),
			(unsigned long)(back != nil ? [back length] : 0),
			(int)(back != nil && [back isEqualToData:out])] UTF8String]);
	}

	{
		/* NSUUID (W2h): the string form round-trips through the parser, the byte form round-trips
		 * through the reader, and TWO IDENTIFIERS DIFFER - an identifier that can repeat is not
		 * one, which is the whole reason +UUID reads the kernel's entropy pool. */
		NSUUID *first = [NSUUID UUID];
		NSUUID *second = [NSUUID UUID];
		NSUUID *parsed = first != nil ? [[NSUUID alloc] initWithUUIDString:[first UUIDString]] : nil;
		unsigned char bytes[16];
		NSUUID *fromBytes = nil;

		first != nil ? (void)[first getUUIDBytes:bytes] : (void)0;
		fromBytes = first != nil ? [[NSUUID alloc] initWithUUIDBytes:bytes] : nil;
		check("uuid-round-trip",
		      first != nil && second != nil && parsed != nil && fromBytes != nil &&
		      [parsed isEqual:first] && [fromBytes isEqual:first] &&
		      ![first isEqual:second] && [first hash] == [parsed hash] &&
		      [[first UUIDString] length] == 36 &&
		      [[NSUUID alloc] initWithUUIDString:@"not-a-uuid"] == nil,
		      first == nil ? "no UUID" : [[first UUIDString] UTF8String]);
	}

	{
		/* THE VERSION AND VARIANT ARE A RULE, NOT RANDOM, and this measures it where a program sees
		 * it: the string's THIRD group leads with 4 (version 4, random) and its FOURTH with 8, 9, A
		 * or B (the 10xx variant), in a 36-character dash-separated form. */
		NSUUID *uuid = [NSUUID UUID];
		NSString *text = [uuid UUIDString];
		unichar version = (text != nil && [text length] == 36) ? [text characterAtIndex:14] : 0;
		unichar variant = (text != nil && [text length] == 36) ? [text characterAtIndex:19] : 0;

		check("uuid-version-and-variant",
		      version == '4' &&
		      (variant == '8' || variant == '9' || variant == 'A' || variant == 'B') &&
		      [text characterAtIndex:8] == '-' && [text characterAtIndex:13] == '-' &&
		      [text characterAtIndex:18] == '-' && [text characterAtIndex:23] == '-',
		      text == nil ? "no string"
		      : [[NSString stringWithFormat:@"len=%lu version=%C variant=%C",
			(unsigned long)[text length], version, variant] UTF8String]);
	}

	{
		/* NSDateInterval (W2h): the two constructors agree because they are ONE subtraction read in
		 * two directions, and an interval that cannot exist is REFUSED rather than stored - which is
		 * the invariant every other method reads without checking. */
		NSDate *start = [NSDate dateWithTimeIntervalSinceReferenceDate:1000.0];
		NSDate *end = [NSDate dateWithTimeIntervalSinceReferenceDate:1060.0];
		NSDateInterval *byDuration = [[NSDateInterval alloc] initWithStartDate:start duration:60.0];
		NSDateInterval *byEnd = [[NSDateInterval alloc] initWithStartDate:start endDate:end];
		BOOL refusedNegative = NO;
		BOOL refusedReversed = NO;

		@try {
			(void)[[NSDateInterval alloc] initWithStartDate:start duration:-1.0];
		} @catch (NSException *e) {
			(void)e;
			refusedNegative = YES;
		}
		@try {
			(void)[[NSDateInterval alloc] initWithStartDate:end endDate:start];
		} @catch (NSException *e) {
			(void)e;
			refusedReversed = YES;
		}
		check("date-interval",
		      byDuration != nil && byEnd != nil && [byDuration duration] == 60.0 &&
		      [[byDuration endDate] isEqualToDate:[byEnd endDate]] &&
		      [byDuration isEqualToDateInterval:byEnd] &&
		      refusedNegative && refusedReversed,
		      "the two constructors, and the invariants they refuse to break");
	}

	{
		/* INTERSECTION AND CONTAINMENT, with values that DISTINGUISH: a partial overlap, so the
		 * intersection's ends are neither input's, plus a disjoint pair answering nil. */
		NSDate *base = [NSDate dateWithTimeIntervalSinceReferenceDate:1000.0];
		NSDateInterval *first = [[NSDateInterval alloc] initWithStartDate:base duration:100.0];
		NSDateInterval *second = [[NSDateInterval alloc]
			initWithStartDate:[NSDate dateWithTimeIntervalSinceReferenceDate:1050.0] duration:100.0];
		NSDateInterval *third = [[NSDateInterval alloc]
			initWithStartDate:[NSDate dateWithTimeIntervalSinceReferenceDate:2000.0] duration:10.0];
		NSDateInterval *overlap = [first intersectionWithDateInterval:second];

		check("date-interval-intersections",
		      [first intersectsDateInterval:second] && ![first intersectsDateInterval:third] &&
		      overlap != nil && [overlap duration] == 50.0 &&
		      [[overlap startDate] isEqualToDate:[second startDate]] &&
		      [[overlap endDate] isEqualToDate:[first endDate]] &&
		      [first intersectionWithDateInterval:third] == nil &&
		      [first containsDate:base] &&
		      ![first containsDate:[NSDate dateWithTimeIntervalSinceReferenceDate:1101.0]],
		      "the intersection's ends are neither input's, and a disjoint pair answers nil");
	}

	{
		/* NSValueTransformer (W2h): the registry holds INSTANCES under names, and looking a name up
		 * FALLS BACK to the class of that name - which is why a transformer whose name matches its
		 * class needs no registration at all. The fixture's name IS its class name, so one fixture
		 * exercises both paths. */
		FnDoubler *doubler = [[FnDoubler alloc] init];

		[NSValueTransformer setValueTransformer:doubler forName:@"FnDoublerByName"];
		check("value-transformer-registry",
		      [NSValueTransformer valueTransformerForName:@"FnDoublerByName"] == doubler &&
		      [NSValueTransformer valueTransformerForName:@"FnDoubler"] != nil &&
		      [NSValueTransformer valueTransformerForName:@"NoSuchTransformerAnywhere"] == nil,
		      "an instance by name, a CLASS by its own name, and nil for a name that is neither");

		check("value-transformer-transform",
		      [[doubler transformedValue:@21] doubleValue] == 42.0 &&
		      [FnDoubler allowsReverseTransformation] &&
		      [[doubler reverseTransformedValue:@42] doubleValue] == 21.0 &&
		      [FnDoubler transformedValueClass] == [NSNumber class],
		      "forward, reverse, and the class it answers for");
	}

	{
		/* THE OPTION SETS, WHERE A COLLISION IS THE FAILURE THAT MATTERS: this header had already assigned 1
		 * and 2 inside the base64 set, so the two members added beside them had to take FREE bits - copying
		 * Apple's numbers would have made one of the four unrequestable in silence. The file-protection values
		 * are a MASK-VALUED set, so AND-ing Mask with a value must extract it. */
		check("data-option-sets-are-collision-free",
		      (NSDataBase64Encoding64CharacterLineLength & NSDataBase64EncodingEndLineWithLineFeed) == 0 &&
		      (NSDataBase64Encoding64CharacterLineLength & NSDataBase64Encoding76CharacterLineLength) == 0 &&
		      (NSDataBase64EncodingEndLineWithLineFeed & NSDataBase64EncodingEndLineWithCarriageReturn) == 0 &&
		      (NSDataBase64Encoding76CharacterLineLength &
		       NSDataBase64EncodingEndLineWithCarriageReturn) == 0 &&
		      (NSDataReadingMappedAlways & NSDataReadingMappedIfSafe) == 0 &&
		      (NSDataReadingMappedAlways & NSDataReadingUncached) == 0 &&
		      (NSDataWritingWithoutOverwriting & NSDataWritingFileProtectionMask) == 0 &&
		      (NSDataWritingFileProtectionComplete & NSDataWritingFileProtectionMask) ==
		       NSDataWritingFileProtectionComplete &&
		      NSDataWritingFileProtectionMask == 0xff,
		      "the base64 members are distinct bits, MappedAlways is its own, and the protection mask extracts a value");
	}

	printf("FOUNDATION-VALUE RESULT ok=%d fail=%d\n", okc, failc);
	/* The exit status, in the probe's OWN output. After a probe the console can stop
	 * serving INPUT for a while (the tier residual), so an `echo $?` that the harness
	 * types may never run. The probe knows exactly what it returns, so it says so - and
	 * this is the same value: failc ? 1 : 0 is the return statement below. */
	printf("FOUNDATION-VALUE-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-VALUE DONE\n");
	return failc ? 1 : 0;
}
