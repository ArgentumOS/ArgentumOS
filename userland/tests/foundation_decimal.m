/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_decimal — the NSDecimal C surface (W3).
 *
 *   decimal-round-modes   Apple's four modes over the table its documentation gives, derived from the
 *                         DEFINITIONS the same page publishes (Plain: nearest, ties away from zero;
 *                         Down: toward zero; Up: away from zero; Bankers: nearest, ties to EVEN)
 *   decimal-arithmetic    the reason this type exists: 0.1 + 0.2 is 0.3, which no binary float says
 *   decimal-division      2/4 exact, 1/3 to 38 digits with the LOSS OF PRECISION reported, 1/0 refused
 *   decimal-compare       equal values in different representations compare EQUAL; NaN propagates
 *   decimal-strings       the printed form, including leading zeros and a negative fraction
 *   decimal-alias-power   the documented aliasing (result may be an operand) and NSDecimalPower
 *   decimal-limits        overflow and underflow at the model's exponent bounds
 */

#import <Foundation/Foundation.h>
#include <stdio.h>
#include <string.h>

static int okc, failc;

static void check(const char *name, int ok, const char *detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-DECIMAL %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-DECIMAL %s FAIL %s\n", name, detail ? detail : "");
	}
}

/* A DECIMAL FROM DIGITS, for a probe that has no NSDecimalNumber yet: the struct is OURS (the header
 * says why), so the probe may fill it directly — that is also what makes the checks independent of the
 * arithmetic they are checking. `digits` is most-significant first, as a human writes it. */
static NSDecimal fn_dec(const char *digits, int exponent, int negative)
{
	NSDecimal d;
	size_t n = strlen(digits);
	size_t i;

	memset(&d, 0, sizeof d);
	d._exponent = (signed char)exponent;
	d._length = (unsigned char)n;
	d._isNegative = (unsigned char)(negative ? 1 : 0);
	for (i = 0; i < n; i++) {
		d._digits[n - 1 - i] = (unsigned char)(digits[i] - '0');
	}
	return d;
}

/* ROTATING BUFFERS, and this is not a detail: one static buffer makes every argument of one formatted
 * message the SAME string, since the arguments are all evaluated before the format runs. The first run
 * of this probe reported "2.5+2.5=2000 2^10=2000 2e3=2000" for exactly that reason. */
static const char *fn_show(NSDecimal d)
{
	static char text[6][320];	/* NSDecimalMax is 128 digits long before the point */
	static int slot;

	slot = (slot + 1) % 6;
	snprintf(text[slot], sizeof text[slot], "%s", [NSDecimalString(&d, nil) UTF8String]);
	return text[slot];
}

int main(void)
{
	/* 1. THE ROUNDING TABLE, at scale 1 (one digit after the point). */
	{
		struct { const char *in; int negative; const char *plain, *down, *up, *bankers; } rows[] = {
			{ "124", 0, "1.2", "1.2", "1.3", "1.2" },
			{ "126", 0, "1.3", "1.2", "1.3", "1.3" },
			{ "125", 0, "1.3", "1.2", "1.3", "1.2" },
			{ "135", 0, "1.4", "1.3", "1.4", "1.4" },
			{ "135", 1, "-1.4", "-1.3", "-1.4", "-1.4" },
			{ NULL, 0, NULL, NULL, NULL, NULL }
		};
		int i;
		int all = 1;

		for (i = 0; rows[i].in != NULL; i++) {
			NSDecimal in = fn_dec(rows[i].in, -2, rows[i].negative);
			NSDecimal out;

			NSDecimalRound(&out, &in, 1, NSRoundPlain);
			all = all && strcmp(fn_show(out), rows[i].plain) == 0;
			NSDecimalRound(&out, &in, 1, NSRoundDown);
			all = all && strcmp(fn_show(out), rows[i].down) == 0;
			NSDecimalRound(&out, &in, 1, NSRoundUp);
			all = all && strcmp(fn_show(out), rows[i].up) == 0;
			NSDecimalRound(&out, &in, 1, NSRoundBankers);
			all = all && strcmp(fn_show(out), rows[i].bankers) == 0;
		}
		check("decimal-round-modes", all,
		      "the four modes over 1.24, 1.26, 1.25, 1.35 and -1.35 at scale 1");
	}

	/* 2. ARITHMETIC, and the one a binary float cannot do. */
	{
		NSDecimal a = fn_dec("1", -1, 0);	/* 0.1 */
		NSDecimal b = fn_dec("2", -1, 0);	/* 0.2 */
		NSDecimal sum;
		NSDecimal one = fn_dec("11", -1, 0);	/* 1.1 */
		NSDecimal two = fn_dec("22", -1, 0);	/* 2.2 */
		NSDecimal s2;
		NSDecimal prod;

		NSDecimalAdd(&sum, &a, &b, NSRoundPlain);
		NSDecimalAdd(&s2, &one, &two, NSRoundPlain);
		NSDecimalMultiply(&prod, &one, &two, NSRoundPlain);
		check("decimal-arithmetic",
		      strcmp(fn_show(sum), "0.3") == 0 &&
		      strcmp(fn_show(s2), "3.3") == 0 &&
		      strcmp(fn_show(prod), "2.42") == 0,
		      [[NSString stringWithFormat:@"0.1+0.2=%s 1.1+2.2=%s 1.1*2.2=%s",
			fn_show(sum), fn_show(s2), fn_show(prod)] UTF8String]);
	}

	/* 3. DIVISION: the exact case, the inexact case with its ERROR, and the refusal. */
	{
		NSDecimal two = fn_dec("2", 0, 0);
		NSDecimal four = fn_dec("4", 0, 0);
		NSDecimal one = fn_dec("1", 0, 0);
		NSDecimal three = fn_dec("3", 0, 0);
		NSDecimal zero = fn_dec("0", 0, 0);
		NSDecimal out;
		NSCalculationError exact = NSDecimalDivide(&out, &two, &four, NSRoundPlain);
		char exactText[32];
		NSCalculationError inexact;
		NSCalculationError refusal;

		snprintf(exactText, sizeof exactText, "%s", fn_show(out));
		inexact = NSDecimalDivide(&out, &one, &three, NSRoundPlain);
		{
			static char third[96];

			snprintf(third, sizeof third, "%s", fn_show(out));
			refusal = NSDecimalDivide(&out, &one, &zero, NSRoundPlain);
			check("decimal-division",
			      exact == NSCalculationNoError && strcmp(exactText, "0.5") == 0 &&
			      inexact == NSCalculationLossOfPrecision &&
			      strncmp(third, "0.3333333333333333333", 21) == 0 &&
			      refusal == NSCalculationDivideByZero && NSDecimalIsNotANumber(&out),
			      [[NSString stringWithFormat:@"2/4=%s (%d) 1/3=%s (%d) 1/0=%d",
				exactText, (int)exact, third, (int)inexact, (int)refusal] UTF8String]);
		}
	}

	/* 4. COMPARING, including two representations of one value — which is the thing a decimal has to get
	 * right and a mantissa comparison does not. */
	{
		NSDecimal a = fn_dec("12", -1, 0);	/* 1.2 */
		NSDecimal b = fn_dec("120", -2, 0);	/* 1.20 */
		NSDecimal c = fn_dec("121", -2, 0);	/* 1.21 */
		NSDecimal neg = fn_dec("12", -1, 1);	/* -1.2 */
		NSDecimal nan;
		NSDecimal one = fn_dec("1", 0, 0);
		NSDecimal propagated;

		{
			NSDecimal zero = fn_dec("0", 0, 0);

			NSDecimalDivide(&nan, &one, &zero, NSRoundPlain);
		}
		NSDecimalAdd(&propagated, &nan, &one, NSRoundPlain);
		check("decimal-compare",
		      NSDecimalCompare(&a, &b) == NSOrderedSame &&
		      NSDecimalCompare(&a, &c) == NSOrderedAscending &&
		      NSDecimalCompare(&neg, &a) == NSOrderedAscending &&
		      NSDecimalCompare(&c, &a) == NSOrderedDescending &&
		      NSDecimalIsNotANumber(&nan) && NSDecimalIsNotANumber(&propagated),
		      [[NSString stringWithFormat:@"1.2 vs 1.20=%ld 1.2 vs 1.21=%ld -1.2 vs 1.2=%ld nan=%d",
			(long)NSDecimalCompare(&a, &b), (long)NSDecimalCompare(&a, &c),
			(long)NSDecimalCompare(&neg, &a), (int)NSDecimalIsNotANumber(&nan)] UTF8String]);
	}

	/* 5. THE PRINTED FORM. */
	{
		NSDecimal small = fn_dec("1", -3, 1);	/* -0.001 */
		NSDecimal thousand = fn_dec("1", 3, 0);	/* 1000 */
		NSDecimal zero = fn_dec("0", 0, 0);
		NSDecimal frac = fn_dec("15", -1, 0);	/* 1.5 */

		check("decimal-strings",
		      strcmp(fn_show(small), "-0.001") == 0 &&
		      strcmp(fn_show(thousand), "1000") == 0 &&
		      strcmp(fn_show(zero), "0") == 0 &&
		      strcmp(fn_show(frac), "1.5") == 0,
		      [[NSString stringWithFormat:@"-0.001=%s 1000=%s 0=%s 1.5=%s",
			fn_show(small), fn_show(thousand), fn_show(zero), fn_show(frac)] UTF8String]);
	}

	/* 6. ALIASING (Apple's own example puts the result on an operand) AND THE POWER. */
	{
		NSDecimal value = fn_dec("25", -1, 0);	/* 2.5 */
		NSDecimal addend = fn_dec("25", -1, 0);	/* 2.5 */
		NSDecimal base = fn_dec("2", 0, 0);
		NSDecimal power;
		NSDecimal moved;

		NSDecimalAdd(&value, &value, &addend, NSRoundPlain);	/* 5.0, aliased */
		NSDecimalPower(&power, &base, 10, NSRoundPlain);	/* 1024 */
		NSDecimalMultiplyByPowerOf10(&moved, &base, 3, NSRoundPlain);	/* 2000 */
		check("decimal-alias-power",
		      strcmp(fn_show(value), "5") == 0 &&
		      strcmp(fn_show(power), "1024") == 0 &&
		      strcmp(fn_show(moved), "2000") == 0,
		      [[NSString stringWithFormat:@"2.5+2.5=%s 2^10=%s 2e3=%s",
			fn_show(value), fn_show(power), fn_show(moved)] UTF8String]);
	}

	/* 7. THE MODEL'S LIMITS, which are part of the contract rather than a failure of it. */
	{
		NSDecimal big = NSDecimalMax;
		NSDecimal out;
		/* NSDecimalMax + itself is NOT an overflow: the bounds are the exponent's and max's exponent is
		 * 90, so what it costs is PRECISION (it rounds to 38 digits). Overflow needs the exponent itself
		 * out of range, which is what the two calls below drive. */
		NSCalculationError sumOfMax = NSDecimalAdd(&out, &big, &big, NSRoundPlain);

		/* THE SHAPE OF THE CONSTANTS AND OF THEIR SUM IS WORTH ASSERTING, and here is why: the first
		 * version of NSDecimalMax held THIRTY-SEVEN nines while claiming a length of 38, so its top
		 * digit was zero, it was not the maximum, and max + max produced a 38-digit mantissa that never
		 * needed rounding — an arithmetic error hidden inside a constant, invisible to every value-based
		 * check. A correct maximum has 38 nines, and its sum with itself needs 39 digits, of which 38
		 * survive (top 9) and the exponent stays 90. */
		check("decimal-limits-shape",
		      big._length == 38 && big._digits[0] == 9 && big._digits[37] == 9 &&
		      (int)big._exponent == NSDecimalMaxExponent - (NSDecimalMaxDigits - 1) &&
		      out._length == 38 && out._digits[37] == 9 && (int)out._exponent == 90,
		      [[NSString stringWithFormat:@"max len=%d top=%d exp=%d; sum len=%d top=%d exp=%d",
			(int)big._length, (int)big._digits[37], (int)big._exponent,
			(int)out._length, (int)out._digits[37], (int)out._exponent] UTF8String]);
		NSCalculationError overflow = NSDecimalMultiplyByPowerOf10(&out, &big, 100, NSRoundPlain);
		NSDecimal one = fn_dec("1", 0, 0);
		NSCalculationError under = NSDecimalMultiplyByPowerOf10(&out, &one, -200, NSRoundPlain);

		check("decimal-limits",
		      sumOfMax == NSCalculationLossOfPrecision &&
		      overflow == NSCalculationOverflow && under == NSCalculationUnderflow,
		      [[NSString stringWithFormat:@"max+max=%d max*10^100=%d 1e-200=%d",
			(int)sumOfMax, (int)overflow, (int)under] UTF8String]);
	}

	printf("FOUNDATION-DECIMAL RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-DECIMAL-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-DECIMAL DONE\n");
	return failc ? 1 : 0;
}
