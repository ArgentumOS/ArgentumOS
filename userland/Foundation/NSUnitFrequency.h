/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUnitFrequency — nine units against the hertz. docs/design/foundation-plan.md §12.3 W12.
 *
 * THE FAMILY WITH A UNIT THAT IS NOT A PREFIX OF THE BASE: `framesPerSecond` IS the hertz under the name a
 * display uses, so its coefficient is exactly 1 and the pair converts without arithmetic. That is worth
 * saying because it is the kind of alias a hand-written table tends to give its own coefficient to, and
 * then to get slightly wrong.
 */

#ifndef FOUNDATION_NSUNITFREQUENCY_H
#define FOUNDATION_NSUNITFREQUENCY_H

#import <Foundation/NSDimension.h>

NS_ASSUME_NONNULL_BEGIN

@interface NSUnitFrequency : NSDimension

+ (NSUnitFrequency *)terahertz;
+ (NSUnitFrequency *)gigahertz;
+ (NSUnitFrequency *)megahertz;
+ (NSUnitFrequency *)kilohertz;
+ (NSUnitFrequency *)hertz;
+ (NSUnitFrequency *)millihertz;
+ (NSUnitFrequency *)microhertz;
+ (NSUnitFrequency *)nanohertz;
+ (NSUnitFrequency *)framesPerSecond;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSUNITFREQUENCY_H */
