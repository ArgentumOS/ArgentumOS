/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSAutoreleasePool.m — the implementation (W2h).
 */
#include <unistd.h>
#import <Foundation/NSAutoreleasePool.h>
#import <Foundation/NSException.h>
#import <Foundation/NSString.h>
#import <objc/objc-arc.h>

@implementation NSAutoreleasePool

/* THE MARKER: implemented, never called by us, and what keeps the runtime on its fast ARC pool path
 * (see the header). An empty body is correct — the runtime only asks whether the method EXISTS. */
- (void)_ARCCompatibleAutoreleasePool
{
}

- (instancetype)init
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_token = objc_autoreleasePoolPush();
	_discarded = NO;
	return self;
}

/* ENDING THE REGION: -drain and -release are the same operation, which is Cocoa's own statement
 * about a reference-counted pool, and the flag is what makes a second call harmless. */
- (void)drain
{
	if (!_discarded) {
		_discarded = YES;
		objc_autoreleasePoolPop(_token);
	}
}

- (oneway void)release
{
	[self drain];
}

- (void)dealloc
{
	[self drain];
	[super dealloc];
}

- (instancetype)retain
{
	[NSException raise:NSInternalInconsistencyException
		    format:@"Cannot retain an autorelease pool: it bounds a region, and a pool that "
			   "outlived that region would hold objects with no boundary to release them at"];
	return self;
}

- (NSUInteger)retainCount
{
	return 1;
}

- (NSString *)description
{
	return [NSString stringWithFormat:@"<%@: %p>", [self class], (void *)self];
}


+ (void)addObject:(id)object
{
	/* THE LEGACY RUNTIME'S OWN MEANING: add the object to the current pool — which, for a class whose pool IS
	 * a runtime stack entry, is exactly -autorelease. Nothing calls this while the marker keeps libobjc2 on
	 * its ARC path; if that ever changes, an autorelease lands HERE instead of halting the guest. */
	[object autorelease];
}

- (void)addObject:(id)object
{
	/* THE INSTANCE FORM, reading stated: this class is a STACK ENTRY and keeps no object list, so it cannot
	 * target a particular pool. Adding an object to a pool means autoreleasing it into the current region —
	 * the same thing the class-side door does, and what the legacy path asks for. */
	[object autorelease];
}

+ (void)showPools
{
	/* A TRUTHFUL DIAGNOSTIC RATHER THAN A PRETENDED LISTING: pools here ARE the runtime's stack entries and
	 * this class keeps no registry to print. Raw write(2), because a library diagnostic must not depend on
	 * printf. */
	static const char line[] =
		"NSAutoreleasePool: pools are runtime stack entries; this class keeps no registry\n";

	(void)write(2, line, sizeof(line) - 1);
}
@end
