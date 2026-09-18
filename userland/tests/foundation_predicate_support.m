/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_predicate, unit 1 of 2 — the fixtures (MRR). docs/design/foundation-plan.md, F11a.
 *
 * The counting predicate lives HERE so that the short-circuit check is a real measurement across
 * a translation unit: the check unit cannot see this counter except through the accessor, so a
 * zero cannot be an accident of its own bookkeeping.
 */

#import "foundation_predicate.h"

static int support_calls;
static BOOL support_answer;

NSArray * _Nullable foundation_predicate_numbers(void)
{
	return @[@1, @2, @3, @4];
}

NSPredicate * _Nullable foundation_predicate_even(void)
{
	return [NSPredicate predicateWithBlock:^BOOL(id object, NSDictionary *bindings) {
		(void)bindings;
		return [object intValue] % 2 == 0;
	}];
}

NSPredicate * _Nullable foundation_predicate_counting(BOOL answer)
{
	/* The answer is stored FILE-SCOPE, so this block CAPTURES NOTHING. That is deliberate: a
	 * capturing block literal built in this unit (-fno-objc-arc) and stored by the library (an
	 * ARC unit) is a combination worth isolating, and it is what the first version of this
	 * fixture faulted on. */
	support_answer = answer;
	return [NSPredicate predicateWithBlock:^BOOL(id object, NSDictionary *bindings) {
		(void)object;
		(void)bindings;
		support_calls++;
		return support_answer;
	}];
}

void foundation_predicate_reset_calls(void)
{
	support_calls = 0;
}

int foundation_predicate_calls(void)
{
	return support_calls;
}
