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
			/* NSDecimalNumber / NSDecimal are not shipped. */
			"decimalValue",			/* the NSDecimal conversion */
			"numberWithDecimal:",		/* the NSDecimal constructor */
			"initWithDecimal:",		/* the NSDecimal initialiser */
			/* NSValue is not shipped, so the API NSNumber inherits from it in
			 * Cocoa has no home here. */
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
		/* THE HARD RULE, mechanically, for NSData/NSMutableData. */
		static const char *classSelectors[] = {
			"data", "dataWithBytes:length:", "dataWithBytesNoCopy:length:",
			"dataWithBytesNoCopy:length:freeWhenDone:", "dataWithData:",
			"dataWithContentsOfFile:", "dataWithBase64EncodedString:", NULL
		};
		static const char *instanceSelectors[] = {
			"initWithBytes:length:", "initWithBytesNoCopy:length:",
			"initWithBytesNoCopy:length:freeWhenDone:", "initWithData:",
			"initWithContentsOfFile:", "initWithBase64EncodedString:options:",
			"length", "bytes", "getBytes:length:", "getBytes:range:",
			"subdataWithRange:", "rangeOfData:options:range:",
			"base64EncodedStringWithOptions:", "writeToFile:atomically:",
			"isEqualToData:", "isEqual:", "hash", "description",
			"copy", "mutableCopy", NULL
		};
		static const char *mutableClassSelectors[] = {
			"dataWithCapacity:", "dataWithLength:", NULL
		};
		static const char *mutableSelectors[] = {
			"initWithCapacity:", "initWithLength:", "appendBytes:length:",
			"appendData:", "setLength:", "increaseLengthBy:", "mutableBytes",
			"replaceBytesInRange:withBytes:", "replaceBytesInRange:withBytes:length:",
			"resetBytesInRange:", NULL
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
		check("data-api-complete", complete,
		      "every public NSData/NSMutableData selector exists (the hard rule)");
	}

	{
		/* THE HARD RULE for NSDate. */
		static const char *classSelectors[] = {
			"date", "dateWithTimeIntervalSince1970:", "dateWithTimeIntervalSinceNow:",
			"dateWithTimeInterval:sinceDate:", "distantPast", "distantFuture",
			"timeIntervalSinceReferenceDate", NULL
		};
		static const char *instanceSelectors[] = {
			"initWithTimeIntervalSince1970:", "initWithTimeIntervalSinceNow:",
			"initWithTimeInterval:sinceDate:",
			"timeIntervalSince1970", "timeIntervalSinceNow",
			"timeIntervalSinceReferenceDate", "timeIntervalSinceDate:",
			"dateByAddingTimeInterval:", "descriptionWithLocale:",
			"isEqualToDate:", "compare:", "earlierDate:", "laterDate:",
			"isEqual:", "hash", "description", "copy", "mutableCopy", NULL
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
		check("date-api-complete", complete,
		      "every public NSDate selector exists (the hard rule)");
	}

	{
		/* NSData's added surface. */
		static const unsigned char bytes[] = { 0xde, 0xad, 0xbe, 0xef };
		NSData *d = [NSData dataWithBytes:bytes length:4];
		NSData *sub = [d subdataWithRange:NSMakeRange(1, 2)];
		NSData *searched = [d rangeOfData:nil options:NSDataSearchDefault
					    range:NSMakeRange(0, 4)].location == NSNotFound ? nil : d;
		unsigned char held[3];
		NSMutableData *m = [NSMutableData dataWithLength:4];
		NSData *needle = [NSData dataWithBytes:bytes length:4];
		NSRange found;

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
		      [d rangeOfData:[NSData dataWithBytes:"zz" length:2]
			     options:NSDataSearchDefault
			       range:NSMakeRange(0, 4)].location == NSNotFound &&
		      [[NSData dataWithBytes:bytes length:4]
		          isEqualToData:[NSData dataWithBytes:bytes length:4]] &&
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

	printf("FOUNDATION-VALUE RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-VALUE DONE\n");
	return failc ? 1 : 0;
}
