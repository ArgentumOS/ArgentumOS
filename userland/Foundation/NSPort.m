/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSPort.m — the endpoint, minus everything Apple deprecated. The design is in NSPort.h; what is HERE is
 * the ownership and the one ordering that matters: WHAT ORDER A PORT IS TAKEN APART IN.
 *
 *   -invalidate: unregister from the run loop FIRST, then let the subclass close its descriptor, and post
 *                the notification in between. Unregistering first is not tidiness — the run loop is
 *                inside select(2) on that descriptor, and a descriptor closed under a pending watch is
 *                how a loop learns EBADF.
 *   -dealloc:    unregisters and closes, and DOES NOT NOTIFY. A notification whose object is a
 *                deallocating pointer is a use-after-free waiting for the one observer that retains it.
 *
 * AND THE LOOP IS HELD STRONG WHILE THE PORT IS SCHEDULED, while the loop holds the PORT weakly — which
 * looks like a cycle and is not one: the weak link is what breaks it, and it is why the caller must keep
 * its own port alive (Apple says the same thing: "you must explicitly invalidate the port prior to
 * releasing it"). The strong direction is the one that makes `-invalidate` able to undo a registration
 * whose run loop the caller has since dropped.
 */

#import <Foundation/NSPort.h>
#import <Foundation/NSSocketPort.h>
#import <Foundation/NSCoding.h>
#import <Foundation/NSCoder.h>
#import <Foundation/NSException.h>
#import <Foundation/NSNotification.h>
#import <Foundation/NSNotificationCenter.h>
#import <Foundation/NSString.h>

NSString *const NSPortDidBecomeInvalidNotification = @"NSPortDidBecomeInvalidNotification";

@interface NSPort (FNPrivate)
- (void)fnForgetRunLoop;
@end

@implementation NSPort

+ (NSPort *)port
{
	/* AUTORELEASED, as a method not named alloc/new/copy must be under MRC — and NSSocketPort because it
	 * is the only concrete subclass §11.5 left standing (see the header). */
	return [[[NSSocketPort alloc] init] autorelease];
}

- (instancetype)init
{
	self = [super init];
	if (self != nil) {
		_valid = YES;
	}
	return self;
}

- (void)invalidate
{
	if (!_valid) {
		return;		/* idempotent: a teardown path may call it twice, and only one is a change */
	}
	_valid = NO;
	if (_runLoop != nil) {
		/* THROUGH THE SUBCLASS'S DOOR, so that a socket port unregisters its descriptor as well. */
		[self removeFromRunLoop:_runLoop forMode:(_mode != nil ? _mode : NSDefaultRunLoopMode)];
	}
	[[NSNotificationCenter defaultCenter] postNotificationName:NSPortDidBecomeInvalidNotification
							    object:self];
}

- (BOOL)isValid
{
	return _valid;
}

- (void)scheduleInRunLoop:(NSRunLoop *)runLoop forMode:(NSRunLoopMode)mode
{
	/* ONE PAIR IS REMEMBERED, so a second schedule REPLACES the first (the header says so). A repeat of
	 * the SAME pair is not a replacement — -invalidate unregisters and a caller may schedule again. */
	if (_runLoop == runLoop && _mode != nil && [mode isEqualToString:_mode]) {
		return;
	}
	[runLoop retain];
	[_runLoop release];
	_runLoop = runLoop;
	[_mode release];
	_mode = [mode copy];
}

- (void)removeFromRunLoop:(NSRunLoop *)runLoop forMode:(NSRunLoopMode)mode
{
	if (_runLoop != runLoop) {
		return;		/* not the loop this port was scheduled in */
	}
	if (_mode != nil && mode != nil && ![_mode isEqualToString:mode]) {
		return;		/* a different mode: the remembered pair is not this one */
	}
	[self fnForgetRunLoop];
}

/* THE LOOP IS DROPPED IN ONE PLACE, so -removeFromRunLoop:, -invalidate and -dealloc cannot disagree
 * about whether it is still held. */
- (void)fnForgetRunLoop
{
	NSRunLoop *loop = _runLoop;

	_runLoop = nil;
	[_mode release];
	_mode = nil;
	[loop release];
}

- (NSUInteger)reservedSpaceLength
{
	/* ZERO, AND IT IS A REAL ANSWER: the message API that would have needed reserved space is struck, so
	 * there is nothing this port reserves. */
	return 0;
}

- (void)portDidBecomeReadable
{
	/* NOTHING, ON PURPOSE — see the header. A port nobody listens to stays a well-formed run-loop source
	 * (the loop wakes, asks, and is told nothing) rather than one that has to be removed to stay quiet, and
	 * a subclass or a consumer overrides this. */
}

/* Apple documents that a port "conforms to NSCoding, but only supports coding by an NSPortCoder", and
 * NSPortCoder does not exist here — so BOTH DIRECTIONS REFUSE at runtime rather than writing a document
 * that cannot be read back. */
- (instancetype)initWithCoder:(NSCoder *)coder
{
	(void)coder;
	[NSException raise:NSInvalidArgumentException
		    format:@"NSPort does not support archiving: a port is codeable only by an NSPortCoder, "
			   @"which this system does not have"];
	[self release];
	return nil;
}

- (void)encodeWithCoder:(NSCoder *)coder
{
	(void)coder;
	[NSException raise:NSInvalidArgumentException
		    format:@"NSPort does not support archiving: a port is codeable only by an NSPortCoder, "
			   @"which this system does not have"];
}

- (void)dealloc
{
	/* THE ONE THING -dealloc DOES NOT DO IS ANNOUNCE. See this file's header. */
	if (_runLoop != nil) {
		[self removeFromRunLoop:_runLoop forMode:(_mode != nil ? _mode : NSDefaultRunLoopMode)];
	}
	[_mode release];
	[_runLoop release];
	[super dealloc];
}

@end
