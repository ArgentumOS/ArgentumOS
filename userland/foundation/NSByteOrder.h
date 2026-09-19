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

#import <foundation/NSObjCRuntime.h>

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

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSBYTEORDER_H */
