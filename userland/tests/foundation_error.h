/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_error — F4's acceptance probe (docs/design/foundation-plan.md).
 *
 * Two translation units again; the support unit builds an error and an exception
 * from OUTSIDE the unit that defines them, which is the cheapest way to show they
 * are ordinary objects.
 */

#ifndef FOUNDATION_ERROR_H
#define FOUNDATION_ERROR_H

#import <Foundation/Foundation.h>

NSError *foundation_error_built(void);
NSException *foundation_error_exception(void);

#endif /* FOUNDATION_ERROR_H */
