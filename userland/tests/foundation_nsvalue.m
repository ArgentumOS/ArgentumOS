/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_nsvalue, unit of 1 — F13.8c's acceptance for NSValue and NSNull.
 * docs/design/foundation-plan.md §10.
 *
 * THE NAME IS `nsvalue` AND NOT `value` because `foundation_value` ALREADY EXISTS: it is F2/F8's
 * probe for NSNumber, NSData and NSDate — "values" in the older sense — and it is tracked, with a
 * header and a support unit. A new probe's name has to be checked against the suite before it is
 * written, which is cheap here and would not have been cheap in the tree.
 *
 * ONE unit, like the set probe: the claim is not a cross-translation-unit boundary but what a BOX
 * does with bytes, and it imports only <foundation/Foundation.h> (which also proves the umbrella
 * exports both headers).
 *
 * THE MEASUREMENT THAT MATTERS IS `value-copies-exactly-its-size`, and it is a CANARY rather than a
 * field comparison: the source buffer is 64 bytes with a real structure at the front and 0xAA after
 * it, the destination is 64 bytes of 0x55, and the assertion is that the structure arrives AND the
 * bytes past it are still 0x55. A box that copied too much would smear 0xAA; one that copied too
 * little would leave 0x55 inside the structure. A plain round trip cannot tell either apart,
 * because `-getValue:` copies back whatever length it was told.
 */

#import <foundation/Foundation.h>

#include <stdio.h>
#include <string.h>

/* A structure with PADDING, which is the point: {double,int} is 16 bytes and not 12, so a size
 * derived by adding field widths would be wrong by four. */
struct FNVMeasure {
	double amount;
	int tag;
};

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-NSVALUE %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-NSVALUE %s FAIL %s\n", name,
		       detail != nil ? [detail UTF8String] : "");
	}
}

int main(void)
{
	{
		struct FNVMeasure mine;
		struct FNVMeasure back;
		struct FNVMeasure different;
		NSValue *boxed;
		NSValue *other;

		memset(&mine, 0, sizeof(mine));
		mine.amount = 2.5;
		mine.tag = 41;
		boxed = [NSValue valueWithBytes:&mine objCType:@encode(struct FNVMeasure)];
		other = [NSValue valueWithBytes:&mine objCType:@encode(struct FNVMeasure)];
		memset(&back, 0, sizeof(back));
		if (boxed != nil) {
			[boxed getValue:&back];
		}
		different = mine;
		different.tag = 42;
		check("value-bytes-roundtrip",
		      boxed != nil && back.amount == 2.5 && back.tag == 41 &&
		      strcmp([boxed objCType], @encode(struct FNVMeasure)) == 0 &&
		      [boxed isEqualToValue:other] && [boxed isEqual:other] &&
		      [boxed hash] == [other hash] &&
		      ![boxed isEqualToValue:[NSValue valueWithBytes:&different
							    objCType:@encode(struct FNVMeasure)]],
		      [NSString stringWithFormat:@"amount=%g tag=%d equal=%d",
			back.amount, back.tag, (int)[boxed isEqualToValue:other]]);
	}

	{
		/* THE CANARY. Source: the structure, then 0xAA. Destination: 0x55 throughout. */
		unsigned char source[64];
		unsigned char destination[64];
		struct FNVMeasure mine;
		NSUInteger i;
		BOOL tail_untouched = YES;
		BOOL head_arrived;

		mine.amount = 1.25;
		mine.tag = 7;
		memset(source, 0xAA, sizeof(source));
		memcpy(source, &mine, sizeof(mine));
		memset(destination, 0x55, sizeof(destination));
		{
			NSValue *boxed = [NSValue valueWithBytes:source
							objCType:@encode(struct FNVMeasure)];

			[boxed getValue:destination];
		}
		head_arrived = memcmp(destination, &mine, sizeof(mine)) == 0;
		for (i = sizeof(mine); i < sizeof(destination); i++) {
			if (destination[i] != 0x55) {
				tail_untouched = NO;
				break;
			}
		}
		check("value-copies-exactly-its-size",
		      head_arrived && tail_untouched && sizeof(mine) == 16,
		      [NSString stringWithFormat:@"head=%d tail=%d sizeof=%lu firstTail=0x%02x",
			(int)head_arrived, (int)tail_untouched, (unsigned long)sizeof(mine),
			(unsigned)destination[sizeof(mine)]]);
	}

	{
		int local = 0;
		NSValue *boxed = [NSValue valueWithPointer:&local];

		check("value-pointer",
		      boxed != nil && [boxed pointerValue] == &local,
		      [NSString stringWithFormat:@"ptr=%p want=%p",
			[boxed pointerValue], (void *)&local]);
	}

	{
		NSRange range = NSMakeRange(2, 5);
		NSValue *boxed = [NSValue valueWithRange:range];
		NSRange back = NSMakeRange(0, 0);

		if (boxed != nil) {
			back = [boxed rangeValue];
		}
		check("value-range",
		      boxed != nil && back.location == 2 && back.length == 5 &&
		      [[boxed description] isEqualToString:@"NSRange: {2, 5}"],
		      [NSString stringWithFormat:@"got={%lu, %lu} text=%@",
			(unsigned long)back.location, (unsigned long)back.length,
			boxed != nil ? [boxed description] : @"?"]);
	}

	{
		/* A BOX IS COMPARED BY ITS BYTES, so a CONTAINER built on -hash/-isEqual: must collapse two
		 * equal boxes — which is exactly what NSSet does. */
		int ten = 10;
		int alsoTen = 10;
		const id boxes[2] = { [NSValue valueWithBytes:&ten objCType:@encode(int)],
				      [NSValue valueWithBytes:&alsoTen objCType:@encode(int)] };
		id one = boxes[0];
		id two = boxes[1];
		NSSet *set = [NSSet setWithObjects:boxes count:2];

		check("value-in-a-container",
		      one != two && [one isEqualToValue:two] && set != nil &&
		      [set count] == 1 && [set containsObject:two] && [one copy] == one,
		      [NSString stringWithFormat:@"count=%lu copyIsSelf=%d",
			(unsigned long)(set != nil ? [set count] : 0), (int)([one copy] == one)]);
	}

	{
		NSNull *one = [NSNull null];
		NSNull *two = [NSNull null];
		NSNull *made = [[NSNull alloc] init];

		check("null-is-one-object",
		      one == two && made == one && [one isEqual:two] && [one hash] == [two hash],
		      [NSString stringWithFormat:@"same=%d allocIsNull=%d",
			(int)(one == two), (int)(made == one)]);
	}

	{
		id placeholder = [NSNull null];
		NSArray *array = @[@"a", placeholder, @"c"];
		NSDictionary *dictionary = @{ placeholder : @"the hole" };

		check("null-holds-a-place-in-a-collection",
		      array != nil && [array count] == 3 &&
		      [[array objectAtIndex:1] isEqual:placeholder] &&
		      [[array objectAtIndex:1] isEqual:[NSNull null]] &&
		      dictionary != nil && [dictionary count] == 1 &&
		      [[dictionary objectForKey:[NSNull null]] isEqualToString:@"the hole"] &&
		      ![placeholder isEqual:@"a"] &&
		      [[placeholder description] isEqualToString:@"<null>"],
		      [NSString stringWithFormat:@"array=%lu dict=%lu text=%@",
			(unsigned long)(array != nil ? [array count] : 0),
			(unsigned long)(dictionary != nil ? [dictionary count] : 0),
			[placeholder description]]);
	}

	printf("FOUNDATION-NSVALUE RESULT ok=%d fail=%d\n", okc, failc);
	/* The exit status, in the probe's OWN output. After a probe the console can stop
	 * serving INPUT for a while (the tier residual), so an `echo $?` that the harness
	 * types may never run. The probe knows exactly what it returns, so it says so - and
	 * this is the same value: failc ? 1 : 0 is the return statement below. */
	printf("FOUNDATION-NSVALUE-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-NSVALUE DONE\n");
	return failc ? 1 : 0;
}
