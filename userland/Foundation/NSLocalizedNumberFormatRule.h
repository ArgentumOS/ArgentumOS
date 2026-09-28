/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * NSLocalizedNumberFormatRule — HOW A NUMBER IS FORMATTED FOR A LOCALIZATION, AS A VALUE (§62.81).
 *
 * THE PUBLISHED SURFACE IS ONE DOOR, AND THAT IS A FACT ABOUT APPLE'S PAGE RATHER THAN AN ECONOMY OF OURS: the
 * class is documented with `+automatic` and with the two conformances below, and with nothing else. So this
 * header is COMPLETE against what Apple publishes — a rule value, copiable and archivable — and the boundary is
 * the one §62.79's rules state one family over: NOTHING HERE FORMATS A NUMBER. A rule is a thing a formatter
 * WOULD consult; this system's formatters do their own work (see NSNumberFormatter), so the value exists, answers
 * for itself, and drives nothing.
 *
 * `+automatic` ANSWERS A VALUE rather than refusing, on the reading §62.78 and §62.79 both used: Apple's is the
 * rule that lets the system decide how a number reads in a localization, and a system that has such a rule as a
 * thing to name has one — a FRESH instance each call, because a rule is copiable state and a shared one would be
 * a shared answer.
 */

#import <Foundation/NSObject.h>
#import <Foundation/NSCoding.h>

NS_ASSUME_NONNULL_BEGIN

@interface NSLocalizedNumberFormatRule : NSObject <NSCopying, NSSecureCoding>

/* THE AUTOMATIC RULE: "let the system decide", as one value. */
+ (NSLocalizedNumberFormatRule *)automatic;

@end

NS_ASSUME_NONNULL_END
