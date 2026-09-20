/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUnitInformationStorage — the information units, and the first concrete dimension.
 * docs/design/foundation-plan.md §12.3 W12 (first slice).
 *
 * THIRTY-FIVE UNITS IN THREE GROUPS, which is Apple's own grouping on its page: three COMMON units
 * (bits, nibbles, bytes), sixteen BINARY (the -ibi- family, 1024-based) and sixteen DECIMAL (the SI
 * prefixes, 1000-based), each in a bit/byte pair. The pairing is the point of the family: `kibibytes` and
 * `kilobytes` differ by 2.4%, which is the difference between what a drive holds and what a reader
 * expects, and the class exists so that difference is expressible rather than argued about.
 *
 * **THE BASE UNIT IS BITS, AND APPLE DOES NOT PUBLISH IT.** `+baseUnit` is declared on NSDimension and
 * each family answers its own; Apple's page for this class does not say which, so this file CHOOSES — and
 * the reason is arithmetic rather than taste: with bits as the base, EVERY coefficient in the family is an
 * integer (1 nibble = 4 bits, 1 byte = 8, 1 kB = 8000, 1 KiB = 8192), while with bytes as the base a
 * kilobit would be 125 bytes and the SI prefix would stop being a round number. Nothing a caller can
 * observe changes as a result — a conversion is a ratio either way — so this is a choice rather than a
 * deviation.
 *
 * **THE SYMBOLS ARE ALSO OURS**, and for the same reason: Apple publishes the units and their RATIOS and
 * not the strings a `+bytes` carries. The spellings here are the conventional short ones ("B", "kB",
 * "KiB", "kbit", "Kibit"), and the probe pins them so they cannot drift silently.
 */

#ifndef FOUNDATION_NSUNITINFORMATIONSTORAGE_H
#define FOUNDATION_NSUNITINFORMATIONSTORAGE_H

#import <Foundation/NSDimension.h>

NS_ASSUME_NONNULL_BEGIN

@interface NSUnitInformationStorage : NSDimension

/* Common units. */
+ (NSUnitInformationStorage *)bits;
+ (NSUnitInformationStorage *)nibbles;
+ (NSUnitInformationStorage *)bytes;

/* The binary family: 1024-based, the -ibi- prefixes. */
+ (NSUnitInformationStorage *)kibibits;
+ (NSUnitInformationStorage *)kibibytes;
+ (NSUnitInformationStorage *)mebibits;
+ (NSUnitInformationStorage *)mebibytes;
+ (NSUnitInformationStorage *)gibibits;
+ (NSUnitInformationStorage *)gibibytes;
+ (NSUnitInformationStorage *)tebibits;
+ (NSUnitInformationStorage *)tebibytes;
+ (NSUnitInformationStorage *)pebibits;
+ (NSUnitInformationStorage *)pebibytes;
+ (NSUnitInformationStorage *)exbibits;
+ (NSUnitInformationStorage *)exbibytes;
+ (NSUnitInformationStorage *)zebibits;
+ (NSUnitInformationStorage *)zebibytes;
+ (NSUnitInformationStorage *)yobibits;
+ (NSUnitInformationStorage *)yobibytes;

/* The decimal family: 1000-based, the SI prefixes. */
+ (NSUnitInformationStorage *)kilobits;
+ (NSUnitInformationStorage *)kilobytes;
+ (NSUnitInformationStorage *)megabits;
+ (NSUnitInformationStorage *)megabytes;
+ (NSUnitInformationStorage *)gigabits;
+ (NSUnitInformationStorage *)gigabytes;
+ (NSUnitInformationStorage *)terabits;
+ (NSUnitInformationStorage *)terabytes;
+ (NSUnitInformationStorage *)petabits;
+ (NSUnitInformationStorage *)petabytes;
+ (NSUnitInformationStorage *)exabits;
+ (NSUnitInformationStorage *)exabytes;
+ (NSUnitInformationStorage *)zettabits;
+ (NSUnitInformationStorage *)zettabytes;
+ (NSUnitInformationStorage *)yottabits;
+ (NSUnitInformationStorage *)yottabytes;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSUNITINFORMATIONSTORAGE_H */
