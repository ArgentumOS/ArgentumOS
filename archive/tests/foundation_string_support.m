/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_string, unit 1 of 2 — the support unit (MRR).
 */

#import "foundation_string.h"

#include <stdio.h>

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

/*
 * THE `va_list` CONTRACT AT A `...:arguments:` ENTRY POINT, and it is C99
 * 7.15.1.4 read carefully — which the F4 record got wrong in BOTH directions:
 *
 *  - a function that takes a `va_list` CONSUMES it. That is the standard's model
 *    (it is what `vsnprintf` does), and it is not a defect in this library;
 *  - so the CALLER has to hand over a list it will not need again — a `va_copy` —
 *    rather than its own list. `+[NSException raise:format:arguments:]` does
 *    exactly that, which is why the shipped path is sound.
 *
 * The check renders through a handed-over COPY and requires the owner's list to
 * still work in the same call: the usage the standard promises, and the one the
 * F4 crash's caller got wrong. Its real value is simpler than either theory: the
 * class-side form had NO caller in any probe, so this CALLS it and asserts the
 * rendering at last.
 *
 * WHAT THIS CHECK COST, kept because both halves are traps:
 *  - its FIRST version handed over a copy and required two renders to agree —
 *    fine, but it can never fail: a copy is an INDEPENDENT tag, so a destructive
 *    callee cannot touch it. Measured: it passed against a library believed (then)
 *    to be consuming its argument.
 *  - its SECOND version handed over the caller's OWN list and required it to
 *    survive — asserting something the standard does NOT promise. It failed 16/17
 *    against the shipped library, and that "failure" was the CHECK being wrong,
 *    not the library: a callee is entitled to consume the list it was given.
 *    (Making the library non-destructive instead — an internal `va_copy` — made
 *    the probe FAULT: a jump to an unmapped address, every register zero.
 *    Measured, and reverted: `va_copy` inside a method whose `va_list` parameter
 *    has already decayed to a pointer is not a form this code should rely on.)
 *
 * `%d-%@` covers the object conversion, with a REAL string rather than a literal so this
 * leg does not also depend on the tagged representation — the dependency that was
 * SUSPECTED of the F4 fault and then cleared by evidence: the shipped `string-format`
 * check had a tagged `%@` all along and passed in the very run that faulted (see the
 * plan; the probe now names that property too).
 */
static int class_arguments_through_copy(NSString *format, ...)
{
	va_list owner;
	va_list handed;
	NSString *first;
	NSString *second;
	NSString *expected = [NSString stringWithUTF8String:"7-x"];
	int ok;

	va_start(owner, format);
	va_copy(handed, owner);
	first = [NSString stringWithFormat:format arguments:handed];
	va_end(handed);
	second = [NSString stringWithFormat:format arguments:owner];
	va_end(owner);

	ok = [first isEqualToString:expected] && [second isEqualToString:expected];
	if (!ok) {
		printf("FOUNDATION-STRING class-arguments: the handed-over copy rendered "
		       "\"%s\", the owner's list then rendered \"%s\", expected \"7-x\"\n",
		       [first UTF8String], [second UTF8String]);
	}
	return ok;
}

int foundation_string_class_arguments_ok(void)
{
	NSString *real = [NSString stringWithUTF8String:"x"];

	return class_arguments_through_copy(@"%d-%@", 7, real);
}
