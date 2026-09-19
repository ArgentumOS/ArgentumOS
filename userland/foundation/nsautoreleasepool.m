/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * nsautoreleasepool.m — the implementation (W2h).
 */
#import <foundation/NSAutoreleasePool.h>
#import <foundation/NSException.h>
#import <foundation/NSString.h>
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

@end
