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
		/* THE HARD RULE, mechanically: every public NSNumber selector must EXIST.
		 * This list IS the audit (docs/design/foundation-plan.md), turned into
		 * something that fails a gate instead of living in prose. */
		static const char *classSelectors[] = {
			"numberWithBool:", "numberWithChar:", "numberWithShort:",
			"numberWithInt:", "numberWithLong:", "numberWithLongLong:",
			"numberWithInteger:", "numberWithUnsignedChar:",
			"numberWithUnsignedShort:", "numberWithUnsignedInt:",
			"numberWithUnsignedLong:", "numberWithUnsignedLongLong:",
			"numberWithUnsignedInteger:", "numberWithFloat:",
			"numberWithDouble:", NULL
		};
		static const char *instanceSelectors[] = {
			"initWithBool:", "initWithChar:", "initWithShort:", "initWithInt:",
			"initWithLong:", "initWithLongLong:", "initWithInteger:",
			"initWithUnsignedChar:", "initWithUnsignedShort:",
			"initWithUnsignedInt:", "initWithUnsignedLong:",
			"initWithUnsignedLongLong:", "initWithUnsignedInteger:",
			"initWithFloat:", "initWithDouble:",
			"boolValue", "charValue", "shortValue", "intValue", "longValue",
			"longLongValue", "integerValue", "unsignedCharValue",
			"unsignedShortValue", "unsignedIntValue", "unsignedLongValue",
			"unsignedLongLongValue", "unsignedIntegerValue", "floatValue",
			"doubleValue", "stringValue", "objCType", "descriptionWithLocale:",
			"isEqualToNumber:", "compare:", "isEqual:", "hash", "description",
			"copy", "mutableCopy", NULL
		};
		NSNumber *n = [NSNumber numberWithInt:42];
		int complete = 1;
		int i;

		for (i = 0; classSelectors[i] != NULL; i++) {
			if (![NSNumber respondsToSelector:sel_registerName(classSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-VALUE missing +%s\n", classSelectors[i]);
			}
		}
		for (i = 0; instanceSelectors[i] != NULL; i++) {
			if (![n respondsToSelector:sel_registerName(instanceSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-VALUE missing -%s\n", instanceSelectors[i]);
			}
		}
		check("number-api-complete", complete,
		      "every public NSNumber selector exists (the hard rule)");
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

	printf("FOUNDATION-VALUE RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-VALUE DONE\n");
	return failc ? 1 : 0;
}
