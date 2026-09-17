/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_string — F1's acceptance probe (docs/design/foundation-plan.md).
 *
 * TWO TRANSLATION UNITS, for the reason the other probes have two: one of the
 * constant strings has to come from ANOTHER unit (so that whichever path clang
 * takes for it — tagged or object — is exercised from outside the unit that
 * defines the classes), and the class whose -description is overridden lives
 * there too.
 */

#ifndef FOUNDATION_STRING_H
#define FOUNDATION_STRING_H

#import <foundation/Foundation.h>

/* A class that overrides the root class's -description. */
@interface NamedThing : NSObject
- (NSString *)description;
@end

/* A constant string from the support unit: 4 characters, so it is a TAGGED one. */
NSString *foundation_string_constant(void);

#endif /* FOUNDATION_STRING_H */
