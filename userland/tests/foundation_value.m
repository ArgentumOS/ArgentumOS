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

	printf("FOUNDATION-VALUE RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-VALUE DONE\n");
	return failc ? 1 : 0;
}
