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
	/* BY NAME, not by pointer: the runtime unifies selectors by name, but the
	 * selector this is handed is the CALL SITE's typed registration, and a `==`
	 * against this translation unit's @selector() is comparing two spellings of the
	 * same name rather than the same word. */
	if (sel_isEqual(aSelector, @selector(marker))) {
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

/* ================================ THE MRR SIDE ================================ */

/* A POOL REFUSES -retain, which is Apple's own diagnostic and the reason a pool cannot outlive the
 * region whose objects it holds. An ARC translation unit cannot even write this call. */
BOOL foundation_mrr_pool_refuses_retain(void)
{
	NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
	BOOL refused = NO;

	@try {
		(void)[pool retain];
	} @catch (NSException *e) {
		refused = YES;
	}
	[pool drain];
	return refused;
}

/* A DRAINED POOL RELEASES WHAT IT HELD: the observable is a DEALLOC, so the fixture counts its own.
 * -autorelease is written as the MESSAGE here, which is what the library implements. */
static int fn_mrr_released = 0;

@interface FnMrrPooled : NSObject
@end

@implementation FnMrrPooled
- (void)dealloc
{
	fn_mrr_released++;
	[super dealloc];
}
@end

BOOL foundation_mrr_pool_releases_on_drain(void)
{
	NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
	FnMrrPooled *held = [[[FnMrrPooled alloc] init] autorelease];

	(void)held;
	if (fn_mrr_released != 0) {
		return NO;	/* held while the pool is open */
	}
	[pool drain];
	return fn_mrr_released == 1;
}

/* A PROXY ANSWERS A MESSAGE IT DOES NOT IMPLEMENT BY FORWARDING IT to the object it stands for, with
 * the argument and the RETURN VALUE intact. This fixture implements the two methods a proxy must and
 * nothing else, so everything else goes through the runtime's forwarding. */
@interface FnMrrProxy : NSProxy
{
	id _target;
}
- (instancetype)initWithTarget:(id)target;
@end

@implementation FnMrrProxy
- (instancetype)initWithTarget:(id)target
{
	_target = target;
	return self;
}

- (NSMethodSignature *)methodSignatureForSelector:(SEL)selector
{
	return [_target methodSignatureForSelector:selector];
}

- (void)forwardInvocation:(NSInvocation *)invocation
{
	[invocation setTarget:_target];
	[invocation invoke];
}
@end

BOOL foundation_mrr_proxy_forwards(void)
{
	NSString *real = @"forwarded";
	FnMrrProxy *proxy = [[FnMrrProxy alloc] initWithTarget:real];
	NSUInteger length = [(id)proxy length];
	BOOL same = [(id)proxy isEqualToString:@"forwarded"];

	return (length == 10) && same;
}
