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

/* --- the forwarding probes (stage F, second half) ---------------------- */

@implementation FastForwarder

- (id)init
{
	self = [super init];
	if (self != nil) {
		_backing = [[Counter alloc] init];	/* MRR: we own it */
	}
	return self;
}

- (void)dealloc
{
	[_backing release];
	[super dealloc];
}

/*
 * THE FAST PATH. The runtime asks this before it hands a call over, and re-looks
 * the selector up on the answer — so the message reaches the real object with its
 * arguments untouched and no invocation is ever built.
 */
- (id)forwardingTargetForSelector:(SEL)aSelector
{
	if (aSelector == @selector(marker)) {
		return _backing;
	}
	return nil;
}

@end

@implementation SlowForwarder

- (id)init
{
	self = [super init];
	if (self != nil) {
		_backing = [[Counter alloc] init];
	}
	return self;
}

- (void)dealloc
{
	[_backing release];
	[super dealloc];
}

- (unsigned long)forwardedCount
{
	return _forwarded;
}

/*
 * THE SLOW PATH, the one that has to MARSHAL. The call arrives as an NSInvocation
 * whose arguments were captured from the register file by ninvoke_amd64.S; calling
 * the method for real — here, on the backing object — is what -invokeWithTarget:
 * does, and it is the whole point of the mechanism.
 */
- (void)forwardInvocation:(NSInvocation *)anInvocation
{
	_forwarded++;
	[anInvocation invokeWithTarget:_backing];
}

@end
