/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSPurgeableData.m — W13c. See the header for the handshake.
 *
 * THE DISCARD GOES THROUGH THE SUPERCLASS'S OWN `-setLength:`, not through this class's override: the
 * override is the WRITE path, and a discard is the opposite of a write. Calling `[super setLength:0]` and
 * then setting the flag is what keeps those two events from being confused with each other.
 *
 * MANUAL OWNERSHIP (MRC: the whole library is).
 */

#import <Foundation/NSPurgeableData.h>

@implementation NSPurgeableData

- (instancetype)init
{
	self = [super init];
	if (self != nil) {
		_accessCount = 0;
		_contentDiscarded = NO;
	}
	return self;
}

- (instancetype)initWithCapacity:(NSUInteger)capacity
{
	self = [super initWithCapacity:capacity];
	if (self != nil) {
		_accessCount = 0;
		_contentDiscarded = NO;
	}
	return self;
}

/* ---- NSDiscardableContent ---- */

- (BOOL)beginContentAccess
{
	if (_contentDiscarded) {
		/* THE ANSWER IS THE INSTRUCTION: the bytes are gone, so the caller builds them again. The flag is
		 * NOT cleared here — it is cleared by the write that recreates them — so a second access before
		 * that write is told the same thing. */
		return NO;
	}
	_accessCount++;
	return YES;
}

- (void)endContentAccess
{
	if (_accessCount > 0) {
		_accessCount--;
	}
}

- (void)discardContentIfPossible
{
	/* AN OUTSTANDING ACCESS REFUSES THE DISCARD, which is the entire reason the count exists. */
	if (_accessCount > 0 || _contentDiscarded) {
		return;
	}
	/* THE SUPERCLASS'S SETTER, deliberately: see the file comment. */
	[super setLength:0];
	_contentDiscarded = YES;
}

- (BOOL)isContentDiscarded
{
	return _contentDiscarded;
}

/* ---- the write path, which is what RECREATES a discarded content ---- */

- (void)setLength:(size_t)length
{
	if (length > 0) {
		_contentDiscarded = NO;
	}
	[super setLength:length];
}

- (void)appendBytes:(const void *)bytes length:(size_t)length
{
	[super appendBytes:bytes length:length];
	if (length > 0) {
		_contentDiscarded = NO;
	}
}

@end
