/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_value — F2's acceptance probe (docs/design/foundation-plan.md).
 *
 * Two translation units again. The support unit BUILDS one value of each kind,
 * which is the point: a boxed number, a data buffer and a date are ordinary
 * objects, so one made in another unit has to compare equal to one made here.
 */

#ifndef FOUNDATION_VALUE_H
#define FOUNDATION_VALUE_H

#import <Foundation/Foundation.h>

NSNumber *foundation_value_number(void);
NSData *foundation_value_data(void);
NSDate *foundation_value_date(void);

#endif /* FOUNDATION_VALUE_H */
