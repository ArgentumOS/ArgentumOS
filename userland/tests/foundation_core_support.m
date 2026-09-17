/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_core, unit 1 of 2 — the subclass and the MRR lifetime exercises.
 *
 * MRR on purpose: this is the regime a root class forces on the files that
 * implement -retain/-release, and it is the half of the probe that can actually
 * send them.
 */

#import "foundation_core.h"

static int deallocs;

@implementation Counter

- (int)value
{
	return _value;
}

- (void)setValue:(int)v
{
	_value = v;
}

- (int)marker
{
	return 4242;
}

- (void)dealloc
{
	deallocs++;
	[super dealloc];	/* MRR: allowed; the root class is the free */
}

@end

int foundation_core_deallocs(void)
{
	return deallocs;
}

int foundation_core_lifecycle(void)
{
	int before = deallocs;
	Counter *c = [[Counter alloc] init];	/* +1 */

	[c setValue:7];
	[c retain];				/* 2 */
	[c release];				/* 1 */
	[c release];				/* 0 -> -dealloc -> object_dispose */
	return deallocs == before + 1;
}

int foundation_core_equality(void)
{
	Counter *a = [[Counter alloc] init];
	Counter *b = [[Counter alloc] init];
	int ok = [a isEqual:a] && ![a isEqual:b] &&
		 [a hash] == [a hash] &&
		 [a hash] == (unsigned long)(uintptr_t)a;

	[a release];
	[b release];
	return ok;
}
