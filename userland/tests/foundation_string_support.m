/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_string, unit 1 of 2 — the support unit (MRR).
 */

#import "foundation_string.h"

@implementation NamedThing

- (NSString *)description
{
	/* Long enough (23 characters) to be an OBJECT rather than a tagged pointer. */
	return @"a NamedThing, thank you";
}

@end

NSString *foundation_string_constant(void)
{
	/* 4 characters: fewer than 9, so clang emits a TAGGED pointer. */
	return @"supt";
}
