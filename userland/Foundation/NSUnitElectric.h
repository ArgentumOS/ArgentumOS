/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * The four electrical families, in one header — and this is a deliberate grouping rather than a shortcut.
 * docs/design/foundation-plan.md §12.3 W12.
 *
 * THEY ARE ONE SUBJECT AND THEY INTERLOCK: charge is current times time, potential difference drives current
 * through resistance, and each family's coefficient is a power of ten against an SI base. Apple gives each
 * its own page, and a caller who wants one of them almost always wants the others (a battery is a charge and
 * a voltage; a circuit is all four).
 *
 * THE BASES ARE THE SI UNITS AND NONE IS A CHOICE: the ampere, the coulomb, the volt and the ohm. The
 * amp-hour units are the one derivation here — a charge is a current for a time, so an ampere-hour is
 * exactly 3600 coulombs and a kiloampere-hour is 3.6e6.
 */

#ifndef FOUNDATION_NSUNITELECTRIC_H
#define FOUNDATION_NSUNITELECTRIC_H

#import <Foundation/NSDimension.h>

NS_ASSUME_NONNULL_BEGIN

@interface NSUnitElectricCurrent : NSDimension
+ (NSUnitElectricCurrent *)megaamperes;
+ (NSUnitElectricCurrent *)kiloamperes;
+ (NSUnitElectricCurrent *)amperes;
+ (NSUnitElectricCurrent *)milliamperes;
+ (NSUnitElectricCurrent *)microamperes;
@end

@interface NSUnitElectricCharge : NSDimension
+ (NSUnitElectricCharge *)coulombs;
+ (NSUnitElectricCharge *)megaampereHours;
+ (NSUnitElectricCharge *)kiloampereHours;
+ (NSUnitElectricCharge *)ampereHours;
+ (NSUnitElectricCharge *)milliampereHours;
+ (NSUnitElectricCharge *)microampereHours;
@end

@interface NSUnitElectricPotentialDifference : NSDimension
+ (NSUnitElectricPotentialDifference *)megavolts;
+ (NSUnitElectricPotentialDifference *)kilovolts;
+ (NSUnitElectricPotentialDifference *)volts;
+ (NSUnitElectricPotentialDifference *)millivolts;
+ (NSUnitElectricPotentialDifference *)microvolts;
@end

@interface NSUnitElectricResistance : NSDimension
+ (NSUnitElectricResistance *)megaohms;
+ (NSUnitElectricResistance *)kiloohms;
+ (NSUnitElectricResistance *)ohms;
+ (NSUnitElectricResistance *)milliohms;
+ (NSUnitElectricResistance *)microohms;
@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSUNITELECTRIC_H */
