/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/* NSDecimal.m — the arithmetic (W3). The LAYOUT is ours (the header says why); the value model,
 * signatures, rounding modes and error codes are Apple's. Digits are LSD-first, one per byte. */

#import <Foundation/NSDecimal.h>
#import <Foundation/NSString.h>


#define FN_DIGITS	NSDecimalMaxDigits

/* ---- digit helpers, LSD-first ------------------------------------------------------------------- */

static int fn_strip(const unsigned char *d, int n)
{
	while (n > 0 && d[n - 1] == 0) {
		n--;
	}
	return n;
}

static int fn_add_digits(const unsigned char *a, int na, const unsigned char *b, int nb, unsigned char *out)
{
	int n = na > nb ? na : nb;
	int carry = 0;
	int i;

	for (i = 0; i < n; i++) {
		int sum = carry + (i < na ? a[i] : 0) + (i < nb ? b[i] : 0);

		out[i] = (unsigned char)(sum % 10);
		carry = sum / 10;
	}
	if (carry) {
		out[n++] = (unsigned char)carry;
	}
	return n;
}

/* a MINUS b for a >= b, as the caller establishes. Safe when `out` IS `a`. */
static int fn_sub_digits(const unsigned char *a, int na, const unsigned char *b, int nb, unsigned char *out)
{
	int borrow = 0;
	int i;

	for (i = 0; i < na; i++) {
		int diff = a[i] - borrow - (i < nb ? b[i] : 0);

		if (diff < 0) {
			diff += 10;
			borrow = 1;
		} else {
			borrow = 0;
		}
		out[i] = (unsigned char)diff;
	}
	return fn_strip(out, na);
}

static int fn_cmp_digits(const unsigned char *a, int na, const unsigned char *b, int nb)
{
	int i;

	if (na != nb) {
		return na < nb ? -1 : 1;
	}
	for (i = na - 1; i >= 0; i--) {
		if (a[i] != b[i]) {
			return a[i] < b[i] ? -1 : 1;
		}
	}
	return 0;
}

static int fn_mul_digits(const unsigned char *a, int na, const unsigned char *b, int nb, unsigned char *out)
{
	int i, j;

	if (na == 0 || nb == 0) {
		return 0;
	}
	for (i = 0; i < na + nb; i++) {
		out[i] = 0;
	}
	for (i = 0; i < na; i++) {
		int carry = 0;

		for (j = 0; j < nb; j++) {
			int cell = out[i + j] + a[i] * b[j] + carry;

			out[i + j] = (unsigned char)(cell % 10);
			carry = cell / 10;
		}
		for (j = i + nb; carry; j++) {
			int cell = out[j] + carry;

			out[j] = (unsigned char)(cell % 10);
			carry = cell / 10;
		}
	}
	return fn_strip(out, na + nb);
}

/* ONE DIGIT TIMES AN ARRAY: what long division needs. */
static int fn_mul_small(const unsigned char *a, int na, int digit, unsigned char *out)
{
	int carry = 0;
	int i;

	for (i = 0; i < na; i++) {
		int cell = a[i] * digit + carry;

		out[i] = (unsigned char)(cell % 10);
		carry = cell / 10;
	}
	if (carry) {
		out[na++] = (unsigned char)carry;
	}
	return fn_strip(out, na);
}

/* a × 10^shift (aligning). −1 when it does not fit, which the caller must decide about. */
static int fn_shift_digits(const unsigned char *a, int na, int shift, unsigned char *out, int capacity)
{
	int i;

	if (na + shift > capacity) {
		return -1;
	}
	for (i = 0; i < shift; i++) {
		out[i] = 0;
	}
	for (i = 0; i < na; i++) {
		out[shift + i] = a[i];
	}
	return fn_strip(out, na + shift);
}

/* THE DIGIT AT A PLACE VALUE, whether or not the mantissa reaches it: how comparing and printing avoid
 * materialising an exponent the 38-digit mantissa could not hold. */
static int fn_digit_at(const NSDecimal *x, int place)
{
	int index = place - (int)x->_exponent;

	if (index < 0 || index >= (int)x->_length) {
		return 0;
	}
	return x->_digits[index];
}

/* ---- struct helpers ----------------------------------------------------------------------------- */

static void fn_make_nan(NSDecimal *r)
{
	int i;

	r->_exponent = 0;
	r->_length = 0;
	r->_isNegative = 0;
	r->_isCompact = 0;
	r->_isNaN = 1;
	for (i = 0; i < FN_DIGITS; i++) {
		r->_digits[i] = 0;
	}
}

static void fn_from_digits(NSDecimal *r, const unsigned char *d, int n, int exponent, int negative)
{
	int i;

	n = fn_strip(d, n);
	r->_length = (unsigned char)n;
	r->_isNegative = (n > 0 && negative) ? 1 : 0;
	r->_isCompact = 0;
	r->_isNaN = 0;
	r->_exponent = (signed char)(n == 0 ? 0 : exponent);
	for (i = 0; i < n; i++) {
		r->_digits[i] = d[i];
	}
	for (; i < FN_DIGITS; i++) {
		r->_digits[i] = 0;
	}
}

/* IS IT ZERO? Of the DIGITS, not of the length: a value may arrive with a length of 1 and an all-zero
 * mantissa, and asking `_length == 0` reads that as a non-zero divisor. */
static int fn_is_zero(const NSDecimal *x)
{
	return !x->_isNaN && fn_strip(x->_digits, x->_length) == 0;
}

/* THE EXPONENT'S RANGE IS PART OF THE MODEL, and a value that leaves it is both REPORTED and
 * SATURATED. Reported because that is what the code is for; saturated because the alternative is a
 * WRAPPED exponent, which reads as a plausible number rather than as an error. So: above the range the
 * result is NSDecimalMax or NSDecimalMin by its sign, below it a signed zero, and the code says which.
 * Measured the hard way — the exponent lives in a signed char, so the whole trick is to compute it in an
 * int and look before the cast. */
static NSCalculationError fn_range(NSDecimal *r)
{
	if (fn_is_zero(r) || r->_isNaN) {
		return NSCalculationNoError;
	}
	if (r->_exponent > NSDecimalMaxExponent) {
		*r = r->_isNegative ? NSDecimalMin : NSDecimalMax;
		return NSCalculationOverflow;
	}
	if (r->_exponent < NSDecimalMinExponent) {
		int negative = r->_isNegative;

		fn_from_digits(r, r->_digits, 0, 0, negative);
		return NSCalculationUnderflow;
	}
	return NSCalculationNoError;
}

/* THE ROUNDING DECISION, shared by both forms: `decide` is the first dropped digit, `tail` says whether
 * anything non-zero follows it, `even` is the parity of the last kept digit (Bankers' tie-break). */
static int fn_round_up(NSRoundingMode mode, int decide, int tail, int even)
{
	switch (mode) {
	case NSRoundDown:
		return 0;
	case NSRoundUp:
		return (decide != 0 || tail) ? 1 : 0;
	case NSRoundBankers:
		if (decide > 5) {
			return 1;
		}
		if (decide < 5) {
			return 0;
		}
		return tail ? 1 : even;
	case NSRoundPlain:
	default:
		return decide >= 5 ? 1 : 0;
	}
}

static int fn_increment(NSDecimal *r, int limit)
{
	int i = 0;
	int carry = 1;

	while (carry && i < limit) {
		int cell = r->_digits[i] + carry;

		r->_digits[i] = (unsigned char)(cell % 10);
		carry = cell / 10;
		i++;
	}
	if (carry) {
		int j;

		for (j = limit; j > 0; j--) {
			r->_digits[j] = r->_digits[j - 1];
		}
		r->_digits[0] = 1;
		r->_exponent = (signed char)(r->_exponent + 1);
		return limit + 1;
	}
	return limit;
}

/* TO 38 SIGNIFICANT DIGITS: drops the HIGH end, so the exponent is untouched and only precision is lost,
 * which is reported. */
static NSCalculationError fn_round38(NSDecimal *r, NSRoundingMode mode)
{
	int length = r->_length;
	int decide;
	int tail = 0;
	int i;

	if (length <= FN_DIGITS) {
		return NSCalculationNoError;
	}
	decide = r->_digits[FN_DIGITS];
	for (i = FN_DIGITS + 1; i < length; i++) {
		if (r->_digits[i] != 0) {
			tail = 1;
			break;
		}
	}
	r->_length = FN_DIGITS;
	if (fn_round_up(mode, decide, tail, r->_digits[FN_DIGITS - 1] % 2)) {
		r->_length = (unsigned char)fn_increment(r, FN_DIGITS);
	}
	r->_length = (unsigned char)fn_strip(r->_digits, r->_length);
	return NSCalculationLossOfPrecision;
}

/* TO A SCALE — `scale` digits AFTER THE POINT, so what is dropped is every digit below place −scale. A
 * digit at index i has place exponent+i, so the digits to drop are indices 0..k−1 with k = −scale −
 * exponent, the DECISION digit is the first dropped one (index k−1, place −scale−1), and dropping k
 * digits moves the exponent to exactly −scale. Getting the sign of k wrong here rounds in the wrong
 * direction off by one whole digit, which is what the probe's table caught. */
static void fn_round_scale(NSDecimal *r, NSInteger scale, NSRoundingMode mode)
{
	int k;
	int decide;
	int tail = 0;
	int kept;
	int even;
	int i;
	int up;

	if (r->_length == 0 || r->_isNaN) {
		return;
	}
	k = (int)(-scale) - (int)r->_exponent;
	if (k <= 0) {
		return;					/* nothing lies below the kept places */
	}
	decide = (k - 1 < (int)r->_length) ? r->_digits[k - 1] : 0;
	for (i = 0; i < k - 1 && i < (int)r->_length; i++) {
		if (r->_digits[i] != 0) {
			tail = 1;
			break;
		}
	}
	kept = (int)r->_length - k;
	if (kept < 0) {
		kept = 0;
	}
	even = (kept > 0) ? r->_digits[k] % 2 : 0;
	up = fn_round_up(mode, decide, tail, even);
	for (i = 0; i < kept; i++) {
		r->_digits[i] = r->_digits[i + k];
	}
	r->_length = (unsigned char)kept;
	r->_exponent = (signed char)(-scale);
	if (up) {
		if (kept == 0) {
			/* EVERYTHING was dropped and the mode says round away from zero: the result is one unit
			 * in the last kept place, whose exponent is already −scale. */
			r->_digits[0] = 1;
			r->_length = 1;
		} else {
			r->_length = (unsigned char)fn_increment(r, kept);
		}
	}
	r->_length = (unsigned char)fn_strip(r->_digits, r->_length);
}

/* ---- the public surface --------------------------------------------------------------------------- */

void NSDecimalCompact(NSDecimal *number)
{
	int i;

	if (number->_isNaN) {
		return;
	}
	number->_length = (unsigned char)fn_strip(number->_digits, number->_length);
	/* LOW ZEROS GO TOO, by raising the exponent: 120 × 10^-2 and 12 × 10^-1 are the same number. */
	while (number->_length > 0 && number->_digits[0] == 0) {
		for (i = 0; i + 1 < number->_length; i++) {
			number->_digits[i] = number->_digits[i + 1];
		}
		number->_length--;
		number->_exponent = (signed char)(number->_exponent + 1);
	}
	if (number->_length == 0) {
		number->_exponent = 0;
		number->_isNegative = 0;
	}
	number->_isCompact = 1;
}

void NSDecimalCopy(NSDecimal *destination, const NSDecimal *source)
{
	*destination = *source;
}

BOOL NSDecimalIsNotANumber(const NSDecimal *number)
{
	return number->_isNaN != 0;
}

NSCalculationError NSDecimalNormalize(NSDecimal *number1, NSDecimal *number2, NSRoundingMode mode)
{
	NSDecimal *big;
	NSDecimal *small;
	unsigned char shifted[FN_DIGITS];
	int shift;
	int n;
	int negative;

	if (number1->_isNaN || number2->_isNaN) {
		return NSCalculationNoError;
	}
	if (number1->_length == 0) {
		number1->_exponent = number2->_exponent;
		return NSCalculationNoError;
	}
	if (number2->_length == 0) {
		number2->_exponent = number1->_exponent;
		return NSCalculationNoError;
	}
	big = (number1->_exponent >= number2->_exponent) ? number1 : number2;
	small = (big == number1) ? number2 : number1;
	shift = (int)big->_exponent - (int)small->_exponent;
	if (shift == 0) {
		return NSCalculationNoError;
	}
	n = fn_shift_digits(big->_digits, big->_length, shift, shifted, FN_DIGITS);
	if (n >= 0) {
		negative = big->_isNegative;
		fn_from_digits(big, shifted, n, small->_exponent, negative);
		return NSCalculationNoError;
	}
	/* IT DOES NOT FIT: the overflow is rounded away and the caller is TOLD. */
	{
		unsigned char wide[FN_DIGITS * 2];
		int wideLength = fn_shift_digits(big->_digits, big->_length, shift, wide, FN_DIGITS * 2);

		negative = big->_isNegative;
		fn_from_digits(big, wide, wideLength, small->_exponent, negative);
		return fn_round38(big, mode);
	}
}

static NSCalculationError fn_add_or_subtract(NSDecimal *result, const NSDecimal *left, const NSDecimal *right,
					     NSRoundingMode mode, int subtract)
{
	NSDecimal a = *left;
	NSDecimal b = *right;
	unsigned char out[FN_DIGITS * 2];
	int n;
	int negative;
	NSCalculationError rounding;

	if (a._isNaN || b._isNaN) {
		fn_make_nan(result);
		return NSCalculationNoError;
	}
	(void)NSDecimalNormalize(&a, &b, mode);
	/* THE SIGNS DECIDE WHICH OPERATION THIS IS: a − b is a + (−b). */
	if (subtract) {
		b._isNegative = !b._isNegative;
	}
	if (a._length == 0) {
		*result = b;
	}
	else if (b._length == 0) {
		*result = a;
	}
	else if (a._isNegative == b._isNegative) {
		n = fn_add_digits(a._digits, a._length, b._digits, b._length, out);
		fn_from_digits(result, out, n, a._exponent, a._isNegative);
	} else {
		int cmp = fn_cmp_digits(a._digits, a._length, b._digits, b._length);

		if (cmp == 0) {
			fn_from_digits(result, out, 0, 0, 0);
			return NSCalculationNoError;
		}
		if (cmp > 0) {
			n = fn_sub_digits(a._digits, a._length, b._digits, b._length, out);
			negative = a._isNegative;
		} else {
			n = fn_sub_digits(b._digits, b._length, a._digits, a._length, out);
			negative = b._isNegative;
		}
		fn_from_digits(result, out, n, a._exponent, negative);
	}
	rounding = fn_round38(result, mode);
	if (rounding != NSCalculationNoError) {
		return rounding;
	}
	NSDecimalCompact(result);
	return fn_range(result);
}

NSCalculationError NSDecimalAdd(NSDecimal *result, const NSDecimal *left, const NSDecimal *right, NSRoundingMode mode)
{
	NSDecimal local;
	NSCalculationError error = fn_add_or_subtract(&local, left, right, mode, 0);

	*result = local;
	return error;
}

NSCalculationError NSDecimalSubtract(NSDecimal *result, const NSDecimal *left, const NSDecimal *right, NSRoundingMode mode)
{
	NSDecimal local;
	NSCalculationError error = fn_add_or_subtract(&local, left, right, mode, 1);

	*result = local;
	return error;
}

NSCalculationError NSDecimalMultiply(NSDecimal *result, const NSDecimal *left, const NSDecimal *right, NSRoundingMode mode)
{
	NSDecimal local;
	unsigned char out[FN_DIGITS * 2];
	int n;
	NSCalculationError rounding;

	if (left->_isNaN || right->_isNaN) {
		fn_make_nan(result);
		return NSCalculationNoError;
	}
	if (left->_length == 0 || right->_length == 0) {
		fn_from_digits(result, out, 0, 0, 0);
		return NSCalculationNoError;
	}
	n = fn_mul_digits(left->_digits, left->_length, right->_digits, right->_length, out);
	fn_from_digits(&local, out, n, left->_exponent + right->_exponent,
		       left->_isNegative != right->_isNegative);
	rounding = fn_round38(&local, mode);
	*result = local;
	if (rounding != NSCalculationNoError) {
		return rounding;
	}
	NSDecimalCompact(result);
	return fn_range(result);
}

/* LONG DIVISION. THE DIGIT COUNT IS THE ONE CHOICE HERE: 39 quotient digits are computed and the LAST
 * one (the least significant) decides how the 38 kept ones round. */
NSCalculationError NSDecimalDivide(NSDecimal *result, const NSDecimal *left, const NSDecimal *right, NSRoundingMode mode)
{
	unsigned char quotient[FN_DIGITS + 1];
	unsigned char remainder[FN_DIGITS + 2];
	unsigned char product[FN_DIGITS + 2];
	int rLength = 0;
	int total = FN_DIGITS + 1;
	int fromNumerator = (int)left->_length;
	int i;
	int exponent;
	int negative;
	int decide;
	int up;

	if (left->_isNaN || right->_isNaN) {
		fn_make_nan(result);
		return NSCalculationNoError;
	}
	if (fn_is_zero(right)) {
		fn_make_nan(result);
		return NSCalculationDivideByZero;
	}
	if (fn_is_zero(left)) {
		fn_from_digits(result, quotient, 0, 0, 0);
		return NSCalculationNoError;
	}
	negative = left->_isNegative != right->_isNegative;
	exponent = (int)left->_exponent - (int)right->_exponent - (total - fromNumerator) + 1;
	for (i = 0; i < total; i++) {
		int next = (i < fromNumerator) ? left->_digits[fromNumerator - 1 - i] : 0;
		int trial = 0;
		int k;

		/* remainder = remainder × 10 + next */
		if (rLength > 0) {
			for (k = rLength; k > 0; k--) {
				remainder[k] = remainder[k - 1];
			}
		}
		remainder[0] = (unsigned char)next;
		rLength = fn_strip(remainder, rLength + 1);
		for (trial = 9; trial >= 0; trial--) {
			int pl = fn_mul_small(right->_digits, right->_length, trial, product);

			if (fn_cmp_digits(product, pl, remainder, rLength) <= 0) {
				rLength = fn_sub_digits(remainder, rLength, product, pl, remainder);
				break;
			}
		}
		quotient[total - 1 - i] = (unsigned char)trial;
	}
	/* THE LAST COMPUTED DIGIT decides; the 38 kept ones are indices 1..38 (LSD-first), so they move down
	 * one place and the exponent rises by one. */
	decide = quotient[0];
	up = fn_round_up(mode, decide, rLength > 0 ? 1 : 0, quotient[1] % 2);
	for (i = 0; i < FN_DIGITS; i++) {
		quotient[i] = quotient[i + 1];
	}
	fn_from_digits(result, quotient, FN_DIGITS, exponent, negative);
	if (up) {
		result->_length = (unsigned char)fn_increment(result, FN_DIGITS);
	}
	result->_length = (unsigned char)fn_strip(result->_digits, result->_length);
	NSDecimalCompact(result);
	if (decide != 0 || rLength > 0) {
		return NSCalculationLossOfPrecision;
	}
	return fn_range(result);
}

NSCalculationError NSDecimalPower(NSDecimal *result, const NSDecimal *number, NSUInteger power, NSRoundingMode mode)
{
	NSDecimal accumulator;
	unsigned char one[1] = { 1 };
	NSUInteger i;
	NSCalculationError error = NSCalculationNoError;

	if (power == 0) {
		fn_from_digits(result, one, 1, 0, 0);
		return NSCalculationNoError;
	}
	accumulator = *number;
	for (i = 1; i < power; i++) {
		error = NSDecimalMultiply(&accumulator, &accumulator, number, mode);
		if (error != NSCalculationNoError) {
			break;
		}
	}
	*result = accumulator;
	NSDecimalCompact(result);
	return error;
}

NSCalculationError NSDecimalMultiplyByPowerOf10(NSDecimal *result, const NSDecimal *number, short power, NSRoundingMode mode)
{
	NSDecimal local = *number;

	(void)mode;
	if (fn_is_zero(&local)) {
		*result = local;
		return NSCalculationNoError;
	}
	{
		int exponent = (int)local._exponent + (int)power;

		if (exponent > NSDecimalMaxExponent) {
			*result = local._isNegative ? NSDecimalMin : NSDecimalMax;
			return NSCalculationOverflow;
		}
		if (exponent < NSDecimalMinExponent) {
			int negative = local._isNegative;

			fn_from_digits(&local, local._digits, 0, 0, negative);
			*result = local;
			return NSCalculationUnderflow;
		}
		local._exponent = (signed char)exponent;
	}
	*result = local;
	NSDecimalCompact(result);
	return NSCalculationNoError;
}

void NSDecimalRound(NSDecimal *result, const NSDecimal *number, NSInteger scale, NSRoundingMode mode)
{
	NSDecimal local = *number;

	if (scale != NSDecimalNoScale) {
		fn_round_scale(&local, scale, mode);
	}
	*result = local;
}

NSComparisonResult NSDecimalCompare(const NSDecimal *left, const NSDecimal *right)
{
	int high;
	int place;

	if (left->_isNaN || right->_isNaN) {
		return NSOrderedSame;
	}
	if (fn_is_zero(left) && fn_is_zero(right)) {
		return NSOrderedSame;
	}
	if (left->_isNegative != right->_isNegative) {
		return left->_isNegative ? NSOrderedAscending : NSOrderedDescending;
	}
	high = (int)left->_exponent + (int)left->_length;
	if ((int)right->_exponent + (int)right->_length > high) {
		high = (int)right->_exponent + (int)right->_length;
	}
	for (place = high - 1; place >= NSDecimalMinExponent - 1; place--) {
		int a = fn_digit_at(left, place);
		int b = fn_digit_at(right, place);

		if (a != b) {
			int ascending = a < b;

			if (left->_isNegative) {
				ascending = !ascending;
			}
			return ascending ? NSOrderedAscending : NSOrderedDescending;
		}
	}
	return NSOrderedSame;
}

NSString *NSDecimalString(const NSDecimal *number, id locale)
{
	/* THE PRINTED FORM IS LONGER THAN THE MANTISSA, and this buffer was once the mantissa's size — which
	 * the probe's NSDecimalMax check turned into a crash: the highest place a 38-digit decimal can reach
	 * is NSDecimalMaxExponent + NSDecimalMaxDigits - 1 (164), the lowest NSDecimalMinExponent (-128), so
	 * a sign, the integer part, a point and the fraction need 164 + 1 + 128 + 2. */
	char text[NSDecimalMaxExponent + NSDecimalMaxDigits + (-NSDecimalMinExponent) + 8];
	int n = 0;
	int high;
	int place;
	int printed = 0;

	(void)locale;
	if (number->_isNaN) {
		return @"NaN";
	}
	if (number->_isNegative && !fn_is_zero(number)) {
		text[n++] = '-';
	}
	if (fn_is_zero(number)) {
		text[n++] = '0';
		text[n] = '\0';
		return [NSString stringWithUTF8String:text];
	}
	high = (int)number->_exponent + (int)number->_length;
	for (place = high - 1; place >= 0; place--) {
		text[n++] = (char)('0' + fn_digit_at(number, place));
		printed++;
	}
	if (printed == 0) {
		text[n++] = '0';
	}
	if (number->_exponent < 0) {
		text[n++] = '.';
		for (place = -1; place >= (int)number->_exponent; place--) {
			text[n++] = (char)('0' + fn_digit_at(number, place));
		}
	}
	text[n] = '\0';
	return [NSString stringWithUTF8String:text];
}

/* THE TWO CONSTANTS: 38 NINES at the largest exponent, and its negative. Counted in groups of ten,
 * because the first version of this macro had thirty-seven and nothing said so: NSDecimalMax claimed a
 * length of 38 with a top digit of zero, so it was not the maximum, and max + max produced a mantissa
 * that never needed rounding — an arithmetic error that hid inside a constant. The probe now asserts the
 * shape (38 digits, top digit 9) for exactly this reason. */
#define FN_DECIMAL_INIT(sign)								\
	{ NSDecimalMaxExponent - (NSDecimalMaxDigits - 1), NSDecimalMaxDigits, sign, 1, 0,	\
	  { 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,						\
	    9, 9, 9, 9, 9, 9, 9, 9, 9, 9,						\
	    9, 9, 9, 9, 9, 9, 9, 9, 9, 9,						\
	    9, 9, 9, 9, 9, 9, 9, 9 } }

const NSDecimal NSDecimalMax = FN_DECIMAL_INIT(0);
const NSDecimal NSDecimalMin = FN_DECIMAL_INIT(1);
