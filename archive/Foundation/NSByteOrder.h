/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSByteOrder.h — the byte-order family (W2c, docs/design/foundation-plan.md §14).
 *
 * THE REPRESENTATION IS OURS AND THE ROUND TRIP IS THE CONTRACT. Apple's `NSSwappedFloat` is a
 * struct holding a 32-bit float's bytes in an `unsigned long`, and `NSSwappedDouble` an
 * `unsigned long long`; on LP64 both are 8 bytes, which is what this header matches. What a
 * program USES is the conversion pair — take a float to its swapped form, take it back — and
 * that round trip is what the probe asserts, because Apple's exact packing is not published
 * (the same situation as NSAlignmentOptions, §14.2).
 *
 * THE CASES' VALUES ARE OURS TOO, and self-consistency is what matters: a program compares
 * `NSHostByteOrder() == NS_LittleEndian`, which works whatever the numbers are.
 */
#ifndef FOUNDATION_NSBYTEORDER_H
#define FOUNDATION_NSBYTEORDER_H

#import <Foundation/NSObjCRuntime.h>

NS_ASSUME_NONNULL_BEGIN

typedef struct {
	unsigned long v;		/* a 32-bit float's bytes, swapped */
} NSSwappedFloat;

typedef struct {
	unsigned long long v;		/* a 64-bit double's bytes, swapped */
} NSSwappedDouble;

typedef enum {
	NS_UnknownByteOrder = -1,
	NS_LittleEndian = 0,
	NS_BigEndian = 1
} NSByteOrder;

NSSwappedFloat NSConvertHostFloatToSwapped(float x);
float NSConvertSwappedFloatToHost(NSSwappedFloat x);
NSSwappedDouble NSConvertHostDoubleToSwapped(double x);
double NSConvertSwappedDoubleToHost(NSSwappedDouble x);
NSByteOrder NSHostByteOrder(void);


/* ===================================================================================================
 * THE BYTE-ORDER CONVERSIONS (§62.51)
 *
 * Apple deprecated these thirty at 10.9 in favour of the `NSSwap*` spelling it uses in `NSByteOrder.h`
 * and the `CFSwap*` functions; §62.24's policy puts them back. THEY ARE THE WHOLE FAMILY, which is
 * why the twelve CROSS directions are here too: a caller who needs big-endian bytes on a little-endian
 * machine has no other door, and a family with a hole in it is a family a caller has to work around.
 *
 * EVERY ONE OF THEM IS A REVERSAL OF THE BYTES, and the ONLY thing that differs between them is which
 * direction the caller wrote down — so the implementations are one helper per width and the direction
 * is decided once (see NSObject.m). Three invariants hold for all of them and the probe measures all
 * three: SWAPPING TWICE IS THE IDENTITY; a HOST conversion is the identity when the direction is the
 * host's own and a reversal when it is not; and a conversion BETWEEN TWO NON-HOST ORDERS is always a
 * reversal, whatever the host is.
 * =================================================================================================== */

unsigned short NSSwapShort(unsigned short value);
unsigned int NSSwapInt(unsigned int value);
unsigned long NSSwapLong(unsigned long value);
unsigned long long NSSwapLongLong(unsigned long long value);
float NSSwapFloat(float value);
double NSSwapDouble(double value);
unsigned short NSSwapHostShortToBig(unsigned short value);
unsigned short NSSwapHostShortToLittle(unsigned short value);
unsigned int NSSwapHostIntToBig(unsigned int value);
unsigned int NSSwapHostIntToLittle(unsigned int value);
unsigned long NSSwapHostLongToBig(unsigned long value);
unsigned long NSSwapHostLongToLittle(unsigned long value);
unsigned long long NSSwapHostLongLongToBig(unsigned long long value);
unsigned long long NSSwapHostLongLongToLittle(unsigned long long value);
float NSSwapHostFloatToBig(float value);
float NSSwapHostFloatToLittle(float value);
double NSSwapHostDoubleToBig(double value);
double NSSwapHostDoubleToLittle(double value);
unsigned short NSSwapBigShortToHost(unsigned short value);
unsigned short NSSwapLittleShortToHost(unsigned short value);
unsigned int NSSwapBigIntToHost(unsigned int value);
unsigned int NSSwapLittleIntToHost(unsigned int value);
unsigned long NSSwapBigLongToHost(unsigned long value);
unsigned long NSSwapLittleLongToHost(unsigned long value);
unsigned long long NSSwapBigLongLongToHost(unsigned long long value);
unsigned long long NSSwapLittleLongLongToHost(unsigned long long value);
float NSSwapBigFloatToHost(float value);
float NSSwapLittleFloatToHost(float value);
double NSSwapBigDoubleToHost(double value);
double NSSwapLittleDoubleToHost(double value);
unsigned short NSSwapBigShortToLittle(unsigned short value);
unsigned short NSSwapLittleShortToBig(unsigned short value);
unsigned int NSSwapBigIntToLittle(unsigned int value);
unsigned int NSSwapLittleIntToBig(unsigned int value);
unsigned long NSSwapBigLongToLittle(unsigned long value);
unsigned long NSSwapLittleLongToBig(unsigned long value);
unsigned long long NSSwapBigLongLongToLittle(unsigned long long value);
unsigned long long NSSwapLittleLongLongToBig(unsigned long long value);
float NSSwapBigFloatToLittle(float value);
float NSSwapLittleFloatToBig(float value);
double NSSwapBigDoubleToLittle(double value);
double NSSwapLittleDoubleToBig(double value);

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSBYTEORDER_H */
