/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUnitPower — eleven units against the watt. docs/design/foundation-plan.md §12.3 W12.
 *
 * THE LARGEST RANGE IN THE SET: ten SI multiples from tera- to femto-, and ONE unit that is a definition
 * from another system. Mechanical horsepower IS 550 foot-pounds-force per second, so its coefficient is
 * written as exactly that product rather than as 745.6998715822702 — and 550, 0.3048 (the foot) and
 * 4.4482216152605 (the pound-force in newtons) are all definitions, which is why the probe can assert the
 * relation rather than a decimal.
 */

#ifndef FOUNDATION_NSUNITPOWER_H
#define FOUNDATION_NSUNITPOWER_H

#import <Foundation/NSDimension.h>

NS_ASSUME_NONNULL_BEGIN

@interface NSUnitPower : NSDimension

+ (NSUnitPower *)terawatts;
+ (NSUnitPower *)gigawatts;
+ (NSUnitPower *)megawatts;
+ (NSUnitPower *)kilowatts;
+ (NSUnitPower *)watts;
+ (NSUnitPower *)milliwatts;
+ (NSUnitPower *)microwatts;
+ (NSUnitPower *)nanowatts;
+ (NSUnitPower *)picowatts;
+ (NSUnitPower *)femtowatts;
+ (NSUnitPower *)horsepower;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSUNITPOWER_H */
