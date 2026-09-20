/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSByteCountFormatter — a byte count, written the way a person reads one.
 * docs/design/foundation-plan.md §12.3 W11.
 *
 * **THIS IS THE ONE CLASS IN W11 WHOSE ARITHMETIC IS OURS.** Every other formatter in this unit binds a
 * rule to ICU's CLDR data — list patterns, interval patterns, relative dates, date fields. ICU has NO
 * byte-count formatter, so "1.1 MB" has to be computed here, and the division, the rounding and the
 * unit choice are this file's own. What Apple DOES publish is what each count style MEANS, and that is
 * quoted below; what it does not publish is the rendering, which is the part marked as ours.
 *
 * THE FOUR COUNT STYLES, IN APPLE'S OWN WORDS (each page read for this file):
 *
 *   File    "Specifies display of file byte counts. The actual behavior for this is platform-specific;
 *           in macOS 10.8, this uses the decimal style, but that may change over time."
 *   Memory  "…in macOS 10.8, this uses the binary style, but that may change over time."
 *   Decimal "Causes 1000 bytes to be shown as 1 KB."
 *   Binary  "Causes 1024 bytes to be shown as 1 KB."
 *
 * So File → DECIMAL and Memory → BINARY are Apple's own stated behaviour for the two "platform-specific"
 * styles, not our reading of them — which is exactly why the four styles collapse to two divisors here.
 *
 * WHAT IS OURS, said plainly rather than discovered: the NUMBER OF FRACTION DIGITS (one, rounded, and
 * dropped when it is zero unless zeroPadsFractionDigits asks for it), the digit GROUPING in the actual
 * count (we do not group; Apple's own output groups), and `adaptive`'s behaviour (Apple publishes the
 * property's name and the sentence "Determines the display style of the size representation" and no
 * more — see the accessor's note for the reading this file implements).
 *
 * THE UNITS ARE A BITMASK and `UseDefault` is ZERO, which is why the "no unit chosen" case is spelled
 * 0: Apple's page says so ("This causes default units appropriate for the platform to be used. This is
 * the default."), and a mask of zero cannot be a bit in the same mask as UseBytes.
 *
 * NOT HERE, NAMED: the two NSMeasurement-taking members (`-stringFromMeasurement:` and
 * `+stringFromMeasurement:countStyle:`). `NSMeasurement` and the `NSUnit*` family are W12's, so those
 * two rows stay open on the ledger and the probe asserts their absence as a WORK ITEM rather than
 * describing them as a boundary (§11's rule).
 */

#ifndef FOUNDATION_NSBYTECOUNTFORMATTER_H
#define FOUNDATION_NSBYTECOUNTFORMATTER_H

#import <Foundation/NSFormatter.h>

@class NSString;

NS_ASSUME_NONNULL_BEGIN

/* Apple's bit positions: one bit per unit, and a RANGE of bits for "YB or higher" because the family
 * has no name for what comes after the last SI prefix. */
typedef unsigned long NSByteCountFormatterUnits;

#define NSByteCountFormatterUseDefault		(0UL)
#define NSByteCountFormatterUseBytes		(1UL << 0)
#define NSByteCountFormatterUseKB		(1UL << 1)
#define NSByteCountFormatterUseMB		(1UL << 2)
#define NSByteCountFormatterUseGB		(1UL << 3)
#define NSByteCountFormatterUseTB		(1UL << 4)
#define NSByteCountFormatterUsePB		(1UL << 5)
#define NSByteCountFormatterUseEB		(1UL << 6)
#define NSByteCountFormatterUseZB		(1UL << 7)
#define NSByteCountFormatterUseYBOrHigher	(0x0FFUL << 8)
#define NSByteCountFormatterUseAll		(0x0FFFFUL)

/* The raw values are Apple's, so a stored integer keeps its meaning across a boundary. */
typedef enum {
	NSByteCountFormatterCountStyleFile = 0,
	NSByteCountFormatterCountStyleMemory = 1,
	NSByteCountFormatterCountStyleDecimal = 2,
	NSByteCountFormatterCountStyleBinary = 3
} NSByteCountFormatterCountStyle;

@interface NSByteCountFormatter : NSFormatter
{
	NSByteCountFormatterUnits _allowedUnits;
	NSByteCountFormatterCountStyle _countStyle;
	NSFormattingContext _formattingContext;
	BOOL _allowsNonnumericFormatting;
	BOOL _includesActualByteCount;
	BOOL _adaptive;
	BOOL _includesCount;
	BOOL _includesUnit;
	BOOL _zeroPadsFractionDigits;
}

/* The whole operation with no object to configure: this style, these defaults. */
+ (nullable NSString *)stringFromByteCount:(long long)byteCount
				 countStyle:(NSByteCountFormatterCountStyle)countStyle;

/* The same, using the receiver's settings. */
- (nullable NSString *)stringFromByteCount:(long long)byteCount;

/* NSFormatter's door: an NSNumber is a byte count and is formatted with the receiver's settings. An
 * object of any other class answers nil — "not my kind of value", the same convention
 * NSListFormatter's door uses. (The NSMeasurement branch Apple's page mentions rides W12.) */
- (nullable NSString *)stringForObjectValue:(nullable id)object;

/* Where the text will appear in a sentence. Declared on the SUBCLASSES, not on NSFormatter — Apple's
 * NSFormatter page has no such member, which was worth measuring rather than assuming. */
- (NSFormattingContext)formattingContext;
- (void)setFormattingContext:(NSFormattingContext)context;

/* The number of bytes to be used for kilobytes: see the four styles quoted above. */
- (NSByteCountFormatterCountStyle)countStyle;
- (void)setCountStyle:(NSByteCountFormatterCountStyle)style;

/* The units that can be used in the output. `UseDefault` (zero) means any of them. */
- (NSByteCountFormatterUnits)allowedUnits;
- (void)setAllowedUnits:(NSByteCountFormatterUnits)units;

/* Allow more natural display of some values: zero prints as "Zero KB" rather than "0 bytes". */
- (BOOL)allowsNonnumericFormatting;
- (void)setAllowsNonnumericFormatting:(BOOL)value;

/* Append the number of bytes after the formatted string — "1.1 MB (1,234,567 bytes)" is Apple's own
 * example of the shape; ours drops the grouping (see the file header). */
- (BOOL)includesActualByteCount;
- (void)setIncludesActualByteCount:(BOOL)value;

/* THE ONE PROPERTY WHOSE BEHAVIOUR APPLE DOES NOT PUBLISH: its page says only "Determines the display
 * style of the size representation". The reading implemented here is the one that sentence supports
 * and that changes something observable: with adaptive YES the unit is chosen by MAGNITUDE EVEN IF
 * `allowedUnits` excludes it, so allowedUnits becomes advisory; with adaptive NO (the default)
 * allowedUnits gates the choice, which is what its own page says it does ("Specifying any units
 * explicitly causes just those units to be used"). Documented here and in §11.6's register rather than
 * left as a silent no-op. */
- (BOOL)isAdaptive;
- (void)setAdaptive:(BOOL)value;

/* Whether the count itself appears, and whether the unit does. Each can be suppressed on its own, which
 * is the only way to ask for "MB" or for "1.1" alone. */
- (BOOL)includesCount;
- (void)setIncludesCount:(BOOL)value;
- (BOOL)includesUnit;
- (void)setIncludesUnit:(BOOL)value;

/* Zero pad the fraction digits so a representation keeps a constant width: 1 MB becomes "1.0 MB". */
- (BOOL)zeroPadsFractionDigits;
- (void)setZeroPadsFractionDigits:(BOOL)value;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSBYTECOUNTFORMATTER_H */
