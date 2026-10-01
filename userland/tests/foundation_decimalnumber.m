/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_decimalnumber — NSDecimalNumber, NSDecimalNumberHandler and the NSNumber bridge (W3b).
 *
 *   decimalnumber-from-string      the documented grammar, including the LOCALE'S decimal separator
 *   decimalnumber-arithmetic       the object layer on the C surface: 0.1 + 0.2 is 0.3
 *   decimalnumber-default-behavior the documented ASYMMETRY: overflow raises, loss of precision does not
 *   decimalnumber-overflow         and the same flag turned off yields the saturated value instead
 *   decimalnumber-scale            the behaviour's SCALE, which is what the object layer adds
 *   decimalnumber-rounding         the four modes through -decimalNumberByRoundingAccordingToBehavior:
 *   decimalnumber-compare          values, including two spellings of one number, and NaN
 *   number-decimal-bridge          NSNumber's three decimal methods, both directions, and cross-class hash
 *   decimalnumber-handler          the handler's own shape, and +setDefaultBehavior: taking effect
 */

#import <Foundation/Foundation.h>
#include <stdio.h>
#include <string.h>

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-DECIMALNUMBER %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-DECIMALNUMBER %s FAIL %s\n", name,
		       detail != nil ? [detail UTF8String] : "");
	}
}

/* THE EXCEPTION A BLOCK THREW, by name, or NULL if it returned. */
static NSString * _Nullable fn_capture_name(NSDecimalNumber * (^block)(void))
{
	@try {
		block();
		return nil;
	} @catch (NSException *exception) {
		return [exception name];
	}
}

static NSDecimalNumber *fn_num(NSString *text)
{
	return [NSDecimalNumber decimalNumberWithString:text];
}

int main(void)
{
	/* 1. THE GRAMMAR: a sign, one separator, one E exponent — and THE SEPARATOR IS THE LOCALE'S. */
	{
		NSDecimalNumber *plain = fn_num(@"2500.6");
		NSDecimalNumber *negative = fn_num(@"-2500.6");
		NSDecimalNumber *scientific = fn_num(@"-2.5006e3");
		NSDecimalNumber *explicitPlus = fn_num(@"+3");
		NSDecimalNumber *french = [NSDecimalNumber decimalNumberWithString:@"2,5"
									    locale:[[NSLocale alloc] initWithLocaleIdentifier:@"fr_FR"]];
		NSDecimalNumber *refused = fn_num(@"3 apples");
		BOOL ok = [[plain stringValue] isEqualToString:@"2500.6"] &&
			  [[negative stringValue] isEqualToString:@"-2500.6"] &&
			  [[scientific stringValue] isEqualToString:@"-2500.6"] &&
			  [[explicitPlus stringValue] isEqualToString:@"3"] &&
			  [[french stringValue] isEqualToString:@"2.5"] &&
			  [refused isEqualToNumber:[NSDecimalNumber notANumber]];

		check("decimalnumber-from-string", ok,
		      [NSString stringWithFormat:@"2500.6=%@ -2500.6=%@ -2.5006e3=%@ +3=%@ fr=%@ refused=%@",
			[plain stringValue], [negative stringValue], [scientific stringValue],
			[explicitPlus stringValue], [french stringValue], [refused stringValue]]);
	}

	/* 2. THE OBJECT LAYER ON THE C SURFACE. */
	{
		NSDecimalNumber *sum = [fn_num(@"0.1") decimalNumberByAdding:fn_num(@"0.2")];
		NSDecimalNumber *product = [fn_num(@"1.1") decimalNumberByMultiplyingBy:fn_num(@"2.2")];
		NSDecimalNumber *half = [fn_num(@"2") decimalNumberByDividingBy:fn_num(@"4")];
		NSDecimalNumber *third = [fn_num(@"1") decimalNumberByDividingBy:fn_num(@"3")];

		check("decimalnumber-arithmetic",
		      [[sum stringValue] isEqualToString:@"0.3"] &&
		      [[product stringValue] isEqualToString:@"2.42"] &&
		      [[half stringValue] isEqualToString:@"0.5"] &&
		      [[third stringValue] hasPrefix:@"0.3333333333333"],
		      [NSString stringWithFormat:@"0.1+0.2=%@ 1.1*2.2=%@ 2/4=%@ 1/3=%@",
			[sum stringValue], [product stringValue], [half stringValue], [third stringValue]]);
	}

	/* 3. THE DOCUMENTED ASYMMETRY. The default behaviour raises on overflow, underflow and divide-by-zero
	 * but NOT on loss of precision — so a sum that cannot hold its last digit returns the rounded value
	 * while a division by zero throws. Both halves are asserted here, because only the pair is the rule. */
	{
		__block NSDecimalNumber *rounded = nil;
		NSString * _Nullable divideByZero = fn_capture_name(^NSDecimalNumber *{
			return [fn_num(@"1") decimalNumberByDividingBy:[NSDecimalNumber zero]];
		});
		NSString * _Nullable inexact = fn_capture_name(^NSDecimalNumber *{
			rounded = [[NSDecimalNumber maximumDecimalNumber]
				   decimalNumberByAdding:[NSDecimalNumber one]];
			return rounded;
		});

		check("decimalnumber-default-behavior",
		      divideByZero != nil &&
		      [divideByZero isEqualToString:NSDecimalNumberDivideByZeroException] &&
		      inexact == nil &&
		      [rounded isEqualToNumber:[NSDecimalNumber maximumDecimalNumber]],
		      [NSString stringWithFormat:@"1/0 threw %@; max+1 threw %@ and gives %.40@",
			divideByZero ? divideByZero : @"nothing", inexact ? inexact : @"nothing",
			rounded ? [rounded stringValue] : @"nil"]);
	}

	/* 4. OVERFLOW: with the flag on the handler throws, with it off the saturated value stands. */
	{
		id <NSDecimalNumberBehaviors> raising =
			[NSDecimalNumberHandler decimalNumberHandlerWithRoundingMode:NSRoundPlain
									      scale:NSDecimalNoScale
								   raiseOnExactness:NO
								    raiseOnOverflow:YES
								   raiseOnUnderflow:NO
								  raiseOnDivideByZero:NO];
		id <NSDecimalNumberBehaviors> quiet =
			[NSDecimalNumberHandler decimalNumberHandlerWithRoundingMode:NSRoundPlain
									      scale:NSDecimalNoScale
								   raiseOnExactness:NO
								    raiseOnOverflow:NO
								   raiseOnUnderflow:NO
								  raiseOnDivideByZero:NO];
		NSString * _Nullable thrown = fn_capture_name(^NSDecimalNumber *{
			return [fn_num(@"1") decimalNumberByMultiplyingByPowerOf10:200 withBehavior:raising];
		});
		NSDecimalNumber *saturated = [fn_num(@"1") decimalNumberByMultiplyingByPowerOf10:200
									 withBehavior:quiet];

		check("decimalnumber-overflow",
		      thrown != nil && [thrown isEqualToString:NSDecimalNumberOverflowException] &&
		      [saturated isEqualToNumber:[NSDecimalNumber maximumDecimalNumber]],
		      [NSString stringWithFormat:@"raised %@; quietly gives %.40@",
			thrown ? thrown : @"nothing", [saturated stringValue]]);
	}

	/* 5. THE SCALE: the behaviour's number of digits after the point, applied to the result. */
	{
		id <NSDecimalNumberBehaviors> twoPlaces =
			[NSDecimalNumberHandler decimalNumberHandlerWithRoundingMode:NSRoundPlain
									      scale:2
								   raiseOnExactness:NO
								    raiseOnOverflow:NO
								   raiseOnUnderflow:NO
								  raiseOnDivideByZero:NO];
		id <NSDecimalNumberBehaviors> twoDown =
			[NSDecimalNumberHandler decimalNumberHandlerWithRoundingMode:NSRoundDown
									      scale:2
								   raiseOnExactness:NO
								    raiseOnOverflow:NO
								   raiseOnUnderflow:NO
								  raiseOnDivideByZero:NO];
		NSDecimalNumber *oneThird = [fn_num(@"1") decimalNumberByDividingBy:fn_num(@"3")
								       withBehavior:twoPlaces];
		NSDecimalNumber *deep = [fn_num(@"2.345") decimalNumberByAdding:fn_num(@"0")
								   withBehavior:twoDown];

		check("decimalnumber-scale",
		      [[oneThird stringValue] isEqualToString:@"0.33"] &&
		      [[deep stringValue] isEqualToString:@"2.34"],
		      [NSString stringWithFormat:@"1/3 at scale 2=%@ (Plain); 2.345 at scale 2=%@ (Down)",
			[oneThird stringValue], [deep stringValue]]);
	}

	/* 6. THE FOUR MODES, through the object layer, over the same table the C probe asserts. */
	{
		const char *expected[4] = { "1.3", "1.2", "1.3", "1.2" };
		NSRoundingMode modes[4] = { NSRoundPlain, NSRoundDown, NSRoundUp, NSRoundBankers };
		int i;
		BOOL ok = YES;
		NSString *values[4] = { @"", @"", @"", @"" };

		for (i = 0; i < 4; i++) {
			id <NSDecimalNumberBehaviors> behavior =
				[NSDecimalNumberHandler decimalNumberHandlerWithRoundingMode:modes[i]
										      scale:1
									   raiseOnExactness:NO
									    raiseOnOverflow:NO
									   raiseOnUnderflow:NO
									  raiseOnDivideByZero:NO];
			NSDecimalNumber *rounded = [fn_num(@"1.25")
						    decimalNumberByRoundingAccordingToBehavior:behavior];

			/* COMPARED AS BYTES, not through +stringWithUTF8String:, whose return OUR header declares
			 * `id _Nullable` — passing that into -isEqualToString: is a nullable-to-nonnull conversion,
			 * which the GUEST build flags as an error while the host's laxer flags do not. (The guest is
			 * the stricter compiler in this project, and this probe learned that twice.) */
			const char *actual = [[rounded stringValue] UTF8String];

			ok = ok && actual != NULL && strcmp(actual, expected[i]) == 0;
			values[i] = [rounded stringValue];
		}
		check("decimalnumber-rounding", ok,
		      [NSString stringWithFormat:@"1.25 at scale 1 = %@,%@,%@,%@",
			values[0], values[1], values[2], values[3]]);
	}

	/* 7. VALUES, INCLUDING TWO SPELLINGS OF ONE NUMBER AND NaN. */
	{
		BOOL same = [fn_num(@"1.2")	isEqualToNumber:fn_num(@"1.20")];
		NSComparisonResult less = [fn_num(@"1.2") compare:fn_num(@"1.21")];
		NSDecimalNumber *nan = [NSDecimalNumber notANumber];

		check("decimalnumber-compare",
		      same && less == NSOrderedAscending &&
		      [nan compare:fn_num(@"1")] == NSOrderedSame &&	/* NaN compares as equal, as the C layer does */
		      [[nan description] isEqualToString:@"NaN"],
		      [NSString stringWithFormat:@"1.2==1.20 %d; 1.2<1.21 %d; NaN=%@",
			(int)same, (int)less, [nan description]]);
	}

	/* 8. THE BRIDGE IN BOTH DIRECTIONS. A double converts through its SHORTEST ROUND-TRIPPING decimal form,
	 * which is the whole reason 0.1 comes back as 0.1 and not as its full binary expansion — and the hash
	 * has to agree with equality ACROSS the class boundary. */
	{
		NSDecimalNumber *fromDecimal = [NSDecimalNumber decimalNumberWithDecimal:fn_num(@"1.25").decimalValue];
		NSNumber *doubleHalf = [NSNumber numberWithDouble:1.5];
		NSNumber *doubleTenth = [NSNumber numberWithDouble:0.1];
		NSNumber *bigInteger = [NSNumber numberWithLongLong:9007199254740993LL];
		NSDecimal halfDecimal = [doubleHalf decimalValue];
		NSDecimal tenthDecimal = [doubleTenth decimalValue];
		NSDecimal integerDecimal = [bigInteger decimalValue];
		NSDecimalNumber *half = [NSDecimalNumber decimalNumberWithDecimal:halfDecimal];
		NSDecimalNumber *tenth = [NSDecimalNumber decimalNumberWithDecimal:tenthDecimal];
		NSDecimalNumber *integer = [NSDecimalNumber decimalNumberWithDecimal:integerDecimal];

		check("number-decimal-bridge",
		      [fromDecimal isKindOfClass:[NSDecimalNumber class]] &&
		      [[half stringValue] isEqualToString:@"1.5"] &&
		      [[tenth stringValue] isEqualToString:@"0.1"] &&
		      [[integer stringValue] isEqualToString:@"9007199254740993"] &&
		      [doubleHalf isEqualToNumber:half] && [half isEqualToNumber:doubleHalf] &&
		      [doubleHalf hash] == [half hash] &&
		      [bigInteger hash] == [integer hash],
		      [NSString stringWithFormat:@"1.5->%@ 0.1->%@ 2^53+1->%@ hashes %d/%d",
			[half stringValue], [tenth stringValue], [integer stringValue],
			(int)([doubleHalf hash] == [half hash]), (int)([bigInteger hash] == [integer hash])]);
	}

	/* 9. THE HANDLER'S SHAPE, AND +setDefaultBehavior: TAKING EFFECT. */
	{
		NSDecimalNumberHandler *defaultHandler = [NSDecimalNumberHandler defaultDecimalNumberHandler];
		id <NSDecimalNumberBehaviors> previous = [NSDecimalNumber defaultBehavior];
		id <NSDecimalNumberBehaviors> zeroScale =
			[NSDecimalNumberHandler decimalNumberHandlerWithRoundingMode:NSRoundPlain
									      scale:0
								   raiseOnExactness:NO
								    raiseOnOverflow:NO
								   raiseOnUnderflow:NO
								  raiseOnDivideByZero:NO];
		NSDecimalNumber *scaled;

		[NSDecimalNumber setDefaultBehavior:zeroScale];
		scaled = [fn_num(@"1.5") decimalNumberByAdding:fn_num(@"1.5")];
		[NSDecimalNumber setDefaultBehavior:previous];
		{
			NSDecimalNumber *restored = [fn_num(@"1.5") decimalNumberByAdding:fn_num(@"1.5")];

			check("decimalnumber-handler",
			      [defaultHandler roundingMode] == NSRoundPlain &&
			      [defaultHandler scale] == NSDecimalNoScale &&
			      [[scaled stringValue] isEqualToString:@"3"] &&
			      [[restored stringValue] isEqualToString:@"3"],
			      [NSString stringWithFormat:@"default mode=%d scale=%d; scale 0 sum=%@; restored sum=%@",
				(int)[defaultHandler roundingMode], (int)[defaultHandler scale],
				[scaled stringValue], [restored stringValue]]);
		}
	}

	printf("FOUNDATION-DECIMALNUMBER RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-DECIMALNUMBER-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-DECIMALNUMBER DONE\n");
	return failc ? 1 : 0;
}
