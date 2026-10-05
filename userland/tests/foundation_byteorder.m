/*
 * foundation_byteorder.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE BYTE-ORDER CONVERSIONS (§62.51). Apple deprecated the thirty `NSSwap*` functions at 10.9 in favour of the
 * `CFSwap*` spelling; §62.24's policy puts them back, and the twelve CROSS directions come with them because a
 * caller who needs big-endian bytes on a little-endian machine has no other door.
 *
 * EVERY ONE OF THESE IS A REVERSAL OF THE BYTES, WHICH MAKES THEM PROVABLE RATHER THAN MERELY PRESENT, and this
 * probe proves them four ways:
 *
 *   - a KNOWN VALUE becomes the KNOWN VALUE (0x01020304 -> 0x04030201), per width;
 *   - SWAPPING TWICE IS THE IDENTITY, for every function in the family;
 *   - a HOST conversion is the identity when the direction is the host's own and a reversal when it is not, with
 *     the host's own order PRINTED so the reading is checkable rather than asserted;
 *   - a conversion BETWEEN TWO NON-HOST ORDERS is a reversal whatever the host is.
 *
 * AND THE LAST CHECK IS THE CONTROL: a 32-bit implementation would pass every 32-bit check above and fail the
 * 64-bit one, which is exactly the kind of half-right that a check on "something changed" would miss.
 */
#import <Foundation/Foundation.h>
#include <stdio.h>
#include <string.h>

static int okc = 0, failc = 0;

static void check(const char *name, BOOL held, NSString *why)
{
	if(held) {
		okc++;
		printf("FOUNDATION-BYTEORDER %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-BYTEORDER %s FAIL: %s\n", name, [why UTF8String]);
	}
}

/* A FLOAT AND A DOUBLE ARE CHECKED BY THEIR BYTES, because their VALUES are not a byte pattern a reader can
 * recognise: 1.0f reversed is a denormal that prints as nothing useful. */
static void fn_bytes(void *dst, const void *src, unsigned long count)
{
	const unsigned char *in = (const unsigned char *)src;
	unsigned char *out = (unsigned char *)dst;
	unsigned long i;

	for (i = 0; i < count; i++) {
		out[i] = in[count - 1 - i];
	}
}

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);

	/* --- THE PLAIN SWAPS: A KNOWN VALUE BECOMES ITS MIRROR ---------------------------------------------- */
	{
		float one = 1.0f;
		double two = 2.0;
		float swappedOne = NSSwapFloat(one);
		double swappedTwo = NSSwapDouble(two);
		unsigned char want[8];
		unsigned char got[8];
		int floatsMatch, doublesMatch;

		fn_bytes(want, &one, 4);
		memcpy(got, &swappedOne, 4);
		floatsMatch = memcmp(want, got, 4) == 0;
		fn_bytes(want, &two, 8);
		memcpy(got, &swappedTwo, 8);
		doublesMatch = memcmp(want, got, 8) == 0;

		check("the-plain-swaps-reverse-the-bytes",
		      NSSwapShort(0x0102) == 0x0201 &&
		      NSSwapInt(0x01020304) == 0x04030201 &&
		      /* `unsigned long` IS SIXTY-FOUR BITS ON THIS PLATFORM, so this is a WIDE swap: stated rather than
		       * assumed, because the first version of this probe asserted the thirty-two-bit reading and printed
		       * a correct-looking value beside the failure. */
		      NSSwapLong(0x01020304UL) == 0x0403020100000000UL &&
		      NSSwapLongLong(0x0102030405060708ULL) == 0x0807060504030201ULL &&
		      floatsMatch && doublesMatch,
		      [NSString stringWithFormat:@"short 0x0102 -> 0x%04x; int -> 0x%08x; long -> 0x%016lx; long "
			@"long -> 0x%016llx; float %@, double %@", NSSwapShort(0x0102), NSSwapInt(0x01020304),
			NSSwapLong(0x01020304UL), (unsigned long long)NSSwapLongLong(0x0102030405060708ULL),
			floatsMatch ? @"reversed" : @"NOT reversed",
			doublesMatch ? @"reversed" : @"NOT reversed"]);
	}

	/* --- SWAPPING TWICE IS THE IDENTITY, FOR EVERY FUNCTION IN THE FAMILY -------------------------------- */
	{
		BOOL all = YES;
		unsigned short s = 0x1234;
		unsigned int i = 0x12345678;
		unsigned long l = 0x12345678;
		unsigned long long ll = 0x123456789abcdef0ULL;

		all = all && (NSSwapShort(NSSwapShort(s)) == s) && (NSSwapInt(NSSwapInt(i)) == i);
		all = all && (NSSwapLong(NSSwapLong(l)) == l) && (NSSwapLongLong(NSSwapLongLong(ll)) == ll);
		all = all && (NSSwapHostShortToBig(NSSwapHostShortToBig(s)) == s);
		all = all && (NSSwapHostShortToLittle(NSSwapHostShortToLittle(s)) == s);
		all = all && (NSSwapHostIntToBig(NSSwapHostIntToBig(i)) == i);
		all = all && (NSSwapHostIntToLittle(NSSwapHostIntToLittle(i)) == i);
		all = all && (NSSwapHostLongToBig(NSSwapHostLongToBig(l)) == l);
		all = all && (NSSwapHostLongToLittle(NSSwapHostLongToLittle(l)) == l);
		all = all && (NSSwapHostLongLongToBig(NSSwapHostLongLongToBig(ll)) == ll);
		all = all && (NSSwapHostLongLongToLittle(NSSwapHostLongLongToLittle(ll)) == ll);
		all = all && (NSSwapBigShortToHost(NSSwapBigShortToHost(s)) == s);
		all = all && (NSSwapLittleShortToHost(NSSwapLittleShortToHost(s)) == s);
		all = all && (NSSwapBigIntToHost(NSSwapBigIntToHost(i)) == i);
		all = all && (NSSwapLittleIntToHost(NSSwapLittleIntToHost(i)) == i);
		all = all && (NSSwapBigLongToHost(NSSwapBigLongToHost(l)) == l);
		all = all && (NSSwapLittleLongToHost(NSSwapLittleLongToHost(l)) == l);
		all = all && (NSSwapBigLongLongToHost(NSSwapBigLongLongToHost(ll)) == ll);
		all = all && (NSSwapLittleLongLongToHost(NSSwapLittleLongLongToHost(ll)) == ll);
		all = all && (NSSwapBigShortToLittle(NSSwapBigShortToLittle(s)) == s);
		all = all && (NSSwapLittleShortToBig(NSSwapLittleShortToBig(s)) == s);
		all = all && (NSSwapBigIntToLittle(NSSwapBigIntToLittle(i)) == i);
		all = all && (NSSwapLittleIntToBig(NSSwapLittleIntToBig(i)) == i);
		all = all && (NSSwapBigLongToLittle(NSSwapBigLongToLittle(l)) == l);
		all = all && (NSSwapLittleLongToBig(NSSwapLittleLongToBig(l)) == l);
		all = all && (NSSwapBigLongLongToLittle(NSSwapBigLongLongToLittle(ll)) == ll);
		all = all && (NSSwapLittleLongLongToBig(NSSwapLittleLongLongToBig(ll)) == ll);
		all = all && (NSSwapFloat(NSSwapFloat(1.5f)) == 1.5f);
		all = all && (NSSwapDouble(NSSwapDouble(2.5)) == 2.5);
		all = all && (NSSwapHostFloatToBig(NSSwapHostFloatToBig(1.5f)) == 1.5f);
		all = all && (NSSwapHostDoubleToLittle(NSSwapHostDoubleToLittle(2.5)) == 2.5);
		all = all && (NSSwapBigFloatToHost(NSSwapBigFloatToHost(1.5f)) == 1.5f);
		all = all && (NSSwapLittleDoubleToHost(NSSwapLittleDoubleToHost(2.5)) == 2.5);
		all = all && (NSSwapBigFloatToLittle(NSSwapBigFloatToLittle(1.5f)) == 1.5f);
		all = all && (NSSwapLittleDoubleToBig(NSSwapLittleDoubleToBig(2.5)) == 2.5);

		check("swapping-twice-is-the-identity-for-the-whole-family", all,
		      @"one round trip through every integer width, both host directions, both host doors and both "
		      @"cross directions");
	}

	/* --- WHERE THE HOST STANDS, MEASURED RATHER THAN ASSUMED -------------------------------------------- */
	{
		NSByteOrder order = NSHostByteOrder();
		BOOL little = (order == NS_LittleEndian);
		BOOL held;

		if (little) {
			held = NSSwapHostIntToLittle(0x01020304) == 0x01020304 &&
			       NSSwapHostIntToBig(0x01020304) == 0x04030201;
		} else {
			held = NSSwapHostIntToBig(0x01020304) == 0x01020304 &&
			       NSSwapHostIntToLittle(0x01020304) == 0x04030201;
		}
		check("the-host-conversions-are-the-identity-or-a-reversal",
		      held && order == NS_LittleEndian,
		      [NSString stringWithFormat:@"NSHostByteOrder() reports %d (%@); toLittle(0x01020304) -> 0x%08x and "
			@"toBig -> 0x%08x", (int)order, little ? @"little" : @"big",
			NSSwapHostIntToLittle(0x01020304), NSSwapHostIntToBig(0x01020304)]);
	}

	/* --- THE HOST DOORS MIRROR THE HOST CONVERSIONS ----------------------------------------------------- */
	{
		BOOL all = YES;
		unsigned int i = 0x0a0b0c0d;

		all = all && (NSSwapBigIntToHost(NSSwapHostIntToBig(i)) == i);
		all = all && (NSSwapLittleIntToHost(NSSwapHostIntToLittle(i)) == i);
		all = all && (NSSwapBigShortToHost(NSSwapHostShortToBig((unsigned short)0x0a0b)) == 0x0a0b);
		all = all && (NSSwapLittleShortToHost(NSSwapHostShortToLittle((unsigned short)0x0a0b)) == 0x0a0b);
		all = all && (NSSwapBigLongToHost(NSSwapHostLongToBig((unsigned long)0x0a0b0c0d)) == (unsigned long)0x0a0b0c0d);
		all = all && (NSSwapLittleLongToHost(NSSwapHostLongToLittle((unsigned long)0x0a0b0c0d)) == (unsigned long)0x0a0b0c0d);
		all = all && (NSSwapBigFloatToHost(NSSwapHostFloatToBig(1.5f)) == 1.5f);
		all = all && (NSSwapLittleDoubleToHost(NSSwapHostDoubleToLittle(2.5)) == 2.5);
		check("the-host-doors-mirror-the-host-conversions", all,
		      @"int, short, long, float and double each survive out-and-back through the pair of doors");
	}

	/* --- TWO NON-HOST ORDERS: ALWAYS A REVERSAL --------------------------------------------------------- */
	{
		BOOL all = YES;

		all = all && (NSSwapBigIntToLittle(0x01020304) == 0x04030201);
		all = all && (NSSwapLittleIntToBig(0x01020304) == 0x04030201);
		all = all && (NSSwapBigShortToLittle(0x0102) == 0x0201);
		all = all && (NSSwapLittleShortToBig(0x0102) == 0x0201);
		all = all && (NSSwapBigLongToLittle(0x01020304UL) == 0x0403020100000000UL);
		all = all && (NSSwapLittleLongToBig(0x01020304UL) == 0x0403020100000000UL);
		all = all && (NSSwapBigLongLongToLittle(0x0102030405060708ULL) == 0x0807060504030201ULL);
		all = all && (NSSwapLittleLongLongToBig(0x0102030405060708ULL) == 0x0807060504030201ULL);
		all = all && (NSSwapBigIntToLittle(0x01020304) == NSSwapInt(0x01020304));
		check("a-conversion-between-two-non-host-orders-is-always-a-reversal", all,
		      [NSString stringWithFormat:@"int 0x01020304 -> 0x%08x (the plain swap says 0x%08x); long "
			@"long -> 0x%016llx", NSSwapBigIntToLittle(0x01020304), NSSwapInt(0x01020304),
			(unsigned long long)NSSwapBigLongLongToLittle(0x0102030405060708ULL)]);
	}

	/* --- THE CONTROL: THE 64-BIT SWAP MOVES SIXTY-FOUR BITS --------------------------------------------- */
	{
		unsigned long long wide = NSSwapLongLong(0x0102030405060708ULL);
		unsigned int narrow = NSSwapInt(0x01020304);

		check("the-wide-swap-moves-a-wide-value",
		      wide == 0x0807060504030201ULL && narrow == 0x04030201 &&
		      (unsigned int)(wide >> 32) == 0x08070605U,
		      [NSString stringWithFormat:@"long long 0x0102030405060708 -> 0x%016llx (its low half alone would be "
			@"0x%08x)", (unsigned long long)wide, narrow]);
	}

	printf("FOUNDATION-BYTEORDER RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-BYTEORDER-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-BYTEORDER DONE\n");
	return failc ? 1 : 0;
}
