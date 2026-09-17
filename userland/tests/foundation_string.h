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

/*
 * The class-side `+stringWithFormat:arguments:` list-ownership contract, checked
 * from a unit that does not implement it: the caller OWNS a va_list, hands the
 * method that list, and must still be able to use it afterwards — so a callee
 * that walks its argument has to walk a COPY (C99 7.15.1.4). Returns non-zero only
 * when both legs agree: the same list rendered twice, and a `va_copy`ed list.
 */
int foundation_string_class_arguments_ok(void);

#endif /* FOUNDATION_STRING_H */
