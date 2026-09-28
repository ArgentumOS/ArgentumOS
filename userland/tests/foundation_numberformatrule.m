/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_numberformatrule — §62.81's acceptance: NSLocalizedNumberFormatRule.
 *
 * ONE unit, importing only <Foundation/Foundation.h>.
 *
 * THE PUBLISHED SURFACE IS ONE DOOR, so the probe checks the four promises a VALUE makes of it: fresh and
 * non-nil, equal to another automatic rule and unequal to anything else, a copy that is a distinct equal object,
 * and a round trip through the archiver with secure coding claimed. The boundary — nothing formats a number with
 * it — is the header's, and the probe does not pretend otherwise.
 */

#import <Foundation/Foundation.h>

#include <stdio.h>

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-NUMBERFORMATRULE %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-NUMBERFORMATRULE %s FAIL %s\n", name,
		       detail != nil ? [detail UTF8String] : "");
	}
}

int main(void)
{
	NSLocalizedNumberFormatRule *rule = [NSLocalizedNumberFormatRule automatic];
	NSLocalizedNumberFormatRule *again = [NSLocalizedNumberFormatRule automatic];

	check("the-automatic-rule-is-a-value-and-fresh-each-call",
	      rule != nil && again != nil && rule != again,
	      [NSString stringWithFormat:@"rule=%p again=%p", (void *)rule, (void *)again]);

	check("two-automatic-rules-are-equal-and-hash-alike",
	      [rule isEqual:again] && [again isEqual:rule] && [rule hash] == [again hash],
	      [NSString stringWithFormat:@"equal=%d hash-equal=%d", (int)[rule isEqual:again],
		(int)([rule hash] == [again hash])]);

	/* NIL IS NOT TESTED HERE ON PURPOSE: -isEqual: is declared with a non-null argument, so passing nil is a
	 * -Wnonnull warning - and this project holds its warning count at zero. What a rule must not equal is
	 * everything that is not a rule. */
	check("the-rule-is-not-equal-to-anything-else",
	      ![rule isEqual:@"not a rule"] && ![rule isEqual:[[NSObject alloc] init]] &&
	      ![rule isEqual:[NSNumber numberWithInt:1]],
	      @"a rule equals a rule and nothing else");

	{
		NSLocalizedNumberFormatRule *copy = [rule copy];

		check("the-rule-copies-as-a-distinct-equal-value",
		      copy != nil && copy != rule && [copy isEqual:rule] && [copy hash] == [rule hash],
		      [NSString stringWithFormat:@"copy=%p rule=%p equal=%d", (void *)copy, (void *)rule,
			(int)[copy isEqual:rule]]);
	}

	{
		id archiveData = [NSKeyedArchiver archivedDataWithRootObject:rule];
		id back = nil;

		if (archiveData != nil) {
			back = [NSKeyedUnarchiver unarchiveObjectWithData:archiveData];
		}
		check("the-rule-round-trips-through-a-keyed-archiver",
		      back != nil && [back isKindOfClass:[NSLocalizedNumberFormatRule class]] &&
		      [back isEqual:rule],
		      [NSString stringWithFormat:@"back=%@ equal=%d", back, (int)[back isEqual:rule]]);
	}

	check("secure-coding-is-claimed",
	      [NSLocalizedNumberFormatRule supportsSecureCoding],
	      [NSString stringWithFormat:@"supportsSecureCoding=%d",
		(int)[NSLocalizedNumberFormatRule supportsSecureCoding]]);

	printf("FOUNDATION-NUMBERFORMATRULE RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-NUMBERFORMATRULE DONE\n");
	return failc == 0 ? 0 : 1;
}
