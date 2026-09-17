/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * nenumerator.m — the cursor.
 *
 * ARC file: it owns no C storage and implements no -retain/-release.
 */

#import <foundation/NSEnumerator.h>
#import <foundation/NSArray.h>

@implementation NSEnumerator

- (id)initWithSequence:(NSArray *)sequence reverse:(BOOL)reverse
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	/* THE SNAPSHOT: -initWithArray: copies the element pointers and retains
	 * each one, so a later mutation of the original cannot disturb this walk. */
	_sequence = [[NSArray alloc] initWithArray:sequence];
	_index = 0;
	_reverse = reverse;
	return self;
}

- (id)nextObject
{
	unsigned long count = [_sequence count];

	if (_index >= count) {
		return nil;
	}
	{
		unsigned long position = _reverse ? (count - 1 - _index) : _index;

		_index++;
		return [_sequence objectAtIndex:position];
	}
}

- (NSArray *)allObjects
{
	NSMutableArray *remaining = [[NSMutableArray alloc] init];
	id object;

	/* Cocoa's contract: what is LEFT, and the cursor is spent afterwards. */
	while ((object = [self nextObject]) != nil) {
		[remaining addObject:object];
	}
	return remaining;
}

- (NSUInteger)countByEnumeratingWithState:(NSFastEnumerationState *)state
				  objects:(id __unsafe_unretained *)buffer
				    count:(NSUInteger)len
{
	unsigned long count = [_sequence count];
	unsigned long taken = 0;

	if (state->state >= count) {
		return 0;
	}
	state->itemsPtr = buffer;
	/* The snapshot never mutates, so the enumerator's OWN counter is the honest
	 * address to hand over: it is stable for the cursor's life. */
	state->mutationsPtr = &_mutations;
	while (state->state < count && taken < len) {
		unsigned long position = _reverse ? (count - 1 - state->state) : state->state;

		buffer[taken++] = [_sequence objectAtIndex:position];
		state->state++;
	}
	/* Keep -nextObject and a for-in loop in step with each other. */
	_index = state->state;
	return taken;
}

@end
