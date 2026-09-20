/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_predicate — the two units' shared surface. docs/design/foundation-plan.md, F11a.
 *
 * The support unit imports ONLY <Foundation/Foundation.h>, so this probe is also what proves
 * NSPredicate reached the umbrella. And it builds one of the predicates itself, which is the
 * family's claim under test: a predicate is a VALUE, so one made over there answers over here.
 */

#ifndef FOUNDATION_PREDICATE_H
#define FOUNDATION_PREDICATE_H

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/* The fixtures. Each is nullable because the constructors are (the house's rule). */
NSArray * _Nullable foundation_predicate_numbers(void);
NSPredicate * _Nullable foundation_predicate_even(void);

/* A predicate whose block COUNTS its own calls — built on this side so the count is a real
 * cross-unit measurement rather than an artefact of either unit's locals. */
NSPredicate * _Nullable foundation_predicate_counting(BOOL answer);
void foundation_predicate_reset_calls(void);
int foundation_predicate_calls(void);

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_PREDICATE_H */
