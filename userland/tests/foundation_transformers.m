/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_transformers.m — THE PROBE FOR §62.96: the five value-transformer names Apple registers, which
 * close the value-transformer family.
 *
 * WHAT IT ASSERTS, AND WHY EACH ONE IS WORTH THE CALLS IT COSTS:
 *   * THE NAMES ARE IN THE REGISTRY, not merely resolvable: `+valueTransformerNames` is the documented door and
 *     it must LIST them. That is the check that separates "registered" from "found by the class-name fallback",
 *     which is the difference this library's own registry makes possible and the reason these five could not be
 *     done the way the one transformer shipped before them was.
 *   * EACH TRANSFORMER ANSWERS BY VALUE, on both sides of its question: the nil pair both ways, the negator
 *     both ways, so a transformer that answered a constant would fail.
 *   * THE NEGATOR'S STATED DOMAIN IS ITS BOUNDARY: a Boolean is what the name promises, so a string answers
 *     nil — the base class's own contract for a value that cannot be transformed — and the probe asserts that
 *     rather than leaving "it coerced something" undiscovered.
 *   * THE TWO UNARCHIVERS ROUND-TRIP A REAL ARCHIVE, through the archiver that pairs with each: the keyed one
 *     through NSKeyedArchiver, the sequential one through NSArchiver. A name that answered nil would "work"
 *     for a caller who only checked for nil.
 *   * AND THE TWO NAME STRINGS ARE PINNED, because they are this library's choice (D2): a nib or a model that
 *     spells @"NSIsNil" is spelling a string nobody publishes, so the value is asserted here and cannot drift.
 *     The deprecated `NSKeyedUnarchiveFromDataTransformerName` is ONE of the five — the row the ledger owed.
 */

#import <Foundation/Foundation.h>
#include <stdio.h>

static int okc = 0, failc = 0;

static void check(const char *name, BOOL held, NSString *why)
{
	if (held) {
		okc++;
		printf("FOUNDATION-TRANSFORMERS %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-TRANSFORMERS %s FAIL: %s\n", name, [why UTF8String]);
	}
}

/* EVERY TRANSFORM IS ASKED THE SAME WAY, so a check reads as a name and an expected answer. */
static id fn_transformed(NSValueTransformerName name, id value)
{
	NSValueTransformer *t = [NSValueTransformer valueTransformerForName:name];

	return (t != nil) ? [t transformedValue:value] : nil;
}

static BOOL fn_is_bool(id object, BOOL want)
{
	return [object isKindOfClass:[NSNumber class]] && [object boolValue] == want;
}

int main(void)
{
	/* 1. THE REGISTRATION ITSELF. */
	{
		NSArray *names = [NSValueTransformer valueTransformerNames];
		NSArray *want = @[ NSIsNilTransformerName, NSIsNotNilTransformerName,
				   NSNegateBooleanTransformerName, NSKeyedUnarchiveFromDataTransformerName,
				   NSUnarchiveFromDataTransformerName ];
		NSMutableArray *missing = [NSMutableArray array];
		unsigned long i;

		for (i = 0; i < [want count]; i++) {
			if (![names containsObject:[want objectAtIndex:i]]) {
				[missing addObject:[want objectAtIndex:i]];
			}
		}
		check("the-five-names-are-in-the-registry",
		      [missing count] == 0,
		      [NSString stringWithFormat:@"registered names are %@; missing %@", names, missing]);
	}

	/* 2. THE NIL PAIR. */
	check("the-is-nil-pair-answers-about-nil",
	      fn_is_bool(fn_transformed(NSIsNilTransformerName, nil), YES) &&
	      fn_is_bool(fn_transformed(NSIsNilTransformerName, @1), NO) &&
	      fn_is_bool(fn_transformed(NSIsNotNilTransformerName, nil), NO) &&
	      fn_is_bool(fn_transformed(NSIsNotNilTransformerName, @1), YES),
	      @"nil answers YES to 'is nil' and NO to 'is not nil', and a value answers the other way");

	/* 3. THE NEGATOR, BOTH WAYS, AND ITS BOUNDARY. */
	{
		id string = fn_transformed(NSNegateBooleanTransformerName, @"not a boolean");

		check("the-negator-negates-a-boolean-and-refuses-what-is-not-one",
		      fn_is_bool(fn_transformed(NSNegateBooleanTransformerName, @YES), NO) &&
		      fn_is_bool(fn_transformed(NSNegateBooleanTransformerName, @NO), YES) &&
		      string == nil,
		      @"@YES answers @NO and @NO answers @YES, and a string answers nil - 'a Boolean' is the "
		      @"domain the name promises");
	}

	/* 4. THE TWO UNARCHIVERS, EACH THROUGH THE ARCHIVER THAT PAIRS WITH IT. */
	{
		NSDictionary *source = @{ @"a" : @1, @"b" : @"two" };
		NSData *keyed = [NSKeyedArchiver archivedDataWithRootObject:source];
		NSData *plain = [NSArchiver archivedDataWithRootObject:source];
		id fromKeyed = fn_transformed(NSKeyedUnarchiveFromDataTransformerName, keyed);
		id fromPlain = fn_transformed(NSUnarchiveFromDataTransformerName, plain);

		check("the-two-unarchivers-round-trip-their-own-archive",
		      keyed != nil && plain != nil &&
		      fromKeyed != nil && [fromKeyed isEqual:source] &&
		      fromPlain != nil && [fromPlain isEqual:source] &&
		      fn_transformed(NSKeyedUnarchiveFromDataTransformerName, @"not data") == nil &&
		      fn_transformed(NSUnarchiveFromDataTransformerName, @"not data") == nil,
		      @"a dictionary round-trips through both, and input that is not data answers nil");
	}

	/* 5. THE NAME STRINGS, WHICH ARE THIS LIBRARY'S (D2) AND THEREFORE PINNED HERE. */
	check("the-name-strings-are-the-transformer-names",
	      [NSIsNilTransformerName isEqualToString:@"NSIsNil"] &&
	      [NSIsNotNilTransformerName isEqualToString:@"NSIsNotNil"] &&
	      [NSNegateBooleanTransformerName isEqualToString:@"NSNegateBoolean"] &&
	      [NSKeyedUnarchiveFromDataTransformerName isEqualToString:@"NSKeyedUnarchiveFromData"] &&
	      [NSUnarchiveFromDataTransformerName isEqualToString:@"NSUnarchiveFromData"],
	      @"a nib or a model spells these as literals, so the values are asserted rather than assumed");

	printf("FOUNDATION-TRANSFORMERS RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-TRANSFORMERS-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-TRANSFORMERS DONE\n");
	return failc ? 1 : 0;
}
